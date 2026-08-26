#ifndef NT_MESH_FORMAT_H
#define NT_MESH_FORMAT_H

#include <stdint.h>

/* Magic: ASCII "MESH" as uint32_t little-endian = 0x4853454D */
#define NT_MESH_MAGIC 0x4853454D
#define NT_MESH_VERSION 3

#define NT_MESH_MAX_STREAMS 8

/* Stream value type */
typedef enum {
    NT_STREAM_UINT8 = 0,   /* 1 byte  */
    NT_STREAM_INT8 = 1,    /* 1 byte  */
    NT_STREAM_UINT16 = 2,  /* 2 bytes */
    NT_STREAM_INT16 = 3,   /* 2 bytes */
    NT_STREAM_FLOAT16 = 4, /* 2 bytes */
    NT_STREAM_FLOAT32 = 5, /* 4 bytes */
} nt_stream_type_t;

/*
 * NtStreamDesc — describes one vertex attribute stream.
 *
 * Layout (8 bytes):
 *   name_hash(4) + type(1) + count(1) + normalized(1) + _pad(1)
 *
 * name_hash: hash of attribute name string (e.g. hash("position")).
 * Builder computes hashes at build time. Runtime matches by uint32 compare.
 * Hash function defined in nt_hash module.
 */
#pragma pack(push, 1)
typedef struct {
    uint32_t name_hash; /* hash of attribute name ("position", "normal", ...) */
    uint8_t type;       /* nt_stream_type_t */
    uint8_t count;      /* components per vertex (1-4) */
    uint8_t normalized; /* 1 = normalize integer types to [0,1] (unsigned) or [-1,1] (signed)
                         *     via GL_TRUE in glVertexAttribPointer. Must be 0 for float types. */
    uint8_t _pad;
} NtStreamDesc;
#pragma pack(pop)

_Static_assert(sizeof(NtStreamDesc) == 8, "NtStreamDesc must be 8 bytes");

/* Vertex wire layout (nt_mesh_wire_vtx_t) */
typedef enum {
    NT_MESH_WIRE_VTX_RAW = 0, /* interleaved — identical to the GPU form */
    NT_MESH_WIRE_VTX_SOA = 1, /* per-stream planes; decode = re-interleave */
} nt_mesh_wire_vtx_t;

/* Index wire layout (nt_mesh_wire_idx_t) */
typedef enum {
    NT_MESH_WIRE_IDX_RAW = 0,     /* plain u16/u32 array — identical to the GPU form */
    NT_MESH_WIRE_IDX_MESHOPT = 1, /* meshopt index codec stream (version 1) */
} nt_mesh_wire_idx_t;

/*
 * NtMeshAssetHeader — binary header prepended to mesh data in ntpack.
 *
 * Layout (52 bytes):
 *   magic(4) + version(2) + stream_count(1) + index_type(1) +
 *   vertex_wire(1) + index_wire(1) + _pad(2) +
 *   vertex_count(4) + index_count(4) +
 *   vertex_data_size(4) + index_data_size(4) +
 *   aabb_min(12) + aabb_max(12)
 *
 * After header: NtStreamDesc[stream_count], then vertex WIRE data, then index
 * WIRE data. Wire is what the pack stores; the GPU form is what nt_gfx uploads
 * after decode.
 *
 * GPU vertex form is interleaved: each vertex contains attributes packed in
 * stream descriptor order. Stride = sum of type_size(type) * count per stream.
 * vertex_wire selects how the pack stores it:
 *   RAW — already interleaved, uploaded as-is.
 *   SOA — one plane per stream in NtStreamDesc order. Plane i element size =
 *         nt_stream_type_size(type) * count (the WHOLE attribute, never split
 *         per component); plane i starts at vertex_count * sum of previous
 *         element sizes. A pure permutation: vertex_data_size is identical to
 *         the interleaved size.
 *
 * GPU index form is a plain uint16/uint32 array per index_type. index_wire
 * selects the stored form: RAW as-is, or a MESHOPT codec stream.
 * index_data_size is always the WIRE size (encoded size when MESHOPT); the
 * decoded size is index_count * element size.
 */
#pragma pack(push, 1)
typedef struct {
    uint32_t magic;       /* NT_MESH_MAGIC */
    uint16_t version;     /* NT_MESH_VERSION */
    uint8_t stream_count; /* number of NtStreamDesc after header */
    uint8_t index_type;   /* 0=none, 1=uint16, 2=uint32 (GPU element width) */
    uint8_t vertex_wire;  /* nt_mesh_wire_vtx_t */
    uint8_t index_wire;   /* nt_mesh_wire_idx_t */
    uint8_t _pad[2];
    uint32_t vertex_count;     /* number of vertices */
    uint32_t index_count;      /* number of indices (0 if index_type==0) */
    uint32_t vertex_data_size; /* vertex wire data in bytes (== GPU size; SOA is a permutation) */
    uint32_t index_data_size;  /* index WIRE data in bytes (encoded size when MESHOPT) */
    float aabb_min[3];         /* axis-aligned bounding box minimum (x, y, z) */
    float aabb_max[3];         /* axis-aligned bounding box maximum (x, y, z) */
} NtMeshAssetHeader;
#pragma pack(pop)

_Static_assert(sizeof(NtMeshAssetHeader) == 52, "NtMeshAssetHeader must be 52 bytes");

/* Byte size of one component of a given stream type */
static inline uint32_t nt_stream_type_size(uint8_t type) {
    static const uint32_t sizes[] = {1, 1, 2, 2, 2, 4};
    return (type < 6) ? sizes[type] : 0;
}

#endif /* NT_MESH_FORMAT_H */
