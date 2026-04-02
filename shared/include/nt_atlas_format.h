#ifndef NT_ATLAS_FORMAT_H
#define NT_ATLAS_FORMAT_H

#include <stdint.h>

/* Magic: ASCII "ATLS" as uint32_t little-endian = 0x534C5441 */
#define NT_ATLAS_MAGIC 0x534C5441
#define NT_ATLAS_VERSION 1

/*
 * Atlas asset binary layout:
 *
 *   Offset 0: NtAtlasHeader (20 bytes)
 *   Offset 20: uint64_t texture_resource_ids[page_count]
 *   Then: NtAtlasRegion regions[region_count]
 *   Then: NtAtlasVertex vertices[total_vertex_count]
 *
 * vertex_offset (in header) is the byte offset from header start
 * to the vertex array, so runtime can jump directly to vertices.
 *
 * Triangle indices are NOT serialized -- fan triangulation is
 * deterministic (vertex 0 as pivot) and trivially regenerated
 * at runtime: triangle i = (0, i+1, i+2).
 */

#pragma pack(push, 1)
typedef struct {
    uint32_t magic;              /*  0: NT_ATLAS_MAGIC */
    uint16_t version;            /*  4: NT_ATLAS_VERSION */
    uint16_t region_count;       /*  6: number of NtAtlasRegion entries */
    uint16_t page_count;         /*  8: number of texture pages */
    uint16_t _pad;               /* 10: alignment padding */
    uint32_t vertex_offset;      /* 12: byte offset from header start to vertex array */
    uint32_t total_vertex_count; /* 16: total NtAtlasVertex entries across all regions */
} NtAtlasHeader;                 /* 20 bytes */
#pragma pack(pop)
_Static_assert(sizeof(NtAtlasHeader) == 20, "NtAtlasHeader must be 20 bytes");

#pragma pack(push, 1)
typedef struct {
    uint64_t name_hash;    /*  0: xxh64 of region name */
    uint16_t source_w;     /*  8: original image width (pre-trim) */
    uint16_t source_h;     /* 10: original image height (pre-trim) */
    int16_t trim_offset_x; /* 12: pixels removed from left */
    int16_t trim_offset_y; /* 14: pixels removed from top */
    float origin_x;        /* 16: origin X in source-image space (D-08: float, not fixed-point) */
    float origin_y;        /* 20: origin Y in source-image space */
    uint16_t vertex_start; /* 24: index into vertex array */
    uint8_t vertex_count;  /* 26: number of vertices for this region */
    uint8_t page_index;    /* 27: which texture page this region belongs to */
    uint8_t rotated;       /* 28: 0=no rotation, 1=90 CW rotation applied in atlas */
    uint8_t _pad[3];       /* 29: align to 32 bytes */
} NtAtlasRegion;           /* 32 bytes */
#pragma pack(pop)
_Static_assert(sizeof(NtAtlasRegion) == 32, "NtAtlasRegion must be 32 bytes");

#pragma pack(push, 1)
typedef struct {
    int16_t local_x;  /*  0: local position X (pixels relative to origin) */
    int16_t local_y;  /*  2: local position Y */
    uint16_t atlas_u; /*  4: atlas UV X (normalized 0-65535 over atlas width) */
    uint16_t atlas_v; /*  6: atlas UV Y (normalized 0-65535 over atlas height) */
} NtAtlasVertex;      /*  8 bytes */
#pragma pack(pop)
_Static_assert(sizeof(NtAtlasVertex) == 8, "NtAtlasVertex must be 8 bytes");

#endif /* NT_ATLAS_FORMAT_H */
