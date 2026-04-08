/*
 * NFP Vector Packing — Minkowski-sum based No-Fit Polygon packer for
 * atlas sprite placement. Formerly inlined into nt_builder_atlas.c as
 * nt_builder_vector.inl; promoted to a proper module in Phase 162.
 */

/* clang-format off */
#include "nt_builder_atlas_vpack.h"
#include "nt_builder_atlas_geometry.h"
#include "nt_builder.h"             /* NT_BUILD_ASSERT */
#include "nt_clipper2_bridge.h"     /* nt_clipper2_minkowski_nfp */
#include "log/nt_log.h"             /* NT_LOG_INFO, NT_LOG_ERROR */
#include "time/nt_time.h"           /* nt_time_now */
#include "tinycthread.h"            /* mtx_t, cnd_t, thrd_t (parallel NFP build) */
/* clang-format on */

#include <math.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* --- Public helpers --- */

void pack_stats_reset(PackStats *stats) { memset(stats, 0, sizeof(*stats)); }

/* --- Area-descending sort entry (moved from tile_pack region) --- */

typedef struct {
    uint32_t index; /* index into sprites[] */
    uint32_t area;  /* trimmed_w * trimmed_h */
} AreaSortEntry;

static int area_sort_cmp(const void *a, const void *b) {
    const AreaSortEntry *ea = (const AreaSortEntry *)a;
    const AreaSortEntry *eb = (const AreaSortEntry *)b;
    if (ea->area > eb->area) {
        return -1;
    }
    if (ea->area < eb->area) {
        return 1;
    }
    if (ea->index < eb->index) {
        return -1;
    }
    if (ea->index > eb->index) {
        return 1;
    }
    return 0;
}

/* Negate polygon vertices (no reversal needed), pack int32 xy pairs for
 * Clipper2, and return the shape hash in one pass. 2D negation = 180deg
 * rotation, preserves winding (CCW stays CCW). */
static uint32_t vpack_negate_pack_xy_hash(const Point2D *in, uint32_t count, Point2D *out_poly, int32_t *out_xy) {
    uint32_t h = 2166136261u; /* FNV-1a offset basis */
    for (uint32_t i = 0; i < count; i++) {
        int32_t x = -in[i].x;
        int32_t y = -in[i].y;
        out_poly[i].x = x;
        out_poly[i].y = y;
        out_xy[i * 2] = x;
        out_xy[(i * 2) + 1] = y;
        h ^= (uint32_t)x;
        h *= 16777619u;
        h ^= (uint32_t)y;
        h *= 16777619u;
    }
    return h;
}

/* Pack int32 xy pairs for Clipper2 and return the shape hash in one pass. */
static uint32_t vpack_pack_xy_hash(const Point2D *in, uint32_t count, int32_t *out_xy) {
    uint32_t h = 2166136261u; /* FNV-1a offset basis */
    for (uint32_t i = 0; i < count; i++) {
        int32_t x = in[i].x;
        int32_t y = in[i].y;
        out_xy[i * 2] = x;
        out_xy[(i * 2) + 1] = y;
        h ^= (uint32_t)x;
        h *= 16777619u;
        h ^= (uint32_t)y;
        h *= 16777619u;
    }
    return h;
}

static void vpack_unpack_xy(const int32_t *in_xy, uint32_t count, Point2D *out_poly) {
    for (uint32_t i = 0; i < count; i++) {
        out_poly[i].x = in_xy[i * 2];
        out_poly[i].y = in_xy[(i * 2) + 1];
    }
}

/* Minkowski sum of two convex CCW polygons.
 * Classic merge-by-edge-angle algorithm. Both A and B must be CCW.
 * Result is CCW convex polygon with at most nA + nB vertices. */
static uint32_t vpack_minkowski(const Point2D *A, uint32_t nA, const Point2D *B, uint32_t nB, Point2D *out) {
    uint32_t i = 0, j = 0;
    for (uint32_t k = 1; k < nA; k++) {
        if (A[k].y < A[i].y || (A[k].y == A[i].y && A[k].x < A[i].x))
            i = k;
    }
    for (uint32_t k = 1; k < nB; k++) {
        if (B[k].y < B[j].y || (B[k].y == B[j].y && B[k].x < B[j].x))
            j = k;
    }

    uint32_t start_i = i, start_j = j;
    uint32_t count = 0;
    do {
        out[count].x = A[i].x + B[j].x;
        out[count].y = A[i].y + B[j].y;
        count++;

        Point2D edgeA = {A[(i + 1) % nA].x - A[i].x, A[(i + 1) % nA].y - A[i].y};
        Point2D edgeB = {B[(j + 1) % nB].x - B[j].x, B[(j + 1) % nB].y - B[j].y};

        int64_t cross = (int64_t)edgeA.x * edgeB.y - (int64_t)edgeA.y * edgeB.x;
        if (cross > 0) {
            i = (i + 1) % nA;
        } else if (cross < 0) {
            j = (j + 1) % nB;
        } else {
            i = (i + 1) % nA;
            j = (j + 1) % nB;
        }
    } while ((i != start_i || j != start_j) && count < nA + nB);
    return count;
}

static bool vpack_intersect(Point2D p1, Point2D p2, Point2D p3, Point2D p4, float *ox, float *oy) {
    int64_t s1_x = p2.x - p1.x;
    int64_t s1_y = p2.y - p1.y;
    int64_t s2_x = p4.x - p3.x;
    int64_t s2_y = p4.y - p3.y;

    int64_t d = -s2_x * s1_y + s1_x * s2_y;
    if (d == 0)
        return false;

    int64_t s_num = -s1_y * (p1.x - p3.x) + s1_x * (p1.y - p3.y);
    int64_t t_num = s2_x * (p1.y - p3.y) - s2_y * (p1.x - p3.x);

    if (d > 0) {
        if (s_num < 0 || s_num > d || t_num < 0 || t_num > d)
            return false;
    } else {
        if (s_num > 0 || s_num < d || t_num > 0 || t_num < d)
            return false;
    }

    *ox = (float)p1.x + (float)(t_num * s1_x) / (float)d;
    *oy = (float)p1.y + (float)(t_num * s1_y) / (float)d;
    return true;
}

/* Integer axis intersection: computes floor and ceil of crossing point.
 * Returns true if edge (p1->p2) crosses the line x=M (is_x_axis) or y=M (!is_x_axis).
 * out_floor/out_ceil: integer bounds of
 * the other coordinate at the crossing. */
static bool vpack_intersect_axis_i(Point2D p1, Point2D p2, bool is_x_axis, int32_t M, int32_t *out_floor, int32_t *out_ceil) {
    if (is_x_axis) {
        int32_t dx = p2.x - p1.x;
        if (dx == 0)
            return false;
        if (!((p1.x < M && p2.x >= M) || (p1.x >= M && p2.x < M)))
            return false;
        /* y = p1.y + (M - p1.x) * dy / dx - exact integer division with floor/ceil */
        int64_t num = (int64_t)(M - p1.x) * (p2.y - p1.y);
        int32_t base = p1.y + (int32_t)(num / dx);
        int64_t rem = num % dx;
        /* Adjust for truncation-toward-zero: need true floor and ceil */
        if (rem != 0 && ((rem < 0) != (dx < 0)))
            base--; /* truncation was ceiling, adjust to floor */
        *out_floor = base;
        *out_ceil = (rem == 0) ? base : base + 1;
    } else {
        int32_t dy = p2.y - p1.y;
        if (dy == 0)
            return false;
        if (!((p1.y < M && p2.y >= M) || (p1.y >= M && p2.y < M)))
            return false;
        int64_t num = (int64_t)(M - p1.y) * (p2.x - p1.x);
        int32_t base = p1.x + (int32_t)(num / dy);
        int64_t rem = num % dy;
        if (rem != 0 && ((rem < 0) != (dy < 0)))
            base--;
        *out_floor = base;
        *out_ceil = (rem == 0) ? base : base + 1;
    }
    return true;
}

/* Multi-ring NFP: outer rings (+1) describe forbidden zones; hole rings (-1)
 * describe pockets where the incoming polygon fits inside the placed polygon.
 * A point is "blocked" iff it lies in some outer ring but not inside any hole.
 * Limits sized to measured maxima (bigatlas: max_verts=41, max_rings=4) with
 * safety margin. Smaller entries → NFP cache fits L3 → large speedup on hot path. */
#define VPACK_NFP_MAX_RINGS 8
#define VPACK_NFP_MAX_VERTS 64
/* Multi-ring NFP: rings describe forbidden zones (one or more disjoint outer
 * boundaries for concave inputs). No hole-packing - sprite holes are not modeled
 * in pipeline_geometry, so NFPs never need pocket fitting.
 *
 * Layout puts AABB first so the broad-phase bounds check in the candidate scan
 * touches cache line 0 instead of fetching the last cache line of the struct. */
typedef struct {
    int32_t min_x, min_y, max_x, max_y;             /* 16B — hot: broad-phase bounds check */
    uint16_t ring_offsets[VPACK_NFP_MAX_RINGS + 1]; /* 18B — warm */
    uint8_t ring_count;                             /* 1B */
    Point2D verts[VPACK_NFP_MAX_VERTS];             /* 512B — cold: touched only after broad-phase passes */
} VPackNFP;

/* Even-odd ray cast point-in-polygon. Winding-independent. */
static bool vpack_point_in_ring(int32_t px, int32_t py, const Point2D *ring, uint32_t n) {
    bool inside = false;
    for (uint32_t i = 0, j = n - 1; i < n; j = i++) {
        if (((ring[i].y > py) != (ring[j].y > py)) && (px < (int32_t)(((int64_t)(ring[j].x - ring[i].x) * (int64_t)(py - ring[i].y)) / (int64_t)(ring[j].y - ring[i].y) + ring[i].x))) {
            inside = !inside;
        }
    }
    return inside;
}

/* Returns true iff (px, py) is blocked by this NFP - point lies inside any ring. */
static bool vpack_point_in_nfp(int32_t px, int32_t py, const VPackNFP *nfp) {
    for (uint8_t r = 0; r < nfp->ring_count; r++) {
        uint32_t start = nfp->ring_offsets[r];
        uint32_t end = nfp->ring_offsets[r + 1];
        uint32_t n = end - start;
        if (n < 3)
            continue;
        if (vpack_point_in_ring(px, py, &nfp->verts[start], n)) {
            return true;
        }
    }
    return false;
}

typedef struct {
    int32_t x, y;
} VPackCand;

/* Bounds for early candidate rejection (set per-sprite before candidate generation) */
typedef struct {
    int32_t min_x, min_y, max_x, max_y;
} VPackBounds;

typedef struct {
    uint64_t *seen_bits;
    uint32_t *dirty_words;
    uint32_t dirty_word_cap;
    uint32_t dirty_count;
    uint32_t max_size;
} VPackCandDedup;

static inline bool vpack_try_mark_cand_seen(int32_t x, int32_t y, VPackCandDedup *dedup) {
    if (!dedup || !dedup->seen_bits) {
        return true;
    }
    if ((uint32_t)x >= dedup->max_size || (uint32_t)y >= dedup->max_size) {
        NT_BUILD_ASSERT(false && "vpack: candidate out of dedup bounds");
        return false;
    }
    size_t bit_index = (size_t)(uint32_t)y * dedup->max_size + (uint32_t)x;
    uint32_t word_index = (uint32_t)(bit_index >> 6);
    uint64_t bit = (uint64_t)1 << (bit_index & 63);
    if (dedup->seen_bits[word_index] & bit) {
        return false;
    }
    if (dedup->seen_bits[word_index] == 0) {
        NT_BUILD_ASSERT(dedup->dirty_count < dedup->dirty_word_cap && "vpack: candidate dedup dirty overflow");
        dedup->dirty_words[dedup->dirty_count++] = word_index;
    }
    dedup->seen_bits[word_index] |= bit;
    return true;
}

static inline void vpack_clear_seen_cands(VPackCandDedup *dedup) {
    if (!dedup || !dedup->seen_bits) {
        return;
    }
    for (uint32_t i = 0; i < dedup->dirty_count; i++) {
        dedup->seen_bits[dedup->dirty_words[i]] = 0;
    }
    dedup->dirty_count = 0;
}

static inline void vpack_add_cand(VPackCand **cands, uint32_t *c_count, uint32_t *c_cap, int32_t x, int32_t y, const VPackBounds *b, VPackCandDedup *dedup) {
    if (x < b->min_x || y < b->min_y || x > b->max_x || y > b->max_y)
        return;
    if (!vpack_try_mark_cand_seen(x, y, dedup))
        return;
    if (*c_count >= *c_cap) {
        *c_cap = (*c_cap == 0) ? 1024 : (*c_cap * 2);
        *cands = (VPackCand *)realloc(*cands, *c_cap * sizeof(VPackCand));
    }
    (*cands)[*c_count].x = x;
    (*cands)[*c_count].y = y;
    (*c_count)++;
}

/* Inflated pack polygon: polygon_inflate may add vertices at concave splits,
 * so this is wider than the final <=max_vertices polygon stored in the atlas blob.
 * Capacity 32 matches
 * polygon_inflate's hard cap. */
#define VPACK_PLACED_MAX_VERTS 32
typedef struct {
    int32_t poly_xy[VPACK_PLACED_MAX_VERTS * 2];
    uint32_t count;
    int32_t x, y;
    int32_t aabb_min_x, aabb_min_y, aabb_max_x, aabb_max_y; /* polygon AABB (relative, not offset by x,y) */
    uint32_t shape_hash;
} VPackPlaced;

/* NFP cache: 4-way set-associative table keyed by (placed_shape_hash, incoming_shape_hash).
 * 16384 total entries @ ~570 B each ≈ 9.3 MB — fits L3. 4 ways gives better LRU
 * approximation than 2 ways at the same total footprint, reducing collisions. */
#define VPACK_NFP_CACHE_SIZE 65536U /* total entries; must be power of 2 */
#define VPACK_NFP_CACHE_WAYS 8U
#define VPACK_NFP_CACHE_SET_COUNT (VPACK_NFP_CACHE_SIZE / VPACK_NFP_CACHE_WAYS)
#define VPACK_NFP_CACHE_READ_RETRIES 4U
/* Cache entry stores NFP coordinates as int16 instead of int32. Atlas coords live
 * in [-4100, +4100] at worst, well within int16 range. Halves memory traffic on
 * the hit path (3.5M hits × 256 bytes saved). */
typedef struct {
    atomic_uint version;                       /* even = stable snapshot, odd = writer in progress */
    uint32_t key_a, key_b;                     /* shape hashes; both 0 = empty slot */
    int16_t verts_xy[VPACK_NFP_MAX_VERTS * 2]; /* interleaved x,y pairs */
    uint16_t ring_offsets[VPACK_NFP_MAX_RINGS + 1];
    uint8_t ring_count;
    int16_t min_x, min_y, max_x, max_y;
} VPackNFPCacheEntry;

/* Copy Point2D (int32) source to VPackNFP output, applying offset. Used for the
 * terminal store in vpack_compute_nfp_one when a freshly computed NFP is written
 * to out_nfp (no cache involvement). */
static void vpack_copy_local_nfp_to_out(const Point2D *verts, const uint16_t *ring_offsets, uint8_t ring_count, int32_t min_x, int32_t min_y, int32_t max_x, int32_t max_y, int32_t off_x,
                                        int32_t off_y, VPackNFP *out_nfp) {
    out_nfp->ring_count = ring_count;
    uint32_t total = ring_offsets[ring_count];
    for (uint8_t r = 0; r <= ring_count; r++) {
        out_nfp->ring_offsets[r] = ring_offsets[r];
    }
    for (uint32_t v = 0; v < total; v++) {
        out_nfp->verts[v].x = verts[v].x + off_x;
        out_nfp->verts[v].y = verts[v].y + off_y;
    }
    out_nfp->min_x = min_x + off_x;
    out_nfp->min_y = min_y + off_y;
    out_nfp->max_x = max_x + off_x;
    out_nfp->max_y = max_y + off_y;
}

/* Copy cache slot (int16) to VPackNFP output, applying offset. Hot hit path. */
static void vpack_copy_cache_slot_to_out(const VPackNFPCacheEntry *slot, int32_t off_x, int32_t off_y, VPackNFP *out_nfp) {
    uint8_t rc = slot->ring_count;
    out_nfp->ring_count = rc;
    uint32_t total = slot->ring_offsets[rc];
    for (uint8_t r = 0; r <= rc; r++) {
        out_nfp->ring_offsets[r] = slot->ring_offsets[r];
    }
    for (uint32_t v = 0; v < total; v++) {
        out_nfp->verts[v].x = (int32_t)slot->verts_xy[v * 2] + off_x;
        out_nfp->verts[v].y = (int32_t)slot->verts_xy[(v * 2) + 1] + off_y;
    }
    out_nfp->min_x = (int32_t)slot->min_x + off_x;
    out_nfp->min_y = (int32_t)slot->min_y + off_y;
    out_nfp->max_x = (int32_t)slot->max_x + off_x;
    out_nfp->max_y = (int32_t)slot->max_y + off_y;
}

static void vpack_store_local_nfp_in_cache(VPackNFPCacheEntry *slot, uint32_t key_a, uint32_t key_b, const Point2D *verts, const uint16_t *ring_offsets, uint8_t ring_count, int32_t min_x,
                                           int32_t min_y, int32_t max_x, int32_t max_y) {
    slot->ring_count = ring_count;
    for (uint8_t r = 0; r <= ring_count; r++) {
        slot->ring_offsets[r] = ring_offsets[r];
    }
    uint32_t total = ring_offsets[ring_count];
    for (uint32_t v = 0; v < total; v++) {
        /* Range-check: all atlas coords fit in int16. */
        NT_BUILD_ASSERT(verts[v].x >= INT16_MIN && verts[v].x <= INT16_MAX && "vpack: cache store x out of int16 range");
        NT_BUILD_ASSERT(verts[v].y >= INT16_MIN && verts[v].y <= INT16_MAX && "vpack: cache store y out of int16 range");
        slot->verts_xy[v * 2] = (int16_t)verts[v].x;
        slot->verts_xy[(v * 2) + 1] = (int16_t)verts[v].y;
    }
    slot->min_x = (int16_t)min_x;
    slot->min_y = (int16_t)min_y;
    slot->max_x = (int16_t)max_x;
    slot->max_y = (int16_t)max_y;
    slot->key_a = key_a;
    slot->key_b = key_b;
}

typedef enum {
    VPACK_CACHE_READ_EMPTY,
    VPACK_CACHE_READ_AVAILABLE,
    VPACK_CACHE_READ_OCCUPIED,
    VPACK_CACHE_READ_BUSY,
    VPACK_CACHE_READ_HIT,
} VPackCacheReadResult;

static VPackCacheReadResult vpack_try_read_nfp_cache_slot(const VPackNFPCacheEntry *slot, uint32_t key_a, uint32_t key_b, int32_t off_x, int32_t off_y, VPackNFP *out_nfp) {
    for (uint32_t attempt = 0; attempt < VPACK_NFP_CACHE_READ_RETRIES; attempt++) {
        uint32_t version0 = atomic_load_explicit(&slot->version, memory_order_acquire);
        if ((version0 & 1u) != 0) {
            continue;
        }
        uint32_t slot_key_a = slot->key_a;
        uint32_t slot_key_b = slot->key_b;
        bool occupied = slot_key_a != 0 || slot_key_b != 0;
        if (slot_key_a == key_a && slot_key_b == key_b) {
            vpack_copy_cache_slot_to_out(slot, off_x, off_y, out_nfp);
        }
        uint32_t version1 = atomic_load_explicit(&slot->version, memory_order_acquire);
        if (version0 == version1 && (version1 & 1u) == 0) {
            if (slot_key_a == key_a && slot_key_b == key_b) {
                return VPACK_CACHE_READ_HIT;
            }
            return occupied ? VPACK_CACHE_READ_OCCUPIED : VPACK_CACHE_READ_EMPTY;
        }
    }
    return VPACK_CACHE_READ_BUSY;
}

static VPackCacheReadResult vpack_read_nfp_cache_locked(const VPackNFPCacheEntry *set, uint32_t key_a, uint32_t key_b, int32_t off_x, int32_t off_y, VPackNFP *out_nfp) {
    bool saw_empty = false;
    bool saw_occupied = false;
    for (uint32_t way = 0; way < VPACK_NFP_CACHE_WAYS; way++) {
        const VPackNFPCacheEntry *slot = &set[way];
        bool slot_occupied = slot->key_a != 0 || slot->key_b != 0;
        if (slot->key_a == key_a && slot->key_b == key_b) {
            vpack_copy_cache_slot_to_out(slot, off_x, off_y, out_nfp);
            return VPACK_CACHE_READ_HIT;
        }
        if (slot_occupied) {
            saw_occupied = true;
        } else {
            saw_empty = true;
        }
    }
    if (saw_empty) {
        return saw_occupied ? VPACK_CACHE_READ_AVAILABLE : VPACK_CACHE_READ_EMPTY;
    }
    return VPACK_CACHE_READ_OCCUPIED;
}

static VPackCacheReadResult vpack_try_read_nfp_cache(const VPackNFPCacheEntry *set, uint32_t key_a, uint32_t key_b, int32_t off_x, int32_t off_y, VPackNFP *out_nfp) {
    for (uint32_t attempt = 0; attempt < VPACK_NFP_CACHE_READ_RETRIES; attempt++) {
        bool saw_busy = false;
        bool saw_empty = false;
        bool saw_occupied = false;
        for (uint32_t way = 0; way < VPACK_NFP_CACHE_WAYS; way++) {
            VPackCacheReadResult slot_result = vpack_try_read_nfp_cache_slot(&set[way], key_a, key_b, off_x, off_y, out_nfp);
            if (slot_result == VPACK_CACHE_READ_HIT) {
                return VPACK_CACHE_READ_HIT;
            }
            if (slot_result == VPACK_CACHE_READ_BUSY) {
                saw_busy = true;
            } else if (slot_result == VPACK_CACHE_READ_OCCUPIED) {
                saw_occupied = true;
            } else {
                saw_empty = true;
            }
        }
        if (!saw_busy) {
            if (saw_empty) {
                return saw_occupied ? VPACK_CACHE_READ_AVAILABLE : VPACK_CACHE_READ_EMPTY;
            }
            return VPACK_CACHE_READ_OCCUPIED;
        }
    }
    return VPACK_CACHE_READ_BUSY;
}

static VPackNFPCacheEntry *vpack_select_nfp_cache_slot_locked(VPackNFPCacheEntry *set, uint32_t key_a, uint32_t key_b) {
    VPackNFPCacheEntry *empty_slot = NULL;
    VPackNFPCacheEntry *victim = &set[0];
    uint32_t victim_version = atomic_load_explicit(&victim->version, memory_order_relaxed);
    for (uint32_t way = 0; way < VPACK_NFP_CACHE_WAYS; way++) {
        VPackNFPCacheEntry *slot = &set[way];
        if (slot->key_a == key_a && slot->key_b == key_b) {
            return slot;
        }
        if (!empty_slot && slot->key_a == 0 && slot->key_b == 0) {
            empty_slot = slot;
        }
        uint32_t version = atomic_load_explicit(&slot->version, memory_order_relaxed);
        if (version < victim_version) {
            victim = slot;
            victim_version = version;
        }
    }
    return empty_slot ? empty_slot : victim;
}

typedef struct {
    VPackPlaced *placed;
    uint32_t count;
    uint32_t used_w;
    uint32_t used_h;
} VPackPage;

#define VPACK_GRID_CELL 128
/* Each cell holds a bitmap of NFPs that overlap it. nfp_words grows with the
 * number of NFPs in the current orient. 64 * 64 = 4096 NFPs. Larger than
 * realistic workloads so grid path stays active. Measured: 32 is slightly
 * better for small workloads (<2048 relevant) but hits grid_fallbacks on
 * the full 4812 bigatlas, which regresses net. 64 is neutral-or-better on
 * both workloads. */
#define VPACK_GRID_WORDS 64

static void vpack_calc_aabb(const Point2D *poly, uint32_t count, int32_t *min_x, int32_t *min_y, int32_t *max_x, int32_t *max_y) {
    if (count == 0)
        return;
    *min_x = *max_x = poly[0].x;
    *min_y = *max_y = poly[0].y;
    for (uint32_t i = 1; i < count; i++) {
        if (poly[i].x < *min_x)
            *min_x = poly[i].x;
        if (poly[i].x > *max_x)
            *max_x = poly[i].x;
        if (poly[i].y < *min_y)
            *min_y = poly[i].y;
        if (poly[i].y > *max_y)
            *max_y = poly[i].y;
    }
}

static void vpack_init_single_ring_nfp(VPackNFP *nfp, const Point2D *ring, uint32_t count, int32_t off_x, int32_t off_y) {
    NT_BUILD_ASSERT(count >= 3 && count <= VPACK_NFP_MAX_VERTS && "vpack: single-ring NFP is invalid");
    nfp->ring_count = 1;
    nfp->ring_offsets[0] = 0;
    nfp->ring_offsets[1] = (uint16_t)count;
    for (uint32_t v = 0; v < count; v++) {
        nfp->verts[v].x = ring[v].x + off_x;
        nfp->verts[v].y = ring[v].y + off_y;
    }
    vpack_calc_aabb(nfp->verts, count, &nfp->min_x, &nfp->min_y, &nfp->max_x, &nfp->max_y);
}

static void vpack_add_nfp_candidates(const VPackNFP *nfp, int32_t min_cand_x, int32_t min_cand_y, VPackCand **cands, uint32_t *cand_count, uint32_t *cand_cap, const VPackBounds *bounds,
                                     VPackCandDedup *dedup) {
    for (uint32_t v = 0; v < nfp->ring_offsets[nfp->ring_count]; v++) {
        vpack_add_cand(cands, cand_count, cand_cap, nfp->verts[v].x, nfp->verts[v].y, bounds, dedup);
    }
    for (uint32_t r = 0; r < nfp->ring_count; r++) {
        uint32_t rs = nfp->ring_offsets[r];
        uint32_t re = nfp->ring_offsets[r + 1];
        uint32_t rn = re - rs;
        for (uint32_t e = 0; e < rn; e++) {
            uint32_t en = (e + 1 == rn) ? 0 : e + 1;
            Point2D pa = nfp->verts[rs + e];
            Point2D pb = nfp->verts[rs + en];
            int32_t vf;
            int32_t vc;
            if (vpack_intersect_axis_i(pa, pb, true, min_cand_x, &vf, &vc)) {
                vpack_add_cand(cands, cand_count, cand_cap, min_cand_x, vf, bounds, dedup);
                if (vc != vf) {
                    vpack_add_cand(cands, cand_count, cand_cap, min_cand_x, vc, bounds, dedup);
                }
            }
            if (vpack_intersect_axis_i(pa, pb, false, min_cand_y, &vf, &vc)) {
                vpack_add_cand(cands, cand_count, cand_cap, vf, min_cand_y, bounds, dedup);
                if (vc != vf) {
                    vpack_add_cand(cands, cand_count, cand_cap, vc, min_cand_y, bounds, dedup);
                }
            }
        }
    }
}

/* Per-thread accumulator for NFP build statistics. Merged into global stats after
 * the parallel section completes. Avoids atomic increments inside the hot loop. */
typedef struct {
    uint64_t or_count;
    uint64_t cache_hits;
    uint64_t cache_misses;
    uint64_t cache_collisions;
} VPackNFPBuildLocalStats;

/* Compute NFP for one (placed, orient_neg) pair. Writes to *out_nfp with placement
 * offset applied. Updates per-thread local stats. Cache accesses are protected by
 * cache_mtx (NULL = no locking,
 * sequential mode).
 *
 * Returns true on success (out_nfp populated), false on failure (caller skips item).
 *
 * This function is called both from sequential and parallel code paths. The result
 *
 * is bit-exact identical regardless of execution mode because:
 *  - cache hit checks and slot reads are serialized
 *  - cache writes publish a fully built local copy
 *  - Clipper2 NFP is a pure
 * function of inputs
 *  - per-thread stats are merged in deterministic order */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static bool vpack_compute_nfp_one(const VPackPlaced *pl_i, const Point2D *neg_poly, const int32_t *neg_xy, uint32_t neg_count, uint32_t neg_hash, VPackNFPCacheEntry *nfp_cache, mtx_t *cache_mtx,
                                  VPackNFP *out_nfp, VPackNFPBuildLocalStats *local_stats) {
    uint32_t cache_a = pl_i->shape_hash;
    uint32_t cache_b = neg_hash;
    /* Hash combine: golden-ratio multipliers then xor-shift mix to smear high
     * and low bits before masking. Helps reduce clustering when keys cluster
     * in either half of the 32-bit space. */
    uint32_t cache_mix = (cache_a * 2654435761U) ^ (cache_b * 1597334677U);
    cache_mix ^= cache_mix >> 16;
    cache_mix *= 0x85ebca6bU;
    cache_mix ^= cache_mix >> 13;
    uint32_t cache_set_idx = cache_mix & (VPACK_NFP_CACHE_SET_COUNT - 1);
    VPackNFPCacheEntry *cache_set = &nfp_cache[cache_set_idx * VPACK_NFP_CACHE_WAYS];

    /* Lock-free read via seqlock; on BUSY just fall through to compute. */
    VPackCacheReadResult cache_result = vpack_try_read_nfp_cache(cache_set, cache_a, cache_b, pl_i->x, pl_i->y, out_nfp);
    if (cache_result == VPACK_CACHE_READ_HIT) {
        local_stats->cache_hits++;
        return true;
    }
    local_stats->cache_misses++;
    (void)cache_mtx;

    Point2D local_verts[VPACK_NFP_MAX_VERTS];
    uint16_t local_ring_offsets[VPACK_NFP_MAX_RINGS + 1];
    uint8_t local_ring_count = 0;
    int32_t local_min_x = 0, local_min_y = 0, local_max_x = 0, local_max_y = 0;
    NT_BUILD_ASSERT(pl_i->count <= VPACK_PLACED_MAX_VERTS && "vpack: placed poly exceeds max verts");
    NT_BUILD_ASSERT(neg_count <= VPACK_PLACED_MAX_VERTS && "vpack: incoming poly exceeds max verts");

    int32_t *nfp_xy = NULL;
    uint32_t *nfp_ring_lengths = NULL;
    uint32_t nfp_ring_count = 0;
    uint32_t nfp_total_verts = nt_clipper2_minkowski_nfp(pl_i->poly_xy, pl_i->count, neg_xy, neg_count, &nfp_xy, &nfp_ring_lengths, &nfp_ring_count);
    local_stats->or_count++;

    bool nfp_ok = (nfp_total_verts >= 3 && nfp_xy && nfp_ring_count > 0 && nfp_ring_count <= VPACK_NFP_MAX_RINGS && nfp_total_verts <= VPACK_NFP_MAX_VERTS);
    if (nfp_ok) {
        local_ring_count = (uint8_t)nfp_ring_count;
        local_ring_offsets[0] = 0;
        uint32_t v_cursor = 0;
        for (uint32_t r = 0; r < nfp_ring_count; r++) {
            uint32_t rl = nfp_ring_lengths[r];
            for (uint32_t v = 0; v < rl; v++) {
                local_verts[v_cursor].x = nfp_xy[v_cursor * 2];
                local_verts[v_cursor].y = nfp_xy[(v_cursor * 2) + 1];
                v_cursor++;
            }
            local_ring_offsets[r + 1] = (uint16_t)v_cursor;
        }
        vpack_calc_aabb(local_verts, v_cursor, &local_min_x, &local_min_y, &local_max_x, &local_max_y);
    } else {
        /* Clipper2 NFP failed - fall back to convex hull pair (always valid). */
        Point2D placed_poly[VPACK_PLACED_MAX_VERTS];
        Point2D placed_hull[VPACK_PLACED_MAX_VERTS * 2];
        Point2D incoming_hull[VPACK_PLACED_MAX_VERTS * 2];
        Point2D convex_sum[VPACK_PLACED_MAX_VERTS * 2];
        Point2D convex_clean[VPACK_PLACED_MAX_VERTS * 2];
        vpack_unpack_xy(pl_i->poly_xy, pl_i->count, placed_poly);
        uint32_t placed_hull_count = convex_hull(placed_poly, pl_i->count, placed_hull);
        uint32_t incoming_hull_count = convex_hull(neg_poly, neg_count, incoming_hull);
        uint32_t convex_count = vpack_minkowski(placed_hull, placed_hull_count, incoming_hull, incoming_hull_count, convex_sum);
        convex_count = remove_collinear(convex_sum, convex_count, convex_clean);
        NT_BUILD_ASSERT(convex_count >= 3 && "vpack: convex NFP fallback failed");

        local_ring_count = 1;
        local_ring_offsets[0] = 0;
        local_ring_offsets[1] = (uint16_t)convex_count;
        for (uint32_t v = 0; v < convex_count; v++) {
            local_verts[v] = convex_clean[v];
        }
        vpack_calc_aabb(local_verts, convex_count, &local_min_x, &local_min_y, &local_max_x, &local_max_y);
        nfp_ok = true;
    }
    free(nfp_xy);
    free(nfp_ring_lengths);

    /* Lock-free write via CAS on the version word. If another writer owns the
     * slot (odd version or CAS fails), just skip — the other writer publishes
     * the same data (function of keys only), so the cache stays consistent. */
    {
        VPackNFPCacheEntry *slot = vpack_select_nfp_cache_slot_locked(cache_set, cache_a, cache_b);
        uint32_t version = atomic_load_explicit(&slot->version, memory_order_relaxed);
        if ((version & 1U) == 0) {
            if (atomic_compare_exchange_strong_explicit(&slot->version, &version, version + 1U, memory_order_acq_rel, memory_order_relaxed)) {
                vpack_store_local_nfp_in_cache(slot, cache_a, cache_b, local_verts, local_ring_offsets, local_ring_count, local_min_x, local_min_y, local_max_x, local_max_y);
                atomic_store_explicit(&slot->version, version + 2U, memory_order_release);
            }
        }
    }

    vpack_copy_local_nfp_to_out(local_verts, local_ring_offsets, local_ring_count, local_min_x, local_min_y, local_max_x, local_max_y, pl_i->x, pl_i->y, out_nfp);
    return true;
}

static uint64_t vpack_score_candidate(int32_t cx, int32_t cy, int32_t poly_max_x, int32_t poly_max_y, uint32_t cur_w, uint32_t cur_h, uint32_t margin, bool power_of_two) {
    uint32_t nw = (uint32_t)cx + (uint32_t)poly_max_x;
    uint32_t nh = (uint32_t)cy + (uint32_t)poly_max_y;
    if (nw < cur_w)
        nw = cur_w;
    if (nh < cur_h)
        nh = cur_h;
    nw += margin;
    nh += margin;
    if (power_of_two) {
        uint32_t pw = 1;
        while (pw < nw)
            pw <<= 1;
        uint32_t ph = 1;
        while (ph < nh)
            ph <<= 1;
        nw = pw;
        nh = ph;
    }
    return ((uint64_t)nw * nh << 16) | ((uint64_t)(cx + cy) & 0xFFFF);
}

static uint64_t vpack_page_lower_bound(const int32_t orient_aabb[8][4], const int32_t orient_min_cand[8][2], uint32_t orient_count, uint32_t page_used_w, uint32_t page_used_h, uint32_t margin,
                                       bool power_of_two) {
    uint64_t best = UINT64_MAX;
    for (uint32_t ori = 0; ori < orient_count; ori++) {
        uint64_t score = vpack_score_candidate(orient_min_cand[ori][0], orient_min_cand[ori][1], orient_aabb[ori][2], orient_aabb[ori][3], page_used_w, page_used_h, margin, power_of_two);
        if (score < best)
            best = score;
    }
    return best;
}

// #region Parallel candidate test thread pool
#define VPACK_PAR_MIN_CANDIDATES 1024

typedef struct {
    uint64_t score;
    uint32_t cand_index;
    uint64_t test_count;
} VPackParResult;

typedef struct {
    const VPackCand *cands;
    uint32_t cand_count;
    const VPackNFP *nfps;
    uint32_t nfp_count;
    uint32_t nfp_words;
    const uint64_t (*nfp_grid)[VPACK_GRID_WORDS];
    uint32_t grid_dim;
    bool use_grid;
    int32_t eff_min_x, eff_min_y, fast_max_x, fast_max_y;
    int32_t poly_max_x, poly_max_y;
    uint32_t used_w, used_h, margin;
    bool power_of_two;
} VPackScanCtx;

/* Batch type for thread pool dispatch. */
typedef enum {
    VPACK_BATCH_SCAN_CANDIDATES, /* parallel candidate testing */
    VPACK_BATCH_NFP_BUILD,       /* parallel NFP build (Clipper2 calls) */
} VPackBatchKind;

/* NFP build batch state. Workers compute one chunk of relevant items. */
typedef struct {
    const VPackPlaced *placed_arr; /* page->placed */
    const uint32_t *relevant_buf;  /* indices into placed_arr */
    uint32_t relevant_count;       /* number of items in batch */
    const Point2D (*orient_neg)[8][32];
    const int32_t (*orient_neg_xy)[8][VPACK_PLACED_MAX_VERTS * 2];
    const uint32_t *orient_counts;
    const uint32_t *orient_neg_hashes;
    uint32_t ori;
    VPackNFP *nfps_out;  /* indexed by ri (per-thread writes own ri) */
    bool *nfp_valid_out; /* per-ri validity flag */
    VPackNFPCacheEntry *nfp_cache;
    /* Per-thread local stats - main aggregates after batch completes */
    VPackNFPBuildLocalStats *thread_stats;
} VPackNFPBuildCtx;

typedef struct {
    /* Per-batch read-only state (set by main before signaling) */
    VPackBatchKind batch_kind;
    VPackScanCtx scan;
    VPackNFPBuildCtx nfp_build;
    /* Per-thread results (heap-allocated, sized to num_workers+1) - used by SCAN */
    VPackParResult *results;
    /* Sync: mutex+cond for sleep-wake (no spin) */
    mtx_t mtx;
    cnd_t cnd_work; /* main signals: work available */
    cnd_t cnd_done; /* workers signal: batch complete */
    /* Cache write mutex - separate from batch sync to avoid contention */
    mtx_t cache_mtx;
    uint32_t num_workers;
    uint32_t active_threads; /* 1..num_workers+1; excess threads skip work */
    uint32_t workers_done;
    uint32_t batch_seq;
    bool batch_ready;
    bool shutdown;
} VPackParCtx;

typedef struct {
    VPackParCtx *ctx;
    uint32_t tid;
} VPackWorkerArg;

static inline bool vpack_par_better(uint64_t score, uint32_t cand_index, uint64_t best_score, uint32_t best_cand_index) {
    return (score < best_score) || (score == best_score && cand_index < best_cand_index);
}

static void vpack_scan_candidate_range(const VPackScanCtx *scan, uint32_t start, uint32_t end, VPackParResult *out_result) {
    uint64_t local_best = out_result->score;
    uint32_t local_best_cand = out_result->cand_index;
    uint64_t local_tests = 0;
    for (uint32_t c = start; c < end; c++) {
        int32_t cx = scan->cands[c].x;
        int32_t cy = scan->cands[c].y;
        if (cx < scan->eff_min_x || cy < scan->eff_min_y || cx > scan->fast_max_x || cy > scan->fast_max_y)
            continue;
        uint64_t score = vpack_score_candidate(cx, cy, scan->poly_max_x, scan->poly_max_y, scan->used_w, scan->used_h, scan->margin, scan->power_of_two);
        if (score >= local_best)
            continue;
        bool safe = true;
        if (scan->use_grid && cx >= 0 && cy >= 0) {
            int32_t gcx = cx / VPACK_GRID_CELL;
            int32_t gcy = cy / VPACK_GRID_CELL;
            if (gcx < (int32_t)scan->grid_dim && gcy < (int32_t)scan->grid_dim) {
                const uint64_t *cell = scan->nfp_grid[gcy * (int32_t)scan->grid_dim + gcx];
                for (uint32_t w = 0; w < scan->nfp_words && safe; w++) {
                    uint64_t bits = cell[w];
                    while (bits) {
                        uint32_t bit_idx = (uint32_t)__builtin_ctzll(bits);
                        uint32_t i = w * 64 + bit_idx;
                        if (cx >= scan->nfps[i].min_x && cx <= scan->nfps[i].max_x && cy >= scan->nfps[i].min_y && cy <= scan->nfps[i].max_y) {
                            local_tests++;
                            if (vpack_point_in_nfp(cx, cy, &scan->nfps[i])) {
                                safe = false;
                                break;
                            }
                        }
                        bits &= bits - 1;
                    }
                }
            }
        } else {
            for (uint32_t i = 0; i < scan->nfp_count && safe; i++) {
                if (cx >= scan->nfps[i].min_x && cx <= scan->nfps[i].max_x && cy >= scan->nfps[i].min_y && cy <= scan->nfps[i].max_y) {
                    local_tests++;
                    if (vpack_point_in_nfp(cx, cy, &scan->nfps[i])) {
                        safe = false;
                    }
                }
            }
        }
        if (safe) {
            local_best = score;
            local_best_cand = c;
        }
    }
    out_result->score = local_best;
    out_result->cand_index = local_best_cand;
    out_result->test_count = local_tests;
}

static void vpack_par_process_chunks(VPackParCtx *ctx, uint32_t tid, VPackParResult *out_result) {
    uint32_t active = ctx->active_threads;
    if (tid >= active) {
        return;
    }
    uint32_t start = (uint32_t)(((uint64_t)ctx->scan.cand_count * tid) / active);
    uint32_t end = (uint32_t)(((uint64_t)ctx->scan.cand_count * (tid + 1)) / active);
    vpack_scan_candidate_range(&ctx->scan, start, end, out_result);
}

/* NFP build chunk processor - each thread handles its slice of relevant items.
 * Writes nfps_out[ri] for ri in [start, end). Per-thread stats accumulated locally. */
static void vpack_par_process_nfp_chunks(VPackParCtx *ctx, uint32_t tid) {
    VPackNFPBuildLocalStats *local = &ctx->nfp_build.thread_stats[tid];
    *local = (VPackNFPBuildLocalStats){0};
    /* Dynamic worker pool: dispatching to 32 threads for 10 items means 22
     * threads get nothing. Excess threads fast-path skip. */
    uint32_t active = ctx->active_threads;
    if (tid >= active) {
        return;
    }
    uint32_t start = (uint32_t)(((uint64_t)ctx->nfp_build.relevant_count * tid) / active);
    uint32_t end = (uint32_t)(((uint64_t)ctx->nfp_build.relevant_count * (tid + 1)) / active);
    uint32_t ori = ctx->nfp_build.ori;
    for (uint32_t ri = start; ri < end; ri++) {
        uint32_t i = ctx->nfp_build.relevant_buf[ri];
        const VPackPlaced *pl_i = &ctx->nfp_build.placed_arr[i];
        const Point2D *neg_poly = (*ctx->nfp_build.orient_neg)[ori];
        const int32_t *neg_xy = (*ctx->nfp_build.orient_neg_xy)[ori];
        uint32_t neg_count = ctx->nfp_build.orient_counts[ori];
        uint32_t neg_hash = ctx->nfp_build.orient_neg_hashes[ori];
        ctx->nfp_build.nfp_valid_out[ri] = vpack_compute_nfp_one(pl_i, neg_poly, neg_xy, neg_count, neg_hash, ctx->nfp_build.nfp_cache, &ctx->cache_mtx, &ctx->nfp_build.nfps_out[ri], local);
    }
}

static int vpack_par_worker(void *arg) {
    VPackWorkerArg *wa = (VPackWorkerArg *)arg;
    VPackParCtx *ctx = wa->ctx;
    uint32_t tid = wa->tid;
    uint32_t seen_batch_seq = 0;
    for (;;) {
        mtx_lock(&ctx->mtx);
        while ((!ctx->batch_ready || ctx->batch_seq == seen_batch_seq) && !ctx->shutdown)
            cnd_wait(&ctx->cnd_work, &ctx->mtx);
        if (ctx->shutdown) {
            mtx_unlock(&ctx->mtx);
            break;
        }
        seen_batch_seq = ctx->batch_seq;
        VPackBatchKind kind = ctx->batch_kind;
        mtx_unlock(&ctx->mtx);

        if (kind == VPACK_BATCH_SCAN_CANDIDATES) {
            VPackParResult result = ctx->results[tid];
            vpack_par_process_chunks(ctx, tid, &result);
            mtx_lock(&ctx->mtx);
            ctx->results[tid] = result;
            ctx->workers_done++;
            if (ctx->workers_done == ctx->num_workers) {
                ctx->batch_ready = false;
                cnd_signal(&ctx->cnd_done);
            }
            mtx_unlock(&ctx->mtx);
        } else { /* VPACK_BATCH_NFP_BUILD */
            vpack_par_process_nfp_chunks(ctx, tid);
            mtx_lock(&ctx->mtx);
            ctx->workers_done++;
            if (ctx->workers_done == ctx->num_workers) {
                ctx->batch_ready = false;
                cnd_signal(&ctx->cnd_done);
            }
            mtx_unlock(&ctx->mtx);
        }
    }
    return 0;
}
// #endregion

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static bool vpack_try_page(const VPackPage *page, const Point2D orient_neg[8][32], const int32_t orient_neg_xy[8][VPACK_PLACED_MAX_VERTS * 2], const uint32_t orient_counts[8],
                           const int32_t orient_aabb[8][4], const int32_t orient_min_cand[8][2], const int32_t orient_max_cand[8][2], uint32_t orient_count, int32_t worst_poly_min_x,
                           int32_t worst_poly_min_y, int32_t worst_poly_max_x, int32_t worst_poly_max_y, int32_t global_min_cand_x, int32_t global_min_cand_y, int32_t global_max_cand_x,
                           int32_t global_max_cand_y, uint32_t extrude, uint32_t margin, bool power_of_two, VPackNFP *nfps, uint64_t (*nfp_grid)[VPACK_GRID_WORDS], VPackCand **cands,
                           uint32_t *cand_cap, uint32_t *relevant_buf, bool *nfp_valid_buf, VPackNFPCacheEntry *nfp_cache, const uint32_t orient_neg_hashes[8], PackStats *stats,
                           uint64_t *io_best_score, uint32_t page_index, uint32_t grid_dim, uint32_t max_size, uint64_t *dirty_bits_buf, uint32_t *dirty_cells_buf, uint64_t *cand_seen_bits,
                           uint32_t *cand_dirty_words, uint32_t cand_seen_word_count, VPackParCtx *par, uint32_t *out_best_page, int32_t *out_best_x, int32_t *out_best_y, uint8_t *out_best_orient,
                           uint32_t *out_best_orient_idx) {
    uint32_t relevant_count = 0;
    for (uint32_t i = 0; i < page->count; i++) {
        int32_t est_min_x = page->placed[i].x + page->placed[i].aabb_min_x - worst_poly_max_x;
        int32_t est_max_x = page->placed[i].x + page->placed[i].aabb_max_x - worst_poly_min_x;
        int32_t est_min_y = page->placed[i].y + page->placed[i].aabb_min_y - worst_poly_max_y;
        int32_t est_max_y = page->placed[i].y + page->placed[i].aabb_max_y - worst_poly_min_y;
        if (est_max_x < global_min_cand_x || est_min_x > global_max_cand_x || est_max_y < global_min_cand_y || est_min_y > global_max_cand_y) {
            continue;
        }
        relevant_buf[relevant_count++] = i;
    }

    bool found_on_page = false;
    for (uint32_t ori = 0; ori < orient_count; ori++) {
        uint32_t cur_count = orient_counts[ori];
        int32_t poly_min_x = orient_aabb[ori][0];
        int32_t poly_min_y = orient_aabb[ori][1];
        int32_t poly_max_x = orient_aabb[ori][2];
        int32_t poly_max_y = orient_aabb[ori][3];
        int32_t min_cand_x = orient_min_cand[ori][0];
        int32_t min_cand_y = orient_min_cand[ori][1];
        int32_t max_cand_x = orient_max_cand[ori][0];
        int32_t max_cand_y = orient_max_cand[ori][1];

        uint32_t cand_count = 0;
        VPackBounds bounds = {min_cand_x, min_cand_y, max_cand_x, max_cand_y};
        VPackCandDedup cand_dedup = {
            .seen_bits = cand_seen_bits,
            .dirty_words = cand_dirty_words,
            .dirty_word_cap = cand_seen_word_count,
            .dirty_count = 0,
            .max_size = max_size,
        };
        vpack_add_cand(cands, &cand_count, cand_cap, min_cand_x, min_cand_y, &bounds, &cand_dedup);

        // #region NFP build phase - parallel or sequential
        /* Build NFPs for all relevant items into nfps[ri] (indexed by ri, deterministic).
         * Each item is independent - safe to parallelize.
         * Phase 2 (sequential) compacts valid
         * NFPs and generates candidates in ri order. */
        if (par && par->num_workers > 0 && relevant_count >= 4) {
            /* Parallel NFP build: dispatch to thread pool */
            mtx_lock(&par->mtx);
            par->batch_kind = VPACK_BATCH_NFP_BUILD;
            par->nfp_build.placed_arr = page->placed;
            par->nfp_build.relevant_buf = relevant_buf;
            par->nfp_build.relevant_count = relevant_count;
            par->nfp_build.orient_neg = (const Point2D(*)[8][32])orient_neg;
            par->nfp_build.orient_neg_xy = (const int32_t(*)[8][VPACK_PLACED_MAX_VERTS * 2]) orient_neg_xy;
            par->nfp_build.orient_counts = orient_counts;
            par->nfp_build.orient_neg_hashes = orient_neg_hashes;
            par->nfp_build.ori = ori;
            par->nfp_build.nfps_out = nfps;
            par->nfp_build.nfp_valid_out = nfp_valid_buf;
            par->nfp_build.nfp_cache = nfp_cache;
            par->workers_done = 0;
            /* Scale active threads with work: at least 1 item per thread,
             * capped at num_workers+1. Each Clipper2 call is ~50µs so we want
             * roughly >= 2 calls per thread to amortize dispatch/wake cost. */
            uint32_t nfp_active = relevant_count / 2;
            if (nfp_active < 1)
                nfp_active = 1;
            if (nfp_active > par->num_workers + 1)
                nfp_active = par->num_workers + 1;
            par->active_threads = nfp_active;
            par->batch_seq++;
            par->batch_ready = true;
            cnd_broadcast(&par->cnd_work);
            mtx_unlock(&par->mtx);

            /* Main thread also processes its chunk (tid=0) */
            vpack_par_process_nfp_chunks(par, 0);

            /* Wait for all workers to finish this batch */
            mtx_lock(&par->mtx);
            while (par->workers_done < par->num_workers)
                cnd_wait(&par->cnd_done, &par->mtx);
            mtx_unlock(&par->mtx);

            /* Aggregate per-thread stats into global stats */
            for (uint32_t t = 0; t <= par->num_workers; t++) {
                stats->or_count += par->nfp_build.thread_stats[t].or_count;
                stats->nfp_cache_hit_count += par->nfp_build.thread_stats[t].cache_hits;
                stats->nfp_cache_miss_count += par->nfp_build.thread_stats[t].cache_misses;
            }
        } else {
            /* Sequential NFP build (small relevant_count or no thread pool) */
            VPackNFPBuildLocalStats local = {0};
            for (uint32_t ri = 0; ri < relevant_count; ri++) {
                uint32_t i = relevant_buf[ri];
                const VPackPlaced *pl_i = &page->placed[i];
                nfp_valid_buf[ri] = vpack_compute_nfp_one(pl_i, orient_neg[ori], orient_neg_xy[ori], cur_count, orient_neg_hashes[ori], nfp_cache, NULL, &nfps[ri], &local);
            }
            stats->or_count += local.or_count;
            stats->nfp_cache_hit_count += local.cache_hits;
            stats->nfp_cache_miss_count += local.cache_misses;
        }

        /* Phase 2 (sequential): compact valid NFPs and generate candidates in ri order.
         * need_candidates is computed per-ri using io_best_score (constant during this orient). */
        uint32_t nfp_count = 0;
        for (uint32_t ri = 0; ri < relevant_count; ri++) {
            if (!nfp_valid_buf[ri]) {
                continue;
            }
            uint32_t i = relevant_buf[ri];

            bool need_candidates = true;
            if (*io_best_score != UINT64_MAX) {
                int32_t est_min_x = page->placed[i].x + page->placed[i].aabb_min_x - poly_max_x;
                int32_t est_min_y = page->placed[i].y + page->placed[i].aabb_min_y - poly_max_y;
                uint64_t opt_score = vpack_score_candidate((est_min_x > 0) ? est_min_x : 0, (est_min_y > 0) ? est_min_y : 0, poly_max_x, poly_max_y, page->used_w, page->used_h, margin, power_of_two);
                if (opt_score > *io_best_score) {
                    need_candidates = false;
                }
            }

            /* Compact: move valid NFP from nfps[ri] down to nfps[nfp_count] if needed */
            if (nfp_count != ri) {
                nfps[nfp_count] = nfps[ri];
            }
            if (need_candidates) {
                vpack_add_nfp_candidates(&nfps[nfp_count], min_cand_x, min_cand_y, cands, &cand_count, cand_cap, &bounds, &cand_dedup);
            }
            nfp_count++;
        }
        // #endregion

        uint32_t nfp_words = (nfp_count + 63) / 64;
        size_t dirty_cell_cap = (size_t)grid_dim * grid_dim;
        size_t dirty_count = 0;
        if (nfp_count > 0 && nfp_words <= VPACK_GRID_WORDS) {
            size_t dirty_words = (((size_t)grid_dim * grid_dim) + 63) / 64;
            memset(dirty_bits_buf, 0, dirty_words * sizeof(uint64_t));
            for (uint32_t n = 0; n < nfp_count; n++) {
                int32_t gx0 = nfps[n].min_x / VPACK_GRID_CELL;
                int32_t gy0 = nfps[n].min_y / VPACK_GRID_CELL;
                int32_t gx1 = nfps[n].max_x / VPACK_GRID_CELL;
                int32_t gy1 = nfps[n].max_y / VPACK_GRID_CELL;
                if (gx0 < 0) {
                    gx0 = 0;
                }
                if (gy0 < 0) {
                    gy0 = 0;
                }
                if (gx1 >= (int32_t)grid_dim) {
                    gx1 = (int32_t)grid_dim - 1;
                }
                if (gy1 >= (int32_t)grid_dim) {
                    gy1 = (int32_t)grid_dim - 1;
                }
                uint32_t word = n / 64;
                uint64_t bit = (uint64_t)1 << (n % 64);
                for (int32_t gy = gy0; gy <= gy1; gy++) {
                    for (int32_t gx = gx0; gx <= gx1; gx++) {
                        uint32_t ci = ((uint32_t)gy * grid_dim) + (uint32_t)gx;
                        uint32_t dw = ci / 64;
                        uint64_t db = (uint64_t)1 << (ci % 64);
                        if (!(dirty_bits_buf[dw] & db)) {
                            dirty_bits_buf[dw] |= db;
                            NT_BUILD_ASSERT(dirty_count < dirty_cell_cap && "vector_pack: dirty cell overflow");
                            dirty_cells_buf[dirty_count++] = ci;
                        }
                        nfp_grid[ci][word] |= bit;
                    }
                }
            }
        }
        bool use_grid = (nfp_count > 0 && nfp_words <= VPACK_GRID_WORDS);

        /* Pre-compute upper coordinate bounds from io_best_score for fast rejection */
        int32_t fast_max_x = max_cand_x;
        int32_t fast_max_y = max_cand_y;
        if (*io_best_score != UINT64_MAX && power_of_two) {
            uint32_t best_area = (uint32_t)(*io_best_score >> 16);
            /* Find max cx such that POT(cx+poly_max_x+margin) * POT(cur_h+margin) <= best_area */
            uint32_t fixed_h = page->used_h + margin;
            uint32_t pot_h = 1;
            while (pot_h < fixed_h)
                pot_h <<= 1;
            if (pot_h > 0) {
                uint32_t max_pot_w = best_area / pot_h;
                int32_t xb = (int32_t)max_pot_w - poly_max_x - (int32_t)margin;
                if (xb < fast_max_x)
                    fast_max_x = xb;
            }
            uint32_t fixed_w = page->used_w + margin;
            uint32_t pot_w = 1;
            while (pot_w < fixed_w)
                pot_w <<= 1;
            if (pot_w > 0) {
                uint32_t max_pot_h = best_area / pot_w;
                int32_t yb = (int32_t)max_pot_h - poly_max_y - (int32_t)margin;
                if (yb < fast_max_y)
                    fast_max_y = yb;
            }
        }

        /* Pre-compute effective min bounds from all lower-bound checks */
        int32_t eff_min_x = min_cand_x;
        if (-poly_min_x > eff_min_x)
            eff_min_x = -poly_min_x;
        if ((int32_t)extrude > eff_min_x)
            eff_min_x = (int32_t)extrude;
        int32_t eff_min_y = min_cand_y;
        if (-poly_min_y > eff_min_y)
            eff_min_y = -poly_min_y;
        if ((int32_t)extrude > eff_min_y)
            eff_min_y = (int32_t)extrude;
        VPackScanCtx scan = {
            .cands = *cands,
            .cand_count = cand_count,
            .nfps = nfps,
            .nfp_count = nfp_count,
            .nfp_words = nfp_words,
            .nfp_grid = (const uint64_t(*)[VPACK_GRID_WORDS])nfp_grid,
            .grid_dim = grid_dim,
            .use_grid = use_grid,
            .eff_min_x = eff_min_x,
            .eff_min_y = eff_min_y,
            .fast_max_x = fast_max_x,
            .fast_max_y = fast_max_y,
            .poly_max_x = poly_max_x,
            .poly_max_y = poly_max_y,
            .used_w = page->used_w,
            .used_h = page->used_h,
            .margin = margin,
            .power_of_two = power_of_two,
        };

        // #region Candidate testing (single-threaded or parallel)
        if (par && par->num_workers > 0 && cand_count >= VPACK_PAR_MIN_CANDIDATES) {
            /* Parallel: dispatch to thread pool */
            mtx_lock(&par->mtx);
            par->batch_kind = VPACK_BATCH_SCAN_CANDIDATES;
            par->scan = scan;
            for (uint32_t t = 0; t <= par->num_workers; t++) {
                par->results[t].score = *io_best_score;
                par->results[t].cand_index = UINT32_MAX;
                par->results[t].test_count = 0;
            }
            par->workers_done = 0;
            /* Scale active threads with candidate count; 128 candidates per
             * thread amortizes dispatch. */
            uint32_t scan_active = cand_count / 128;
            if (scan_active < 1)
                scan_active = 1;
            if (scan_active > par->num_workers + 1)
                scan_active = par->num_workers + 1;
            par->active_threads = scan_active;
            par->batch_seq++;
            par->batch_ready = true;
            cnd_broadcast(&par->cnd_work);
            mtx_unlock(&par->mtx);

            /* Main thread also processes chunks (tid=0) */
            VPackParResult main_result = par->results[0];
            vpack_par_process_chunks(par, 0, &main_result);
            par->results[0] = main_result;

            /* Wait for all workers to finish this batch */
            mtx_lock(&par->mtx);
            while (par->workers_done < par->num_workers)
                cnd_wait(&par->cnd_done, &par->mtx);
            mtx_unlock(&par->mtx);

            /* Reduce: find global best across all threads */
            uint64_t reduce_score = *io_best_score;
            uint32_t reduce_cand = UINT32_MAX;
            for (uint32_t t = 0; t <= par->num_workers; t++) {
                stats->test_count += par->results[t].test_count;
                if (par->results[t].cand_index == UINT32_MAX)
                    continue;
                if (vpack_par_better(par->results[t].score, par->results[t].cand_index, reduce_score, reduce_cand)) {
                    reduce_score = par->results[t].score;
                    reduce_cand = par->results[t].cand_index;
                }
            }
            if (reduce_cand != UINT32_MAX) {
                *io_best_score = reduce_score;
                *out_best_page = page_index;
                *out_best_x = (*cands)[reduce_cand].x;
                *out_best_y = (*cands)[reduce_cand].y;
                *out_best_orient = (uint8_t)ori;
                *out_best_orient_idx = ori;
                found_on_page = true;
            }
        } else {
            VPackParResult result = {.score = *io_best_score, .cand_index = UINT32_MAX, .test_count = 0};
            vpack_scan_candidate_range(&scan, 0, cand_count, &result);
            stats->test_count += result.test_count;
            if (result.cand_index != UINT32_MAX) {
                *io_best_score = result.score;
                *out_best_page = page_index;
                *out_best_x = (*cands)[result.cand_index].x;
                *out_best_y = (*cands)[result.cand_index].y;
                *out_best_orient = (uint8_t)ori;
                *out_best_orient_idx = ori;
                found_on_page = true;
            }
        }
        // #endregion

        for (size_t d = 0; d < dirty_count; d++) {
            memset(nfp_grid[dirty_cells_buf[d]], 0, sizeof(uint64_t[VPACK_GRID_WORDS]));
        }
        vpack_clear_seen_cands(&cand_dedup);

        if (found_on_page && *out_best_page == page_index && *out_best_x == min_cand_x && *out_best_y == min_cand_y) {
            break;
        }
    }

    return found_on_page;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
uint32_t vector_pack(const uint32_t *trim_w, const uint32_t *trim_h, Point2D **hull_verts, const uint32_t *hull_counts, uint32_t sprite_count, const nt_atlas_opts_t *opts,
                     AtlasPlacement *out_placements, uint32_t *out_page_count, uint32_t *out_page_w, uint32_t *out_page_h, PackStats *stats, uint32_t thread_count) {
    uint32_t extrude = opts->extrude;
    uint32_t padding = opts->padding;
    uint32_t margin = opts->margin;
    uint32_t max_size = opts->max_size;
    NT_LOG_INFO("  vector_pack: thread_count=%u, sprites=%u", thread_count, sprite_count);

    pack_stats_reset(stats);

    // #region Build exact pack polygons once (inflate by extrude+padding/2)
    float dilate = (float)extrude + ((float)padding * 0.5F);
    Point2D **inf_polys = (Point2D **)malloc(sprite_count * sizeof(Point2D *));
    uint32_t *inf_counts = (uint32_t *)malloc(sprite_count * sizeof(uint32_t));
    NT_BUILD_ASSERT(inf_polys && inf_counts && "vector_pack: alloc failed");
    for (uint32_t i = 0; i < sprite_count; i++) {
        /* polygon_inflate contract: output buffer must hold at least max(n, 32) entries
         * because Clipper2 inflate may insert extra vertices at concave splits. */
        size_t alloc_n = (hull_counts[i] > VPACK_PLACED_MAX_VERTS) ? hull_counts[i] : VPACK_PLACED_MAX_VERTS;
        inf_polys[i] = (Point2D *)malloc(alloc_n * sizeof(Point2D));
        NT_BUILD_ASSERT(inf_polys[i] && "vector_pack: alloc failed");
        if (dilate > 0.0F) {
            inf_counts[i] = polygon_inflate(hull_verts[i], hull_counts[i], dilate, inf_polys[i]);
        } else {
            memcpy(inf_polys[i], hull_verts[i], hull_counts[i] * sizeof(Point2D));
            inf_counts[i] = hull_counts[i];
        }
        NT_BUILD_ASSERT(inf_counts[i] <= VPACK_PLACED_MAX_VERTS && "vector_pack: inflated polygon exceeds VPACK_PLACED_MAX_VERTS");
    }
    // #endregion

    // #region Sort by area descending
    AreaSortEntry *sorted = (AreaSortEntry *)malloc(sprite_count * sizeof(AreaSortEntry));
    NT_BUILD_ASSERT(sorted && "vector_pack: alloc failed");
    for (uint32_t i = 0; i < sprite_count; i++) {
        sorted[i].index = i;
        sorted[i].area = trim_w[i] * trim_h[i];
    }
    qsort(sorted, sprite_count, sizeof(AreaSortEntry), area_sort_cmp);
    // #endregion

    VPackPlaced *placed = (VPackPlaced *)malloc(sprite_count * sizeof(VPackPlaced));
    /* With triangle decomposition: up to T_p * T_i NFPs per placed sprite.
     * For 8-vertex polygons: 6 tris each - 36 NFPs per pair. Allocate generously. */
    uint32_t max_nfps = sprite_count * 36 + 64;
    VPackNFP *nfps = (VPackNFP *)malloc(max_nfps * sizeof(VPackNFP));
    uint32_t cand_cap = 1024;
    VPackCand *cands = (VPackCand *)malloc(cand_cap * sizeof(VPackCand));
    NT_BUILD_ASSERT(placed && nfps && cands && "vector_pack: alloc failed");

    /* Pre-allocate NFP spatial grid on heap (reused across orientations+sprites) */
    uint32_t grid_dim = max_size / VPACK_GRID_CELL + 1;
    uint64_t(*nfp_grid)[VPACK_GRID_WORDS] = (uint64_t(*)[VPACK_GRID_WORDS])calloc((size_t)grid_dim * grid_dim, sizeof(uint64_t[VPACK_GRID_WORDS]));
    NT_BUILD_ASSERT(nfp_grid && "vector_pack: grid alloc failed");

    /* NFP cache: avoid recomputing Minkowski for identical shape pairs */
    VPackNFPCacheEntry *nfp_cache = (VPackNFPCacheEntry *)calloc(VPACK_NFP_CACHE_SIZE, sizeof(VPackNFPCacheEntry));
    NT_BUILD_ASSERT(nfp_cache && "vector_pack: cache alloc failed");
    for (uint32_t i = 0; i < VPACK_NFP_CACHE_SIZE; i++) {
        atomic_init(&nfp_cache[i].version, 0);
    }

    VPackPage pages[ATLAS_MAX_PAGES];
    memset(pages, 0, sizeof(pages));
    pages[0].placed = placed;
    uint32_t page_count = 1;

    uint32_t *relevant_buf = (uint32_t *)malloc(sprite_count * sizeof(uint32_t));
    bool *nfp_valid_buf = (bool *)malloc(sprite_count * sizeof(bool));
    NT_BUILD_ASSERT(relevant_buf && nfp_valid_buf && "vector_pack: alloc failed");

    size_t grid_cell_count = (size_t)grid_dim * grid_dim;
    size_t dirty_words = (grid_cell_count + 63) / 64;
    uint64_t *dirty_bits_buf = (uint64_t *)malloc(dirty_words * sizeof(uint64_t));
    NT_BUILD_ASSERT(dirty_bits_buf && "vector_pack: dirty_bits alloc failed");
    uint32_t *dirty_cells_buf = (uint32_t *)malloc(grid_cell_count * sizeof(uint32_t));
    NT_BUILD_ASSERT(dirty_cells_buf && "vector_pack: dirty_cells alloc failed");
    size_t cand_seen_bit_count = (size_t)max_size * max_size;
    uint32_t cand_seen_word_count = (uint32_t)((cand_seen_bit_count + 63) / 64);
    uint64_t *cand_seen_bits = (uint64_t *)calloc(cand_seen_word_count, sizeof(uint64_t));
    uint32_t *cand_dirty_words = (uint32_t *)malloc((size_t)cand_seen_word_count * sizeof(uint32_t));
    NT_BUILD_ASSERT(cand_seen_bits && cand_dirty_words && "vector_pack: candidate dedup alloc failed");

    // #region Thread pool for parallel candidate testing
    VPackParCtx par_ctx;
    memset(&par_ctx, 0, sizeof(par_ctx));
    thrd_t *par_threads = NULL;
    VPackWorkerArg *par_args = NULL;
    uint32_t num_workers = 0;
    if (thread_count > 1) {
        num_workers = thread_count - 1; /* main thread is also a worker (tid=0) */
        if (num_workers > 31)
            num_workers = 31; /* cap: more threads = more sync overhead per dispatch */
        par_ctx.num_workers = num_workers;
        par_ctx.results = (VPackParResult *)calloc(num_workers + 1, sizeof(VPackParResult));
        par_ctx.nfp_build.thread_stats = (VPackNFPBuildLocalStats *)calloc(num_workers + 1, sizeof(VPackNFPBuildLocalStats));
        NT_BUILD_ASSERT(par_ctx.results && par_ctx.nfp_build.thread_stats && "vector_pack: par results alloc failed");
        mtx_init(&par_ctx.mtx, mtx_plain);
        mtx_init(&par_ctx.cache_mtx, mtx_plain);
        cnd_init(&par_ctx.cnd_work);
        cnd_init(&par_ctx.cnd_done);
        par_threads = (thrd_t *)malloc(num_workers * sizeof(thrd_t));
        par_args = (VPackWorkerArg *)malloc(num_workers * sizeof(VPackWorkerArg));
        NT_BUILD_ASSERT(par_threads && par_args && "vector_pack: thread alloc failed");
        for (uint32_t t = 0; t < num_workers; t++) {
            par_args[t].ctx = &par_ctx;
            par_args[t].tid = t + 1;
            int rc = thrd_create(&par_threads[t], vpack_par_worker, &par_args[t]);
            NT_BUILD_ASSERT(rc == thrd_success && "vector_pack: worker thread create failed");
        }
        NT_LOG_INFO("  vector_pack: %u candidate-test workers + main thread", num_workers);
    }
    VPackParCtx *par = (num_workers > 0) ? &par_ctx : NULL;
    // #endregion

    for (uint32_t s = 0; s < sprite_count; s++) {
        uint32_t idx = sorted[s].index;
        uint32_t orient_count = opts->allow_rotate ? 8 : 1;

        /* Transform exact pack polygon for all orientations.
         * Orthogonal D4 transforms preserve the exact offset shape, so we can
         * reuse the once-built inflated polygon instead of
         * inflating per orientation. */
        Point2D orient_polys[8][32];
        uint32_t orient_counts[8];
        orient_counts[0] = inf_counts[idx];
        memcpy(orient_polys[0], inf_polys[idx], inf_counts[idx] * sizeof(Point2D));
        for (uint32_t r = 1; r < orient_count; r++) {
            polygon_transform(inf_polys[idx], inf_counts[idx], (uint8_t)r, (int32_t)trim_w[idx], (int32_t)trim_h[idx], orient_polys[r]);
            orient_counts[r] = inf_counts[idx];
        }

        // #region Deduplicate orientations (skip transforms that produce identical polygons)
        uint8_t orient_orig[8]; /* maps compacted index -> original 0-7 orientation flag */
        for (uint32_t r = 0; r < orient_count; r++) {
            orient_orig[r] = (uint8_t)r;
        }
        {
            uint32_t source_orient_count = orient_count;
            uint32_t dedup_count = 0;
            for (uint32_t r = 0; r < source_orient_count; r++) {
                bool dup = false;
                int32_t r_aabb[4];
                vpack_calc_aabb(orient_polys[r], orient_counts[r], &r_aabb[0], &r_aabb[1], &r_aabb[2], &r_aabb[3]);
                for (uint32_t p = 0; p < dedup_count && !dup; p++) {
                    if (orient_counts[p] != orient_counts[r]) {
                        continue;
                    }
                    int32_t p_aabb[4];
                    vpack_calc_aabb(orient_polys[p], orient_counts[p], &p_aabb[0], &p_aabb[1], &p_aabb[2], &p_aabb[3]);
                    bool same = true;
                    for (uint32_t v = 0; v < orient_counts[r] && same; v++) {
                        if ((orient_polys[r][v].x - r_aabb[0]) != (orient_polys[p][v].x - p_aabb[0]) || (orient_polys[r][v].y - r_aabb[1]) != (orient_polys[p][v].y - p_aabb[1])) {
                            same = false;
                        }
                    }
                    if (same) {
                        dup = true;
                    }
                }
                if (!dup) {
                    if (dedup_count != r) {
                        memcpy(orient_polys[dedup_count], orient_polys[r], orient_counts[r] * sizeof(Point2D));
                        orient_counts[dedup_count] = orient_counts[r];
                    }
                    orient_orig[dedup_count] = (uint8_t)r;
                    dedup_count++;
                }
            }
            orient_count = dedup_count;
        }
        // #endregion

        int32_t min_edge = (int32_t)margin > (int32_t)extrude ? (int32_t)margin : (int32_t)extrude;

        /* Try all orientations, pick best placement across all */
        bool found_any = false;
        uint32_t best_page = 0;
        int32_t best_x = 0;
        int32_t best_y = 0;
        uint8_t best_orient = 0;
        uint64_t best_score = UINT64_MAX;
        uint32_t best_orient_idx = 0; /* which orient_polys[] was used */

        /* Pre-compute per-orientation AABBs, bounds, negated polys */
        Point2D orient_neg[8][32];
        int32_t orient_neg_xy[8][VPACK_PLACED_MAX_VERTS * 2];
        uint32_t orient_neg_hashes[8];
        int32_t orient_aabb[8][4];     /* min_x, min_y, max_x, max_y */
        int32_t orient_min_cand[8][2]; /* min_cand_x, min_cand_y */
        int32_t orient_max_cand[8][2]; /* max_cand_x, max_cand_y */
        /* Worst-case AABB across all orientations for shared placed-sprite pre-filter */
        int32_t worst_poly_max_x = 0, worst_poly_max_y = 0;
        int32_t worst_poly_min_x = 0, worst_poly_min_y = 0;
        int32_t global_min_cand_x = INT32_MAX, global_min_cand_y = INT32_MAX;
        int32_t global_max_cand_x = 0, global_max_cand_y = 0;
        for (uint32_t ori = 0; ori < orient_count; ori++) {
            orient_neg_hashes[ori] = vpack_negate_pack_xy_hash(orient_polys[ori], orient_counts[ori], orient_neg[ori], orient_neg_xy[ori]);
            vpack_calc_aabb(orient_polys[ori], orient_counts[ori], &orient_aabb[ori][0], &orient_aabb[ori][1], &orient_aabb[ori][2], &orient_aabb[ori][3]);
            int32_t mcx = min_edge - orient_aabb[ori][0];
            if (mcx < min_edge)
                mcx = min_edge;
            int32_t mcy = min_edge - orient_aabb[ori][1];
            if (mcy < min_edge)
                mcy = min_edge;
            orient_min_cand[ori][0] = mcx;
            orient_min_cand[ori][1] = mcy;
            orient_max_cand[ori][0] = (int32_t)max_size - (int32_t)margin - orient_aabb[ori][2] - 1;
            orient_max_cand[ori][1] = (int32_t)max_size - (int32_t)margin - orient_aabb[ori][3] - 1;
            if (orient_aabb[ori][2] > worst_poly_max_x)
                worst_poly_max_x = orient_aabb[ori][2];
            if (orient_aabb[ori][3] > worst_poly_max_y)
                worst_poly_max_y = orient_aabb[ori][3];
            if (orient_aabb[ori][0] < worst_poly_min_x)
                worst_poly_min_x = orient_aabb[ori][0];
            if (orient_aabb[ori][1] < worst_poly_min_y)
                worst_poly_min_y = orient_aabb[ori][1];
            if (mcx < global_min_cand_x)
                global_min_cand_x = mcx;
            if (mcy < global_min_cand_y)
                global_min_cand_y = mcy;
            if (orient_max_cand[ori][0] > global_max_cand_x)
                global_max_cand_x = orient_max_cand[ori][0];
            if (orient_max_cand[ori][1] > global_max_cand_y)
                global_max_cand_y = orient_max_cand[ori][1];
        }

        double sprite_start = nt_time_now();
        uint32_t page_count_before = page_count;
        for (uint32_t pi = 0; pi < page_count_before; pi++) {
            if (best_score != UINT64_MAX) {
                uint64_t page_lb = vpack_page_lower_bound(orient_aabb, orient_min_cand, orient_count, pages[pi].used_w, pages[pi].used_h, margin, opts->power_of_two);
                if (page_lb >= best_score) {
                    continue;
                }
            }
            stats->page_scan_count++;
            if (vpack_try_page(&pages[pi], orient_neg, orient_neg_xy, orient_counts, orient_aabb, orient_min_cand, orient_max_cand, orient_count, worst_poly_min_x, worst_poly_min_y, worst_poly_max_x,
                               worst_poly_max_y, global_min_cand_x, global_min_cand_y, global_max_cand_x, global_max_cand_y, extrude, margin, opts->power_of_two, nfps, nfp_grid, &cands, &cand_cap,
                               relevant_buf, nfp_valid_buf, nfp_cache, orient_neg_hashes, stats, &best_score, pi, grid_dim, max_size, dirty_bits_buf, dirty_cells_buf, cand_seen_bits, cand_dirty_words,
                               cand_seen_word_count, par, &best_page, &best_x, &best_y, &best_orient, &best_orient_idx)) {
                found_any = true;
            }
        }

        if (!found_any) {
            if (page_count >= ATLAS_MAX_PAGES) {
                NT_LOG_ERROR("NFP packing failed: Out of pages (>=%u) for sprite %u!", ATLAS_MAX_PAGES, s);
                for (uint32_t r = s; r < sprite_count; r++) {
                    uint32_t ridx = sorted[r].index;
                    out_placements[ridx].sprite_index = ridx;
                    out_placements[ridx].page = 0;
                    out_placements[ridx].x = opts->margin;
                    out_placements[ridx].y = opts->margin;
                    out_placements[ridx].transform = 0;
                }
                break;
            }

            uint32_t new_page = page_count++;
            pages[new_page].placed = (VPackPlaced *)malloc(sprite_count * sizeof(VPackPlaced));
            NT_BUILD_ASSERT(pages[new_page].placed && "vector_pack: alloc failed");
            pages[new_page].count = 0;
            pages[new_page].used_w = 0;
            pages[new_page].used_h = 0;
            stats->page_new_count++;
            stats->page_scan_count++;
            found_any = vpack_try_page(&pages[new_page], orient_neg, orient_neg_xy, orient_counts, orient_aabb, orient_min_cand, orient_max_cand, orient_count, worst_poly_min_x, worst_poly_min_y,
                                       worst_poly_max_x, worst_poly_max_y, global_min_cand_x, global_min_cand_y, global_max_cand_x, global_max_cand_y, extrude, margin, opts->power_of_two, nfps,
                                       nfp_grid, &cands, &cand_cap, relevant_buf, nfp_valid_buf, nfp_cache, orient_neg_hashes, stats, &best_score, new_page, grid_dim, max_size, dirty_bits_buf,
                                       dirty_cells_buf, cand_seen_bits, cand_dirty_words, cand_seen_word_count, par, &best_page, &best_x, &best_y, &best_orient, &best_orient_idx);
            NT_BUILD_ASSERT(found_any && "vector_pack: empty page should accept placement");
        }

        // #region Save placement (using winning orientation's polygon)
        {
            Point2D *win_poly = orient_polys[best_orient_idx];
            uint32_t win_count = orient_counts[best_orient_idx];
            int32_t win_poly_max_x, win_poly_max_y, win_trash;
            vpack_calc_aabb(win_poly, win_count, &win_trash, &win_trash, &win_poly_max_x, &win_poly_max_y);

            if (pages[best_page].count > 0) {
                stats->page_existing_hit_count++;
            }

            VPackPlaced *pl = &pages[best_page].placed[pages[best_page].count];
            pl->count = win_count;
            pl->x = best_x;
            pl->y = best_y;
            pl->shape_hash = vpack_pack_xy_hash(win_poly, win_count, pl->poly_xy);
            pl->aabb_min_x = orient_aabb[best_orient_idx][0];
            pl->aabb_min_y = orient_aabb[best_orient_idx][1];
            pl->aabb_max_x = orient_aabb[best_orient_idx][2];
            pl->aabb_max_y = orient_aabb[best_orient_idx][3];
            pages[best_page].count++;

            out_placements[idx].sprite_index = idx;
            out_placements[idx].page = best_page;
            NT_BUILD_ASSERT(best_x >= (int32_t)extrude && best_y >= (int32_t)extrude && "vector_pack: placement too close to edge for extrude");
            out_placements[idx].x = (uint32_t)(best_x - (int32_t)extrude);
            out_placements[idx].y = (uint32_t)(best_y - (int32_t)extrude);
            out_placements[idx].transform = orient_orig[best_orient];

            if (best_x + win_poly_max_x > (int32_t)pages[best_page].used_w) {
                pages[best_page].used_w = (uint32_t)(best_x + win_poly_max_x);
            }
            if (best_y + win_poly_max_y > (int32_t)pages[best_page].used_h) {
                pages[best_page].used_h = (uint32_t)(best_y + win_poly_max_y);
            }
        }
        // #endregion

        /* Slow-sprite warning: replaces the per-sprite SLOW log that lived in tile_pack. */
        double sprite_elapsed = nt_time_now() - sprite_start;
        if (sprite_elapsed > 1.0) {
            NT_LOG_WARN("  SLOW sprite #%u/%u: %.1fs (idx=%u, page=%u)", s, sprite_count, sprite_elapsed, idx, best_page);
        }
    }

    // #region POT expansion
    *out_page_count = page_count;
    for (uint32_t i = 0; i < page_count; i++) {
        uint32_t final_w = pages[i].used_w + margin;
        uint32_t final_h = pages[i].used_h + margin;
        if (final_w > max_size) {
            final_w = max_size;
        }
        if (final_h > max_size) {
            final_h = max_size;
        }
        stats->frontier_area += (uint64_t)final_w * (uint64_t)final_h;
        if (opts->power_of_two) {
            uint32_t pot_w = 1;
            while (pot_w < final_w)
                pot_w <<= 1;
            uint32_t pot_h = 1;
            while (pot_h < final_h)
                pot_h <<= 1;
            final_w = pot_w;
            final_h = pot_h;
        }
        out_page_w[i] = final_w;
        out_page_h[i] = final_h;
    }
    // #endregion

    // #region Cleanup
    for (uint32_t i = 0; i < sprite_count; i++)
        free(inf_polys[i]);
    free(inf_polys);
    free(inf_counts);
    free(sorted);
    for (uint32_t i = 1; i < page_count; i++)
        free(pages[i].placed);
    free(placed);
    free(nfps);
    free(cands);
    free(nfp_grid);
    free(nfp_cache);
    free(relevant_buf);
    free(nfp_valid_buf);
    free(dirty_cells_buf);
    free(dirty_bits_buf);
    free(cand_dirty_words);
    free(cand_seen_bits);
    /* Shutdown thread pool */
    if (num_workers > 0) {
        mtx_lock(&par_ctx.mtx);
        par_ctx.shutdown = true;
        cnd_broadcast(&par_ctx.cnd_work);
        mtx_unlock(&par_ctx.mtx);
        for (uint32_t t = 0; t < num_workers; t++)
            thrd_join(par_threads[t], NULL);
        mtx_destroy(&par_ctx.mtx);
        mtx_destroy(&par_ctx.cache_mtx);
        cnd_destroy(&par_ctx.cnd_work);
        cnd_destroy(&par_ctx.cnd_done);
        free(par_ctx.results);
        free(par_ctx.nfp_build.thread_stats);
        free(par_threads);
        free(par_args);
    }
    // #endregion

    return sprite_count;
}

/* --- Test-access wrapper (vpack internals remain static) --- */

#ifdef NT_BUILDER_ATLAS_TEST_ACCESS
bool nt_atlas_test_vpack_point_in_nfp(const int32_t *verts_xy, uint32_t vert_count, const uint16_t *ring_offsets, uint32_t ring_count, int32_t px, int32_t py) {
    NT_BUILD_ASSERT(ring_count <= VPACK_NFP_MAX_RINGS && "nt_atlas_test_vpack_point_in_nfp: too many rings");
    NT_BUILD_ASSERT(vert_count <= VPACK_NFP_MAX_VERTS && "nt_atlas_test_vpack_point_in_nfp: too many vertices");

    VPackNFP nfp = {0};
    nfp.ring_count = (uint8_t)ring_count;
    for (uint32_t r = 0; r <= ring_count; r++) {
        nfp.ring_offsets[r] = ring_offsets[r];
    }
    for (uint32_t v = 0; v < vert_count; v++) {
        nfp.verts[v].x = verts_xy[v * 2];
        nfp.verts[v].y = verts_xy[(v * 2) + 1];
    }
    return vpack_point_in_nfp(px, py, &nfp);
}
#endif
