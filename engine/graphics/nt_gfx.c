#include "graphics/nt_gfx_internal.h"

#include <stdlib.h>
#include <string.h>

#include "core/nt_assert.h"
#include "log/nt_log.h"

/* ---- Descriptor ownership helpers ----
   Shader source/label and immutable buffer data are strdup/memcpy'd so
   context-loss recreation doesn't chase dangling pointers.
   TODO(assets): remove copies once shaders/buffers come from asset packs
   that outlive the resource. */

static char *nt_gfx_strdup(const char *s) {
    if (!s) {
        return NULL;
    }
    size_t len = strlen(s) + 1;
    char *copy = (char *)malloc(len);
    if (copy) {
        memcpy(copy, s, len);
    }
    return copy;
}

static void nt_gfx_free_shader_desc(nt_shader_desc_t *d) {
    free((void *)d->source);
    free((void *)d->label);
    memset(d, 0, sizeof(*d));
}

static void nt_gfx_free_buffer_desc(nt_buffer_desc_t *d) {
    free((void *)d->data);
    free((void *)d->label);
    memset(d, 0, sizeof(*d));
}

/* Pipeline-owned shader source copies.
   Pipelines keep their own vs/fs source so context-loss recovery
   works even if the original shader was destroyed. */
typedef struct {
    char *vs_source;
    char *fs_source;
} nt_gfx_pipeline_sources_t;

static void nt_gfx_free_pipeline_data(uint32_t slot);

/* ---- Global state ---- */

nt_gfx_t g_nt_gfx;

/* ---- File-scope internal state ---- */

static struct {
    nt_gfx_pool_t shader_pool;
    nt_gfx_pool_t pipeline_pool;
    nt_gfx_pool_t buffer_pool;

    uint32_t *shader_backends; /* backend handles parallel to pool slots */
    uint32_t *pipeline_backends;
    uint32_t *buffer_backends;

    nt_shader_desc_t *shader_descs; /* stored descs for context loss recreation */
    nt_pipeline_desc_t *pipeline_descs;
    nt_buffer_desc_t *buffer_descs;
    nt_gfx_pipeline_sources_t *pipeline_sources; /* pipeline-owned shader source copies */

    nt_gfx_render_state_t render_state;
    uint32_t bound_pipeline; /* currently bound pipeline backend handle */
} s_gfx;

/* ---- Pool helpers ---- */

void nt_gfx_pool_init(nt_gfx_pool_t *pool, uint32_t capacity) {
    pool->capacity = capacity;
    pool->slots = (nt_gfx_slot_t *)calloc(capacity + 1, sizeof(nt_gfx_slot_t));
    pool->free_queue = (uint32_t *)malloc(capacity * sizeof(uint32_t));
    pool->queue_top = capacity;

    /* Fill free queue with indices 1..capacity (skip 0, reserved invalid) */
    for (uint32_t i = 0; i < capacity; i++) {
        pool->free_queue[i] = capacity - i; /* stack: top has lowest index */
    }
}

void nt_gfx_pool_shutdown(nt_gfx_pool_t *pool) {
    free(pool->slots);
    free(pool->free_queue);
    pool->slots = NULL;
    pool->free_queue = NULL;
    pool->capacity = 0;
    pool->queue_top = 0;
}

uint32_t nt_gfx_pool_alloc(nt_gfx_pool_t *pool) {
    if (pool->queue_top == 0) {
        return 0; /* pool full */
    }
    pool->queue_top--;
    uint32_t slot_index = pool->free_queue[pool->queue_top];

    /* Increment generation (starts at 1 for first allocation) */
    uint32_t prev_id = pool->slots[slot_index].id;
    uint32_t prev_gen = prev_id >> NT_GFX_SLOT_SHIFT;
    uint32_t new_gen = prev_gen + 1;

    uint32_t id = (new_gen << NT_GFX_SLOT_SHIFT) | slot_index;
    pool->slots[slot_index].id = id;
    return id;
}

void nt_gfx_pool_free(nt_gfx_pool_t *pool, uint32_t id) {
    uint32_t slot_index = id & NT_GFX_SLOT_MASK;
    if (slot_index == 0 || slot_index > pool->capacity) {
        return;
    }
    if (pool->slots[slot_index].id != id) {
        return; /* stale handle */
    }
    /* Increment generation so old handles are invalid. Keep generation
       in slot so the next alloc produces a higher generation. Clear the
       slot index bits so valid() rejects the freed id. */
    uint32_t gen = (id >> NT_GFX_SLOT_SHIFT) + 1;
    pool->slots[slot_index].id = gen << NT_GFX_SLOT_SHIFT;
    pool->free_queue[pool->queue_top] = slot_index;
    pool->queue_top++;
}

bool nt_gfx_pool_valid(const nt_gfx_pool_t *pool, uint32_t id) {
    if (id == 0) {
        return false;
    }
    uint32_t slot_index = id & NT_GFX_SLOT_MASK;
    if (slot_index == 0 || slot_index > pool->capacity) {
        return false;
    }
    return pool->slots[slot_index].id == id;
}

uint32_t nt_gfx_pool_slot_index(uint32_t id) { return id & NT_GFX_SLOT_MASK; }

/* ---- Pipeline data cleanup ---- */

static void nt_gfx_free_pipeline_data(uint32_t slot) {
    free((void *)s_gfx.pipeline_descs[slot].label);
    free(s_gfx.pipeline_sources[slot].vs_source);
    free(s_gfx.pipeline_sources[slot].fs_source);
    memset(&s_gfx.pipeline_descs[slot], 0, sizeof(nt_pipeline_desc_t));
    memset(&s_gfx.pipeline_sources[slot], 0, sizeof(nt_gfx_pipeline_sources_t));
}

/* ---- Lifecycle ---- */

void nt_gfx_init(const nt_gfx_desc_t *desc) {
    NT_ASSERT(desc);
    memset(&s_gfx, 0, sizeof(s_gfx));
    memset(&g_nt_gfx, 0, sizeof(g_nt_gfx));

    nt_gfx_pool_init(&s_gfx.shader_pool, desc->max_shaders);
    nt_gfx_pool_init(&s_gfx.pipeline_pool, desc->max_pipelines);
    nt_gfx_pool_init(&s_gfx.buffer_pool, desc->max_buffers);

    s_gfx.shader_backends = (uint32_t *)calloc(desc->max_shaders + 1, sizeof(uint32_t));
    s_gfx.pipeline_backends = (uint32_t *)calloc(desc->max_pipelines + 1, sizeof(uint32_t));
    s_gfx.buffer_backends = (uint32_t *)calloc(desc->max_buffers + 1, sizeof(uint32_t));

    s_gfx.shader_descs = (nt_shader_desc_t *)calloc(desc->max_shaders + 1, sizeof(nt_shader_desc_t));
    s_gfx.pipeline_descs = (nt_pipeline_desc_t *)calloc(desc->max_pipelines + 1, sizeof(nt_pipeline_desc_t));
    s_gfx.buffer_descs = (nt_buffer_desc_t *)calloc(desc->max_buffers + 1, sizeof(nt_buffer_desc_t));
    s_gfx.pipeline_sources = (nt_gfx_pipeline_sources_t *)calloc(desc->max_pipelines + 1, sizeof(nt_gfx_pipeline_sources_t));

    s_gfx.render_state = NT_GFX_STATE_IDLE;

    if (!nt_gfx_backend_init(desc)) {
        nt_log_error("gfx: backend init failed");
        nt_gfx_shutdown();
        return;
    }

    g_nt_gfx.initialized = true;
}

void nt_gfx_shutdown(void) {
    nt_gfx_backend_shutdown();

    /* Free owned strings/data in stored descriptors */
    for (uint32_t i = 1; i <= s_gfx.shader_pool.capacity; i++) {
        nt_gfx_free_shader_desc(&s_gfx.shader_descs[i]);
    }
    for (uint32_t i = 1; i <= s_gfx.pipeline_pool.capacity; i++) {
        nt_gfx_free_pipeline_data(i);
    }
    for (uint32_t i = 1; i <= s_gfx.buffer_pool.capacity; i++) {
        nt_gfx_free_buffer_desc(&s_gfx.buffer_descs[i]);
    }

    nt_gfx_pool_shutdown(&s_gfx.shader_pool);
    nt_gfx_pool_shutdown(&s_gfx.pipeline_pool);
    nt_gfx_pool_shutdown(&s_gfx.buffer_pool);

    free(s_gfx.shader_backends);
    free(s_gfx.pipeline_backends);
    free(s_gfx.buffer_backends);
    free(s_gfx.shader_descs);
    free(s_gfx.pipeline_descs);
    free(s_gfx.buffer_descs);
    free(s_gfx.pipeline_sources);

    memset(&s_gfx, 0, sizeof(s_gfx));
    memset(&g_nt_gfx, 0, sizeof(g_nt_gfx));
}

/* ---- Context restoration ---- */

/* TODO(assets): restore_context assumes synchronous recreation (all data in memory).
   When assets load from packs/network, this needs staged recovery across frames. */
static bool restore_context(void) {
    if (!nt_gfx_backend_recreate_all_resources()) {
        return false;
    }

    g_nt_gfx.context_lost = false;
    g_nt_gfx.context_restored = true;
    s_gfx.bound_pipeline = 0;

    /* A slot is live when its index bits match its position (freed slots have index bits zeroed) */
#define SLOT_IS_LIVE(pool, idx) (((pool).slots[(idx)].id & NT_GFX_SLOT_MASK) == (idx))

    /* Recreate standalone shaders (for future pipeline creation) */
    for (uint32_t i = 1; i <= s_gfx.shader_pool.capacity; i++) {
        if (SLOT_IS_LIVE(s_gfx.shader_pool, i)) {
            s_gfx.shader_backends[i] = nt_gfx_backend_create_shader(&s_gfx.shader_descs[i]);
        }
    }

    /* Recreate buffers */
    for (uint32_t i = 1; i <= s_gfx.buffer_pool.capacity; i++) {
        if (SLOT_IS_LIVE(s_gfx.buffer_pool, i)) {
            s_gfx.buffer_backends[i] = nt_gfx_backend_create_buffer(&s_gfx.buffer_descs[i]);
        }
    }

    /* Recreate pipelines from pipeline-owned shader sources.
       This works even if the original shaders were destroyed. */
    for (uint32_t i = 1; i <= s_gfx.pipeline_pool.capacity; i++) {
        if (SLOT_IS_LIVE(s_gfx.pipeline_pool, i)) {
            const nt_pipeline_desc_t *pdesc = &s_gfx.pipeline_descs[i];
            nt_shader_desc_t vs_desc = {.type = NT_SHADER_VERTEX, .source = s_gfx.pipeline_sources[i].vs_source};
            nt_shader_desc_t fs_desc = {.type = NT_SHADER_FRAGMENT, .source = s_gfx.pipeline_sources[i].fs_source};
            uint32_t vs_backend = nt_gfx_backend_create_shader(&vs_desc);
            uint32_t fs_backend = nt_gfx_backend_create_shader(&fs_desc);
            s_gfx.pipeline_backends[i] = nt_gfx_backend_create_pipeline(pdesc, vs_backend, fs_backend);
            /* Shader objects can be deleted after linking — GL keeps them alive through the program */
            nt_gfx_backend_destroy_shader(vs_backend);
            nt_gfx_backend_destroy_shader(fs_backend);
        }
    }

#undef SLOT_IS_LIVE
    return true;
}

/* ---- Frame / Pass ---- */

void nt_gfx_begin_frame(void) {
    if (nt_gfx_backend_is_context_lost()) {
        g_nt_gfx.context_lost = true;
    }

    if (g_nt_gfx.context_lost) {
        if (!restore_context()) {
            return; /* Context still unavailable — skip frame */
        }
    }

    NT_ASSERT(s_gfx.render_state == NT_GFX_STATE_IDLE);
    if (s_gfx.render_state != NT_GFX_STATE_IDLE) {
        nt_log_error("gfx: begin_frame called outside IDLE state");
        return;
    }

    s_gfx.render_state = NT_GFX_STATE_FRAME;
    memset(&g_nt_gfx.frame_stats, 0, sizeof(g_nt_gfx.frame_stats));
    nt_gfx_backend_begin_frame();
}

void nt_gfx_end_frame(void) {
    if (g_nt_gfx.context_lost) {
        return;
    }

    NT_ASSERT(s_gfx.render_state == NT_GFX_STATE_FRAME);
    if (s_gfx.render_state != NT_GFX_STATE_FRAME) {
        nt_log_error("gfx: end_frame called outside FRAME state");
        return;
    }

    s_gfx.render_state = NT_GFX_STATE_IDLE;
    nt_gfx_backend_end_frame();
    g_nt_gfx.context_restored = false;
}

void nt_gfx_begin_pass(const nt_pass_desc_t *desc) {
    if (g_nt_gfx.context_lost) {
        return;
    }

    NT_ASSERT(s_gfx.render_state == NT_GFX_STATE_FRAME);
    if (s_gfx.render_state != NT_GFX_STATE_FRAME) {
        nt_log_error("gfx: begin_pass called outside FRAME state");
        return;
    }

    s_gfx.render_state = NT_GFX_STATE_PASS;
    nt_gfx_backend_begin_pass(desc);
}

void nt_gfx_end_pass(void) {
    if (g_nt_gfx.context_lost) {
        return;
    }

    NT_ASSERT(s_gfx.render_state == NT_GFX_STATE_PASS);
    if (s_gfx.render_state != NT_GFX_STATE_PASS) {
        nt_log_error("gfx: end_pass called outside PASS state");
        return;
    }

    s_gfx.render_state = NT_GFX_STATE_FRAME;
    nt_gfx_backend_end_pass();
}

/* ---- Resource creation ---- */

nt_shader_t nt_gfx_make_shader(const nt_shader_desc_t *desc) {
    nt_shader_t result = {0};
    if (!desc) {
        return result;
    }

    uint32_t id = nt_gfx_pool_alloc(&s_gfx.shader_pool);
    if (id == 0) {
        nt_log_error("gfx: shader pool full");
        return result;
    }

    uint32_t backend = nt_gfx_backend_create_shader(desc);
    if (backend == 0) {
        nt_log_error("gfx: backend shader creation failed");
        nt_gfx_pool_free(&s_gfx.shader_pool, id);
        return result;
    }

    uint32_t slot = nt_gfx_pool_slot_index(id);
    s_gfx.shader_backends[slot] = backend;
    s_gfx.shader_descs[slot] = *desc;
    s_gfx.shader_descs[slot].source = nt_gfx_strdup(desc->source);
    s_gfx.shader_descs[slot].label = nt_gfx_strdup(desc->label);

    result.id = id;
    return result;
}

nt_pipeline_t nt_gfx_make_pipeline(const nt_pipeline_desc_t *desc) {
    nt_pipeline_t result = {0};
    if (!desc) {
        return result;
    }

    if (!nt_gfx_pool_valid(&s_gfx.shader_pool, desc->vertex_shader.id) || !nt_gfx_pool_valid(&s_gfx.shader_pool, desc->fragment_shader.id)) {
        nt_log_error("gfx: pipeline creation failed: invalid shader handle");
        return result;
    }
    if (desc->layout.attr_count > NT_GFX_MAX_VERTEX_ATTRS) {
        nt_log_error("gfx: pipeline creation failed: too many vertex attrs");
        return result;
    }

    uint32_t id = nt_gfx_pool_alloc(&s_gfx.pipeline_pool);
    if (id == 0) {
        nt_log_error("gfx: pipeline pool full");
        return result;
    }

    uint32_t vs_slot = nt_gfx_pool_slot_index(desc->vertex_shader.id);
    uint32_t fs_slot = nt_gfx_pool_slot_index(desc->fragment_shader.id);
    uint32_t vs_backend = s_gfx.shader_backends[vs_slot];
    uint32_t fs_backend = s_gfx.shader_backends[fs_slot];

    uint32_t backend = nt_gfx_backend_create_pipeline(desc, vs_backend, fs_backend);
    if (backend == 0) {
        nt_log_error("gfx: backend pipeline creation failed");
        nt_gfx_pool_free(&s_gfx.pipeline_pool, id);
        return result;
    }

    uint32_t slot = nt_gfx_pool_slot_index(id);
    s_gfx.pipeline_backends[slot] = backend;
    s_gfx.pipeline_descs[slot] = *desc;
    s_gfx.pipeline_descs[slot].label = nt_gfx_strdup(desc->label);

    /* Copy shader sources so pipeline can recover independently of shader lifetime */
    s_gfx.pipeline_sources[slot].vs_source = nt_gfx_strdup(s_gfx.shader_descs[vs_slot].source);
    s_gfx.pipeline_sources[slot].fs_source = nt_gfx_strdup(s_gfx.shader_descs[fs_slot].source);

    result.id = id;
    return result;
}

nt_buffer_t nt_gfx_make_buffer(const nt_buffer_desc_t *desc) {
    nt_buffer_t result = {0};
    if (!desc) {
        return result;
    }

    uint32_t id = nt_gfx_pool_alloc(&s_gfx.buffer_pool);
    if (id == 0) {
        nt_log_error("gfx: buffer pool full");
        return result;
    }

    uint32_t backend = nt_gfx_backend_create_buffer(desc);
    if (backend == 0) {
        nt_log_error("gfx: backend buffer creation failed");
        nt_gfx_pool_free(&s_gfx.buffer_pool, id);
        return result;
    }

    uint32_t slot = nt_gfx_pool_slot_index(id);
    s_gfx.buffer_backends[slot] = backend;
    s_gfx.buffer_descs[slot] = *desc;
    s_gfx.buffer_descs[slot].label = nt_gfx_strdup(desc->label);
    /* Immutable buffers: copy data for context-loss recreation.
       Dynamic/stream: game refills every frame, recreate empty. */
    if (desc->usage == NT_USAGE_IMMUTABLE && desc->data && desc->size > 0) {
        void *copy = malloc(desc->size);
        if (copy) {
            memcpy(copy, desc->data, desc->size);
        }
        s_gfx.buffer_descs[slot].data = copy;
    } else {
        s_gfx.buffer_descs[slot].data = NULL;
    }

    result.id = id;
    return result;
}

/* ---- Resource destruction ---- */

void nt_gfx_destroy_shader(nt_shader_t shd) {
    if (!nt_gfx_pool_valid(&s_gfx.shader_pool, shd.id)) {
        nt_log_error("gfx: destroy_shader: invalid handle");
        return;
    }
    uint32_t slot = nt_gfx_pool_slot_index(shd.id);
    nt_gfx_backend_destroy_shader(s_gfx.shader_backends[slot]);
    s_gfx.shader_backends[slot] = 0;
    nt_gfx_free_shader_desc(&s_gfx.shader_descs[slot]);
    nt_gfx_pool_free(&s_gfx.shader_pool, shd.id);
}

void nt_gfx_destroy_pipeline(nt_pipeline_t pip) {
    if (!nt_gfx_pool_valid(&s_gfx.pipeline_pool, pip.id)) {
        nt_log_error("gfx: destroy_pipeline: invalid handle");
        return;
    }
    uint32_t slot = nt_gfx_pool_slot_index(pip.id);
    if (s_gfx.bound_pipeline == s_gfx.pipeline_backends[slot]) {
        s_gfx.bound_pipeline = 0;
    }
    nt_gfx_backend_destroy_pipeline(s_gfx.pipeline_backends[slot]);
    s_gfx.pipeline_backends[slot] = 0;
    nt_gfx_free_pipeline_data(slot);
    nt_gfx_pool_free(&s_gfx.pipeline_pool, pip.id);
}

void nt_gfx_destroy_buffer(nt_buffer_t buf) {
    if (!nt_gfx_pool_valid(&s_gfx.buffer_pool, buf.id)) {
        nt_log_error("gfx: destroy_buffer: invalid handle");
        return;
    }
    uint32_t slot = nt_gfx_pool_slot_index(buf.id);
    nt_gfx_backend_destroy_buffer(s_gfx.buffer_backends[slot]);
    s_gfx.buffer_backends[slot] = 0;
    nt_gfx_free_buffer_desc(&s_gfx.buffer_descs[slot]);
    nt_gfx_pool_free(&s_gfx.buffer_pool, buf.id);
}

/* ---- Draw state ---- */

void nt_gfx_bind_pipeline(nt_pipeline_t pip) {
    if (g_nt_gfx.context_lost) {
        return;
    }
    if (!nt_gfx_pool_valid(&s_gfx.pipeline_pool, pip.id)) {
        nt_log_error("gfx: bind_pipeline: invalid handle");
        return;
    }
    uint32_t slot = nt_gfx_pool_slot_index(pip.id);
    s_gfx.bound_pipeline = s_gfx.pipeline_backends[slot];
    nt_gfx_backend_bind_pipeline(s_gfx.bound_pipeline);
}

void nt_gfx_bind_vertex_buffer(nt_buffer_t buf) {
    if (g_nt_gfx.context_lost) {
        return;
    }
    if (!nt_gfx_pool_valid(&s_gfx.buffer_pool, buf.id)) {
        nt_log_error("gfx: bind_vertex_buffer: invalid handle");
        return;
    }
    uint32_t slot = nt_gfx_pool_slot_index(buf.id);
    NT_ASSERT(s_gfx.buffer_descs[slot].type == NT_BUFFER_VERTEX);
    if (s_gfx.buffer_descs[slot].type != NT_BUFFER_VERTEX) {
        nt_log_error("gfx: bind_vertex_buffer: buffer is not vertex type");
        return;
    }
    nt_gfx_backend_bind_vertex_buffer(s_gfx.buffer_backends[slot]);
}

void nt_gfx_bind_index_buffer(nt_buffer_t buf) {
    if (g_nt_gfx.context_lost) {
        return;
    }
    if (!nt_gfx_pool_valid(&s_gfx.buffer_pool, buf.id)) {
        nt_log_error("gfx: bind_index_buffer: invalid handle");
        return;
    }
    uint32_t slot = nt_gfx_pool_slot_index(buf.id);
    NT_ASSERT(s_gfx.buffer_descs[slot].type == NT_BUFFER_INDEX);
    if (s_gfx.buffer_descs[slot].type != NT_BUFFER_INDEX) {
        nt_log_error("gfx: bind_index_buffer: buffer is not index type");
        return;
    }
    nt_gfx_backend_bind_index_buffer(s_gfx.buffer_backends[slot]);
}

/* ---- Uniforms ---- */

void nt_gfx_set_uniform_mat4(const char *name, const float *matrix) {
    if (g_nt_gfx.context_lost) {
        return;
    }
    nt_gfx_backend_set_uniform_mat4(name, matrix);
}

void nt_gfx_set_uniform_vec4(const char *name, const float *vec) {
    if (g_nt_gfx.context_lost) {
        return;
    }
    nt_gfx_backend_set_uniform_vec4(name, vec);
}

void nt_gfx_set_uniform_float(const char *name, float val) {
    if (g_nt_gfx.context_lost) {
        return;
    }
    nt_gfx_backend_set_uniform_float(name, val);
}

void nt_gfx_set_uniform_int(const char *name, int val) {
    if (g_nt_gfx.context_lost) {
        return;
    }
    nt_gfx_backend_set_uniform_int(name, val);
}

/* ---- Draw calls ---- */

void nt_gfx_draw(uint32_t first_vertex, uint32_t num_vertices) {
    if (g_nt_gfx.context_lost) {
        return;
    }

    NT_ASSERT(s_gfx.render_state == NT_GFX_STATE_PASS);
    if (s_gfx.render_state != NT_GFX_STATE_PASS) {
        nt_log_error("gfx: draw called outside PASS state");
        return;
    }
    NT_ASSERT(s_gfx.bound_pipeline != 0);
    if (s_gfx.bound_pipeline == 0) {
        nt_log_error("gfx: draw called without bound pipeline");
        return;
    }

    g_nt_gfx.frame_stats.draw_calls++;
    g_nt_gfx.frame_stats.vertices += num_vertices;
    nt_gfx_backend_draw(first_vertex, num_vertices, false);
}

void nt_gfx_draw_indexed(uint32_t first_index, uint32_t num_indices) {
    if (g_nt_gfx.context_lost) {
        return;
    }

    NT_ASSERT(s_gfx.render_state == NT_GFX_STATE_PASS);
    if (s_gfx.render_state != NT_GFX_STATE_PASS) {
        nt_log_error("gfx: draw_indexed called outside PASS state");
        return;
    }
    NT_ASSERT(s_gfx.bound_pipeline != 0);
    if (s_gfx.bound_pipeline == 0) {
        nt_log_error("gfx: draw_indexed called without bound pipeline");
        return;
    }

    g_nt_gfx.frame_stats.draw_calls++;
    g_nt_gfx.frame_stats.indices += num_indices;
    nt_gfx_backend_draw(first_index, num_indices, true);
}

void nt_gfx_draw_indexed_instanced(uint32_t first_index, uint32_t num_indices, uint32_t instance_count) {
    if (g_nt_gfx.context_lost) {
        return;
    }

    NT_ASSERT(s_gfx.render_state == NT_GFX_STATE_PASS);
    if (s_gfx.render_state != NT_GFX_STATE_PASS) {
        nt_log_error("gfx: draw_indexed_instanced called outside PASS state");
        return;
    }
    NT_ASSERT(s_gfx.bound_pipeline != 0);
    if (s_gfx.bound_pipeline == 0) {
        nt_log_error("gfx: draw_indexed_instanced called without bound pipeline");
        return;
    }

    g_nt_gfx.frame_stats.draw_calls++;
    g_nt_gfx.frame_stats.indices += num_indices * instance_count;
    g_nt_gfx.frame_stats.instances += instance_count;
    nt_gfx_backend_draw_instanced(first_index, num_indices, true, instance_count);
}

/* ---- Instance buffer ---- */

void nt_gfx_bind_instance_buffer(nt_buffer_t buf) {
    if (g_nt_gfx.context_lost) {
        return;
    }
    if (!nt_gfx_pool_valid(&s_gfx.buffer_pool, buf.id)) {
        nt_log_error("gfx: bind_instance_buffer: invalid handle");
        return;
    }
    uint32_t slot = nt_gfx_pool_slot_index(buf.id);
    NT_ASSERT(s_gfx.buffer_descs[slot].type == NT_BUFFER_VERTEX);
    if (s_gfx.buffer_descs[slot].type != NT_BUFFER_VERTEX) {
        nt_log_error("gfx: bind_instance_buffer: buffer is not vertex type");
        return;
    }
    nt_gfx_backend_bind_instance_buffer(s_gfx.buffer_backends[slot]);
}

/* ---- Buffer update ---- */

void nt_gfx_update_buffer(nt_buffer_t buf, const void *data, uint32_t size) {
    if (g_nt_gfx.context_lost) {
        return;
    }
    if (!nt_gfx_pool_valid(&s_gfx.buffer_pool, buf.id)) {
        nt_log_error("gfx: update_buffer: invalid handle");
        return;
    }
    uint32_t slot = nt_gfx_pool_slot_index(buf.id);
    NT_ASSERT(s_gfx.buffer_descs[slot].usage != NT_USAGE_IMMUTABLE);
    if (s_gfx.buffer_descs[slot].usage == NT_USAGE_IMMUTABLE) {
        nt_log_error("gfx: update_buffer: cannot update immutable buffer");
        return;
    }
    NT_ASSERT(size <= s_gfx.buffer_descs[slot].size);
    if (size > s_gfx.buffer_descs[slot].size) {
        nt_log_error("gfx: update_buffer: size exceeds buffer capacity");
        return;
    }
    nt_gfx_backend_update_buffer(s_gfx.buffer_backends[slot], data, size);
}
