#include "renderers/nt_mesh_renderer.h"

#include "comp_storage/nt_comp_storage.h"
#include "core/nt_assert.h"
#include "drawable_comp/nt_drawable_comp.h"
#include "graphics/nt_gfx.h"
#include "material/nt_material.h"
#include "material_comp/nt_material_comp.h"
#include "mesh_comp/nt_mesh_comp.h"
#include "renderers/nt_renderer_shared.h"
#include "transform_comp/nt_transform_comp.h"

#include <string.h>

static struct {
    nt_renderer_mesh_caches_t caches;
    bool initialized;
} s_mesh_renderer;

/* clang-format off */
static const nt_vertex_layout_t s_instance_layout = {
    .attr_count = 4,
    .stride = sizeof(nt_mesh_instance_t),
    .attrs = {
        {.location = 4, .type = NT_VERTEX_FLOAT, .count = 4, .offset = 0},
        {.location = 5, .type = NT_VERTEX_FLOAT, .count = 4, .offset = 16},
        {.location = 6, .type = NT_VERTEX_FLOAT, .count = 4, .offset = 32},
        {.location = 7, .type = NT_VERTEX_UINT8, .count = 4, .normalized = true, .offset = offsetof(nt_mesh_instance_t, color)},
    },
};
/* clang-format on */

/* ---- Lifecycle ---- */

nt_result_t nt_mesh_renderer_init(const nt_mesh_renderer_desc_t *desc) {
    NT_ASSERT(!s_mesh_renderer.initialized);
    NT_ASSERT(desc);
    memset(&s_mesh_renderer, 0, sizeof(s_mesh_renderer));
    if (nt_renderer_mesh_caches_init(&s_mesh_renderer.caches, desc->max_pipelines, desc->max_mesh_layouts, &s_instance_layout, "mesh_pipeline", "mesh_vi") != NT_OK) {
        return NT_ERR_INIT_FAILED;
    }
    s_mesh_renderer.initialized = true;
    return NT_OK;
}

void nt_mesh_renderer_shutdown(void) {
    if (!s_mesh_renderer.initialized) {
        return;
    }
    nt_renderer_mesh_caches_shutdown(&s_mesh_renderer.caches);
    memset(&s_mesh_renderer, 0, sizeof(s_mesh_renderer));
}

void nt_mesh_renderer_restore_gpu(void) {
    if (s_mesh_renderer.initialized) {
        nt_renderer_mesh_caches_reset(&s_mesh_renderer.caches);
    }
}

/* ---- Draw ---- */

void nt_mesh_renderer_draw(nt_mesh_t mesh, nt_material_t material, uint32_t offset, uint32_t count) {
    NT_ASSERT(s_mesh_renderer.initialized);
    NT_ASSERT(count > 0);
    const nt_material_info_t *mat_info = nt_material_get_info(material);
    const nt_gfx_mesh_info_t *mesh_info = nt_gfx_get_mesh_info(mesh);
    NT_ASSERT(mat_info != NULL && mesh_info != NULL && "mesh draw references a destroyed material or mesh");
    nt_renderer_mesh_draw_t draw = {0};
    if (nt_renderer_mesh_resolve(&s_mesh_renderer.caches, &draw, material, mat_info, mesh, mesh_info)) {
        nt_renderer_mesh_record(&draw, mat_info, mesh_info, NT_MATERIAL_MAX_TEXTURES, (nt_texture_t){0}, offset, count);
    }
}

static uint32_t find_run_end(const nt_render_item_t *items, uint32_t run_start, uint32_t count) {
    uint32_t run_end = run_start + 1;
    while (run_end < count && items[run_end].batch_key == items[run_start].batch_key) {
        run_end++;
    }
    return run_end;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void nt_mesh_renderer_draw_list(const nt_render_item_t *items, uint32_t count) {
    NT_ASSERT(s_mesh_renderer.initialized);
    NT_ASSERT(count == 0 || items != NULL);
    /* Inline sparse reads, as the sprite emit does: no per-instance accessor call or liveness assert. */
    const nt_transform_comp_view_t transform_view = nt_transform_comp_view();
    const nt_drawable_comp_view_t drawable_view = nt_drawable_comp_view();
    nt_renderer_mesh_draw_t draw = {0};
    for (uint32_t run_start = 0, run_end = 0; run_start < count; run_start = run_end) {
        run_end = find_run_end(items, run_start, count);
        const nt_entity_t leader = {.id = items[run_start].entity};
        const nt_material_t material = *nt_material_comp_handle(leader);
        const nt_mesh_t mesh = *nt_mesh_comp_handle(leader);
        const nt_material_info_t *mat_info = nt_material_get_info(material);
        const nt_gfx_mesh_info_t *mesh_info = nt_gfx_get_mesh_info(mesh);
        NT_ASSERT(mat_info != NULL && mesh_info != NULL && "mesh render item references a destroyed material or mesh");
        if (!nt_renderer_mesh_resolve(&s_mesh_renderer.caches, &draw, material, mat_info, mesh, mesh_info)) {
            continue;
        }

        const uint32_t instance_count = run_end - run_start;
        NT_ASSERT(instance_count <= UINT32_MAX / sizeof(nt_mesh_instance_t) && "mesh draw_list: run exceeds the frame storage address range");
        uint32_t offset = 0;
        nt_mesh_instance_t *dst = (nt_mesh_instance_t *)nt_gfx_frame_alloc(NT_GFX_FRAME_VERTEX, instance_count * (uint32_t)sizeof(nt_mesh_instance_t), 4, &offset);
        for (uint32_t i = run_start; i < run_end; i++, dst++) {
            const uint16_t entity_index = nt_entity_index((nt_entity_t){.id = items[i].entity});
            const uint16_t transform_index = transform_view.sparse_indices[entity_index];
            const uint16_t drawable_index = drawable_view.sparse_indices[entity_index];
            NT_ASSERT(transform_index != NT_INVALID_COMP_INDEX && "mesh render item: entity has no transform component");
            NT_ASSERT(drawable_index != NT_INVALID_COMP_INDEX && "mesh render item: entity has no drawable component");
            nt_mesh_instance_world_rows(dst->world_rows, transform_view.world_matrices[transform_index]);
            dst->color = drawable_view.colors_packed[drawable_index];
        }
        nt_renderer_mesh_record(&draw, mat_info, mesh_info, NT_MATERIAL_MAX_TEXTURES, (nt_texture_t){0}, offset, instance_count);
    }
}

#ifdef NT_TEST_ACCESS
/* ---- Test accessors ---- */

uint32_t nt_mesh_renderer_test_pipeline_cache_count(void) { return s_mesh_renderer.caches.pipeline_count; }

uint32_t nt_mesh_renderer_test_vertex_input_count(void) { return nt_renderer_mesh_vi_cache_live_count(&s_mesh_renderer.caches.vi_cache); }

bool nt_mesh_renderer_test_initialized(void) { return s_mesh_renderer.initialized; }
#endif
