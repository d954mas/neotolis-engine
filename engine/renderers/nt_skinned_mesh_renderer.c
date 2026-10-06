#include "renderers/nt_skinned_mesh_renderer.h"

#include "comp_storage/nt_comp_storage.h"
#include "core/nt_assert.h"
#include "drawable_comp/nt_drawable_comp.h"
#include "graphics/nt_gfx.h"
#include "hash/nt_hash.h"
#include "material/nt_material.h"
#include "material_comp/nt_material_comp.h"
#include "mesh_comp/nt_mesh_comp.h"
#include "renderers/nt_renderer_shared.h"
#include "skin_comp/nt_skin_comp.h"
#include "transform_comp/nt_transform_comp.h"

#include <string.h>

static struct {
    nt_renderer_mesh_caches_t caches;
    uint32_t skin_sampler_hash;
    bool initialized;
} s_skinned;

/* clang-format off */
static const nt_vertex_layout_t s_instance_layout = {
    .attr_count = 6,
    .stride = sizeof(nt_skinned_mesh_instance_t),
    .attrs = {
        {.location = 10, .type = NT_VERTEX_FLOAT, .count = 4, .offset = 0},
        {.location = 11, .type = NT_VERTEX_FLOAT, .count = 4, .offset = 16},
        {.location = 12, .type = NT_VERTEX_FLOAT, .count = 4, .offset = 32},
        {.location = 14, .type = NT_VERTEX_UINT16, .count = 4, .offset = offsetof(nt_skinned_mesh_instance_t, skin_origins)},
        {.location = 15, .type = NT_VERTEX_FLOAT, .count = 1, .offset = offsetof(nt_skinned_mesh_instance_t, skin_alpha)},
        {.location = 13, .type = NT_VERTEX_UINT8, .count = 4, .normalized = true, .offset = offsetof(nt_skinned_mesh_instance_t, color)},
    },
};
/* clang-format on */

static bool run_compatible(const nt_render_item_t *items, uint32_t leader, uint32_t candidate, uint32_t texture_id) {
    if (items[candidate].batch_key != items[leader].batch_key) {
        return false;
    }
    nt_entity_t entity = {.id = items[candidate].entity};
    return nt_skin_comp_handle(entity)->texture.id == texture_id;
}

static uint32_t find_run_end(const nt_render_item_t *items, uint32_t leader, uint32_t end, uint32_t texture_id) {
    uint32_t run_end = leader + 1;
    while (run_end < end && run_compatible(items, leader, run_end, texture_id)) {
        run_end++;
    }
    return run_end;
}

nt_result_t nt_skinned_mesh_renderer_init(const nt_skinned_mesh_renderer_desc_t *desc) {
    NT_ASSERT(!s_skinned.initialized);
    NT_ASSERT(desc != NULL);
    memset(&s_skinned, 0, sizeof(s_skinned));
    s_skinned.skin_sampler_hash = nt_hash32_str("u_skin_matrices").value;
    if (nt_renderer_mesh_caches_init(&s_skinned.caches, desc->max_pipelines, desc->max_mesh_layouts, &s_instance_layout, "skinned_mesh_renderer") != NT_OK) {
        memset(&s_skinned, 0, sizeof(s_skinned));
        return NT_ERR_INIT_FAILED;
    }
    s_skinned.initialized = true;
    return NT_OK;
}

void nt_skinned_mesh_renderer_shutdown(void) {
    if (!s_skinned.initialized) {
        return;
    }
    nt_renderer_mesh_caches_shutdown(&s_skinned.caches);
    memset(&s_skinned, 0, sizeof(s_skinned));
}

void nt_skinned_mesh_renderer_restore_gpu(void) {
    if (s_skinned.initialized) {
        nt_renderer_mesh_caches_reset(&s_skinned.caches);
    }
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity) -- NT_ASSERT expansion inflates the metric
void nt_skinned_mesh_renderer_draw(nt_mesh_t mesh, nt_material_t material, nt_texture_t deformation, uint32_t offset, uint32_t count) {
    NT_ASSERT(s_skinned.initialized);
    NT_ASSERT(count > 0);
    NT_ASSERT(deformation.id != 0 && "skinned draw requires a deformation texture");
    const nt_material_info_t *mat_info = nt_material_get_info(material);
    const nt_gfx_mesh_info_t *mesh_info = nt_gfx_get_mesh_info(mesh);
    NT_ASSERT(mat_info != NULL && mesh_info != NULL && "skinned draw references a destroyed material or mesh");
    nt_renderer_mesh_draw_t draw = {0};
    if (nt_renderer_mesh_resolve(&s_skinned.caches, &draw, material, mat_info, mesh, mesh_info)) {
        nt_renderer_mesh_record(&draw, mat_info, mesh_info, s_skinned.skin_sampler_hash, deformation, offset, count);
    }
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void nt_skinned_mesh_renderer_draw_list(const nt_render_item_t *items, uint32_t count) {
    NT_ASSERT(s_skinned.initialized);
    NT_ASSERT(count == 0 || items != NULL);
    /* Transform and drawable by inline sparse reads; the skin binding goes through its asserting accessor. */
    const nt_transform_comp_view_t transform_view = nt_transform_comp_view();
    const nt_drawable_comp_view_t drawable_view = nt_drawable_comp_view();
    nt_renderer_mesh_draw_t draw = {0};
    for (uint32_t run_start = 0, run_end = 0; run_start < count; run_start = run_end) {
        const nt_entity_t leader = {.id = items[run_start].entity};
        const nt_texture_t deformation = nt_skin_comp_handle(leader)->texture;
        const nt_material_t material = *nt_material_comp_handle(leader);
        const nt_mesh_t mesh = *nt_mesh_comp_handle(leader);
        NT_ASSERT(deformation.id != 0 && "skinned draw requires a deformation texture");
        run_end = find_run_end(items, run_start, count, deformation.id); /* the run shares the leader's texture */
        const nt_material_info_t *mat_info = nt_material_get_info(material);
        const nt_gfx_mesh_info_t *mesh_info = nt_gfx_get_mesh_info(mesh);
        NT_ASSERT(mat_info != NULL && mesh_info != NULL && "skinned render item references a destroyed material or mesh");
        if (!nt_renderer_mesh_resolve(&s_skinned.caches, &draw, material, mat_info, mesh, mesh_info)) {
            continue;
        }

        const uint32_t instance_count = run_end - run_start;
        NT_ASSERT(instance_count <= UINT32_MAX / sizeof(nt_skinned_mesh_instance_t) && "skinned draw_list: run exceeds the frame storage address range");
        uint32_t offset = 0;
        nt_skinned_mesh_instance_t *dst = (nt_skinned_mesh_instance_t *)nt_gfx_frame_alloc(NT_GFX_FRAME_VERTEX, instance_count * (uint32_t)sizeof(nt_skinned_mesh_instance_t), 4, &offset);
        for (uint32_t i = run_start; i < run_end; i++, dst++) {
            const nt_entity_t entity = {.id = items[i].entity};
            const uint16_t entity_index = nt_entity_index(entity);
            const uint16_t transform_index = transform_view.sparse_indices[entity_index];
            const uint16_t drawable_index = drawable_view.sparse_indices[entity_index];
            NT_ASSERT(transform_index != NT_INVALID_COMP_INDEX && "skinned render item: entity has no transform component");
            NT_ASSERT(drawable_index != NT_INVALID_COMP_INDEX && "skinned render item: entity has no drawable component");
            const nt_deformation_binding_t binding = *nt_skin_comp_handle(entity);
            nt_mesh_instance_world_rows(dst->world_rows, transform_view.world_matrices[transform_index]);
            dst->skin_origins[0] = binding.x0;
            dst->skin_origins[1] = binding.y0;
            dst->skin_origins[2] = binding.x1;
            dst->skin_origins[3] = binding.y1;
            dst->skin_alpha = binding.alpha;
            dst->color = drawable_view.colors_packed[drawable_index];
        }
        nt_renderer_mesh_record(&draw, mat_info, mesh_info, s_skinned.skin_sampler_hash, deformation, offset, instance_count);
    }
}

#ifdef NT_TEST_ACCESS
uint32_t nt_skinned_mesh_renderer_test_pipeline_cache_count(void) { return s_skinned.caches.pipeline_count; }
uint32_t nt_skinned_mesh_renderer_test_vertex_input_count(void) { return nt_renderer_mesh_vi_cache_live_count(&s_skinned.caches.vi_cache); }
bool nt_skinned_mesh_renderer_test_initialized(void) { return s_skinned.initialized; }
#endif
