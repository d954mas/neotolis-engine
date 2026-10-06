#ifndef NT_MESH_RENDERER_H
#define NT_MESH_RENDERER_H

#include "core/nt_assert.h"
#include "core/nt_types.h"
#include "graphics/nt_gfx.h"
#include "material/nt_material.h"
#include "pool/nt_pool.h"
#include "render/nt_render_defs.h"

#include <stddef.h>

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

/* One GPU instance (locations 4-6 world rows, 7 color); the instance buffer stride is its size. */
typedef struct {
    float world_rows[3][4];
    uint32_t color; /* RGBA8 0xAABBGGRR (nt_color_pack) */
} nt_mesh_instance_t;

_Static_assert(sizeof(nt_mesh_instance_t) == 52 && offsetof(nt_mesh_instance_t, color) == 48, "mesh instance layout");

/* The instance world rows of both mesh renderers: the transpose of the affine part of a
 * column-major mat4, row r holding (m[r], m[4 + r], m[8 + r], m[12 + r]). */
static inline void nt_mesh_instance_world_rows(float rows[3][4], const float world[16]) {
    rows[0][0] = world[0];
    rows[0][1] = world[4];
    rows[0][2] = world[8];
    rows[0][3] = world[12];
    rows[1][0] = world[1];
    rows[1][1] = world[5];
    rows[1][2] = world[9];
    rows[1][3] = world[13];
    rows[2][0] = world[2];
    rows[2][1] = world[6];
    rows[2][2] = world[10];
    rows[2][3] = world[14];
}

typedef struct {
    uint16_t max_pipelines; /* pipeline cache capacity, default: 64 */
    /* Vertex-input versions kept per mesh (one per distinct derived layout
     * drawing that mesh). Exceeding it ASSERTS -- silent eviction would hide
     * re-creation thrash as an invisible perf regression; raise the knob
     * instead. Default: 4. */
    uint16_t max_mesh_layouts;
} nt_mesh_renderer_desc_t;

static inline nt_mesh_renderer_desc_t nt_mesh_renderer_desc_defaults(void) { return (nt_mesh_renderer_desc_t){.max_pipelines = 64, .max_mesh_layouts = 4}; }

/* desc is required, non-NULL and borrowed for the duration of the call. */
nt_result_t nt_mesh_renderer_init(const nt_mesh_renderer_desc_t *desc);
void nt_mesh_renderer_shutdown(void);
/* Retains CPU storage and initialization; drops pipeline and vertex-input
 * caches. Inactive modules are unchanged. */
void nt_mesh_renderer_restore_gpu(void);

/* Records one instanced draw of mesh with material in the current pass: count instances
 * (nt_mesh_instance_t) at byte offset in NT_GFX_FRAME_VERTEX, from
 * nt_gfx_frame_alloc(NT_GFX_FRAME_VERTEX, count * sizeof(nt_mesh_instance_t), 4, &offset),
 * filled before the next nt_gfx call. One allocation may be drawn in any number of passes of
 * the frame. A material whose program is not ready, or a pipeline or vertex input that could
 * not be created, records nothing. Resolves pipeline, vertex input and material state per
 * call: draw a batch, not one object per call. count > 0. */
void nt_mesh_renderer_draw(nt_mesh_t mesh, nt_material_t material, uint32_t offset, uint32_t count);

/* ECS adapter. Caller filters visibility (nt_render_is_visible) and order; the renderer draws
 * every item. Adjacent equal batch keys form one run: its world and drawable color are packed
 * into vertex frame storage and drawn as one instanced draw in the current pass. Material
 * state is applied once for adjacent runs of one material within the call. batch_key must
 * come from each item's current material/mesh bindings; items may be NULL only when count
 * is 0 and are borrowed for the call. Every item needs transform and drawable components. */
void nt_mesh_renderer_draw_list(const nt_render_item_t *items, uint32_t count);

// #region test_access
#ifdef NT_TEST_ACCESS
uint32_t nt_mesh_renderer_test_pipeline_cache_count(void);
/* Live entries across the whole vertex-input versions table. */
uint32_t nt_mesh_renderer_test_vertex_input_count(void);
bool nt_mesh_renderer_test_initialized(void);
#endif
// #endregion

#endif /* NT_MESH_RENDERER_H */
