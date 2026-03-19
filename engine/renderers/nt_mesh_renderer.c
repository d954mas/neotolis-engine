#include "renderers/nt_mesh_renderer.h"

#include "core/nt_assert.h"
#include "drawable_comp/nt_drawable_comp.h"
#include "graphics/nt_gfx.h"
#include "log/nt_log.h"
#include "material/nt_material.h"
#include "material_comp/nt_material_comp.h"
#include "mesh_comp/nt_mesh_comp.h"
#include "transform_comp/nt_transform_comp.h"

#include <stdlib.h>
#include <string.h>

/* ---- Pipeline cache entry ---- */

typedef struct {
    uint64_t key; /* hash of full pipeline signature (layout + shaders + render state) */
    nt_pipeline_t pipeline;
} nt_pipeline_cache_entry_t;

/* ---- Module state ---- */

static struct {
    nt_pipeline_cache_entry_t *entries; /* [max_pipelines] */
    uint16_t max_pipelines;
    uint16_t count;

    nt_buffer_t instance_buf; /* dynamic vertex buffer for nt_mesh_instance_t */

    nt_mesh_instance_t *instance_data; /* CPU staging array [max_instances] */
    uint16_t max_instances;

    /* Per-frame tracking for test accessors */
    uint32_t frame_draw_calls;
    uint32_t frame_instance_total;

    bool initialized;
} s_mesh_renderer;

/* ---- Instance layout (vertex attributes with divisor=1) ---- */

static const nt_vertex_layout_t s_instance_layout = {
    .attr_count = 5,
    .stride = 80, /* sizeof(nt_mesh_instance_t) */
    .attrs =
        {
            {.location = 4, .format = NT_FORMAT_FLOAT4, .offset = 0},  /* world_matrix col 0 */
            {.location = 5, .format = NT_FORMAT_FLOAT4, .offset = 16}, /* world_matrix col 1 */
            {.location = 6, .format = NT_FORMAT_FLOAT4, .offset = 32}, /* world_matrix col 2 */
            {.location = 7, .format = NT_FORMAT_FLOAT4, .offset = 48}, /* world_matrix col 3 */
            {.location = 8, .format = NT_FORMAT_FLOAT4, .offset = 64}, /* color */
        },
};

/* ---- Stream type to vertex format mapping ---- */

/* Visible to tests via NT_MESH_RENDERER_TEST_ACCESS */
nt_vertex_format_t nt_stream_to_vertex_format(uint8_t type, uint8_t count, uint8_t normalized) {
    switch (type) {
    case NT_STREAM_FLOAT32:
        if (count == 1)
            return NT_FORMAT_FLOAT;
        if (count == 2)
            return NT_FORMAT_FLOAT2;
        if (count == 3)
            return NT_FORMAT_FLOAT3;
        return NT_FORMAT_FLOAT4;
    case NT_STREAM_FLOAT16:
        return (count <= 2) ? NT_FORMAT_HALF2 : NT_FORMAT_HALF4;
    case NT_STREAM_INT16:
        return (count <= 2) ? (normalized ? NT_FORMAT_SHORT2N : NT_FORMAT_SHORT2) : (normalized ? NT_FORMAT_SHORT4N : NT_FORMAT_SHORT4);
    case NT_STREAM_UINT8:
        return normalized ? NT_FORMAT_UBYTE4N : NT_FORMAT_UBYTE4;
    case NT_STREAM_INT8:
        return NT_FORMAT_BYTE4N;
    case NT_STREAM_UINT16:
        /* GL_UNSIGNED_SHORT vertex attrs: uncommon, treat as short for now */
        return (count <= 2) ? NT_FORMAT_SHORT2 : NT_FORMAT_SHORT4;
    default:
        nt_log_error("mesh_renderer: unmapped stream type in vertex layout");
        return NT_FORMAT_FLOAT4;
    }
}

/* ---- Vertex layout offset computation helper ---- */

static uint16_t stream_byte_size(const NtStreamDesc *s) { return (uint16_t)(nt_stream_type_size(s->type) * s->count); }

/* ---- Pipeline cache lookup/create ---- */

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static nt_pipeline_t find_or_create_pipeline(const nt_material_info_t *mat_info, const nt_gfx_mesh_info_t *mesh_info) {

    /* Full pipeline signature: layout + shaders + render state.
     * Multiplicative hash combining to avoid collisions. */
    uint32_t state_bits = ((uint32_t)mat_info->blend_mode) | ((uint32_t)mat_info->depth_test << 4) |
                          ((uint32_t)mat_info->depth_write << 5) | ((uint32_t)mat_info->cull_mode << 6);
    uint64_t key = mesh_info->layout_hash;
    key = key * 0x9E3779B97F4A7C15ULL + mat_info->resolved_vs;
    key = key * 0x9E3779B97F4A7C15ULL + mat_info->resolved_fs;
    key = key * 0x9E3779B97F4A7C15ULL + state_bits;

    /* Linear scan for cached entry */
    for (uint16_t i = 0; i < s_mesh_renderer.count; i++) {
        if (s_mesh_renderer.entries[i].key == key) {
            return s_mesh_renderer.entries[i].pipeline;
        }
    }

    /* Build vertex layout from mesh streams + material attr_map */
    nt_vertex_layout_t layout;
    memset(&layout, 0, sizeof(layout));
    layout.stride = mesh_info->stride;

    uint16_t offset = 0;
    for (uint8_t si = 0; si < mesh_info->stream_count; si++) {
        const NtStreamDesc *stream = &mesh_info->streams[si];

        /* Find location in material attr_map */
        uint8_t location = 0;
        bool found = false;
        for (uint8_t ai = 0; ai < mat_info->attr_map_count; ai++) {
            if (mat_info->attr_map_hashes[ai] == stream->name_hash) {
                location = mat_info->attr_map_locations[ai];
                found = true;
                break;
            }
        }

        if (found) {
            NT_ASSERT(layout.attr_count < NT_GFX_MAX_VERTEX_ATTRS);
            if (layout.attr_count >= NT_GFX_MAX_VERTEX_ATTRS) {
                nt_log_error("mesh_renderer: vertex attr count exceeds max");
                break;
            }
            layout.attrs[layout.attr_count].location = location;
            layout.attrs[layout.attr_count].format = nt_stream_to_vertex_format(stream->type, stream->count, stream->normalized);
            layout.attrs[layout.attr_count].offset = offset;
            layout.attr_count++;
        }

        offset += stream_byte_size(stream);
    }

    /* Create pipeline descriptor */
    nt_pipeline_desc_t desc;
    memset(&desc, 0, sizeof(desc));
    desc.vertex_shader = (nt_shader_t){.id = mat_info->resolved_vs};
    desc.fragment_shader = (nt_shader_t){.id = mat_info->resolved_fs};
    desc.layout = layout;
    desc.instance_layout = s_instance_layout;
    desc.depth_test = mat_info->depth_test;
    desc.depth_write = mat_info->depth_write;
    desc.depth_func = NT_DEPTH_LESS;
    desc.blend = (mat_info->blend_mode == NT_BLEND_MODE_ALPHA);
    if (desc.blend) {
        desc.blend_src = NT_BLEND_SRC_ALPHA;
        desc.blend_dst = NT_BLEND_ONE_MINUS_SRC_ALPHA;
    }
    desc.cull_mode = (uint8_t)mat_info->cull_mode;
    desc.label = (mat_info->label != NULL) ? mat_info->label : "mesh_pipeline";

    nt_pipeline_t pip = nt_gfx_make_pipeline(&desc);

    /* Store in cache — full cache is a configuration bug, not a runtime recovery case */
    NT_ASSERT(s_mesh_renderer.count < s_mesh_renderer.max_pipelines);
    if (s_mesh_renderer.count < s_mesh_renderer.max_pipelines) {
        s_mesh_renderer.entries[s_mesh_renderer.count].key = key;
        s_mesh_renderer.entries[s_mesh_renderer.count].pipeline = pip;
        s_mesh_renderer.count++;
    } else {
        nt_log_error("mesh_renderer: pipeline cache full — increase max_pipelines in desc");
        nt_gfx_destroy_pipeline(pip);
        return (nt_pipeline_t){0};
    }

    return pip;
}

/* ---- Lifecycle ---- */

nt_result_t nt_mesh_renderer_init(const nt_mesh_renderer_desc_t *desc) {
    NT_ASSERT(!s_mesh_renderer.initialized);
    NT_ASSERT(desc);
    NT_ASSERT(desc->max_instances > 0);
    NT_ASSERT(desc->max_pipelines > 0);
    if (s_mesh_renderer.initialized || !desc || desc->max_instances == 0 || desc->max_pipelines == 0) {
        return NT_ERR_INIT_FAILED;
    }

    memset(&s_mesh_renderer, 0, sizeof(s_mesh_renderer));

    s_mesh_renderer.max_instances = desc->max_instances;
    s_mesh_renderer.max_pipelines = desc->max_pipelines;

    /* Allocate pipeline cache */
    s_mesh_renderer.entries = (nt_pipeline_cache_entry_t *)calloc(desc->max_pipelines, sizeof(nt_pipeline_cache_entry_t));
    if (!s_mesh_renderer.entries) {
        nt_log_error("mesh_renderer: failed to allocate pipeline cache");
        return NT_ERR_INIT_FAILED;
    }

    /* Allocate CPU staging array */
    s_mesh_renderer.instance_data = (nt_mesh_instance_t *)calloc(desc->max_instances, sizeof(nt_mesh_instance_t));
    if (!s_mesh_renderer.instance_data) {
        free(s_mesh_renderer.entries);
        s_mesh_renderer.entries = NULL;
        nt_log_error("mesh_renderer: failed to allocate instance data");
        return NT_ERR_INIT_FAILED;
    }

    /* Create instance buffer (STREAM: rewritten every frame) */
    s_mesh_renderer.instance_buf = nt_gfx_make_buffer(&(nt_buffer_desc_t){
        .type = NT_BUFFER_VERTEX,
        .usage = NT_USAGE_STREAM,
        .size = (uint32_t)desc->max_instances * (uint32_t)sizeof(nt_mesh_instance_t),
        .data = NULL,
        .label = "mesh_renderer_instance",
    });

    if (s_mesh_renderer.instance_buf.id == 0) {
        free(s_mesh_renderer.instance_data);
        s_mesh_renderer.instance_data = NULL;
        free(s_mesh_renderer.entries);
        s_mesh_renderer.entries = NULL;
        nt_log_error("mesh_renderer: failed to create instance buffer");
        return NT_ERR_INIT_FAILED;
    }

    s_mesh_renderer.initialized = true;
    return NT_OK;
}

void nt_mesh_renderer_shutdown(void) {
    if (!s_mesh_renderer.initialized) {
        return;
    }

    /* Destroy instance buffer */
    nt_gfx_destroy_buffer(s_mesh_renderer.instance_buf);

    /* Destroy all cached pipelines */
    for (uint16_t i = 0; i < s_mesh_renderer.count; i++) {
        nt_gfx_destroy_pipeline(s_mesh_renderer.entries[i].pipeline);
    }

    /* Free pipeline cache */
    free(s_mesh_renderer.entries);

    /* Free CPU staging array */
    free(s_mesh_renderer.instance_data);

    memset(&s_mesh_renderer, 0, sizeof(s_mesh_renderer));
}

void nt_mesh_renderer_restore_gpu(void) {
    uint16_t saved_max = s_mesh_renderer.max_instances;
    uint16_t saved_pip = s_mesh_renderer.max_pipelines;
    nt_mesh_renderer_shutdown();
    nt_mesh_renderer_desc_t desc = {.max_instances = saved_max, .max_pipelines = saved_pip};
    nt_mesh_renderer_init(&desc);
}

/* ---- Draw list ---- */

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void nt_mesh_renderer_draw_list(const nt_render_item_t *items, uint32_t count) {

    NT_ASSERT(s_mesh_renderer.initialized);
    if (!s_mesh_renderer.initialized || count == 0) {
        return;
    }

    /* Reset per-frame tracking */
    s_mesh_renderer.frame_draw_calls = 0;
    s_mesh_renderer.frame_instance_total = 0;

    /* Run detection loop */
    uint32_t run_start = 0;
    nt_material_t prev_mat = {0};
    nt_mesh_t prev_mesh = {0};

    while (run_start < count) {
        /* Reconstruct entity from item */
        nt_entity_t entity = {.id = items[run_start].entity};

        /* Get material and mesh handles from components */
        nt_material_t run_mat = *nt_material_comp_handle(entity);
        nt_mesh_t run_mesh = *nt_mesh_comp_handle(entity);

        /* Detect run end via batch_key (same material+mesh = same key, independent of sort order) */
        uint32_t run_end = run_start + 1;
        while (run_end < count && items[run_end].batch_key == items[run_start].batch_key) {
            run_end++;
        }

        uint32_t instance_count = run_end - run_start;

        /* Get material info and mesh info */
        const nt_material_info_t *mat_info = nt_material_get_info(run_mat);
        const nt_gfx_mesh_info_t *mesh_info = nt_gfx_get_mesh_info(run_mesh);

        if (!mat_info || !mat_info->ready || !mesh_info) {
            nt_log_error("mesh_renderer: skipping run — material or mesh not ready");
            run_start = run_end;
            continue;
        }

        /* Bind pipeline (if material or mesh changed) */
        if (run_mat.id != prev_mat.id || run_mesh.id != prev_mesh.id) {
            nt_pipeline_t pip = find_or_create_pipeline(mat_info, mesh_info);
            nt_gfx_bind_pipeline(pip);

            /* Bind textures and set sampler uniforms to correct texture units */
            for (uint8_t t = 0; t < mat_info->tex_count; t++) {
                if (mat_info->resolved_tex[t] != 0) {
                    nt_gfx_bind_texture((nt_texture_t){.id = mat_info->resolved_tex[t]}, t);
                    if (mat_info->tex_names[t] != NULL) {
                        nt_gfx_set_uniform_int(mat_info->tex_names[t], (int)t);
                    }
                }
            }

            /* Bind mesh VBO (+ IBO if indexed) */
            nt_gfx_bind_vertex_buffer(mesh_info->vbo);
            if (mesh_info->index_count > 0) {
                nt_gfx_bind_index_buffer(mesh_info->ibo);
            }

            prev_mat = run_mat;
            prev_mesh = run_mesh;
        }

        /* Pack instance data (split into sub-batches if exceeds max_instances) */
        uint32_t instances_remaining = instance_count;
        uint32_t batch_offset = 0;

        while (instances_remaining > 0) {
            uint32_t batch_count = instances_remaining;
            if (batch_count > s_mesh_renderer.max_instances) {
                batch_count = s_mesh_renderer.max_instances;
            }

            /* Pack world_matrix + color for this batch.
             * Reads from component arrays via entity→index lookup (scattered access).
             * Acceptable: ~20μs at 1K entities. Inline world_matrix in render item
             * would make packing sequential but sort 6× slower (96B vs 16B elements).
             * If CPU-bound at 10K+: switch to radix/indirect sort + fat item. */
            for (uint32_t i = 0; i < batch_count; i++) {
                nt_entity_t e = {.id = items[run_start + batch_offset + i].entity};
                const float *world = nt_transform_comp_world_matrix(e);
                const float *color = nt_drawable_comp_color(e);
                memcpy(s_mesh_renderer.instance_data[i].world_matrix, world, 64);
                memcpy(s_mesh_renderer.instance_data[i].color, color, 16);
            }

            /* Upload instance buffer */
            nt_gfx_update_buffer(s_mesh_renderer.instance_buf, s_mesh_renderer.instance_data, batch_count * (uint32_t)sizeof(nt_mesh_instance_t));
            nt_gfx_bind_instance_buffer(s_mesh_renderer.instance_buf);

            /* Draw (indexed or non-indexed) */
            if (mesh_info->index_count > 0) {
                nt_gfx_draw_indexed_instanced(0, mesh_info->index_count, mesh_info->vertex_count, batch_count);
            } else {
                nt_gfx_draw_instanced(0, mesh_info->vertex_count, batch_count);
            }

            s_mesh_renderer.frame_draw_calls++;
            s_mesh_renderer.frame_instance_total += batch_count;

            batch_offset += batch_count;
            instances_remaining -= batch_count;
        }

        run_start = run_end;
    }
}

/* ---- Test accessors (always compiled; header guard controls visibility) ---- */

uint32_t nt_mesh_renderer_test_pipeline_cache_count(void) { return s_mesh_renderer.count; }

uint32_t nt_mesh_renderer_test_draw_call_count(void) { return s_mesh_renderer.frame_draw_calls; }

uint32_t nt_mesh_renderer_test_instance_total(void) { return s_mesh_renderer.frame_instance_total; }
