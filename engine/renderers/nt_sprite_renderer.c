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
#include "log/nt_log.h"
#include "material/nt_material.h"
#include "material_comp/nt_material_comp.h"
#include "render/nt_render_defs.h"
#include "renderers/nt_renderer_shared.h"
#include "sprite_comp/nt_sprite_comp.h"
#include "transform_comp/nt_transform_comp.h"

/* Base 20 B sprite vertex stride — custom attrs append after this offset. */
#define NT_SPRITE_BASE_STRIDE 20
/* Distinct material states; also bounds the layout cache, which holds fewer. */
#ifndef NT_SPRITE_RENDERER_MAX_PIPELINES
#define NT_SPRITE_RENDERER_MAX_PIPELINES 16
#endif

// #region module state
/* A resolved material: what an emit or a draw_list run records. */
typedef struct {
    nt_material_t material;
    const nt_material_info_t *info;
    nt_program_t program; /* the program `pipeline` was built on: a replace keeps the material handle */
    nt_pipeline_t pipeline;
    nt_vertex_input_t vertex_input;
    uint32_t stride; /* 20 + attr_map_count * 16 */
    uint64_t frame;  /* gfx frame of the selection */
    /* Slot 0 is the atlas page, substituted per emit or run. */
    nt_gfx_texture_binding_t textures[NT_MATERIAL_MAX_TEXTURES];
} nt_sprite_material_t;

/* One allocation of an emit or a draw_list item. */
typedef struct {
    uint8_t *vertices;
    uint32_t *indices;
    uint32_t base; /* absolute index of the first vertex */
    uint32_t first_index;
} nt_sprite_alloc_t;

static struct {
    nt_renderer_pipeline_entry_t entries[NT_SPRITE_RENDERER_MAX_PIPELINES];
    uint16_t count;

    /* Layout-hash cache over the frame buffers; custom layouts may share a pipeline. */
    struct {
        uint64_t key;
        nt_vertex_input_t vi;
    } vi_entries[NT_SPRITE_RENDERER_MAX_PIPELINES];
    uint16_t vi_count;

    /* One-shot so a load-time skip does not spam; re-armed when a pipeline is built. */
    bool warned_program_not_ready;

    nt_sprite_material_t current; /* set_material; the immediate emits read it */

    /* Params last written, per program: equal params record nothing, so adjacent emits merge. */
    struct {
        nt_program_t program;
        nt_material_t material;
        float params[NT_MATERIAL_MAX_PARAMS][4];
    } recorded;

#ifdef NT_TEST_ACCESS
    nt_sprite_test_emit_t last_emit;
#endif
} s_sprite;
// #endregion

// #region lifecycle
void nt_sprite_renderer_shutdown(void) {
    for (uint16_t i = 0; i < s_sprite.count; i++) {
        nt_gfx_destroy_pipeline(s_sprite.entries[i].pipeline);
    }
    for (uint16_t i = 0; i < s_sprite.vi_count; i++) {
        nt_gfx_destroy_vertex_input(s_sprite.vi_entries[i].vi);
    }
    memset(&s_sprite, 0, sizeof(s_sprite));
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
    layout.stride = offset;
    return layout;
}

/* Exact vertex-input identity: 0 for the base 20 B layout, else attr_map count
 * plus every location packed bit-exact, so two attr_maps never share a key. */
_Static_assert(NT_MATERIAL_MAX_ATTR_MAP < 16, "sprite VI key packs attr_map_count in 4 bits");
_Static_assert(NT_GFX_MAX_VERTEX_ATTRS <= 16, "sprite VI key packs a location in 4 bits");
_Static_assert(4 + NT_MATERIAL_MAX_ATTR_MAP * 4 <= 64, "sprite VI key overflows uint64");
static uint64_t nt_sprite_layout_key(const nt_material_info_t *mat_info) {
    uint64_t key = mat_info->attr_map_count;
    for (uint8_t ai = 0; ai < mat_info->attr_map_count; ai++) {
        key |= (uint64_t)mat_info->attr_map_locations[ai] << (4 + ai * 4);
    }
    return key;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static nt_pipeline_t find_or_create_pipeline(const nt_material_info_t *mat_info) {
    /* A recovered context may still have materials awaiting a new program. */
    if (!nt_gfx_program_ready(mat_info->program)) {
        nt_renderer_warn_program_not_ready(&s_sprite.warned_program_not_ready, mat_info);
        return (nt_pipeline_t){0};
    }
    /* Vertex-inputs own layouts; the pipeline is program x state, keyed by its exact desc identity. */
    const nt_pipeline_desc_t desc = nt_renderer_material_pipeline_desc(mat_info, "sprite_pipeline");
    const nt_gfx_pipeline_key_t key = nt_gfx_pipeline_key(&desc);
    const nt_pipeline_t cached = nt_renderer_pipeline_cache_find(s_sprite.entries, s_sprite.count, &key);
    if (cached.id != 0) {
        return cached;
    }
    return nt_renderer_pipeline_cache_insert(s_sprite.entries, &s_sprite.count, NT_SPRITE_RENDERER_MAX_PIPELINES, &key, &desc, &s_sprite.warned_program_not_ready);
}

/* Entries are weak: a context loss frees vertex-input slots, so a hit validates and a dead
 * entry recreates in place over the current frame buffers. */
static nt_vertex_input_t find_or_create_vertex_input(const nt_material_info_t *mat_info) {
    const nt_buffer_t vbo = nt_gfx_frame_buffer(NT_GFX_FRAME_VERTEX);
    const nt_buffer_t ibo = nt_gfx_frame_buffer(NT_GFX_FRAME_INDEX);
    if (vbo.id == 0 || ibo.id == 0) {
        return NT_VERTEX_INPUT_INVALID; /* lost context: the frame buffers come back at restore */
    }
    const uint64_t key = nt_sprite_layout_key(mat_info);
    uint16_t idx = s_sprite.vi_count;
    for (uint16_t i = 0; i < s_sprite.vi_count; i++) {
        const bool live = nt_gfx_vertex_input_valid(s_sprite.vi_entries[i].vi);
        if (s_sprite.vi_entries[i].key == key) {
            if (live) {
                return s_sprite.vi_entries[i].vi;
            }
            idx = i;
            break;
        }
        if (!live && idx == s_sprite.vi_count) {
            idx = i; /* a layout the loss freed; reused unless the key itself turns up */
        }
    }
    NT_ASSERT(idx < NT_SPRITE_RENDERER_MAX_PIPELINES && "sprite vertex-input cache full; raise NT_SPRITE_RENDERER_MAX_PIPELINES");
    nt_vertex_input_t vi = nt_gfx_make_vertex_input(&(nt_vertex_input_desc_t){
        .layout = build_sprite_layout(mat_info),
        .vertex_buffer = vbo,
        .index_buffer = ibo,
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

// #region material
/* A failed pipeline or vertex input leaves the material not drawable (pipeline 0) until the
 * next resolve retries. */
static void resolve_material(nt_material_t mat, const nt_material_info_t *mi, nt_sprite_material_t *out) {
    /* A zero budget would make every sprite silently vanish. */
    NT_ASSERT(g_nt_gfx_frame_storage[NT_GFX_FRAME_VERTEX].capacity > 0 && g_nt_gfx_frame_storage[NT_GFX_FRAME_INDEX].capacity > 0 &&
              "sprite renderer: set nt_gfx_desc_t.frame_capacity[NT_GFX_FRAME_VERTEX] and [NT_GFX_FRAME_INDEX]");
    out->material = mat;
    out->info = mi;
    out->program = mi->program;
    out->stride = (uint32_t)NT_SPRITE_BASE_STRIDE + ((uint32_t)mi->attr_map_count * 16U);
    out->pipeline = find_or_create_pipeline(mi);
    out->vertex_input = (out->pipeline.id != 0) ? find_or_create_vertex_input(mi) : NT_VERTEX_INPUT_INVALID;
    if (out->vertex_input.id == 0) {
        out->pipeline = (nt_pipeline_t){0};
    }
    for (uint8_t t = 0; t < mi->tex_count; t++) {
        out->textures[t] = (nt_gfx_texture_binding_t){
            .name = {.value = mi->tex_name_hashes[t]},
            /* Slot 0 is the atlas page by contract: its declared resource is never sampled. */
            .texture = {.id = (t == 0) ? 0U : nt_resource_get(mi->tex_resources[t])},
            .sampler = mi->tex_samplers[t],
        };
    }
}

/* Pipeline first: uniforms and the texture set land on its program. */
static void record_state(nt_sprite_material_t *m, uint32_t page_tex) {
    const nt_material_info_t *mi = m->info;
    nt_gfx_bind_pipeline(m->pipeline);
    const size_t param_bytes = (size_t)mi->param_count * sizeof(mi->params[0]);
    if (s_sprite.recorded.program.id != m->program.id || s_sprite.recorded.material.id != m->material.id || memcmp(s_sprite.recorded.params, mi->params, param_bytes) != 0) {
        nt_renderer_set_material_uniforms(mi);
        s_sprite.recorded.program = m->program;
        s_sprite.recorded.material = m->material;
        memcpy(s_sprite.recorded.params, mi->params, param_bytes);
    }
    if (mi->tex_count > 0) {
        m->textures[0].texture.id = page_tex;
    }
    /* Also with no declarations: gfx checks that the program samples nothing. */
    nt_gfx_apply_texture_bindings(m->textures, mi->tex_count);
    nt_gfx_bind_vertex_input(m->vertex_input);
}

void nt_sprite_renderer_set_material(nt_material_t mat) {
    NT_ASSERT(mat.id != 0 && "nt_sprite_renderer_set_material: invalid material handle");
    const nt_material_info_t *mi = nt_material_get_info(mat);
    /* Assignment, not liveness: on the frame the context dies the program is
     * already dead here, and a relink that met a new loss left it unassigned. */
    NT_ASSERT(mi != NULL && (mi->program.id != 0 || g_nt_gfx.context_lost) && "nt_sprite_renderer_set_material: material has no program");
    nt_sprite_material_t *c = &s_sprite.current;
    /* Params are compared when recorded, and resources are not stepped between draws of a frame.
     * A failed resolve (pipeline 0) retries next frame; a destroyed program takes its pipeline
     * with it, so that selection resolves again. */
    const uint64_t frame = g_nt_gfx.counters.frame_sequence;
    if (mat.id == c->material.id && mi->program.id == c->program.id && frame == c->frame && (c->pipeline.id == 0 || nt_gfx_pipeline_valid(c->pipeline))) {
        return;
    }
    resolve_material(mat, mi, c);
    c->frame = frame;
}
// #endregion

// #region allocation
#ifdef NT_TEST_ACCESS
static void capture_emit(const nt_sprite_alloc_t *a, uint32_t stride, uint32_t vertex_count, uint32_t index_count) {
    s_sprite.last_emit = (nt_sprite_test_emit_t){
        .vertices = a->vertices,
        .stride = stride,
        .first_vertex = a->base,
        .vertex_count = vertex_count,
        .first_index = a->first_index,
        .index_count = index_count,
    };
}
#endif

/* Records the selected material and allocates one immediate emit; false draws nothing. Every
 * emit starts at a multiple of 4 vertices for gl_VertexID & 3 corner shaders. */
static bool emit_begin(uint32_t page_tex, uint32_t vertex_count, uint32_t index_count, nt_sprite_alloc_t *a) {
    nt_sprite_material_t *m = &s_sprite.current;
    NT_ASSERT(m->frame == g_nt_gfx.counters.frame_sequence && "sprite emit: call nt_sprite_renderer_set_material in this frame");
    if (m->pipeline.id == 0 || (m->info->tex_count > 0 && page_tex == 0)) {
        return false;
    }
    record_state(m, page_tex);
    uint32_t voff = 0;
    uint32_t ioff = 0;
    a->vertices = (uint8_t *)nt_gfx_frame_alloc(NT_GFX_FRAME_VERTEX, vertex_count * m->stride, 4U * m->stride, &voff);
    a->indices = (uint32_t *)nt_gfx_frame_alloc(NT_GFX_FRAME_INDEX, index_count * 4U, 4U, &ioff);
    a->base = voff / m->stride;
    a->first_index = ioff / 4U;
#ifdef NT_TEST_ACCESS
    capture_emit(a, m->stride, vertex_count, index_count);
#endif
    return true;
}

/* One draw_list run: contiguous vertices and indices, drawn once. */
typedef struct {
    uint32_t stride;
    uint32_t next_vertex;
    uint32_t first_index;
    uint32_t index_count;
    uint32_t vertex_count;
} nt_sprite_run_t;

/* The first item aligns the run to 4 vertices; the rest follow contiguously, since nothing
 * else allocates or calls gfx inside a run. */
static void run_alloc(nt_sprite_run_t *run, uint32_t vertex_count, uint32_t index_count, nt_sprite_alloc_t *a) {
    uint32_t voff = 0;
    uint32_t ioff = 0;
    if (run->index_count == 0) {
        a->vertices = (uint8_t *)nt_gfx_frame_alloc(NT_GFX_FRAME_VERTEX, vertex_count * run->stride, 4U * run->stride, &voff);
        a->indices = (uint32_t *)nt_gfx_frame_alloc(NT_GFX_FRAME_INDEX, index_count * 4U, 4U, &ioff);
        run->next_vertex = voff / run->stride;
        run->first_index = ioff / 4U;
    } else {
        a->vertices = (uint8_t *)nt_gfx_frame_alloc(NT_GFX_FRAME_VERTEX, vertex_count * run->stride, 1U, &voff);
        a->indices = (uint32_t *)nt_gfx_frame_alloc(NT_GFX_FRAME_INDEX, index_count * 4U, 1U, &ioff);
        NT_ASSERT(voff == run->next_vertex * run->stride && ioff / 4U == run->first_index + run->index_count && "sprite run: allocation not contiguous");
    }
    a->base = run->next_vertex;
    a->first_index = ioff / 4U;
    run->next_vertex += vertex_count;
    run->vertex_count += vertex_count;
    run->index_count += index_count;
#ifdef NT_TEST_ACCESS
    capture_emit(a, run->stride, vertex_count, index_count);
#endif
}
// #endregion

// #region custom_attrs
/* Whole FLOAT4 lanes: a constant-size copy inlines, a variable-size one is a libc call per vertex.
 * Out of line: inlining it at every emit kind costs ~3 KB of wasm and measured no faster. */
static NT_NOINLINE void bake_custom_lanes(uint8_t *dst, uint32_t count, uint32_t stride, uint32_t lanes, const uint8_t *src) {
    for (uint32_t i = 0; i < count; i++) {
        uint8_t *v = dst + ((size_t)i * stride) + NT_SPRITE_BASE_STRIDE;
        for (uint32_t l = 0; l < lanes; ++l) {
            memcpy(v + ((size_t)l * 16U), src + ((size_t)l * 16U), 16U);
        }
    }
}

/* Bake one emit's custom block — identical for every vertex, like color — after the 20 B base
 * of each vertex. No block: the material's attr defaults. Plain material: no-op. */
static inline void bake_custom_attrs(const nt_sprite_material_t *m, uint8_t *dst, uint32_t count, const float *custom, uint8_t custom_bytes) {
    const uint32_t bytes = m->stride - NT_SPRITE_BASE_STRIDE;
    /* A wrong size desyncs the vertex stride from the pipeline's. */
    NT_ASSERT((custom_bytes == 0U || custom_bytes == bytes) && "custom block size doesn't match the bound material's attr_map stride");
    if (bytes == 0) {
        return;
    }
    const uint8_t *src = (const uint8_t *)custom;
    if (custom_bytes == 0U) {
        src = m->info->has_attr_defaults ? (const uint8_t *)m->info->attr_map_defaults : NULL;
    }
    NT_ASSERT(src != NULL && "custom-attr material: pass a custom block or give the material attr defaults");
    bake_custom_lanes(dst, count, m->stride, bytes / 16U, src);
}
// #endregion

// #region write_region
/* always_inline keeps the ECS hot path's inlined shape. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static NT_ALWAYS_INLINE void write_region(const nt_texture_region_t *r, const float (*positions)[2], const nt_atlas_uv_t *uvs, const uint16_t *idx, float ipu, const float *m, float origin_x,
                                          float origin_y, uint32_t color_packed, uint8_t flip_bits, uint32_t stride, const nt_sprite_alloc_t *a) {
    NT_ASSERT(r != NULL && positions != NULL && uvs != NULL && idx != NULL);
    NT_ASSERT(m != NULL);

    /* Source-space positions omit origin, so different pivots can share geometry. */
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

#ifdef __wasm_simd128__
    if (r->vertex_count == 4) {
        /* De-interleave positions[4][2] → pxs/pys lanes. */
        v128_t lo = wasm_v128_load(&positions[0][0]);
        v128_t hi = wasm_v128_load(&positions[2][0]);
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
            nt_sprite_vertex_t *v = (nt_sprite_vertex_t *)(a->vertices + ((size_t)i * stride));
            v->position[0] = xs_arr[i];
            v->position[1] = ys_arr[i];
            v->position[2] = zs_arr[i];
            v->texcoord[0] = uvs[i].atlas_u;
            v->texcoord[1] = uvs[i].atlas_v;
            memcpy(v->color, &color_packed, sizeof(v->color));
        }
    } else
#endif /* __wasm_simd128__ */
    {
        /* Locals: the byte stores below may alias the matrix, which would reload it per vertex. */
        const float m0 = m[0];
        const float m1 = m[1];
        const float m2 = m[2];
        const float m4 = m[4];
        const float m5 = m[5];
        const float m6 = m[6];
        const uint8_t vertex_count = r->vertex_count;
        for (uint8_t i = 0; i < vertex_count; i++) {
            float px = positions[i][0];
            if (fx) {
                px = -px;
            }
            float py = positions[i][1];
            if (fy) {
                py = -py;
            }
            nt_sprite_vertex_t *v = (nt_sprite_vertex_t *)(a->vertices + ((size_t)i * stride));
            v->position[0] = (m0 * px) + (m4 * py) + tx;
            v->position[1] = (m1 * px) + (m5 * py) + ty;
            v->position[2] = (m2 * px) + (m6 * py) + tz;
            v->texcoord[0] = uvs[i].atlas_u;
            v->texcoord[1] = uvs[i].atlas_v;
            /* 0xAABBGGRR in little-endian memory is R, G, B, A. */
            memcpy(v->color, &color_packed, sizeof(v->color));
        }
    }

    uint32_t *out_idx = a->indices;
    const uint32_t base = a->base;
    const uint8_t index_count = r->index_count;
    for (uint8_t i = 0; i < index_count; i++) {
        out_idx[i] = base + (uint32_t)idx[i];
    }
}
// #endregion

// #region slice9_grid
/* Local space matches write_region's: Y-up, pivot-relative, mirrored by negating positions.
 * Row 0 is the local bottom and samples v_max: blob vertices are Y-up, atlas_v is PNG Y-down. */
typedef struct {
    const nt_texture_region_t *region;
    const nt_atlas_uv_t *uvs;
    const uint16_t *src_lrtb; /* source px: the UV cuts */
    float band_per_px;        /* slice9_scale / pixels_per_unit: source px -> w/h units */
    float w;                  /* local grid size */
    float h;
    float origin_x; /* pivot, normalized over w/h */
    float origin_y;
    uint32_t color_packed;
    uint8_t flip_bits;
    const float *world_matrix;
} slice9_grid_t;

/* UV bbox of a region's vertices, as {u_min, u_max, v_min, v_max} in u16 space. */
static void region_uv_bounds(const nt_atlas_uv_t *verts, uint8_t count, uint16_t out[4]) {
    out[0] = UINT16_MAX;
    out[1] = 0;
    out[2] = UINT16_MAX;
    out[3] = 0;
    for (uint8_t i = 0; i < count; i++) {
        const uint16_t au = verts[i].atlas_u;
        const uint16_t av = verts[i].atlas_v;
        out[0] = (au < out[0]) ? au : out[0];
        out[1] = (au > out[1]) ? au : out[1];
        out[2] = (av < out[2]) ? av : out[2];
        out[3] = (av > out[3]) ? av : out[3];
    }
}

/* Grid splits for one slice9: dst bands become local coordinates, src borders
 * become UV cuts. Rows are Y-up, so V descends with them. */
static void slice9_build_splits(const slice9_grid_t *g, float lxs[4], float lys[4], uint16_t us[4], uint16_t vs[4]) {
    float bl = (float)g->src_lrtb[0] * g->band_per_px;
    float br = (float)g->src_lrtb[1] * g->band_per_px;
    float bt = (float)g->src_lrtb[2] * g->band_per_px;
    float bb = (float)g->src_lrtb[3] * g->band_per_px;
    /* Proportionally shrink dst bands when the target is smaller than their sum. */
    if (bl + br > g->w) {
        const float ratio = g->w / (bl + br);
        bl *= ratio;
        br *= ratio;
    }
    if (bt + bb > g->h) {
        const float ratio = g->h / (bt + bb);
        bt *= ratio;
        bb *= ratio;
    }
    lxs[0] = 0.0F;
    lxs[1] = bl;
    lxs[2] = g->w - br;
    lxs[3] = g->w;
    lys[0] = 0.0F;
    lys[1] = bb;
    lys[2] = g->h - bt;
    lys[3] = g->h;

    uint16_t uv_bounds[4]; /* u_min, u_max, v_min, v_max */
    region_uv_bounds(g->uvs, g->region->vertex_count, uv_bounds);
    const uint16_t u_range = (uint16_t)(uv_bounds[1] - uv_bounds[0]);
    const uint16_t v_range = (uint16_t)(uv_bounds[3] - uv_bounds[2]);
    /* Integer math avoids precision loss. */
    us[0] = uv_bounds[0];
    us[1] = (uint16_t)(uv_bounds[0] + (((uint32_t)g->src_lrtb[0] * u_range) / g->region->source_w));
    us[2] = (uint16_t)(uv_bounds[1] - (((uint32_t)g->src_lrtb[1] * u_range) / g->region->source_w));
    us[3] = uv_bounds[1];
    vs[0] = uv_bounds[3];
    vs[1] = (uint16_t)(uv_bounds[3] - (((uint32_t)g->src_lrtb[3] * v_range) / g->region->source_h));
    vs[2] = (uint16_t)(uv_bounds[2] + (((uint32_t)g->src_lrtb[2] * v_range) / g->region->source_h));
    vs[3] = uv_bounds[2];
}

/* Writes the 16 grid vertices. The pivot rides in the translation and mirrors
 * with the geometry, exactly as write_region does it. */
static void write_slice9_vertices(const slice9_grid_t *g, const float lxs[4], const float lys[4], const uint16_t us[4], const uint16_t vs[4], uint8_t *dst, uint32_t stride) {
    const float *m = g->world_matrix;
    const bool fx = (g->flip_bits & NT_SPRITE_FLAG_FLIP_X) != 0;
    const bool fy = (g->flip_bits & NT_SPRITE_FLAG_FLIP_Y) != 0;
    const float dx = fx ? -(g->origin_x * g->w) : (g->origin_x * g->w);
    const float dy = fy ? -(g->origin_y * g->h) : (g->origin_y * g->h);
    const float tx = m[12] - (m[0] * dx) - (m[4] * dy);
    const float ty = m[13] - (m[1] * dx) - (m[5] * dy);
    const float tz = m[14] - (m[2] * dx) - (m[6] * dy);

    for (uint8_t row = 0; row < 4; row++) {
        const float py = fy ? -lys[row] : lys[row];
        for (uint8_t col = 0; col < 4; col++) {
            const float px = fx ? -lxs[col] : lxs[col];
            nt_sprite_vertex_t *v = (nt_sprite_vertex_t *)(dst + ((size_t)((row * 4) + col) * stride));
            v->position[0] = (m[0] * px) + (m[4] * py) + tx;
            v->position[1] = (m[1] * px) + (m[5] * py) + ty;
            v->position[2] = (m[2] * px) + (m[6] * py) + tz;
            v->texcoord[0] = us[col];
            v->texcoord[1] = vs[row];
            memcpy(v->color, &g->color_packed, sizeof(v->color));
        }
    }
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity) — four asserts, no control flow of its own
static void slice9_assert_region(const slice9_grid_t *g) {
    NT_ASSERT(g->region->transform == 0 && "slice9 region must have transform == 0 (no rotation)");
    NT_ASSERT(g->region->trim_offset_x == 0 && g->region->trim_offset_y == 0 && "slice9 region must be untrimmed");
    NT_ASSERT(g->region->source_w > 0 && g->region->source_h > 0 && "slice9 region source dimensions must be non-zero");
    NT_ASSERT(g->src_lrtb[0] + g->src_lrtb[1] < g->region->source_w && g->src_lrtb[2] + g->src_lrtb[3] < g->region->source_h && "slice9 src borders exceed source dimensions");
}

/* 16 vertices and 54 indices into one allocation. */
static void write_slice9(const slice9_grid_t *g, uint32_t stride, const nt_sprite_alloc_t *a) {
    slice9_assert_region(g);
    float lxs[4];
    float lys[4];
    uint16_t us[4];
    uint16_t vs[4];
    slice9_build_splits(g, lxs, lys, us, vs);
    write_slice9_vertices(g, lxs, lys, us, vs, a->vertices, stride);

    /* 54 indices: 9 cells x 2 triangles x 3 indices. */
    uint32_t *out_idx = a->indices;
    uint32_t ii = 0;
    for (uint32_t row = 0; row < 3; row++) {
        for (uint32_t col = 0; col < 3; col++) {
            const uint32_t i_bl = a->base + (row * 4U) + col;
            const uint32_t i_br = i_bl + 1U;
            const uint32_t i_tl = i_bl + 4U;
            const uint32_t i_tr = i_tl + 1U;
            /* CCW in local Y-up, like the blob's triangles, so CULL_BACK draws it. */
            out_idx[ii++] = i_tl;
            out_idx[ii++] = i_bl;
            out_idx[ii++] = i_tr;
            out_idx[ii++] = i_bl;
            out_idx[ii++] = i_br;
            out_idx[ii++] = i_tr;
        }
    }
}
// #endregion

// #region emit_region
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void nt_sprite_renderer_emit_region(nt_resource_t atlas, uint32_t region_index, const float *world_matrix, float origin_x, float origin_y, uint32_t color_packed, uint8_t flip_bits,
                                    const float *custom, uint8_t custom_bytes) {
    NT_ASSERT(world_matrix != NULL);
    NT_ASSERT(atlas.id != 0 && "nt_sprite_renderer_emit_region: invalid atlas handle");
    NT_ASSERT(nt_resource_is_ready(atlas) && "nt_sprite_renderer_emit_region: atlas must be READY");

    nt_atlas_region_handles_t h;
    nt_atlas_get_region_handles(atlas, region_index, &h);
    if (h.region->vertex_count == 0U) {
        return; /* tombstone or out-of-range */
    }
    nt_sprite_alloc_t a;
    if (!emit_begin(nt_resource_get(h.page_resource), h.region->vertex_count, h.region->index_count, &a)) {
        return;
    }
    const nt_sprite_material_t *m = &s_sprite.current;
    write_region(h.region, h.positions, h.uvs, h.indices, h.ipu, world_matrix, origin_x, origin_y, color_packed, flip_bits, m->stride, &a);
    bake_custom_attrs(m, a.vertices, h.region->vertex_count, custom, custom_bytes);
    nt_gfx_draw_indexed(a.first_index, h.region->index_count, h.region->vertex_count);
}
// #endregion

// #region emit_geometry
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void nt_sprite_renderer_emit_geometry(nt_resource_t atlas, uint32_t region_index, const float (*positions)[2], uint32_t vertex_count, const uint16_t *indices, uint32_t index_count,
                                      const float *world_matrix, uint32_t color_packed, const float *custom, uint8_t custom_bytes) {
    NT_ASSERT(positions != NULL && indices != NULL && world_matrix != NULL);
    NT_ASSERT(atlas.id != 0 && "nt_sprite_renderer_emit_geometry: invalid atlas handle");
    NT_ASSERT(nt_resource_is_ready(atlas) && "nt_sprite_renderer_emit_geometry: atlas must be READY");
    NT_ASSERT(vertex_count > 0U && index_count > 0U && "nt_sprite_renderer_emit_geometry: empty geometry");
    NT_ASSERT(index_count % 3U == 0U && "nt_sprite_renderer_emit_geometry: indices must form whole triangles");
    /* uint16 local indices address at most 65536 vertices; the byte sizes below must not wrap. */
    NT_ASSERT(vertex_count <= 65536U && index_count <= UINT32_MAX / 4U && "nt_sprite_renderer_emit_geometry: geometry too large");

    nt_atlas_region_handles_t h;
    nt_atlas_get_region_handles(atlas, region_index, &h);
    if (h.region->vertex_count == 0U) {
        return; /* tombstone */
    }
    nt_sprite_alloc_t a;
    if (!emit_begin(nt_resource_get(h.page_resource), vertex_count, index_count, &a)) {
        return;
    }
    const nt_sprite_material_t *m = &s_sprite.current;

    /* Sample at the region's UV centroid -- the corner of vertex 0 would
     * land at the texel boundary and bleed into neighbours under linear
     * filtering. Centroid is safely inside the region for any convex
     * polygon, and exactly the pixel center for a 4-vert axis-aligned
     * white region. uint16 atlas_u/v sums fit uint32 for the polygon
     * worst case (8 verts * 65535 << 2^32). */
    uint32_t sum_u = 0;
    uint32_t sum_v = 0;
    for (uint8_t i = 0; i < h.region->vertex_count; i++) {
        sum_u += h.uvs[i].atlas_u;
        sum_v += h.uvs[i].atlas_v;
    }
    const uint16_t shared_u = (uint16_t)(sum_u / h.region->vertex_count);
    const uint16_t shared_v = (uint16_t)(sum_v / h.region->vertex_count);

    const float *wm = world_matrix;
    for (uint32_t i = 0; i < vertex_count; i++) {
        const float px = positions[i][0];
        const float py = positions[i][1];
        nt_sprite_vertex_t *v = (nt_sprite_vertex_t *)(a.vertices + ((size_t)i * m->stride));
        v->position[0] = (wm[0] * px) + (wm[4] * py) + wm[12];
        v->position[1] = (wm[1] * px) + (wm[5] * py) + wm[13];
        v->position[2] = (wm[2] * px) + (wm[6] * py) + wm[14];
        v->texcoord[0] = shared_u;
        v->texcoord[1] = shared_v;
        memcpy(v->color, &color_packed, sizeof(v->color));
    }
    bake_custom_attrs(m, a.vertices, vertex_count, custom, custom_bytes);

    for (uint32_t i = 0; i < index_count; i++) {
        NT_ASSERT(indices[i] < vertex_count && "nt_sprite_renderer_emit_geometry: index out of range");
        a.indices[i] = a.base + (uint32_t)indices[i];
    }
    nt_gfx_draw_indexed(a.first_index, index_count, vertex_count);
}
// #endregion

// #region emit_slice9
/* src borders pick UV cut; dst borders set rendered corner/edge size. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void nt_sprite_renderer_emit_slice9(nt_resource_t atlas, uint32_t region_index, const float *world_matrix, float w, float h, float origin_x, float origin_y, const uint16_t src_lrtb[4],
                                    float slice9_scale, uint32_t color_packed, uint8_t flip_bits, const float *custom, uint8_t custom_bytes) {
    NT_ASSERT(atlas.id != 0 && "emit_slice9: invalid atlas handle");
    NT_ASSERT(nt_resource_is_ready(atlas) && "emit_slice9: atlas must be READY");
    NT_ASSERT(world_matrix != NULL && "emit_slice9: world_matrix must be non-NULL (pass NT_MATH_MAT4_IDENTITY for none)");
    NT_ASSERT(isfinite(w) && isfinite(h) && isfinite(origin_x) && isfinite(origin_y));
    NT_ASSERT(isfinite(slice9_scale) && slice9_scale > 0.0F && "emit_slice9: slice9_scale must be finite > 0");
    NT_ASSERT(w >= 0.0F && h >= 0.0F && "slice9 target dimensions must be non-negative");

    nt_atlas_region_handles_t rh;
    nt_atlas_get_region_handles(atlas, region_index, &rh);
    if (rh.region->vertex_count == 0U) {
        return; /* tombstone */
    }
    nt_sprite_alloc_t a;
    if (!emit_begin(nt_resource_get(rh.page_resource), 16U, 54U, &a)) {
        return;
    }
    const nt_sprite_material_t *m = &s_sprite.current;
    const slice9_grid_t grid = {
        .region = rh.region,
        .uvs = rh.uvs,
        .src_lrtb = (src_lrtb != NULL) ? src_lrtb : rh.region->slice9_lrtb,
        .band_per_px = slice9_scale * nt_atlas_get_inverse_pixels_per_unit(atlas),
        .w = w,
        .h = h,
        .origin_x = origin_x,
        .origin_y = origin_y,
        .color_packed = color_packed,
        .flip_bits = flip_bits,
        .world_matrix = world_matrix,
    };
    write_slice9(&grid, m->stride, &a);
    bake_custom_attrs(m, a.vertices, 16U, custom, custom_bytes);
    nt_gfx_draw_indexed(a.first_index, 54U, 16U);
}
// #endregion

// #region draw_list
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void write_item(const nt_render_item_t *item, const nt_sprite_comp_view_t *sv, const nt_transform_comp_view_t *tv, const nt_drawable_comp_view_t *dv, const nt_sprite_material_t *m,
                       nt_sprite_run_t *run) {
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

    const nt_sprite_resolved_region_t *resolved = &sv->resolved[s_idx];
    uint8_t flags = sv->flags[s_idx];
    NT_ASSERT((flags & NT_SPRITE_FLAG_RESOLVED) != 0 && "sprite render item: sprite is unresolved");
    NT_ASSERT(resolved->region != NULL && "sprite render item: resolved region is NULL");
    NT_ASSERT(resolved->region->vertex_count != 0 && "sprite render item: region is tombstoned");
    const nt_texture_region_t *r = resolved->region;
    NT_ASSERT(resolved->positions != NULL && resolved->uvs != NULL && resolved->indices != NULL);

    const float origin_x = (flags & NT_SPRITE_FLAG_ORIGIN_OV) ? sv->origin[s_idx][0] : r->origin_x;
    const float origin_y = (flags & NT_SPRITE_FLAG_ORIGIN_OV) ? sv->origin[s_idx][1] : r->origin_y;
    const uint8_t flip_bits = flags & (NT_SPRITE_FLAG_FLIP_X | NT_SPRITE_FLAG_FLIP_Y);
    const float ipu = resolved->ipu;
    nt_sprite_alloc_t a;

    // #region write_item_slice9_branch
    /* A zero override is the documented way to turn a baked nine-patch back into a plain quad. */
    const uint16_t *s9 = (flags & NT_SPRITE_FLAG_SLICE9_OV) ? sv->slice9_lrtb[s_idx] : r->slice9_lrtb;
    if ((s9[0] | s9[1] | s9[2] | s9[3]) != 0) {
        NT_ASSERT(isfinite(sv->slice9_scale[s_idx]) && sv->slice9_scale[s_idx] > 0.0F && "write_item: sv->slice9_scale[s_idx] must be finite > 0");
        const slice9_grid_t grid = {
            .region = r,
            .uvs = resolved->uvs,
            .src_lrtb = s9,
            .band_per_px = sv->slice9_scale[s_idx] * ipu,
            .w = (float)r->source_w * ipu,
            .h = (float)r->source_h * ipu,
            .origin_x = origin_x,
            .origin_y = origin_y,
            .color_packed = dv->colors_packed[d_idx],
            .flip_bits = flip_bits,
            .world_matrix = tv->world_matrices[t_idx],
        };
        run_alloc(run, 16U, 54U, &a);
        write_slice9(&grid, m->stride, &a);
        bake_custom_attrs(m, a.vertices, 16U, NULL, 0U);
        return;
    }
    // #endregion
    run_alloc(run, r->vertex_count, r->index_count, &a);
    write_region(r, resolved->positions, resolved->uvs, resolved->indices, ipu, tv->world_matrices[t_idx], origin_x, origin_y, dv->colors_packed[d_idx], flip_bits, m->stride, &a);
    bake_custom_attrs(m, a.vertices, r->vertex_count, NULL, 0U);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void nt_sprite_renderer_draw_list(const nt_render_item_t *items, uint32_t count) {
    if (count == 0) {
        return;
    }
    NT_ASSERT(items != NULL);

    nt_sprite_comp_view_t sv = nt_sprite_comp_view();
    nt_transform_comp_view_t tv = nt_transform_comp_view();
    nt_drawable_comp_view_t dv = nt_drawable_comp_view();

    /* Runs split per atlas page too, so consecutive runs often share a material;
     * nothing can replace its program inside this call, so the last resolve holds. */
    nt_sprite_material_t m = {0};
    uint32_t run_start = 0;
    while (run_start < count) {
        uint32_t run_end = run_start + 1;
        while (run_end < count && items[run_end].batch_key == items[run_start].batch_key) {
            run_end++;
        }

        nt_entity_t leader = {.id = items[run_start].entity};
        const nt_material_t *mat = nt_material_comp_handle(leader);
        const nt_material_info_t *mat_info = nt_material_get_info(*mat);
        NT_ASSERT(mat_info != NULL && "sprite draw_list: material destroyed while its items are drawn");
        if (mat->id != m.material.id) {
            resolve_material(*mat, mat_info, &m);
        }

        /* Equal batch keys share the page: the run draws with its leader's. */
        const uint16_t leader_sprite = sv.sparse_indices[nt_entity_index(leader)];
        NT_ASSERT(leader_sprite != NT_INVALID_COMP_INDEX && "sprite render item: entity has no sprite component");
        /* Checked here as well as per item: an unresolved leader has no page and would skip the run. */
        NT_ASSERT((sv.flags[leader_sprite] & NT_SPRITE_FLAG_RESOLVED) != 0 && sv.resolved[leader_sprite].region != NULL && sv.resolved[leader_sprite].region->vertex_count != 0 &&
                  "sprite render item: sprite is unresolved or tombstoned");
        const uint32_t page_tex = nt_resource_get(sv.resolved[leader_sprite].page_resource);
        if (m.pipeline.id == 0 || (mat_info->tex_count > 0 && page_tex == 0)) {
            run_start = run_end;
            continue;
        }
        record_state(&m, page_tex);

        nt_sprite_run_t run = {.stride = m.stride};
        for (uint32_t i = run_start; i < run_end; i++) {
            write_item(&items[i], &sv, &tv, &dv, &m, &run);
        }
        nt_gfx_draw_indexed(run.first_index, run.index_count, run.vertex_count);
        run_start = run_end;
    }
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

void nt_sprite_renderer_test_last_emit(nt_sprite_test_emit_t *out) {
    NT_ASSERT(out != NULL);
    *out = s_sprite.last_emit;
}

uint32_t nt_sprite_renderer_test_pipeline_cache_count(void) { return s_sprite.count; }
uint32_t nt_sprite_renderer_test_vertex_input_cache_count(void) { return s_sprite.vi_count; }
#endif
// #endregion
