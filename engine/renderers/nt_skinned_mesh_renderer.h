#ifndef NT_SKINNED_MESH_RENDERER_H
#define NT_SKINNED_MESH_RENDERER_H

#include "core/nt_types.h"
#include "render/nt_render_defs.h"
#include "renderers/nt_mesh_renderer.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint16_t max_instances;
    uint16_t max_pipelines;
    uint16_t max_mesh_layouts;
} nt_skinned_mesh_renderer_desc_t;

static inline nt_skinned_mesh_renderer_desc_t nt_skinned_mesh_renderer_desc_defaults(void) {
    return (nt_skinned_mesh_renderer_desc_t){
        .max_instances = 4096,
        .max_pipelines = 64,
        .max_mesh_layouts = 4,
    };
}

nt_result_t nt_skinned_mesh_renderer_init(const nt_skinned_mesh_renderer_desc_t *desc);
void nt_skinned_mesh_renderer_shutdown(void);

/* Retains CPU storage and initialization; drops GPU caches and recreates the
 * instance buffer. Failure must be retried before drawing. */
nt_result_t nt_skinned_mesh_renderer_restore_gpu(void);

/* The caller owns visibility, sorting and every referenced lifetime. Materials
 * declare u_skin_matrices; this renderer supplies that declared texture and its
 * default sampler from skin_comp. The fast normal path requires positive
 * uniform joint scale. Items may be NULL only when count is zero. */
void nt_skinned_mesh_renderer_draw_list(const nt_render_item_t *items, uint32_t count);

// #region test_access
#ifdef NT_TEST_ACCESS
uint32_t nt_skinned_mesh_renderer_test_pipeline_cache_count(void);
uint32_t nt_skinned_mesh_renderer_test_vertex_input_count(void);
uint32_t nt_skinned_mesh_renderer_test_draw_call_count(void);
uint32_t nt_skinned_mesh_renderer_test_instance_total(void);
uint32_t nt_skinned_mesh_renderer_test_ring_cursor(void);
bool nt_skinned_mesh_renderer_test_initialized(void);
#endif
// #endregion

#endif /* NT_SKINNED_MESH_RENDERER_H */
