#ifndef NT_MESH_RENDERER_H
#define NT_MESH_RENDERER_H

#include "core/nt_assert.h"
#include "core/nt_types.h"
#include "graphics/nt_gfx.h"
#include "material/nt_material.h"
#include "pool/nt_pool.h"
#include "render/nt_render_defs.h"

_Static_assert(NT_POOL_SLOT_SHIFT == 16 && NT_POOL_SLOT_MASK == UINT16_MAX, "mesh batch key requires 16-bit pool slots");

/* Handles must match the item's current bindings at prepare. Store the returned
 * token unchanged; use separate lists for an explicit boundary. */
static inline uint32_t nt_mesh_renderer_batch_key(nt_material_t material, nt_mesh_t mesh) {
    uint32_t material_slot = nt_pool_slot_index(material.id);
    uint32_t mesh_slot = nt_pool_slot_index(mesh.id);
    NT_ASSERT(material_slot != 0);
    NT_ASSERT(mesh_slot != 0);
    return (material_slot << NT_POOL_SLOT_SHIFT) | mesh_slot;
}

typedef struct {
    uint16_t max_pipelines; /* pipeline cache capacity, default: 64 */
    /* Vertex-input versions kept per mesh (one per distinct derived layout x
     * color mode drawing that mesh). Exceeding it ASSERTS -- silent eviction
     * would hide re-creation thrash as an invisible perf regression; raise the
     * knob instead. Default: 4 (3-4 versions is the expected population). */
    uint16_t max_mesh_layouts;
} nt_mesh_renderer_desc_t;

static inline nt_mesh_renderer_desc_t nt_mesh_renderer_desc_defaults(void) { return (nt_mesh_renderer_desc_t){.max_pipelines = 64, .max_mesh_layouts = 4}; }

/* One resolved instanced draw, written by a mesh renderer's prepare: draw reads
 * no entity component. Valid until the next nt_gfx_begin_frame or GPU
 * restore (skinned runs also until the next nt_skeletal_gpu_begin_frame); the
 * referenced material, its program and textures, and the mesh stay live until
 * the last draw. Fields are renderer-filled; copy, filter or concatenate runs,
 * never build them by hand. */
typedef struct {
    nt_pipeline_t pipeline;
    nt_vertex_input_t vertex_input;
    nt_material_t material;
    nt_texture_t supplied_texture; /* replaces the material texture at supplied_slot; 0 = none */
    uint32_t offset;               /* vertex frame storage byte offset of the first instance */
    uint32_t instance_count;
    uint32_t index_count; /* 0 = non-indexed */
    uint32_t vertex_count;
    uint8_t supplied_slot;
    uint8_t color_mode;     /* nt_color_mode_t */
    uint8_t color_location; /* the instance color attribute, white for NT_COLOR_MODE_NONE */
} nt_mesh_run_t;

/* desc is required, non-NULL and borrowed for the duration of the call. */
nt_result_t nt_mesh_renderer_init(const nt_mesh_renderer_desc_t *desc);
void nt_mesh_renderer_shutdown(void);
/* Retains CPU storage and initialization; drops pipeline and vertex-input
 * caches. Inactive modules are unchanged. */
void nt_mesh_renderer_restore_gpu(void);

/* Contract: caller must pre-filter `items` by visibility — the renderer draws
 * every entry unconditionally and does not consult drawable_comp's visible
 * flag, color alpha, or entity-enabled state. Use nt_render_is_visible()
 * (engine/render/nt_render_util.h) as the canonical filter when building
 * the items array. */
/* batch_key must come from each item's current material/mesh bindings; adjacent
 * equal keys merge into one run. items may be NULL only when count is 0; it is
 * borrowed for the call, and bindings may change after it returns. */
/* Resolves pipeline and vertex input per run (creating them on a cache miss),
 * packs world and color of drawable runs into one vertex frame storage allocation and
 * writes the runs; returns their count. Runs whose program is not ready or
 * whose pipeline/vertex input failed are skipped. Writes no buffer.
 * Call at any point of the frame before the draws.
 * max_runs >= count always suffices; fewer asserts when exceeded. */
uint32_t nt_mesh_renderer_prepare(const nt_render_item_t *items, uint32_t count, nt_mesh_run_t *runs, uint32_t max_runs);
/* Executes runs of this gfx frame in order in the current pass, any
 * number of times. runs may be NULL only when run_count is 0. */
void nt_mesh_renderer_draw(const nt_mesh_run_t *runs, uint32_t run_count);

// #region test_access
#ifdef NT_TEST_ACCESS
uint32_t nt_mesh_renderer_test_pipeline_cache_count(void);
/* Live entries across the whole vertex-input versions table. */
uint32_t nt_mesh_renderer_test_vertex_input_count(void);
bool nt_mesh_renderer_test_initialized(void);
#endif
// #endregion

#endif /* NT_MESH_RENDERER_H */
