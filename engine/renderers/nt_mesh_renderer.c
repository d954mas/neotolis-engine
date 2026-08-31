#include "renderers/nt_mesh_renderer.h"

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

typedef struct {
    uint64_t key;
    nt_vertex_input_t vi;
} nt_mesh_vi_version_t;

static struct {
    nt_renderer_pipeline_entry_t *entries; /* [max_pipelines] */
    uint16_t max_pipelines;
    uint16_t count;

    nt_mesh_vi_version_t *vi_versions; /* [nt_gfx_max_meshes()][max_mesh_layouts] */
    /* Row ownership includes the mesh generation. */
    nt_mesh_t *vi_meshes;
    uint16_t max_mesh_layouts;
    uint16_t vi_mesh_capacity; /* nt_gfx_max_meshes() at init time */

    nt_buffer_t instance_buf; /* dynamic vertex buffer for instance data */

    uint8_t *instance_data; /* CPU staging byte buffer [max_instances * NT_INSTANCE_STRIDE_MAX] */
    uint16_t max_instances;
    uint32_t ring_cursor; /* next free byte in instance_buf; disjoint writes avoid driver copies of in-flight data */

    /* One-shot so a load-time skip does not spam; re-armed when a pipeline is
     * built, i.e. when something became drawable again. */
    bool warned_program_not_ready;

    /* Per-frame tracking for test accessors */
    uint32_t frame_draw_calls;
    uint32_t frame_instance_total;

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

/* ---- Pack helpers ---- */

/* Pack mat4x3: extract 3 rows from column-major mat4 (cglm convention).
 * row0 = (m[0], m[4], m[8],  m[12]) -- first basis components + translation x
 * row1 = (m[1], m[5], m[9],  m[13]) -- second basis + translation y
 * row2 = (m[2], m[6], m[10], m[14]) -- third basis + translation z
 * Row 3 (0,0,0,1) is reconstructed in the vertex shader.
 * Transpose: column-major cols → row-major rows with stride-4 gather. */
static void pack_mat4x3(float *dst, const float *m) {
    /* row 0 */
    dst[0] = m[0];
    dst[1] = m[4];
    dst[2] = m[8];
    dst[3] = m[12];
    /* row 1 */
    dst[4] = m[1];
    dst[5] = m[5];
    dst[6] = m[9];
    dst[7] = m[13];
    /* row 2 */
    dst[8] = m[2];
    dst[9] = m[6];
    dst[10] = m[10];
    dst[11] = m[14];
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

/* Pack stream types and gfx vertex types are distinct enums on purpose: the
 * pack's on-disk format must not leak into the gfx API. Total mapping --
 * count and normalized pass through the attribute unchanged. */
nt_vertex_type_t nt_stream_to_vertex_type(uint8_t type) {
    switch (type) {
    case NT_STREAM_UINT8:
        return NT_VERTEX_UINT8;
    case NT_STREAM_INT8:
        return NT_VERTEX_INT8;
    case NT_STREAM_UINT16:
        return NT_VERTEX_UINT16;
    case NT_STREAM_INT16:
        return NT_VERTEX_INT16;
    case NT_STREAM_FLOAT16:
        return NT_VERTEX_HALF;
    case NT_STREAM_FLOAT32:
        return NT_VERTEX_FLOAT;
    default:
        NT_LOG_ERROR("unmapped stream type in vertex layout");
        return NT_VERTEX_FLOAT;
    }
}

/* ---- Vertex layout offset computation helper ---- */

static uint16_t stream_byte_size(const NtStreamDesc *s) { return (uint16_t)(nt_stream_type_size(s->type) * s->count); }

/* ---- Pipeline cache lookup/create ---- */

static nt_pipeline_t find_or_create_pipeline(const nt_material_info_t *mat_info) {
    /* Sprite and text gate on readiness here; this renderer gates in draw_list, so
     * state the requirement where the pipeline is actually built. */
    NT_ASSERT(nt_gfx_program_ready(mat_info->program) && "find_or_create_pipeline: caller must gate on nt_gfx_program_ready");

    /* Layouts live on the vertex-input versions; the pipeline signature is
     * program x render state (the text-renderer pattern). render_state_hash
     * folds color_mode, so pipelines still split per color mode -- an accepted
     * over-split: nothing in the slimmed pipeline depends on it. */
    const uint64_t key = ((uint64_t)mat_info->program.id * 0x9E3779B97F4A7C15ULL) + mat_info->render_state_hash;

    const nt_pipeline_t cached = nt_renderer_pipeline_cache_find(s_mesh_renderer.entries, s_mesh_renderer.count, key);
    if (cached.id != 0) {
        return cached;
    }

    nt_pipeline_desc_t desc;
    memset(&desc, 0, sizeof(desc));
    desc.program = mat_info->program;
    desc.depth_test = mat_info->depth_test;
    desc.depth_write = mat_info->depth_write;
    desc.depth_func = NT_DEPTH_LESS;
    desc.blend = mat_info->blend;
    desc.cull_mode = (uint8_t)mat_info->cull_mode;
    desc.label = (mat_info->label != NULL) ? mat_info->label : "mesh_pipeline";

    return nt_renderer_pipeline_cache_insert(s_mesh_renderer.entries, &s_mesh_renderer.count, s_mesh_renderer.max_pipelines, key, &desc, &s_mesh_renderer.warned_program_not_ready);
}

/* ---- Vertex-input versions (Godot-style, one row per mesh pool slot) ---- */

/* Derived layout = mesh streams x material attr_map: only mapped streams
 * become attributes, so materials whose extra attr_map entries match none of
 * this mesh's streams share a vertex input. */
static nt_vertex_layout_t build_mesh_vertex_layout(const nt_material_info_t *mat_info, const nt_gfx_mesh_info_t *mesh_info) {
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
            layout.attrs[layout.attr_count].location = location;
            layout.attrs[layout.attr_count].type = nt_stream_to_vertex_type(stream->type);
            layout.attrs[layout.attr_count].count = stream->count;
            layout.attrs[layout.attr_count].normalized = stream->normalized != 0;
            layout.attrs[layout.attr_count].offset = offset;
            layout.attr_count++;
        }

        offset += stream_byte_size(stream);
    }
    return layout;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity) -- NT_ASSERT expansion inflates the metric
static nt_vertex_input_t find_or_create_vertex_input(nt_mesh_t mesh, const nt_material_info_t *mat_info, const nt_gfx_mesh_info_t *mesh_info) {
    const nt_vertex_layout_t layout = build_mesh_vertex_layout(mat_info, mesh_info);
    /* Hash only the used attrs (memset'd, so padding is deterministic; the
     * unused tail would be pure noise). color_mode selects the instance
     * layout, which the baked divisor state depends on. */
    uint64_t key = nt_hash64(layout.attrs, (uint32_t)layout.attr_count * (uint32_t)sizeof(nt_vertex_attr_t)).value;
    key = key * 0x9E3779B97F4A7C15ULL + ((uint64_t)layout.attr_count << 24 | (uint64_t)layout.stride << 8 | (uint64_t)mat_info->color_mode);

    const uint32_t slot = nt_pool_slot_index(mesh.id);
    NT_ASSERT(slot != 0 && slot <= s_mesh_renderer.vi_mesh_capacity);
    nt_mesh_vi_version_t *row = &s_mesh_renderer.vi_versions[(size_t)(slot - 1) * s_mesh_renderer.max_mesh_layouts];
    if (s_mesh_renderer.vi_meshes[slot - 1].id != mesh.id) {
        /* Bufferless vertex inputs have no buffer-destroy cascade hook. */
        for (uint16_t i = 0; i < s_mesh_renderer.max_mesh_layouts; i++) {
            nt_gfx_destroy_vertex_input(row[i].vi);
            row[i] = (nt_mesh_vi_version_t){0};
        }
        s_mesh_renderer.vi_meshes[slot - 1] = mesh;
    }

    nt_mesh_vi_version_t *reusable = NULL;
    for (uint16_t i = 0; i < s_mesh_renderer.max_mesh_layouts; i++) {
        nt_mesh_vi_version_t *e = &row[i];
        const bool live = nt_gfx_vertex_input_valid(e->vi);
        if (e->key == key && live) {
            return e->vi;
        }
        if (reusable == NULL && !live) {
            reusable = e;
        }
    }
    /* Crash-early over silent eviction: 5+ layouts cycling on one mesh would
     * re-create VAOs every frame as an invisible perf regression. */
    NT_ASSERT(reusable != NULL && "mesh vertex-input versions exhausted -- raise nt_mesh_renderer_desc_t.max_mesh_layouts");
    if (reusable == NULL) {
        return NT_VERTEX_INPUT_INVALID;
    }

    const nt_vertex_input_t vi = nt_gfx_make_vertex_input(&(nt_vertex_input_desc_t){
        .layout = layout,
        .instance_layout = s_instance_layouts[mat_info->color_mode],
        /* A material mapping none of this mesh's streams derives an empty
         * layout -- the attribute-less gl_VertexID path takes no buffer. */
        .vertex_buffer = (layout.attr_count > 0) ? mesh_info->vbo : (nt_buffer_t){0},
        .index_buffer = mesh_info->ibo,
        .label = "mesh_vi",
    });
    if (vi.id == 0) {
        return vi; /* lost context / backend failure: caller skips the run */
    }
    reusable->key = key;
    reusable->vi = vi;
    return vi;
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
    /* NONE color mode reads the generic attribute instead of a buffer. */
    nt_gfx_set_vertex_attrib_default(7, 1.0F, 1.0F, 1.0F, 1.0F);
    return NT_OK;
}

static void destroy_gpu_resources(void) {
    nt_gfx_destroy_buffer(s_mesh_renderer.instance_buf);
    s_mesh_renderer.instance_buf = (nt_buffer_t){0};
    for (uint16_t i = 0; i < s_mesh_renderer.count; i++) {
        nt_gfx_destroy_pipeline(s_mesh_renderer.entries[i].pipeline);
    }
    s_mesh_renderer.count = 0;
    for (size_t i = 0; i < (size_t)s_mesh_renderer.vi_mesh_capacity * s_mesh_renderer.max_mesh_layouts; i++) {
        nt_gfx_destroy_vertex_input(s_mesh_renderer.vi_versions[i].vi);
        s_mesh_renderer.vi_versions[i] = (nt_mesh_vi_version_t){0};
    }
    memset(s_mesh_renderer.vi_meshes, 0, (size_t)s_mesh_renderer.vi_mesh_capacity * sizeof(nt_mesh_t));
    s_mesh_renderer.ring_cursor = 0;
    s_mesh_renderer.frame_draw_calls = 0;
    s_mesh_renderer.frame_instance_total = 0;
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
    s_mesh_renderer.max_mesh_layouts = desc->max_mesh_layouts;
    /* The capacity lives only in nt_gfx_desc_t; hardcoding it here would write
     * out of bounds on bigger gfx configs. */
    s_mesh_renderer.vi_mesh_capacity = nt_gfx_max_meshes();
    NT_ASSERT(s_mesh_renderer.vi_mesh_capacity > 0 && "nt_mesh_renderer_init: nt_gfx_init must run first");

    /* Allocate pipeline cache */
    s_mesh_renderer.entries = (nt_renderer_pipeline_entry_t *)calloc(desc->max_pipelines, sizeof(nt_renderer_pipeline_entry_t));
    if (!s_mesh_renderer.entries) {
        NT_LOG_ERROR("failed to allocate pipeline cache");
        return NT_ERR_INIT_FAILED;
    }

    /* Vertex-input versions table: one row per mesh pool slot */
    s_mesh_renderer.vi_versions = (nt_mesh_vi_version_t *)calloc((size_t)s_mesh_renderer.vi_mesh_capacity * desc->max_mesh_layouts, sizeof(nt_mesh_vi_version_t));
    s_mesh_renderer.vi_meshes = (nt_mesh_t *)calloc(s_mesh_renderer.vi_mesh_capacity, sizeof(nt_mesh_t));
    if (!s_mesh_renderer.vi_versions || !s_mesh_renderer.vi_meshes) {
        free(s_mesh_renderer.vi_meshes);
        s_mesh_renderer.vi_meshes = NULL;
        free(s_mesh_renderer.vi_versions);
        s_mesh_renderer.vi_versions = NULL;
        free(s_mesh_renderer.entries);
        s_mesh_renderer.entries = NULL;
        NT_LOG_ERROR("failed to allocate vertex-input versions");
        return NT_ERR_INIT_FAILED;
    }

    /* Allocate CPU staging byte buffer (worst-case stride) */
    s_mesh_renderer.instance_data = (uint8_t *)calloc(desc->max_instances, NT_INSTANCE_STRIDE_MAX);
    if (!s_mesh_renderer.instance_data) {
        free(s_mesh_renderer.vi_meshes);
        s_mesh_renderer.vi_meshes = NULL;
        free(s_mesh_renderer.vi_versions);
        s_mesh_renderer.vi_versions = NULL;
        free(s_mesh_renderer.entries);
        s_mesh_renderer.entries = NULL;
        NT_LOG_ERROR("failed to allocate instance data");
        return NT_ERR_INIT_FAILED;
    }

    if (create_gpu_resources() != NT_OK) {
        free(s_mesh_renderer.instance_data);
        s_mesh_renderer.instance_data = NULL;
        free(s_mesh_renderer.vi_meshes);
        s_mesh_renderer.vi_meshes = NULL;
        free(s_mesh_renderer.vi_versions);
        s_mesh_renderer.vi_versions = NULL;
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
    free(s_mesh_renderer.vi_versions);
    free(s_mesh_renderer.vi_meshes);

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

    /* Reset per-frame tracking */
    s_mesh_renderer.frame_draw_calls = 0;
    s_mesh_renderer.frame_instance_total = 0;

    /* Restore generic attribute 7 to white once per draw_list call.
     * NONE mode shaders read this as identity color. Protects against
     * other renderers or user code changing the value between frames. */
    nt_gfx_set_vertex_attrib_default(7, 1.0F, 1.0F, 1.0F, 1.0F);

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
            nt_color_mode_t color_mode = (mat_info != NULL) ? mat_info->color_mode : NT_COLOR_MODE_NONE;
            NT_ASSERT(color_mode <= NT_COLOR_MODE_FLOAT4); /* corrupted material = programmer error */
            uint16_t stride = s_instance_layouts[color_mode].stride;

            for (uint32_t i = scan; i < run_end; i++) {
                nt_entity_t e = {.id = items[i].entity};
                uint8_t *dst = s_mesh_renderer.instance_data + packed_size;

                const float *world = nt_transform_comp_world_matrix(e);
                pack_mat4x3((float *)dst, world);

                if (color_mode == NT_COLOR_MODE_RGBA8) {
                    const float *color = nt_drawable_comp_color(e);
                    pack_rgba8(dst + 48, color);
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

            /* Bind pipeline + vertex input (if material or mesh changed) */
            if (run_mat.id != prev_mat.id || run_mesh.id != prev_mesh.id) {
                nt_pipeline_t pip = find_or_create_pipeline(mat_info);
                nt_vertex_input_t vi = (pip.id != 0) ? find_or_create_vertex_input(run_mesh, mat_info, mesh_info) : NT_VERTEX_INPUT_INVALID;
                if (pip.id == 0 || vi.id == 0) {
                    draw_byte_offset += instance_count * s_instance_layouts[mat_info->color_mode].stride;
                    run_start = run_end;
                    continue;
                }
                nt_gfx_bind_pipeline(pip);
                nt_gfx_bind_vertex_input(vi);

                /* Sampler units are program state shared with every other
                 * material on this program, so each declared slot is written
                 * whether or not its texture resolved. */
                for (uint8_t t = 0; t < mat_info->tex_count; t++) {
                    if (mat_info->tex_names[t] != NULL) {
                        nt_gfx_set_uniform_int(mat_info->tex_names[t], (int)t);
                    }
                    if (mat_info->resolved_tex[t] != 0) {
                        nt_gfx_bind_texture((nt_texture_t){.id = mat_info->resolved_tex[t]}, t);
                        if (mat_info->resolved_sampler[t].id != 0) {
                            nt_gfx_bind_sampler(mat_info->resolved_sampler[t], t);
                        }
                    }
                }

                /* Apply material params as uniforms */
                for (uint8_t p = 0; p < mat_info->param_count; p++) {
                    if (mat_info->param_names[p] != NULL) {
                        nt_gfx_set_uniform_vec4(mat_info->param_names[p], mat_info->params[p]);
                    }
                }

                prev_mat = run_mat;
                prev_mesh = run_mesh;
            }

            nt_gfx_bind_instance_buffer(s_mesh_renderer.instance_buf, draw_byte_offset);

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

uint32_t nt_mesh_renderer_test_vertex_input_count(void) {
    uint32_t live = 0;
    for (size_t i = 0; i < (size_t)s_mesh_renderer.vi_mesh_capacity * s_mesh_renderer.max_mesh_layouts; i++) {
        if (nt_gfx_vertex_input_valid(s_mesh_renderer.vi_versions[i].vi)) {
            live++;
        }
    }
    return live;
}

uint32_t nt_mesh_renderer_test_draw_call_count(void) { return s_mesh_renderer.frame_draw_calls; }

uint32_t nt_mesh_renderer_test_instance_total(void) { return s_mesh_renderer.frame_instance_total; }

uint32_t nt_mesh_renderer_test_ring_cursor(void) { return s_mesh_renderer.ring_cursor; }

bool nt_mesh_renderer_test_initialized(void) { return s_mesh_renderer.initialized; }
