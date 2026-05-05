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

/* Per-state-change draw command. emit_one writes vertices/indices into shared
 * staging buffers; draw_list opens/closes commands at batch_key boundaries.
 * flush() uploads VBO+IBO once, then replays each cmd with bind+draw. */
typedef struct {
    nt_pipeline_t pipeline;
    uint32_t resolved_tex[NT_MATERIAL_MAX_TEXTURES];
    const char *tex_names[NT_MATERIAL_MAX_TEXTURES];
    uint8_t tex_count;
    uint32_t first_index; /* offset into s_sprite.indices[] */
    uint32_t index_count;
} nt_sprite_draw_cmd_t;

static struct {
    bool initialized;
    uint16_t max_pipelines;
    nt_sprite_pipeline_entry_t entries[NT_SPRITE_RENDERER_MAX_PIPELINES_HARDCAP];
    uint16_t count;

    nt_buffer_t vbo; /* dynamic, sized for NT_SPRITE_RENDERER_MAX_VERTICES * 24 */
    nt_buffer_t ibo; /* dynamic, sized for NT_SPRITE_RENDERER_MAX_INDICES * 4 (uint32 indices) */
    nt_sprite_vertex_t vertices[NT_SPRITE_RENDERER_MAX_VERTICES];
    uint32_t indices[NT_SPRITE_RENDERER_MAX_INDICES];
    uint32_t vertex_count;
    uint32_t index_count;

    /* Recorded per-state draw commands. Last entry is the "currently open"
     * cmd that emit_one writes into; closed by close_current_cmd() before a
     * new state is pushed or before flush(). */
    nt_sprite_draw_cmd_t cmds[NT_SPRITE_RENDERER_MAX_DRAW_CMDS];
    uint32_t cmd_count;

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
        .size = NT_SPRITE_RENDERER_MAX_INDICES * (uint32_t)sizeof(uint32_t),
        .index_type = NT_INDEX_UINT32,
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
static void open_cmd(nt_pipeline_t pip, const nt_material_info_t *mi) {
    NT_ASSERT(s_sprite.cmd_count < NT_SPRITE_RENDERER_MAX_DRAW_CMDS && "sprite draw-cmd queue full; raise NT_SPRITE_RENDERER_MAX_DRAW_CMDS");
    nt_sprite_draw_cmd_t *c = &s_sprite.cmds[s_sprite.cmd_count++];
    c->pipeline = pip;
    c->tex_count = mi->tex_count;
    for (uint8_t i = 0; i < mi->tex_count; i++) {
        c->resolved_tex[i] = mi->resolved_tex[i];
        c->tex_names[i] = mi->tex_names[i];
    }
    c->first_index = s_sprite.index_count;
    c->index_count = 0;
}

/* Re-open a cmd with state copied from another cmd. Used by emit_one's
 * overflow recovery path: snapshot the current cmd's state before flush()
 * resets cmd_count, then re-open with first_index=0 in the fresh staging. */
static void open_cmd_from_snapshot(const nt_sprite_draw_cmd_t *snap) {
    NT_ASSERT(s_sprite.cmd_count < NT_SPRITE_RENDERER_MAX_DRAW_CMDS);
    nt_sprite_draw_cmd_t *c = &s_sprite.cmds[s_sprite.cmd_count++];
    *c = *snap;
    c->first_index = s_sprite.index_count;
    c->index_count = 0;
}
// #endregion

// #region emit_one (per-sprite vertex/index emit)
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void emit_one(const nt_render_item_t *item) {
    nt_entity_t e = {.id = item->entity};

    /* Resolve atlas + region — sprite_comp guarantees atlas/region_index for
     * resolved sprites. Single combined accessor: one user_data lookup +
     * one bounds check, returns all four pointers. Tombstone
     * (vertex_count==0) → zero-draw early-out. */
    nt_resource_t atlas = *nt_sprite_comp_atlas(e);
    uint16_t ridx = *nt_sprite_comp_region_index(e);
    nt_atlas_region_view_t view = nt_atlas_get_region_view(atlas, ridx);
    if (view.region == NULL || view.region->vertex_count == 0) {
        return;
    }
    const nt_texture_region_t *r = view.region;
    const float(*cpos)[2] = view.cached_pos;
    const float(*cuv)[2] = view.cached_uv;
    const uint16_t *idx = view.indices;
    NT_ASSERT(cpos != NULL && cuv != NULL && idx != NULL);

    /* Capacity guard — if this sprite would overflow staging, snapshot the
     * open cmd's state, flush() (uploads + replays cmds + resets), then
     * re-open a fresh cmd with the same state at first_index=0. The caller
     * sees no state change; the GPU just gets two flushes instead of one. */
    if (s_sprite.vertex_count + r->vertex_count > NT_SPRITE_RENDERER_MAX_VERTICES || s_sprite.index_count + r->index_count > NT_SPRITE_RENDERER_MAX_INDICES) {
        NT_ASSERT(s_sprite.cmd_count > 0 && "emit_one called with no open cmd");
        nt_sprite_draw_cmd_t snapshot = s_sprite.cmds[s_sprite.cmd_count - 1];
        nt_sprite_renderer_flush();
        open_cmd_from_snapshot(&snapshot);
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

    /* Emit indices (rebase to staging base; uint32 leaves headroom for big
     * VBOs without per-cmd splits). */
    for (uint8_t i = 0; i < r->index_count; i++) {
        s_sprite.indices[s_sprite.index_count + i] = base + (uint32_t)idx[i];
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
/* Defold-style two-phase pipeline (D-18 semantics preserved):
 *
 *   Phase 1 (this function): walk run boundaries, open a draw cmd per
 *   batch_key with its pipeline + texture state, append per-sprite vertices
 *   into shared staging via emit_one.
 *
 *   Phase 2 (nt_sprite_renderer_flush, called once at the end): single VBO
 *   + IBO upload, then iterate cmds binding state once per cmd and issuing
 *   one nt_gfx_draw_indexed with byte offsets. emit_one auto-flushes
 *   mid-list when staging overflows and re-opens the same cmd state, so
 *   the contract still gives N flushes for N batch_keys in the common case
 *   (= 1 for bunnymark with 60k uniformly-keyed sprites).
 *
 *   Why N draws and not 1: between cmds the GPU may need a new pipeline
 *   binding (different shader/blend/depth) or a new texture binding, which
 *   must happen between draw calls in WebGL2/GL. Cmds inside flush() bind
 *   only what changed (pipeline & texture id tracked since previous cmd). */
void nt_sprite_renderer_draw_list(const nt_render_item_t *items, uint32_t count) {
    NT_ASSERT(s_sprite.initialized);
    if (count == 0) {
        return;
    }

    /* Reset per-frame counter (per-renderer test counter — distinct from
     * nt_gfx_get_frame_draw_calls, which is reset by nt_gfx_begin_frame). */
    s_sprite.frame_draw_calls = 0;

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

        /* D-18: every batch_key boundary opens a fresh cmd. Even when the
         * resolved pipeline matches the previous cmd, the game has signalled
         * the runs are not state-compatible — keep them as separate draws
         * so flush() can re-bind whatever the game needs. */
        nt_pipeline_t pip = find_or_create_pipeline(mat_info);
        close_current_cmd();
        open_cmd(pip, mat_info);

        for (uint32_t i = run_start; i < run_end; i++) {
            emit_one(&items[i]);
        }
        run_start = run_end;
    }
    nt_sprite_renderer_flush();
}
// #endregion

// #region flush
/* Phase 2 of the Defold-style pipeline: single VBO + IBO upload, then replay
 * recorded draw cmds with state binding only when it changes between cmds.
 * Resets all staging counters at the end so the next draw_list / external
 * caller starts clean. Idempotent when nothing was emitted. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void nt_sprite_renderer_flush(void) {
    /* Closing the open cmd is safe here (no-op if cmd_count==0). */
    close_current_cmd();
    if (s_sprite.cmd_count == 0 || s_sprite.vertex_count == 0) {
        s_sprite.vertex_count = 0;
        s_sprite.index_count = 0;
        s_sprite.cmd_count = 0;
        return;
    }

    /* Single upload for the whole frame's worth of geometry — vs the previous
     * implementation that did one update_buffer per 4096-sprite flush
     * (16 uploads on 60k bunnies), each potentially stalling the WebGL
     * driver's dynamic VBO. orphan_buffer hints the driver to allocate fresh
     * storage on every rewrite so the GPU can keep consuming the previous
     * frame while we're staging the next one. */
    nt_gfx_orphan_buffer(s_sprite.vbo, s_sprite.vertices, s_sprite.vertex_count * (uint32_t)sizeof(nt_sprite_vertex_t));
    nt_gfx_orphan_buffer(s_sprite.ibo, s_sprite.indices, s_sprite.index_count * (uint32_t)sizeof(uint32_t));

    uint32_t bound_pipeline_id = 0;
    uint32_t bound_tex_ids[NT_MATERIAL_MAX_TEXTURES] = {0};

    for (uint32_t ci = 0; ci < s_sprite.cmd_count; ci++) {
        const nt_sprite_draw_cmd_t *c = &s_sprite.cmds[ci];

        if (c->pipeline.id != bound_pipeline_id) {
            /* GL ordering: each pipeline owns a VAO, and GL_ELEMENT_ARRAY_BUFFER
             * is part of VAO state. So pipeline → VBO (re-applies attribs into
             * the new VAO) → IBO (binds the EBO into the new VAO). Reordering
             * any of these breaks the next draw silently. */
            nt_gfx_bind_pipeline(c->pipeline);
            nt_gfx_bind_vertex_buffer(s_sprite.vbo);
            nt_gfx_bind_index_buffer(s_sprite.ibo);
            bound_pipeline_id = c->pipeline.id;
        }

        for (uint8_t t = 0; t < c->tex_count; t++) {
            if (c->resolved_tex[t] != 0 && c->resolved_tex[t] != bound_tex_ids[t]) {
                nt_gfx_bind_texture((nt_texture_t){.id = c->resolved_tex[t]}, t);
                if (c->tex_names[t] != NULL) {
                    nt_gfx_set_uniform_int(c->tex_names[t], (int)t);
                }
                bound_tex_ids[t] = c->resolved_tex[t];
            }
        }

        nt_gfx_draw_indexed(c->first_index, c->index_count, s_sprite.vertex_count);
        s_sprite.frame_draw_calls++;
    }

    s_sprite.vertex_count = 0;
    s_sprite.index_count = 0;
    s_sprite.cmd_count = 0;
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
