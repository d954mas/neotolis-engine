#include "renderers/nt_skinned_mesh_renderer.h"

#include "comp_storage/nt_comp_storage.h"
#include "core/nt_assert.h"
#include "drawable_comp/nt_drawable_comp.h"
#include "graphics/nt_gfx.h"
#include "hash/nt_hash.h"
#include "log/nt_log.h"
#include "material/nt_material.h"
#include "material_comp/nt_material_comp.h"
#include "mesh_comp/nt_mesh_comp.h"
#include "renderers/nt_mesh_run_internal.h"
#include "renderers/nt_renderer_shared.h"
#include "skin_comp/nt_skin_comp.h"
#include "transform_comp/nt_transform_comp.h"

#include <stdlib.h>
#include <string.h>

static struct {
    nt_renderer_pipeline_entry_t *pipelines;
    uint16_t max_pipelines;
    uint16_t pipeline_count;
    nt_renderer_mesh_vi_cache_t vi_cache;
    uint32_t skin_sampler_hash;
    bool warned_program_not_ready;
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

static nt_pipeline_t find_or_create_pipeline(const nt_material_info_t *material) {
    NT_ASSERT(nt_gfx_program_ready(material->program));
    const nt_pipeline_desc_t desc = nt_renderer_material_pipeline_desc(material, "skinned_mesh_pipeline");
    const nt_gfx_pipeline_key_t key = nt_gfx_pipeline_key(&desc);
    nt_pipeline_t pipeline = nt_renderer_pipeline_cache_find(s_skinned.pipelines, s_skinned.pipeline_count, &key);
    if (pipeline.id != 0) {
        return pipeline;
    }
    return nt_renderer_pipeline_cache_insert(s_skinned.pipelines, &s_skinned.pipeline_count, s_skinned.max_pipelines, &key, &desc, &s_skinned.warned_program_not_ready);
}

static void reset_gpu_caches(void) {
    for (uint16_t i = 0; i < s_skinned.pipeline_count; i++) {
        nt_gfx_destroy_pipeline(s_skinned.pipelines[i].pipeline);
    }
    s_skinned.pipeline_count = 0;
    nt_renderer_mesh_vi_cache_reset(&s_skinned.vi_cache);
    s_skinned.warned_program_not_ready = false;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity) -- NT_ASSERT expansion inflates the metric
nt_result_t nt_skinned_mesh_renderer_init(const nt_skinned_mesh_renderer_desc_t *desc) {
    NT_ASSERT(!s_skinned.initialized);
    NT_ASSERT(desc != NULL);
    NT_ASSERT(desc->max_pipelines > 0);
    NT_ASSERT(desc->max_mesh_layouts > 0);
    memset(&s_skinned, 0, sizeof(s_skinned));
    s_skinned.max_pipelines = desc->max_pipelines;
    s_skinned.skin_sampler_hash = nt_hash32_str("u_skin_matrices").value;

    s_skinned.pipelines = (nt_renderer_pipeline_entry_t *)calloc(desc->max_pipelines, sizeof(nt_renderer_pipeline_entry_t));
    if (s_skinned.pipelines == NULL) {
        NT_LOG_ERROR("failed to allocate pipeline cache");
        return NT_ERR_INIT_FAILED;
    }
    if (nt_renderer_mesh_vi_cache_init(&s_skinned.vi_cache, desc->max_mesh_layouts) != NT_OK) {
        free(s_skinned.pipelines);
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
    reset_gpu_caches();
    nt_renderer_mesh_vi_cache_shutdown(&s_skinned.vi_cache);
    free(s_skinned.pipelines);
    memset(&s_skinned, 0, sizeof(s_skinned));
}

void nt_skinned_mesh_renderer_restore_gpu(void) {
    if (s_skinned.initialized) {
        reset_gpu_caches();
    }
}

/* The material slot the run's deformation texture replaces; NT_MATERIAL_MAX_TEXTURES
 * when undeclared, which leaves the program sampler uncovered and gfx asserts. */
static uint8_t skin_slot(const nt_material_info_t *material) {
    for (uint8_t i = 0; i < material->tex_count; i++) {
        if (material->tex_name_hashes[i] == s_skinned.skin_sampler_hash) {
            return i;
        }
    }
    return NT_MATERIAL_MAX_TEXTURES;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
uint32_t nt_skinned_mesh_renderer_prepare(const nt_render_item_t *items, uint32_t count, nt_mesh_run_t *runs, uint32_t max_runs) {
    NT_ASSERT(s_skinned.initialized);
    if (!s_skinned.initialized || count == 0) {
        return 0;
    }
    NT_ASSERT(items != NULL && runs != NULL);

    // #region resolve runs
    /* A run's offset holds its first item index until the reserve places it. */
    uint32_t run_count = 0;
    uint64_t size = 0;
    nt_material_t previous_material = {0};
    nt_mesh_t previous_mesh = {0};
    nt_pipeline_t pipeline = {0};
    nt_vertex_input_t vertex_input = {0};
    uint8_t supplied_slot = NT_MATERIAL_MAX_TEXTURES;
    for (uint32_t run_start = 0, run_end = 0; run_start < count; run_start = run_end) {
        nt_entity_t entity = {.id = items[run_start].entity};
        const nt_texture_t deformation = nt_skin_comp_handle(entity)->texture;
        const nt_material_t material_handle = *nt_material_comp_handle(entity);
        const nt_mesh_t mesh_handle = *nt_mesh_comp_handle(entity);
        run_end = find_run_end(items, run_start, count, deformation.id);
        const nt_material_info_t *material = nt_material_get_info(material_handle);
        const nt_gfx_mesh_info_t *mesh = nt_gfx_get_mesh_info(mesh_handle);
        NT_ASSERT(material != NULL && mesh != NULL && "skinned render item references a destroyed material or mesh");
        if (!nt_gfx_program_ready(material->program)) {
            nt_renderer_warn_program_not_ready(&s_skinned.warned_program_not_ready, material);
            continue;
        }

        const bool material_changed = material_handle.id != previous_material.id;
        if (material_changed) {
            pipeline = find_or_create_pipeline(material);
            supplied_slot = skin_slot(material);
        }
        if (material_changed || mesh_handle.id != previous_mesh.id) {
            vertex_input = pipeline.id != 0 ? nt_renderer_mesh_vi_cache_find_or_create(&s_skinned.vi_cache, material_handle, mesh_handle, material, mesh, &s_instance_layout, "skinned_mesh_vi")
                                            : NT_VERTEX_INPUT_INVALID;
        }
        if (pipeline.id == 0 || vertex_input.id == 0) {
            /* Retry creation on the next run instead of reusing a failure. */
            previous_material = (nt_material_t){0};
            previous_mesh = (nt_mesh_t){0};
            continue;
        }
        previous_material = material_handle;
        previous_mesh = mesh_handle;

        NT_ASSERT(run_count < max_runs && "skinned_mesh_renderer_prepare: runs exhausted; max_runs >= count always suffices");
        const uint32_t instance_count = run_end - run_start;
        runs[run_count++] = (nt_mesh_run_t){
            .pipeline = pipeline,
            .vertex_input = vertex_input,
            .material = material_handle,
            .supplied_texture = deformation,
            .offset = run_start,
            .instance_count = instance_count,
            .index_count = mesh->index_count,
            .vertex_count = mesh->vertex_count,
            .supplied_slot = supplied_slot,
        };
        size += (uint64_t)instance_count * sizeof(nt_skinned_mesh_instance_t);
    }
    // #endregion
    if (run_count == 0) {
        return 0;
    }

    // #region pack instances
    NT_ASSERT(size <= UINT32_MAX && "skinned_mesh_renderer_prepare: instance data exceeds the frame storage address range");
    uint32_t offset = 0;
    nt_skinned_mesh_instance_t *const base = (nt_skinned_mesh_instance_t *)nt_gfx_frame_alloc(NT_GFX_FRAME_VERTEX, (uint32_t)size, 4, &offset); /* bound by offset: 4 is enough */
    nt_skinned_mesh_instance_t *dst = base;
    const nt_transform_comp_view_t transform_view = nt_transform_comp_view();
    const nt_drawable_comp_view_t drawable_view = nt_drawable_comp_view();
    for (uint32_t r = 0; r < run_count; r++) {
        nt_mesh_run_t *run = &runs[r];
        const uint32_t first = run->offset;
        run->offset = offset + (uint32_t)((uint8_t *)dst - (uint8_t *)base);
        const uint32_t end = first + run->instance_count;
        for (uint32_t i = first; i < end; i++, dst++) {
            const nt_entity_t entity = {.id = items[i].entity};
            const uint16_t entity_index = nt_entity_index(entity);
            const uint16_t transform_index = transform_view.sparse_indices[entity_index];
            const uint16_t drawable_index = drawable_view.sparse_indices[entity_index];
            NT_ASSERT(transform_index != NT_INVALID_COMP_INDEX && "skinned render item: entity has no transform component");
            NT_ASSERT(drawable_index != NT_INVALID_COMP_INDEX && "skinned render item: entity has no drawable component");
            const nt_deformation_binding_t binding = *nt_skin_comp_handle(entity);
            NT_ASSERT(binding.texture.id != 0 && "skinned draw requires a deformation texture");
            nt_renderer_pack_world(&dst->world_rows[0][0], transform_view.world_matrices[transform_index]);
            dst->skin_origins[0] = binding.x0;
            dst->skin_origins[1] = binding.y0;
            dst->skin_origins[2] = binding.x1;
            dst->skin_origins[3] = binding.y1;
            dst->skin_alpha = binding.alpha;
            dst->color = drawable_view.colors_packed[drawable_index];
        }
    }
    // #endregion
    return run_count;
}

void nt_skinned_mesh_renderer_draw(const nt_mesh_run_t *runs, uint32_t run_count) {
    NT_ASSERT(s_skinned.initialized);
    NT_ASSERT(run_count == 0 || runs != NULL);
    nt_mesh_runs_draw(runs, run_count);
}

#ifdef NT_TEST_ACCESS
uint32_t nt_skinned_mesh_renderer_test_pipeline_cache_count(void) { return s_skinned.pipeline_count; }
uint32_t nt_skinned_mesh_renderer_test_vertex_input_count(void) { return nt_renderer_mesh_vi_cache_live_count(&s_skinned.vi_cache); }
bool nt_skinned_mesh_renderer_test_initialized(void) { return s_skinned.initialized; }
#endif
