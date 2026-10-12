#ifndef NT_MESH_RENDERER_H
#define NT_MESH_RENDERER_H

#include "core/nt_assert.h"
#include "core/nt_types.h"
#include "graphics/nt_gfx.h"
#include "material/nt_material.h"
#include "pool/nt_pool.h"
#include "render/nt_render_defs.h"

#include <stddef.h>

/* Must come from the target compile-defines: nt_mesh_renderer (0) or nt_skinned_mesh_renderer (1). */
#ifndef NT_MESH_RENDERER_SKINNED
#error "NT_MESH_RENDERER_SKINNED not defined -- link nt_mesh_renderer or nt_skinned_mesh_renderer"
#endif

/* Pipeline cache capacity; raise with -DNT_MESH_RENDERER_MAX_PIPELINES=N. */
#ifndef NT_MESH_RENDERER_MAX_PIPELINES
#define NT_MESH_RENDERER_MAX_PIPELINES 64
#endif
_Static_assert(NT_MESH_RENDERER_MAX_PIPELINES <= 65535, "NT_MESH_RENDERER_MAX_PIPELINES overflows the uint16 cache counter");

/* Units a program has left after the material's slots. */
#define NT_MESH_MAX_DRAW_TEXTURES (NT_GFX_MAX_TEXTURE_SLOTS - NT_MATERIAL_MAX_TEXTURES)

_Static_assert(NT_POOL_SLOT_SHIFT == 16 && NT_POOL_SLOT_MASK == UINT16_MAX, "mesh batch key requires 16-bit pool slots");

/* Handles must match the item's current bindings at draw_list. Store the returned
 * token unchanged; use separate lists for an explicit boundary. */
static inline uint32_t nt_mesh_renderer_batch_key(nt_material_t material, nt_mesh_t mesh) {
    uint32_t material_slot = nt_pool_slot_index(material.id);
    uint32_t mesh_slot = nt_pool_slot_index(mesh.id);
    NT_ASSERT(material_slot != 0);
    NT_ASSERT(mesh_slot != 0);
    return (material_slot << NT_POOL_SLOT_SHIFT) | mesh_slot;
}

/* One GPU instance (NT_INST_LOC_WORLD_ROW0..2 world rows, NT_INST_LOC_COLOR color); the instance buffer stride is its size. */
typedef struct {
    float world_rows[3][4];
    uint32_t color; /* RGBA8 0xAABBGGRR (nt_color_pack) */
} nt_mesh_instance_t;

_Static_assert(sizeof(nt_mesh_instance_t) == 52 && offsetof(nt_mesh_instance_t, color) == 48, "mesh instance layout");

/* Instance world rows of every mesh instance layout: the transposed affine part of a column-major mat4. */
static inline void nt_mesh_instance_world_rows(float rows[3][4], const float world[16]) {
    for (int r = 0; r < 3; r++) {
        rows[r][0] = world[r];
        rows[r][1] = world[4 + r];
        rows[r][2] = world[8 + r];
        rows[r][3] = world[12 + r];
    }
}

/* Call after nt_gfx_init: registers the instance layouts, whose handles a new nt_gfx_init
 * invalidates, so shut down before it and init again after. */
void nt_mesh_renderer_init(void);
void nt_mesh_renderer_shutdown(void);

/* Layout of nt_mesh_instance_t, for nt_mesh_renderer_draw. */
nt_instance_layout_t nt_mesh_renderer_instance_layout(void);

/* Records one instanced draw in the current pass: count > 0 instances of `layout` at byte offset
 * of `instances` (NT_BUFFER_VERTEX): a frame vertex stream's buffer (nt_gfx_frame_buffer of the
 * stream they were allocated from, filled before nt_gfx_end_frame; one allocation may be drawn in
 * any number of passes) or a game-owned buffer. The mesh owns one vertex input per frame stream
 * and one for every other buffer (nt_gfx_mesh_vertex_input), pinned to the first layout drawn
 * through it, so two layouts of one mesh use different streams. draw_textures (NULL iff
 * draw_texture_count is 0) are bindings owned by the draw, applied with the material's textures
 * as one set; borrowed for the call. Records nothing while the program is not ready or a pipeline
 * or vertex input cannot be created. */
void nt_mesh_renderer_draw(nt_mesh_t mesh, nt_material_t material, nt_instance_layout_t layout, nt_buffer_t instances, uint32_t offset, uint32_t count, const nt_gfx_texture_binding_t *draw_textures,
                           uint8_t draw_texture_count);

/* ECS adapter: adjacent equal batch keys form one run, packed into frame vertex stream `stream`
 * from transform and drawable and drawn as one instanced draw in the current pass. A pass with
 * its own stream keeps its runs' offsets, and so its instance pointers, while counts stay. The
 * caller filters visibility (nt_render_is_visible) and order. batch_key must come from each
 * item's current bindings; items may be NULL only when count is 0. */
void nt_mesh_renderer_draw_list(uint32_t stream, const nt_render_item_t *items, uint32_t count);

#if NT_MESH_RENDERER_SKINNED
/* One GPU instance (NT_INST_LOC_* world rows 8-10, color 11, skin origins 12, skin alpha 13);
 * the instance buffer stride is its size. */
typedef struct {
    float world_rows[3][4];
    uint16_t skin_origins[4]; /* deformation texel origins x0, y0, x1, y1 */
    float skin_alpha;         /* blend between the two deformation frames */
    uint32_t color;           /* RGBA8 0xAABBGGRR (nt_color_pack) */
} nt_skinned_mesh_instance_t;

_Static_assert(sizeof(nt_skinned_mesh_instance_t) == 64 && offsetof(nt_skinned_mesh_instance_t, skin_origins) == 48 && offsetof(nt_skinned_mesh_instance_t, skin_alpha) == 56 &&
                   offsetof(nt_skinned_mesh_instance_t, color) == 60,
               "skinned mesh instance layout");

/* Layout of nt_skinned_mesh_instance_t. A skinned core draw passes it with one draw texture
 * {nt_hash32_str("u_skin_matrices"), deformation, NT_SAMPLER_DEFAULT}; the material
 * (common/skin.glsl) does not declare u_skin_matrices. */
nt_instance_layout_t nt_mesh_renderer_skinned_instance_layout(void);

/* nt_mesh_renderer_draw_list for skinned items: runs also split on the deformation texture,
 * drawn as each run's u_skin_matrices draw texture, and every item also needs a skin component
 * with a deformation binding of this frame. Call after nt_skeletal_gpu_flush: a texture write
 * precedes the draws that sample it (render/architecture.md, Draw-phase command stream). */
void nt_mesh_renderer_draw_list_skinned(uint32_t stream, const nt_render_item_t *items, uint32_t count);
#endif

// #region test_access
#ifdef NT_TEST_ACCESS
uint32_t nt_mesh_renderer_test_pipeline_cache_count(void);
#endif
// #endregion

#endif /* NT_MESH_RENDERER_H */
