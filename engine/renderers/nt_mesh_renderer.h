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
 * until draw_list returns. Store the returned token unchanged; use separate
 * draw_list calls for an explicit boundary. */
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
    /* Vertex-input versions kept per mesh (one per distinct derived layout x
     * color mode drawing that mesh). Exceeding it ASSERTS -- silent eviction
     * would hide re-creation thrash as an invisible perf regression; raise the
     * knob instead. Default: 4 (3-4 versions is the expected population). */
    uint16_t max_mesh_layouts;
} nt_mesh_renderer_desc_t;

static inline nt_mesh_renderer_desc_t nt_mesh_renderer_desc_defaults(void) { return (nt_mesh_renderer_desc_t){.max_instances = 4096, .max_pipelines = 64, .max_mesh_layouts = 4}; }

nt_result_t nt_mesh_renderer_init(const nt_mesh_renderer_desc_t *desc);
void nt_mesh_renderer_shutdown(void);
/* Retains CPU storage and initialization; drops GPU caches and recreates buffers.
 * Failure returns NT_ERR_INIT_FAILED: retry before drawing, or shut down.
 * Inactive modules are unchanged and return NT_OK. */
nt_result_t nt_mesh_renderer_restore_gpu(void);

/* Contract: caller must pre-filter `items` by visibility — the renderer draws
 * every entry unconditionally and does not consult drawable_comp's visible
 * flag, color alpha, or entity-enabled state. Use nt_render_is_visible()
 * (engine/render/nt_render_util.h) as the canonical filter when building
 * the items array. */
/* batch_key must come from each item's current material/mesh bindings. Entities,
 * bindings, and referenced resources stay live and unchanged through this call. */
/* items may be NULL only when count is 0; otherwise it is borrowed for the call. */
void nt_mesh_renderer_draw_list(const nt_render_item_t *items, uint32_t count);

/* State transitions of the last draw_list call: how often the material block was
 * replayed, and how many pipeline / vertex-input binds that call actually issued.
 * Reset at the top of every draw_list. */
uint32_t nt_mesh_renderer_frame_material_applies(void);
uint32_t nt_mesh_renderer_frame_pipeline_binds(void);
uint32_t nt_mesh_renderer_frame_vertex_input_binds(void);

// #region test_access
#ifdef NT_TEST_ACCESS
uint32_t nt_mesh_renderer_test_pipeline_cache_count(void);
/* Live entries across the whole vertex-input versions table. */
uint32_t nt_mesh_renderer_test_vertex_input_count(void);
uint32_t nt_mesh_renderer_test_draw_call_count(void);
uint32_t nt_mesh_renderer_test_instance_total(void);
uint32_t nt_mesh_renderer_test_ring_cursor(void);
bool nt_mesh_renderer_test_initialized(void);
#endif
// #endregion

/* Stream type → vertex attribute type mapping (used by pipeline builder, testable) */
nt_vertex_type_t nt_stream_to_vertex_type(uint8_t type);

#endif /* NT_MESH_RENDERER_H */
