#include "renderers/nt_sprite_renderer.h"

#include "core/nt_builtins.h"
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef __wasm_simd128__
#include <wasm_simd128.h>
#endif

#include "atlas/nt_atlas.h"
#include "comp_storage/nt_comp_storage.h"
#include "core/nt_assert.h"
#include "drawable_comp/nt_drawable_comp.h"
#include "graphics/nt_gfx.h"
#include "log/nt_log.h"
#include "material/nt_material.h"
#include "material_comp/nt_material_comp.h"
#include "render/nt_render_defs.h"
#include "renderers/nt_renderer_shared.h"
#include "sprite_comp/nt_sprite_comp.h"
#include "transform_comp/nt_transform_comp.h"

/* Base 20 B sprite vertex stride — custom attrs append after this offset. */
#define NT_SPRITE_BASE_STRIDE 20

// #region module state
typedef struct {
    nt_pipeline_t pipeline;
    /* Captured independently because custom layouts may share a pipeline. */
    nt_vertex_input_t vertex_input;
    nt_material_t material; /* handle for material-param lookup at flush; param values
                               are NOT snapshotted — material info is stable within a
                               frame (nt_material_step ran before render) so we
                               re-fetch via nt_material_get_info at flush time */
    uint32_t resolved_tex[NT_MATERIAL_MAX_TEXTURES];
    uint32_t tex_name_hashes[NT_MATERIAL_MAX_TEXTURES];
    nt_sampler_t resolved_sampler[NT_MATERIAL_MAX_TEXTURES]; /* per-binding override, .id==0 keeps texture default */
    uint8_t tex_count;
    uint32_t first_index; /* offset into s_sprite.indices[] */
    uint32_t index_count;
    uint32_t first_vertex; /* vertex index into staging — for per-cmd vertex stats */
} nt_sprite_draw_cmd_t;

static struct {
    bool initialized;
    uint16_t max_pipelines;
    nt_renderer_pipeline_entry_t entries[NT_SPRITE_RENDERER_MAX_PIPELINES_HARDCAP];
    uint16_t count;

    /* Layout-hash cache over the shared VBO/IBO; custom layouts may share a
     * pipeline. The pipeline hardcap is reused only as a capacity choice. */
    struct {
        uint64_t key;
        nt_vertex_input_t vi;
    } vi_entries[NT_SPRITE_RENDERER_MAX_PIPELINES_HARDCAP];
    uint16_t vi_count;

    nt_buffer_t vbo; /* dynamic, sized for the worst single flush (staging_size) */
    nt_buffer_t ibo; /* dynamic, sized for max_indices * 2 (uint16 indices) */
    /* One variable-stride CPU staging buffer: vertex i at staging + i*cur_stride,
     * custom block at +20. Sized for the worst single flush:
     * max(max_vertices*20, custom_max_vertices*(20+NT_SPRITE_CUSTOM_STRIDE_MAX)). */
    uint8_t *staging;
    uint16_t *indices;
    uint32_t staging_size; /* byte capacity of staging (worst single flush) */
    uint32_t max_vertices; /* runtime CPU staging caps (from desc) */
    uint32_t max_indices;
    uint32_t custom_max_vertices; /* custom flush caps here (custom verts are bigger) */
    uint32_t vertex_count;
    uint32_t index_count;
    /* Byte stride of the current batch = 20 + cur_material_custom_bytes. Set per-flush
     * in open_cmd, so the plain path stays a constant 20. */
    uint32_t cur_stride;
    /* "Current" per-widget custom attr block (set via set_custom_attrs, baked
     * into every emitted vertex like color). cur_custom_bytes==0 → plain emit. */
    uint8_t cur_custom_attrs[NT_SPRITE_CUSTOM_STRIDE_MAX];
    uint8_t cur_custom_bytes;
    /* Custom byte count the bound material's attr_map declares (attr_map_count *
     * 16, one FLOAT4 per attr — matches build_sprite_layout). bake asserts the
     * caller's cur_custom_bytes matches, catching a forgotten/mismatched
     * set_custom_attrs. 0 for a plain material. */
    uint8_t cur_material_custom_bytes;

    /* Recorded per-state draw commands. Last entry is the "currently open"
     * cmd that emit_one writes into; closed by close_current_cmd() before a
     * new state is pushed or before flush(). */
    nt_sprite_draw_cmd_t cmds[NT_SPRITE_RENDERER_MAX_DRAW_CMDS];
    uint32_t cmd_count;

    /* Reset per draw_list call; SEPARATE from nt_gfx_get_frame_draw_calls. */
    uint32_t last_draw_list_calls;

    /* Material of the most recently opened cmd; reset on flush. */
    nt_material_t current_mat;
    /* One-shot so a load-time skip does not spam; re-armed when a pipeline is
     * built, i.e. when something became drawable again. */
    bool warned_program_not_ready;
    /* The open cmd's pipeline was built on this program. A flat replace keeps the
     * material handle, so the fence below must compare programs too. */
    nt_program_t current_program;
#ifdef NT_TEST_ACCESS
    /* Captured pre-flush so tests can read back the last emit. */
    uint32_t last_emit_vertex_count;
    uint32_t last_emit_index_count;
    uint32_t last_emit_first_vertex;
    /* Captured at end of emit_slice9. */
    uint32_t last_slice9_vertex_count;
    uint32_t last_slice9_index_count;
    /* Flushes that ACTUALLY replayed cmds (past the empty early-return). Empty no-op flushes don't count. */
    uint32_t test_nonempty_flush_calls;
#endif
} s_sprite;
// #endregion

// #region lifecycle
static nt_result_t create_gpu_resources(void) {
    s_sprite.vbo = nt_gfx_make_buffer(&(nt_buffer_desc_t){
        .type = NT_BUFFER_VERTEX,
        .usage = NT_USAGE_DYNAMIC,
        .size = s_sprite.staging_size,
        .label = "sprite_vbo",
    });
    if (s_sprite.vbo.id == 0) {
        return NT_ERR_INIT_FAILED;
    }
    s_sprite.ibo = nt_gfx_make_buffer(&(nt_buffer_desc_t){
        .type = NT_BUFFER_INDEX,
        .usage = NT_USAGE_DYNAMIC,
        .size = s_sprite.max_indices * (uint32_t)sizeof(uint16_t),
        .index_type = NT_INDEX_UINT16,
        .label = "sprite_ibo",
    });
    if (s_sprite.ibo.id == 0) {
        nt_gfx_destroy_buffer(s_sprite.vbo);
        s_sprite.vbo = (nt_buffer_t){0};
        return NT_ERR_INIT_FAILED;
    }
    return NT_OK;
}

static void destroy_gpu_resources(void) {
    for (uint16_t i = 0; i < s_sprite.count; i++) {
        nt_gfx_destroy_pipeline(s_sprite.entries[i].pipeline);
    }
    s_sprite.count = 0;
    for (uint16_t i = 0; i < s_sprite.vi_count; i++) {
        nt_gfx_destroy_vertex_input(s_sprite.vi_entries[i].vi);
    }
    s_sprite.vi_count = 0;
    nt_gfx_destroy_buffer(s_sprite.vbo);
    nt_gfx_destroy_buffer(s_sprite.ibo);
    s_sprite.vbo = (nt_buffer_t){0};
    s_sprite.ibo = (nt_buffer_t){0};

    /* Queued commands reference the discarded GPU objects; never flush them. */
    s_sprite.cmd_count = 0;
    s_sprite.vertex_count = 0;
    s_sprite.index_count = 0;
    s_sprite.cur_stride = 0;
    s_sprite.cur_custom_bytes = 0;
    s_sprite.cur_material_custom_bytes = 0;
    s_sprite.current_mat = (nt_material_t){0};
    s_sprite.current_program = NT_PROGRAM_INVALID;
    s_sprite.last_draw_list_calls = 0;
    s_sprite.warned_program_not_ready = false;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
nt_result_t nt_sprite_renderer_init(const nt_sprite_renderer_desc_t *desc) {
    NT_ASSERT(!s_sprite.initialized);
    nt_sprite_renderer_desc_t d = (desc != NULL) ? *desc : nt_sprite_renderer_desc_defaults();
    /* 0 cap means "use compile-time default" — lets callers that only set
     * max_pipelines keep working without restating the staging caps. */
    if (d.max_vertices == 0) {
        d.max_vertices = NT_SPRITE_RENDERER_MAX_VERTICES;
    }
    if (d.max_indices == 0) {
        d.max_indices = NT_SPRITE_RENDERER_MAX_INDICES;
    }
    if (d.custom_max_vertices == 0) {
        d.custom_max_vertices = 4096;
    }
    NT_ASSERT(d.max_pipelines > 0 && d.max_pipelines <= NT_SPRITE_RENDERER_MAX_PIPELINES_HARDCAP);
    NT_ASSERT(d.max_vertices <= 65536 && "sprite max_vertices must fit uint16 index range");
    /* Custom flush indexes into indices[] too, so cmv must not exceed the base
     * caps. */
    NT_ASSERT(d.custom_max_vertices <= d.max_vertices && "sprite custom_max_vertices must not exceed max_vertices");
    NT_ASSERT(d.custom_max_vertices >= 16U && "sprite custom_max_vertices must be >= 16 (largest fixed single emit: slice9 = 16 verts)");

    memset(&s_sprite, 0, sizeof(s_sprite));
    s_sprite.max_pipelines = d.max_pipelines;
    s_sprite.max_vertices = d.max_vertices;
    s_sprite.max_indices = d.max_indices;
    s_sprite.custom_max_vertices = d.custom_max_vertices;

    /* Worst single flush: a pure-plain batch caps at max_vertices × 20 B; a
     * custom-attr batch caps at custom_max_vertices × the extended stride. One
     * buffer holds either — the larger of the two. */
    const uint32_t plain_bytes = d.max_vertices * NT_SPRITE_BASE_STRIDE;
    const uint32_t custom_bytes = d.custom_max_vertices * (NT_SPRITE_BASE_STRIDE + NT_SPRITE_CUSTOM_STRIDE_MAX);
    s_sprite.staging_size = (plain_bytes > custom_bytes) ? plain_bytes : custom_bytes;

    /* Heap-backed CPU staging (mirrors nt_mesh_renderer): one variable-stride
     * byte buffer holds base + interleaved custom block by construction, so flush
     * uploads it directly with no interleave pass. */
    s_sprite.staging = (uint8_t *)calloc(1, s_sprite.staging_size);
    s_sprite.indices = (uint16_t *)calloc(d.max_indices, sizeof(uint16_t));
    if (s_sprite.staging == NULL || s_sprite.indices == NULL || create_gpu_resources() != NT_OK) {
        free(s_sprite.staging);
        free(s_sprite.indices);
        memset(&s_sprite, 0, sizeof(s_sprite));
        NT_LOG_ERROR("failed to initialize sprite renderer");
        return NT_ERR_INIT_FAILED;
    }

    s_sprite.initialized = true;
    return NT_OK;
}

void nt_sprite_renderer_shutdown(void) {
    if (!s_sprite.initialized) {
        return;
    }
    destroy_gpu_resources();
    free(s_sprite.staging);
    free(s_sprite.indices);
    memset(&s_sprite, 0, sizeof(s_sprite));
}

nt_result_t nt_sprite_renderer_restore_gpu(void) {
    if (!s_sprite.initialized) {
        return NT_OK;
    }
    destroy_gpu_resources();
    return create_gpu_resources();
}
// #endregion

// #region pipeline cache
/* Build the fixed sprite vertex layout once — 20-byte stride is locked.
 * Uses NT_ATTR_POSITION/COLOR/TEXCOORD0 location enum so the sprite vertex
 * shader can declare matching layout(location=N) bindings.
 * texcoord uses normalized uint16: GL maps 0..65535 → 0..1 in the shader at no
 * cost, and atlas UVs are already u16 in the blob — emit copies them
 * verbatim without a float roundtrip. */
static nt_vertex_layout_t s_sprite_layout = {
    .stride = 20,
    .attr_count = 3,
    .attrs =
        {
            {.location = NT_ATTR_POSITION, .type = NT_VERTEX_FLOAT, .count = 3, .offset = 0},
            {.location = NT_ATTR_TEXCOORD0, .type = NT_VERTEX_UINT16, .count = 2, .normalized = true, .offset = 12},
            {.location = NT_ATTR_COLOR, .type = NT_VERTEX_UINT8, .count = 4, .normalized = true, .offset = 16},
        },
};

/* Fail-early: a custom attr at a base location (pos/color/texcoord) silently
 * corrupts the VAO — assert the requested location isn't already placed. */
static void assert_attr_location_free(const nt_vertex_layout_t *layout, uint32_t location) {
    for (uint8_t bi = 0; bi < layout->attr_count; bi++) {
        NT_ASSERT(layout->attrs[bi].location != location && "sprite custom attr location collides with a base or earlier attr location");
    }
}

/* Build the vertex layout for a material: the verbatim 20 B base, plus — when
 * the material declares custom attrs (attr_map_count>0) — each declared attr
 * appended after offset 20 as a FLOAT4, with its GL location pulled from the
 * attr_map (NOT hardcoded). Plain materials get the base layout verbatim
 * (opt-in). Mirrors nt_mesh_renderer's attr_map-driven location lookup. */
static nt_vertex_layout_t build_sprite_layout(const nt_material_info_t *mat_info) {
    nt_vertex_layout_t layout = s_sprite_layout;
    if (mat_info->attr_map_count == 0) {
        return layout;
    }
    uint16_t offset = NT_SPRITE_BASE_STRIDE;
    for (uint8_t ai = 0; ai < mat_info->attr_map_count; ai++) {
        NT_ASSERT(layout.attr_count < NT_GFX_MAX_VERTEX_ATTRS && "sprite extended layout exceeds NT_GFX_MAX_VERTEX_ATTRS");
        assert_attr_location_free(&layout, mat_info->attr_map_locations[ai]);
        layout.attrs[layout.attr_count].location = mat_info->attr_map_locations[ai];
        layout.attrs[layout.attr_count].type = NT_VERTEX_FLOAT; /* each declared custom attr is one vec4 */
        layout.attrs[layout.attr_count].count = 4;
        layout.attrs[layout.attr_count].offset = offset;
        layout.attr_count++;
        offset += 16; /* FLOAT4 */
    }
    NT_ASSERT(offset - NT_SPRITE_BASE_STRIDE <= NT_SPRITE_CUSTOM_STRIDE_MAX && "sprite custom attr block exceeds NT_SPRITE_CUSTOM_STRIDE_MAX");
    layout.stride = offset;
    return layout;
}

/* Layout discriminator folded into the pipeline-cache key: 0 for the base 20 B
 * layout, a distinct non-zero value for each extended layout, so a base
 * material and a custom-attr material can never alias the same pipeline.
 * Mixes attr_map count + each (location, FLOAT4) so two distinct attr_maps
 * yield distinct keys. */
static uint64_t nt_sprite_layout_hash(const nt_material_info_t *mat_info) {
    if (mat_info->attr_map_count == 0) {
        return 0;
    }
    uint64_t h = 0x100000001B3ULL; /* nonzero seed so a single base-located attr never hashes to 0 */
    h = h * 0x9E3779B97F4A7C15ULL + mat_info->attr_map_count;
    for (uint8_t ai = 0; ai < mat_info->attr_map_count; ai++) {
        h = h * 0x9E3779B97F4A7C15ULL + mat_info->attr_map_locations[ai];
    }
    return h;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static nt_pipeline_t find_or_create_pipeline(const nt_material_info_t *mat_info) {
    /* A recovered context may still have materials awaiting a new program. */
    if (!nt_gfx_program_ready(mat_info->program)) {
        /* The one choke point every caller passes through, so the immediate and
         * draw_list paths both get told. */
        nt_renderer_warn_program_not_ready(&s_sprite.warned_program_not_ready, mat_info);
        return (nt_pipeline_t){0};
    }
    /* Vertex-inputs own layouts, so the pipeline key is program x state. */
    uint64_t key = (uint64_t)mat_info->program.id * 0x9E3779B97F4A7C15ULL;
    key = key * 0x9E3779B97F4A7C15ULL + mat_info->render_state_hash;

    const nt_pipeline_t cached = nt_renderer_pipeline_cache_find(s_sprite.entries, s_sprite.count, key);
    if (cached.id != 0) {
        return cached;
    }

    nt_pipeline_desc_t desc;
    memset(&desc, 0, sizeof(desc));
    desc.program = mat_info->program;
    desc.depth_test = mat_info->depth_test;
    desc.depth_write = mat_info->depth_write;
    desc.depth_func = NT_DEPTH_LESS;
    desc.blend = mat_info->blend;
    desc.cull_mode = (uint8_t)mat_info->cull_mode;
    desc.label = (mat_info->label != NULL) ? mat_info->label : "sprite_pipeline";

    return nt_renderer_pipeline_cache_insert(s_sprite.entries, &s_sprite.count, s_sprite.max_pipelines, key, &desc, &s_sprite.warned_program_not_ready);
}

/* Entries are weak: a context loss frees vertex-input slots, so a hit
 * validates and a dead entry recreates in place (the mesh-row pattern) --
 * repeated losses must not grow the cache toward the hardcap. */
static nt_vertex_input_t find_or_create_vertex_input(const nt_material_info_t *mat_info) {
    const uint64_t key = nt_sprite_layout_hash(mat_info);
    uint16_t idx = s_sprite.vi_count;
    for (uint16_t i = 0; i < s_sprite.vi_count; i++) {
        if (s_sprite.vi_entries[i].key != key) {
            continue;
        }
        if (nt_gfx_vertex_input_valid(s_sprite.vi_entries[i].vi)) {
            return s_sprite.vi_entries[i].vi;
        }
        idx = i; /* dead entry: recreate in place */
        break;
    }
    if (idx == s_sprite.vi_count) {
        NT_ASSERT(s_sprite.vi_count < NT_SPRITE_RENDERER_MAX_PIPELINES_HARDCAP && "sprite vertex-input cache full; raise NT_SPRITE_RENDERER_MAX_PIPELINES_HARDCAP");
        if (s_sprite.vi_count >= NT_SPRITE_RENDERER_MAX_PIPELINES_HARDCAP) {
            return NT_VERTEX_INPUT_INVALID; /* OFF-mode guard, same as the mesh versions table */
        }
    }
    nt_vertex_input_t vi = nt_gfx_make_vertex_input(&(nt_vertex_input_desc_t){
        .layout = build_sprite_layout(mat_info),
        .vertex_buffer = s_sprite.vbo,
        .index_buffer = s_sprite.ibo,
        .label = "sprite_vi",
    });
    if (vi.id != 0) {
        s_sprite.vi_entries[idx].key = key;
        s_sprite.vi_entries[idx].vi = vi;
        if (idx == s_sprite.vi_count) {
            s_sprite.vi_count++;
        }
    }
    return vi;
}
// #endregion

// #region cmd queue helpers
/* Close the currently-open draw cmd by computing its index_count from the
 * accumulated staging position. Empty cmds (no indices written) are popped
 * so they never reach the GPU as zero-draw glDrawElements calls. */
static void close_current_cmd(void) {
    if (s_sprite.cmd_count == 0) {
        return;
    }
    nt_sprite_draw_cmd_t *c = &s_sprite.cmds[s_sprite.cmd_count - 1];
    c->index_count = s_sprite.index_count - c->first_index;
    if (c->index_count == 0) {
        s_sprite.cmd_count--;
    }
}

/* Open a new cmd with state captured from a resolved material_info, anchored
 * at the current staging index_count. Caller must close the previous cmd via
 * close_current_cmd() before opening a new one. */
static void open_cmd(nt_pipeline_t pip, const nt_material_info_t *mi, nt_material_t mat) {
    if (s_sprite.cmd_count >= NT_SPRITE_RENDERER_MAX_DRAW_CMDS) {
        nt_sprite_renderer_flush();
    }
    NT_ASSERT(s_sprite.cmd_count < NT_SPRITE_RENDERER_MAX_DRAW_CMDS && "sprite draw-cmd queue full; raise NT_SPRITE_RENDERER_MAX_DRAW_CMDS");
    nt_sprite_draw_cmd_t *c = &s_sprite.cmds[s_sprite.cmd_count++];
    memset(c, 0, sizeof(*c)); /* slots reuse across frames; clear stale fields */
    c->pipeline = pip;
    c->vertex_input = find_or_create_vertex_input(mi);
    c->material = mat;
    s_sprite.current_mat = mat;
    s_sprite.current_program = mi->program;
    /* Expected custom-attr bytes for the bound material (one FLOAT4 per declared
     * attr) — bake asserts the caller's set_custom_attrs block matches. Set here
     * (not in set_material) so the ECS draw_list path, which calls open_cmd
     * directly, is covered too. */
    s_sprite.cur_material_custom_bytes = (uint8_t)(mi->attr_map_count * 16);
    /* The whole batch opened here uploads at this stride (set per-flush, not
     * per-emit, so the plain fast path stays a constant 20). */
    s_sprite.cur_stride = (uint32_t)NT_SPRITE_BASE_STRIDE + s_sprite.cur_material_custom_bytes;
    c->tex_count = mi->tex_count;
    for (uint8_t i = 0; i < mi->tex_count; i++) {
        c->resolved_tex[i] = mi->resolved_tex[i];
        c->tex_name_hashes[i] = mi->tex_name_hashes[i];
        c->resolved_sampler[i] = mi->resolved_sampler[i];
    }
    c->first_index = s_sprite.index_count;
    c->first_vertex = s_sprite.vertex_count;
}

/* Re-open a cmd with state copied from another cmd. Used by emit_one's
 * overflow recovery path: snapshot the current cmd's state before flush()
 * resets cmd_count, then re-open with first_index=0 in the fresh staging. */
static void open_cmd_from_snapshot(const nt_sprite_draw_cmd_t *snap) {
    if (s_sprite.cmd_count >= NT_SPRITE_RENDERER_MAX_DRAW_CMDS) {
        nt_sprite_renderer_flush();
    }
    NT_ASSERT(s_sprite.cmd_count < NT_SPRITE_RENDERER_MAX_DRAW_CMDS);
    nt_sprite_draw_cmd_t *c = &s_sprite.cmds[s_sprite.cmd_count++];
    *c = *snap;
    c->first_index = s_sprite.index_count;
    c->index_count = 0;
    c->first_vertex = s_sprite.vertex_count;
}

/* Atlas page is the texture source of truth — split cmd if a run crosses pages. */
static bool ensure_current_cmd_page_texture(uint32_t page_tex) {
    NT_ASSERT(s_sprite.cmd_count > 0 && "sprite emit called with no open cmd");
    if (page_tex == 0) {
        return false;
    }

    nt_sprite_draw_cmd_t *c = &s_sprite.cmds[s_sprite.cmd_count - 1];
    /* Analytic-coverage shaders never sample the page: no substitution, no split. */
    if (c->tex_count == 0) {
        return true;
    }
    if (c->resolved_tex[0] == page_tex) {
        return true;
    }

    const bool current_cmd_empty = s_sprite.index_count == c->first_index;
    if (current_cmd_empty) {
        c->resolved_tex[0] = page_tex;
        return true;
    }

    nt_sprite_draw_cmd_t snapshot = *c;
    snapshot.resolved_tex[0] = page_tex;
    close_current_cmd();
    open_cmd_from_snapshot(&snapshot);
    return true;
}
// #endregion

// #region custom_attrs
/* Bake the current per-widget custom attr block into staging for the vertex
 * range [base, base+count) — identical for every vertex of the emit (the value
 * is per-widget, supplied by the caller, like color). The block sits CONTIGUOUS
 * with each vertex at staging + i*cur_stride + 20, so flush uploads it directly.
 * No-op when no custom attrs are set (plain emit — the base is already staged). */
static inline void bake_custom_attrs(uint32_t base, uint32_t count) {
    /* The pipeline stride comes from the material's attr_map; the bake stride comes
     * from set_custom_attrs. A mismatch (forgot set_custom_attrs, or wrong byte
     * count) corrupts the draw — catch it here. Plain material: both 0. */
    NT_ASSERT(s_sprite.cur_custom_bytes == s_sprite.cur_material_custom_bytes && "custom-attr bytes don't match the bound material's attr_map stride (forgot set_custom_attrs or wrong size)");
    if (s_sprite.cur_custom_bytes == 0) {
        return;
    }
    NT_ASSERT(base + count <= s_sprite.custom_max_vertices && "custom-attr bake out of range at extended stride");
    for (uint32_t i = 0; i < count; i++) {
        uint8_t *dst = s_sprite.staging + ((size_t)(base + i) * s_sprite.cur_stride) + NT_SPRITE_BASE_STRIDE;
        memcpy(dst, s_sprite.cur_custom_attrs, s_sprite.cur_custom_bytes);
    }
    /* Each emit CONSUMES its block: the next emit that forgets set_custom_attrs
     * then trips the material-stride assert instead of silently reusing this one. */
    s_sprite.cur_custom_bytes = 0;
}

void nt_sprite_renderer_set_custom_attrs(const float *attrs, uint8_t bytes) {
    NT_ASSERT(s_sprite.initialized);
    NT_ASSERT(attrs != NULL && bytes > 0 && bytes <= NT_SPRITE_CUSTOM_STRIDE_MAX && "set_custom_attrs: block exceeds NT_SPRITE_CUSTOM_STRIDE_MAX");
    memcpy(s_sprite.cur_custom_attrs, attrs, bytes);
    s_sprite.cur_custom_bytes = bytes;
}
// #endregion

// #region set_material
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void nt_sprite_renderer_set_material(nt_material_t mat) {
    NT_ASSERT(s_sprite.initialized);
    NT_ASSERT(s_sprite.vbo.id != 0 && s_sprite.ibo.id != 0 && "retry failed GPU restore before submitting materials");
    NT_ASSERT(mat.id != 0 && "nt_sprite_renderer_set_material: invalid material handle");

    /* Validate BEFORE same-handle early return: stale handle (destroyed material,
     * bumped generation) must assert even if the id still matches the cached one. */
    const nt_material_info_t *mat_info = nt_material_get_info(mat);
    /* Assignment, not liveness: on the frame the context dies the program is
     * already dead here, and trapping on that would crash a recoverable event.
     * make_pipeline polls the lost context and hands back an invalid pipeline. */
    NT_ASSERT(mat_info != NULL && mat_info->program.id != 0 && "nt_sprite_renderer_set_material: material has no program");

    /* Same-handle no-op only when cmd is still live; flush resets cmd_count. */
    if (mat.id == s_sprite.current_mat.id && mat_info->program.id == s_sprite.current_program.id && s_sprite.cmd_count > 0) {
        return;
    }

    if (s_sprite.cmd_count > 0) {
        nt_sprite_renderer_flush();
    }

    /* Plain material clears any stale custom-attr block so it can't leak into a
     * plain emit; a custom-attr material's block is supplied by the caller via
     * set_custom_attrs after this bind. */
    if (mat_info->attr_map_count == 0) {
        s_sprite.cur_custom_bytes = 0;
    }

    /* An invalid pipeline still opens a cmd; flush drops it. find_or_create_pipeline
     * has already said why. */
    nt_pipeline_t pip = find_or_create_pipeline(mat_info);
    open_cmd(pip, mat_info, mat);
}
// #endregion

// #region emit_region_resolved
/* always_inline keeps the ECS hot path's inlined shape. */
#if defined(__GNUC__) || defined(__clang__)
#define NT_SPRITE_EMIT_INLINE static inline __attribute__((always_inline))
#elif defined(_MSC_VER)
#define NT_SPRITE_EMIT_INLINE static inline __forceinline
#else
#define NT_SPRITE_EMIT_INLINE static inline
#endif

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
NT_SPRITE_EMIT_INLINE void emit_region_resolved(const nt_texture_region_t *r, const float (*cpos)[2], const nt_atlas_vertex_t *vraw, const uint16_t *idx, uint32_t page_tex, float ipu, const float *m,
                                                float origin_x, float origin_y, uint32_t color_packed, uint8_t flip_bits) {
    NT_ASSERT(r != NULL && cpos != NULL && vraw != NULL && idx != NULL);
    NT_ASSERT(m != NULL);
    if (r->vertex_count == 0U) {
        return; /* tombstone — silent no-op (matches old emit_one behaviour) */
    }
    if (!ensure_current_cmd_page_texture(page_tex)) {
        return;
    }

    /* Cap by the bound MATERIAL's stride, not the per-emit block: a forgotten
     * set_custom_attrs still emits at the extended stride. */
    const uint32_t vcap = (s_sprite.cur_material_custom_bytes > 0) ? s_sprite.custom_max_vertices : s_sprite.max_vertices;
    if (s_sprite.vertex_count + r->vertex_count > vcap || s_sprite.index_count + r->index_count > s_sprite.max_indices) {
        NT_ASSERT(s_sprite.cmd_count > 0 && "emit_region_resolved called with no open cmd");
        nt_sprite_draw_cmd_t snapshot = s_sprite.cmds[s_sprite.cmd_count - 1];
        nt_sprite_renderer_flush();
        open_cmd_from_snapshot(&snapshot);
    }

    /* cached_pos is source-space (no origin baked) -- regions with
     * different origins can share vertex data. */
    float tx = m[12];
    float ty = m[13];
    float tz = m[14];

    bool fx = (flip_bits & NT_SPRITE_FLAG_FLIP_X) != 0;
    bool fy = (flip_bits & NT_SPRITE_FLAG_FLIP_Y) != 0;

    /* Bake pivot into translation: world = m*local + (t - m*pivot).
     * Flip mirrors around the pivot by sign-flipping dx/dy. */
    float dx = origin_x * (float)r->source_w * ipu;
    float dy = origin_y * (float)r->source_h * ipu;
    if (fx) {
        dx = -dx;
    }
    if (fy) {
        dy = -dy;
    }
    tx -= (m[0] * dx) + (m[4] * dy);
    ty -= (m[1] * dx) + (m[5] * dy);
    tz -= (m[2] * dx) + (m[6] * dy);

    uint8_t cr = (uint8_t)(color_packed & 0xFFU);
    uint8_t cg = (uint8_t)((color_packed >> 8) & 0xFFU);
    uint8_t cb = (uint8_t)((color_packed >> 16) & 0xFFU);
    uint8_t ca = (uint8_t)((color_packed >> 24) & 0xFFU);

    uint32_t base = s_sprite.vertex_count;
#ifdef __wasm_simd128__
    if (r->vertex_count == 4) {
        /* De-interleave cpos[4][2] → pxs/pys lanes. */
        v128_t lo = wasm_v128_load(&cpos[0][0]);
        v128_t hi = wasm_v128_load(&cpos[2][0]);
        v128_t pxs = wasm_i32x4_shuffle(lo, hi, 0, 2, 4, 6);
        v128_t pys = wasm_i32x4_shuffle(lo, hi, 1, 3, 5, 7);
        if (fx) {
            pxs = wasm_f32x4_neg(pxs);
        }
        if (fy) {
            pys = wasm_f32x4_neg(pys);
        }
        v128_t m0 = wasm_f32x4_splat(m[0]);
        v128_t m1 = wasm_f32x4_splat(m[1]);
        v128_t m2 = wasm_f32x4_splat(m[2]);
        v128_t m4 = wasm_f32x4_splat(m[4]);
        v128_t m5 = wasm_f32x4_splat(m[5]);
        v128_t m6 = wasm_f32x4_splat(m[6]);
        v128_t mtx = wasm_f32x4_splat(tx);
        v128_t mty = wasm_f32x4_splat(ty);
        v128_t mtz = wasm_f32x4_splat(tz);
        v128_t xs = wasm_f32x4_add(wasm_f32x4_add(wasm_f32x4_mul(m0, pxs), wasm_f32x4_mul(m4, pys)), mtx);
        v128_t ys = wasm_f32x4_add(wasm_f32x4_add(wasm_f32x4_mul(m1, pxs), wasm_f32x4_mul(m5, pys)), mty);
        v128_t zs = wasm_f32x4_add(wasm_f32x4_add(wasm_f32x4_mul(m2, pxs), wasm_f32x4_mul(m6, pys)), mtz);
        float xs_arr[4];
        float ys_arr[4];
        float zs_arr[4];
        wasm_v128_store(xs_arr, xs);
        wasm_v128_store(ys_arr, ys);
        wasm_v128_store(zs_arr, zs);
        for (uint8_t i = 0; i < 4; i++) {
            nt_sprite_vertex_t *v = (nt_sprite_vertex_t *)(s_sprite.staging + ((size_t)(base + i) * s_sprite.cur_stride));
            v->position[0] = xs_arr[i];
            v->position[1] = ys_arr[i];
            v->position[2] = zs_arr[i];
            v->texcoord[0] = vraw[i].atlas_u;
            v->texcoord[1] = vraw[i].atlas_v;
            v->color[0] = cr;
            v->color[1] = cg;
            v->color[2] = cb;
            v->color[3] = ca;
        }
    } else
#endif /* __wasm_simd128__ */
    {
        for (uint8_t i = 0; i < r->vertex_count; i++) {
            float px = cpos[i][0];
            if (fx) {
                px = -px;
            }
            float py = cpos[i][1];
            if (fy) {
                py = -py;
            }
            nt_sprite_vertex_t *v = (nt_sprite_vertex_t *)(s_sprite.staging + ((size_t)(base + i) * s_sprite.cur_stride));
            v->position[0] = (m[0] * px) + (m[4] * py) + tx;
            v->position[1] = (m[1] * px) + (m[5] * py) + ty;
            v->position[2] = (m[2] * px) + (m[6] * py) + tz;
            v->texcoord[0] = vraw[i].atlas_u;
            v->texcoord[1] = vraw[i].atlas_v;
            v->color[0] = cr;
            v->color[1] = cg;
            v->color[2] = cb;
            v->color[3] = ca;
        }
    }
    bake_custom_attrs(base, r->vertex_count);
    s_sprite.vertex_count += r->vertex_count;

    /* Emit indices (rebase to staging base). Each flush chunk is capped to
     * 65536 vertices, so uint16 indices stay valid. */
    uint16_t *out_idx = &s_sprite.indices[s_sprite.index_count];
    for (uint8_t i = 0; i < r->index_count; i++) {
        uint32_t rebased = base + (uint32_t)idx[i];
        NT_ASSERT(rebased <= UINT16_MAX && "sprite uint16 index chunk overflow");
        out_idx[i] = (uint16_t)rebased;
    }
    s_sprite.index_count += r->index_count;

#ifdef NT_TEST_ACCESS
    /* Capture per-emit counts + first-vertex offset so tests can read
     * back emitted positions after draw_list completes (flush resets
     * vertex_count but leaves the array data intact). */
    s_sprite.last_emit_vertex_count = r->vertex_count;
    s_sprite.last_emit_index_count = r->index_count;
    s_sprite.last_emit_first_vertex = base;
#endif
}
// #endregion

// #region emit_one
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void emit_one(const nt_render_item_t *item, const nt_sprite_comp_view_t *sv, const nt_transform_comp_view_t *tv, const nt_drawable_comp_view_t *dv) {
    nt_entity_t e = {.id = item->entity};
    uint16_t eidx = nt_entity_index(e);

    /* Inlined vs three calls each doing the same liveness assert + array read. */
    uint16_t s_idx = sv->sparse_indices[eidx];
    uint16_t t_idx = tv->sparse_indices[eidx];
    uint16_t d_idx = dv->sparse_indices[eidx];

    /* NT_INVALID_COMP_INDEX would index SoA OOB -- catches stale items. */
    NT_ASSERT(s_idx != NT_INVALID_COMP_INDEX && "sprite render item: entity has no sprite component");
    NT_ASSERT(t_idx != NT_INVALID_COMP_INDEX && "sprite render item: entity has no transform component");
    NT_ASSERT(d_idx != NT_INVALID_COMP_INDEX && "sprite render item: entity has no drawable component");

    nt_resource_t atlas = sv->atlas[s_idx];
    const nt_sprite_resolved_region_t *resolved = &sv->resolved[s_idx];
    uint8_t flags = sv->flags[s_idx];
    NT_ASSERT((flags & NT_SPRITE_FLAG_RESOLVED) != 0 && "sprite render item: sprite is unresolved");
    NT_ASSERT(resolved->region != NULL && "sprite render item: resolved region is NULL");
    NT_ASSERT(resolved->region->vertex_count != 0 && "sprite render item: region is tombstoned");
    const nt_texture_region_t *r = resolved->region;
    NT_ASSERT(resolved->cached_pos != NULL && resolved->raw_vertices != NULL && resolved->indices != NULL);
    uint32_t page_tex = nt_resource_get(resolved->page_resource);

    const float origin_x = (flags & NT_SPRITE_FLAG_ORIGIN_OV) ? sv->origin[s_idx][0] : r->origin_x;
    const float origin_y = (flags & NT_SPRITE_FLAG_ORIGIN_OV) ? sv->origin[s_idx][1] : r->origin_y;
    const uint8_t flip_bits = flags & (NT_SPRITE_FLAG_FLIP_X | NT_SPRITE_FLAG_FLIP_Y);
    const float ipu = nt_atlas_get_inverse_pixels_per_unit(atlas);

    // #region emit_one_slice9_branch
    bool has_s9_ov = (flags & NT_SPRITE_FLAG_SLICE9_OV) != 0;
    bool has_s9_region = (r->slice9_lrtb[0] | r->slice9_lrtb[1] | r->slice9_lrtb[2] | r->slice9_lrtb[3]) != 0;
    if (has_s9_ov || has_s9_region) {
        uint16_t sl;
        uint16_t sr;
        uint16_t st;
        uint16_t sb;
        if (has_s9_ov) {
            sl = sv->slice9_lrtb[s_idx][0];
            sr = sv->slice9_lrtb[s_idx][1];
            st = sv->slice9_lrtb[s_idx][2];
            sb = sv->slice9_lrtb[s_idx][3];
        } else {
            sl = r->slice9_lrtb[0];
            sr = r->slice9_lrtb[1];
            st = r->slice9_lrtb[2];
            sb = r->slice9_lrtb[3];
        }
        /* Inline slice9 emit using world_matrix directly (same transform
         * pipeline as regular sprites in emit_region_resolved). */
        NT_ASSERT(r->transform == 0 && "slice9 region must have transform == 0");
        NT_ASSERT(r->trim_offset_x == 0 && r->trim_offset_y == 0 && "slice9 region must be untrimmed");
        NT_ASSERT(r->source_w > 0 && r->source_h > 0);
        NT_ASSERT(sl + sr < r->source_w && st + sb < r->source_h);

        if (!ensure_current_cmd_page_texture(page_tex)) {
            return;
        }
        const uint32_t vcap = (s_sprite.cur_material_custom_bytes > 0) ? s_sprite.custom_max_vertices : s_sprite.max_vertices;
        if (s_sprite.vertex_count + 16U > vcap || s_sprite.index_count + 54U > s_sprite.max_indices) {
            NT_ASSERT(s_sprite.cmd_count > 0);
            nt_sprite_draw_cmd_t snapshot = s_sprite.cmds[s_sprite.cmd_count - 1];
            nt_sprite_renderer_flush();
            open_cmd_from_snapshot(&snapshot);
        }

        /* Flip border swap */
        uint16_t fl = sl;
        uint16_t fr = sr;
        uint16_t ft = st;
        uint16_t fb = sb;
        if (flip_bits & NT_SPRITE_FLAG_FLIP_X) {
            fl = sr;
            fr = sl;
        }
        if (flip_bits & NT_SPRITE_FLAG_FLIP_Y) {
            ft = sb;
            fb = st;
        }

        /* DST corner size = src × per-entity slice9_scale; last-line tripwire. */
        NT_ASSERT(isfinite(sv->slice9_scale[s_idx]) && sv->slice9_scale[s_idx] > 0.0F && "emit_one: sv->slice9_scale[s_idx] must be finite > 0");
        const float s9_scale = sv->slice9_scale[s_idx];

        /* Build 4x4 grid in local source space (ipu-scaled, origin at 0,0). */
        const float src_w = (float)r->source_w * ipu;
        const float src_h = (float)r->source_h * ipu;
        float fl_w = (float)fl * ipu * s9_scale;
        float fr_w = (float)fr * ipu * s9_scale;
        float ft_w = (float)ft * ipu * s9_scale;
        float fb_w = (float)fb * ipu * s9_scale;
        /* Proportionally shrink borders when source rect is smaller than total borders */
        if (fl_w + fr_w > src_w) {
            float ratio = src_w / (fl_w + fr_w);
            fl_w *= ratio;
            fr_w *= ratio;
        }
        if (ft_w + fb_w > src_h) {
            float ratio = src_h / (ft_w + fb_w);
            ft_w *= ratio;
            fb_w *= ratio;
        }
        float lxs[4] = {0.0F, fl_w, src_w - fr_w, src_w};
        float lys[4] = {0.0F, fb_w, src_h - ft_w, src_h};

        /* Pivot offset into translation (mirrors emit_region_resolved). */
        const float *m = tv->world_matrices[t_idx];
        float dx = origin_x * src_w;
        float dy = origin_y * src_h;
        if (flip_bits & NT_SPRITE_FLAG_FLIP_X) {
            dx = -dx;
        }
        if (flip_bits & NT_SPRITE_FLAG_FLIP_Y) {
            dy = -dy;
        }
        const float tx = m[12] - (m[0] * dx) - (m[4] * dy);
        const float ty = m[13] - (m[1] * dx) - (m[5] * dy);
        const float tz = m[14] - (m[2] * dx) - (m[6] * dy);

        /* Transform 4x4 grid points through world_matrix. */
        float wxs[4][4]; /* wxs[row][col] */
        float wys[4][4];
        float wzs[4][4];
        for (uint8_t row = 0; row < 4; row++) {
            for (uint8_t col = 0; col < 4; col++) {
                float px = lxs[col];
                float py = lys[row];
                if (flip_bits & NT_SPRITE_FLAG_FLIP_X) {
                    px = -px;
                }
                if (flip_bits & NT_SPRITE_FLAG_FLIP_Y) {
                    py = -py;
                }
                wxs[row][col] = (m[0] * px) + (m[4] * py) + tx;
                wys[row][col] = (m[1] * px) + (m[5] * py) + ty;
                wzs[row][col] = (m[2] * px) + (m[6] * py) + tz;
            }
        }

        /* UV splits from region vertices (same as emit_slice9). */
        uint16_t u_min = UINT16_MAX;
        uint16_t u_max = 0;
        uint16_t v_min = UINT16_MAX;
        uint16_t v_max = 0;
        for (uint8_t i = 0; i < r->vertex_count; i++) {
            uint16_t au = resolved->raw_vertices[i].atlas_u;
            uint16_t av = resolved->raw_vertices[i].atlas_v;
            if (au < u_min) {
                u_min = au;
            }
            if (au > u_max) {
                u_max = au;
            }
            if (av < v_min) {
                v_min = av;
            }
            if (av > v_max) {
                v_max = av;
            }
        }
        uint16_t u_range = (uint16_t)(u_max - u_min);
        uint16_t v_range = (uint16_t)(v_max - v_min);
        uint16_t us[4] = {
            u_min,
            (uint16_t)(u_min + (((uint32_t)fl * u_range) / r->source_w)),
            (uint16_t)(u_max - (((uint32_t)fr * u_range) / r->source_w)),
            u_max,
        };
        /* V inverted: geometry Y-up, texture V is PNG Y-down. */
        uint16_t vs[4] = {
            v_max,
            (uint16_t)(v_max - (((uint32_t)fb * v_range) / r->source_h)),
            (uint16_t)(v_min + (((uint32_t)ft * v_range) / r->source_h)),
            v_min,
        };
        if (flip_bits & NT_SPRITE_FLAG_FLIP_X) {
            uint16_t t0 = us[0];
            us[0] = us[3];
            us[3] = t0;
            uint16_t t1 = us[1];
            us[1] = us[2];
            us[2] = t1;
        }
        if (flip_bits & NT_SPRITE_FLAG_FLIP_Y) {
            uint16_t t0 = vs[0];
            vs[0] = vs[3];
            vs[3] = t0;
            uint16_t t1 = vs[1];
            vs[1] = vs[2];
            vs[2] = t1;
        }

        /* Unpack color. */
        const uint32_t s9_color = dv->colors_packed[d_idx];
        const uint8_t cr = (uint8_t)(s9_color & 0xFFU);
        const uint8_t cg = (uint8_t)((s9_color >> 8) & 0xFFU);
        const uint8_t cb = (uint8_t)((s9_color >> 16) & 0xFFU);
        const uint8_t ca = (uint8_t)((s9_color >> 24) & 0xFFU);

        /* Emit 4x4 grid = 16 unique vertices (shared at cell boundaries). */
        const uint32_t base = s_sprite.vertex_count;
        for (uint8_t row = 0; row < 4; row++) {
            for (uint8_t col = 0; col < 4; col++) {
                nt_sprite_vertex_t *v = (nt_sprite_vertex_t *)(s_sprite.staging + ((size_t)(base + (row * 4) + col) * s_sprite.cur_stride));
                v->position[0] = wxs[row][col];
                v->position[1] = wys[row][col];
                v->position[2] = wzs[row][col];
                v->texcoord[0] = us[col];
                v->texcoord[1] = vs[row];
                v->color[0] = cr;
                v->color[1] = cg;
                v->color[2] = cb;
                v->color[3] = ca;
            }
        }

        /* 54 indices: 9 cells x 2 triangles x 3 indices. */
        uint32_t ii = 0;
        for (uint8_t row = 0; row < 3; row++) {
            for (uint8_t col = 0; col < 3; col++) {
                const uint16_t i_bl = (uint16_t)(base + (row * 4) + col);
                const uint16_t i_br = (uint16_t)(i_bl + 1U);
                const uint16_t i_tl = (uint16_t)(i_bl + 4U);
                const uint16_t i_tr = (uint16_t)(i_tl + 1U);
                uint16_t *out_idx = &s_sprite.indices[s_sprite.index_count + ii];
                out_idx[0] = i_tl;
                out_idx[1] = i_tr;
                out_idx[2] = i_bl;
                out_idx[3] = i_bl;
                out_idx[4] = i_tr;
                out_idx[5] = i_br;
                ii += 6;
            }
        }
        bake_custom_attrs(base, 16U);
        s_sprite.vertex_count += 16U;
        s_sprite.index_count += 54U;
#ifdef NT_TEST_ACCESS
        s_sprite.last_slice9_vertex_count = 16U;
        s_sprite.last_slice9_index_count = 54U;
        s_sprite.last_emit_vertex_count = 16U;
        s_sprite.last_emit_index_count = 54U;
        s_sprite.last_emit_first_vertex = base;
#endif
        return;
    }

    // #endregion
    emit_region_resolved(r, resolved->cached_pos, resolved->raw_vertices, resolved->indices, page_tex, ipu, tv->world_matrices[t_idx], origin_x, origin_y, dv->colors_packed[d_idx], flip_bits);
}
// #endregion

// #region emit_region
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void nt_sprite_renderer_emit_region(nt_resource_t atlas, uint32_t region_index, const float *world_matrix, float origin_x, float origin_y, uint32_t color_packed, uint8_t flip_bits) {
    NT_ASSERT(s_sprite.initialized);
    NT_ASSERT(world_matrix != NULL);
    NT_ASSERT(atlas.id != 0 && "nt_sprite_renderer_emit_region: invalid atlas handle");
    NT_ASSERT(nt_resource_is_ready(atlas) && "nt_sprite_renderer_emit_region: atlas must be READY");
    NT_ASSERT(s_sprite.cmd_count > 0 && "nt_sprite_renderer_emit_region: call nt_sprite_renderer_set_material first");

    nt_atlas_region_handles_t h;
    nt_atlas_get_region_handles(atlas, region_index, &h);
    if (h.region->vertex_count == 0U) {
        return; /* tombstone or out-of-range */
    }
    emit_region_resolved(h.region, h.cached_pos, h.raw_vertices, h.indices, nt_resource_get(h.page_resource), h.ipu, world_matrix, origin_x, origin_y, color_packed, flip_bits);
}
// #endregion

// #region emit_geometry
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void nt_sprite_renderer_emit_geometry(nt_resource_t atlas, uint32_t region_index, const float (*positions)[2], uint32_t vertex_count, const uint16_t *indices, uint32_t index_count,
                                      const float *world_matrix, uint32_t color_packed) {
    NT_ASSERT(s_sprite.initialized);
    NT_ASSERT(positions != NULL && indices != NULL && world_matrix != NULL);
    NT_ASSERT(atlas.id != 0 && "nt_sprite_renderer_emit_geometry: invalid atlas handle");
    NT_ASSERT(nt_resource_is_ready(atlas) && "nt_sprite_renderer_emit_geometry: atlas must be READY");
    NT_ASSERT(s_sprite.cmd_count > 0 && "nt_sprite_renderer_emit_geometry: call nt_sprite_renderer_set_material first");
    NT_ASSERT(vertex_count > 0U && index_count > 0U && "nt_sprite_renderer_emit_geometry: empty geometry");
    NT_ASSERT(vertex_count <= s_sprite.max_vertices && "nt_sprite_renderer_emit_geometry: vertex_count exceeds staging capacity");
    NT_ASSERT(index_count <= s_sprite.max_indices && "nt_sprite_renderer_emit_geometry: index_count exceeds staging capacity");

    nt_atlas_region_handles_t h;
    nt_atlas_get_region_handles(atlas, region_index, &h);
    if (h.region->vertex_count == 0U) {
        return; /* tombstone */
    }
    const uint32_t page_tex = nt_resource_get(h.page_resource);
    if (!ensure_current_cmd_page_texture(page_tex)) {
        return;
    }

    /* Cap by the bound MATERIAL's stride, not the per-emit block: a forgotten
     * set_custom_attrs still emits at the extended stride. */
    const uint32_t vcap = (s_sprite.cur_material_custom_bytes > 0) ? s_sprite.custom_max_vertices : s_sprite.max_vertices;
    if (s_sprite.vertex_count + vertex_count > vcap || s_sprite.index_count + index_count > s_sprite.max_indices) {
        nt_sprite_draw_cmd_t snapshot = s_sprite.cmds[s_sprite.cmd_count - 1];
        nt_sprite_renderer_flush();
        open_cmd_from_snapshot(&snapshot);
    }
    /* A single emit larger than the (post-flush empty) cap can't fit — fail fast instead of overrunning staging. */
    NT_ASSERT(vertex_count <= vcap && index_count <= s_sprite.max_indices && "emit_geometry: single emit exceeds staging cap (raise custom_max_vertices / max_indices)");

    /* Sample at the region's UV centroid -- the corner of vertex 0 would
     * land at the texel boundary and bleed into neighbours under linear
     * filtering. Centroid is safely inside the region for any convex
     * polygon, and exactly the pixel center for a 4-vert axis-aligned
     * white region. uint16 atlas_u/v sums fit uint32 for the polygon
     * worst case (8 verts * 65535 << 2^32). */
    uint32_t sum_u = 0;
    uint32_t sum_v = 0;
    for (uint8_t i = 0; i < h.region->vertex_count; i++) {
        sum_u += h.raw_vertices[i].atlas_u;
        sum_v += h.raw_vertices[i].atlas_v;
    }
    const uint16_t shared_u = (uint16_t)(sum_u / h.region->vertex_count);
    const uint16_t shared_v = (uint16_t)(sum_v / h.region->vertex_count);

    const uint8_t cr = (uint8_t)(color_packed & 0xFFU);
    const uint8_t cg = (uint8_t)((color_packed >> 8) & 0xFFU);
    const uint8_t cb = (uint8_t)((color_packed >> 16) & 0xFFU);
    const uint8_t ca = (uint8_t)((color_packed >> 24) & 0xFFU);

    const float *m = world_matrix;
    const float tx = m[12];
    const float ty = m[13];
    const float tz = m[14];

    const uint32_t base = s_sprite.vertex_count;
    for (uint32_t i = 0; i < vertex_count; i++) {
        const float px = positions[i][0];
        const float py = positions[i][1];
        nt_sprite_vertex_t *v = (nt_sprite_vertex_t *)(s_sprite.staging + ((size_t)(base + i) * s_sprite.cur_stride));
        v->position[0] = (m[0] * px) + (m[4] * py) + tx;
        v->position[1] = (m[1] * px) + (m[5] * py) + ty;
        v->position[2] = (m[2] * px) + (m[6] * py) + tz;
        v->texcoord[0] = shared_u;
        v->texcoord[1] = shared_v;
        v->color[0] = cr;
        v->color[1] = cg;
        v->color[2] = cb;
        v->color[3] = ca;
    }
    bake_custom_attrs(base, vertex_count);
    s_sprite.vertex_count += vertex_count;

    uint16_t *out_idx = &s_sprite.indices[s_sprite.index_count];
    for (uint32_t i = 0; i < index_count; i++) {
        const uint32_t rebased = base + (uint32_t)indices[i];
        NT_ASSERT(rebased <= UINT16_MAX && "sprite uint16 index chunk overflow");
        NT_ASSERT(indices[i] < vertex_count && "nt_sprite_renderer_emit_geometry: index out of range");
        out_idx[i] = (uint16_t)rebased;
    }
    s_sprite.index_count += index_count;

#ifdef NT_TEST_ACCESS
    s_sprite.last_emit_vertex_count = vertex_count;
    s_sprite.last_emit_index_count = index_count;
    s_sprite.last_emit_first_vertex = base;
#endif
}
// #endregion

// #region emit_slice9
/* Production scales 0.1..10; overflow asserts at scale > ~4096 for a 16 px border. */
static inline uint16_t scale_slice9_border(uint16_t base, float scale) {
    const float f = ((float)base * scale) + 0.5F;
    NT_ASSERT(f >= 0.0F && f <= 65535.0F && "slice9 border × scale overflows uint16_t");
    return (uint16_t)f;
}

/* src borders pick UV cut; dst borders set rendered corner/edge size. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void nt_sprite_renderer_emit_slice9(nt_resource_t atlas, uint32_t region_index, float x, float y, float w, float h, const uint16_t src_lrtb[4], float slice9_scale, uint32_t color_packed,
                                    uint8_t flip_bits, const float *world_matrix) {
    NT_ASSERT(s_sprite.initialized);
    NT_ASSERT(atlas.id != 0 && "emit_slice9: invalid atlas handle");
    NT_ASSERT(nt_resource_is_ready(atlas) && "emit_slice9: atlas must be READY");
    NT_ASSERT(s_sprite.cmd_count > 0 && "emit_slice9: call nt_sprite_renderer_set_material first");
    NT_ASSERT(world_matrix != NULL && "emit_slice9: world_matrix must be non-NULL (pass NT_MATH_MAT4_IDENTITY for none)");
    NT_ASSERT(isfinite(x) && isfinite(y) && isfinite(w) && isfinite(h));
    NT_ASSERT(isfinite(slice9_scale) && slice9_scale > 0.0F && "emit_slice9: slice9_scale must be finite > 0");
    NT_ASSERT(w >= 0.0F && h >= 0.0F && "slice9 target dimensions must be non-negative");

    nt_atlas_region_handles_t rh;
    nt_atlas_get_region_handles(atlas, region_index, &rh);
    if (rh.region->vertex_count == 0U) {
        return; /* tombstone */
    }

    const float ipu = nt_atlas_get_inverse_pixels_per_unit(atlas);

    /* NULL src_lrtb → atlas-baked borders for this region. */
    const uint16_t src_sl = (src_lrtb != NULL) ? src_lrtb[0] : rh.region->slice9_lrtb[0];
    const uint16_t src_sr = (src_lrtb != NULL) ? src_lrtb[1] : rh.region->slice9_lrtb[1];
    const uint16_t src_st = (src_lrtb != NULL) ? src_lrtb[2] : rh.region->slice9_lrtb[2];
    const uint16_t src_sb = (src_lrtb != NULL) ? src_lrtb[3] : rh.region->slice9_lrtb[3];
    const uint16_t dst_sl = scale_slice9_border(src_sl, slice9_scale);
    const uint16_t dst_sr = scale_slice9_border(src_sr, slice9_scale);
    const uint16_t dst_st = scale_slice9_border(src_st, slice9_scale);
    const uint16_t dst_sb = scale_slice9_border(src_sb, slice9_scale);

    NT_ASSERT(rh.region->transform == 0 && "slice9 region must have transform == 0 (no rotation)");
    NT_ASSERT(rh.region->trim_offset_x == 0 && rh.region->trim_offset_y == 0 && "slice9 region must be untrimmed");
    NT_ASSERT(rh.region->source_w > 0 && rh.region->source_h > 0 && "slice9 region source dimensions must be non-zero");
    NT_ASSERT(src_sl + src_sr < rh.region->source_w && src_st + src_sb < rh.region->source_h && "slice9 src borders exceed source dimensions");
    NT_ASSERT(ipu > 0.0F && "slice9 ipu must be positive");

    const uint32_t page_tex = nt_resource_get(rh.page_resource);
    if (!ensure_current_cmd_page_texture(page_tex)) {
        return;
    }

    /* Symmetric swap keeps UV and position consistent under flip. */
    uint16_t fsrc_l = src_sl;
    uint16_t fsrc_r = src_sr;
    uint16_t fsrc_t = src_st;
    uint16_t fsrc_b = src_sb;
    uint16_t fdst_l = dst_sl;
    uint16_t fdst_r = dst_sr;
    uint16_t fdst_t = dst_st;
    uint16_t fdst_b = dst_sb;
    if (flip_bits & NT_SPRITE_FLAG_FLIP_X) {
        fsrc_l = src_sr;
        fsrc_r = src_sl;
        fdst_l = dst_sr;
        fdst_r = dst_sl;
    }
    if (flip_bits & NT_SPRITE_FLAG_FLIP_Y) {
        fsrc_t = src_sb;
        fsrc_b = src_st;
        fdst_t = dst_sb;
        fdst_b = dst_st;
    }
    const uint16_t fl = fsrc_l;
    const uint16_t fr = fsrc_r;
    const uint16_t ft = fsrc_t;
    const uint16_t fb = fsrc_b;

    /* Extract bbox UVs from region vertices (u16 space). */
    uint16_t u_min = UINT16_MAX;
    uint16_t u_max = 0;
    uint16_t v_min = UINT16_MAX;
    uint16_t v_max = 0;
    for (uint8_t i = 0; i < rh.region->vertex_count; i++) {
        uint16_t au = rh.raw_vertices[i].atlas_u;
        uint16_t av = rh.raw_vertices[i].atlas_v;
        if (au < u_min) {
            u_min = au;
        }
        if (au > u_max) {
            u_max = au;
        }
        if (av < v_min) {
            v_min = av;
        }
        if (av > v_max) {
            v_max = av;
        }
    }

    // #region position_and_uv_splits
    /* Positions use DST borders; UV cuts use SRC borders. */
    float fl_w = (float)fdst_l * ipu;
    float fr_w = (float)fdst_r * ipu;
    float ft_w = (float)fdst_t * ipu;
    float fb_w = (float)fdst_b * ipu;
    /* Proportionally shrink dst borders when target rect is smaller than total borders */
    if (fl_w + fr_w > w) {
        float ratio = w / (fl_w + fr_w);
        fl_w *= ratio;
        fr_w *= ratio;
    }
    if (ft_w + fb_w > h) {
        float ratio = h / (ft_w + fb_w);
        ft_w *= ratio;
        fb_w *= ratio;
    }
    float xs[4] = {x, x + fl_w, x + w - fr_w, x + w};
    float ys[4] = {y, y + fb_w, y + h - ft_w, y + h};

    /* UV splits (4 u-values, 4 v-values in u16). Integer math avoids precision loss. */
    uint16_t u_range = (uint16_t)(u_max - u_min);
    uint16_t v_range = (uint16_t)(v_max - v_min);
    uint16_t us[4] = {
        u_min,
        (uint16_t)(u_min + (((uint32_t)fl * u_range) / rh.region->source_w)),
        (uint16_t)(u_max - (((uint32_t)fr * u_range) / rh.region->source_w)),
        u_max,
    };
    /* V splits inverted: geometry Y-up but texture V is PNG Y-down.
     * vs[0] (geometry bottom) → v_max (texture bottom). */
    uint16_t vs[4] = {
        v_max,
        (uint16_t)(v_max - (((uint32_t)fb * v_range) / rh.region->source_h)),
        (uint16_t)(v_min + (((uint32_t)ft * v_range) / rh.region->source_h)),
        v_min,
    };

    /* UV flip after split computation. */
    if (flip_bits & NT_SPRITE_FLAG_FLIP_X) {
        uint16_t t0 = us[0];
        us[0] = us[3];
        us[3] = t0;
        uint16_t t1 = us[1];
        us[1] = us[2];
        us[2] = t1;
    }
    if (flip_bits & NT_SPRITE_FLAG_FLIP_Y) {
        uint16_t t0 = vs[0];
        vs[0] = vs[3];
        vs[3] = t0;
        uint16_t t1 = vs[1];
        vs[1] = vs[2];
        vs[2] = t1;
    }

    // #endregion

    // #region emit_slice9_vertices
    const uint32_t vcap = (s_sprite.cur_material_custom_bytes > 0) ? s_sprite.custom_max_vertices : s_sprite.max_vertices;
    if (s_sprite.vertex_count + 16U > vcap || s_sprite.index_count + 54U > s_sprite.max_indices) {
        NT_ASSERT(s_sprite.cmd_count > 0 && "emit_slice9 called with no open cmd");
        nt_sprite_draw_cmd_t snapshot = s_sprite.cmds[s_sprite.cmd_count - 1];
        nt_sprite_renderer_flush();
        open_cmd_from_snapshot(&snapshot);
    }

    /* Unpack color. */
    uint8_t cr = (uint8_t)(color_packed & 0xFFU);
    uint8_t cg = (uint8_t)((color_packed >> 8) & 0xFFU);
    uint8_t cb = (uint8_t)((color_packed >> 16) & 0xFFU);
    uint8_t ca = (uint8_t)((color_packed >> 24) & 0xFFU);

    uint32_t base = s_sprite.vertex_count;

    /* Emit 4x4 grid = 16 unique vertices (shared at cell boundaries). */
    for (uint8_t row = 0; row < 4; row++) {
        for (uint8_t col = 0; col < 4; col++) {
            nt_sprite_vertex_t *v = (nt_sprite_vertex_t *)(s_sprite.staging + ((size_t)(base + (row * 4) + col) * s_sprite.cur_stride));
            v->position[0] = xs[col];
            v->position[1] = ys[row];
            v->position[2] = 0.0F;
            v->texcoord[0] = us[col];
            v->texcoord[1] = vs[row];
            v->color[0] = cr;
            v->color[1] = cg;
            v->color[2] = cb;
            v->color[3] = ca;
        }
    }

    /* 54 indices: 9 cells x 2 triangles x 3 indices. */
    uint16_t *out_idx = &s_sprite.indices[s_sprite.index_count];
    uint32_t ii = 0;
    for (uint8_t row = 0; row < 3; row++) {
        for (uint8_t col = 0; col < 3; col++) {
            const uint16_t i_bl = (uint16_t)(base + (row * 4) + col);
            const uint16_t i_br = (uint16_t)(i_bl + 1U);
            const uint16_t i_tl = (uint16_t)(i_bl + 4U);
            const uint16_t i_tr = (uint16_t)(i_tl + 1U);
            out_idx[ii++] = i_tl;
            out_idx[ii++] = i_tr;
            out_idx[ii++] = i_bl;
            out_idx[ii++] = i_bl;
            out_idx[ii++] = i_tr;
            out_idx[ii++] = i_br;
        }
    }

    /* Skip identity mat4 to save the transform pass; same 9-float subset as
     * emit_region_resolved (m[0,1,2,4,5,6,12,13,14]). */
    const float *m = world_matrix;
    const bool is_identity = m[0] == 1.0F && m[1] == 0.0F && m[2] == 0.0F && m[4] == 0.0F && m[5] == 1.0F && m[6] == 0.0F && m[12] == 0.0F && m[13] == 0.0F && m[14] == 0.0F;
    if (!is_identity) {
        for (uint32_t vi = 0; vi < 16U; vi++) {
            nt_sprite_vertex_t *v = (nt_sprite_vertex_t *)(s_sprite.staging + ((size_t)(base + vi) * s_sprite.cur_stride));
            const float px = v->position[0];
            const float py = v->position[1];
            v->position[0] = (m[0] * px) + (m[4] * py) + m[12];
            v->position[1] = (m[1] * px) + (m[5] * py) + m[13];
            v->position[2] = (m[2] * px) + (m[6] * py) + m[14];
        }
    }

    bake_custom_attrs(base, 16U);
    s_sprite.vertex_count += 16U;
    s_sprite.index_count += 54U;

#ifdef NT_TEST_ACCESS
    s_sprite.last_slice9_vertex_count = 16U;
    s_sprite.last_slice9_index_count = 54U;
    /* Also update the generic last_emit so test_last_emit_position/texcoord work. */
    s_sprite.last_emit_vertex_count = 16U;
    s_sprite.last_emit_index_count = 54U;
    s_sprite.last_emit_first_vertex = base;
#endif
    // #endregion
}
// #endregion

// #endregion

// #region draw_list
/* Emit pass: stream verts into staging per batch_key; flush() does the upload. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void nt_sprite_renderer_draw_list(const nt_render_item_t *items, uint32_t count) {
    NT_ASSERT(s_sprite.initialized);
    if (count == 0) {
        return;
    }
    NT_ASSERT(items != NULL);
    NT_ASSERT(s_sprite.vbo.id != 0 && s_sprite.ibo.id != 0 && "retry failed GPU restore before drawing");

    s_sprite.last_draw_list_calls = 0;
    s_sprite.cur_custom_bytes = 0; /* ECS sprite path emits no custom attrs */
    nt_sprite_comp_view_t sv = nt_sprite_comp_view();
    nt_transform_comp_view_t tv = nt_transform_comp_view();
    nt_drawable_comp_view_t dv = nt_drawable_comp_view();

    uint32_t run_start = 0;
    while (run_start < count) {
        uint32_t run_end = run_start + 1;
        while (run_end < count && items[run_end].batch_key == items[run_start].batch_key) {
            run_end++;
        }

        /* Resolve material + pipeline for this run via run-leader entity */
        nt_entity_t leader = {.id = items[run_start].entity};
        const nt_material_t *mat = nt_material_comp_handle(leader);
        const nt_material_info_t *mat_info = nt_material_get_info(*mat);
        if (mat_info == NULL) { /* destroyed between item build and replay */
            run_start = run_end;
            continue;
        }

        /* Each batch_key boundary opens a fresh cmd. */
        nt_pipeline_t pip = find_or_create_pipeline(mat_info);
        if (pip.id == 0) { /* context died mid-frame; skip rather than draw through a stale bind */
            run_start = run_end;
            continue;
        }
        close_current_cmd();
        open_cmd(pip, mat_info, *mat);

        for (uint32_t i = run_start; i < run_end; i++) {
            emit_one(&items[i], &sv, &tv, &dv);
        }
        run_start = run_end;
    }
    nt_sprite_renderer_flush();
}
// #endregion

// #region flush
/* Upload staging, replay cmds; rebind state only on delta. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void nt_sprite_renderer_flush(void) {
    close_current_cmd();
    if (s_sprite.cmd_count == 0 || s_sprite.vertex_count == 0) {
        s_sprite.vertex_count = 0;
        s_sprite.index_count = 0;
        s_sprite.cmd_count = 0;
        return;
    }
#ifdef NT_TEST_ACCESS
    s_sprite.test_nonempty_flush_calls++;
#endif

    /* orphan_buffer asks the driver to allocate fresh storage instead of
     * mutating the bound VBO in place, so the GPU can keep consuming the
     * previous frame's draws while we stage the next one.
     *
     * staging is already interleaved at i*cur_stride, so flush uploads it directly. */
    NT_ASSERT((size_t)s_sprite.vertex_count * s_sprite.cur_stride <= s_sprite.staging_size && "sprite flush exceeds staging byte capacity");
    nt_gfx_orphan_buffer(s_sprite.vbo, s_sprite.staging, s_sprite.vertex_count * s_sprite.cur_stride);
    if (s_sprite.index_count > 0) {
        nt_gfx_orphan_buffer(s_sprite.ibo, s_sprite.indices, s_sprite.index_count * (uint32_t)sizeof(uint16_t));
    }

    nt_renderer_bound_t bound = {0};

    for (uint32_t ci = 0; ci < s_sprite.cmd_count; ci++) {
        const nt_sprite_draw_cmd_t *c = &s_sprite.cmds[ci];

        /* Context loss or program destruction can invalidate a queued pipeline;
         * the vertex input dies with the context or a buffer cascade. */
        if (!nt_gfx_pipeline_valid(c->pipeline) || !nt_gfx_vertex_input_valid(c->vertex_input)) {
            continue;
        }

        nt_renderer_bind_pipeline(&bound, c->pipeline);
        /* Geometry is one bind, independent of pipeline changes: two custom
         * layouts can share a pipeline and still switch vertex inputs here. */
        nt_renderer_bind_vertex_input(&bound, c->vertex_input);

        for (uint8_t t = 0; t < c->tex_count; t++) {
            /* nt_resource_set_placeholder_texture exists to keep slots resolvable
             * through async load races, so an unresolved slot is a developer bug. */
            NT_ASSERT((c->resolved_sampler[t].id != 0 || c->resolved_tex[t] != 0) && "sprite cmd slot has no resolved texture — register a placeholder via nt_resource_set_placeholder_texture");
        }

        /* Sampler units and params come from the cmd's captured hashes, so a cmd
         * whose material died still replays them. */
        const nt_material_info_t *mi = nt_material_get_info(c->material);
        const nt_renderer_material_view_t view = {
            .tex_count = c->tex_count,
            .tex_name_hashes = c->tex_name_hashes,
            .resolved_tex = c->resolved_tex,
            .resolved_sampler = c->resolved_sampler,
            .param_count = (mi != NULL) ? mi->param_count : 0,
            .param_name_hashes = (mi != NULL) ? mi->param_name_hashes : NULL,
            .params = (mi != NULL) ? mi->params : NULL,
        };
        nt_renderer_apply_material_uniforms(&bound, c->material.id, &view);
        /* Per cmd, not per material: one material's page can change on a page split. */
        nt_renderer_apply_texture_slots(&bound, &view);

        /* Per-cmd vertex delta — avoids stats inflation across state splits. */
        uint32_t cmd_vertex_end = (ci + 1U < s_sprite.cmd_count) ? s_sprite.cmds[ci + 1U].first_vertex : s_sprite.vertex_count;
        uint32_t cmd_vertex_count = cmd_vertex_end - c->first_vertex;
        nt_gfx_draw_indexed(c->first_index, c->index_count, cmd_vertex_count);
        s_sprite.last_draw_list_calls++;
    }

    s_sprite.vertex_count = 0;
    s_sprite.index_count = 0;
    s_sprite.cmd_count = 0;
    /* draw_list opens cmds per batch_key, not via current_mat. Reset
     * the fence so a following same-handle set_material() re-opens. */
    s_sprite.current_mat = (nt_material_t){0};
    s_sprite.current_program = NT_PROGRAM_INVALID;
}
// #endregion

// #region test accessors
#ifdef NT_TEST_ACCESS
void nt_sprite_renderer_test_layout(nt_material_t mat, nt_sprite_layout_info_t *out) {
    NT_ASSERT(out != NULL);
    const nt_material_info_t *mi = nt_material_get_info(mat);
    NT_ASSERT(mi != NULL && mi->program.id != 0);
    nt_vertex_layout_t layout = build_sprite_layout(mi);
    memset(out, 0, sizeof(*out));
    out->stride = layout.stride;
    out->attr_count = layout.attr_count;
    for (uint8_t i = 0; i < layout.attr_count && i < 16; i++) {
        out->locations[i] = layout.attrs[i].location;
        out->offsets[i] = layout.attrs[i].offset;
    }
}

void nt_sprite_renderer_test_last_emit_radial(uint32_t v_idx, float *out, uint8_t float_count) {
    NT_ASSERT(out != NULL);
    NT_ASSERT(v_idx < s_sprite.last_emit_vertex_count && "last_emit_radial: index out of range");
    NT_ASSERT((uint32_t)float_count * sizeof(float) <= NT_SPRITE_CUSTOM_STRIDE_MAX && "last_emit_radial: float_count exceeds custom stride");
    /* Custom block sits at +20 within each vertex's cur_stride slot in staging
     * (flush leaves cur_stride + staging data intact for readback). */
    const uint8_t *src = s_sprite.staging + ((size_t)(s_sprite.last_emit_first_vertex + v_idx) * s_sprite.cur_stride) + NT_SPRITE_BASE_STRIDE;
    memcpy(out, src, (size_t)float_count * sizeof(float));
}

uint32_t nt_sprite_renderer_test_pipeline_cache_count(void) { return s_sprite.count; }
uint32_t nt_sprite_renderer_test_vertex_input_cache_count(void) { return s_sprite.vi_count; }

uint32_t nt_sprite_renderer_test_cmd_count(void) { return s_sprite.cmd_count; }
uint32_t nt_sprite_renderer_test_draw_call_count(void) { return s_sprite.last_draw_list_calls; }
uint32_t nt_sprite_renderer_test_vertex_count(void) { return s_sprite.vertex_count; }
uint32_t nt_sprite_renderer_test_last_emit_vertex_count(void) { return s_sprite.last_emit_vertex_count; }
uint32_t nt_sprite_renderer_test_last_emit_index_count(void) { return s_sprite.last_emit_index_count; }

void nt_sprite_renderer_test_last_emit_position(uint32_t v_idx, float out[3]) {
    NT_ASSERT(v_idx < s_sprite.last_emit_vertex_count && "last_emit_position: index out of range");
    const nt_sprite_vertex_t *v = (const nt_sprite_vertex_t *)(s_sprite.staging + ((size_t)(s_sprite.last_emit_first_vertex + v_idx) * s_sprite.cur_stride));
    out[0] = v->position[0];
    out[1] = v->position[1];
    out[2] = v->position[2];
}

void nt_sprite_renderer_test_last_emit_texcoord(uint32_t v_idx, uint16_t out[2]) {
    NT_ASSERT(v_idx < s_sprite.last_emit_vertex_count && "last_emit_texcoord: index out of range");
    const nt_sprite_vertex_t *v = (const nt_sprite_vertex_t *)(s_sprite.staging + ((size_t)(s_sprite.last_emit_first_vertex + v_idx) * s_sprite.cur_stride));
    out[0] = v->texcoord[0];
    out[1] = v->texcoord[1];
}

void nt_sprite_renderer_test_last_emit_color(uint32_t v_idx, uint8_t out[4]) {
    NT_ASSERT(v_idx < s_sprite.last_emit_vertex_count && "last_emit_color: index out of range");
    const nt_sprite_vertex_t *v = (const nt_sprite_vertex_t *)(s_sprite.staging + ((size_t)(s_sprite.last_emit_first_vertex + v_idx) * s_sprite.cur_stride));
    out[0] = v->color[0];
    out[1] = v->color[1];
    out[2] = v->color[2];
    out[3] = v->color[3];
}

bool nt_sprite_renderer_test_initialized(void) { return s_sprite.initialized; }
uint32_t nt_sprite_renderer_test_last_slice9_vertex_count(void) { return s_sprite.last_slice9_vertex_count; }
uint32_t nt_sprite_renderer_test_last_slice9_index_count(void) { return s_sprite.last_slice9_index_count; }
uint32_t nt_sprite_renderer_test_nonempty_flush_calls(void) { return s_sprite.test_nonempty_flush_calls; }
void nt_sprite_renderer_test_reset_nonempty_flush_calls(void) { s_sprite.test_nonempty_flush_calls = 0; }
#endif
// #endregion
