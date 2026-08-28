#ifndef NT_MESH_RENDERER_H
#define NT_MESH_RENDERER_H

#include "core/nt_assert.h"
#include "core/nt_types.h"
#include "graphics/nt_gfx.h"
#include "material/nt_material.h"
#include "pool/nt_pool.h"
#include "render/nt_render_defs.h"

_Static_assert(NT_POOL_SLOT_SHIFT == 16 && NT_POOL_SLOT_MASK == UINT16_MAX, "mesh batch key requires 16-bit pool slots");

/* Handles must match the item's current bindings and stay live and unrebound
 * until draw_list returns. Packing is exact for simultaneously live slots. */
static inline uint32_t nt_mesh_renderer_batch_key(nt_material_t material, nt_mesh_t mesh) {
    uint32_t material_slot = nt_pool_slot_index(material.id);
    uint32_t mesh_slot = nt_pool_slot_index(mesh.id);
    NT_ASSERT(material_slot != 0);
    NT_ASSERT(mesh_slot != 0);
    return (material_slot << NT_POOL_SLOT_SHIFT) | mesh_slot;
}

typedef struct {
    uint16_t max_instances; /* max per single instanced draw call, default: 4096 */
    uint16_t max_pipelines; /* pipeline cache capacity, default: 64 */
} nt_mesh_renderer_desc_t;

static inline nt_mesh_renderer_desc_t nt_mesh_renderer_desc_defaults(void) { return (nt_mesh_renderer_desc_t){.max_instances = 4096, .max_pipelines = 64}; }

nt_result_t nt_mesh_renderer_init(const nt_mesh_renderer_desc_t *desc);
void nt_mesh_renderer_shutdown(void);
void nt_mesh_renderer_restore_gpu(void);

/* Contract: caller must pre-filter `items` by visibility — the renderer draws
 * every entry unconditionally and does not consult drawable_comp's visible
 * flag, color alpha, or entity-enabled state. Use nt_render_is_visible()
 * (engine/render/nt_render_util.h) as the canonical filter when building
 * the items array. */
/* batch_key must come from each item's current material/mesh bindings. Entities,
 * bindings, and referenced resources stay live and unchanged through this call. */
/* items may be NULL only when count is 0; otherwise it is borrowed for the call. */
void nt_mesh_renderer_draw_list(const nt_render_item_t *items, uint32_t count);

// #region test_access
#ifdef NT_TEST_ACCESS
uint32_t nt_mesh_renderer_test_pipeline_cache_count(void);
uint32_t nt_mesh_renderer_test_draw_call_count(void);
uint32_t nt_mesh_renderer_test_instance_total(void);
uint32_t nt_mesh_renderer_test_ring_cursor(void);
#endif
// #endregion

/* Stream type → vertex attribute type mapping (used by pipeline builder, testable) */
nt_vertex_type_t nt_stream_to_vertex_type(uint8_t type);

#endif /* NT_MESH_RENDERER_H */
