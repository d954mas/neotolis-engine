#include "renderers/nt_mesh_renderer.h"

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
#include "transform_comp/nt_transform_comp.h"

#include <stdlib.h>
#include <string.h>

/* Texel mirror of the mesh payload read by nt_instance() in common/instance.glsl. */
typedef struct {
    float world_rows[12];
    float color[4];
} nt_mesh_instance_t;
_Static_assert(sizeof(nt_mesh_instance_t) == (size_t)NT_MESH_RENDERER_INSTANCE_TEXELS * 16U, "mesh instance payload is four texels");

/* ---- Module state ---- */

static struct {
    nt_renderer_pipeline_entry_t *entries; /* [max_pipelines] */
    uint16_t max_pipelines;
    uint16_t count;

    nt_renderer_mesh_vi_cache_t vi_cache;

    nt_mesh_instance_t *staging; /* one transient texture of texels */
    uint32_t slice_instances;
    nt_hash32_t instance_base;
    nt_hash32_t instances_sampler;

    /* One-shot so a load-time skip does not spam; re-armed when a pipeline is
     * built, i.e. when something became drawable again. */
    bool warned_program_not_ready;

#ifdef NT_TEST_ACCESS
    /* Per-frame tracking for test accessors */
    uint32_t frame_draw_calls;
    uint32_t frame_instance_total;
#endif

    bool initialized;
} s_mesh_renderer;

static const float s_white[4] = {1.0F, 1.0F, 1.0F, 1.0F};

/* ---- Pipeline cache lookup/create ---- */

static nt_pipeline_t find_or_create_pipeline(const nt_material_info_t *mat_info) {
    /* Sprite and text gate on readiness here; this renderer gates in draw_list, so
     * state the requirement where the pipeline is actually built. */
    NT_ASSERT(nt_gfx_program_ready(mat_info->program) && "find_or_create_pipeline: caller must gate on nt_gfx_program_ready");

    /* Layouts live on the vertex-input versions; the pipeline is program x
     * render state, keyed by its exact desc identity. */
    const nt_pipeline_desc_t desc = nt_renderer_material_pipeline_desc(mat_info, "mesh_pipeline");
    const nt_gfx_pipeline_key_t key = nt_gfx_pipeline_key(&desc);

    const nt_pipeline_t cached = nt_renderer_pipeline_cache_find(s_mesh_renderer.entries, s_mesh_renderer.count, &key);
    if (cached.id != 0) {
        return cached;
    }
    return nt_renderer_pipeline_cache_insert(s_mesh_renderer.entries, &s_mesh_renderer.count, s_mesh_renderer.max_pipelines, &key, &desc, &s_mesh_renderer.warned_program_not_ready);
}

/* ---- Lifecycle ---- */

static void destroy_gpu_resources(void) {
    for (uint16_t i = 0; i < s_mesh_renderer.count; i++) {
        nt_gfx_destroy_pipeline(s_mesh_renderer.entries[i].pipeline);
    }
    s_mesh_renderer.count = 0;
    nt_renderer_mesh_vi_cache_reset(&s_mesh_renderer.vi_cache);
#ifdef NT_TEST_ACCESS
    s_mesh_renderer.frame_draw_calls = 0;
    s_mesh_renderer.frame_instance_total = 0;
#endif
    s_mesh_renderer.warned_program_not_ready = false;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
nt_result_t nt_mesh_renderer_init(const nt_mesh_renderer_desc_t *desc) {
    NT_ASSERT(!s_mesh_renderer.initialized);
    NT_ASSERT(desc);
    NT_ASSERT(desc->max_pipelines > 0);
    NT_ASSERT(desc->max_mesh_layouts > 0);

    memset(&s_mesh_renderer, 0, sizeof(s_mesh_renderer));

    /* 0 without transient textures, and on the stub backend. */
    s_mesh_renderer.slice_instances = nt_gfx_transient_texture_capacity() / NT_MESH_RENDERER_INSTANCE_TEXELS;
    if (s_mesh_renderer.slice_instances == 0) {
        NT_LOG_ERROR("instance data needs gfx transient textures -- set nt_gfx_desc_t.max_transient_textures");
        return NT_ERR_INIT_FAILED;
    }
    s_mesh_renderer.max_pipelines = desc->max_pipelines;
    s_mesh_renderer.instance_base = nt_hash32_str(NT_RENDERER_INSTANCE_BASE_UNIFORM);
    s_mesh_renderer.instances_sampler = nt_hash32_str(NT_MATERIAL_INSTANCES_SAMPLER);
    /* Allocate pipeline cache */
    s_mesh_renderer.entries = (nt_renderer_pipeline_entry_t *)calloc(desc->max_pipelines, sizeof(nt_renderer_pipeline_entry_t));
    if (!s_mesh_renderer.entries) {
        NT_LOG_ERROR("failed to allocate pipeline cache");
        return NT_ERR_INIT_FAILED;
    }

    if (nt_renderer_mesh_vi_cache_init(&s_mesh_renderer.vi_cache, desc->max_mesh_layouts) != NT_OK) {
        free(s_mesh_renderer.entries);
        s_mesh_renderer.entries = NULL;
        return NT_ERR_INIT_FAILED;
    }

    /* The whole texture, not slice_instances payloads: uploads read up to the end of the last row. */
    s_mesh_renderer.staging = (nt_mesh_instance_t *)calloc(nt_gfx_transient_texture_capacity(), 16U);
    if (!s_mesh_renderer.staging) {
        nt_renderer_mesh_vi_cache_shutdown(&s_mesh_renderer.vi_cache);
        free(s_mesh_renderer.entries);
        s_mesh_renderer.entries = NULL;
        NT_LOG_ERROR("failed to allocate instance data");
        return NT_ERR_INIT_FAILED;
    }

    s_mesh_renderer.initialized = true;
    return NT_OK;
}

void nt_mesh_renderer_shutdown(void) {
    if (!s_mesh_renderer.initialized) {
        return;
    }

    destroy_gpu_resources();
    nt_renderer_mesh_vi_cache_shutdown(&s_mesh_renderer.vi_cache);

    /* Free pipeline cache */
    free(s_mesh_renderer.entries);

    free(s_mesh_renderer.staging);

    memset(&s_mesh_renderer, 0, sizeof(s_mesh_renderer));
}

nt_result_t nt_mesh_renderer_restore_gpu(void) {
    if (s_mesh_renderer.initialized) {
        destroy_gpu_resources();
    }
    return NT_OK;
}

/* ---- Draw list ---- */

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void nt_mesh_renderer_draw_list(const nt_render_item_t *items, uint32_t count) {

    NT_ASSERT(s_mesh_renderer.initialized);
    if (!s_mesh_renderer.initialized || count == 0) {
        return;
    }
    NT_ASSERT(items != NULL);

#ifdef NT_TEST_ACCESS
    /* Reset per-frame tracking */
    s_mesh_renderer.frame_draw_calls = 0;
    s_mesh_renderer.frame_instance_total = 0;
#endif

    /* Each slice packs up to one transient texture of instances, uploads it, then
     * draws its runs with a base index into that texture. */
    nt_material_t prev_mat = {0};
    nt_mesh_t prev_mesh = {0};
    nt_renderer_bound_t bound = {0};
    nt_pipeline_t pip = {0};
    nt_vertex_input_t vi = {0};
    const nt_drawable_comp_view_t drawable_view = nt_drawable_comp_view();

    for (uint32_t slice_start = 0; slice_start < count; slice_start += s_mesh_renderer.slice_instances) {
        uint32_t slice_count = count - slice_start;
        if (slice_count > s_mesh_renderer.slice_instances) {
            slice_count = s_mesh_renderer.slice_instances;
        }
        const uint32_t slice_end = slice_start + slice_count;

        for (uint32_t i = 0; i < slice_count; i++) {
            const nt_entity_t e = {.id = items[slice_start + i].entity};
            nt_mesh_instance_t *dst = &s_mesh_renderer.staging[i];
            nt_renderer_pack_world(dst->world_rows, nt_transform_comp_world_matrix(e));
            const bool has_drawable = drawable_view.sparse_indices != NULL && drawable_view.sparse_indices[nt_entity_index(e)] != NT_INVALID_COMP_INDEX;
            memcpy(dst->color, has_drawable ? nt_drawable_comp_color(e) : s_white, sizeof(dst->color));
        }
        const nt_gfx_texture_binding_t instances = {
            .name = s_mesh_renderer.instances_sampler,
            .texture = nt_gfx_transient_texture(s_mesh_renderer.staging, slice_count * NT_MESH_RENDERER_INSTANCE_TEXELS),
        };
        /* INVALID while the context is lost (every draw is a no-op) or when gfx has no
         * transient texture left, which its failed creates already logged. */
        if (instances.texture.id == 0) {
            return;
        }
        bool slice_unbound = true; /* each slice samples its own instance texture */

        uint32_t run_start = slice_start;
        while (run_start < slice_end) {
            nt_entity_t entity = {.id = items[run_start].entity};
            nt_material_t run_mat = *nt_material_comp_handle(entity);
            nt_mesh_t run_mesh = *nt_mesh_comp_handle(entity);

            uint32_t run_end = run_start + 1;
            while (run_end < slice_end && items[run_end].batch_key == items[run_start].batch_key) {
                run_end++;
            }

            uint32_t instance_count = run_end - run_start;

            const nt_material_info_t *mat_info = nt_material_get_info(run_mat);
            const nt_gfx_mesh_info_t *mesh_info = nt_gfx_get_mesh_info(run_mesh);

            NT_ASSERT(mat_info != NULL && mesh_info != NULL && "draw_list: a run's material or mesh was destroyed mid-call");
            if (!nt_gfx_program_ready(mat_info->program)) {
                nt_renderer_warn_program_not_ready(&s_mesh_renderer.warned_program_not_ready, mat_info);
                run_start = run_end;
                continue;
            }

            const bool mat_changed = run_mat.id != prev_mat.id;
            const bool mesh_changed = run_mesh.id != prev_mesh.id;
            if (mat_changed) {
                pip = find_or_create_pipeline(mat_info);
            }
            /* VI identity is (mesh row, material-derived layout), so a mesh change re-resolves too. */
            if (mat_changed || mesh_changed) {
                vi = (pip.id != 0) ? nt_renderer_mesh_vi_cache_find_or_create(&s_mesh_renderer.vi_cache, run_mat, run_mesh, mat_info, mesh_info, "mesh_vi") : NT_VERTEX_INPUT_INVALID;
            }
            if (pip.id == 0 || vi.id == 0) {
                run_start = run_end;
                prev_mat = (nt_material_t){0};
                prev_mesh = (nt_mesh_t){0};
                continue;
            }

            if (mat_changed || slice_unbound) {
                const nt_renderer_material_view_t view = nt_renderer_material_view(mat_info);
                nt_renderer_bind_pipeline(&bound, pip);
                nt_renderer_apply_material_uniforms(&bound, run_mat.id, &view);
                nt_renderer_apply_texture_slots(&view, &instances);
                slice_unbound = false;
            }
            nt_renderer_bind_vertex_input(&bound, vi);
            prev_mat = run_mat;
            prev_mesh = run_mesh;

            nt_gfx_set_uniform_int(s_mesh_renderer.instance_base, (int)(run_start - slice_start));
            if (mesh_info->index_count > 0) {
                nt_gfx_draw_indexed_instanced(0, mesh_info->index_count, mesh_info->vertex_count, instance_count);
            } else {
                nt_gfx_draw_instanced(0, mesh_info->vertex_count, instance_count);
            }

#ifdef NT_TEST_ACCESS
            s_mesh_renderer.frame_draw_calls++;
            s_mesh_renderer.frame_instance_total += instance_count;
#endif

            run_start = run_end;
        }
    }
}

#ifdef NT_TEST_ACCESS
/* ---- Test accessors ---- */

uint32_t nt_mesh_renderer_test_pipeline_cache_count(void) { return s_mesh_renderer.count; }

uint32_t nt_mesh_renderer_test_vertex_input_count(void) { return nt_renderer_mesh_vi_cache_live_count(&s_mesh_renderer.vi_cache); }

uint32_t nt_mesh_renderer_test_draw_call_count(void) { return s_mesh_renderer.frame_draw_calls; }

uint32_t nt_mesh_renderer_test_instance_total(void) { return s_mesh_renderer.frame_instance_total; }

bool nt_mesh_renderer_test_initialized(void) { return s_mesh_renderer.initialized; }
#endif
