#ifndef NT_MESH_RENDERER_H
#define NT_MESH_RENDERER_H

#include "core/nt_types.h"
#include "graphics/nt_gfx.h"
#include "render/nt_render_defs.h"

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
void nt_mesh_renderer_draw_list(const nt_render_item_t *items, uint32_t count);

// #region test_access
#ifdef NT_TEST_ACCESS
uint32_t nt_mesh_renderer_test_pipeline_cache_count(void);
uint32_t nt_mesh_renderer_test_draw_call_count(void);
uint32_t nt_mesh_renderer_test_instance_total(void);
#endif
// #endregion

/* Stream type → vertex format mapping (used by pipeline builder, testable) */
nt_vertex_format_t nt_stream_to_vertex_format(uint8_t type, uint8_t count, uint8_t normalized);

#endif /* NT_MESH_RENDERER_H */
