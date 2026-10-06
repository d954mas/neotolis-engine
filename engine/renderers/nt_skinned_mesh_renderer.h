#ifndef NT_SKINNED_MESH_RENDERER_H
#define NT_SKINNED_MESH_RENDERER_H

#include "core/nt_types.h"
#include "render/nt_render_defs.h"
#include "renderers/nt_mesh_renderer.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* One GPU instance (locations 10-12 world rows, 14 origins, 15 alpha, 13 color);
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

typedef struct {
    uint16_t max_pipelines;
    uint16_t max_mesh_layouts;
} nt_skinned_mesh_renderer_desc_t;

static inline nt_skinned_mesh_renderer_desc_t nt_skinned_mesh_renderer_desc_defaults(void) {
    return (nt_skinned_mesh_renderer_desc_t){
        .max_pipelines = 64,
        .max_mesh_layouts = 4,
    };
}

/* desc is required, non-NULL and borrowed for the duration of the call. */
nt_result_t nt_skinned_mesh_renderer_init(const nt_skinned_mesh_renderer_desc_t *desc);
void nt_skinned_mesh_renderer_shutdown(void);

/* Retains CPU storage and initialization; drops pipeline and vertex-input
 * caches. Inactive modules are unchanged. */
void nt_skinned_mesh_renderer_restore_gpu(void);

/* Caller controls visibility/sorting. items may be NULL only when count is
 * zero; it is borrowed for the call, and bindings may change after it returns. */
/* common/skin.glsl requires joints/weights mapped by material attr_map and
 * positive uniform joint/world scale. Declare u_skin_matrices in the material;
 * the run supplies the entity's deformation texture and its default sampler. */
/* Splits items into runs of equal batch_key and deformation texture, resolves
 * pipeline and vertex input per run (creating them on a cache miss), packs
 * world, deformation binding and drawable color of every instance into one vertex frame
 * storage allocation and writes the runs; returns their count. Runs whose
 * program is not ready or whose pipeline/vertex input failed are skipped.
 * Writes no buffer. Call after the items' nt_skeletal_gpu_reserve, at any
 * point of the frame before the draws.
 * max_runs >= count always suffices; fewer asserts when exceeded. */
uint32_t nt_skinned_mesh_renderer_prepare(const nt_render_item_t *items, uint32_t count, nt_mesh_run_t *runs, uint32_t max_runs);
/* Executes runs of this gfx frame in order in the current pass after
 * nt_skeletal_gpu_flush, any number of times. runs may be NULL only when
 * run_count is 0. */
void nt_skinned_mesh_renderer_draw(const nt_mesh_run_t *runs, uint32_t run_count);

// #region test_access
#ifdef NT_TEST_ACCESS
uint32_t nt_skinned_mesh_renderer_test_pipeline_cache_count(void);
uint32_t nt_skinned_mesh_renderer_test_vertex_input_count(void);
bool nt_skinned_mesh_renderer_test_initialized(void);
#endif
// #endregion

#endif /* NT_SKINNED_MESH_RENDERER_H */
