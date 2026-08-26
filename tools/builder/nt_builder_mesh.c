/* clang-format off */
#include "nt_builder_internal.h"
#include "hash/nt_hash.h"
#include "nt_mesh_format.h"
#include "cgltf.h"
#include "meshoptimizer.h"
/* clang-format on */

/* --- Stream layout validation (shared by add_mesh and scene mesh paths) --- */

static nt_build_result_t nt_validate_layout_alignment(const char *label, const NtStreamLayout *layout, uint32_t stream_count);

static nt_build_result_t nt_validate_stream_entry(const char *label, uint32_t s, const NtStreamLayout *st) {
    if (st->engine_name == NULL) {
        NT_LOG_ERROR("%s: stream[%u] engine_name is NULL", label, s);
        return NT_BUILD_ERR_VALIDATION;
    }
    if (st->gltf_name == NULL) {
        NT_LOG_ERROR("%s: stream[%u] '%s': gltf_name is NULL", label, s, st->engine_name);
        return NT_BUILD_ERR_VALIDATION;
    }
    const char *name = st->engine_name;
    if (st->count < 1 || st->count > 4) {
        NT_LOG_ERROR("%s: stream[%u] count %u out of range [1, 4]", label, s, st->count);
        return NT_BUILD_ERR_VALIDATION;
    }
    if (nt_stream_type_size((uint8_t)st->type) == 0) {
        NT_LOG_ERROR("%s: stream[%u] '%s': invalid stream type %d", label, s, name, (int)st->type);
        return NT_BUILD_ERR_VALIDATION;
    }
    if (st->source_components > 4) {
        NT_LOG_ERROR("%s: stream[%u] '%s': source_components %u out of range [0, 4]", label, s, name, (uint32_t)st->source_components);
        return NT_BUILD_ERR_VALIDATION;
    }
    if (st->source_components != 0 && st->count > st->source_components) {
        NT_LOG_ERROR("%s: stream[%u] '%s': count %u exceeds source_components %u (widening is not supported)", label, s, name, (uint32_t)st->count, (uint32_t)st->source_components);
        return NT_BUILD_ERR_VALIDATION;
    }
    if (st->normalized && (st->type == NT_STREAM_FLOAT32 || st->type == NT_STREAM_FLOAT16)) {
        NT_LOG_ERROR("%s: stream[%u] '%s': normalized=true is invalid for float types", label, s, name);
        return NT_BUILD_ERR_VALIDATION;
    }
    return NT_BUILD_OK;
}

nt_build_result_t nt_builder_validate_stream_layout(const char *label, const NtStreamLayout *layout, uint32_t stream_count) {
    if (stream_count == 0 || stream_count > NT_MESH_MAX_STREAMS) {
        NT_LOG_ERROR("%s: stream_count %u out of range [1, %d]", label, stream_count, NT_MESH_MAX_STREAMS);
        return NT_BUILD_ERR_VALIDATION;
    }

    bool has_position = false;
    uint32_t name_hashes[NT_MESH_MAX_STREAMS];
    for (uint32_t s = 0; s < stream_count; s++) {
        if (nt_validate_stream_entry(label, s, &layout[s]) != NT_BUILD_OK) {
            return NT_BUILD_ERR_VALIDATION;
        }
        /* The runtime matches shader locations by NtStreamDesc.name_hash, so two streams
         * whose names share a hash (identical OR a 32-bit collision) bind one location,
         * last wins. Compare the hashes the pack will actually carry. */
        name_hashes[s] = nt_hash32_str(layout[s].engine_name).value;
        for (uint32_t p = 0; p < s; p++) {
            if (name_hashes[p] == name_hashes[s]) {
                NT_LOG_ERROR("%s: stream[%u] '%s' and stream[%u] '%s' share name_hash 0x%08x -- rename one", label, s, layout[s].engine_name, p, layout[p].engine_name, name_hashes[s]);
                return NT_BUILD_ERR_VALIDATION;
            }
        }
        /* One source attribute -> one stream: duplicates make behavior order-dependent
         * (AABB follows the first POSITION; tangent compute replaces only the first TANGENT). */
        for (uint32_t p = 0; p < s; p++) {
            if (layout[p].gltf_name != NULL && layout[s].gltf_name != NULL && strcmp(layout[p].gltf_name, layout[s].gltf_name) == 0) {
                NT_LOG_ERROR("%s: stream[%u] duplicates gltf_name '%s' of stream[%u]", label, s, layout[s].gltf_name, p);
                return NT_BUILD_ERR_VALIDATION;
            }
        }
        if (layout[s].gltf_name != NULL && strcmp(layout[s].gltf_name, "POSITION") == 0) {
            has_position = true;
        }
    }
    if (!has_position) {
        NT_LOG_ERROR("%s: stream layout missing required POSITION attribute", label);
        return NT_BUILD_ERR_VALIDATION;
    }
    return nt_validate_layout_alignment(label, layout, stream_count);
}

/* WebGL2 rejects attribute offset/stride not divisible by the attribute's type size
 * (desktop GL tolerates it, so a bad layout would only fail in the browser). */
static nt_build_result_t nt_validate_layout_alignment(const char *label, const NtStreamLayout *layout, uint32_t stream_count) {
    uint32_t offset = 0;
    for (uint32_t s = 0; s < stream_count; s++) {
        uint32_t comp_size = nt_stream_type_size((uint8_t)layout[s].type);
        if (offset % comp_size != 0) {
            NT_LOG_ERROR("%s: stream[%u] '%s' offset %u is not a multiple of its type size %u (WebGL2 rule) -- reorder streams so larger types come first", label, s,
                         layout[s].engine_name ? layout[s].engine_name : "(null)", offset, comp_size);
            return NT_BUILD_ERR_VALIDATION;
        }
        offset += comp_size * layout[s].count;
    }
    uint32_t stride = offset;
    for (uint32_t s = 0; s < stream_count; s++) {
        uint32_t comp_size = nt_stream_type_size((uint8_t)layout[s].type);
        if (stride % comp_size != 0) {
            NT_LOG_ERROR("%s: vertex stride %u is not a multiple of stream[%u] '%s' type size %u (WebGL2 rule) -- widen a count or change a stream type so the stride divides evenly", label, stride, s,
                         layout[s].engine_name ? layout[s].engine_name : "(null)", comp_size);
            return NT_BUILD_ERR_VALIDATION;
        }
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
        /* Single-mesh mode: expect exactly 1 mesh, 1 primitive */
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
            if (prim->attributes[a].name != NULL && strcmp(prim->attributes[a].name, layout[s].gltf_name) == 0) {
                acc = prim->attributes[a].data;
                break;
            }
        }

        if (!acc) {
            NT_LOG_ERROR("%s: attribute %s not found in glTF data", path, layout[s].gltf_name ? layout[s].gltf_name : "(null)");
            return NT_BUILD_ERR_VALIDATION;
        }

        uint32_t acc_components = (uint32_t)cgltf_num_components(acc->type);
        uint32_t src_components = (layout[s].source_components != 0) ? layout[s].source_components : layout[s].count;
        if (acc_components != src_components) {
            NT_LOG_ERROR("%s: attribute %s has %u components, layout expects %u", path, layout[s].gltf_name ? layout[s].gltf_name : "(null)", acc_components, src_components);
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

        /* cgltf can only unpack whole source-width elements; narrow by compacting after */
        cgltf_size float_count = (cgltf_size)vertex_count * (cgltf_size)src_components;
        stream_floats[s] = (float *)calloc(float_count, sizeof(float));
        NT_BUILD_ASSERT(stream_floats[s] && "failed to allocate float buffer for vertex stream");

        cgltf_size unpacked = cgltf_accessor_unpack_floats(acc, stream_floats[s], float_count);
        if (unpacked == 0) {
            NT_LOG_ERROR("%s: failed to unpack floats for %s", path, layout[s].gltf_name ? layout[s].gltf_name : "(null)");
            return NT_BUILD_ERR_FORMAT;
        }
        nt_builder_narrow_stream_floats(stream_floats[s], vertex_count, src_components, layout[s].count);
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

/* Narrowed POSITION drops trailing axes from the pack -- the AABB must not
 * keep extents the vertex data no longer carries. */
static void nt_clamp_aabb_to_position_count(const NtStreamLayout *layout, uint32_t stream_count, float aabb_min[3], float aabb_max[3]) {
    for (uint32_t s = 0; s < stream_count; s++) {
        if (layout[s].gltf_name != NULL && strcmp(layout[s].gltf_name, "POSITION") == 0) {
            for (uint32_t axis = layout[s].count; axis < 3; axis++) {
                aabb_min[axis] = 0.0F;
                aabb_max[axis] = 0.0F;
            }
            return;
        }
    }
}

/* --- Wire encode: SOA planes + meshopt index stream (decoded by nt_meshwire) --- */

/* Permutes interleaved vertices into per-stream planes (the pinned SOA plane
 * contract in nt_mesh_format.h: whole-attribute elements in desc order). */
static void nt_deinterleave_to_planes(uint8_t *dst, const uint8_t *src, uint32_t vertex_count, const NtStreamLayout *layout, uint32_t stream_count, uint32_t stride) {
    uint8_t *plane = dst;
    uint32_t offset = 0;
    for (uint32_t s = 0; s < stream_count; s++) {
        uint32_t elem = nt_stream_type_size((uint8_t)layout[s].type) * layout[s].count;
        for (uint32_t v = 0; v < vertex_count; v++) {
            memcpy(plane + ((size_t)v * elem), src + ((size_t)v * stride) + offset, elem);
        }
        plane += (size_t)vertex_count * elem;
        offset += elem;
    }
}

/* Every index must be < vertex_count: encodeIndexBufferBound sizes its varints
 * from vertex_count, so an out-of-range index overruns the encode buffer (and
 * was a silent hole in the RAW path too). */
static nt_build_result_t nt_validate_index_range(const uint8_t *index_buf, uint32_t index_count, uint8_t index_type, uint32_t vertex_count) {
    for (uint32_t i = 0; i < index_count; i++) {
        uint32_t v = (index_type == 1) ? ((const uint16_t *)index_buf)[i] : ((const uint32_t *)index_buf)[i];
        if (v >= vertex_count) {
            NT_LOG_ERROR("mesh: index[%u] = %u out of range (vertex_count %u)", i, v, vertex_count);
            return NT_BUILD_ERR_VALIDATION;
        }
    }
    return NT_BUILD_OK;
}

/* Encodes triangles with the meshopt index codec, then decodes back and
 * REPLACES index_buf: the canonical (rotation-normalized) form is the pack's
 * ground truth, so the runtime decode is byte-exact against it.
 * Returns the encoded buffer (caller frees) or NULL when encoding is not
 * smaller than RAW; *out_encoded_size is valid only on non-NULL return. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity) — NT_BUILD_ASSERT expansions dominate the count
static uint8_t *nt_encode_indices_meshopt(uint8_t *index_buf, uint32_t index_count, uint8_t index_type, uint32_t index_data_size, uint32_t vertex_count, uint32_t *out_encoded_size) {
    /* meshopt encode input is strictly u32 -- widen the builder's u16 */
    uint32_t *idx32 = (uint32_t *)malloc((size_t)index_count * sizeof(uint32_t));
    NT_BUILD_ASSERT(idx32 && "index widen alloc failed");
    if (index_type == 1) {
        const uint16_t *src16 = (const uint16_t *)index_buf;
        for (uint32_t i = 0; i < index_count; i++) {
            idx32[i] = src16[i];
        }
    } else {
        memcpy(idx32, index_buf, (size_t)index_count * sizeof(uint32_t));
    }

    /* Pin the codec version explicitly: the upstream default may drift on re-vendor */
    meshopt_encodeIndexVersion(1);
    size_t bound = meshopt_encodeIndexBufferBound(index_count, vertex_count);
    unsigned char *enc = (unsigned char *)malloc(bound);
    NT_BUILD_ASSERT(enc && "index encode alloc failed");
    size_t enc_size = meshopt_encodeIndexBuffer(enc, bound, idx32, index_count);
    free(idx32);
    NT_BUILD_ASSERT(enc_size > 0 && "meshopt index encode failed");

    uint32_t idx_elem = (index_type == 1) ? 2U : 4U;
    int decode_rc = meshopt_decodeIndexBuffer(index_buf, index_count, idx_elem, enc, enc_size);
    NT_BUILD_ASSERT(decode_rc == 0 && "meshopt decode-back failed -- vendored codec broken");

    if (enc_size >= index_data_size) {
        free(enc); /* rare (tiny meshes): canonical RAW already in index_buf */
        return NULL;
    }
    *out_encoded_size = (uint32_t)enc_size;
    return enc;
}

/* --- Shared: build binary mesh output buffer from mesh components --- */

// NOLINTNEXTLINE(readability-function-cognitive-complexity) — NT_BUILD_ASSERT expansions dominate the count
nt_build_result_t nt_builder_build_mesh_buffer(const NtStreamLayout *layout, uint32_t stream_count, float *stream_floats[], uint32_t vertex_count, const cgltf_primitive *prim, uint8_t *index_buf,
                                               uint32_t index_count, uint8_t index_type, uint32_t index_data_size, uint8_t **out_data, uint32_t *out_size) {
    uint32_t vertex_stride = 0;
    for (uint32_t s = 0; s < stream_count; s++) {
        vertex_stride += nt_stream_type_size((uint8_t)layout[s].type) * layout[s].count;
    }

    uint32_t vertex_data_size = 0;
    uint8_t *vertex_buf = nt_interleave_vertices(layout, stream_count, stream_floats, vertex_count, vertex_stride, &vertex_data_size);
    NT_BUILD_ASSERT(vertex_buf && "interleave_vertices alloc failed");

    if (index_buf && index_count > 0) {
        nt_build_result_t idx_ret = nt_validate_index_range(index_buf, index_count, index_type, vertex_count);
        if (idx_ret != NT_BUILD_OK) {
            free(vertex_buf);
            return idx_ret;
        }
    }

    /* Automatic wire policy: vertices always SOA; indices meshopt iff smaller */
    uint8_t index_wire = NT_MESH_WIRE_IDX_RAW;
    uint8_t *encoded_idx = NULL;
    uint32_t wire_index_size = index_data_size;
    if (index_buf && index_type != 0 && index_count > 0 && index_count % 3 == 0) {
        uint32_t encoded_size = 0;
        encoded_idx = nt_encode_indices_meshopt(index_buf, index_count, index_type, index_data_size, vertex_count, &encoded_size);
        if (encoded_idx) {
            index_wire = NT_MESH_WIRE_IDX_MESHOPT;
            wire_index_size = encoded_size;
        }
    }
    const uint8_t *wire_index = encoded_idx ? encoded_idx : index_buf;

    uint8_t *soa_buf = (uint8_t *)malloc(vertex_data_size > 0 ? vertex_data_size : 1);
    NT_BUILD_ASSERT(soa_buf && "SOA plane alloc failed");
    nt_deinterleave_to_planes(soa_buf, vertex_buf, vertex_count, layout, stream_count, vertex_stride);
    free(vertex_buf);
    vertex_buf = soa_buf;

    NtMeshAssetHeader mesh_hdr;
    memset(&mesh_hdr, 0, sizeof(mesh_hdr));
    mesh_hdr.magic = NT_MESH_MAGIC;
    mesh_hdr.version = NT_MESH_VERSION;
    mesh_hdr.stream_count = (uint8_t)stream_count;
    mesh_hdr.index_type = index_type;
    mesh_hdr.vertex_wire = NT_MESH_WIRE_VTX_SOA;
    mesh_hdr.index_wire = index_wire;
    mesh_hdr.vertex_count = vertex_count;
    mesh_hdr.index_count = index_count;
    mesh_hdr.vertex_data_size = vertex_data_size;
    mesh_hdr.index_data_size = wire_index_size;
    if (prim) {
        nt_extract_aabb(prim, mesh_hdr.aabb_min, mesh_hdr.aabb_max);
        nt_clamp_aabb_to_position_count(layout, stream_count, mesh_hdr.aabb_min, mesh_hdr.aabb_max);
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
    uint32_t total = (uint32_t)sizeof(NtMeshAssetHeader) + descs_size + vertex_data_size + wire_index_size;

    uint8_t *buf = (uint8_t *)malloc(total);
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
    if (wire_index_size > 0 && wire_index) {
        memcpy(buf + off, wire_index, wire_index_size);
    }

    free(encoded_idx);
    free(vertex_buf);
    *out_data = buf;
    *out_size = total;
    return NT_BUILD_OK;
}

/* --- Decode: glTF file -> binary mesh buffer (eager, called from add_mesh) --- */

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
nt_build_result_t nt_builder_decode_mesh(const char *path, const NtStreamLayout *layout, uint32_t stream_count, nt_tangent_mode_t tangent_mode, const char *mesh_name, uint32_t mesh_index,
                                         uint8_t **out_data, uint32_t *out_size) {
    /* Tangent computation is scene-API only; a non-default mode here would be silently ignored */
    NT_BUILD_ASSERT(tangent_mode == NT_TANGENT_AUTO && "add_mesh reads TANGENT from the glTF; use the scene API to compute tangents");
    if (!path || !layout || !out_data || !out_size) {
        return NT_BUILD_ERR_VALIDATION;
    }

    nt_build_result_t ret = nt_builder_validate_stream_layout(path, layout, stream_count);
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
