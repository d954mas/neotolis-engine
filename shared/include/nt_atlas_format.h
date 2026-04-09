#ifndef NT_ATLAS_FORMAT_H
#define NT_ATLAS_FORMAT_H

#include <stdint.h>

/* Magic: ASCII "ATLS" as uint32_t little-endian = 0x534C5441 */
#define NT_ATLAS_MAGIC 0x534C5441
#define NT_ATLAS_VERSION 3

/*
 * Atlas asset binary layout (v3):
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
 *
 * v3 changes from v2:
 *   - NtAtlasRegion.rotated → transform (field rename only; same 3-bit D4
 *     flags, clearer intent: it's a transform mask, not a bool)
 *   - NtAtlasRegion.vertex_start and .index_start widened from uint16_t to
 *     uint32_t so a single atlas can hold more than 64K vertices/indices.
 *     Region struct grew from 32 to 36 bytes.
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
    uint16_t source_w;     /*  8: original image width in pixels (pre-trim) */
    uint16_t source_h;     /* 10: original image height in pixels (pre-trim) */
    int16_t trim_offset_x; /* 12: pixels stripped from the left edge during alpha trim
                            *     (add to NtAtlasVertex.local_x to get source-image space X) */
    int16_t trim_offset_y; /* 14: pixels stripped from the top edge during alpha trim */
    float origin_x;        /* 16: pivot X, normalized over source_w (NOT trim_w).
                            *     0.0 = left edge, 0.5 = centre (default), 1.0 = right edge.
                            *     Values outside [0, 1] are allowed — the pivot may lie outside
                            *     the frame (weapons, effects, motion-stabilised sprites).
                            *     Runtime resolves: pivot_px_x = origin_x * source_w.
                            *     Source-space (not trim-space) gives stable pivots across
                            *     animation frames where trim bounds vary. */
    float origin_y;        /* 20: pivot Y, normalized over source_h. Same semantics. */
    uint32_t vertex_start; /* 24: index into vertex array (uint32 in v3, was uint16 in v2) */
    uint32_t index_start;  /* 28: index into the index array (uint32 in v3, was uint16 in v2) */
    uint8_t vertex_count;  /* 32: number of vertices for this region (max 16 per builder limit) */
    uint8_t page_index;    /* 33: which texture page this region belongs to */
    uint8_t transform;     /* 34: D4 transform flags — bit0=flipH, bit1=flipV, bit2=diagonal.
                            *     Apply order: diagonal → flipH → flipV. 0 = identity. */
    uint8_t index_count;   /* 35: triangle indices for this region. uint8_t caps at 255 =
                            *     85 triangles; with max_vertices=16 the ear-clip/fan output
                            *     is at most (16-2)*3 = 42 indices, so 1 byte is sufficient. */
} NtAtlasRegion;           /* 36 bytes */
#pragma pack(pop)
_Static_assert(sizeof(NtAtlasRegion) == 36, "NtAtlasRegion must be 36 bytes");

#pragma pack(push, 1)
typedef struct {
    int16_t local_x;  /*  0: pixel X in trim-rect local space (0..trim_w-1).
                       *     Add NtAtlasRegion.trim_offset_x to get source-image space X.
                       *     Subtract (origin_x * source_w) to get offset from the pivot:
                       *       pivot_relative_x = (local_x + trim_offset_x) - (origin_x * source_w)
                       *     The rendered world-space position of the vertex is then:
                       *       world_x = entity_pos_x + pivot_relative_x * scale_x */
    int16_t local_y;  /*  2: pixel Y in trim-rect local space (0..trim_h-1). Same semantics. */
    uint16_t atlas_u; /*  4: atlas UV X (normalized 0-65535 over atlas width) */
    uint16_t atlas_v; /*  6: atlas UV Y (normalized 0-65535 over atlas height) */
} NtAtlasVertex;      /*  8 bytes */
#pragma pack(pop)
_Static_assert(sizeof(NtAtlasVertex) == 8, "NtAtlasVertex must be 8 bytes");

#endif /* NT_ATLAS_FORMAT_H */
