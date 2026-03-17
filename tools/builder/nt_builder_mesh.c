/* clang-format off */
#include "nt_builder_internal.h"
#include "nt_mesh_format.h"
#include "cgltf.h"
/* clang-format on */

#include <math.h>

/* --- Type conversion helpers --- */

static float nt_clampf(float v, float lo, float hi) {
    if (v < lo) {
        return lo;
    }
    if (v > hi) {
        return hi;
    }
    return v;
}

static void nt_convert_component(float value, nt_stream_type_t type, bool normalized, uint8_t *out_ptr, bool *warned_f16) {
    switch (type) {
    case NT_STREAM_FLOAT32: {
        memcpy(out_ptr, &value, sizeof(float));
        break;
    }
    case NT_STREAM_FLOAT16: {
        if (!*warned_f16 && fabsf(value) > 65504.0F) {
            (void)fprintf(stderr, "WARNING: float16 overflow (value=%.6g exceeds +-65504)\n", (double)value);
            *warned_f16 = true;
        }
        uint16_t h = nt_builder_float32_to_float16(value);
        memcpy(out_ptr, &h, sizeof(uint16_t));
        break;
    }
    case NT_STREAM_UINT8: {
        if (normalized) {
            float c = nt_clampf(value, 0.0F, 1.0F);
            *out_ptr = (uint8_t)((c * 255.0F) + 0.5F);
        } else {
            *out_ptr = (uint8_t)nt_clampf(value + 0.5F, 0.0F, 255.0F);
        }
        break;
    }
    case NT_STREAM_INT8: {
        if (normalized) {
            float c = nt_clampf(value, -1.0F, 1.0F);
            float bias = (c >= 0.0F) ? 0.5F : -0.5F;
            int8_t s = (int8_t)((c * 127.0F) + bias);
            memcpy(out_ptr, &s, sizeof(int8_t));
        } else {
            int8_t s = (int8_t)nt_clampf(value + ((value >= 0.0F) ? 0.5F : -0.5F), -128.0F, 127.0F);
            memcpy(out_ptr, &s, sizeof(int8_t));
        }
        break;
    }
    case NT_STREAM_UINT16: {
        if (normalized) {
            float c = nt_clampf(value, 0.0F, 1.0F);
            uint16_t u = (uint16_t)((c * 65535.0F) + 0.5F);
            memcpy(out_ptr, &u, sizeof(uint16_t));
        } else {
            uint16_t u = (uint16_t)nt_clampf(value + 0.5F, 0.0F, 65535.0F);
            memcpy(out_ptr, &u, sizeof(uint16_t));
        }
        break;
    }
    case NT_STREAM_INT16: {
        if (normalized) {
            float c = nt_clampf(value, -1.0F, 1.0F);
            float bias = (c >= 0.0F) ? 0.5F : -0.5F;
            int16_t s = (int16_t)((c * 32767.0F) + bias);
            memcpy(out_ptr, &s, sizeof(int16_t));
        } else {
            int16_t s = (int16_t)nt_clampf(value + ((value >= 0.0F) ? 0.5F : -0.5F), -32768.0F, 32767.0F);
            memcpy(out_ptr, &s, sizeof(int16_t));
        }
        break;
    }
    }
}

/* --- Stream layout validation --- */

static nt_build_result_t nt_validate_stream_layout(const char *path, const NtStreamLayout *layout, uint32_t stream_count) {
    if (stream_count == 0 || stream_count > NT_MESH_MAX_STREAMS) {
        (void)fprintf(stderr, "ERROR: %s: stream_count %u out of range [1, %d]\n", path, stream_count, NT_MESH_MAX_STREAMS);
        return NT_BUILD_ERR_VALIDATION;
    }

    bool has_position = false;
    for (uint32_t s = 0; s < stream_count; s++) {
        if (layout[s].count < 1 || layout[s].count > 4) {
            (void)fprintf(stderr, "ERROR: %s: stream[%u] count %u out of range [1, 4]\n", path, s, layout[s].count);
            return NT_BUILD_ERR_VALIDATION;
        }
        if (layout[s].normalized && (layout[s].type == NT_STREAM_FLOAT32 || layout[s].type == NT_STREAM_FLOAT16)) {
            (void)fprintf(stderr, "ERROR: %s: stream[%u] '%s': normalized=true is invalid for float types\n", path, s, layout[s].engine_name ? layout[s].engine_name : "(null)");
            return NT_BUILD_ERR_VALIDATION;
        }
        if (layout[s].gltf_name != NULL && strcmp(layout[s].gltf_name, "POSITION") == 0) {
            has_position = true;
        }
    }
    if (!has_position) {
        (void)fprintf(stderr, "ERROR: %s: stream layout missing required POSITION attribute\n", path);
        return NT_BUILD_ERR_VALIDATION;
    }
    return NT_BUILD_OK;
}

/* --- glTF parsing --- */

static nt_build_result_t nt_parse_gltf(const char *path, cgltf_data **out_data, cgltf_primitive **out_prim) {
    cgltf_options options;
    memset(&options, 0, sizeof(options));

    cgltf_result result = cgltf_parse_file(&options, path, out_data);
    if (result != cgltf_result_success) {
        (void)fprintf(stderr, "ERROR: %s: failed to parse glTF (cgltf error %d)\n", path, (int)result);
        return NT_BUILD_ERR_FORMAT;
    }

    result = cgltf_load_buffers(&options, *out_data, path);
    if (result != cgltf_result_success) {
        (void)fprintf(stderr, "ERROR: %s: failed to load glTF buffers (cgltf error %d)\n", path, (int)result);
        cgltf_free(*out_data);
        *out_data = NULL;
        return NT_BUILD_ERR_IO;
    }

    result = cgltf_validate(*out_data);
    if (result != cgltf_result_success) {
        (void)fprintf(stderr, "WARNING: %s: glTF validation issues (cgltf error %d)\n", path, (int)result);
    }

    if ((*out_data)->meshes_count != 1) {
        (void)fprintf(stderr, "ERROR: %s: expected 1 mesh, found %zu\n", path, (*out_data)->meshes_count);
        cgltf_free(*out_data);
        *out_data = NULL;
        return NT_BUILD_ERR_VALIDATION;
    }
    if ((*out_data)->meshes[0].primitives_count != 1) {
        (void)fprintf(stderr, "ERROR: %s: expected 1 primitive, found %zu\n", path, (*out_data)->meshes[0].primitives_count);
        cgltf_free(*out_data);
        *out_data = NULL;
        return NT_BUILD_ERR_VALIDATION;
    }

    *out_prim = &(*out_data)->meshes[0].primitives[0];

    if ((*out_prim)->type != cgltf_primitive_type_triangles) {
        (void)fprintf(stderr, "ERROR: %s: primitive type %d is not TRIANGLES (only triangles supported)\n", path, (int)(*out_prim)->type);
        cgltf_free(*out_data);
        *out_data = NULL;
        return NT_BUILD_ERR_VALIDATION;
    }

    return NT_BUILD_OK;
}

/* --- Vertex attribute extraction --- */

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static nt_build_result_t nt_extract_vertex_streams(const char *path, const cgltf_primitive *prim, const NtStreamLayout *layout, uint32_t stream_count, float *stream_floats[],
                                                   uint32_t *out_vertex_count) {
    uint32_t vertex_count = 0;
    bool vertex_count_set = false;

    for (uint32_t s = 0; s < stream_count; s++) {
        const cgltf_accessor *acc = NULL;
        for (cgltf_size a = 0; a < prim->attributes_count; a++) {
            if (layout[s].gltf_name != NULL && strcmp(prim->attributes[a].name, layout[s].gltf_name) == 0) {
                acc = prim->attributes[a].data;
                break;
            }
        }

        if (!acc) {
            (void)fprintf(stderr, "ERROR: %s: attribute %s not found in glTF data\n", path, layout[s].gltf_name ? layout[s].gltf_name : "(null)");
            return NT_BUILD_ERR_VALIDATION;
        }

        uint32_t acc_components = (uint32_t)cgltf_num_components(acc->type);
        if (acc_components != layout[s].count) {
            (void)fprintf(stderr, "ERROR: %s: attribute %s has %u components, layout expects %u\n", path, layout[s].gltf_name ? layout[s].gltf_name : "(null)", acc_components,
                          (uint32_t)layout[s].count);
            return NT_BUILD_ERR_VALIDATION;
        }

        uint32_t count = (uint32_t)acc->count;
        if (!vertex_count_set) {
            vertex_count = count;
            vertex_count_set = true;
        } else if (count != vertex_count) {
            (void)fprintf(stderr, "ERROR: %s: attribute %s has %u vertices, expected %u\n", path, layout[s].gltf_name ? layout[s].gltf_name : "(null)", count, vertex_count);
            return NT_BUILD_ERR_VALIDATION;
        }

        cgltf_size float_count = (cgltf_size)vertex_count * (cgltf_size)layout[s].count;
        stream_floats[s] = (float *)calloc(float_count, sizeof(float));
        if (!stream_floats[s]) {
            (void)fprintf(stderr, "ERROR: %s: failed to allocate float buffer for %s\n", path, layout[s].gltf_name ? layout[s].gltf_name : "(null)");
            return NT_BUILD_ERR_IO;
        }

        cgltf_size unpacked = cgltf_accessor_unpack_floats(acc, stream_floats[s], float_count);
        if (unpacked == 0) {
            (void)fprintf(stderr, "ERROR: %s: failed to unpack floats for %s\n", path, layout[s].gltf_name ? layout[s].gltf_name : "(null)");
            return NT_BUILD_ERR_FORMAT;
        }
    }

    if (!vertex_count_set) {
        (void)fprintf(stderr, "ERROR: %s: no attributes found\n", path);
        return NT_BUILD_ERR_VALIDATION;
    }

    if (vertex_count > NT_BUILD_MAX_VERTICES) {
        (void)fprintf(stderr, "ERROR: %s: vertex count %u exceeds max %d\n", path, vertex_count, NT_BUILD_MAX_VERTICES);
        return NT_BUILD_ERR_LIMIT;
    }

    *out_vertex_count = vertex_count;
    return NT_BUILD_OK;
}

/* --- Vertex interleaving --- */

static uint8_t *nt_interleave_vertices(const NtStreamLayout *layout, uint32_t stream_count, float *stream_floats[], uint32_t vertex_count, uint32_t vertex_stride, uint32_t *out_vertex_data_size) {
    uint32_t vertex_data_size = vertex_count * vertex_stride;
    uint8_t *vertex_buf = (uint8_t *)calloc(vertex_data_size > 0 ? vertex_data_size : 1, 1);
    if (!vertex_buf) {
        return NULL;
    }

    bool warned_f16 = false;

    for (uint32_t v = 0; v < vertex_count; v++) {
        uint8_t *dst = vertex_buf + ((size_t)v * vertex_stride);
        uint32_t offset = 0;
        for (uint32_t s = 0; s < stream_count; s++) {
            uint32_t comp_size = nt_stream_type_size((uint8_t)layout[s].type);
            for (uint8_t c = 0; c < layout[s].count; c++) {
                float val = stream_floats[s][((size_t)v * layout[s].count) + c];
                nt_convert_component(val, layout[s].type, layout[s].normalized, dst + offset, &warned_f16);
                offset += comp_size;
            }
        }
    }

    *out_vertex_data_size = vertex_data_size;
    return vertex_buf;
}

/* --- Mesh import (called from finish_pack) --- */

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
nt_build_result_t nt_builder_import_mesh(NtBuilderContext *ctx, const char *path, const NtStreamLayout *layout, uint32_t stream_count, uint32_t resource_id) {
    if (!ctx || !path || !layout) {
        return NT_BUILD_ERR_VALIDATION;
    }

    nt_build_result_t ret = nt_validate_stream_layout(path, layout, stream_count);
    if (ret != NT_BUILD_OK) {
        return ret;
    }

    cgltf_data *data = NULL;
    cgltf_primitive *prim = NULL;
    ret = nt_parse_gltf(path, &data, &prim);
    if (ret != NT_BUILD_OK) {
        return ret;
    }

    float *stream_floats[NT_MESH_MAX_STREAMS];
    memset((void *)stream_floats, 0, sizeof(stream_floats));
    uint32_t vertex_count = 0;

    ret = nt_extract_vertex_streams(path, prim, layout, stream_count, stream_floats, &vertex_count);
    if (ret != NT_BUILD_OK) {
        goto cleanup_streams;
    }

    {
        uint32_t vertex_stride = 0;
        for (uint32_t s = 0; s < stream_count; s++) {
            vertex_stride += nt_stream_type_size((uint8_t)layout[s].type) * layout[s].count;
        }

        uint32_t vertex_data_size = 0;
        uint8_t *vertex_buf = nt_interleave_vertices(layout, stream_count, stream_floats, vertex_count, vertex_stride, &vertex_data_size);
        if (!vertex_buf) {
            (void)fprintf(stderr, "ERROR: %s: failed to allocate vertex buffer\n", path);
            ret = NT_BUILD_ERR_IO;
            goto cleanup_streams;
        }

        uint32_t index_count = 0;
        uint8_t index_type = 0;
        uint8_t *index_buf = NULL;
        uint32_t index_data_size = 0;

        if (prim->indices != NULL) {
            index_count = (uint32_t)prim->indices->count;
            if (index_count > NT_BUILD_MAX_INDICES) {
                (void)fprintf(stderr, "ERROR: %s: index count %u exceeds max %d\n", path, index_count, NT_BUILD_MAX_INDICES);
                free(vertex_buf);
                ret = NT_BUILD_ERR_LIMIT;
                goto cleanup_streams;
            }

            if (vertex_count <= 65535) {
                index_type = 1;
                index_data_size = index_count * (uint32_t)sizeof(uint16_t);
            } else {
                index_type = 2;
                index_data_size = index_count * (uint32_t)sizeof(uint32_t);
            }

            index_buf = (uint8_t *)calloc(index_data_size, 1);
            if (!index_buf) {
                free(vertex_buf);
                ret = NT_BUILD_ERR_IO;
                goto cleanup_streams;
            }

            size_t idx_elem_size = (index_type == 1) ? sizeof(uint16_t) : sizeof(uint32_t);
            cgltf_accessor_unpack_indices(prim->indices, index_buf, idx_elem_size, index_count);
        }

        NtMeshAssetHeader mesh_hdr;
        memset(&mesh_hdr, 0, sizeof(mesh_hdr));
        mesh_hdr.magic = NT_MESH_MAGIC;
        mesh_hdr.version = NT_MESH_VERSION;
        mesh_hdr.stream_count = (uint8_t)stream_count;
        mesh_hdr.index_type = index_type;
        mesh_hdr.vertex_count = vertex_count;
        mesh_hdr.index_count = index_count;
        mesh_hdr.vertex_data_size = vertex_data_size;
        mesh_hdr.index_data_size = index_data_size;

        NtStreamDesc descs[NT_MESH_MAX_STREAMS];
        memset(descs, 0, sizeof(descs));
        for (uint32_t s = 0; s < stream_count; s++) {
            descs[s].name_hash = nt_builder_fnv1a(layout[s].engine_name);
            descs[s].type = (uint8_t)layout[s].type;
            descs[s].count = layout[s].count;
            descs[s].normalized = layout[s].normalized ? 1 : 0;
            descs[s]._pad = 0;
        }

        uint32_t descs_size = stream_count * (uint32_t)sizeof(NtStreamDesc);
        uint32_t total_asset_size = (uint32_t)sizeof(NtMeshAssetHeader) + descs_size + vertex_data_size + index_data_size;

        ret = nt_builder_append_data(ctx, &mesh_hdr, (uint32_t)sizeof(NtMeshAssetHeader));
        if (ret == NT_BUILD_OK) {
            ret = nt_builder_append_data(ctx, descs, descs_size);
        }
        if (ret == NT_BUILD_OK && vertex_data_size > 0) {
            ret = nt_builder_append_data(ctx, vertex_buf, vertex_data_size);
        }
        if (ret == NT_BUILD_OK && index_data_size > 0) {
            ret = nt_builder_append_data(ctx, index_buf, index_data_size);
        }

        if (ret == NT_BUILD_OK) {
            ret = nt_builder_register_asset(ctx, resource_id, NT_ASSET_MESH, NT_MESH_VERSION, total_asset_size);
        }

        free(vertex_buf);
        free(index_buf);
    }

cleanup_streams:
    for (uint32_t s = 0; s < stream_count; s++) {
        free(stream_floats[s]);
    }
    cgltf_free(data);
    return ret;
}
