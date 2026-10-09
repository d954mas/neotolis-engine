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

/* Instance world rows of both mesh renderers: the transposed affine part of a column-major mat4. */
static inline void nt_mesh_instance_world_rows(float rows[3][4], const float world[16]) {
    for (int r = 0; r < 3; r++) {
        rows[r][0] = world[r];
        rows[r][1] = world[4 + r];
        rows[r][2] = world[8 + r];
        rows[r][3] = world[12 + r];
    }
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

/* Records one instanced draw in the current pass: count > 0 instances (nt_mesh_instance_t) at
 * byte offset in frame vertex stream `stream` (the one they were allocated from), filled before
 * nt_gfx_end_frame; one allocation may be drawn in any number of passes. Records nothing while
 * the program is not ready or a pipeline or vertex input cannot be created. */
void nt_mesh_renderer_draw(nt_mesh_t mesh, nt_material_t material, uint32_t stream, uint32_t offset, uint32_t count);

/* ECS adapter: adjacent equal batch keys form one run, packed into frame vertex stream `stream`
 * from transform and drawable and drawn as one instanced draw in the current pass. A pass with
 * its own stream keeps its runs' offsets, and so its instance pointers, while counts stay. The
 * caller filters visibility (nt_render_is_visible) and order. batch_key must come from each
 * item's current bindings; items may be NULL only when count is 0. */
void nt_mesh_renderer_draw_list(uint32_t stream, const nt_render_item_t *items, uint32_t count);

// #region test_access
#ifdef NT_TEST_ACCESS
uint32_t nt_mesh_renderer_test_pipeline_cache_count(void);
/* Live entries across the whole vertex-input versions table. */
uint32_t nt_mesh_renderer_test_vertex_input_count(void);
bool nt_mesh_renderer_test_initialized(void);
#endif
// #endregion

#endif /* NT_MESH_RENDERER_H */
