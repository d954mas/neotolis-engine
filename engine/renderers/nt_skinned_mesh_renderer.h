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
    uint16_t max_mesh_vertex_inputs; /* as nt_mesh_renderer_desc_t */
} nt_skinned_mesh_renderer_desc_t;

static inline nt_skinned_mesh_renderer_desc_t nt_skinned_mesh_renderer_desc_defaults(void) {
    return (nt_skinned_mesh_renderer_desc_t){
        .max_pipelines = 64,
        .max_mesh_vertex_inputs = 4,
    };
}

/* desc is required, non-NULL and borrowed for the duration of the call. */
nt_result_t nt_skinned_mesh_renderer_init(const nt_skinned_mesh_renderer_desc_t *desc);
void nt_skinned_mesh_renderer_shutdown(void);

/* Retains CPU storage and initialization; drops pipeline and vertex-input
 * caches. Inactive modules are unchanged. */
void nt_skinned_mesh_renderer_restore_gpu(void);

/* nt_mesh_renderer_draw for skinned instances (nt_skinned_mesh_instance_t). The material uses
 * common/skin.glsl and declares u_skin_matrices, which deformation replaces. Call after
 * nt_skeletal_gpu_flush: a texture write precedes the draws that sample it (render/architecture.md,
 * Draw-phase command stream). */
void nt_skinned_mesh_renderer_draw(nt_mesh_t mesh, nt_material_t material, nt_texture_t deformation, uint32_t stream, uint32_t offset, uint32_t count);

/* nt_mesh_renderer_draw_list for skinned items: runs also split on the deformation texture,
 * and every item also needs a skin component with a deformation binding of this frame. Call
 * after nt_skeletal_gpu_flush. */
void nt_skinned_mesh_renderer_draw_list(uint32_t stream, const nt_render_item_t *items, uint32_t count);

// #region test_access
#ifdef NT_TEST_ACCESS
uint32_t nt_skinned_mesh_renderer_test_pipeline_cache_count(void);
uint32_t nt_skinned_mesh_renderer_test_vertex_input_count(void);
bool nt_skinned_mesh_renderer_test_initialized(void);
#endif
// #endregion

#endif /* NT_SKINNED_MESH_RENDERER_H */
