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

/* Records one instanced draw in the current pass: count > 0 instances (nt_mesh_instance_t) at byte
 * offset of `instances`, a frame vertex stream's buffer (filled before nt_gfx_end_frame) or a
 * game-owned NT_BUFFER_VERTEX buffer. Records nothing while the program is not ready or a pipeline
 * or vertex input cannot be created. */
void nt_mesh_renderer_draw(nt_mesh_t mesh, nt_material_t material, nt_buffer_t instances, uint32_t offset, uint32_t count);

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

/* nt_mesh_renderer_draw for skinned instances (nt_skinned_mesh_instance_t). `deformation` is drawn
 * as u_skin_matrices, which the material (common/skin.glsl) does not declare. Call after
 * nt_skeletal_gpu_flush. */
void nt_mesh_renderer_draw_skinned(nt_mesh_t mesh, nt_material_t material, nt_texture_t deformation, nt_buffer_t instances, uint32_t offset, uint32_t count);

/* nt_mesh_renderer_draw_list for skinned items: runs also split on the deformation texture,
 * drawn as each run's u_skin_matrices draw texture, and every item also needs a skin component
 * with a deformation binding of this frame. Call after nt_skeletal_gpu_flush: a texture write
 * precedes the draws that sample it (render/architecture.md, Draw-phase command stream). */
void nt_mesh_renderer_draw_list_skinned(uint32_t stream, const nt_render_item_t *items, uint32_t count);
#endif

#endif /* NT_MESH_RENDERER_H */
