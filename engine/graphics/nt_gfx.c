#include "graphics/nt_gfx_internal.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "core/nt_platform.h"

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

    nt_gfx_render_state_t render_state;
    uint32_t bound_pipeline; /* currently bound pipeline backend handle */
    bool has_index_buffer;   /* track if index buffer is bound for draw */
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
    pool->slots[slot_index].id = 0;
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

/* ---- Default pool sizes ---- */

#define NT_GFX_DEFAULT_SHADERS 32
#define NT_GFX_DEFAULT_PIPELINES 16
#define NT_GFX_DEFAULT_BUFFERS 128

/* ---- Lifecycle ---- */

void nt_gfx_init(const nt_gfx_desc_t *desc) {
    memset(&s_gfx, 0, sizeof(s_gfx));
    memset(&g_nt_gfx, 0, sizeof(g_nt_gfx));

    uint32_t max_shd = (desc && desc->max_shaders > 0) ? desc->max_shaders : NT_GFX_DEFAULT_SHADERS;
    uint32_t max_pip = (desc && desc->max_pipelines > 0) ? desc->max_pipelines : NT_GFX_DEFAULT_PIPELINES;
    uint32_t max_buf = (desc && desc->max_buffers > 0) ? desc->max_buffers : NT_GFX_DEFAULT_BUFFERS;

    nt_gfx_pool_init(&s_gfx.shader_pool, max_shd);
    nt_gfx_pool_init(&s_gfx.pipeline_pool, max_pip);
    nt_gfx_pool_init(&s_gfx.buffer_pool, max_buf);

    s_gfx.shader_backends = (uint32_t *)calloc(max_shd + 1, sizeof(uint32_t));
    s_gfx.pipeline_backends = (uint32_t *)calloc(max_pip + 1, sizeof(uint32_t));
    s_gfx.buffer_backends = (uint32_t *)calloc(max_buf + 1, sizeof(uint32_t));

    s_gfx.shader_descs = (nt_shader_desc_t *)calloc(max_shd + 1, sizeof(nt_shader_desc_t));
    s_gfx.pipeline_descs = (nt_pipeline_desc_t *)calloc(max_pip + 1, sizeof(nt_pipeline_desc_t));
    s_gfx.buffer_descs = (nt_buffer_desc_t *)calloc(max_buf + 1, sizeof(nt_buffer_desc_t));

    s_gfx.render_state = NT_GFX_STATE_IDLE;
    g_nt_gfx.initialized = true;

    nt_gfx_backend_init(desc);
}

void nt_gfx_shutdown(void) {
    nt_gfx_backend_shutdown();

    nt_gfx_pool_shutdown(&s_gfx.shader_pool);
    nt_gfx_pool_shutdown(&s_gfx.pipeline_pool);
    nt_gfx_pool_shutdown(&s_gfx.buffer_pool);

    free(s_gfx.shader_backends);
    free(s_gfx.pipeline_backends);
    free(s_gfx.buffer_backends);
    free(s_gfx.shader_descs);
    free(s_gfx.pipeline_descs);
    free(s_gfx.buffer_descs);

    memset(&s_gfx, 0, sizeof(s_gfx));
    memset(&g_nt_gfx, 0, sizeof(g_nt_gfx));
}

/* ---- Context restoration ---- */

static void restore_context(void) {
    g_nt_gfx.context_lost = false;
    g_nt_gfx.context_restored = true;

    nt_gfx_backend_recreate_all_resources();

    /* Recreate shaders first (pipelines reference shader backend handles) */
    for (uint32_t i = 1; i <= s_gfx.shader_pool.capacity; i++) {
        if (s_gfx.shader_pool.slots[i].id != 0) {
            s_gfx.shader_backends[i] = nt_gfx_backend_create_shader(&s_gfx.shader_descs[i]);
        }
    }

    /* Recreate buffers */
    for (uint32_t i = 1; i <= s_gfx.buffer_pool.capacity; i++) {
        if (s_gfx.buffer_pool.slots[i].id != 0) {
            s_gfx.buffer_backends[i] = nt_gfx_backend_create_buffer(&s_gfx.buffer_descs[i]);
        }
    }

    /* Recreate pipelines (need shader backend handles) */
    for (uint32_t i = 1; i <= s_gfx.pipeline_pool.capacity; i++) {
        if (s_gfx.pipeline_pool.slots[i].id != 0) {
            const nt_pipeline_desc_t *pdesc = &s_gfx.pipeline_descs[i];
            uint32_t vs_slot = nt_gfx_pool_slot_index(pdesc->vertex_shader.id);
            uint32_t fs_slot = nt_gfx_pool_slot_index(pdesc->fragment_shader.id);
            uint32_t vs_backend = s_gfx.shader_backends[vs_slot];
            uint32_t fs_backend = s_gfx.shader_backends[fs_slot];
            s_gfx.pipeline_backends[i] = nt_gfx_backend_create_pipeline(pdesc, vs_backend, fs_backend);
        }
    }
}

/* ---- Frame / Pass ---- */

void nt_gfx_begin_frame(void) {
    if (nt_gfx_backend_is_context_lost()) {
        g_nt_gfx.context_lost = true;
        return;
    }

    if (g_nt_gfx.context_lost) {
        restore_context();
    }

#ifdef NT_ENABLE_ASSERTS
    assert(s_gfx.render_state == NT_GFX_STATE_IDLE);
#endif
    if (s_gfx.render_state != NT_GFX_STATE_IDLE) {
        return;
    }

    s_gfx.render_state = NT_GFX_STATE_FRAME;
    nt_gfx_backend_begin_frame();
}

void nt_gfx_end_frame(void) {
    if (g_nt_gfx.context_lost) {
        return;
    }

#ifdef NT_ENABLE_ASSERTS
    assert(s_gfx.render_state == NT_GFX_STATE_FRAME);
#endif
    if (s_gfx.render_state != NT_GFX_STATE_FRAME) {
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

#ifdef NT_ENABLE_ASSERTS
    assert(s_gfx.render_state == NT_GFX_STATE_FRAME);
#endif
    if (s_gfx.render_state != NT_GFX_STATE_FRAME) {
        return;
    }

    s_gfx.render_state = NT_GFX_STATE_PASS;
    nt_gfx_backend_begin_pass(desc);
}

void nt_gfx_end_pass(void) {
    if (g_nt_gfx.context_lost) {
        return;
    }

#ifdef NT_ENABLE_ASSERTS
    assert(s_gfx.render_state == NT_GFX_STATE_PASS);
#endif
    if (s_gfx.render_state != NT_GFX_STATE_PASS) {
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
        return result;
    }

    uint32_t backend = nt_gfx_backend_create_shader(desc);
    if (backend == 0) {
        nt_gfx_pool_free(&s_gfx.shader_pool, id);
        return result;
    }

    uint32_t slot = nt_gfx_pool_slot_index(id);
    s_gfx.shader_backends[slot] = backend;
    s_gfx.shader_descs[slot] = *desc;

    result.id = id;
    return result;
}

nt_pipeline_t nt_gfx_make_pipeline(const nt_pipeline_desc_t *desc) {
    nt_pipeline_t result = {0};
    if (!desc) {
        return result;
    }

    if (!nt_gfx_pool_valid(&s_gfx.shader_pool, desc->vertex_shader.id) || !nt_gfx_pool_valid(&s_gfx.shader_pool, desc->fragment_shader.id)) {
        return result;
    }

    uint32_t id = nt_gfx_pool_alloc(&s_gfx.pipeline_pool);
    if (id == 0) {
        return result;
    }

    uint32_t vs_slot = nt_gfx_pool_slot_index(desc->vertex_shader.id);
    uint32_t fs_slot = nt_gfx_pool_slot_index(desc->fragment_shader.id);
    uint32_t vs_backend = s_gfx.shader_backends[vs_slot];
    uint32_t fs_backend = s_gfx.shader_backends[fs_slot];

    uint32_t backend = nt_gfx_backend_create_pipeline(desc, vs_backend, fs_backend);
    if (backend == 0) {
        nt_gfx_pool_free(&s_gfx.pipeline_pool, id);
        return result;
    }

    uint32_t slot = nt_gfx_pool_slot_index(id);
    s_gfx.pipeline_backends[slot] = backend;
    s_gfx.pipeline_descs[slot] = *desc;

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
        return result;
    }

    uint32_t backend = nt_gfx_backend_create_buffer(desc);
    if (backend == 0) {
        nt_gfx_pool_free(&s_gfx.buffer_pool, id);
        return result;
    }

    uint32_t slot = nt_gfx_pool_slot_index(id);
    s_gfx.buffer_backends[slot] = backend;
    s_gfx.buffer_descs[slot] = *desc;

    result.id = id;
    return result;
}

/* ---- Resource destruction ---- */

void nt_gfx_destroy_shader(nt_shader_t shd) {
    if (!nt_gfx_pool_valid(&s_gfx.shader_pool, shd.id)) {
        return;
    }
    uint32_t slot = nt_gfx_pool_slot_index(shd.id);
    nt_gfx_backend_destroy_shader(s_gfx.shader_backends[slot]);
    s_gfx.shader_backends[slot] = 0;
    memset(&s_gfx.shader_descs[slot], 0, sizeof(nt_shader_desc_t));
    nt_gfx_pool_free(&s_gfx.shader_pool, shd.id);
}

void nt_gfx_destroy_pipeline(nt_pipeline_t pip) {
    if (!nt_gfx_pool_valid(&s_gfx.pipeline_pool, pip.id)) {
        return;
    }
    uint32_t slot = nt_gfx_pool_slot_index(pip.id);
    nt_gfx_backend_destroy_pipeline(s_gfx.pipeline_backends[slot]);
    s_gfx.pipeline_backends[slot] = 0;
    memset(&s_gfx.pipeline_descs[slot], 0, sizeof(nt_pipeline_desc_t));
    nt_gfx_pool_free(&s_gfx.pipeline_pool, pip.id);
}

void nt_gfx_destroy_buffer(nt_buffer_t buf) {
    if (!nt_gfx_pool_valid(&s_gfx.buffer_pool, buf.id)) {
        return;
    }
    uint32_t slot = nt_gfx_pool_slot_index(buf.id);
    nt_gfx_backend_destroy_buffer(s_gfx.buffer_backends[slot]);
    s_gfx.buffer_backends[slot] = 0;
    memset(&s_gfx.buffer_descs[slot], 0, sizeof(nt_buffer_desc_t));
    nt_gfx_pool_free(&s_gfx.buffer_pool, buf.id);
}

/* ---- Draw state ---- */

void nt_gfx_bind_pipeline(nt_pipeline_t pip) {
    if (g_nt_gfx.context_lost) {
        return;
    }
    if (!nt_gfx_pool_valid(&s_gfx.pipeline_pool, pip.id)) {
        return;
    }
    uint32_t slot = nt_gfx_pool_slot_index(pip.id);
    s_gfx.bound_pipeline = s_gfx.pipeline_backends[slot];
    s_gfx.has_index_buffer = false;
    nt_gfx_backend_bind_pipeline(s_gfx.bound_pipeline);
}

void nt_gfx_bind_vertex_buffer(nt_buffer_t buf) {
    if (g_nt_gfx.context_lost) {
        return;
    }
    if (!nt_gfx_pool_valid(&s_gfx.buffer_pool, buf.id)) {
        return;
    }
    uint32_t slot = nt_gfx_pool_slot_index(buf.id);
    nt_gfx_backend_bind_vertex_buffer(s_gfx.buffer_backends[slot]);
}

void nt_gfx_bind_index_buffer(nt_buffer_t buf) {
    if (g_nt_gfx.context_lost) {
        return;
    }
    if (!nt_gfx_pool_valid(&s_gfx.buffer_pool, buf.id)) {
        return;
    }
    uint32_t slot = nt_gfx_pool_slot_index(buf.id);
    s_gfx.has_index_buffer = true;
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

/* ---- Draw call ---- */

void nt_gfx_draw(uint32_t first_element, uint32_t num_elements) {
    if (g_nt_gfx.context_lost) {
        return;
    }

#ifdef NT_ENABLE_ASSERTS
    assert(s_gfx.render_state == NT_GFX_STATE_PASS);
#endif
    if (s_gfx.render_state != NT_GFX_STATE_PASS) {
        return;
    }

    nt_gfx_backend_draw(first_element, num_elements, s_gfx.has_index_buffer);
}

/* ---- Buffer update ---- */

void nt_gfx_update_buffer(nt_buffer_t buf, const void *data, uint32_t size) {
    if (g_nt_gfx.context_lost) {
        return;
    }
    if (!nt_gfx_pool_valid(&s_gfx.buffer_pool, buf.id)) {
        return;
    }
    uint32_t slot = nt_gfx_pool_slot_index(buf.id);
    nt_gfx_backend_update_buffer(s_gfx.buffer_backends[slot], data, size);
}
