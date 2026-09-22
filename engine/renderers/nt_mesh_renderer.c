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

/* ---- Module state ---- */

static struct {
    nt_renderer_pipeline_entry_t *entries; /* [max_pipelines] */
    uint16_t max_pipelines;
    uint16_t count;

    nt_renderer_mesh_vi_cache_t vi_cache;

    nt_buffer_t instance_buf; /* dynamic vertex buffer for instance data */

    uint8_t *instance_data; /* CPU staging byte buffer [max_instances * NT_INSTANCE_STRIDE_MAX] */
    uint16_t max_instances;
    uint32_t ring_cursor; /* next free byte in instance_buf; disjoint writes avoid driver copies of in-flight data */

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
    /* Sprite and text gate on readiness here; this renderer gates in draw_list, so
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

static nt_result_t create_gpu_resources(void) {
    s_mesh_renderer.instance_buf = nt_gfx_make_buffer(&(nt_buffer_desc_t){
        .type = NT_BUFFER_VERTEX,
        .usage = NT_USAGE_STREAM,
        .size = (uint32_t)s_mesh_renderer.max_instances * (uint32_t)NT_INSTANCE_STRIDE_MAX,
        .label = "mesh_renderer_instance",
    });
    if (s_mesh_renderer.instance_buf.id == 0) {
        return NT_ERR_INIT_FAILED;
    }
    return NT_OK;
}

static void destroy_gpu_resources(void) {
    nt_gfx_destroy_buffer(s_mesh_renderer.instance_buf);
    s_mesh_renderer.instance_buf = (nt_buffer_t){0};
    for (uint16_t i = 0; i < s_mesh_renderer.count; i++) {
        nt_gfx_destroy_pipeline(s_mesh_renderer.entries[i].pipeline);
    }
    s_mesh_renderer.count = 0;
    nt_renderer_mesh_vi_cache_reset(&s_mesh_renderer.vi_cache);
    s_mesh_renderer.ring_cursor = 0;
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
    NT_ASSERT(desc->max_instances > 0);
    NT_ASSERT(desc->max_pipelines > 0);
    NT_ASSERT(desc->max_mesh_layouts > 0);

    memset(&s_mesh_renderer, 0, sizeof(s_mesh_renderer));

    s_mesh_renderer.max_instances = desc->max_instances;
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

    /* Allocate CPU staging byte buffer (worst-case stride) */
    s_mesh_renderer.instance_data = (uint8_t *)calloc(desc->max_instances, NT_INSTANCE_STRIDE_MAX);
    if (!s_mesh_renderer.instance_data) {
        nt_renderer_mesh_vi_cache_shutdown(&s_mesh_renderer.vi_cache);
        free(s_mesh_renderer.entries);
        s_mesh_renderer.entries = NULL;
        NT_LOG_ERROR("failed to allocate instance data");
        return NT_ERR_INIT_FAILED;
    }

    if (create_gpu_resources() != NT_OK) {
        free(s_mesh_renderer.instance_data);
        s_mesh_renderer.instance_data = NULL;
        nt_renderer_mesh_vi_cache_shutdown(&s_mesh_renderer.vi_cache);
        free(s_mesh_renderer.entries);
        s_mesh_renderer.entries = NULL;
        NT_LOG_ERROR("failed to create instance buffer");
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

    /* Free CPU staging byte buffer */
    free(s_mesh_renderer.instance_data);

    memset(&s_mesh_renderer, 0, sizeof(s_mesh_renderer));
}

nt_result_t nt_mesh_renderer_restore_gpu(void) {
    if (!s_mesh_renderer.initialized) {
        return NT_OK;
    }
    destroy_gpu_resources();
    return create_gpu_resources();
}

/* ---- Draw list ---- */

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void nt_mesh_renderer_draw_list(const nt_render_item_t *items, uint32_t count) {

    NT_ASSERT(s_mesh_renderer.initialized);
    if (!s_mesh_renderer.initialized || count == 0) {
        return;
    }
    NT_ASSERT(items != NULL);
    NT_ASSERT(s_mesh_renderer.instance_buf.id != 0 && "retry failed GPU restore before drawing");

#ifdef NT_TEST_ACCESS
    /* Reset per-frame tracking */
    s_mesh_renderer.frame_draw_calls = 0;
    s_mesh_renderer.frame_instance_total = 0;
#endif

    /* Process items in chunks of max_instances.
     * Each chunk: pack instance data -> upload -> draw with offsets.
     * Typically 1 chunk (items < max_instances). */
    uint32_t chunk_start = 0;
    nt_material_t prev_mat = {0};
    nt_mesh_t prev_mesh = {0};
    /* Call-scoped: chunks share it because the per-chunk instance upload touches
     * ARRAY_BUFFER only and leaves pipeline / VI / texture-unit state alone. */
    nt_renderer_bound_t bound = {0};
    nt_pipeline_t pip = {0};
    nt_vertex_input_t vi = {0};
    const nt_drawable_comp_view_t drawable_view = nt_drawable_comp_view();

    while (chunk_start < count) {
        uint32_t chunk_count = count - chunk_start;
        if (chunk_count > s_mesh_renderer.max_instances) {
            chunk_count = s_mesh_renderer.max_instances;
        }
        uint32_t chunk_end = chunk_start + chunk_count;

        /* ---- Pack all instances in this chunk into byte buffer ---- */
        /* First pass: pack at variable stride per draw group */
        uint32_t packed_size = 0;
        uint32_t scan = chunk_start;
        while (scan < chunk_end) {
            uint32_t run_end = scan + 1;
            while (run_end < chunk_end && items[run_end].batch_key == items[scan].batch_key) {
                run_end++;
            }

            /* Determine color mode for this run */
            nt_entity_t first_entity = {.id = items[scan].entity};
            nt_material_t run_mat = *nt_material_comp_handle(first_entity);
            const nt_material_info_t *mat_info = nt_material_get_info(run_mat);
            NT_ASSERT(mat_info != NULL && "mesh render item references a destroyed material");
            nt_color_mode_t color_mode = mat_info->color_mode;
            NT_ASSERT(color_mode <= NT_COLOR_MODE_FLOAT4); /* corrupted material = programmer error */
            uint16_t stride = s_instance_layouts[color_mode].stride;

            for (uint32_t i = scan; i < run_end; i++) {
                nt_entity_t e = {.id = items[i].entity};
                uint8_t *dst = s_mesh_renderer.instance_data + packed_size;

                const float *world = nt_transform_comp_world_matrix(e);
                nt_renderer_pack_world((float *)dst, world);

                if (color_mode == NT_COLOR_MODE_RGBA8) {
                    const uint16_t drawable_index = drawable_view.sparse_indices[nt_entity_index(e)];
                    NT_ASSERT(drawable_index != NT_INVALID_COMP_INDEX && "mesh render item: entity has no drawable component");
                    memcpy(dst + 48, &drawable_view.colors_packed[drawable_index], sizeof(uint32_t));
                } else if (color_mode == NT_COLOR_MODE_FLOAT4) {
                    const float *color = nt_drawable_comp_color(e);
                    memcpy(dst + 48, color, 16);
                }
                /* NONE: nothing after the 48 bytes */

                packed_size += stride;
            }
            scan = run_end;
        }

        /* ---- Single GPU upload for packed byte data, ring-allocated ---- */
        uint32_t capacity = (uint32_t)s_mesh_renderer.max_instances * (uint32_t)NT_INSTANCE_STRIDE_MAX;
        if (s_mesh_renderer.ring_cursor + packed_size > capacity) {
            s_mesh_renderer.ring_cursor = 0; /* wrap overlaps in-flight data (every alloc once a frame fills capacity); driver copies */
        }
        uint32_t ring_base = s_mesh_renderer.ring_cursor;
        s_mesh_renderer.ring_cursor = ring_base + packed_size;
        nt_gfx_update_buffer(s_mesh_renderer.instance_buf, ring_base, s_mesh_renderer.instance_data, packed_size);

        /* ---- Draw runs within this chunk ---- */
        uint32_t run_start = chunk_start;
        uint32_t draw_byte_offset = ring_base;

        while (run_start < chunk_end) {
            nt_entity_t entity = {.id = items[run_start].entity};
            nt_material_t run_mat = *nt_material_comp_handle(entity);
            nt_mesh_t run_mesh = *nt_mesh_comp_handle(entity);

            uint32_t run_end = run_start + 1;
            while (run_end < chunk_end && items[run_end].batch_key == items[run_start].batch_key) {
                run_end++;
            }

            uint32_t instance_count = run_end - run_start;

            const nt_material_info_t *mat_info = nt_material_get_info(run_mat);
            const nt_gfx_mesh_info_t *mesh_info = nt_gfx_get_mesh_info(run_mesh);

            NT_ASSERT(mat_info != NULL && mesh_info != NULL && "draw_list: a run's material or mesh was destroyed mid-call");
            if (!nt_gfx_program_ready(mat_info->program)) {
                nt_renderer_warn_program_not_ready(&s_mesh_renderer.warned_program_not_ready, mat_info);
                /* Still need to advance byte offset for skipped runs */
                draw_byte_offset += instance_count * s_instance_layouts[mat_info->color_mode].stride;
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
                vi = (pip.id != 0) ? nt_renderer_mesh_vi_cache_find_or_create(&s_mesh_renderer.vi_cache, run_mat, run_mesh, mat_info, mesh_info, s_instance_layouts, "mesh_vi")
                                   : NT_VERTEX_INPUT_INVALID;
            }
            if (pip.id == 0 || vi.id == 0) {
                draw_byte_offset += instance_count * s_instance_layouts[mat_info->color_mode].stride;
                run_start = run_end;
                prev_mat = (nt_material_t){0};
                prev_mesh = (nt_mesh_t){0};
                continue;
            }

            if (mat_changed) {
                const nt_renderer_material_view_t view = nt_renderer_material_view(mat_info);
                nt_renderer_bind_pipeline(&bound, pip);
                nt_renderer_apply_material_uniforms(&bound, run_mat.id, &view);
                /* Mesh renderer texture slots come from the material alone. */
                nt_renderer_apply_texture_slots(&view);
            }
            nt_renderer_bind_vertex_input(&bound, vi);
            prev_mat = run_mat;
            prev_mesh = run_mesh;

            if (mat_info->color_mode == NT_COLOR_MODE_NONE) {
                /* Native GL leaves a generic value unspecified after drawing with an enabled array there. */
                nt_gfx_set_vertex_attrib_default(7, 1.0F, 1.0F, 1.0F, 1.0F);
            }
            nt_gfx_bind_instance_buffer(s_mesh_renderer.instance_buf, draw_byte_offset);

            if (mesh_info->index_count > 0) {
                nt_gfx_draw_indexed_instanced(0, mesh_info->index_count, mesh_info->vertex_count, instance_count);
            } else {
                nt_gfx_draw_instanced(0, mesh_info->vertex_count, instance_count);
            }

#ifdef NT_TEST_ACCESS
            s_mesh_renderer.frame_draw_calls++;
            s_mesh_renderer.frame_instance_total += instance_count;
#endif

            draw_byte_offset += instance_count * s_instance_layouts[mat_info->color_mode].stride;
            run_start = run_end;
        }

        chunk_start = chunk_end;
    }
}

#ifdef NT_TEST_ACCESS
/* ---- Test accessors ---- */

uint32_t nt_mesh_renderer_test_pipeline_cache_count(void) { return s_mesh_renderer.count; }

uint32_t nt_mesh_renderer_test_vertex_input_count(void) { return nt_renderer_mesh_vi_cache_live_count(&s_mesh_renderer.vi_cache); }

uint32_t nt_mesh_renderer_test_draw_call_count(void) { return s_mesh_renderer.frame_draw_calls; }

uint32_t nt_mesh_renderer_test_instance_total(void) { return s_mesh_renderer.frame_instance_total; }

uint32_t nt_mesh_renderer_test_ring_cursor(void) { return s_mesh_renderer.ring_cursor; }

bool nt_mesh_renderer_test_initialized(void) { return s_mesh_renderer.initialized; }
#endif
