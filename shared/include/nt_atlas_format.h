#ifndef NT_ATLAS_FORMAT_H
#define NT_ATLAS_FORMAT_H

#include <stdint.h>

/* Magic: ASCII "ATLS" as uint32_t little-endian = 0x534C5441 */
#define NT_ATLAS_MAGIC 0x534C5441
#define NT_ATLAS_VERSION 2

/*
 * Atlas asset binary layout (v2):
 *
 *   Offset 0: NtAtlasHeader (28 bytes)
 *   Then: uint64_t texture_resource_ids[page_count]
 *   Then: NtAtlasRegion regions[region_count]
 *   Then: NtAtlasVertex vertices[total_vertex_count]  (at vertex_offset)
 *   Then: uint16_t  indices[total_index_count]   (at index_offset)
 *
 * vertex_offset / index_offset are byte offsets from header start.
 *
 * Indices are local per region (0 .. vertex_count-1).
 * Runtime offsets them by vertex_start when building GPU buffers.
 * Triangle list: every 3 consecutive indices form one triangle.
 * Convex regions use fan triangulation; concave use ear-clipping.
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
    uint32_t index_offset;       /* 20: byte offset from header start to index array */
    uint32_t total_index_count;  /* 24: total uint16_t index entries across all regions */
} NtAtlasHeader;                 /* 28 bytes */
#pragma pack(pop)
_Static_assert(sizeof(NtAtlasHeader) == 28, "NtAtlasHeader must be 28 bytes");

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
    uint8_t rotated;       /* 28: 3-bit transform: bit0=flipH, bit1=flipV, bit2=diagonal */
    uint8_t index_count;   /* 29: number of triangle indices for this region */
    uint16_t index_start;  /* 30: index into the index array */
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
