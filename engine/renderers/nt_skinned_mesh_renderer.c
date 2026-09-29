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

/* Texel mirror of the skinned payload read by nt_skinned_instance() in
 * common/skin.glsl. Frame origins are texel coordinates stored as floats,
 * exact below 2^24; skin.x is the blend alpha between the two frames. */
typedef struct {
    float world_rows[12];
    float frames[4];
    float skin[4];
    float color[4];
} nt_skinned_instance_t;
#define NT_SKINNED_INSTANCE_TEXELS 6U
_Static_assert(sizeof(nt_skinned_instance_t) == (size_t)NT_SKINNED_INSTANCE_TEXELS * 16U, "skinned instance payload is six texels");

static struct {
    nt_renderer_pipeline_entry_t *pipelines;
    uint16_t max_pipelines;
    uint16_t pipeline_count;
    nt_renderer_mesh_vi_cache_t vi_cache;
    nt_skinned_instance_t *staging; /* [slice_instances]: one transient texture */
    uint32_t slice_instances;
    nt_hash32_t instance_base;
    nt_hash32_t instances_sampler;
    uint32_t skin_sampler_hash;
    bool warned_program_not_ready;
#ifdef NT_TEST_ACCESS
    uint32_t frame_draw_calls;
    uint32_t frame_instance_total;
#endif
    bool initialized;
} s_skinned;

static const float s_white[4] = {1.0F, 1.0F, 1.0F, 1.0F};

static void pack_instance(nt_skinned_instance_t *dst, nt_entity_t entity, const nt_deformation_binding_t *binding, const nt_drawable_comp_view_t *drawables) {
    nt_renderer_pack_world(dst->world_rows, nt_transform_comp_world_matrix(entity));
    dst->frames[0] = (float)binding->x0;
    dst->frames[1] = (float)binding->y0;
    dst->frames[2] = (float)binding->x1;
    dst->frames[3] = (float)binding->y1;
    dst->skin[0] = binding->alpha;
    dst->skin[1] = 0.0F;
    dst->skin[2] = 0.0F;
    dst->skin[3] = 0.0F;
    const bool has_drawable = drawables->sparse_indices != NULL && drawables->sparse_indices[nt_entity_index(entity)] != NT_INVALID_COMP_INDEX;
    memcpy(dst->color, has_drawable ? nt_drawable_comp_color(entity) : s_white, sizeof(dst->color));
}

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

static void destroy_gpu_resources(void) {
    for (uint16_t i = 0; i < s_skinned.pipeline_count; i++) {
        nt_gfx_destroy_pipeline(s_skinned.pipelines[i].pipeline);
    }
    s_skinned.pipeline_count = 0;
    nt_renderer_mesh_vi_cache_reset(&s_skinned.vi_cache);
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
    NT_ASSERT(desc->max_pipelines > 0);
    NT_ASSERT(desc->max_mesh_layouts > 0);
    memset(&s_skinned, 0, sizeof(s_skinned));
    /* 0 without transient textures, and on the stub backend. */
    s_skinned.slice_instances = nt_gfx_transient_texture_capacity() / NT_SKINNED_INSTANCE_TEXELS;
    if (s_skinned.slice_instances == 0) {
        NT_LOG_ERROR("instance data needs gfx transient textures -- set nt_gfx_desc_t.max_transient_textures");
        return NT_ERR_INIT_FAILED;
    }
    s_skinned.max_pipelines = desc->max_pipelines;
    s_skinned.skin_sampler_hash = nt_hash32_str("u_skin_matrices").value;
    s_skinned.instance_base = nt_hash32_str(NT_RENDERER_INSTANCE_BASE_UNIFORM);
    s_skinned.instances_sampler = nt_hash32_str(NT_MATERIAL_INSTANCES_SAMPLER);

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
    s_skinned.staging = (nt_skinned_instance_t *)calloc(s_skinned.slice_instances, sizeof(nt_skinned_instance_t));
    if (s_skinned.staging == NULL) {
        nt_renderer_mesh_vi_cache_shutdown(&s_skinned.vi_cache);
        free(s_skinned.pipelines);
        NT_LOG_ERROR("failed to allocate instance data");
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
    free(s_skinned.staging);
    memset(&s_skinned, 0, sizeof(s_skinned));
}

nt_result_t nt_skinned_mesh_renderer_restore_gpu(void) {
    if (s_skinned.initialized) {
        destroy_gpu_resources();
    }
    return NT_OK;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void nt_skinned_mesh_renderer_draw_list(const nt_render_item_t *items, uint32_t count) {
    NT_ASSERT(s_skinned.initialized);
    if (!s_skinned.initialized || count == 0) {
        return;
    }
    NT_ASSERT(items != NULL);
#ifdef NT_TEST_ACCESS
    s_skinned.frame_draw_calls = 0;
    s_skinned.frame_instance_total = 0;
#endif
    nt_material_t previous_material = {0};
    nt_mesh_t previous_mesh = {0};
    nt_texture_t previous_deformation = {0};
    nt_renderer_bound_t bound = {0};
    nt_pipeline_t pipeline = {0};
    nt_vertex_input_t vertex_input = {0};
    const nt_drawable_comp_view_t drawable_view = nt_drawable_comp_view();

    for (uint32_t slice_start = 0; slice_start < count; slice_start += s_skinned.slice_instances) {
        uint32_t slice_count = count - slice_start;
        if (slice_count > s_skinned.slice_instances) {
            slice_count = s_skinned.slice_instances;
        }
        const uint32_t slice_end = slice_start + slice_count;
        for (uint32_t i = 0; i < slice_count; i++) {
            const nt_entity_t entity = {.id = items[slice_start + i].entity};
            const nt_deformation_binding_t binding = *nt_skin_comp_handle(entity);
            NT_ASSERT(binding.texture.id != 0 && "skinned draw requires a deformation texture");
            pack_instance(&s_skinned.staging[i], entity, &binding, &drawable_view);
        }
        const nt_gfx_texture_binding_t instances = {
            .name = s_skinned.instances_sampler,
            .texture = nt_gfx_transient_texture(s_skinned.staging, slice_count * NT_SKINNED_INSTANCE_TEXELS),
        };
        /* INVALID only while the context is lost, when every draw is a no-op anyway. */
        if (instances.texture.id == 0) {
            return;
        }
        bool slice_unbound = true; /* each slice samples its own instance texture */

        uint32_t run_start = slice_start;
        while (run_start < slice_end) {
            nt_entity_t entity = {.id = items[run_start].entity};
            const nt_deformation_binding_t deformation = *nt_skin_comp_handle(entity);
            nt_material_t material_handle = *nt_material_comp_handle(entity);
            nt_mesh_t mesh_handle = *nt_mesh_comp_handle(entity);
            const uint32_t run_end = find_run_end(items, run_start, slice_end, deformation.texture.id);
            const uint32_t instance_count = run_end - run_start;
            const nt_material_info_t *material = nt_material_get_info(material_handle);
            const nt_gfx_mesh_info_t *mesh = nt_gfx_get_mesh_info(mesh_handle);
            NT_ASSERT(material != NULL && mesh != NULL && "skinned draw references a destroyed material or mesh");
            if (!nt_gfx_program_ready(material->program)) {
                nt_renderer_warn_program_not_ready(&s_skinned.warned_program_not_ready, material);
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
                vertex_input =
                    pipeline.id != 0 ? nt_renderer_mesh_vi_cache_find_or_create(&s_skinned.vi_cache, material_handle, mesh_handle, material, mesh, "skinned_mesh_vi") : NT_VERTEX_INPUT_INVALID;
            }
            if (pipeline.id == 0 || vertex_input.id == 0) {
                run_start = run_end;
                previous_material = (nt_material_t){0};
                previous_mesh = (nt_mesh_t){0};
                previous_deformation = (nt_texture_t){0};
                continue;
            }

            nt_renderer_bind_pipeline(&bound, pipeline);
            if (material_changed || deformation_changed || slice_unbound) {
                nt_sampler_t samplers[NT_MATERIAL_MAX_TEXTURES];
                const nt_renderer_material_view_t view = supplied_material_view(material, deformation.texture, samplers);
                nt_renderer_apply_material_uniforms(&bound, material_handle.id, &view);
                nt_renderer_apply_texture_slots(&view, &instances);
                slice_unbound = false;
            }
            nt_renderer_bind_vertex_input(&bound, vertex_input);
            previous_material = material_handle;
            previous_mesh = mesh_handle;
            previous_deformation = deformation.texture;
            nt_gfx_set_uniform_int(s_skinned.instance_base, (int)(run_start - slice_start));
            if (mesh->index_count > 0) {
                nt_gfx_draw_indexed_instanced(0, mesh->index_count, mesh->vertex_count, instance_count);
            } else {
                nt_gfx_draw_instanced(0, mesh->vertex_count, instance_count);
            }
#ifdef NT_TEST_ACCESS
            s_skinned.frame_draw_calls++;
            s_skinned.frame_instance_total += instance_count;
#endif
            run_start = run_end;
        }
    }
}

#ifdef NT_TEST_ACCESS
uint32_t nt_skinned_mesh_renderer_test_pipeline_cache_count(void) { return s_skinned.pipeline_count; }
uint32_t nt_skinned_mesh_renderer_test_vertex_input_count(void) { return nt_renderer_mesh_vi_cache_live_count(&s_skinned.vi_cache); }
uint32_t nt_skinned_mesh_renderer_test_draw_call_count(void) { return s_skinned.frame_draw_calls; }
uint32_t nt_skinned_mesh_renderer_test_instance_total(void) { return s_skinned.frame_instance_total; }
bool nt_skinned_mesh_renderer_test_initialized(void) { return s_skinned.initialized; }
#endif
