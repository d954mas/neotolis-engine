#ifndef NT_MESH_FORMAT_H
#define NT_MESH_FORMAT_H

#include <stdint.h>

/* Magic: ASCII "MESH" as uint32_t little-endian = 0x4853454D */
#define NT_MESH_MAGIC 0x4853454D
#define NT_MESH_VERSION 1

/* Attribute mask bits -- positions MUST match NT_ATTR_* in nt_gfx.h */
#define NT_MESH_ATTR_POSITION (1u << 0)  /* NT_ATTR_POSITION = 0 */
#define NT_MESH_ATTR_NORMAL (1u << 1)    /* NT_ATTR_NORMAL = 1 */
#define NT_MESH_ATTR_COLOR (1u << 2)     /* NT_ATTR_COLOR = 2 */
#define NT_MESH_ATTR_TEXCOORD0 (1u << 3) /* NT_ATTR_TEXCOORD0 = 3 */

/* Required attributes for a valid mesh */
#define NT_MESH_ATTR_REQUIRED NT_MESH_ATTR_POSITION

/*
 * MeshAssetHeader -- binary header prepended to mesh data in NEOPAK pack.
 *
 * Layout (24 bytes):
 *   magic(4) + version(2) + attribute_mask(2) +
 *   vertex_count(4) + index_count(4) +
 *   vertex_data_size(4) + index_data_size(4)
 *
 * After header: vertex data (vertex_data_size bytes), then index data
 * (index_data_size bytes).
 *
 * Vertex data is tightly packed per-vertex structs. Attributes present
 * are indicated by attribute_mask. Attribute order in vertex struct is
 * always: POSITION, NORMAL, COLOR, TEXCOORD0 (skip absent).
 *
 * Vertex attribute formats:
 *   POSITION:  float32 x 3 (12 bytes)
 *   NORMAL:    float32 x 3 (12 bytes)
 *   COLOR:     uint8 x 4 normalized (4 bytes)
 *   TEXCOORD0: float32 x 2 (8 bytes)
 *
 * Index data: uint16_t indices.
 */
#pragma pack(push, 1)
typedef struct {
    uint32_t magic;            /* NT_MESH_MAGIC */
    uint16_t version;          /* NT_MESH_VERSION */
    uint16_t attribute_mask;   /* bitmask of NT_MESH_ATTR_* */
    uint32_t vertex_count;     /* number of vertices */
    uint32_t index_count;      /* number of indices (0 = non-indexed) */
    uint32_t vertex_data_size; /* total vertex data in bytes */
    uint32_t index_data_size;  /* total index data in bytes */
} NtMeshAssetHeader;
#pragma pack(pop)

_Static_assert(sizeof(NtMeshAssetHeader) == 24, "MeshAssetHeader must be 24 bytes");

#endif /* NT_MESH_FORMAT_H */
