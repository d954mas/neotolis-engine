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

    nt_buffer_t instance_buf; /* dynamic vertex buffer for instance data */

    uint8_t *instance_data; /* CPU staging byte buffer [max_instances * NT_INSTANCE_STRIDE_MAX] */
    uint16_t max_instances;

    /* Per-frame tracking for test accessors */
    uint32_t frame_draw_calls;
    uint32_t frame_instance_total;

    bool initialized;
} s_mesh_renderer;

/* ---- Instance layout per color mode (per D-15: locations 4-6 for mat4x3, 7 for color) ---- */

/* clang-format off */
static const struct {
    nt_vertex_layout_t layout;
    uint16_t stride;
} s_instance_layouts[3] = {
    [NT_COLOR_MODE_NONE] = {
        .layout = {
            .attr_count = 3,
            .stride = NT_INSTANCE_STRIDE_NONE,
            .attrs = {
                {.location = 4, .format = NT_FORMAT_FLOAT4, .offset = 0},
                {.location = 5, .format = NT_FORMAT_FLOAT4, .offset = 16},
                {.location = 6, .format = NT_FORMAT_FLOAT4, .offset = 32},
            },
        },
        .stride = NT_INSTANCE_STRIDE_NONE,
    },
    [NT_COLOR_MODE_RGBA8] = {
        .layout = {
            .attr_count = 4,
            .stride = NT_INSTANCE_STRIDE_RGBA8,
            .attrs = {
                {.location = 4, .format = NT_FORMAT_FLOAT4, .offset = 0},
                {.location = 5, .format = NT_FORMAT_FLOAT4, .offset = 16},
                {.location = 6, .format = NT_FORMAT_FLOAT4, .offset = 32},
                {.location = 7, .format = NT_FORMAT_UBYTE4N, .offset = 48},
            },
        },
        .stride = NT_INSTANCE_STRIDE_RGBA8,
    },
    [NT_COLOR_MODE_FLOAT4] = {
        .layout = {
            .attr_count = 4,
            .stride = NT_INSTANCE_STRIDE_FLOAT4,
            .attrs = {
                {.location = 4, .format = NT_FORMAT_FLOAT4, .offset = 0},
                {.location = 5, .format = NT_FORMAT_FLOAT4, .offset = 16},
                {.location = 6, .format = NT_FORMAT_FLOAT4, .offset = 32},
                {.location = 7, .format = NT_FORMAT_FLOAT4, .offset = 48},
            },
        },
        .stride = NT_INSTANCE_STRIDE_FLOAT4,
    },
};
/* clang-format on */

/* ---- Pack helpers ---- */

/* Pack mat4x3: extract 3 rows from column-major mat4 (cglm convention).
 * row0 = (m[0], m[4], m[8],  m[12]) -- first basis components + translation x
 * row1 = (m[1], m[5], m[9],  m[13]) -- second basis + translation y
 * row2 = (m[2], m[6], m[10], m[14]) -- third basis + translation z
 * Row 3 (0,0,0,1) is reconstructed in the vertex shader. */
static void pack_mat4x3(uint8_t *dst, const float *m) {
    float rows[12];
    rows[0] = m[0];
    rows[1] = m[4];
    rows[2] = m[8];
    rows[3] = m[12];
    rows[4] = m[1];
    rows[5] = m[5];
    rows[6] = m[9];
    rows[7] = m[13];
    rows[8] = m[2];
    rows[9] = m[6];
    rows[10] = m[10];
    rows[11] = m[14];
    memcpy(dst, rows, 48);
}

/* Float-to-uint8 with clamping and rounding (same pattern as shape renderer) */
static inline uint8_t float_to_u8(float v) {
    if (v <= 0.0F) {
        return 0;
    }
    if (v >= 1.0F) {
        return 255;
    }
    return (uint8_t)((v * 255.0F) + 0.5F);
}

/* Pack float[4] color to RGBA8 (4 bytes) */
static void pack_rgba8(uint8_t *dst, const float color[4]) {
    dst[0] = float_to_u8(color[0]);
    dst[1] = float_to_u8(color[1]);
    dst[2] = float_to_u8(color[2]);
    dst[3] = float_to_u8(color[3]);
}

/* ---- Stream type to vertex format mapping ---- */

/* Map mesh stream type to GL vertex format.
 * FLOAT32/FLOAT16: exact count 1-4 via lookup table.
 * INT16/UINT16: count 2 or 4, with optional normalization.
 * UINT8/INT8: count must be 4 (vertex colors, bone indices, packed normals).
 * If new byte counts needed, extend nt_vertex_format_t or switch to raw (type, count, normalized). */

/* clang-format off */
static const nt_vertex_format_t s_float32_formats[4] = {NT_FORMAT_FLOAT, NT_FORMAT_FLOAT2, NT_FORMAT_FLOAT3, NT_FORMAT_FLOAT4};
static const nt_vertex_format_t s_float16_formats[4] = {NT_FORMAT_HALF,  NT_FORMAT_HALF2,  NT_FORMAT_HALF3,  NT_FORMAT_HALF4};
/* clang-format on */

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
nt_vertex_format_t nt_stream_to_vertex_format(uint8_t type, uint8_t count, uint8_t normalized) {
    uint8_t idx = (count >= 1 && count <= 4) ? (uint8_t)(count - 1) : 3;
    switch (type) {
    case NT_STREAM_FLOAT32:
        return s_float32_formats[idx];
    case NT_STREAM_FLOAT16:
        return s_float16_formats[idx];
    case NT_STREAM_INT16:
        if (count <= 2) {
            return normalized ? NT_FORMAT_SHORT2N : NT_FORMAT_SHORT2;
        }
        return normalized ? NT_FORMAT_SHORT4N : NT_FORMAT_SHORT4;
    case NT_STREAM_UINT8:
        NT_ASSERT(count == 4);
        return normalized ? NT_FORMAT_UBYTE4N : NT_FORMAT_UBYTE4;
    case NT_STREAM_INT8:
        NT_ASSERT(count == 4);
        return NT_FORMAT_BYTE4N;
    case NT_STREAM_UINT16:
        /* GL_UNSIGNED_SHORT vertex attrs: uncommon, treat as short for now */
        return (count <= 2) ? NT_FORMAT_SHORT2 : NT_FORMAT_SHORT4;
    default:
        NT_LOG_ERROR("unmapped stream type in vertex layout");
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
    uint32_t state_bits = ((uint32_t)mat_info->blend_mode) | ((uint32_t)mat_info->depth_test << 4) | ((uint32_t)mat_info->depth_write << 5) | ((uint32_t)mat_info->cull_mode << 6) |
                          ((uint32_t)mat_info->color_mode << 8);
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
                NT_LOG_ERROR("vertex attr count exceeds max");
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
    desc.instance_layout = s_instance_layouts[mat_info->color_mode].layout;
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

    /* Store in cache -- full cache is a configuration bug, not a runtime recovery case */
    NT_ASSERT(s_mesh_renderer.count < s_mesh_renderer.max_pipelines);
    if (s_mesh_renderer.count < s_mesh_renderer.max_pipelines) {
        s_mesh_renderer.entries[s_mesh_renderer.count].key = key;
        s_mesh_renderer.entries[s_mesh_renderer.count].pipeline = pip;
        s_mesh_renderer.count++;
    } else {
        NT_LOG_ERROR("pipeline cache full -- increase max_pipelines in desc");
        nt_gfx_destroy_pipeline(pip);
        return (nt_pipeline_t){0};
    }

    return pip;
}

/* ---- Lifecycle ---- */

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
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
        NT_LOG_ERROR("failed to allocate pipeline cache");
        return NT_ERR_INIT_FAILED;
    }

    /* Allocate CPU staging byte buffer (worst-case stride) */
    s_mesh_renderer.instance_data = (uint8_t *)calloc(desc->max_instances, NT_INSTANCE_STRIDE_MAX);
    if (!s_mesh_renderer.instance_data) {
        free(s_mesh_renderer.entries);
        s_mesh_renderer.entries = NULL;
        NT_LOG_ERROR("failed to allocate instance data");
        return NT_ERR_INIT_FAILED;
    }

    /* Create instance buffer (STREAM: rewritten every frame) */
    s_mesh_renderer.instance_buf = nt_gfx_make_buffer(&(nt_buffer_desc_t){
        .type = NT_BUFFER_VERTEX,
        .usage = NT_USAGE_STREAM,
        .size = (uint32_t)desc->max_instances * (uint32_t)NT_INSTANCE_STRIDE_MAX,
        .data = NULL,
        .label = "mesh_renderer_instance",
    });

    if (s_mesh_renderer.instance_buf.id == 0) {
        free(s_mesh_renderer.instance_data);
        s_mesh_renderer.instance_data = NULL;
        free(s_mesh_renderer.entries);
        s_mesh_renderer.entries = NULL;
        NT_LOG_ERROR("failed to create instance buffer");
        return NT_ERR_INIT_FAILED;
    }

    /* Set generic attribute 7 to white -- when color attribute is disabled
     * (NONE mode), shaders read (1,1,1,1) as identity for multiplication.
     * Must be called after GL context exists. */
    nt_gfx_set_vertex_attrib_default(7, 1.0F, 1.0F, 1.0F, 1.0F);

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

    /* Free CPU staging byte buffer */
    free(s_mesh_renderer.instance_data);

    memset(&s_mesh_renderer, 0, sizeof(s_mesh_renderer));
}

void nt_mesh_renderer_restore_gpu(void) {
    uint16_t saved_max = s_mesh_renderer.max_instances;
    uint16_t saved_pip = s_mesh_renderer.max_pipelines;
    nt_mesh_renderer_shutdown();
    nt_mesh_renderer_desc_t desc = {.max_instances = saved_max, .max_pipelines = saved_pip};
    nt_result_t res = nt_mesh_renderer_init(&desc);
    NT_ASSERT(res == NT_OK); /* context alive but GPU alloc failed = fatal */
    (void)res;
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

    /* Process items in chunks of max_instances.
     * Each chunk: pack instance data -> upload -> draw with offsets.
     * Typically 1 chunk (items < max_instances). */
    uint32_t chunk_start = 0;
    nt_material_t prev_mat = {0};
    nt_mesh_t prev_mesh = {0};

    while (chunk_start < count) {
        uint32_t chunk_count = count - chunk_start;
        if (chunk_count > s_mesh_renderer.max_instances) {
            chunk_count = s_mesh_renderer.max_instances;
        }
        uint32_t chunk_end = chunk_start + chunk_count;

        /* ---- Pack all instances in this chunk into byte buffer ---- */
        /* First pass: pack at variable stride per draw group */
        uint32_t byte_offset = 0;
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
            nt_color_mode_t color_mode = (mat_info != NULL) ? mat_info->color_mode : NT_COLOR_MODE_NONE;
            uint16_t stride = s_instance_layouts[color_mode].stride;

            for (uint32_t i = scan; i < run_end; i++) {
                nt_entity_t e = {.id = items[i].entity};
                uint8_t *dst = s_mesh_renderer.instance_data + byte_offset;

                const float *world = nt_transform_comp_world_matrix(e);
                pack_mat4x3(dst, world);

                if (color_mode == NT_COLOR_MODE_RGBA8) {
                    const float *color = nt_drawable_comp_color(e);
                    pack_rgba8(dst + 48, color);
                } else if (color_mode == NT_COLOR_MODE_FLOAT4) {
                    const float *color = nt_drawable_comp_color(e);
                    memcpy(dst + 48, color, 16);
                }
                /* NONE: nothing after the 48 bytes */

                byte_offset += stride;
            }
            scan = run_end;
        }

        /* ---- Single GPU upload for packed byte data ---- */
        nt_gfx_update_buffer(s_mesh_renderer.instance_buf, s_mesh_renderer.instance_data, byte_offset);
        nt_gfx_bind_instance_buffer(s_mesh_renderer.instance_buf);

        /* ---- Draw runs within this chunk ---- */
        uint32_t run_start = chunk_start;
        uint32_t draw_byte_offset = 0;

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

            NT_ASSERT(mat_info && mat_info->ready && mesh_info); /* caller must filter not-ready items */
            if (!mat_info || !mat_info->ready || !mesh_info) {
                /* Still need to advance byte offset for skipped runs */
                nt_color_mode_t cm = (mat_info != NULL) ? mat_info->color_mode : NT_COLOR_MODE_NONE;
                draw_byte_offset += instance_count * s_instance_layouts[cm].stride;
                run_start = run_end;
                continue;
            }

            /* Bind pipeline (if material or mesh changed) */
            if (run_mat.id != prev_mat.id || run_mesh.id != prev_mesh.id) {
                nt_pipeline_t pip = find_or_create_pipeline(mat_info, mesh_info);
                nt_gfx_bind_pipeline(pip);

                for (uint8_t t = 0; t < mat_info->tex_count; t++) {
                    if (mat_info->resolved_tex[t] != 0) {
                        nt_gfx_bind_texture((nt_texture_t){.id = mat_info->resolved_tex[t]}, t);
                        if (mat_info->tex_names[t] != NULL) {
                            nt_gfx_set_uniform_int(mat_info->tex_names[t], (int)t);
                        }
                    }
                }

                /* Apply material params as uniforms */
                for (uint8_t p = 0; p < mat_info->param_count; p++) {
                    if (mat_info->param_names[p] != NULL) {
                        nt_gfx_set_uniform_vec4(mat_info->param_names[p], mat_info->params[p]);
                    }
                }

                nt_gfx_bind_vertex_buffer(mesh_info->vbo);
                if (mesh_info->index_count > 0) {
                    nt_gfx_bind_index_buffer(mesh_info->ibo);
                }

                prev_mat = run_mat;
                prev_mesh = run_mesh;
            }

            nt_gfx_set_instance_offset(draw_byte_offset);

            if (mesh_info->index_count > 0) {
                nt_gfx_draw_indexed_instanced(0, mesh_info->index_count, mesh_info->vertex_count, instance_count);
            } else {
                nt_gfx_draw_instanced(0, mesh_info->vertex_count, instance_count);
            }

            s_mesh_renderer.frame_draw_calls++;
            s_mesh_renderer.frame_instance_total += instance_count;

            draw_byte_offset += instance_count * s_instance_layouts[mat_info->color_mode].stride;
            run_start = run_end;
        }

        chunk_start = chunk_end;
    }
}

/* ---- Test accessors (always compiled; header guard controls visibility) ---- */

uint32_t nt_mesh_renderer_test_pipeline_cache_count(void) { return s_mesh_renderer.count; }

uint32_t nt_mesh_renderer_test_draw_call_count(void) { return s_mesh_renderer.frame_draw_calls; }

uint32_t nt_mesh_renderer_test_instance_total(void) { return s_mesh_renderer.frame_instance_total; }
