#include "renderers/nt_sprite_renderer.h"

#include <string.h>

#include "atlas/nt_atlas.h"
#include "core/nt_assert.h"
#include "drawable_comp/nt_drawable_comp.h"
#include "graphics/nt_gfx.h"
#include "log/nt_log.h"
#include "material/nt_material.h"
#include "material_comp/nt_material_comp.h"
#include "render/nt_render_defs.h"
#include "sprite_comp/nt_sprite_comp.h"
#include "transform_comp/nt_transform_comp.h"

// #region module state
typedef struct {
    uint64_t key;
    nt_pipeline_t pipeline;
} nt_sprite_pipeline_entry_t;

static struct {
    bool initialized;
    uint16_t max_pipelines;
    nt_sprite_pipeline_entry_t entries[NT_SPRITE_RENDERER_MAX_PIPELINES_HARDCAP];
    uint16_t count;

    nt_buffer_t vbo; /* dynamic, sized for NT_SPRITE_RENDERER_MAX_VERTICES * 24 */
    nt_buffer_t ibo; /* dynamic, sized for NT_SPRITE_RENDERER_MAX_INDICES * 2 */
    nt_sprite_vertex_t vertices[NT_SPRITE_RENDERER_MAX_VERTICES];
    uint16_t indices[NT_SPRITE_RENDERER_MAX_INDICES];
    uint32_t vertex_count;
    uint32_t index_count;

    /* Per-batch state (set inside draw_list, consumed by flush) */
    nt_pipeline_t current_pipeline;
    uint32_t frame_draw_calls; /* test counter; SEPARATE from nt_gfx_get_frame_draw_calls per CONTEXT D-39 */
#ifdef NT_SPRITE_RENDERER_TEST_ACCESS
    /* Test-only: last emit_one() vertex/index counts captured BEFORE flush
     * resets s_sprite.vertex_count. Read by polygon-emit test to verify
     * region.vertex_count==N polygons emit N vertices (Issue 7 fix). */
    uint32_t last_emit_vertex_count;
    uint32_t last_emit_index_count;
#endif
} s_sprite;
// #endregion

// #region pack helper
static inline uint8_t pack_u8(float c) {
    if (c <= 0.0F) {
        return 0;
    }
    if (c >= 1.0F) {
        return 255;
    }
    return (uint8_t)((c * 255.0F) + 0.5F);
}
// #endregion

// #region lifecycle
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
nt_result_t nt_sprite_renderer_init(const nt_sprite_renderer_desc_t *desc) {
    NT_ASSERT(!s_sprite.initialized);
    nt_sprite_renderer_desc_t d = (desc != NULL) ? *desc : nt_sprite_renderer_desc_defaults();
    NT_ASSERT(d.max_pipelines > 0 && d.max_pipelines <= NT_SPRITE_RENDERER_MAX_PIPELINES_HARDCAP);

    memset(&s_sprite, 0, sizeof(s_sprite));
    s_sprite.max_pipelines = d.max_pipelines;

    /* Dynamic VBO and IBO. Pattern: nt_text_renderer.c init code. */
    s_sprite.vbo = nt_gfx_make_buffer(&(nt_buffer_desc_t){
        .type = NT_BUFFER_VERTEX,
        .usage = NT_USAGE_DYNAMIC,
        .size = NT_SPRITE_RENDERER_MAX_VERTICES * (uint32_t)sizeof(nt_sprite_vertex_t),
        .label = "sprite_vbo",
    });
    NT_ASSERT(s_sprite.vbo.id != 0);

    s_sprite.ibo = nt_gfx_make_buffer(&(nt_buffer_desc_t){
        .type = NT_BUFFER_INDEX,
        .usage = NT_USAGE_DYNAMIC,
        .size = NT_SPRITE_RENDERER_MAX_INDICES * (uint32_t)sizeof(uint16_t),
        .index_type = NT_INDEX_UINT16,
        .label = "sprite_ibo",
    });
    NT_ASSERT(s_sprite.ibo.id != 0);

    s_sprite.initialized = true;
    return NT_OK;
}

void nt_sprite_renderer_shutdown(void) {
    if (!s_sprite.initialized) {
        return;
    }
    /* Destroy pipelines in cache */
    for (uint16_t i = 0; i < s_sprite.count; i++) {
        nt_gfx_destroy_pipeline(s_sprite.entries[i].pipeline);
        s_sprite.entries[i] = (nt_sprite_pipeline_entry_t){0};
    }
    s_sprite.count = 0;
    nt_gfx_destroy_buffer(s_sprite.vbo);
    nt_gfx_destroy_buffer(s_sprite.ibo);
    memset(&s_sprite, 0, sizeof(s_sprite));
}

void nt_sprite_renderer_restore_gpu(void) {
    if (!s_sprite.initialized) {
        return;
    }
    uint16_t saved_pip = s_sprite.max_pipelines;
    nt_sprite_renderer_shutdown();
    nt_sprite_renderer_desc_t desc = {.max_pipelines = saved_pip};
    NT_ASSERT(nt_sprite_renderer_init(&desc) == NT_OK);
}
// #endregion

// #region pipeline cache
/* Build the fixed sprite vertex layout once — D-16 locks 24-byte stride.
 * Uses NT_ATTR_POSITION/COLOR/TEXCOORD0 location enum so the sprite vertex
 * shader can declare matching layout(location=N) bindings. */
static nt_vertex_layout_t s_sprite_layout = {
    .stride = 24,
    .attr_count = 3,
    .attrs =
        {
            {.location = NT_ATTR_POSITION, .format = NT_FORMAT_FLOAT3, .offset = 0},
            {.location = NT_ATTR_TEXCOORD0, .format = NT_FORMAT_FLOAT2, .offset = 12},
            {.location = NT_ATTR_COLOR, .format = NT_FORMAT_UBYTE4N, .offset = 20},
        },
};

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static nt_pipeline_t find_or_create_pipeline(const nt_material_info_t *mat_info) {
    /* Pipeline signature: vs/fs handles + render-state bits.
     * Layout omitted from key — sprite vertex layout is fixed by D-16. */
    uint32_t state_bits = ((uint32_t)mat_info->blend_mode) | ((uint32_t)mat_info->depth_test << 4) | ((uint32_t)mat_info->depth_write << 5) | ((uint32_t)mat_info->cull_mode << 6) |
                          ((uint32_t)mat_info->color_mode << 8);
    uint64_t key = 0;
    key = key * 0x9E3779B97F4A7C15ULL + mat_info->resolved_vs;
    key = key * 0x9E3779B97F4A7C15ULL + mat_info->resolved_fs;
    key = key * 0x9E3779B97F4A7C15ULL + state_bits;

    /* Linear scan for cached entry */
    for (uint16_t i = 0; i < s_sprite.count; i++) {
        if (s_sprite.entries[i].key == key) {
            return s_sprite.entries[i].pipeline;
        }
    }

    /* Miss — create. Cache full is a configuration bug, not a runtime
     * recovery case (D-19). */
    NT_ASSERT(s_sprite.count < s_sprite.max_pipelines && "sprite pipeline cache exhausted; raise NT_SPRITE_RENDERER_MAX_PIPELINES or desc.max_pipelines");

    nt_pipeline_desc_t desc;
    memset(&desc, 0, sizeof(desc));
    desc.vertex_shader = (nt_shader_t){.id = mat_info->resolved_vs};
    desc.fragment_shader = (nt_shader_t){.id = mat_info->resolved_fs};
    desc.layout = s_sprite_layout;
    desc.depth_test = mat_info->depth_test;
    desc.depth_write = mat_info->depth_write;
    desc.depth_func = NT_DEPTH_LESS;
    desc.blend = (mat_info->blend_mode == NT_BLEND_MODE_ALPHA);
    if (desc.blend) {
        /* Premultiplied-alpha blend (D-24): material with NT_BLEND_MODE_ALPHA
         * pairs with builder-side premultiplication. nt_text_renderer uses the
         * same (ONE, ONE_MINUS_SRC_ALPHA) recipe. */
        desc.blend_src = NT_BLEND_ONE;
        desc.blend_dst = NT_BLEND_ONE_MINUS_SRC_ALPHA;
    }
    desc.cull_mode = (uint8_t)mat_info->cull_mode;
    desc.label = (mat_info->label != NULL) ? mat_info->label : "sprite_pipeline";

    nt_pipeline_t pip = nt_gfx_make_pipeline(&desc);
    NT_ASSERT(pip.id != 0);

    s_sprite.entries[s_sprite.count].key = key;
    s_sprite.entries[s_sprite.count].pipeline = pip;
    s_sprite.count++;
    return pip;
}
// #endregion

// #region emit_one (per-sprite vertex/index emit)
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void emit_one(const nt_render_item_t *item) {
    nt_entity_t e = {.id = item->entity};

    /* Resolve atlas + region — sprite_comp guarantees atlas/region_index for
     * resolved sprites. Tombstone (vertex_count==0) → zero-draw early-out. */
    nt_resource_t atlas = *nt_sprite_comp_atlas(e);
    uint16_t ridx = *nt_sprite_comp_region_index(e);
    const nt_texture_region_t *r = nt_atlas_get_region(atlas, ridx);
    if (r == NULL || r->vertex_count == 0) {
        return;
    }

    const float(*cpos)[2] = nt_atlas_get_region_cached_pos(atlas, ridx);
    const float(*cuv)[2] = nt_atlas_get_region_cached_uv(atlas, ridx);
    const uint16_t *idx = nt_atlas_get_region_indices(atlas, ridx);
    NT_ASSERT(cpos != NULL && cuv != NULL && idx != NULL);

    /* Capacity guard — flush if this emit would overflow staging (auto-flush
     * mid-run is fine; the same pipeline is rebound on the next emit). */
    if (s_sprite.vertex_count + r->vertex_count > NT_SPRITE_RENDERER_MAX_VERTICES || s_sprite.index_count + r->index_count > NT_SPRITE_RENDERER_MAX_INDICES) {
        nt_sprite_renderer_flush();
    }

    /* Model matrix + origin override (D-07). Override path is cold for the
     * Bunnymark default — branch is predictable per-batch. */
    const float *m = nt_transform_comp_world_matrix(e);
    float local_model[16];
    uint8_t flags = *nt_sprite_comp_flags(e);
    if (flags & NT_SPRITE_FLAG_ORIGIN_OV) {
        const float *o = nt_sprite_comp_origin(e);
        float ppu = nt_atlas_get_pixels_per_unit(atlas);
        float ipu = (ppu > 0.0F) ? 1.0F / ppu : 1.0F;
        float dx = (o[0] - r->origin_x) * (float)r->source_w * ipu;
        float dy = (o[1] - r->origin_y) * (float)r->source_h * ipu;
        memcpy(local_model, m, 64);
        local_model[12] -= (local_model[0] * dx) + (local_model[4] * dy);
        local_model[13] -= (local_model[1] * dx) + (local_model[5] * dy);
        local_model[14] -= (local_model[2] * dx) + (local_model[6] * dy);
        m = local_model;
    }

    /* Color (DrawableComponent) → packed RGBA8 */
    const float *cf = nt_drawable_comp_color(e);
    uint8_t cr = pack_u8(cf[0]);
    uint8_t cg = pack_u8(cf[1]);
    uint8_t cb = pack_u8(cf[2]);
    uint8_t ca = pack_u8(cf[3]);

    /* Flip flags (D-09) — per-vertex negate of cached_pos */
    bool fx = (flags & NT_SPRITE_FLAG_FLIP_X) != 0;
    bool fy = (flags & NT_SPRITE_FLAG_FLIP_Y) != 0;

    /* Emit vertices (uniform rect/polygon path, D-08) */
    uint32_t base = s_sprite.vertex_count;
    for (uint8_t i = 0; i < r->vertex_count; i++) {
        float px = cpos[i][0];
        if (fx) {
            px = -px;
        }
        float py = cpos[i][1];
        if (fy) {
            py = -py;
        }
        nt_sprite_vertex_t *v = &s_sprite.vertices[base + i];
        v->position[0] = (m[0] * px) + (m[4] * py) + m[12];
        v->position[1] = (m[1] * px) + (m[5] * py) + m[13];
        v->position[2] = (m[2] * px) + (m[6] * py) + m[14];
        v->texcoord[0] = cuv[i][0];
        v->texcoord[1] = cuv[i][1];
        v->color[0] = cr;
        v->color[1] = cg;
        v->color[2] = cb;
        v->color[3] = ca;
    }
    s_sprite.vertex_count += r->vertex_count;

    /* Emit indices (rebase to staging base) */
    for (uint8_t i = 0; i < r->index_count; i++) {
        s_sprite.indices[s_sprite.index_count + i] = (uint16_t)(base + idx[i]);
    }
    s_sprite.index_count += r->index_count;

#ifdef NT_SPRITE_RENDERER_TEST_ACCESS
    /* Capture per-emit counts so polygon test can read them after draw_list
     * completes (flush resets vertex_count). Fields under same guard in
     * s_sprite struct (Task 1). */
    s_sprite.last_emit_vertex_count = r->vertex_count;
    s_sprite.last_emit_index_count = r->index_count;
#endif
}
// #endregion

// #region draw_list
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void nt_sprite_renderer_draw_list(const nt_render_item_t *items, uint32_t count) {
    NT_ASSERT(s_sprite.initialized);
    if (count == 0) {
        return;
    }

    /* Reset per-frame counter (per-renderer test counter — distinct from
     * nt_gfx_get_frame_draw_calls, which is reset by nt_gfx_begin_frame). */
    s_sprite.frame_draw_calls = 0;

    bool first_group = true;
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
        if (mat_info == NULL || !mat_info->ready || mat_info->resolved_vs == 0 || mat_info->resolved_fs == 0) {
            /* Material not yet ready — skip the run silently (legitimate
             * runtime state, not a bug). */
            run_start = run_end;
            continue;
        }

        /* D-18: flush at every batch_key boundary (one nt_gfx_draw_indexed per
         * batch group). This is the contract — even when consecutive groups
         * resolve to the same cached pipeline, the game has signalled they
         * are not state-compatible by giving them different batch_keys. */
        if (!first_group && s_sprite.vertex_count > 0) {
            nt_sprite_renderer_flush();
        }
        first_group = false;

        nt_pipeline_t pip = find_or_create_pipeline(mat_info);
        if (s_sprite.current_pipeline.id != pip.id) {
            nt_gfx_bind_pipeline(pip);
            s_sprite.current_pipeline = pip;

            /* Bind first material texture (atlas page) — sprite shader uses
             * a single sampler. Mirrors nt_mesh_renderer texture binding. */
            for (uint8_t t = 0; t < mat_info->tex_count; t++) {
                if (mat_info->resolved_tex[t] != 0) {
                    nt_gfx_bind_texture((nt_texture_t){.id = mat_info->resolved_tex[t]}, t);
                    if (mat_info->tex_names[t] != NULL) {
                        nt_gfx_set_uniform_int(mat_info->tex_names[t], (int)t);
                    }
                }
            }
        }

        for (uint32_t i = run_start; i < run_end; i++) {
            emit_one(&items[i]);
        }
        run_start = run_end;
    }
    nt_sprite_renderer_flush();
    s_sprite.current_pipeline = (nt_pipeline_t){0};
}
// #endregion

// #region flush
void nt_sprite_renderer_flush(void) {
    if (s_sprite.vertex_count == 0) {
        return;
    }
    nt_gfx_update_buffer(s_sprite.vbo, s_sprite.vertices, s_sprite.vertex_count * (uint32_t)sizeof(nt_sprite_vertex_t));
    nt_gfx_update_buffer(s_sprite.ibo, s_sprite.indices, s_sprite.index_count * (uint32_t)sizeof(uint16_t));
    nt_gfx_bind_vertex_buffer(s_sprite.vbo);
    nt_gfx_bind_index_buffer(s_sprite.ibo);
    nt_gfx_draw_indexed(0, s_sprite.index_count, s_sprite.vertex_count);
    s_sprite.vertex_count = 0;
    s_sprite.index_count = 0;
    s_sprite.frame_draw_calls++;
}
// #endregion

// #region test accessors
#ifdef NT_SPRITE_RENDERER_TEST_ACCESS
uint32_t nt_sprite_renderer_test_pipeline_cache_count(void) { return s_sprite.count; }
uint32_t nt_sprite_renderer_test_draw_call_count(void) { return s_sprite.frame_draw_calls; }
uint32_t nt_sprite_renderer_test_vertex_count(void) { return s_sprite.vertex_count; }
uint32_t nt_sprite_renderer_test_last_emit_vertex_count(void) { return s_sprite.last_emit_vertex_count; }
uint32_t nt_sprite_renderer_test_last_emit_index_count(void) { return s_sprite.last_emit_index_count; }
bool nt_sprite_renderer_test_initialized(void) { return s_sprite.initialized; }
#endif
// #endregion
