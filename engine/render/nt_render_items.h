#ifndef NT_RENDER_ITEMS_H
#define NT_RENDER_ITEMS_H

#include <string.h>

#include "render/nt_render_defs.h"
#include "sort/nt_sort.h"

/* ---- Sort (typed radix sort for render items, defined in nt_render_items.c) ---- */

void nt_sort_by_key(nt_render_item_t *items, uint32_t count, nt_render_item_t *scratch);

/* ---- Sort key helpers (inline, no component knowledge) ---- */

static inline uint64_t nt_sort_key_opaque(uint32_t material_id, uint32_t mesh_id) { return ((uint64_t)material_id << 32) | (uint64_t)mesh_id; }

static inline uint16_t nt_depth_to_u16(float depth, float near_plane, float far_plane) {
    float range = far_plane - near_plane;
    if (range <= 0.0F) {
        return 0;
    }
    float t = (depth - near_plane) / range;
    if (t < 0.0F) {
        t = 0.0F;
    }
    if (t > 1.0F) {
        t = 1.0F;
    }
    return (uint16_t)(t * 65535.0F);
}

static inline uint64_t nt_sort_key_depth_back_to_front(float depth, float near_plane, float far_plane) { return (uint64_t)(0xFFFF - nt_depth_to_u16(depth, near_plane, far_plane)) << 48; }

static inline uint64_t nt_sort_key_z(float z) {
    uint32_t bits;
    memcpy(&bits, &z, sizeof(bits));
    if (bits & 0x80000000U) {
        bits = ~bits; /* negative: invert all bits */
    } else {
        bits ^= 0x80000000U; /* positive: invert sign bit */
    }
    return (uint64_t)bits << 32;
}

/* ---- Batch key helper (material+mesh → state compatibility) ---- */

static inline uint32_t nt_batch_key(uint32_t material_id, uint32_t mesh_id) { return (material_id * 0x9E3779B9U) ^ mesh_id; }

/* ---- Declared functions (implemented in nt_render_items.c) ---- */

float nt_calc_view_depth(uint32_t entity_id, const float view_pos[3], const float view_fwd[3]);

#endif /* NT_RENDER_ITEMS_H */
