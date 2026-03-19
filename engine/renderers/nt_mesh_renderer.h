#ifndef NT_MESH_RENDERER_H
#define NT_MESH_RENDERER_H

#include "core/nt_types.h"
#include "render/nt_render_defs.h"

typedef struct {
    uint16_t max_instances; /* max per single instanced draw call, default: 4096 */
} nt_mesh_renderer_desc_t;

static inline nt_mesh_renderer_desc_t nt_mesh_renderer_desc_defaults(void) { return (nt_mesh_renderer_desc_t){.max_instances = 4096}; }

nt_result_t nt_mesh_renderer_init(const nt_mesh_renderer_desc_t *desc);
void nt_mesh_renderer_shutdown(void);
void nt_mesh_renderer_restore_gpu(void);

void nt_mesh_renderer_draw_list(const nt_render_item_t *items, uint32_t count);

#ifdef NT_MESH_RENDERER_TEST_ACCESS
uint32_t nt_mesh_renderer_test_pipeline_cache_count(void);
uint32_t nt_mesh_renderer_test_draw_call_count(void);
uint32_t nt_mesh_renderer_test_instance_total(void);
#endif

#endif /* NT_MESH_RENDERER_H */
