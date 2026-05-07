#include "renderers/nt_sprite_renderer.h"

#include <stdint.h>
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
#include "sprite_comp/nt_sprite_comp.h"
#include "transform_comp/nt_transform_comp.h"

// #region module state
typedef struct {
    uint64_t key;
    nt_pipeline_t pipeline;
} nt_sprite_pipeline_entry_t;

typedef struct {
    nt_pipeline_t pipeline;
    uint32_t resolved_tex[NT_MATERIAL_MAX_TEXTURES];
    const char *tex_names[NT_MATERIAL_MAX_TEXTURES];
    nt_sampler_t resolved_sampler[NT_MATERIAL_MAX_TEXTURES]; /* per-binding override, .id==0 keeps texture default */
    uint8_t tex_count;
    uint32_t first_index; /* offset into s_sprite.indices[] */
    uint32_t index_count;
    uint32_t first_vertex; /* offset into s_sprite.vertices[] — for per-cmd vertex stats */
} nt_sprite_draw_cmd_t;

static struct {
    bool initialized;
    uint16_t max_pipelines;
    nt_sprite_pipeline_entry_t entries[NT_SPRITE_RENDERER_MAX_PIPELINES_HARDCAP];
    uint16_t count;

    nt_buffer_t vbo; /* dynamic, sized for NT_SPRITE_RENDERER_MAX_VERTICES * 24 */
    nt_buffer_t ibo; /* dynamic, sized for NT_SPRITE_RENDERER_MAX_INDICES * 2 (uint16 indices) */
    nt_sprite_vertex_t vertices[NT_SPRITE_RENDERER_MAX_VERTICES];
    uint16_t indices[NT_SPRITE_RENDERER_MAX_INDICES];
    uint32_t vertex_count;
    uint32_t index_count;

    /* Recorded per-state draw commands. Last entry is the "currently open"
     * cmd that emit_one writes into; closed by close_current_cmd() before a
     * new state is pushed or before flush(). */
    nt_sprite_draw_cmd_t cmds[NT_SPRITE_RENDERER_MAX_DRAW_CMDS];
    uint32_t cmd_count;

    uint32_t frame_draw_calls; /* test counter; SEPARATE from nt_gfx_get_frame_draw_calls */
#ifdef NT_SPRITE_RENDERER_TEST_ACCESS
    /* Test-only: last emit_one() vertex/index counts captured BEFORE flush
     * resets s_sprite.vertex_count. Read by polygon-emit test to verify
     * region.vertex_count==N polygons emit N vertices. */
    uint32_t last_emit_vertex_count;
    uint32_t last_emit_index_count;
#endif
} s_sprite;
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
/* Build the fixed sprite vertex layout once — 24-byte stride is locked.
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
     * Layout omitted from key — sprite vertex layout is fixed. */
    uint64_t key = 0;
    key = key * 0x9E3779B97F4A7C15ULL + mat_info->resolved_vs;
    key = key * 0x9E3779B97F4A7C15ULL + mat_info->resolved_fs;
    key = key * 0x9E3779B97F4A7C15ULL + nt_material_state_bits(mat_info);

    /* Linear scan for cached entry */
    for (uint16_t i = 0; i < s_sprite.count; i++) {
        if (s_sprite.entries[i].key == key) {
            return s_sprite.entries[i].pipeline;
        }
    }

    /* Miss — create. Cache full is a configuration bug, not a runtime
     * recovery case. */
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
        /* Premultiplied-alpha blend: material with NT_BLEND_MODE_ALPHA
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
    if (s_sprite.cmd_count >= NT_SPRITE_RENDERER_MAX_DRAW_CMDS) {
        nt_sprite_renderer_flush();
    }
    NT_ASSERT(s_sprite.cmd_count < NT_SPRITE_RENDERER_MAX_DRAW_CMDS && "sprite draw-cmd queue full; raise NT_SPRITE_RENDERER_MAX_DRAW_CMDS");
    nt_sprite_draw_cmd_t *c = &s_sprite.cmds[s_sprite.cmd_count++];
    memset(c, 0, sizeof(*c)); /* slots reuse across frames; clear stale fields */
    c->pipeline = pip;
    c->tex_count = mi->tex_count;
    for (uint8_t i = 0; i < mi->tex_count; i++) {
        c->resolved_tex[i] = mi->resolved_tex[i];
        c->tex_names[i] = mi->tex_names[i];
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
    /* Lazy slot 0 inject — see draw_list contract in nt_sprite_renderer.h. */
    if (c->tex_count == 0) {
        c->tex_count = 1;
        c->tex_names[0] = NULL;
        c->resolved_sampler[0] = NT_SAMPLER_INVALID;
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

// #region emit_one (per-sprite vertex/index emit)
/* Caller passes pre-fetched SoA views; sparse_indices[entity_index] → dense slot. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void emit_one(const nt_render_item_t *item, const nt_sprite_comp_view_t *sv, const nt_transform_comp_view_t *tv, const nt_drawable_comp_view_t *dv) {
    nt_entity_t e = {.id = item->entity};
    uint16_t eidx = nt_entity_index(e);

    /* Inline sparse->dense lookups — three array reads vs three function
     * calls each doing a liveness assert + the same lookup. */
    uint16_t s_idx = sv->sparse_indices[eidx];
    uint16_t t_idx = tv->sparse_indices[eidx];
    uint16_t d_idx = dv->sparse_indices[eidx];

    /* AGENTS.md "fail early, prefer asserts": dense indices must be valid.
     * NT_INVALID_COMP_INDEX (0xFFFF) would index SoA arrays out of bounds.
     * Catches stale render items, items for entities missing a component,
     * items built after entity destruction. Compiles out in release. */
    NT_ASSERT(s_idx != NT_INVALID_COMP_INDEX && "sprite render item: entity has no sprite component");
    NT_ASSERT(t_idx != NT_INVALID_COMP_INDEX && "sprite render item: entity has no transform component");
    NT_ASSERT(d_idx != NT_INVALID_COMP_INDEX && "sprite render item: entity has no drawable component");

    /* Resolve atlas + region. Tombstone → zero-draw early-out. */
    nt_resource_t atlas = sv->atlas[s_idx];
    const nt_sprite_resolved_region_t *resolved = &sv->resolved[s_idx];
    if ((sv->flags[s_idx] & NT_SPRITE_FLAG_RESOLVED) == 0 || resolved->region == NULL || resolved->region->vertex_count == 0) {
        return;
    }
    const nt_texture_region_t *r = resolved->region;
    const float(*cpos)[2] = resolved->cached_pos;
    const float(*cuv)[2] = resolved->cached_uv;
    const uint16_t *idx = resolved->indices;
    NT_ASSERT(cpos != NULL && cuv != NULL && idx != NULL);
    uint32_t page_tex = nt_resource_get(resolved->page_resource);
    if (!ensure_current_cmd_page_texture(page_tex)) {
        return;
    }

    /* Capacity guard: if this sprite would overflow staging, or exceed the
     * uint16 indexable vertex range, snapshot the open cmd's state, flush()
     * (uploads + replays cmds + resets), then re-open a fresh cmd with the
     * same state at first_index=0. The caller sees no state change; the GPU
     * just gets another chunk draw instead of UNSIGNED_INT indices. */
    if (s_sprite.vertex_count + r->vertex_count > NT_SPRITE_RENDERER_MAX_VERTICES || s_sprite.index_count + r->index_count > NT_SPRITE_RENDERER_MAX_INDICES) {
        NT_ASSERT(s_sprite.cmd_count > 0 && "emit_one called with no open cmd");
        nt_sprite_draw_cmd_t snapshot = s_sprite.cmds[s_sprite.cmd_count - 1];
        nt_sprite_renderer_flush();
        open_cmd_from_snapshot(&snapshot);
    }

    /* Origin override: bake -m*delta into tx/ty/tz, no matrix copy. */
    const float *m = tv->world_matrices[t_idx];
    float tx = m[12];
    float ty = m[13];
    float tz = m[14];
    uint8_t flags = sv->flags[s_idx];
    if (flags & NT_SPRITE_FLAG_ORIGIN_OV) {
        const float *o = sv->origin[s_idx];
        /* get_pixels_per_unit asserts ppu > 0, so direct invert is safe. */
        float ipu = 1.0F / nt_atlas_get_pixels_per_unit(atlas);
        float dx = (o[0] - r->origin_x) * (float)r->source_w * ipu;
        float dy = (o[1] - r->origin_y) * (float)r->source_h * ipu;
        tx -= (m[0] * dx) + (m[4] * dy);
        ty -= (m[1] * dx) + (m[5] * dy);
        tz -= (m[2] * dx) + (m[6] * dy);
    }

    uint32_t color32 = dv->colors_packed[d_idx];
    uint8_t cr = (uint8_t)(color32 & 0xFFU);
    uint8_t cg = (uint8_t)((color32 >> 8) & 0xFFU);
    uint8_t cb = (uint8_t)((color32 >> 16) & 0xFFU);
    uint8_t ca = (uint8_t)((color32 >> 24) & 0xFFU);

    /* Flip flags — per-vertex negate of cached_pos */
    bool fx = (flags & NT_SPRITE_FLAG_FLIP_X) != 0;
    bool fy = (flags & NT_SPRITE_FLAG_FLIP_Y) != 0;

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
            nt_sprite_vertex_t *v = &s_sprite.vertices[base + i];
            v->position[0] = xs_arr[i];
            v->position[1] = ys_arr[i];
            v->position[2] = zs_arr[i];
            v->texcoord[0] = cuv[i][0];
            v->texcoord[1] = cuv[i][1];
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
            nt_sprite_vertex_t *v = &s_sprite.vertices[base + i];
            v->position[0] = (m[0] * px) + (m[4] * py) + tx;
            v->position[1] = (m[1] * px) + (m[5] * py) + ty;
            v->position[2] = (m[2] * px) + (m[6] * py) + tz;
            v->texcoord[0] = cuv[i][0];
            v->texcoord[1] = cuv[i][1];
            v->color[0] = cr;
            v->color[1] = cg;
            v->color[2] = cb;
            v->color[3] = ca;
        }
    }
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
/* Phase 1 of the two-phase pipeline: open a cmd per batch_key,
 * stream verts into staging via emit_one. Phase 2 = nt_sprite_renderer_flush. */
void nt_sprite_renderer_draw_list(const nt_render_item_t *items, uint32_t count) {
    NT_ASSERT(s_sprite.initialized);
    if (count == 0) {
        return;
    }

    s_sprite.frame_draw_calls = 0;
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
        if (mat_info == NULL || !mat_info->ready || mat_info->resolved_vs == 0 || mat_info->resolved_fs == 0) {
            /* Material not yet ready — skip the run silently (legitimate
             * runtime state, not a bug). */
            run_start = run_end;
            continue;
        }

        /* Each batch_key boundary opens a fresh cmd. */
        nt_pipeline_t pip = find_or_create_pipeline(mat_info);
        close_current_cmd();
        open_cmd(pip, mat_info);

        for (uint32_t i = run_start; i < run_end; i++) {
            emit_one(&items[i], &sv, &tv, &dv);
        }
        run_start = run_end;
    }
    nt_sprite_renderer_flush();
}
// #endregion

// #region flush
/* Phase 2: upload staging, replay cmds, rebind state only on delta. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void nt_sprite_renderer_flush(void) {
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
    if (s_sprite.index_count > 0) {
        nt_gfx_orphan_buffer(s_sprite.ibo, s_sprite.indices, s_sprite.index_count * (uint32_t)sizeof(uint16_t));
    }

    uint32_t bound_pipeline_id = 0;
    uint32_t bound_ibo_id = 0;
    uint32_t bound_tex_ids[NT_MATERIAL_MAX_TEXTURES] = {0};
    /* Tracked separately so override→no-override cmd transitions reset to default. */
    uint32_t bound_sampler_ids[NT_MATERIAL_MAX_TEXTURES] = {0};

    for (uint32_t ci = 0; ci < s_sprite.cmd_count; ci++) {
        const nt_sprite_draw_cmd_t *c = &s_sprite.cmds[ci];

        if (c->pipeline.id != bound_pipeline_id) {
            /* GL ordering: each pipeline owns a VAO, and GL_ELEMENT_ARRAY_BUFFER
             * is part of VAO state. So pipeline → VBO (re-applies attribs into
             * the new VAO) → IBO (binds the EBO into the new VAO). Reordering
             * any of these breaks the next draw silently. */
            nt_gfx_bind_pipeline(c->pipeline);
            nt_gfx_bind_vertex_buffer(s_sprite.vbo);
            bound_pipeline_id = c->pipeline.id;
            bound_ibo_id = 0;
        }

        if (s_sprite.ibo.id != bound_ibo_id) {
            nt_gfx_bind_index_buffer(s_sprite.ibo);
            bound_ibo_id = s_sprite.ibo.id;
        }

        for (uint8_t t = 0; t < c->tex_count; t++) {
            if (c->resolved_tex[t] != 0 && c->resolved_tex[t] != bound_tex_ids[t]) {
                nt_gfx_bind_texture((nt_texture_t){.id = c->resolved_tex[t]}, t);
                if (c->tex_names[t] != NULL) {
                    nt_gfx_set_uniform_int(c->tex_names[t], (int)t);
                }
                bound_tex_ids[t] = c->resolved_tex[t];
                /* bind_texture also bound the texture's default sampler. */
                bound_sampler_ids[t] = nt_gfx_get_texture_default_sampler((nt_texture_t){.id = c->resolved_tex[t]}).id;
            }
            /* Effective sampler = override if set, else texture's asset default. */
            uint32_t want_sampler;
            if (c->resolved_sampler[t].id != 0) {
                want_sampler = c->resolved_sampler[t].id;
            } else if (c->resolved_tex[t] != 0) {
                want_sampler = nt_gfx_get_texture_default_sampler((nt_texture_t){.id = c->resolved_tex[t]}).id;
            } else {
                /* Slot declared by material (tex_count > t) but no texture
                 * resolved. Cmd will render with stale unit state. Warn once
                 * — usually a material/resource resolve race. */
                static bool s_warned_unbound;
                if (!s_warned_unbound) {
                    NT_LOG_WARN("sprite_renderer: cmd slot %u has no resolved texture — material binding race?", (unsigned)t);
                    s_warned_unbound = true;
                }
                want_sampler = 0;
            }
            if (want_sampler != bound_sampler_ids[t]) {
                nt_gfx_bind_sampler((nt_sampler_t){.id = want_sampler}, t);
                bound_sampler_ids[t] = want_sampler;
            }
        }

        /* Per-cmd vertex delta — avoids stats inflation across state splits. */
        uint32_t cmd_vertex_end = (ci + 1U < s_sprite.cmd_count) ? s_sprite.cmds[ci + 1U].first_vertex : s_sprite.vertex_count;
        uint32_t cmd_vertex_count = cmd_vertex_end - c->first_vertex;
        nt_gfx_draw_indexed(c->first_index, c->index_count, cmd_vertex_count);
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
