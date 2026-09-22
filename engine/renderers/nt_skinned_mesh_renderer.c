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
#include "renderers/nt_renderer_shared.h"
#include "skin_comp/nt_skin_comp.h"
#include "transform_comp/nt_transform_comp.h"

#include <stdlib.h>
#include <string.h>

#define NT_SKINNED_INSTANCE_STRIDE_NONE 60
#define NT_SKINNED_INSTANCE_STRIDE_RGBA8 64
#define NT_SKINNED_INSTANCE_STRIDE_FLOAT4 76
#define NT_SKINNED_INSTANCE_STRIDE_MAX NT_SKINNED_INSTANCE_STRIDE_FLOAT4

static struct {
    nt_renderer_pipeline_entry_t *pipelines;
    uint16_t max_pipelines;
    uint16_t pipeline_count;
    nt_renderer_mesh_vi_cache_t vi_cache;
    nt_buffer_t instance_buf;
    uint8_t *instance_data;
    uint16_t max_instances;
    uint32_t ring_cursor;
    uint32_t skin_sampler_hash;
    bool warned_program_not_ready;
#ifdef NT_TEST_ACCESS
    uint32_t frame_draw_calls;
    uint32_t frame_instance_total;
#endif
    bool initialized;
} s_skinned;

/* clang-format off */
static const nt_vertex_layout_t s_instance_layouts[3] = {
    [NT_COLOR_MODE_NONE] = {
        .attr_count = 5,
        .stride = NT_SKINNED_INSTANCE_STRIDE_NONE,
        .attrs = {
            {.location = 10, .type = NT_VERTEX_FLOAT, .count = 4, .offset = 0},
            {.location = 11, .type = NT_VERTEX_FLOAT, .count = 4, .offset = 16},
            {.location = 12, .type = NT_VERTEX_FLOAT, .count = 4, .offset = 32},
            {.location = 14, .type = NT_VERTEX_UINT16, .count = 4, .offset = 48},
            {.location = 15, .type = NT_VERTEX_FLOAT, .count = 1, .offset = 56},
        },
    },
    [NT_COLOR_MODE_RGBA8] = {
        .attr_count = 6,
        .stride = NT_SKINNED_INSTANCE_STRIDE_RGBA8,
        .attrs = {
            {.location = 10, .type = NT_VERTEX_FLOAT, .count = 4, .offset = 0},
            {.location = 11, .type = NT_VERTEX_FLOAT, .count = 4, .offset = 16},
            {.location = 12, .type = NT_VERTEX_FLOAT, .count = 4, .offset = 32},
            {.location = 14, .type = NT_VERTEX_UINT16, .count = 4, .offset = 48},
            {.location = 15, .type = NT_VERTEX_FLOAT, .count = 1, .offset = 56},
            {.location = 13, .type = NT_VERTEX_UINT8, .count = 4, .normalized = true, .offset = 60},
        },
    },
    [NT_COLOR_MODE_FLOAT4] = {
        .attr_count = 6,
        .stride = NT_SKINNED_INSTANCE_STRIDE_FLOAT4,
        .attrs = {
            {.location = 10, .type = NT_VERTEX_FLOAT, .count = 4, .offset = 0},
            {.location = 11, .type = NT_VERTEX_FLOAT, .count = 4, .offset = 16},
            {.location = 12, .type = NT_VERTEX_FLOAT, .count = 4, .offset = 32},
            {.location = 14, .type = NT_VERTEX_UINT16, .count = 4, .offset = 48},
            {.location = 15, .type = NT_VERTEX_FLOAT, .count = 1, .offset = 56},
            {.location = 13, .type = NT_VERTEX_FLOAT, .count = 4, .offset = 60},
        },
    },
};
/* clang-format on */

static void pack_skin_binding(uint8_t *dst, const nt_deformation_binding_t *binding) {
    uint16_t origins[4] = {binding->x0, binding->y0, binding->x1, binding->y1};
    memcpy(dst, origins, sizeof(origins));
    memcpy(dst + sizeof(origins), &binding->alpha, sizeof(binding->alpha));
}

static uint16_t instance_stride(nt_color_mode_t color_mode) { return s_instance_layouts[color_mode].stride; }

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

static nt_renderer_material_view_t supplied_material_view(const nt_material_info_t *material, nt_texture_t deformation, nt_sampler_t samplers[NT_MATERIAL_MAX_TEXTURES]) {
    nt_renderer_material_view_t view = {
        .tex_count = material->tex_count,
        .tex_name_hashes = material->tex_name_hashes,
        .tex_samplers = samplers,
        .param_count = material->param_count,
        .param_name_hashes = material->param_name_hashes,
        .params = material->params,
    };
    for (uint8_t i = 0; i < material->tex_count; i++) {
        if (material->tex_name_hashes[i] == s_skinned.skin_sampler_hash) {
            view.resolved_tex[i] = deformation.id;
            samplers[i] = NT_SAMPLER_DEFAULT;
        } else {
            view.resolved_tex[i] = nt_resource_get(material->tex_resources[i]);
            samplers[i] = material->tex_samplers[i];
        }
    }
    return view;
}

static nt_result_t create_gpu_resources(void) {
    s_skinned.instance_buf = nt_gfx_make_buffer(&(nt_buffer_desc_t){
        .type = NT_BUFFER_VERTEX,
        .usage = NT_USAGE_STREAM,
        .size = (uint32_t)s_skinned.max_instances * NT_SKINNED_INSTANCE_STRIDE_MAX,
        .label = "skinned_mesh_renderer_instance",
    });
    if (s_skinned.instance_buf.id == 0) {
        return NT_ERR_INIT_FAILED;
    }
    return NT_OK;
}

static void destroy_gpu_resources(void) {
    nt_gfx_destroy_buffer(s_skinned.instance_buf);
    s_skinned.instance_buf = (nt_buffer_t){0};
    for (uint16_t i = 0; i < s_skinned.pipeline_count; i++) {
        nt_gfx_destroy_pipeline(s_skinned.pipelines[i].pipeline);
    }
    s_skinned.pipeline_count = 0;
    nt_renderer_mesh_vi_cache_reset(&s_skinned.vi_cache);
    s_skinned.ring_cursor = 0;
#ifdef NT_TEST_ACCESS
    s_skinned.frame_draw_calls = 0;
    s_skinned.frame_instance_total = 0;
#endif
    s_skinned.warned_program_not_ready = false;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity) -- NT_ASSERT expansion inflates the metric
nt_result_t nt_skinned_mesh_renderer_init(const nt_skinned_mesh_renderer_desc_t *desc) {
    NT_ASSERT(!s_skinned.initialized);
    NT_ASSERT(desc != NULL);
    NT_ASSERT(desc->max_instances > 0);
    NT_ASSERT(desc->max_pipelines > 0);
    NT_ASSERT(desc->max_mesh_layouts > 0);
    memset(&s_skinned, 0, sizeof(s_skinned));
    s_skinned.max_instances = desc->max_instances;
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
    s_skinned.instance_data = (uint8_t *)calloc(desc->max_instances, NT_SKINNED_INSTANCE_STRIDE_MAX);
    if (s_skinned.instance_data == NULL) {
        nt_renderer_mesh_vi_cache_shutdown(&s_skinned.vi_cache);
        free(s_skinned.pipelines);
        NT_LOG_ERROR("failed to allocate instance data");
        memset(&s_skinned, 0, sizeof(s_skinned));
        return NT_ERR_INIT_FAILED;
    }
    if (create_gpu_resources() != NT_OK) {
        free(s_skinned.instance_data);
        nt_renderer_mesh_vi_cache_shutdown(&s_skinned.vi_cache);
        free(s_skinned.pipelines);
        NT_LOG_ERROR("failed to create instance buffer");
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
    destroy_gpu_resources();
    nt_renderer_mesh_vi_cache_shutdown(&s_skinned.vi_cache);
    free(s_skinned.pipelines);
    free(s_skinned.instance_data);
    memset(&s_skinned, 0, sizeof(s_skinned));
}

nt_result_t nt_skinned_mesh_renderer_restore_gpu(void) {
    if (!s_skinned.initialized) {
        return NT_OK;
    }
    destroy_gpu_resources();
    return create_gpu_resources();
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void nt_skinned_mesh_renderer_draw_list(const nt_render_item_t *items, uint32_t count) {
    NT_ASSERT(s_skinned.initialized);
    if (!s_skinned.initialized || count == 0) {
        return;
    }
    NT_ASSERT(items != NULL);
    NT_ASSERT(s_skinned.instance_buf.id != 0 && "retry failed GPU restore before drawing");
#ifdef NT_TEST_ACCESS
    s_skinned.frame_draw_calls = 0;
    s_skinned.frame_instance_total = 0;
#endif
    uint32_t chunk_start = 0;
    nt_material_t previous_material = {0};
    nt_mesh_t previous_mesh = {0};
    nt_texture_t previous_deformation = {0};
    nt_renderer_bound_t bound = {0};
    nt_pipeline_t pipeline = {0};
    nt_vertex_input_t vertex_input = {0};
    const nt_drawable_comp_view_t drawable_view = nt_drawable_comp_view();

    while (chunk_start < count) {
        uint32_t chunk_count = count - chunk_start;
        if (chunk_count > s_skinned.max_instances) {
            chunk_count = s_skinned.max_instances;
        }
        const uint32_t chunk_end = chunk_start + chunk_count;
        uint32_t packed_size = 0;
        uint32_t scan = chunk_start;
        while (scan < chunk_end) {
            nt_entity_t leader = {.id = items[scan].entity};
            uint32_t run_end = scan + 1;
            while (run_end < chunk_end && items[run_end].batch_key == items[scan].batch_key) {
                run_end++;
            }
            nt_material_t material_handle = *nt_material_comp_handle(leader);
            const nt_material_info_t *material = nt_material_get_info(material_handle);
            const nt_color_mode_t color_mode = material != NULL ? material->color_mode : NT_COLOR_MODE_NONE;
            NT_ASSERT(color_mode <= NT_COLOR_MODE_FLOAT4);
            const uint16_t stride = instance_stride(color_mode);
            for (uint32_t i = scan; i < run_end; i++) {
                nt_entity_t entity = {.id = items[i].entity};
                const nt_deformation_binding_t binding = *nt_skin_comp_handle(entity);
                NT_ASSERT(binding.texture.id != 0 && "skinned draw requires a deformation texture");
                uint8_t *dst = s_skinned.instance_data + packed_size;
                nt_renderer_pack_world((float *)dst, nt_transform_comp_world_matrix(entity));
                pack_skin_binding(dst + 48, &binding);
                if (color_mode == NT_COLOR_MODE_RGBA8) {
                    const uint16_t drawable_index = drawable_view.sparse_indices[nt_entity_index(entity)];
                    NT_ASSERT(drawable_index != NT_INVALID_COMP_INDEX && "skinned render item: entity has no drawable component");
                    memcpy(dst + 60, &drawable_view.colors_packed[drawable_index], sizeof(uint32_t));
                } else if (color_mode == NT_COLOR_MODE_FLOAT4) {
                    memcpy(dst + 60, nt_drawable_comp_color(entity), 16);
                }
                packed_size += stride;
            }
            scan = run_end;
        }

        const uint32_t capacity = (uint32_t)s_skinned.max_instances * NT_SKINNED_INSTANCE_STRIDE_MAX;
        if (s_skinned.ring_cursor + packed_size > capacity) {
            s_skinned.ring_cursor = 0;
        }
        const uint32_t ring_base = s_skinned.ring_cursor;
        s_skinned.ring_cursor += packed_size;
        nt_gfx_update_buffer(s_skinned.instance_buf, ring_base, s_skinned.instance_data, packed_size);

        uint32_t run_start = chunk_start;
        uint32_t draw_offset = ring_base;
        while (run_start < chunk_end) {
            nt_entity_t entity = {.id = items[run_start].entity};
            const nt_deformation_binding_t deformation = *nt_skin_comp_handle(entity);
            nt_material_t material_handle = *nt_material_comp_handle(entity);
            nt_mesh_t mesh_handle = *nt_mesh_comp_handle(entity);
            const uint32_t run_end = find_run_end(items, run_start, chunk_end, deformation.texture.id);
            const uint32_t instance_count = run_end - run_start;
            const nt_material_info_t *material = nt_material_get_info(material_handle);
            const nt_gfx_mesh_info_t *mesh = nt_gfx_get_mesh_info(mesh_handle);
            NT_ASSERT(material != NULL && mesh != NULL && "skinned draw references a destroyed material or mesh");
            const uint16_t stride = instance_stride(material->color_mode);
            if (!nt_gfx_program_ready(material->program)) {
                nt_renderer_warn_program_not_ready(&s_skinned.warned_program_not_ready, material);
                draw_offset += instance_count * stride;
                run_start = run_end;
                continue;
            }

            const bool material_changed = material_handle.id != previous_material.id;
            const bool mesh_changed = mesh_handle.id != previous_mesh.id;
            const bool deformation_changed = deformation.texture.id != previous_deformation.id;
            if (material_changed) {
                pipeline = find_or_create_pipeline(material);
            }
            if (material_changed || mesh_changed) {
                vertex_input = pipeline.id != 0 ? nt_renderer_mesh_vi_cache_find_or_create(&s_skinned.vi_cache, material_handle, mesh_handle, material, mesh, s_instance_layouts, "skinned_mesh_vi")
                                                : NT_VERTEX_INPUT_INVALID;
            }
            if (pipeline.id == 0 || vertex_input.id == 0) {
                draw_offset += instance_count * stride;
                run_start = run_end;
                previous_material = (nt_material_t){0};
                previous_mesh = (nt_mesh_t){0};
                previous_deformation = (nt_texture_t){0};
                continue;
            }

            nt_renderer_bind_pipeline(&bound, pipeline);
            if (material_changed) {
                nt_sampler_t samplers[NT_MATERIAL_MAX_TEXTURES];
                const nt_renderer_material_view_t view = supplied_material_view(material, deformation.texture, samplers);
                nt_renderer_apply_material_uniforms(&bound, material_handle.id, &view);
                nt_renderer_apply_texture_slots(&view);
            } else if (deformation_changed) {
                nt_sampler_t samplers[NT_MATERIAL_MAX_TEXTURES];
                const nt_renderer_material_view_t view = supplied_material_view(material, deformation.texture, samplers);
                nt_renderer_apply_texture_slots(&view);
            }
            nt_renderer_bind_vertex_input(&bound, vertex_input);
            previous_material = material_handle;
            previous_mesh = mesh_handle;
            previous_deformation = deformation.texture;
            if (material->color_mode == NT_COLOR_MODE_NONE) {
                /* Native GL leaves a generic value unspecified after drawing with an enabled array there. */
                nt_gfx_set_vertex_attrib_default(13, 1.0F, 1.0F, 1.0F, 1.0F);
            }
            nt_gfx_bind_instance_buffer(s_skinned.instance_buf, draw_offset);
            if (mesh->index_count > 0) {
                nt_gfx_draw_indexed_instanced(0, mesh->index_count, mesh->vertex_count, instance_count);
            } else {
                nt_gfx_draw_instanced(0, mesh->vertex_count, instance_count);
            }
#ifdef NT_TEST_ACCESS
            s_skinned.frame_draw_calls++;
            s_skinned.frame_instance_total += instance_count;
#endif
            draw_offset += instance_count * stride;
            run_start = run_end;
        }
        chunk_start = chunk_end;
    }
}

#ifdef NT_TEST_ACCESS
uint32_t nt_skinned_mesh_renderer_test_pipeline_cache_count(void) { return s_skinned.pipeline_count; }
uint32_t nt_skinned_mesh_renderer_test_vertex_input_count(void) { return nt_renderer_mesh_vi_cache_live_count(&s_skinned.vi_cache); }
uint32_t nt_skinned_mesh_renderer_test_draw_call_count(void) { return s_skinned.frame_draw_calls; }
uint32_t nt_skinned_mesh_renderer_test_instance_total(void) { return s_skinned.frame_instance_total; }
uint32_t nt_skinned_mesh_renderer_test_ring_cursor(void) { return s_skinned.ring_cursor; }
bool nt_skinned_mesh_renderer_test_initialized(void) { return s_skinned.initialized; }
#endif
