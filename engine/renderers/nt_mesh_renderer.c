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
#include "renderers/nt_mesh_run_internal.h"
#include "renderers/nt_renderer_shared.h"
#include "transform_comp/nt_transform_comp.h"

#include <stdlib.h>
#include <string.h>

/* ---- Module state ---- */

static struct {
    nt_renderer_pipeline_entry_t *entries; /* [max_pipelines] */
    uint16_t max_pipelines;
    uint16_t count;

    nt_renderer_mesh_vi_cache_t vi_cache;

    /* One-shot so a load-time skip does not spam; re-armed when a pipeline is
     * built, i.e. when something became drawable again. */
    bool warned_program_not_ready;

    bool initialized;
} s_mesh_renderer;

/* ---- Instance layout per color mode (locations 4-6 for mat4x3, 7 for color) ---- */

/* clang-format off */
static const nt_vertex_layout_t s_instance_layouts[3] = {
    [NT_COLOR_MODE_NONE] = {
        .attr_count = 3,
        .stride = NT_INSTANCE_STRIDE_NONE,
        .attrs = {
            {.location = 4, .type = NT_VERTEX_FLOAT, .count = 4, .offset = 0},
            {.location = 5, .type = NT_VERTEX_FLOAT, .count = 4, .offset = 16},
            {.location = 6, .type = NT_VERTEX_FLOAT, .count = 4, .offset = 32},
        },
    },
    [NT_COLOR_MODE_RGBA8] = {
        .attr_count = 4,
        .stride = NT_INSTANCE_STRIDE_RGBA8,
        .attrs = {
            {.location = 4, .type = NT_VERTEX_FLOAT, .count = 4, .offset = 0},
            {.location = 5, .type = NT_VERTEX_FLOAT, .count = 4, .offset = 16},
            {.location = 6, .type = NT_VERTEX_FLOAT, .count = 4, .offset = 32},
            {.location = 7, .type = NT_VERTEX_UINT8, .count = 4, .normalized = true, .offset = 48},
        },
    },
    [NT_COLOR_MODE_FLOAT4] = {
        .attr_count = 4,
        .stride = NT_INSTANCE_STRIDE_FLOAT4,
        .attrs = {
            {.location = 4, .type = NT_VERTEX_FLOAT, .count = 4, .offset = 0},
            {.location = 5, .type = NT_VERTEX_FLOAT, .count = 4, .offset = 16},
            {.location = 6, .type = NT_VERTEX_FLOAT, .count = 4, .offset = 32},
            {.location = 7, .type = NT_VERTEX_FLOAT, .count = 4, .offset = 48},
        },
    },
};
/* clang-format on */

/* ---- Pipeline cache lookup/create ---- */

static nt_pipeline_t find_or_create_pipeline(const nt_material_info_t *mat_info) {
    /* Sprite and text gate on readiness here; this renderer gates in prepare, so
     * state the requirement where the pipeline is actually built. */
    NT_ASSERT(nt_gfx_program_ready(mat_info->program) && "find_or_create_pipeline: caller must gate on nt_gfx_program_ready");

    /* Layouts and color_mode live on the vertex-input versions; the pipeline is
     * program x render state, keyed by its exact desc identity. */
    const nt_pipeline_desc_t desc = nt_renderer_material_pipeline_desc(mat_info, "mesh_pipeline");
    const nt_gfx_pipeline_key_t key = nt_gfx_pipeline_key(&desc);

    const nt_pipeline_t cached = nt_renderer_pipeline_cache_find(s_mesh_renderer.entries, s_mesh_renderer.count, &key);
    if (cached.id != 0) {
        return cached;
    }
    return nt_renderer_pipeline_cache_insert(s_mesh_renderer.entries, &s_mesh_renderer.count, s_mesh_renderer.max_pipelines, &key, &desc, &s_mesh_renderer.warned_program_not_ready);
}

/* ---- Lifecycle ---- */

static void reset_gpu_caches(void) {
    for (uint16_t i = 0; i < s_mesh_renderer.count; i++) {
        nt_gfx_destroy_pipeline(s_mesh_renderer.entries[i].pipeline);
    }
    s_mesh_renderer.count = 0;
    nt_renderer_mesh_vi_cache_reset(&s_mesh_renderer.vi_cache);
    s_mesh_renderer.warned_program_not_ready = false;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
nt_result_t nt_mesh_renderer_init(const nt_mesh_renderer_desc_t *desc) {
    NT_ASSERT(!s_mesh_renderer.initialized);
    NT_ASSERT(desc);
    NT_ASSERT(desc->max_pipelines > 0);
    NT_ASSERT(desc->max_mesh_layouts > 0);

    memset(&s_mesh_renderer, 0, sizeof(s_mesh_renderer));

    s_mesh_renderer.max_pipelines = desc->max_pipelines;
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

    s_mesh_renderer.initialized = true;
    return NT_OK;
}

void nt_mesh_renderer_shutdown(void) {
    if (!s_mesh_renderer.initialized) {
        return;
    }

    reset_gpu_caches();
    nt_renderer_mesh_vi_cache_shutdown(&s_mesh_renderer.vi_cache);

    /* Free pipeline cache */
    free(s_mesh_renderer.entries);

    memset(&s_mesh_renderer, 0, sizeof(s_mesh_renderer));
}

void nt_mesh_renderer_restore_gpu(void) {
    if (s_mesh_renderer.initialized) {
        reset_gpu_caches();
    }
}

/* ---- Prepare / draw ---- */

static uint32_t find_run_end(const nt_render_item_t *items, uint32_t run_start, uint32_t count) {
    uint32_t run_end = run_start + 1;
    while (run_end < count && items[run_end].batch_key == items[run_start].batch_key) {
        run_end++;
    }
    return run_end;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
uint32_t nt_mesh_renderer_prepare(const nt_render_item_t *items, uint32_t count, nt_mesh_run_t *runs, uint32_t max_runs) {
    NT_ASSERT(s_mesh_renderer.initialized);
    if (!s_mesh_renderer.initialized || count == 0) {
        return 0;
    }
    NT_ASSERT(items != NULL && runs != NULL);

    // #region resolve runs
    /* A run's offset holds its first item index until the reserve places it. */
    uint32_t run_count = 0;
    uint32_t size = 0;
    nt_material_t prev_mat = {0};
    nt_mesh_t prev_mesh = {0};
    nt_pipeline_t pip = {0};
    nt_vertex_input_t vi = {0};
    for (uint32_t run_start = 0, run_end = 0; run_start < count; run_start = run_end) {
        run_end = find_run_end(items, run_start, count);
        nt_entity_t entity = {.id = items[run_start].entity};
        const nt_material_t run_mat = *nt_material_comp_handle(entity);
        const nt_mesh_t run_mesh = *nt_mesh_comp_handle(entity);
        const nt_material_info_t *mat_info = nt_material_get_info(run_mat);
        const nt_gfx_mesh_info_t *mesh_info = nt_gfx_get_mesh_info(run_mesh);
        NT_ASSERT(mat_info != NULL && mesh_info != NULL && "mesh render item references a destroyed material or mesh");
        NT_ASSERT(mat_info->color_mode <= NT_COLOR_MODE_FLOAT4); /* corrupted material = programmer error */
        if (!nt_gfx_program_ready(mat_info->program)) {
            nt_renderer_warn_program_not_ready(&s_mesh_renderer.warned_program_not_ready, mat_info);
            continue;
        }

        const bool mat_changed = run_mat.id != prev_mat.id;
        if (mat_changed) {
            pip = find_or_create_pipeline(mat_info);
        }
        /* VI identity is (mesh row, material-derived layout), so a mesh change re-resolves too. */
        if (mat_changed || run_mesh.id != prev_mesh.id) {
            vi = (pip.id != 0) ? nt_renderer_mesh_vi_cache_find_or_create(&s_mesh_renderer.vi_cache, run_mat, run_mesh, mat_info, mesh_info, s_instance_layouts, "mesh_vi") : NT_VERTEX_INPUT_INVALID;
        }
        if (pip.id == 0 || vi.id == 0) {
            /* Retry creation on the next run instead of reusing a failure. */
            prev_mat = (nt_material_t){0};
            prev_mesh = (nt_mesh_t){0};
            continue;
        }
        prev_mat = run_mat;
        prev_mesh = run_mesh;

        NT_ASSERT(run_count < max_runs && "mesh_renderer_prepare: runs exhausted; max_runs >= count always suffices");
        const uint32_t instance_count = run_end - run_start;
        runs[run_count++] = (nt_mesh_run_t){
            .pipeline = pip,
            .vertex_input = vi,
            .material = run_mat,
            .offset = run_start,
            .instance_count = instance_count,
            .index_count = mesh_info->index_count,
            .vertex_count = mesh_info->vertex_count,
            .supplied_slot = NT_MATERIAL_MAX_TEXTURES,
            .color_mode = (uint8_t)mat_info->color_mode,
            .color_location = 7,
        };
        size += instance_count * s_instance_layouts[mat_info->color_mode].stride;
    }
    // #endregion
    if (run_count == 0) {
        return 0;
    }

    // #region pack instances
    uint32_t offset = 0;
    uint8_t *const base = (uint8_t *)nt_frame_arena_reserve(size, &offset);
    uint8_t *dst = base;
    const nt_drawable_comp_view_t drawable_view = nt_drawable_comp_view();
    for (uint32_t r = 0; r < run_count; r++) {
        nt_mesh_run_t *run = &runs[r];
        const uint32_t first = run->offset;
        run->offset = offset + (uint32_t)(dst - base);
        const uint32_t end = first + run->instance_count;
        const uint8_t color_mode = run->color_mode;
        const uint16_t stride = s_instance_layouts[color_mode].stride;
        for (uint32_t i = first; i < end; i++) {
            nt_entity_t e = {.id = items[i].entity};
            nt_renderer_pack_world((float *)dst, nt_transform_comp_world_matrix(e));
            if (color_mode == NT_COLOR_MODE_RGBA8) {
                const uint16_t drawable_index = drawable_view.sparse_indices[nt_entity_index(e)];
                NT_ASSERT(drawable_index != NT_INVALID_COMP_INDEX && "mesh render item: entity has no drawable component");
                memcpy(dst + 48, &drawable_view.colors_packed[drawable_index], sizeof(uint32_t));
            } else if (color_mode == NT_COLOR_MODE_FLOAT4) {
                memcpy(dst + 48, nt_drawable_comp_color(e), 16);
            }
            /* NONE: nothing after the 48 bytes */
            dst += stride;
        }
    }
    // #endregion
    return run_count;
}

void nt_mesh_renderer_draw(const nt_mesh_run_t *runs, uint32_t run_count) {
    NT_ASSERT(s_mesh_renderer.initialized);
    NT_ASSERT(run_count == 0 || runs != NULL);
    nt_mesh_runs_draw(runs, run_count);
}

#ifdef NT_TEST_ACCESS
/* ---- Test accessors ---- */

uint32_t nt_mesh_renderer_test_pipeline_cache_count(void) { return s_mesh_renderer.count; }

uint32_t nt_mesh_renderer_test_vertex_input_count(void) { return nt_renderer_mesh_vi_cache_live_count(&s_mesh_renderer.vi_cache); }

bool nt_mesh_renderer_test_initialized(void) { return s_mesh_renderer.initialized; }
#endif
