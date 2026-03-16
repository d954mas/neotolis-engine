#ifndef NT_MESH_FORMAT_H
#define NT_MESH_FORMAT_H

#include <stdint.h>

/* Magic: ASCII "MESH" as uint32_t little-endian = 0x4853454D */
#define NT_MESH_MAGIC 0x4853454D
#define NT_MESH_VERSION 1

#define NT_MESH_MAX_STREAMS 8

/* Stream value type */
typedef enum {
    NT_STREAM_UINT8 = 0,   /* 1 byte  */
    NT_STREAM_INT16 = 1,   /* 2 bytes */
    NT_STREAM_UINT16 = 2,  /* 2 bytes */
    NT_STREAM_FLOAT32 = 3, /* 4 bytes */
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
    uint8_t normalized; /* 1 = normalize to [0,1] or [-1,1] on GPU */
    uint8_t _pad;
} NtStreamDesc;
#pragma pack(pop)

_Static_assert(sizeof(NtStreamDesc) == 8, "NtStreamDesc must be 8 bytes");

/*
 * NtMeshAssetHeader — binary header prepended to mesh data in NEOPAK pack.
 *
 * Layout (24 bytes):
 *   magic(4) + version(2) + stream_count(1) + index_type(1) +
 *   vertex_count(4) + index_count(4) +
 *   vertex_data_size(4) + index_data_size(4)
 *
 * After header: NtStreamDesc[stream_count], then vertex data, then index data.
 *
 * Vertex data is interleaved: each vertex contains attributes packed in stream
 * descriptor order. Stride = sum of type_size(type) * count for each stream.
 *
 * Index data: uint16 or uint32 depending on index_type.
 */
#pragma pack(push, 1)
typedef struct {
    uint32_t magic;            /* NT_MESH_MAGIC */
    uint16_t version;          /* NT_MESH_VERSION */
    uint8_t stream_count;      /* number of NtStreamDesc after header */
    uint8_t index_type;        /* 0=none, 1=uint16, 2=uint32 */
    uint32_t vertex_count;     /* number of vertices */
    uint32_t index_count;      /* number of indices (0 if index_type==0) */
    uint32_t vertex_data_size; /* total vertex data in bytes */
    uint32_t index_data_size;  /* total index data in bytes */
} NtMeshAssetHeader;
#pragma pack(pop)

_Static_assert(sizeof(NtMeshAssetHeader) == 24, "NtMeshAssetHeader must be 24 bytes");

/* Byte size of one component of a given stream type */
static inline uint32_t nt_stream_type_size(uint8_t type) {
    static const uint32_t sizes[] = {1, 2, 2, 4};
    return (type < 4) ? sizes[type] : 0;
}

#endif /* NT_MESH_FORMAT_H */
