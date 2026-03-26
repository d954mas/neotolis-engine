/* clang-format off */
#include "nt_builder_internal.h"
#include "hash/nt_hash.h"
#include "nt_mesh_format.h"
#include "cgltf.h"
/* clang-format on */

/* --- Stream layout validation --- */

static nt_build_result_t nt_validate_stream_layout(const char *path, const NtStreamLayout *layout, uint32_t stream_count) {
    if (stream_count == 0 || stream_count > NT_MESH_MAX_STREAMS) {
        NT_LOG_ERROR("%s: stream_count %u out of range [1, %d]", path, stream_count, NT_MESH_MAX_STREAMS);
        return NT_BUILD_ERR_VALIDATION;
    }

    bool has_position = false;
    for (uint32_t s = 0; s < stream_count; s++) {
        if (layout[s].count < 1 || layout[s].count > 4) {
            NT_LOG_ERROR("%s: stream[%u] count %u out of range [1, 4]", path, s, layout[s].count);
            return NT_BUILD_ERR_VALIDATION;
        }
        if (layout[s].normalized && (layout[s].type == NT_STREAM_FLOAT32 || layout[s].type == NT_STREAM_FLOAT16)) {
            NT_LOG_ERROR("%s: stream[%u] '%s': normalized=true is invalid for float types", path, s, layout[s].engine_name ? layout[s].engine_name : "(null)");
            return NT_BUILD_ERR_VALIDATION;
        }
        if (layout[s].gltf_name != NULL && strcmp(layout[s].gltf_name, "POSITION") == 0) {
            has_position = true;
        }
    }
    if (!has_position) {
        NT_LOG_ERROR("%s: stream layout missing required POSITION attribute", path);
        return NT_BUILD_ERR_VALIDATION;
    }
    return NT_BUILD_OK;
}

/* --- glTF parsing with multi-mesh support --- */

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static nt_build_result_t nt_parse_gltf_mesh(const char *path, const char *mesh_name, uint32_t mesh_index, cgltf_data **out_data, cgltf_primitive **out_prim) {
    cgltf_options options;
    memset(&options, 0, sizeof(options));

    cgltf_result result = cgltf_parse_file(&options, path, out_data);
    if (result != cgltf_result_success) {
        NT_LOG_ERROR("%s: failed to parse glTF (cgltf error %d)", path, (int)result);
        return NT_BUILD_ERR_FORMAT;
    }

    result = cgltf_load_buffers(&options, *out_data, path);
    if (result != cgltf_result_success) {
        cgltf_free(*out_data);
        *out_data = NULL;
        NT_BUILD_ASSERT(0 && "failed to load glTF buffers");
    }

    result = cgltf_validate(*out_data);
    if (result != cgltf_result_success) {
        NT_LOG_WARN("%s: glTF validation issues (cgltf error %d)", path, (int)result);
    }

    /* Determine which mesh/primitive to select */
    cgltf_size sel_mesh = 0;

    if (mesh_name != NULL) {
        /* Select mesh by name */
        bool found = false;
        for (cgltf_size i = 0; i < (*out_data)->meshes_count; i++) {
            if ((*out_data)->meshes[i].name != NULL && strcmp((*out_data)->meshes[i].name, mesh_name) == 0) {
                sel_mesh = i;
                found = true;
                break;
            }
        }
        if (!found) {
            NT_LOG_ERROR("%s: mesh '%s' not found", path, mesh_name);
            cgltf_free(*out_data);
            *out_data = NULL;
            return NT_BUILD_ERR_VALIDATION;
        }
    } else if (mesh_index != UINT32_MAX) {
        /* Select mesh by index */
        if (mesh_index >= (uint32_t)(*out_data)->meshes_count) {
            NT_LOG_ERROR("%s: mesh_index %u out of range (meshes_count=%zu)", path, mesh_index, (*out_data)->meshes_count);
            cgltf_free(*out_data);
            *out_data = NULL;
            return NT_BUILD_ERR_VALIDATION;
        }
        sel_mesh = mesh_index;
    } else {
        /* Single-mesh mode (D-11): expect exactly 1 mesh, 1 primitive */
        if ((*out_data)->meshes_count != 1) {
            NT_LOG_ERROR("%s: expected 1 mesh, found %zu", path, (*out_data)->meshes_count);
            cgltf_free(*out_data);
            *out_data = NULL;
            return NT_BUILD_ERR_VALIDATION;
        }
        if ((*out_data)->meshes[0].primitives_count != 1) {
            NT_LOG_ERROR("%s: expected 1 primitive, found %zu", path, (*out_data)->meshes[0].primitives_count);
            cgltf_free(*out_data);
            *out_data = NULL;
            return NT_BUILD_ERR_VALIDATION;
        }
    }

    if ((*out_data)->meshes[sel_mesh].primitives_count < 1) {
        NT_LOG_ERROR("%s: mesh[%zu] has no primitives", path, sel_mesh);
        cgltf_free(*out_data);
        *out_data = NULL;
        return NT_BUILD_ERR_VALIDATION;
    }

    if ((*out_data)->meshes[sel_mesh].primitives_count > 1) {
        NT_LOG_ERROR("%s: mesh[%zu] has %zu primitives, add_mesh supports only single-primitive meshes (use scene API for multi-primitive)", path, sel_mesh,
                     (*out_data)->meshes[sel_mesh].primitives_count);
        cgltf_free(*out_data);
        *out_data = NULL;
        return NT_BUILD_ERR_VALIDATION;
    }

    *out_prim = &(*out_data)->meshes[sel_mesh].primitives[0];

    if ((*out_prim)->type != cgltf_primitive_type_triangles) {
        NT_LOG_ERROR("%s: primitive type %d is not TRIANGLES (only triangles supported)", path, (int)(*out_prim)->type);
        cgltf_free(*out_data);
        *out_data = NULL;
        return NT_BUILD_ERR_VALIDATION;
    }

    return NT_BUILD_OK;
}

/* --- AABB extraction from POSITION accessor --- */

#include <float.h>

void nt_extract_aabb(const cgltf_primitive *prim, float out_min[3], float out_max[3]) {
    const cgltf_accessor *pos_acc = NULL;
    for (cgltf_size a = 0; a < prim->attributes_count; a++) {
        if (prim->attributes[a].name != NULL && strcmp(prim->attributes[a].name, "POSITION") == 0) {
            pos_acc = prim->attributes[a].data;
            break;
        }
    }
    if (!pos_acc) {
        memset(out_min, 0, 3 * sizeof(float));
        memset(out_max, 0, 3 * sizeof(float));
        return;
    }

    NT_BUILD_ASSERT(pos_acc->type == cgltf_type_vec3 && "POSITION accessor must be VEC3");

    if (pos_acc->has_min && pos_acc->has_max) {
        /* Fast path: use pre-computed bounds from glTF exporter */
        for (int i = 0; i < 3; i++) {
            out_min[i] = (float)pos_acc->min[i];
            out_max[i] = (float)pos_acc->max[i];
        }
        return;
    }

    /* Slow path: compute from vertex data */
    out_min[0] = out_min[1] = out_min[2] = FLT_MAX;
    out_max[0] = out_max[1] = out_max[2] = -FLT_MAX;

    float tmp[3] = {0};
    for (cgltf_size v = 0; v < pos_acc->count; v++) {
        cgltf_accessor_read_float(pos_acc, v, tmp, 3);
        for (int i = 0; i < 3; i++) {
            if (tmp[i] < out_min[i]) {
                out_min[i] = tmp[i];
            }
            if (tmp[i] > out_max[i]) {
                out_max[i] = tmp[i];
            }
        }
    }
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
            NT_LOG_ERROR("%s: attribute %s not found in glTF data", path, layout[s].gltf_name ? layout[s].gltf_name : "(null)");
            return NT_BUILD_ERR_VALIDATION;
        }

        uint32_t acc_components = (uint32_t)cgltf_num_components(acc->type);
        if (acc_components != layout[s].count) {
            NT_LOG_ERROR("%s: attribute %s has %u components, layout expects %u", path, layout[s].gltf_name ? layout[s].gltf_name : "(null)", acc_components, (uint32_t)layout[s].count);
            return NT_BUILD_ERR_VALIDATION;
        }

        uint32_t count = (uint32_t)acc->count;
        if (!vertex_count_set) {
            vertex_count = count;
            vertex_count_set = true;
        } else if (count != vertex_count) {
            NT_LOG_ERROR("%s: attribute %s has %u vertices, expected %u", path, layout[s].gltf_name ? layout[s].gltf_name : "(null)", count, vertex_count);
            return NT_BUILD_ERR_VALIDATION;
        }

        cgltf_size float_count = (cgltf_size)vertex_count * (cgltf_size)layout[s].count;
        stream_floats[s] = (float *)calloc(float_count, sizeof(float));
        NT_BUILD_ASSERT(stream_floats[s] && "failed to allocate float buffer for vertex stream");

        cgltf_size unpacked = cgltf_accessor_unpack_floats(acc, stream_floats[s], float_count);
        if (unpacked == 0) {
            NT_LOG_ERROR("%s: failed to unpack floats for %s", path, layout[s].gltf_name ? layout[s].gltf_name : "(null)");
            return NT_BUILD_ERR_FORMAT;
        }
    }

    if (!vertex_count_set) {
        NT_LOG_ERROR("%s: no attributes found", path);
        return NT_BUILD_ERR_VALIDATION;
    }

    if (vertex_count > NT_BUILD_MAX_VERTICES) {
        NT_LOG_ERROR("%s: vertex count %u exceeds max %d", path, vertex_count, NT_BUILD_MAX_VERTICES);
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

    for (uint32_t v = 0; v < vertex_count; v++) {
        uint8_t *dst = vertex_buf + ((size_t)v * vertex_stride);
        uint32_t offset = 0;
        for (uint32_t s = 0; s < stream_count; s++) {
            uint32_t comp_size = nt_stream_type_size((uint8_t)layout[s].type);
            for (uint8_t c = 0; c < layout[s].count; c++) {
                float val = stream_floats[s][((size_t)v * layout[s].count) + c];
                nt_builder_convert_component(val, layout[s].type, layout[s].normalized, dst + offset);
                offset += comp_size;
            }
        }
    }

    *out_vertex_data_size = vertex_data_size;
    return vertex_buf;
}

/* --- Shared: build binary mesh output buffer from mesh components --- */

nt_build_result_t nt_builder_build_mesh_buffer(const NtStreamLayout *layout, uint32_t stream_count, float *stream_floats[], uint32_t vertex_count, const cgltf_primitive *prim, uint8_t *index_buf,
                                               uint32_t index_count, uint8_t index_type, uint32_t index_data_size, uint8_t **out_data, uint32_t *out_size) {
    uint32_t vertex_stride = 0;
    for (uint32_t s = 0; s < stream_count; s++) {
        vertex_stride += nt_stream_type_size((uint8_t)layout[s].type) * layout[s].count;
    }

    uint32_t vertex_data_size = 0;
    uint8_t *vertex_buf = nt_interleave_vertices(layout, stream_count, stream_floats, vertex_count, vertex_stride, &vertex_data_size);
    NT_BUILD_ASSERT(vertex_buf && "interleave_vertices alloc failed");

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
    if (prim) {
        nt_extract_aabb(prim, mesh_hdr.aabb_min, mesh_hdr.aabb_max);
    }

    NtStreamDesc descs[NT_MESH_MAX_STREAMS];
    memset(descs, 0, sizeof(descs));
    for (uint32_t s = 0; s < stream_count; s++) {
        descs[s].name_hash = nt_hash32_str(layout[s].engine_name).value;
        descs[s].type = (uint8_t)layout[s].type;
        descs[s].count = layout[s].count;
        descs[s].normalized = layout[s].normalized ? 1 : 0;
        descs[s]._pad = 0;
    }

    uint32_t descs_size = stream_count * (uint32_t)sizeof(NtStreamDesc);
    uint32_t total = (uint32_t)sizeof(NtMeshAssetHeader) + descs_size + vertex_data_size + index_data_size;

    uint8_t *buf = (uint8_t *)malloc(total);
    if (!buf) {
        free(vertex_buf);
    }
    NT_BUILD_ASSERT(buf && "mesh buffer alloc failed");

    uint32_t off = 0;
    memcpy(buf + off, &mesh_hdr, sizeof(NtMeshAssetHeader));
    off += (uint32_t)sizeof(NtMeshAssetHeader);
    memcpy(buf + off, descs, descs_size);
    off += descs_size;
    if (vertex_data_size > 0) {
        memcpy(buf + off, vertex_buf, vertex_data_size);
        off += vertex_data_size;
    }
    if (index_data_size > 0 && index_buf) {
        memcpy(buf + off, index_buf, index_data_size);
    }

    free(vertex_buf);
    *out_data = buf;
    *out_size = total;
    return NT_BUILD_OK;
}

/* --- Decode: glTF file -> binary mesh buffer (eager, called from add_mesh) --- */

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
nt_build_result_t nt_builder_decode_mesh(const char *path, const NtStreamLayout *layout, uint32_t stream_count, nt_tangent_mode_t tangent_mode, const char *mesh_name, uint32_t mesh_index,
                                         uint8_t **out_data, uint32_t *out_size) {
    (void)tangent_mode; /* reserved for future tangent support in add_mesh path */
    if (!path || !layout || !out_data || !out_size) {
        return NT_BUILD_ERR_VALIDATION;
    }

    nt_build_result_t ret = nt_validate_stream_layout(path, layout, stream_count);
    if (ret != NT_BUILD_OK) {
        return ret;
    }

    cgltf_data *data = NULL;
    cgltf_primitive *prim = NULL;
    ret = nt_parse_gltf_mesh(path, mesh_name, mesh_index, &data, &prim);
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
        uint32_t index_count = 0;
        uint8_t index_type = 0;
        uint8_t *index_buf = NULL;
        uint32_t index_data_size = 0;

        if (prim->indices != NULL) {
            index_count = (uint32_t)prim->indices->count;
            if (index_count > NT_BUILD_MAX_INDICES) {
                NT_LOG_ERROR("%s: index count %u exceeds max %d", path, index_count, NT_BUILD_MAX_INDICES);
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
            NT_BUILD_ASSERT(index_buf && "index buffer alloc failed");

            size_t idx_elem_size = (index_type == 1) ? sizeof(uint16_t) : sizeof(uint32_t);
            cgltf_accessor_unpack_indices(prim->indices, index_buf, idx_elem_size, index_count);
        }

        ret = nt_builder_build_mesh_buffer(layout, stream_count, stream_floats, vertex_count, prim, index_buf, index_count, index_type, index_data_size, out_data, out_size);
        free(index_buf);
    }

cleanup_streams:
    for (uint32_t s = 0; s < stream_count; s++) {
        free(stream_floats[s]);
    }
    cgltf_free(data);
    return ret;
}
