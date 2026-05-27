#include "renderers/nt_sprite_renderer.h"

#include "core/nt_builtins.h"
#include <math.h>
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
    nt_material_t material; /* handle for material-param lookup at flush; param values
                               are NOT snapshotted — material info is stable within a
                               frame (nt_material_step ran before render) so we
                               re-fetch via nt_material_get_info at flush time */
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

    /* Reset per draw_list call; SEPARATE from nt_gfx_get_frame_draw_calls. */
    uint32_t last_draw_list_calls;

    /* Material of the most recently opened cmd; reset on flush. */
    nt_material_t current_mat;
#ifdef NT_TEST_ACCESS
    /* Captured pre-flush so tests can read back the last emit. */
    uint32_t last_emit_vertex_count;
    uint32_t last_emit_index_count;
    uint32_t last_emit_first_vertex;
    /* Captured at end of emit_slice9. */
    uint32_t last_slice9_vertex_count;
    uint32_t last_slice9_index_count;
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
/* Build the fixed sprite vertex layout once — 20-byte stride is locked.
 * Uses NT_ATTR_POSITION/COLOR/TEXCOORD0 location enum so the sprite vertex
 * shader can declare matching layout(location=N) bindings.
 * texcoord uses USHORT2N: GL normalizes 0..65535 → 0..1 in the shader at no
 * cost, and atlas UVs are already u16 in the blob — emit copies them
 * verbatim without a float roundtrip. */
static nt_vertex_layout_t s_sprite_layout = {
    .stride = 20,
    .attr_count = 3,
    .attrs =
        {
            {.location = NT_ATTR_POSITION, .format = NT_FORMAT_FLOAT3, .offset = 0},
            {.location = NT_ATTR_TEXCOORD0, .format = NT_FORMAT_USHORT2N, .offset = 12},
            {.location = NT_ATTR_COLOR, .format = NT_FORMAT_UBYTE4N, .offset = 16},
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
static void open_cmd(nt_pipeline_t pip, const nt_material_info_t *mi, nt_material_t mat) {
    if (s_sprite.cmd_count >= NT_SPRITE_RENDERER_MAX_DRAW_CMDS) {
        nt_sprite_renderer_flush();
    }
    NT_ASSERT(s_sprite.cmd_count < NT_SPRITE_RENDERER_MAX_DRAW_CMDS && "sprite draw-cmd queue full; raise NT_SPRITE_RENDERER_MAX_DRAW_CMDS");
    nt_sprite_draw_cmd_t *c = &s_sprite.cmds[s_sprite.cmd_count++];
    memset(c, 0, sizeof(*c)); /* slots reuse across frames; clear stale fields */
    c->pipeline = pip;
    c->material = mat;
    s_sprite.current_mat = mat;
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

// #region set_material
void nt_sprite_renderer_set_material(nt_material_t mat) {
    NT_ASSERT(s_sprite.initialized);
    NT_ASSERT(mat.id != 0 && "nt_sprite_renderer_set_material: invalid material handle");

    /* Validate BEFORE same-handle early return: stale handle (destroyed material,
     * bumped generation) must assert even if the id still matches the cached one. */
    const nt_material_info_t *mat_info = nt_material_get_info(mat);
    NT_ASSERT(mat_info != NULL && mat_info->ready && mat_info->resolved_vs != 0 && mat_info->resolved_fs != 0 && "nt_sprite_renderer_set_material: material not ready");

    /* Same-handle no-op only when cmd is still live; flush resets cmd_count. */
    if (mat.id == s_sprite.current_mat.id && s_sprite.cmd_count > 0) {
        return;
    }

    if (s_sprite.cmd_count > 0) {
        nt_sprite_renderer_flush();
    }

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

    /* Snapshot+flush+reopen on staging overflow keeps the caller's open
     * cmd state across the chunk boundary. */
    if (s_sprite.vertex_count + r->vertex_count > NT_SPRITE_RENDERER_MAX_VERTICES || s_sprite.index_count + r->index_count > NT_SPRITE_RENDERER_MAX_INDICES) {
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
            nt_sprite_vertex_t *v = &s_sprite.vertices[base + i];
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
            nt_sprite_vertex_t *v = &s_sprite.vertices[base + i];
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
    if ((sv->flags[s_idx] & NT_SPRITE_FLAG_RESOLVED) == 0 || resolved->region == NULL || resolved->region->vertex_count == 0) {
        return; /* tombstone */
    }
    const nt_texture_region_t *r = resolved->region;
    NT_ASSERT(resolved->cached_pos != NULL && resolved->raw_vertices != NULL && resolved->indices != NULL);
    uint32_t page_tex = nt_resource_get(resolved->page_resource);

    uint8_t flags = sv->flags[s_idx];
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
        if (s_sprite.vertex_count + 16U > NT_SPRITE_RENDERER_MAX_VERTICES || s_sprite.index_count + 54U > NT_SPRITE_RENDERER_MAX_INDICES) {
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

        /* Build 4x4 grid in local source space (ipu-scaled, origin at 0,0). */
        const float src_w = (float)r->source_w * ipu;
        const float src_h = (float)r->source_h * ipu;
        float fl_w = (float)fl * ipu;
        float fr_w = (float)fr * ipu;
        float ft_w = (float)ft * ipu;
        float fb_w = (float)fb * ipu;
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
                nt_sprite_vertex_t *v = &s_sprite.vertices[base + (row * 4) + col];
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
    NT_ASSERT(vertex_count <= NT_SPRITE_RENDERER_MAX_VERTICES && "nt_sprite_renderer_emit_geometry: vertex_count exceeds staging capacity");
    NT_ASSERT(index_count <= NT_SPRITE_RENDERER_MAX_INDICES && "nt_sprite_renderer_emit_geometry: index_count exceeds staging capacity");

    nt_atlas_region_handles_t h;
    nt_atlas_get_region_handles(atlas, region_index, &h);
    if (h.region->vertex_count == 0U) {
        return; /* tombstone */
    }
    const uint32_t page_tex = nt_resource_get(h.page_resource);
    if (!ensure_current_cmd_page_texture(page_tex)) {
        return;
    }

    /* Overflow handling mirrors emit_region_resolved: snapshot the open
     * cmd, flush, reopen with the same state. */
    if (s_sprite.vertex_count + vertex_count > NT_SPRITE_RENDERER_MAX_VERTICES || s_sprite.index_count + index_count > NT_SPRITE_RENDERER_MAX_INDICES) {
        nt_sprite_draw_cmd_t snapshot = s_sprite.cmds[s_sprite.cmd_count - 1];
        nt_sprite_renderer_flush();
        open_cmd_from_snapshot(&snapshot);
    }

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
        nt_sprite_vertex_t *v = &s_sprite.vertices[base + i];
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
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void nt_sprite_renderer_emit_slice9(nt_resource_t atlas, uint32_t region_index, float x, float y, float w, float h, uint16_t sl, uint16_t sr, uint16_t st, uint16_t sb, uint32_t color_packed,
                                    uint8_t flip_bits, float rotation) {
    NT_ASSERT(s_sprite.initialized);
    NT_ASSERT(atlas.id != 0 && "nt_sprite_renderer_emit_slice9: invalid atlas handle");
    NT_ASSERT(nt_resource_is_ready(atlas) && "nt_sprite_renderer_emit_slice9: atlas must be READY");
    NT_ASSERT(s_sprite.cmd_count > 0 && "nt_sprite_renderer_emit_slice9: call nt_sprite_renderer_set_material first");
    NT_ASSERT(isfinite(x) && isfinite(y) && isfinite(w) && isfinite(h) && isfinite(rotation));
    NT_ASSERT(w >= 0.0F && h >= 0.0F && "slice9 target dimensions must be non-negative");

    nt_atlas_region_handles_t rh;
    nt_atlas_get_region_handles(atlas, region_index, &rh);
    if (rh.region->vertex_count == 0U) {
        return; /* tombstone */
    }

    const float ipu = nt_atlas_get_inverse_pixels_per_unit(atlas);

    /* Slice9 regions must be non-rotated, untrimmed (RECT shape forces no trim). */
    NT_ASSERT(rh.region->transform == 0 && "slice9 region must have transform == 0 (no rotation)");
    NT_ASSERT(rh.region->trim_offset_x == 0 && rh.region->trim_offset_y == 0 && "slice9 region must be untrimmed (builder should force RECT shape)");
    NT_ASSERT(rh.region->source_w > 0 && rh.region->source_h > 0 && "slice9 region source dimensions must be non-zero");
    NT_ASSERT(sl + sr < rh.region->source_w && st + sb < rh.region->source_h && "slice9 borders exceed source dimensions (per-entity override invalid?)");
    NT_ASSERT(ipu > 0.0F && "slice9 ipu must be positive");

    const uint32_t page_tex = nt_resource_get(rh.page_resource);
    if (!ensure_current_cmd_page_texture(page_tex)) {
        return;
    }

    /* Flip border swap (D-54-19): swap borders before computing splits. */
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
    float fl_w = (float)fl * ipu;
    float fr_w = (float)fr * ipu;
    float ft_w = (float)ft * ipu;
    float fb_w = (float)fb * ipu;
    /* Proportionally shrink borders when target rect is smaller than total borders */
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

    /* UV flip after split computation (D-54-19). */
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
    if (s_sprite.vertex_count + 16U > NT_SPRITE_RENDERER_MAX_VERTICES || s_sprite.index_count + 54U > NT_SPRITE_RENDERER_MAX_INDICES) {
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
            nt_sprite_vertex_t *v = &s_sprite.vertices[base + (row * 4) + col];
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

    /* Rotate all 16 vertices around rect center if rotation != 0. */
    if (rotation != 0.0F) {
        const float rcx = x + (w * 0.5F);
        const float rcy = y + (h * 0.5F);
        const float rc = cosf(rotation);
        const float rs = sinf(rotation);
        for (uint32_t vi = 0; vi < 16U; vi++) {
            nt_sprite_vertex_t *v = &s_sprite.vertices[base + vi];
            const float dx = v->position[0] - rcx;
            const float dy = v->position[1] - rcy;
            v->position[0] = (dx * rc) - (dy * rs) + rcx;
            v->position[1] = (dx * rs) + (dy * rc) + rcy;
        }
    }

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

// #region draw_list
/* Phase 1: open a cmd per batch_key, stream verts into staging via
 * emit_one. Phase 2 = nt_sprite_renderer_flush. */
void nt_sprite_renderer_draw_list(const nt_render_item_t *items, uint32_t count) {
    NT_ASSERT(s_sprite.initialized);
    if (count == 0) {
        return;
    }

    s_sprite.last_draw_list_calls = 0;
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

    /* orphan_buffer asks the driver to allocate fresh storage instead of
     * mutating the bound VBO in place, so the GPU can keep consuming the
     * previous frame's draws while we stage the next one. */
    nt_gfx_orphan_buffer(s_sprite.vbo, s_sprite.vertices, s_sprite.vertex_count * (uint32_t)sizeof(nt_sprite_vertex_t));
    if (s_sprite.index_count > 0) {
        nt_gfx_orphan_buffer(s_sprite.ibo, s_sprite.indices, s_sprite.index_count * (uint32_t)sizeof(uint16_t));
    }

    uint32_t bound_pipeline_id = 0;
    uint32_t bound_ibo_id = 0;
    uint32_t bound_tex_ids[NT_MATERIAL_MAX_TEXTURES] = {0};
    /* Tracked separately so override→no-override cmd transitions reset to default. */
    uint32_t bound_sampler_ids[NT_MATERIAL_MAX_TEXTURES] = {0};
    uint32_t bound_mat_id = 0;

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
            /* Sampler uniforms ("u_tex0" etc) are program-scoped — the new
             * program has fresh uniform locations defaulting to 0. Reset the
             * tex/sampler tracking so the inner loop forces rebind +
             * set_uniform_int for every slot, even if the texture id matches. */
            memset(bound_tex_ids, 0, sizeof(bound_tex_ids));
            memset(bound_sampler_ids, 0, sizeof(bound_sampler_ids));
        }

        if (s_sprite.ibo.id != bound_ibo_id) {
            nt_gfx_bind_index_buffer(s_sprite.ibo);
            bound_ibo_id = s_sprite.ibo.id;
        }

        /* Apply material params on material change. Most cmds in a flush share
         * the same material (atlas page split / sampler split don't change it),
         * so the lookup + uniform set runs only at run boundaries. If the
         * material was destroyed between draw_list capture and flush replay,
         * leave bound_mat_id unchanged so a later cmd with the same material
         * can re-attempt the lookup once it's resolvable again. */
        if (c->material.id != bound_mat_id) {
            const nt_material_info_t *mi = nt_material_get_info(c->material);
            if (mi != NULL) {
                for (uint8_t p = 0; p < mi->param_count; p++) {
                    if (mi->param_names[p] != NULL) {
                        nt_gfx_set_uniform_vec4(mi->param_names[p], mi->params[p]);
                    }
                }
                bound_mat_id = c->material.id;
            }
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
            /* Effective sampler = override if set, else texture's asset default.
             * Material declared a binding for slot t but resolved_tex == 0 is
             * a developer bug — the engine ships nt_resource_set_placeholder_texture
             * exactly to keep textures resolvable through async load races.
             * Fail-early per AGENTS.md: dev catches it on first frame. */
            NT_ASSERT((c->resolved_sampler[t].id != 0 || c->resolved_tex[t] != 0) && "sprite cmd slot has no resolved texture — register a placeholder via nt_resource_set_placeholder_texture");
            uint32_t want_sampler;
            if (c->resolved_sampler[t].id != 0) {
                want_sampler = c->resolved_sampler[t].id;
            } else {
                want_sampler = nt_gfx_get_texture_default_sampler((nt_texture_t){.id = c->resolved_tex[t]}).id;
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
        s_sprite.last_draw_list_calls++;
    }

    s_sprite.vertex_count = 0;
    s_sprite.index_count = 0;
    s_sprite.cmd_count = 0;
    /* draw_list opens cmds per batch_key, not via current_mat. Reset
     * the fence so a following same-handle set_material() re-opens. */
    s_sprite.current_mat = (nt_material_t){0};
}
// #endregion

// #region test accessors
#ifdef NT_TEST_ACCESS
uint32_t nt_sprite_renderer_test_pipeline_cache_count(void) { return s_sprite.count; }
uint32_t nt_sprite_renderer_test_draw_call_count(void) { return s_sprite.last_draw_list_calls; }
uint32_t nt_sprite_renderer_test_vertex_count(void) { return s_sprite.vertex_count; }
uint32_t nt_sprite_renderer_test_last_emit_vertex_count(void) { return s_sprite.last_emit_vertex_count; }
uint32_t nt_sprite_renderer_test_last_emit_index_count(void) { return s_sprite.last_emit_index_count; }

void nt_sprite_renderer_test_last_emit_position(uint32_t v_idx, float out[3]) {
    NT_ASSERT(v_idx < s_sprite.last_emit_vertex_count && "last_emit_position: index out of range");
    const nt_sprite_vertex_t *v = &s_sprite.vertices[s_sprite.last_emit_first_vertex + v_idx];
    out[0] = v->position[0];
    out[1] = v->position[1];
    out[2] = v->position[2];
}

void nt_sprite_renderer_test_last_emit_texcoord(uint32_t v_idx, uint16_t out[2]) {
    NT_ASSERT(v_idx < s_sprite.last_emit_vertex_count && "last_emit_texcoord: index out of range");
    const nt_sprite_vertex_t *v = &s_sprite.vertices[s_sprite.last_emit_first_vertex + v_idx];
    out[0] = v->texcoord[0];
    out[1] = v->texcoord[1];
}

void nt_sprite_renderer_test_last_emit_color(uint32_t v_idx, uint8_t out[4]) {
    NT_ASSERT(v_idx < s_sprite.last_emit_vertex_count && "last_emit_color: index out of range");
    const nt_sprite_vertex_t *v = &s_sprite.vertices[s_sprite.last_emit_first_vertex + v_idx];
    out[0] = v->color[0];
    out[1] = v->color[1];
    out[2] = v->color[2];
    out[3] = v->color[3];
}

bool nt_sprite_renderer_test_initialized(void) { return s_sprite.initialized; }
uint32_t nt_sprite_renderer_test_last_slice9_vertex_count(void) { return s_sprite.last_slice9_vertex_count; }
uint32_t nt_sprite_renderer_test_last_slice9_index_count(void) { return s_sprite.last_slice9_index_count; }
#endif
// #endregion
