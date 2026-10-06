#ifndef NT_SPRITE_RENDERER_H
#define NT_SPRITE_RENDERER_H

#include "core/nt_assert.h"
#include "core/nt_types.h"
#include "material/nt_material.h"
#include "pool/nt_pool.h"
#include "render/nt_render_defs.h"
#include "resource/nt_resource.h"

_Static_assert(NT_POOL_SLOT_SHIFT == 16 && NT_POOL_SLOT_MASK == UINT16_MAX, "sprite batch key requires 16-bit material slots");

/* ---- Vertex format — 20 bytes ---- */

typedef struct {
    float position[3];    /* 12 B */
    uint16_t texcoord[2]; /*  4 B — normalized to [0,1] in shader (atlas UVs are
                           * stored as u16 0..65535 in the blob; emit copies them
                           * directly without float intermediate) */
    uint8_t color[4];     /*  4 B */
} nt_sprite_vertex_t;
_Static_assert(sizeof(nt_sprite_vertex_t) == 20, "sprite vertex must be 20 bytes");

/* Byte cap for a material's appended custom per-vertex attribute block (opt-in).
 * Headroom for four FLOAT4 blocks (a_radial + a_tint + a_uvrect + a_layout) = 64 B,
 * spent in full by the radial-image material. Only custom-attr materials pay this;
 * plain sprites keep the locked 20 B vertex. */
#ifndef NT_SPRITE_CUSTOM_STRIDE_MAX
#define NT_SPRITE_CUSTOM_STRIDE_MAX 64
#endif

/* Only RESOLVED, non-tombstoned sprites have a page_resource. Material and the
 * currently published texture must stay live and unrebound until draw_list returns.
 * Store the returned token unchanged: draw_list takes a run's page from its first item. */
static inline uint32_t nt_sprite_renderer_batch_key(nt_material_t material, nt_resource_t page_resource) {
    uint32_t material_slot = nt_pool_slot_index(material.id);
    NT_ASSERT(material_slot != 0);
    NT_ASSERT(page_resource.id != 0);
    uint32_t texture_slot = nt_pool_slot_index(nt_resource_get(page_resource));
    return (material_slot << NT_POOL_SLOT_SHIFT) | texture_slot;
}

/* ---- Contracts ----
 *
 * Geometry goes to gfx frame storage: the host sets frame_capacity[NT_GFX_FRAME_VERTEX] and
 * [NT_GFX_FRAME_INDEX] (asserted). Every emit and run records its draw at the call, so it
 * needs an open pass, and call order is draw order; gfx drops equal binds and merges
 * contiguous draws.
 *
 * Uniforms are program state: the renderer writes a material's params only when they differ
 * from what it last wrote, so nothing else may write the uniforms of a program that sprite
 * materials use.
 *
 * A material that samples the atlas declares its page sampler at slot 0, whose declared
 * resource is never sampled: the renderer substitutes the page texture and the material may
 * override the sampler. Every declared slot must resolve to a texture (register a placeholder
 * for async loads). No textures = no page, e.g. nt_ui_radial's flat SDF. */

/* Destroys the cached pipelines and vertex inputs. Required before nt_gfx_shutdown or a gfx
 * re-init: new gfx pools reuse handle ids. */
void nt_sprite_renderer_shutdown(void);

/* Contracts:
 *   1. Caller pre-filters invisible, unresolved, and tombstoned sprites;
 *      renderer draws every entry.
 *   2. Equal batch keys share material and page: a run draws with its first item's page.
 *   3. Frame UBOs (e.g. view_proj) are shader-specific — register and bind
 *      them before draw_list; renderer does not touch UBOs.
 *   4. Entities, required components, and material bindings stay live and
 *      unchanged through draw_list.
 * Does not change the material selected by set_material. */
/* items may be NULL only when count is 0; otherwise it is borrowed for the call. */
void nt_sprite_renderer_draw_list(const nt_render_item_t *items, uint32_t count);

/* ---- Non-ECS public emit surface ---- */

/* Selects the material the emits below draw with, for the current gfx frame. Requires a valid
 * material with an assigned program; an unready program makes the emits draw nothing. Another
 * set_material (the UI walker, a CUSTOM handler) replaces the selection, so a caller selects
 * its own before emitting. Numeric params are compared when each emit is recorded. */
void nt_sprite_renderer_set_material(nt_material_t mat);

/* Every emit below takes an optional custom block (custom, custom_bytes), baked into each of
 * its vertices like color. A custom-attr material (attr_map_count > 0) needs custom_bytes ==
 * attr_map_count*16 (asserted), or 0 to bake the material's attr defaults; with neither it
 * asserts. A plain material takes NULL, 0.
 *
 * Every emit starts at a multiple of 4 vertices, so a shader may derive a quad corner from
 * gl_VertexID & 3. */

/* Emit one atlas region at one mat4 transform.
 *
 *   atlas         - must be a READY atlas resource (asserted).
 *   region_index  - tombstoned regions silently no-op.
 *   world_matrix  - 16-float column-major mat4 (cglm convention). Only
 *                   m[0/1/2/4/5/6/12/13/14] are read: columns 0+1 carry
 *                   2D rotation/scale, m[12/13/14] carry translation.
 *   origin_x, _y  - pivot in normalized region-space (e.g. {0.5, 0.5}).
 *   color_packed  - 0xAABBGGRR, straight alpha (the shader premultiplies).
 *   flip_bits     - NT_SPRITE_FLAG_FLIP_X | _FLIP_Y, 0 = none. */
void nt_sprite_renderer_emit_region(nt_resource_t atlas, uint32_t region_index, const float *world_matrix, float origin_x, float origin_y, uint32_t color_packed, uint8_t flip_bits,
                                    const float *custom, uint8_t custom_bytes);

/* Emit a 9-quad slice9 image. Same vertex format, local space and pipeline as
 * emit_region: the grid is built Y-up around the pivot and flip_bits mirror it
 * by negating positions, so the caller's world_matrix decides which way is up.
 *
 *   atlas, region_index - must be READY; tombstones no-op.
 *   world_matrix        - 16-float column-major mat4, same convention as
 *                         emit_region. Pass NT_MATH_MAT4_IDENTITY for none.
 *   w, h                - rendered size in the matrix's units.
 *   origin_x, _y        - pivot, normalized over w/h (e.g. {0.5, 0.5}).
 *   src_lrtb            - src borders {l,r,t,b} in source pixels; NULL = read
 *                         atlas-baked borders for this region.
 *   slice9_scale        - dst corner size = src × scale (always). Pass 1.0F
 *                         for src verbatim. Corners proportionally shrunk if
 *                         total > w/h. Border pixels convert to w/h units
 *                         through the atlas's pixels_per_unit, so swapping an
 *                         SD atlas for a denser HD one keeps the corner size.
 *                         Bands stay exact — no pixel snapping.
 *   color_packed        - 0xAABBGGRR.
 *   flip_bits           - NT_SPRITE_FLAG_FLIP_X | _FLIP_Y. The grid is CCW like
 *                         blob triangles; mirroring reverses that, as in emit_region.
 *
 * Emits 16 vertices + 54 indices (4x4 shared grid). */
void nt_sprite_renderer_emit_slice9(nt_resource_t atlas, uint32_t region_index, const float *world_matrix, float w, float h, float origin_x, float origin_y, const uint16_t src_lrtb[4],
                                    float slice9_scale, uint32_t color_packed, uint8_t flip_bits, const float *custom, uint8_t custom_bytes);

/* Emit an arbitrary triangle list sampling a single UV from the given
 * atlas region. Intended for solid-color shapes drawn against a
 * white-pixel region (rounded corners, ring sectors, custom fan/strip).
 *
 *   atlas, region_index - the region whose UV centroid is sampled by
 *                         every emitted vertex. Centroid (mean of region
 *                         vertex UVs) avoids texel-corner sampling that
 *                         would bleed neighbours under linear filtering.
 *                         Region must be READY and have vertex_count > 0;
 *                         tombstones no-op.
 *   positions           - vertex_count XY pairs in local space.
 *   indices             - index_count uint16s, local to this emit. Must
 *                         reference indices < vertex_count.
 *   world_matrix        - 16-float column-major mat4 (cglm convention),
 *                         same subset read as emit_region.
 *   color_packed        - 0xAABBGGRR. */
void nt_sprite_renderer_emit_geometry(nt_resource_t atlas, uint32_t region_index, const float (*positions)[2], uint32_t vertex_count, const uint16_t *indices, uint32_t index_count,
                                      const float *world_matrix, uint32_t color_packed, const float *custom, uint8_t custom_bytes);

// #region test_access
#ifdef NT_TEST_ACCESS
/* Resolved vertex layout snapshot for a material: stride + per-attr GL
 * location/offset. attr_count==3 for a plain material (base 20 B), 3+N for a
 * custom-attr material (extended stride). */
typedef struct {
    uint32_t stride;
    uint32_t attr_count;
    uint32_t locations[16]; /* NT_GFX_MAX_VERTEX_ATTRS */
    uint32_t offsets[16];
} nt_sprite_layout_info_t;
void nt_sprite_renderer_test_layout(nt_material_t mat, nt_sprite_layout_info_t *out);

/* The last emit or draw_list item. vertices points at its first vertex in frame storage
 * staging, valid until the next nt_gfx_begin_frame. */
typedef struct {
    const uint8_t *vertices;
    uint32_t stride;
    uint32_t first_vertex;
    uint32_t vertex_count;
    uint32_t first_index;
    uint32_t index_count;
} nt_sprite_test_emit_t;
void nt_sprite_renderer_test_last_emit(nt_sprite_test_emit_t *out);
uint32_t nt_sprite_renderer_test_pipeline_cache_count(void);
uint32_t nt_sprite_renderer_test_vertex_input_cache_count(void);
#endif
// #endregion

#endif /* NT_SPRITE_RENDERER_H */
