/* clang-format off */
#include "nt_builder_internal.h"
#include "hash/nt_hash.h"
#include "nt_atlas_format.h"
#include "time/nt_time.h"
#include "stb_image.h"
#include "stb_image_write.h"
/* clang-format on */

#include <math.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

/* --- Internal point type for geometry operations --- */

typedef struct {
    int32_t x, y;
} Point2D;

/* --- Alpha plane: extract dense 1-byte alpha from RGBA --- */

static uint8_t *alpha_plane_extract(const uint8_t *rgba, uint32_t w, uint32_t h) {
    uint32_t count = w * h;
    uint8_t *alpha = (uint8_t *)malloc(count);
    NT_BUILD_ASSERT(alpha && "alpha_plane_extract: alloc failed");
    for (uint32_t i = 0; i < count; i++) {
        alpha[i] = rgba[(i * 4) + 3];
    }
    return alpha;
}

/* --- Alpha trim: find tight bounding box of non-transparent pixels --- */
/* Operates on dense alpha plane (1 byte per pixel). */

static bool alpha_trim(const uint8_t *alpha, uint32_t w, uint32_t h, uint8_t threshold, uint32_t *out_x, uint32_t *out_y, uint32_t *out_w, uint32_t *out_h) {
    uint32_t min_x = w;
    uint32_t min_y = h;
    uint32_t max_x = 0;
    uint32_t max_y = 0;
    bool found = false;

    for (uint32_t y = 0; y < h; y++) {
        for (uint32_t x = 0; x < w; x++) {
            // NOLINTNEXTLINE(clang-analyzer-core.UndefinedBinaryOperatorResult)
            if (alpha[(y * w) + x] >= threshold) {
                if (x < min_x) {
                    min_x = x;
                }
                if (x > max_x) {
                    max_x = x;
                }
                if (y < min_y) {
                    min_y = y;
                }
                if (y > max_y) {
                    max_y = y;
                }
                found = true;
            }
        }
    }

    if (!found) {
        return false;
    }

    *out_x = min_x;
    *out_y = min_y;
    *out_w = max_x - min_x + 1;
    *out_h = max_y - min_y + 1;
    return true;
}

/* --- 2D cross product for hull orientation --- */

static int64_t cross2d(Point2D o, Point2D a, Point2D b) { return ((int64_t)(a.x - o.x) * (int64_t)(b.y - o.y)) - ((int64_t)(a.y - o.y) * (int64_t)(b.x - o.x)); }

/* --- qsort comparator: sort by x, then by y --- */

static int point2d_cmp(const void *a, const void *b) {
    const Point2D *pa = (const Point2D *)a;
    const Point2D *pb = (const Point2D *)b;
    if (pa->x != pb->x) {
        return (pa->x < pb->x) ? -1 : 1;
    }
    if (pa->y != pb->y) {
        return (pa->y < pb->y) ? -1 : 1;
    }
    return 0;
}

/* --- Convex hull: Andrew's monotone chain algorithm --- */

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static uint32_t convex_hull(const Point2D *pts, uint32_t n, Point2D *out) {
    if (n == 0) {
        return 0;
    }
    if (n == 1) {
        out[0] = pts[0];
        return 1;
    }
    if (n == 2) {
        out[0] = pts[0];
        out[1] = pts[1];
        return 2;
    }

    /* Sort input points (need mutable copy) */
    Point2D *sorted = (Point2D *)malloc(n * sizeof(Point2D));
    NT_BUILD_ASSERT(sorted && "convex_hull: alloc failed");
    memcpy(sorted, pts, n * sizeof(Point2D));
    qsort(sorted, n, sizeof(Point2D), point2d_cmp);

    /* Build hull in 'out' buffer. Max hull size is 2*n. */
    uint32_t k = 0;

    // #region Lower hull
    for (uint32_t i = 0; i < n; i++) {
        while (k >= 2 && cross2d(out[k - 2], out[k - 1], sorted[i]) <= 0) {
            k--;
        }
        out[k++] = sorted[i];
    }
    // #endregion

    // #region Upper hull
    uint32_t lower_size = k + 1; /* +1 because we'll decrement after loop */
    for (uint32_t i = n - 1; i > 0; i--) {
        uint32_t ii = i - 1;
        while (k >= lower_size && cross2d(out[k - 2], out[k - 1], sorted[ii]) <= 0) {
            k--;
        }
        out[k++] = sorted[ii];
    }
    // #endregion

    free(sorted);

    /* Remove last point (duplicate of first) */
    k--;
    return k;
}

/* --- Convex hull simplification: min-area vertex removal --- */
/* Iteratively remove the vertex whose removal adds the smallest triangle area.
 * For convex polygons, removing a vertex always makes the polygon LARGER —
 * the edge between neighbors passes OUTSIDE the removed vertex.
 * Result is guaranteed to fully contain the original hull. */

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static uint32_t hull_simplify(const Point2D *hull, uint32_t n, uint32_t max_vertices, Point2D *out) {
    if (n <= max_vertices) {
        memcpy(out, hull, n * sizeof(Point2D));
        return n;
    }

    /* Boolean keep[] array -- start with all vertices kept */
    bool *keep = (bool *)malloc(n * sizeof(bool));
    NT_BUILD_ASSERT(keep && "hull_simplify: alloc failed");
    for (uint32_t i = 0; i < n; i++) {
        keep[i] = true;
    }

    uint32_t current_count = n;

    // #region Iterative vertex removal by minimum triangle area
    while (current_count > max_vertices) {
        double min_area = 1e30;
        uint32_t min_idx = 0;

        for (uint32_t i = 0; i < n; i++) {
            if (!keep[i]) {
                continue;
            }

            /* Find prev and next kept vertices (wrap around for closed hull) */
            uint32_t prev = i;
            do {
                prev = (prev == 0) ? n - 1 : prev - 1;
            } while (!keep[prev] && prev != i);

            uint32_t next = i;
            do {
                next = (next + 1) % n;
            } while (!keep[next] && next != i);

            if (prev == i || next == i) {
                continue;
            }

            /* Triangle area = 0.5 * |cross(prev->i, prev->next)| */
            double area = fabs((double)(hull[i].x - hull[prev].x) * (double)(hull[next].y - hull[prev].y) - (double)(hull[i].y - hull[prev].y) * (double)(hull[next].x - hull[prev].x));
            if (area < min_area) {
                min_area = area;
                min_idx = i;
            }
        }

        keep[min_idx] = false;
        current_count--;
    }
    // #endregion

    /* Collect kept vertices in order */
    uint32_t count = 0;
    for (uint32_t i = 0; i < n; i++) {
        if (keep[i]) {
            out[count++] = hull[i];
        }
    }

    free(keep);
    return count;
}

/* --- Fan triangulation from vertex 0 --- */

static uint32_t fan_triangulate(uint32_t vertex_count, uint16_t *indices) {
    if (vertex_count < 3) {
        return 0;
    }
    uint32_t tri_count = vertex_count - 2;
    for (uint32_t i = 0; i < tri_count; i++) {
        indices[(i * 3) + 0] = 0;
        indices[(i * 3) + 1] = (uint16_t)(i + 1);
        indices[(i * 3) + 2] = (uint16_t)(i + 2);
    }
    return tri_count;
}

/* --- Tile packer types --- */

/* Maximum number of atlas pages (each page = one texture) */
#ifndef ATLAS_MAX_PAGES
#define ATLAS_MAX_PAGES 64
#endif

/* Sprite placement result after packing */
typedef struct {
    uint32_t sprite_index; /* index into original sprite array */
    uint32_t page;         /* which atlas page (0-based) */
    uint32_t x, y;         /* placement position in atlas (top-left of cell including extrude) */
    uint32_t trimmed_w;    /* trimmed sprite width */
    uint32_t trimmed_h;    /* trimmed sprite height */
    uint32_t trim_x;       /* trim offset from source image left */
    uint32_t trim_y;       /* trim offset from source image top */
    uint8_t rotation;      /* 0=none, 1=90CW, 2=180, 3=270CW */
} AtlasPlacement;

/* Tile grid: bitset storage, 1 bit per tile cell.
 * Each row is an array of uint64_t words (row_words = ceil(tw/64)).
 * All packing operates at tile resolution for speed.
 * tile_size (e.g. 8) means each cell covers tile_size x tile_size pixels. */
typedef struct {
    uint64_t *rows;     /* row_words * th uint64_t words, row-major bitset */
    uint32_t tw;        /* grid width in tiles */
    uint32_t th;        /* grid height in tiles */
    uint32_t row_words; /* ceil(tw / 64) — uint64_t words per row */
    uint32_t tile_size; /* pixels per tile side */
} TileGrid;

/* Sort entry for area-descending sort (ATLAS-03) */
typedef struct {
    uint32_t index; /* index into sprites[] */
    uint32_t area;  /* trimmed_w * trimmed_h */
} AreaSortEntry;

/* --- Tile grid functions --- */

static TileGrid tgrid_create(uint32_t tw, uint32_t th, uint32_t tile_size) {
    TileGrid g;
    g.tw = tw;
    g.th = th;
    g.row_words = (tw + 63) / 64;
    g.tile_size = tile_size;
    g.rows = (uint64_t *)calloc((size_t)g.row_words * th, sizeof(uint64_t));
    NT_BUILD_ASSERT(g.rows && "tgrid_create: alloc failed");
    return g;
}

static void tgrid_free(TileGrid *g) {
    free(g->rows);
    g->rows = NULL;
    g->tw = 0;
    g->th = 0;
    g->row_words = 0;
}

static inline uint8_t tgrid_get(const TileGrid *g, uint32_t tx, uint32_t ty) { return (uint8_t)((g->rows[((size_t)ty * g->row_words) + (tx >> 6)] >> (tx & 63)) & 1); }

static inline void tgrid_set(TileGrid *g, uint32_t tx, uint32_t ty) { g->rows[((size_t)ty * g->row_words) + (tx >> 6)] |= (1ULL << (tx & 63)); }

/* Grow tile grid by doubling the smaller dimension. Copies existing cells.
 * Returns old dimensions via out_old_tw/out_old_th for priority-area scanning. */
static void tgrid_grow(TileGrid *g, uint32_t max_tw, uint32_t max_th, uint32_t *out_old_tw, uint32_t *out_old_th) {
    uint32_t old_tw = g->tw;
    uint32_t old_th = g->th;
    uint32_t old_rw = g->row_words;
    *out_old_tw = old_tw;
    *out_old_th = old_th;

    /* Double the smaller dimension (or width if equal) */
    uint32_t new_tw = (old_tw <= old_th) ? old_tw * 2 : old_tw;
    uint32_t new_th = (old_tw <= old_th) ? old_th : old_th * 2;
    if (new_tw > max_tw) {
        new_tw = max_tw;
    }
    if (new_th > max_th) {
        new_th = max_th;
    }
    uint32_t new_rw = (new_tw + 63) / 64;

    uint64_t *new_rows = (uint64_t *)calloc((size_t)new_rw * new_th, sizeof(uint64_t));
    NT_BUILD_ASSERT(new_rows && "tgrid_grow: alloc failed");

    /* Copy old rows (word count may differ if width changed) */
    uint32_t copy_words = (old_rw < new_rw) ? old_rw : new_rw;
    for (uint32_t y = 0; y < old_th; y++) {
        memcpy(new_rows + ((size_t)y * new_rw), g->rows + ((size_t)y * old_rw), copy_words * sizeof(uint64_t));
    }

    free(g->rows);
    g->rows = new_rows;
    g->tw = new_tw;
    g->th = new_th;
    g->row_words = new_rw;
}

/* --- Round up to next power of two --- */

static uint32_t next_pot(uint32_t v) {
    if (v == 0) {
        return 1;
    }
    v--;
    v |= v >> 1;
    v |= v >> 2;
    v |= v >> 4;
    v |= v >> 8;
    v |= v >> 16;
    return v + 1;
}

/* --- Quadtree acceleration for tile grid occupancy --- */

/* Quadtree overlays a TileGrid to track occupancy at multiple scales.
 * Leaf nodes correspond to BLOCK_SIZE x BLOCK_SIZE tile blocks.
 * Each internal node summarizes 4 children as EMPTY/MIXED/FULL.
 * Used to skip fully-occupied regions in O(1) during scan_region. */

#define QTREE_BLOCK_SHIFT 3                        /* log2(BLOCK_SIZE) */
#define QTREE_BLOCK_SIZE (1U << QTREE_BLOCK_SHIFT) /* 8 tiles per leaf block */

#define QTREE_EMPTY 0
#define QTREE_MIXED 1
#define QTREE_FULL 2

typedef struct {
    uint8_t *nodes;   /* EMPTY=0, MIXED=1, FULL=2 per node */
    uint32_t levels;  /* number of levels (0 = leaf-only) */
    uint32_t leaf_bw; /* leaf grid width  (blocks across) */
    uint32_t leaf_bh; /* leaf grid height (blocks down)   */
    uint32_t dim;     /* padded power-of-two dimension in blocks (max(leaf_bw,leaf_bh) rounded up) */
    uint32_t node_count;
} QuadTree;

/* Total nodes in a complete quadtree: sum of 4^0 + 4^1 + ... + 4^(levels-1).
 * Equals (4^levels - 1) / 3. */
static uint32_t qtree_total_nodes(uint32_t levels) {
    if (levels == 0) {
        return 0;
    }
    /* 4^levels via shift: 1 << (2*levels) */
    uint32_t pow4 = 1U << (2U * levels);
    return (pow4 - 1) / 3;
}

/* Compute number of quadtree levels for a dim x dim leaf grid.
 * levels = ceil(log2(dim)) + 1 (so root covers everything). */
static uint32_t qtree_compute_levels(uint32_t dim) {
    if (dim <= 1) {
        return 1;
    }
    uint32_t l = 0;
    uint32_t v = dim - 1;
    while (v > 0) {
        v >>= 1;
        l++;
    }
    return l + 1; /* +1 because level 0 = root, level (levels-1) = leaves */
}

/* Offset of the first node at a given level (0 = root). */
static uint32_t qtree_level_offset(uint32_t level) {
    if (level == 0) {
        return 0;
    }
    return (((uint32_t)1 << (2U * level)) - 1) / 3;
}

/* Create quadtree for a grid of tw x th tiles. */
static QuadTree qtree_create(uint32_t tw, uint32_t th) {
    QuadTree qt;
    qt.leaf_bw = (tw + QTREE_BLOCK_SIZE - 1) >> QTREE_BLOCK_SHIFT;
    qt.leaf_bh = (th + QTREE_BLOCK_SIZE - 1) >> QTREE_BLOCK_SHIFT;
    uint32_t max_dim = (qt.leaf_bw > qt.leaf_bh) ? qt.leaf_bw : qt.leaf_bh;
    /* Round up to power of two so the tree is complete */
    qt.dim = next_pot(max_dim);
    if (qt.dim == 0) {
        qt.dim = 1;
    }
    qt.levels = qtree_compute_levels(qt.dim);
    qt.node_count = qtree_total_nodes(qt.levels);
    NT_BUILD_ASSERT(qt.node_count > 0 && "qtree_create: zero nodes");
    // NOLINTNEXTLINE(clang-analyzer-optin.portability.UnixAPI)
    qt.nodes = (uint8_t *)calloc(qt.node_count, 1); /* all EMPTY */
    NT_BUILD_ASSERT(qt.nodes && "qtree_create: alloc failed");
    return qt;
}

static void qtree_free(QuadTree *qt) {
    free(qt->nodes);
    qt->nodes = NULL;
    qt->node_count = 0;
}

/* Classify a leaf block from the tile grid bitset.
 * Block (bx, by) covers tiles [bx*8 .. bx*8+7] x [by*8 .. by*8+7]. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static uint8_t qtree_classify_block(const TileGrid *g, uint32_t bx, uint32_t by) {
    uint32_t tx0 = bx << QTREE_BLOCK_SHIFT;
    uint32_t ty0 = by << QTREE_BLOCK_SHIFT;
    uint32_t tx1 = tx0 + QTREE_BLOCK_SIZE;
    uint32_t ty1 = ty0 + QTREE_BLOCK_SIZE;
    if (tx1 > g->tw) {
        tx1 = g->tw;
    }
    if (ty1 > g->th) {
        ty1 = g->th;
    }

    /* Out-of-bounds blocks (after grid) are EMPTY */
    if (tx0 >= g->tw || ty0 >= g->th) {
        return QTREE_EMPTY;
    }

    bool has_set = false;
    bool has_clear = false;

    for (uint32_t ty = ty0; ty < ty1; ty++) {
        const uint64_t *row = g->rows + ((size_t)ty * g->row_words);
        /* Check tiles [tx0..tx1) in this row */
        uint32_t w0 = tx0 >> 6;
        uint32_t b0 = tx0 & 63;
        uint32_t w1 = (tx1 - 1) >> 6;

        if (w0 == w1) {
            /* All bits in one word */
            uint32_t count = tx1 - tx0;
            uint64_t mask = (count < 64) ? ((1ULL << count) - 1) : UINT64_MAX;
            uint64_t bits = (row[w0] >> b0) & mask;
            if (bits != 0) {
                has_set = true;
            }
            if (bits != mask) {
                has_clear = true;
            }
        } else {
            /* First word */
            uint64_t first_mask = UINT64_MAX << b0;
            uint64_t first_bits = row[w0] & first_mask;
            if (first_bits != 0) {
                has_set = true;
            }
            if (first_bits != first_mask) {
                has_clear = true;
            }
            /* Middle words (full) */
            for (uint32_t w = w0 + 1; w < w1; w++) {
                if (row[w] != 0) {
                    has_set = true;
                }
                if (row[w] != UINT64_MAX) {
                    has_clear = true;
                }
            }
            /* Last word */
            uint32_t b1 = ((tx1 - 1) & 63) + 1;
            uint64_t last_mask = (b1 < 64) ? ((1ULL << b1) - 1) : UINT64_MAX;
            uint64_t last_bits = row[w1] & last_mask;
            if (last_bits != 0) {
                has_set = true;
            }
            if (last_bits != last_mask) {
                has_clear = true;
            }
        }
        if (has_set && has_clear) {
            return QTREE_MIXED;
        }
    }

    if (has_set && !has_clear) {
        return QTREE_FULL;
    }
    if (!has_set) {
        return QTREE_EMPTY;
    }
    return QTREE_MIXED;
}

/* Rebuild the entire quadtree from the tile grid (used after growth). */
static void qtree_rebuild(QuadTree *qt, const TileGrid *g) {
    /* Classify all leaf blocks */
    uint32_t leaf_off = qtree_level_offset(qt->levels - 1);
    uint32_t leaf_dim = qt->dim; /* leaves span dim x dim */

    for (uint32_t by = 0; by < leaf_dim; by++) {
        for (uint32_t bx = 0; bx < leaf_dim; bx++) {
            uint32_t idx = leaf_off + (by * leaf_dim) + bx;
            if (bx < qt->leaf_bw && by < qt->leaf_bh) {
                qt->nodes[idx] = qtree_classify_block(g, bx, by);
            } else {
                qt->nodes[idx] = QTREE_EMPTY; /* padding */
            }
        }
    }

    /* Propagate bottom-up */
    for (int32_t lev = (int32_t)qt->levels - 2; lev >= 0; lev--) {
        uint32_t off = qtree_level_offset((uint32_t)lev);
        uint32_t child_off = qtree_level_offset((uint32_t)lev + 1);
        uint32_t side = (uint32_t)1 << (uint32_t)lev; /* nodes per side at this level */
        for (uint32_t ny = 0; ny < side; ny++) {
            for (uint32_t nx = 0; nx < side; nx++) {
                uint32_t parent = off + (ny * side) + nx;
                uint32_t c_side = side * 2;
                uint32_t cx = nx * 2;
                uint32_t cy = ny * 2;
                uint8_t c0 = qt->nodes[child_off + (cy * c_side) + cx];
                uint8_t c1 = qt->nodes[child_off + (cy * c_side) + cx + 1];
                uint8_t c2 = qt->nodes[child_off + ((cy + 1) * c_side) + cx];
                uint8_t c3 = qt->nodes[child_off + ((cy + 1) * c_side) + cx + 1];
                if (c0 == QTREE_FULL && c1 == QTREE_FULL && c2 == QTREE_FULL && c3 == QTREE_FULL) {
                    qt->nodes[parent] = QTREE_FULL;
                } else if (c0 == QTREE_EMPTY && c1 == QTREE_EMPTY && c2 == QTREE_EMPTY && c3 == QTREE_EMPTY) {
                    qt->nodes[parent] = QTREE_EMPTY;
                } else {
                    qt->nodes[parent] = QTREE_MIXED;
                }
            }
        }
    }
}

/* Update quadtree after stamping a sprite at tile rect [tx0..tx0+stw) x [ty0..ty0+sth).
 * Only reclassifies affected leaf blocks, then propagates upward. */
static void qtree_update_after_stamp(QuadTree *qt, const TileGrid *g, int32_t tx0, int32_t ty0, uint32_t stw, uint32_t sth) {
    /* Convert tile range to block range */
    uint32_t bx0 = (tx0 >= 0) ? ((uint32_t)tx0 >> QTREE_BLOCK_SHIFT) : 0;
    uint32_t by0 = (ty0 >= 0) ? ((uint32_t)ty0 >> QTREE_BLOCK_SHIFT) : 0;
    uint32_t tx_end = (uint32_t)tx0 + stw;
    uint32_t ty_end = (uint32_t)ty0 + sth;
    uint32_t bx1 = (tx_end + QTREE_BLOCK_SIZE - 1) >> QTREE_BLOCK_SHIFT;
    uint32_t by1 = (ty_end + QTREE_BLOCK_SIZE - 1) >> QTREE_BLOCK_SHIFT;
    if (bx1 > qt->leaf_bw) {
        bx1 = qt->leaf_bw;
    }
    if (by1 > qt->leaf_bh) {
        by1 = qt->leaf_bh;
    }

    uint32_t leaf_off = qtree_level_offset(qt->levels - 1);
    uint32_t leaf_dim = qt->dim;

    /* Reclassify affected leaf blocks */
    for (uint32_t by = by0; by < by1; by++) {
        for (uint32_t bx = bx0; bx < bx1; bx++) {
            uint32_t idx = leaf_off + (by * leaf_dim) + bx;
            qt->nodes[idx] = qtree_classify_block(g, bx, by);
        }
    }

    /* Propagate bottom-up through all ancestor levels */
    for (int32_t lev = (int32_t)qt->levels - 2; lev >= 0; lev--) {
        uint32_t off = qtree_level_offset((uint32_t)lev);
        uint32_t child_off = qtree_level_offset((uint32_t)lev + 1);
        uint32_t side = (uint32_t)1 << (uint32_t)lev;
        uint32_t c_side = side * 2;

        /* Affected parent block range at this level */
        uint32_t pbx0 = bx0 >> 1;
        uint32_t pby0 = by0 >> 1;
        uint32_t pbx1 = (bx1 + 1) >> 1;
        uint32_t pby1 = (by1 + 1) >> 1;
        if (pbx1 > side) {
            pbx1 = side;
        }
        if (pby1 > side) {
            pby1 = side;
        }

        for (uint32_t ny = pby0; ny < pby1; ny++) {
            for (uint32_t nx = pbx0; nx < pbx1; nx++) {
                uint32_t parent = off + (ny * side) + nx;
                uint32_t cx = nx * 2;
                uint32_t cy = ny * 2;
                uint8_t c0 = qt->nodes[child_off + (cy * c_side) + cx];
                uint8_t c1 = qt->nodes[child_off + (cy * c_side) + cx + 1];
                uint8_t c2 = qt->nodes[child_off + ((cy + 1) * c_side) + cx];
                uint8_t c3 = qt->nodes[child_off + ((cy + 1) * c_side) + cx + 1];
                if (c0 == QTREE_FULL && c1 == QTREE_FULL && c2 == QTREE_FULL && c3 == QTREE_FULL) {
                    qt->nodes[parent] = QTREE_FULL;
                } else if (c0 == QTREE_EMPTY && c1 == QTREE_EMPTY && c2 == QTREE_EMPTY && c3 == QTREE_EMPTY) {
                    qt->nodes[parent] = QTREE_EMPTY;
                } else {
                    qt->nodes[parent] = QTREE_MIXED;
                }
            }
        }

        /* Shift range for next parent level */
        bx0 = pbx0;
        by0 = pby0;
        bx1 = pbx1;
        by1 = pby1;
    }
}

/* Check if a horizontal span of tiles [tx..tx+span_w) is fully occupied at block level.
 * Uses the leaf level of the quadtree for O(1)-per-block checks.
 * Returns true if ALL blocks covering the span are FULL. */
static bool qtree_row_full(const QuadTree *qt, uint32_t tx, uint32_t ty, uint32_t span_w) {
    uint32_t bx0 = tx >> QTREE_BLOCK_SHIFT;
    uint32_t bx1 = (tx + span_w + QTREE_BLOCK_SIZE - 1) >> QTREE_BLOCK_SHIFT;
    uint32_t by = ty >> QTREE_BLOCK_SHIFT;
    if (by >= qt->leaf_bh) {
        return false;
    }
    if (bx1 > qt->leaf_bw) {
        bx1 = qt->leaf_bw;
    }

    uint32_t leaf_off = qtree_level_offset(qt->levels - 1);
    uint32_t leaf_dim = qt->dim;

    for (uint32_t bx = bx0; bx < bx1; bx++) {
        if (qt->nodes[leaf_off + (by * leaf_dim) + bx] != QTREE_FULL) {
            return false;
        }
    }
    return true;
}

/* Find next tx >= start_tx where the block at (tx, ty) is NOT full.
 * Scans leaf blocks to skip FULL columns quickly.
 * Returns max_tx if all remaining blocks are FULL. */
static uint32_t qtree_next_nonfull_tx(const QuadTree *qt, uint32_t start_tx, uint32_t ty, uint32_t max_tx) {
    uint32_t by = ty >> QTREE_BLOCK_SHIFT;
    if (by >= qt->leaf_bh) {
        return start_tx; /* out of range, don't skip */
    }

    uint32_t leaf_off = qtree_level_offset(qt->levels - 1);
    uint32_t leaf_dim = qt->dim;
    uint32_t bx = start_tx >> QTREE_BLOCK_SHIFT;
    uint32_t max_bx = (max_tx + QTREE_BLOCK_SIZE - 1) >> QTREE_BLOCK_SHIFT;
    if (max_bx > qt->leaf_bw) {
        max_bx = qt->leaf_bw;
    }

    while (bx < max_bx) {
        if (qt->nodes[leaf_off + (by * leaf_dim) + bx] != QTREE_FULL) {
            /* This block is not full, return the start of this block or start_tx */
            uint32_t block_start = bx << QTREE_BLOCK_SHIFT;
            return (block_start > start_tx) ? block_start : start_tx;
        }
        bx++;
    }
    return max_tx;
}

/* Grow quadtree to match a grown tile grid. Rebuilds from scratch. */
static void qtree_grow(QuadTree *qt, const TileGrid *g) {
    qtree_free(qt);
    *qt = qtree_create(g->tw, g->th);
    qtree_rebuild(qt, g);
}

/* --- Polygon inflation: expand convex hull outward by N pixels --- */

/* Offset each edge outward along its normal, recompute vertices as intersections. */
static uint32_t polygon_inflate(const Point2D *hull, uint32_t n, float amount, Point2D *out) {
    if (n < 3) {
        memcpy(out, hull, n * sizeof(Point2D));
        return n;
    }

    /* Compute offset edges (each stored as a point + direction on the offset line) */
    float *ox = (float *)malloc(n * sizeof(float) * 4); /* [ax, ay, bx, by] per edge */
    NT_BUILD_ASSERT(ox && "polygon_inflate: alloc failed");

    for (uint32_t i = 0; i < n; i++) {
        uint32_t j = (i + 1) % n;
        float dx = (float)(hull[j].x - hull[i].x);
        float dy = (float)(hull[j].y - hull[i].y);
        float len = sqrtf((dx * dx) + (dy * dy));
        if (len < 1e-6F) {
            len = 1.0F;
        }
        /* Outward normal (CCW polygon: rotate edge direction CW 90) */
        float nx = dy / len;
        float ny = -dx / len;
        ox[(i * 4) + 0] = (float)hull[i].x + (nx * amount);
        ox[(i * 4) + 1] = (float)hull[i].y + (ny * amount);
        ox[(i * 4) + 2] = (float)hull[j].x + (nx * amount);
        ox[(i * 4) + 3] = (float)hull[j].y + (ny * amount);
    }

    /* Intersect adjacent offset edges to get inflated vertices */
    uint32_t out_count = 0;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t j = (i + 1) % n;
        /* Line i: from (ax0,ay0) to (bx0,by0), line j: from (ax1,ay1) to (bx1,by1) */
        float ax0 = ox[(i * 4) + 0];
        float ay0 = ox[(i * 4) + 1];
        float bx0 = ox[(i * 4) + 2];
        float by0 = ox[(i * 4) + 3];
        float ax1 = ox[(j * 4) + 0];
        float ay1 = ox[(j * 4) + 1];
        float bx1 = ox[(j * 4) + 2];
        float by1 = ox[(j * 4) + 3];

        float d0x = bx0 - ax0;
        float d0y = by0 - ay0;
        float d1x = bx1 - ax1;
        float d1y = by1 - ay1;

        float denom = (d0x * d1y) - (d0y * d1x);
        if (fabsf(denom) < 1e-6F) {
            /* Parallel edges: use midpoint of the two offset endpoints */
            out[out_count].x = (int32_t)roundf((bx0 + ax1) * 0.5F);
            out[out_count].y = (int32_t)roundf((by0 + ay1) * 0.5F);
        } else {
            float t = (((ax1 - ax0) * d1y) - ((ay1 - ay0) * d1x)) / denom;
            out[out_count].x = (int32_t)roundf(ax0 + (t * d0x));
            out[out_count].y = (int32_t)roundf(ay0 + (t * d0y));
        }
        out_count++;
    }

    free(ox);
    return out_count;
}

/* Rotate polygon vertices by 0/90/180/270. tw/th = original trimmed sprite dims. */
static void polygon_rotate(const Point2D *src, uint32_t n, uint8_t rotation, int32_t tw, int32_t th, Point2D *out) {
    for (uint32_t i = 0; i < n; i++) {
        switch (rotation) {
        case 1: /* 90 CW */
            out[i].x = th - 1 - src[i].y;
            out[i].y = src[i].x;
            break;
        case 2: /* 180 */
            out[i].x = tw - 1 - src[i].x;
            out[i].y = th - 1 - src[i].y;
            break;
        case 3: /* 270 CW */
            out[i].x = src[i].y;
            out[i].y = tw - 1 - src[i].x;
            break;
        default: /* 0 */
            out[i] = src[i];
            break;
        }
    }
}

/* --- Triangle vs AABB overlap test (Separating Axis Theorem) --- */

/* Project triangle and AABB onto axis, return true if projections are separated. */
static bool sat_separated(const Point2D tri[3], int32_t rx, int32_t ry, int32_t rw, int32_t rh, int32_t ax, int32_t ay) {
    /* Project triangle vertices onto axis */
    int64_t t0 = ((int64_t)tri[0].x * ax) + ((int64_t)tri[0].y * ay);
    int64_t t1 = ((int64_t)tri[1].x * ax) + ((int64_t)tri[1].y * ay);
    int64_t t2 = ((int64_t)tri[2].x * ax) + ((int64_t)tri[2].y * ay);
    int64_t tri_min = t0;
    int64_t tri_max = t0;
    if (t1 < tri_min) {
        tri_min = t1;
    }
    if (t1 > tri_max) {
        tri_max = t1;
    }
    if (t2 < tri_min) {
        tri_min = t2;
    }
    if (t2 > tri_max) {
        tri_max = t2;
    }

    /* Project AABB corners onto axis */
    int32_t cx[4] = {rx, rx + rw, rx + rw, rx};
    int32_t cy[4] = {ry, ry, ry + rh, ry + rh};
    int64_t r0 = ((int64_t)cx[0] * ax) + ((int64_t)cy[0] * ay);
    int64_t rect_min = r0;
    int64_t rect_max = r0;
    for (int k = 1; k < 4; k++) {
        int64_t rp = ((int64_t)cx[k] * ax) + ((int64_t)cy[k] * ay);
        if (rp < rect_min) {
            rect_min = rp;
        }
        if (rp > rect_max) {
            rect_max = rp;
        }
    }

    return tri_max < rect_min || rect_max < tri_min;
}

/* Test if triangle (3 Point2D) overlaps axis-aligned rect [rx, rx+rw) x [ry, ry+rh).
 * Uses Separating Axis Theorem: 3 triangle edge normals + 2 AABB axes. */
static bool triangle_overlaps_rect(const Point2D tri[3], int32_t rx, int32_t ry, int32_t rw, int32_t rh) {
    // #region AABB axes (X and Y)
    if (sat_separated(tri, rx, ry, rw, rh, 1, 0)) {
        return false;
    }
    if (sat_separated(tri, rx, ry, rw, rh, 0, 1)) {
        return false;
    }
    // #endregion

    // #region Triangle edge normals
    for (int i = 0; i < 3; i++) {
        int j = (i + 1) % 3;
        int32_t ex = tri[j].x - tri[i].x;
        int32_t ey = tri[j].y - tri[i].y;
        /* Edge normal (perpendicular) */
        int32_t nx = -ey;
        int32_t ny = ex;
        if (sat_separated(tri, rx, ry, rw, rh, nx, ny)) {
            return false;
        }
    }
    // #endregion

    return true;
}

/* --- Rasterize convex hull polygon onto tile grid via fan triangulation --- */

/* Create tile grid from inflated polygon. The polygon vertices are in pixel
 * coordinates. The grid covers the polygon bounding box in tile units.
 * Origin offset (in tiles) returned via out_ox, out_oy. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static TileGrid tgrid_from_polygon(const Point2D *hull, uint32_t hull_n, uint32_t sprite_tw, uint32_t sprite_th, uint32_t tile_size, int32_t *out_ox, int32_t *out_oy) {
    (void)sprite_tw;
    (void)sprite_th;

    /* Compute polygon bounding box in pixels */
    int32_t px_min = hull[0].x;
    int32_t px_max = hull[0].x;
    int32_t py_min = hull[0].y;
    int32_t py_max = hull[0].y;
    for (uint32_t i = 1; i < hull_n; i++) {
        if (hull[i].x < px_min) {
            px_min = hull[i].x;
        }
        if (hull[i].x > px_max) {
            px_max = hull[i].x;
        }
        if (hull[i].y < py_min) {
            py_min = hull[i].y;
        }
        if (hull[i].y > py_max) {
            py_max = hull[i].y;
        }
    }

    /* Convert bounding box to tile coordinates (floor for min, ceil for max) */
    int32_t ts = (int32_t)tile_size;
    int32_t tile_x0 = (px_min >= 0) ? (px_min / ts) : (((px_min - ts + 1) / ts));
    int32_t tile_y0 = (py_min >= 0) ? (py_min / ts) : (((py_min - ts + 1) / ts));
    int32_t tile_x1 = (px_max >= 0) ? ((px_max + ts) / ts) : ((px_max + 1) / ts);
    int32_t tile_y1 = (py_max >= 0) ? ((py_max + ts) / ts) : ((py_max + 1) / ts);

    uint32_t tw = (uint32_t)(tile_x1 - tile_x0);
    uint32_t th = (uint32_t)(tile_y1 - tile_y0);
    if (tw == 0) {
        tw = 1;
    }
    if (th == 0) {
        th = 1;
    }

    *out_ox = tile_x0;
    *out_oy = tile_y0;

    TileGrid g = tgrid_create(tw, th, tile_size);

    /* Fan-triangulate the polygon from vertex 0 */
    uint16_t indices[96]; /* max 32 vertices -> 30 tris -> 90 indices */
    uint32_t tri_count = fan_triangulate(hull_n, indices);

    /* For each tile cell, test if any triangle overlaps the tile rect */
    for (uint32_t ty = 0; ty < th; ty++) {
        for (uint32_t tx = 0; tx < tw; tx++) {
            int32_t rx = (tile_x0 + (int32_t)tx) * ts;
            int32_t ry = (tile_y0 + (int32_t)ty) * ts;
            for (uint32_t t = 0; t < tri_count; t++) {
                Point2D tri[3] = {hull[indices[(t * 3) + 0]], hull[indices[(t * 3) + 1]], hull[indices[(t * 3) + 2]]};
                if (triangle_overlaps_rect(tri, rx, ry, ts, ts)) {
                    tgrid_set(&g, tx, ty);
                    break;
                }
            }
        }
    }

    return g;
}

/* --- Tile grid collision test (bitset) --- */

/* Count trailing zeros (portable). Returns 64 if v == 0. */
static inline uint32_t tgrid_ctz64(uint64_t v) {
    if (v == 0) {
        return 64;
    }
#if defined(__GNUC__) || defined(__clang__)
    return (uint32_t)__builtin_ctzll(v);
#elif defined(_MSC_VER)
    unsigned long idx;
    _BitScanForward64(&idx, v);
    return (uint32_t)idx;
#else
    uint32_t c = 0;
    while ((v & 1) == 0) {
        v >>= 1;
        c++;
    }
    return c;
#endif
}

/* Test if placing sprite grid at (tx,ty) on atlas grid collides.
 * Sprite grid origin offset is (ox,oy) in tiles.
 * out_skip: how many tile columns to skip ahead on collision.
 * Uses word-level AND for fast collision detection. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static bool tgrid_test(const TileGrid *atlas, const TileGrid *sprite, int32_t tx, int32_t ty, int32_t ox, int32_t oy, uint32_t *out_skip) {
    int32_t base_x = tx + ox;
    int32_t base_y = ty + oy;

    for (uint32_t sy = 0; sy < sprite->th; sy++) {
        int32_t ay = base_y + (int32_t)sy;
        if (ay < 0 || (uint32_t)ay >= atlas->th) {
            continue;
        }
        const uint64_t *s_row = sprite->rows + ((size_t)sy * sprite->row_words);
        const uint64_t *a_row = atlas->rows + ((size_t)(uint32_t)ay * atlas->row_words);

        for (uint32_t sw = 0; sw < sprite->row_words; sw++) {
            uint64_t s = s_row[sw];
            if (s == 0) {
                continue;
            }
            /* Sprite bit range: [base_x + sw*64 .. base_x + sw*64 + 63] maps to atlas columns */
            int32_t col = base_x + (int32_t)(sw * 64);

            /* Skip if entire word is out of atlas bounds */
            if (col >= (int32_t)atlas->tw || col + 64 <= 0) {
                continue;
            }

            /* Mask off bits that fall outside atlas bounds */
            if (col < 0) {
                s >>= (uint32_t)(-col);
                col = 0;
            }
            if ((uint32_t)col + 64 > atlas->tw) {
                uint32_t valid = atlas->tw - (uint32_t)col;
                if (valid < 64) {
                    s &= (1ULL << valid) - 1;
                }
            }
            if (s == 0) {
                continue;
            }

            uint32_t a_word = (uint32_t)col >> 6;
            uint32_t a_bit = (uint32_t)col & 63;

            /* Check overlap: sprite word shifted into atlas word(s) */
            uint64_t hit = a_row[a_word] & (s << a_bit);
            if (a_bit > 0 && (a_word + 1) < atlas->row_words) {
                hit |= a_row[a_word + 1] & (s >> (64 - a_bit));
            }

            if (hit) {
                /* Collision — compute skip via ctz */
                if (out_skip) {
                    uint64_t hit_lo = a_row[a_word] & (s << a_bit);
                    uint32_t hit_col;
                    if (hit_lo) {
                        hit_col = (a_word * 64) + tgrid_ctz64(hit_lo);
                    } else {
                        hit_col = ((a_word + 1) * 64) + tgrid_ctz64(a_row[a_word + 1] & (s >> (64 - a_bit)));
                    }
                    /* Advance past consecutive occupied atlas tiles */
                    uint32_t skip_to = hit_col + 1;
                    while (skip_to < atlas->tw && tgrid_get(atlas, skip_to, (uint32_t)ay)) {
                        skip_to++;
                    }
                    *out_skip = skip_to - (uint32_t)tx;
                }
                return true;
            }
        }
    }
    return false;
}

/* --- Tile grid stamp (bitset) --- */

/* Stamp sprite grid onto atlas grid at (tx,ty) with origin offset (ox,oy).
 * Uses word-level OR for fast stamping. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void tgrid_stamp(TileGrid *atlas, const TileGrid *sprite, int32_t tx, int32_t ty, int32_t ox, int32_t oy) {
    int32_t base_x = tx + ox;
    int32_t base_y = ty + oy;

    for (uint32_t sy = 0; sy < sprite->th; sy++) {
        int32_t ay = base_y + (int32_t)sy;
        if (ay < 0 || (uint32_t)ay >= atlas->th) {
            continue;
        }
        const uint64_t *s_row = sprite->rows + ((size_t)sy * sprite->row_words);
        uint64_t *a_row = atlas->rows + ((size_t)(uint32_t)ay * atlas->row_words);

        for (uint32_t sw = 0; sw < sprite->row_words; sw++) {
            uint64_t s = s_row[sw];
            if (s == 0) {
                continue;
            }
            int32_t col = base_x + (int32_t)(sw * 64);
            if (col >= (int32_t)atlas->tw || col + 64 <= 0) {
                continue;
            }
            if (col < 0) {
                s >>= (uint32_t)(-col);
                col = 0;
            }
            if ((uint32_t)col + 64 > atlas->tw) {
                uint32_t valid = atlas->tw - (uint32_t)col;
                if (valid < 64) {
                    s &= (1ULL << valid) - 1;
                }
            }
            if (s == 0) {
                continue;
            }

            uint32_t a_word = (uint32_t)col >> 6;
            uint32_t a_bit = (uint32_t)col & 63;

            a_row[a_word] |= (s << a_bit);
            if (a_bit > 0 && (a_word + 1) < atlas->row_words) {
                a_row[a_word + 1] |= (s >> (64 - a_bit));
            }
        }
    }
}

/* --- Row occupancy OR mask: pre-compute combined mask for quick rejection --- */

/* Build OR of all atlas rows in [y0..y0+h). Result has a bit set if ANY row in the
 * range has that column occupied. Allows fast skip: if OR-mask shows no gap wide
 * enough for the sprite, skip the entire ty without per-position tgrid_test. */
static void tgrid_row_or(const TileGrid *g, uint32_t y0, uint32_t h, uint64_t *out, uint32_t out_words) {
    memset(out, 0, (size_t)out_words * sizeof(uint64_t));
    uint32_t y_end = y0 + h;
    if (y_end > g->th) {
        y_end = g->th;
    }
    for (uint32_t y = y0; y < y_end; y++) {
        const uint64_t *row = g->rows + ((size_t)y * g->row_words);
        for (uint32_t w = 0; w < out_words && w < g->row_words; w++) {
            out[w] |= row[w];
        }
    }
}

/* Check if OR-mask bit at position `col` is free (0). Quick single-bit test. */
static inline bool tgrid_or_bit_free(const uint64_t *mask, uint32_t mask_words, uint32_t col) {
    uint32_t w = col >> 6;
    if (w >= mask_words) {
        return false;
    }
    return (mask[w] & (1ULL << (col & 63))) == 0;
}

/* Find next zero bit in OR-mask at or after position `from`. Returns mask_bits if none. */
static uint32_t tgrid_or_next_free(const uint64_t *mask, uint32_t mask_words, uint32_t mask_bits, uint32_t from) {
    uint32_t w = from >> 6;
    uint32_t b = from & 63;
    while (w < mask_words) {
        uint64_t v = mask[w] >> b;
        if (v != UINT64_MAX >> b) {
            /* There's a zero bit in this word at or after position b */
            if (b > 0) {
                /* Re-check from bit b: invert and find first set */
                uint64_t inv = ~mask[w] & (UINT64_MAX << b);
                if (inv) {
                    return (w * 64) + tgrid_ctz64(inv);
                }
            } else {
                uint64_t inv = ~mask[w];
                if (inv) {
                    return (w * 64) + tgrid_ctz64(inv);
                }
            }
        }
        w++;
        b = 0;
    }
    return mask_bits;
}

/* --- Scan a rectangular region of the atlas grid for a valid placement --- */

/* Try to place a sprite (with given rotation variants) within [ty_min..ty_max) x [tx_min..tx_max).
 * Returns true if placed, with best position in out_tx/out_ty/out_rot.
 * Uses quadtree skip + OR-mask pre-rejection to skip positions where any sprite row would collide. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static bool scan_region(const TileGrid *atlas, const QuadTree *qt, const TileGrid *rot_sg, const int32_t *rot_ox, const int32_t *rot_oy, uint32_t rot_count, uint32_t tx_min, uint32_t ty_min,
                        uint32_t tx_max, uint32_t ty_max, uint32_t *out_tx, uint32_t *out_ty, uint8_t *out_rot) {
    uint32_t best_tx = UINT32_MAX;
    uint32_t best_ty = UINT32_MAX;
    bool found = false;

    /* Allocate OR-mask buffer (reused across rows) */
    uint64_t *or_mask = (uint64_t *)malloc((size_t)atlas->row_words * sizeof(uint64_t));
    NT_BUILD_ASSERT(or_mask && "scan_region: alloc failed");

    for (uint32_t r = 0; r < rot_count; r++) {
        /* Early-out: if we already found at best_ty, later rotations only need to beat it */
        uint32_t eff_ty_max = found ? (best_ty + 1) : ty_max;
        if (eff_ty_max > ty_max) {
            eff_ty_max = ty_max;
        }

        for (uint32_t ty = ty_min; ty < eff_ty_max; ty++) {
            /* Pre-compute OR of atlas rows covered by sprite at this ty */
            int32_t ay0 = (int32_t)ty + rot_oy[r];
            uint32_t or_y0 = (ay0 >= 0) ? (uint32_t)ay0 : 0;
            uint32_t or_h = rot_sg[r].th;
            if (ay0 < 0) {
                or_h = (or_h > (uint32_t)(-ay0)) ? or_h - (uint32_t)(-ay0) : 0;
            }
            tgrid_row_or(atlas, or_y0, or_h, or_mask, atlas->row_words);

            for (uint32_t tx = tx_min; tx < tx_max;) {
                /* Quadtree skip: if the block at sprite's first column is FULL, advance */
                if (qt) {
                    int32_t qt_col = (int32_t)tx + rot_ox[r];
                    if (qt_col >= 0 && (uint32_t)qt_col < atlas->tw) {
                        uint32_t qt_row = ((int32_t)ty + rot_oy[r] >= 0) ? (uint32_t)((int32_t)ty + rot_oy[r]) : 0;
                        if (qtree_row_full(qt, (uint32_t)qt_col, qt_row, 1)) {
                            uint32_t next_tx = qtree_next_nonfull_tx(qt, (uint32_t)qt_col, qt_row, atlas->tw);
                            int32_t skip_tx = (int32_t)next_tx - rot_ox[r];
                            if (skip_tx > (int32_t)tx) {
                                tx = (uint32_t)skip_tx;
                                continue;
                            }
                        }
                    }
                }

                /* Quick rejection: if first sprite column is occupied in OR-mask, skip ahead */
                int32_t col0 = (int32_t)tx + rot_ox[r];
                if (col0 >= 0 && (uint32_t)col0 < atlas->tw) {
                    if (!tgrid_or_bit_free(or_mask, atlas->row_words, (uint32_t)col0)) {
                        uint32_t next_free = tgrid_or_next_free(or_mask, atlas->row_words, atlas->tw, (uint32_t)col0 + 1);
                        int32_t next_tx = (int32_t)next_free - rot_ox[r];
                        if (next_tx <= (int32_t)tx) {
                            next_tx = (int32_t)tx + 1;
                        }
                        tx = (uint32_t)next_tx;
                        continue;
                    }
                }

                uint32_t skip = 0;
                if (!tgrid_test(atlas, &rot_sg[r], (int32_t)tx, (int32_t)ty, rot_ox[r], rot_oy[r], &skip)) {
                    /* Verify the sprite fits within atlas bounds (min + max check) */
                    int32_t abs_min_tx = (int32_t)tx + rot_ox[r];
                    int32_t abs_min_ty = (int32_t)ty + rot_oy[r];
                    int32_t abs_max_tx = abs_min_tx + (int32_t)rot_sg[r].tw;
                    int32_t abs_max_ty = abs_min_ty + (int32_t)rot_sg[r].th;
                    if (abs_min_tx >= 0 && abs_min_ty >= 0 && (uint32_t)abs_max_tx <= atlas->tw && (uint32_t)abs_max_ty <= atlas->th) {
                        if (ty < best_ty || (ty == best_ty && tx < best_tx)) {
                            best_tx = tx;
                            best_ty = ty;
                            *out_rot = (uint8_t)r;
                            found = true;
                        }
                        break; /* first-fit in this row */
                    }
                    /* Bounds fail: if max exceeded, skip rest of row; if min negative, advance */
                    if ((uint32_t)abs_max_tx > atlas->tw || (uint32_t)abs_max_ty > atlas->th) {
                        break; /* sprite won't fit further right/down */
                    }
                    tx++;
                    continue;
                }
                if (skip > 1) {
                    tx += skip;
                } else {
                    tx++;
                }
            }
            if (found) {
                break;
            }
        }
    }

    free(or_mask);

    if (found) {
        *out_tx = best_tx;
        *out_ty = best_ty;
    }
    return found;
}

/* --- Area-descending sort comparator (ATLAS-03) --- */

static int area_sort_cmp(const void *a, const void *b) {
    const AreaSortEntry *ea = (const AreaSortEntry *)a;
    const AreaSortEntry *eb = (const AreaSortEntry *)b;
    /* Descending by area */
    if (ea->area > eb->area) {
        return -1;
    }
    if (ea->area < eb->area) {
        return 1;
    }
    return 0;
}

/* --- Tile packer with tile-grid collision (ATLAS-02, ATLAS-03, ATLAS-04, ATLAS-18) --- */

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static uint32_t tile_pack(const uint32_t *trim_w, const uint32_t *trim_h, Point2D **hull_verts, const uint32_t *hull_counts, uint32_t sprite_count, const nt_atlas_opts_t *opts,
                          AtlasPlacement *out_placements, uint32_t *out_page_count, uint32_t *out_page_w, uint32_t *out_page_h) {
    uint32_t max_size = opts->max_size;
    uint32_t margin = opts->margin;
    uint32_t extrude = opts->extrude;
    uint32_t padding = opts->padding;
    bool allow_rotate = opts->allow_rotate;
    bool pot = opts->power_of_two;
    float dilate = (float)extrude + ((float)padding * 0.5F);
    NT_BUILD_ASSERT(opts->tile_size > 0 && "tile_pack: tile_size must be > 0");
    uint32_t ts = opts->tile_size;

    // #region Build per-sprite inflated polygon tile grids
    TileGrid *sprite_grids = (TileGrid *)calloc(sprite_count, sizeof(TileGrid));
    int32_t *grid_ox = (int32_t *)calloc(sprite_count, sizeof(int32_t));
    int32_t *grid_oy = (int32_t *)calloc(sprite_count, sizeof(int32_t));
    NT_BUILD_ASSERT(sprite_grids && grid_ox && grid_oy && "tile_pack: alloc failed");

    for (uint32_t i = 0; i < sprite_count; i++) {
        Point2D inf_poly[32];
        uint32_t inf_n = polygon_inflate(hull_verts[i], hull_counts[i], dilate, inf_poly);
        sprite_grids[i] = tgrid_from_polygon(inf_poly, inf_n, trim_w[i], trim_h[i], ts, &grid_ox[i], &grid_oy[i]);
    }
    // #endregion

    // #region Sort sprites by area descending (ATLAS-03)
    AreaSortEntry *sorted = (AreaSortEntry *)malloc(sprite_count * sizeof(AreaSortEntry));
    NT_BUILD_ASSERT(sorted && "tile_pack: alloc failed");
    for (uint32_t i = 0; i < sprite_count; i++) {
        sorted[i].index = i;
        sorted[i].area = trim_w[i] * trim_h[i];
    }
    qsort(sorted, sprite_count, sizeof(AreaSortEntry), area_sort_cmp);
    // #endregion

    // #region Calculate initial atlas size
    uint64_t total_area = 0;
    uint32_t max_cell_w = 0;
    uint32_t max_cell_h = 0;
    for (uint32_t i = 0; i < sprite_count; i++) {
        uint32_t mw = sprite_grids[i].tw * ts;
        uint32_t mh = sprite_grids[i].th * ts;
        total_area += (uint64_t)mw * mh;
        if (mw > max_cell_w) {
            max_cell_w = mw;
        }
        if (mh > max_cell_h) {
            max_cell_h = mh;
        }
    }
    /* Start below total area — page growth will expand as needed for tighter final fit */
    double side = sqrt((double)total_area) * 0.8;
    uint32_t init_dim = (uint32_t)(side + 0.5);
    uint32_t min_dim = (max_cell_w > max_cell_h ? max_cell_w : max_cell_h) + (2 * margin);
    if (init_dim < min_dim) {
        init_dim = min_dim;
    }
    if (init_dim > max_size) {
        init_dim = max_size;
    }
    if (pot) {
        init_dim = next_pot(init_dim);
        if (init_dim > max_size) {
            init_dim = max_size;
        }
    }
    // #endregion

    // #region Pack sprites onto pages using tile-grid collision
    uint32_t atlas_tw = (init_dim + ts - 1) / ts;
    uint32_t atlas_th = atlas_tw;
    uint32_t max_tw = (max_size + ts - 1) / ts; /* max grid size in tiles */
    uint32_t max_th = max_tw;
    TileGrid page_grids[ATLAS_MAX_PAGES];
    QuadTree page_qtrees[ATLAS_MAX_PAGES];
    uint32_t page_count = 1;
    uint32_t page_w[ATLAS_MAX_PAGES];
    uint32_t page_h[ATLAS_MAX_PAGES];
    uint32_t page_max_x[ATLAS_MAX_PAGES]; /* pixel bounding box for final page size */
    uint32_t page_max_y[ATLAS_MAX_PAGES];
    uint32_t page_used_tw[ATLAS_MAX_PAGES]; /* used extent in tiles — scan limit */
    uint32_t page_used_th[ATLAS_MAX_PAGES];
    memset(page_max_x, 0, sizeof(page_max_x));
    memset(page_max_y, 0, sizeof(page_max_y));
    memset(page_used_tw, 0, sizeof(page_used_tw));
    memset(page_used_th, 0, sizeof(page_used_th));
    memset(page_qtrees, 0, sizeof(page_qtrees));

    for (uint32_t p = 0; p < ATLAS_MAX_PAGES; p++) {
        page_w[p] = init_dim;
        page_h[p] = init_dim;
    }
    page_grids[0] = tgrid_create(atlas_tw, atlas_th, ts);
    page_qtrees[0] = qtree_create(atlas_tw, atlas_th);

    uint32_t margin_tiles = (margin + ts - 1) / ts;
    uint32_t placement_count = 0;
    double pack_start_time = nt_time_now();
    double last_log_time = pack_start_time;

    for (uint32_t si = 0; si < sprite_count; si++) {
        /* Progress log every 10 seconds */
        double now = nt_time_now();
        if (now - last_log_time >= 10.0) {
            NT_LOG_INFO("  packing: %u/%u sprites placed (%.0fs elapsed)", si, sprite_count, now - pack_start_time);
            last_log_time = now;
        }
        uint32_t idx = sorted[si].index;
        TileGrid *base_sg = &sprite_grids[idx];
        int32_t base_ox = grid_ox[idx];
        int32_t base_oy = grid_oy[idx];

        /* For rotations, build rotated tile grids from rotated inflated polygon. */
        uint32_t rot_count = allow_rotate ? 4 : 1;
        TileGrid rot_sg[4];
        int32_t rot_ox[4];
        int32_t rot_oy[4];
        rot_sg[0] = *base_sg; /* just reference, don't free */
        rot_ox[0] = base_ox;
        rot_oy[0] = base_oy;

        if (allow_rotate) {
            Point2D inf_poly[32];
            uint32_t inf_n = polygon_inflate(hull_verts[idx], hull_counts[idx], dilate, inf_poly);
            for (uint32_t r = 1; r <= 3; r++) {
                Point2D rot_poly[32];
                polygon_rotate(inf_poly, inf_n, (uint8_t)r, (int32_t)trim_w[idx], (int32_t)trim_h[idx], rot_poly);
                uint32_t rtw = (r == 1 || r == 3) ? trim_h[idx] : trim_w[idx];
                uint32_t rth = (r == 1 || r == 3) ? trim_w[idx] : trim_h[idx];
                rot_sg[r] = tgrid_from_polygon(rot_poly, inf_n, rtw, rth, ts, &rot_ox[r], &rot_oy[r]);
            }
        }

        bool placed = false;
        uint32_t best_page = 0;
        uint32_t best_tx = UINT32_MAX;
        uint32_t best_ty = UINT32_MAX;
        uint8_t best_rot = 0;

        /* Max sprite tile extent for scan limit (any rotation) */
        uint32_t max_stw = rot_sg[0].tw;
        uint32_t max_sth = rot_sg[0].th;
        for (uint32_t r = 1; r < rot_count; r++) {
            if (rot_sg[r].tw > max_stw) {
                max_stw = rot_sg[r].tw;
            }
            if (rot_sg[r].th > max_sth) {
                max_sth = rot_sg[r].th;
            }
        }

        // #region Try existing pages (scan limited to used extent + sprite size)
        for (uint32_t pi = 0; pi < page_count && !placed; pi++) {
            uint32_t scan_tw = page_used_tw[pi] + max_stw + 1;
            uint32_t scan_th = page_used_th[pi] + max_sth + 1;
            if (scan_tw > page_grids[pi].tw) {
                scan_tw = page_grids[pi].tw;
            }
            if (scan_th > page_grids[pi].th) {
                scan_th = page_grids[pi].th;
            }
            placed = scan_region(&page_grids[pi], &page_qtrees[pi], rot_sg, rot_ox, rot_oy, rot_count, margin_tiles, margin_tiles, scan_tw, scan_th, &best_tx, &best_ty, &best_rot);
            if (placed) {
                best_page = pi;
            }
        }
        // #endregion

        // #region Page growth: grow last page before creating new one
        if (!placed) {
            uint32_t pi = page_count - 1; /* grow last page */
            while (!placed && (page_grids[pi].tw < max_tw || page_grids[pi].th < max_th)) {
                uint32_t old_tw = 0;
                uint32_t old_th = 0;
                tgrid_grow(&page_grids[pi], max_tw, max_th, &old_tw, &old_th);
                qtree_grow(&page_qtrees[pi], &page_grids[pi]);
                page_w[pi] = page_grids[pi].tw * ts;
                page_h[pi] = page_grids[pi].th * ts;

                /* Scan limit: used extent + sprite size, clamped to grid */
                uint32_t g_scan_tw = page_used_tw[pi] + max_stw + 1;
                uint32_t g_scan_th = page_used_th[pi] + max_sth + 1;
                if (g_scan_tw > page_grids[pi].tw) {
                    g_scan_tw = page_grids[pi].tw;
                }
                if (g_scan_th > page_grids[pi].th) {
                    g_scan_th = page_grids[pi].th;
                }

                /* Priority area: try old region first to fill gaps */
                placed = scan_region(&page_grids[pi], &page_qtrees[pi], rot_sg, rot_ox, rot_oy, rot_count, margin_tiles, margin_tiles, old_tw, old_th, &best_tx, &best_ty, &best_rot);

                /* Then scan used extent */
                if (!placed) {
                    placed = scan_region(&page_grids[pi], &page_qtrees[pi], rot_sg, rot_ox, rot_oy, rot_count, margin_tiles, margin_tiles, g_scan_tw, g_scan_th, &best_tx, &best_ty, &best_rot);
                }

                if (placed) {
                    best_page = pi;
                }
            }
        }
        // #endregion

        // #region Multi-page overflow: new page only when growth exhausted (ATLAS-18)
        if (!placed) {
            NT_BUILD_ASSERT(page_count < ATLAS_MAX_PAGES && "atlas: too many pages");
            uint32_t new_page = page_count;
            page_count++;
            page_grids[new_page] = tgrid_create(atlas_tw, atlas_th, ts);
            page_qtrees[new_page] = qtree_create(atlas_tw, atlas_th);
            page_w[new_page] = init_dim;
            page_h[new_page] = init_dim;

            placed = scan_region(&page_grids[new_page], &page_qtrees[new_page], rot_sg, rot_ox, rot_oy, rot_count, margin_tiles, margin_tiles, page_grids[new_page].tw, page_grids[new_page].th,
                                 &best_tx, &best_ty, &best_rot);
            if (!placed) {
                /* New page too small — grow it */
                while (!placed && (page_grids[new_page].tw < max_tw || page_grids[new_page].th < max_th)) {
                    uint32_t old_tw = 0;
                    uint32_t old_th = 0;
                    tgrid_grow(&page_grids[new_page], max_tw, max_th, &old_tw, &old_th);
                    qtree_grow(&page_qtrees[new_page], &page_grids[new_page]);
                    page_w[new_page] = page_grids[new_page].tw * ts;
                    page_h[new_page] = page_grids[new_page].th * ts;
                    placed = scan_region(&page_grids[new_page], &page_qtrees[new_page], rot_sg, rot_ox, rot_oy, rot_count, margin_tiles, margin_tiles, page_grids[new_page].tw, page_grids[new_page].th,
                                         &best_tx, &best_ty, &best_rot);
                }
            }
            if (placed) {
                best_page = new_page;
            }
        }
        // #endregion

        NT_BUILD_ASSERT(placed && "tile_pack: failed to place sprite");

        /* Stamp onto page grid and update quadtree */
        tgrid_stamp(&page_grids[best_page], &rot_sg[best_rot], (int32_t)best_tx, (int32_t)best_ty, rot_ox[best_rot], rot_oy[best_rot]);
        {
            int32_t stamp_tx = (int32_t)best_tx + rot_ox[best_rot];
            int32_t stamp_ty = (int32_t)best_ty + rot_oy[best_rot];
            qtree_update_after_stamp(&page_qtrees[best_page], &page_grids[best_page], stamp_tx, stamp_ty, rot_sg[best_rot].tw, rot_sg[best_rot].th);
        }

        /* Track bounding box in pixels */
        int32_t abs_px = ((int32_t)best_tx + rot_ox[best_rot]) * (int32_t)ts;
        int32_t abs_py = ((int32_t)best_ty + rot_oy[best_rot]) * (int32_t)ts;
        uint32_t right = (uint32_t)(abs_px + (int32_t)(rot_sg[best_rot].tw * ts));
        uint32_t bottom = (uint32_t)(abs_py + (int32_t)(rot_sg[best_rot].th * ts));
        if (right > page_max_x[best_page]) {
            page_max_x[best_page] = right;
        }
        if (bottom > page_max_y[best_page]) {
            page_max_y[best_page] = bottom;
        }

        /* Track used extent in tiles (for scan limiting) */
        uint32_t used_right_t = (uint32_t)((int32_t)best_tx + rot_ox[best_rot]) + rot_sg[best_rot].tw;
        uint32_t used_bottom_t = (uint32_t)((int32_t)best_ty + rot_oy[best_rot]) + rot_sg[best_rot].th;
        if (used_right_t > page_used_tw[best_page]) {
            page_used_tw[best_page] = used_right_t;
        }
        if (used_bottom_t > page_used_th[best_page]) {
            page_used_th[best_page] = used_bottom_t;
        }

        /* Record placement — convert tile coords to pixel coords */
        AtlasPlacement *pl = &out_placements[placement_count++];
        pl->sprite_index = idx;
        pl->page = best_page;
        pl->x = best_tx * ts;
        pl->y = best_ty * ts;
        pl->trimmed_w = trim_w[idx];
        pl->trimmed_h = trim_h[idx];
        pl->trim_x = 0;
        pl->trim_y = 0;
        pl->rotation = best_rot;

        /* Free rotated grids (but not rot_sg[0] which is base_sg) */
        if (allow_rotate) {
            tgrid_free(&rot_sg[1]);
            tgrid_free(&rot_sg[2]);
            tgrid_free(&rot_sg[3]);
        }
    }
    // #endregion

    // #region Final page dimensions from bounding box + margin
    for (uint32_t p = 0; p < page_count; p++) {
        uint32_t fw = page_max_x[p] + margin;
        uint32_t fh = page_max_y[p] + margin;
        if (pot) {
            fw = next_pot(fw);
            fh = next_pot(fh);
        }
        if (fw > max_size) {
            fw = max_size;
        }
        if (fh > max_size) {
            fh = max_size;
        }
        out_page_w[p] = fw;
        out_page_h[p] = fh;
    }
    // #endregion

    /* Cleanup */
    for (uint32_t p = 0; p < page_count; p++) {
        tgrid_free(&page_grids[p]);
        qtree_free(&page_qtrees[p]);
    }
    for (uint32_t i = 0; i < sprite_count; i++) {
        tgrid_free(&sprite_grids[i]);
    }
    free(sprite_grids);
    free(grid_ox);
    free(grid_oy);
    free(sorted);

    *out_page_count = page_count;
    return placement_count;
}

/* --- Test-access wrappers (geometry algorithms are static, tests call these) --- */

#ifdef NT_BUILDER_ATLAS_TEST_ACCESS
bool nt_atlas_test_alpha_trim(const uint8_t *rgba, uint32_t w, uint32_t h, uint8_t threshold, uint32_t *ox, uint32_t *oy, uint32_t *ow, uint32_t *oh) {
    uint8_t *ap = alpha_plane_extract(rgba, w, h);
    bool result = alpha_trim(ap, w, h, threshold, ox, oy, ow, oh);
    free(ap);
    return result;
}
uint32_t nt_atlas_test_convex_hull(const void *pts, uint32_t n, void *out) { return convex_hull((const Point2D *)pts, n, (Point2D *)out); }
uint32_t nt_atlas_test_rdp_simplify(const void *hull, uint32_t n, uint32_t max_v, void *out) { return hull_simplify((const Point2D *)hull, n, max_v, (Point2D *)out); }
uint32_t nt_atlas_test_fan_triangulate(uint32_t vc, uint16_t *idx) { return fan_triangulate(vc, idx); }
#endif

/* --- Atlas sprite array growth --- */

static void atlas_grow_sprites(NtBuildAtlasState *state) {
    uint32_t new_cap = state->sprite_capacity * 2;
    NtAtlasSpriteInput *new_arr = (NtAtlasSpriteInput *)realloc(state->sprites, new_cap * sizeof(NtAtlasSpriteInput));
    NT_BUILD_ASSERT(new_arr && "atlas_grow_sprites: realloc failed");
    state->sprites = new_arr;
    state->sprite_capacity = new_cap;
}

/* --- Extract filename with extension from path (D-06) --- */

static const char *extract_filename(const char *path) {
    const char *last = path;
    for (const char *p = path; *p; p++) {
        if (*p == '/' || *p == '\\') {
            last = p + 1;
        }
    }
    return last;
}

/* --- Edge extrude: duplicate border pixels outward (ATLAS-11) --- */

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void extrude_edges(uint8_t *page, uint32_t page_w, uint32_t page_h, uint32_t px, uint32_t py, uint32_t sw, uint32_t sh, uint32_t extrude_count) {
    if (extrude_count == 0) {
        return;
    }
    /* px, py = position of the inner (trimmed) sprite rect within the page.
     * sw, sh = trimmed sprite dimensions. */

    // #region Top and bottom edge extrusion
    for (uint32_t e = 1; e <= extrude_count; e++) {
        /* Top edge: duplicate row py to row py - e */
        if (py >= e) {
            uint32_t dst_y = py - e;
            for (uint32_t x = px; x < px + sw && x < page_w; x++) {
                memcpy(&page[((size_t)dst_y * page_w + x) * 4], &page[((size_t)py * page_w + x) * 4], 4);
            }
        }
        /* Bottom edge: duplicate row py+sh-1 to row py+sh-1+e */
        uint32_t src_y = py + sh - 1;
        uint32_t dst_y = src_y + e;
        if (dst_y < page_h) {
            for (uint32_t x = px; x < px + sw && x < page_w; x++) {
                memcpy(&page[((size_t)dst_y * page_w + x) * 4], &page[((size_t)src_y * page_w + x) * 4], 4);
            }
        }
    }
    // #endregion

    // #region Left and right edge extrusion (includes extruded corner area)
    for (uint32_t e = 1; e <= extrude_count; e++) {
        uint32_t y_start = (py >= extrude_count) ? py - extrude_count : 0;
        uint32_t y_end = py + sh + extrude_count;
        if (y_end > page_h) {
            y_end = page_h;
        }
        /* Left edge */
        if (px >= e) {
            uint32_t dst_x = px - e;
            for (uint32_t y = y_start; y < y_end; y++) {
                memcpy(&page[((size_t)y * page_w + dst_x) * 4], &page[((size_t)y * page_w + px) * 4], 4);
            }
        }
        /* Right edge */
        uint32_t src_x = px + sw - 1;
        uint32_t dst_x = src_x + e;
        if (dst_x < page_w) {
            for (uint32_t y = y_start; y < y_end; y++) {
                memcpy(&page[((size_t)y * page_w + dst_x) * 4], &page[((size_t)y * page_w + src_x) * 4], 4);
            }
        }
    }
    // #endregion
}

/* --- Debug PNG: draw 2px outline around region (D-09, D-10) --- */

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void debug_draw_rect_outline(uint8_t *page, uint32_t page_w, uint32_t page_h, uint32_t rx, uint32_t ry, uint32_t rw, uint32_t rh) {
    /* Bright magenta outline: 255,0,255,255 */
    static const uint8_t color[4] = {255, 0, 255, 255};
    for (uint32_t t = 0; t < 2; t++) { /* 2px border */
        /* Top edge */
        if (ry + t < page_h) {
            for (uint32_t x = rx; x < rx + rw && x < page_w; x++) {
                memcpy(&page[((size_t)(ry + t) * page_w + x) * 4], color, 4);
            }
        }
        /* Bottom edge */
        uint32_t by = ry + rh - 1 - t;
        if (by < page_h && by >= ry) {
            for (uint32_t x = rx; x < rx + rw && x < page_w; x++) {
                memcpy(&page[((size_t)by * page_w + x) * 4], color, 4);
            }
        }
        /* Left edge */
        if (rx + t < page_w) {
            for (uint32_t y = ry; y < ry + rh && y < page_h; y++) {
                memcpy(&page[((size_t)y * page_w + rx + t) * 4], color, 4);
            }
        }
        /* Right edge */
        uint32_t bx = rx + rw - 1 - t;
        if (bx < page_w && bx >= rx) {
            for (uint32_t y = ry; y < ry + rh && y < page_h; y++) {
                memcpy(&page[((size_t)y * page_w + bx) * 4], color, 4);
            }
        }
    }
}

/* --- Debug line drawing (Bresenham) for hull outlines --- */

static void debug_draw_line(uint8_t *page, uint32_t pw, uint32_t ph, int32_t x0, int32_t y0, int32_t x1, int32_t y1, const uint8_t color[4]) {
    int32_t dx = abs(x1 - x0);
    int32_t dy = -abs(y1 - y0);
    int32_t sx = (x0 < x1) ? 1 : -1;
    int32_t sy = (y0 < y1) ? 1 : -1;
    int32_t err = dx + dy;

    for (;;) {
        if (x0 >= 0 && (uint32_t)x0 < pw && y0 >= 0 && (uint32_t)y0 < ph) {
            memcpy(&page[((size_t)y0 * pw + (uint32_t)x0) * 4], color, 4);
        }
        if (x0 == x1 && y0 == y1) {
            break;
        }
        int32_t e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

/* --- Draw convex hull outline on debug page --- */

static void debug_draw_hull_outline(uint8_t *page, uint32_t pw, uint32_t ph, const Point2D *hull, uint32_t vert_count, uint32_t inner_x, uint32_t inner_y, uint32_t trim_w, uint32_t trim_h,
                                    uint8_t rotation) {
    static const uint8_t color[4] = {255, 0, 255, 255};
    if (vert_count < 2) {
        return;
    }

    /* Transform hull vertex from local trimmed-sprite space to atlas pixel coords */
    for (uint32_t i = 0; i < vert_count; i++) {
        uint32_t next = (i + 1) % vert_count;
        int32_t lx0 = hull[i].x;
        int32_t ly0 = hull[i].y;
        int32_t lx1 = hull[next].x;
        int32_t ly1 = hull[next].y;

        int32_t ax0;
        int32_t ay0;
        int32_t ax1;
        int32_t ay1;
        switch (rotation) {
        case 1: /* 90 CW */
            ax0 = (int32_t)inner_x + ((int32_t)trim_h - 1 - ly0);
            ay0 = (int32_t)inner_y + lx0;
            ax1 = (int32_t)inner_x + ((int32_t)trim_h - 1 - ly1);
            ay1 = (int32_t)inner_y + lx1;
            break;
        case 2: /* 180 */
            ax0 = (int32_t)inner_x + ((int32_t)trim_w - 1 - lx0);
            ay0 = (int32_t)inner_y + ((int32_t)trim_h - 1 - ly0);
            ax1 = (int32_t)inner_x + ((int32_t)trim_w - 1 - lx1);
            ay1 = (int32_t)inner_y + ((int32_t)trim_h - 1 - ly1);
            break;
        case 3: /* 270 CW */
            ax0 = (int32_t)inner_x + ly0;
            ay0 = (int32_t)inner_y + ((int32_t)trim_w - 1 - lx0);
            ax1 = (int32_t)inner_x + ly1;
            ay1 = (int32_t)inner_y + ((int32_t)trim_w - 1 - lx1);
            break;
        default: /* 0 */
            ax0 = (int32_t)inner_x + lx0;
            ay0 = (int32_t)inner_y + ly0;
            ax1 = (int32_t)inner_x + lx1;
            ay1 = (int32_t)inner_y + ly1;
            break;
        }

        debug_draw_line(page, pw, ph, ax0, ay0, ax1, ay1, color);
    }
}

/* --- Blit trimmed sprite pixels to atlas page --- */

static void blit_sprite(uint8_t *page, uint32_t page_w, const uint8_t *sprite_rgba, uint32_t sprite_w, uint32_t trim_x, uint32_t trim_y, uint32_t trim_w, uint32_t trim_h, uint32_t dest_x,
                        uint32_t dest_y, uint8_t rotation) {
    /* Blit only non-transparent pixels to avoid overwriting neighbors in polygon mode.
     * Rotation 0: row-scan with alpha skip.
     * Rotations 1/2/3: pixel-by-pixel with coordinate transform + alpha skip. */
    for (uint32_t sy = 0; sy < trim_h; sy++) {
        for (uint32_t sx = 0; sx < trim_w; sx++) {
            const uint8_t *src = &sprite_rgba[((size_t)(trim_y + sy) * sprite_w + trim_x + sx) * 4];
            if (src[3] == 0) {
                continue; /* skip fully transparent pixels */
            }
            uint32_t dx;
            uint32_t dy;
            switch (rotation) {
            case 0:
                dx = dest_x + sx;
                dy = dest_y + sy;
                break;
            case 1:
                dx = dest_x + (trim_h - 1 - sy);
                dy = dest_y + sx;
                break;
            case 2:
                dx = dest_x + (trim_w - 1 - sx);
                dy = dest_y + (trim_h - 1 - sy);
                break;
            case 3:
                dx = dest_x + sy;
                dy = dest_y + (trim_w - 1 - sx);
                break;
            default:
                dx = dest_x + sx;
                dy = dest_y + sy;
                break;
            }
            memcpy(&page[((size_t)dy * page_w + dx) * 4], src, 4);
        }
    }
}

/* --- Atlas API --- */

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void nt_builder_begin_atlas(NtBuilderContext *ctx, const char *name, const nt_atlas_opts_t *opts) {
    NT_BUILD_ASSERT(ctx && "begin_atlas: ctx is NULL");
    NT_BUILD_ASSERT(name && "begin_atlas: name is NULL");
    NT_BUILD_ASSERT(!ctx->active_atlas && "begin_atlas: nested atlas not allowed");

    NtBuildAtlasState *state = (NtBuildAtlasState *)calloc(1, sizeof(NtBuildAtlasState));
    NT_BUILD_ASSERT(state && "begin_atlas: alloc failed");

    state->name = strdup(name);
    NT_BUILD_ASSERT(state->name && "begin_atlas: strdup failed");

    /* Copy opts or use defaults */
    if (opts) {
        state->opts = *opts;
        /* Handle compress pointer: deep-copy, then zero pointer in opts */
        if (opts->compress) {
            state->compress = *opts->compress;
            state->has_compress = true;
        }
    } else {
        state->opts = nt_atlas_opts_defaults();
    }
    state->opts.compress = NULL; /* zeroed -- use has_compress flag */
    NT_BUILD_ASSERT(state->opts.max_vertices <= 32 && "begin_atlas: max_vertices must be <= 32 (tile grid index buffer limit)");
    NT_BUILD_ASSERT(state->opts.tile_size > 0 && state->opts.tile_size <= 32 && "begin_atlas: tile_size must be 1-32");

    /* Initialize sprite array */
    state->sprite_capacity = 64;
    state->sprites = (NtAtlasSpriteInput *)calloc(state->sprite_capacity, sizeof(NtAtlasSpriteInput));
    NT_BUILD_ASSERT(state->sprites && "begin_atlas: alloc failed");
    state->sprite_count = 0;

    ctx->active_atlas = state;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void nt_builder_atlas_add(NtBuilderContext *ctx, const char *path, const char *name_override) {
    NT_BUILD_ASSERT(ctx && path && "atlas_add: invalid args");
    NT_BUILD_ASSERT(ctx->active_atlas && "atlas_add: no active atlas (call begin_atlas first)");

    NtBuildAtlasState *state = ctx->active_atlas;

    /* Resolve file path via asset roots */
    char *resolved_path = nt_builder_find_file(path, NULL, ctx);
    const char *read_path = resolved_path ? resolved_path : path;

    /* Read file */
    uint32_t file_size = 0;
    uint8_t *file_data = (uint8_t *)nt_builder_read_file(read_path, &file_size);
    NT_BUILD_ASSERT(file_data && "atlas_add: failed to read file");
    free(resolved_path);

    /* Decode PNG via stb_image (force RGBA) */
    int w = 0;
    int h = 0;
    int channels = 0;
    uint8_t *pixels = stbi_load_from_memory(file_data, (int)file_size, &w, &h, &channels, 4);
    free(file_data);
    NT_BUILD_ASSERT(pixels && "atlas_add: stbi_load_from_memory failed");

    /* Determine region name (D-06, D-07) */
    const char *region_name = name_override ? name_override : extract_filename(path);

    /* Compute decoded hash */
    uint64_t decoded_hash = nt_hash64(pixels, (uint32_t)w * (uint32_t)h * 4).value;

    /* Grow array if needed */
    if (state->sprite_count >= state->sprite_capacity) {
        atlas_grow_sprites(state);
    }

    /* Fill sprite input */
    NtAtlasSpriteInput *sprite = &state->sprites[state->sprite_count++];
    sprite->rgba = pixels; /* take ownership */
    sprite->width = (uint32_t)w;
    sprite->height = (uint32_t)h;
    sprite->name = strdup(region_name);
    NT_BUILD_ASSERT(sprite->name && "atlas_add: strdup failed");
    sprite->origin_x = 0.5F;
    sprite->origin_y = 0.5F;
    sprite->decoded_hash = decoded_hash;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void nt_builder_atlas_add_raw(NtBuilderContext *ctx, const uint8_t *rgba_pixels, uint32_t width, uint32_t height, const char *name) {
    NT_BUILD_ASSERT(ctx && rgba_pixels && "atlas_add_raw: invalid args");
    NT_BUILD_ASSERT(name && "atlas_add_raw: name is required for raw pixels (D-07)");
    NT_BUILD_ASSERT(ctx->active_atlas && "atlas_add_raw: no active atlas (call begin_atlas first)");

    NtBuildAtlasState *state = ctx->active_atlas;

    /* Deep-copy RGBA pixels */
    uint32_t pixel_bytes = width * height * 4;
    uint8_t *pixels = (uint8_t *)malloc(pixel_bytes);
    NT_BUILD_ASSERT(pixels && "atlas_add_raw: alloc failed");
    memcpy(pixels, rgba_pixels, pixel_bytes);

    /* Compute decoded hash */
    uint64_t decoded_hash = nt_hash64(pixels, pixel_bytes).value;

    /* Grow array if needed */
    if (state->sprite_count >= state->sprite_capacity) {
        atlas_grow_sprites(state);
    }

    /* Fill sprite input */
    NtAtlasSpriteInput *sprite = &state->sprites[state->sprite_count++];
    sprite->rgba = pixels;
    sprite->width = width;
    sprite->height = height;
    sprite->name = strdup(name);
    NT_BUILD_ASSERT(sprite->name && "atlas_add_raw: strdup failed");
    sprite->origin_x = 0.5F;
    sprite->origin_y = 0.5F;
    sprite->decoded_hash = decoded_hash;
}

/* --- Glob callback for atlas --- */

typedef struct {
    NtBuilderContext *ctx;
    uint32_t match_count;
} AtlasGlobData;

static void atlas_glob_callback(const char *full_path, void *user) {
    AtlasGlobData *d = (AtlasGlobData *)user;
    d->match_count++;
    nt_builder_atlas_add(d->ctx, full_path, NULL);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void nt_builder_atlas_add_glob(NtBuilderContext *ctx, const char *pattern) {
    NT_BUILD_ASSERT(ctx && pattern && "atlas_add_glob: invalid args");
    NT_BUILD_ASSERT(ctx->active_atlas && "atlas_add_glob: no active atlas");

    AtlasGlobData data = {.ctx = ctx, .match_count = 0};
    NT_BUILD_ASSERT(nt_builder_glob_iterate(pattern, atlas_glob_callback, &data) && "atlas_add_glob: glob overflow");
    NT_BUILD_ASSERT(data.match_count > 0 && "atlas_add_glob: no files matched pattern");
}

/* --- uint64_t sort comparator for atlas cache key (D-13) --- */

static int uint64_cmp(const void *a, const void *b) {
    uint64_t va = *(const uint64_t *)a;
    uint64_t vb = *(const uint64_t *)b;
    if (va < vb) {
        return -1;
    }
    if (va > vb) {
        return 1;
    }
    return 0;
}

/* --- Atlas cache key computation (D-13) --- */

static uint64_t compute_atlas_cache_key(const NtAtlasSpriteInput *sprites, uint32_t sprite_count, const nt_atlas_opts_t *opts, bool has_compress, const nt_tex_compress_opts_t *compress) {
    /* Collect decoded hashes */
    uint64_t *hashes = (uint64_t *)malloc(sprite_count * sizeof(uint64_t));
    NT_BUILD_ASSERT(hashes && "compute_atlas_cache_key: alloc failed");
    for (uint32_t i = 0; i < sprite_count; i++) {
        hashes[i] = sprites[i].decoded_hash;
    }

    /* Sort for order-independence */
    qsort(hashes, sprite_count, sizeof(uint64_t), uint64_cmp);

    /* Build key buffer: sorted hashes + serialized opts */
    size_t hash_bytes = sprite_count * sizeof(uint64_t);
    /* Serialize opts fields (excluding compress pointer) */
    uint8_t opts_buf[128];
    uint32_t pos = 0;
    memcpy(opts_buf + pos, &opts->max_size, sizeof(opts->max_size));
    pos += (uint32_t)sizeof(opts->max_size);
    memcpy(opts_buf + pos, &opts->padding, sizeof(opts->padding));
    pos += (uint32_t)sizeof(opts->padding);
    memcpy(opts_buf + pos, &opts->margin, sizeof(opts->margin));
    pos += (uint32_t)sizeof(opts->margin);
    memcpy(opts_buf + pos, &opts->extrude, sizeof(opts->extrude));
    pos += (uint32_t)sizeof(opts->extrude);
    memcpy(opts_buf + pos, &opts->alpha_threshold, sizeof(opts->alpha_threshold));
    pos += (uint32_t)sizeof(opts->alpha_threshold);
    memcpy(opts_buf + pos, &opts->max_vertices, sizeof(opts->max_vertices));
    pos += (uint32_t)sizeof(opts->max_vertices);
    memcpy(opts_buf + pos, &opts->format, sizeof(opts->format));
    pos += (uint32_t)sizeof(opts->format);
    memcpy(opts_buf + pos, &opts->tile_size, sizeof(opts->tile_size));
    pos += (uint32_t)sizeof(opts->tile_size);
    uint8_t flags = (uint8_t)((opts->allow_rotate ? 1 : 0) | (opts->power_of_two ? 2 : 0) | (opts->polygon_mode ? 4 : 0) | (opts->debug_png ? 8 : 0));
    opts_buf[pos++] = flags;
    uint8_t hc = has_compress ? 1 : 0;
    opts_buf[pos++] = hc;
    if (has_compress) {
        memcpy(opts_buf + pos, &compress->mode, sizeof(compress->mode));
        pos += (uint32_t)sizeof(compress->mode);
        memcpy(opts_buf + pos, &compress->quality, sizeof(compress->quality));
        pos += (uint32_t)sizeof(compress->quality);
    }

    /* Combine into single buffer and hash */
    size_t total = hash_bytes + pos;
    uint8_t *buf = (uint8_t *)malloc(total);
    NT_BUILD_ASSERT(buf && "compute_atlas_cache_key: alloc failed");
    memcpy(buf, hashes, hash_bytes);
    memcpy(buf + hash_bytes, opts_buf, pos);
    free(hashes);

    nt_hash64_t key = nt_hash64(buf, (uint32_t)total);
    free(buf);
    return key.value;
}

/* --- Atlas cache file I/O (D-13) --- */

/* Cache file layout:
 *   uint32_t placement_count
 *   uint32_t page_count
 *   uint32_t page_w[page_count]
 *   uint32_t page_h[page_count]
 *   AtlasPlacement placements[placement_count]
 *   uint8_t page_pixels[page_count][page_w[i]*page_h[i]*4]
 */

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static bool atlas_cache_write(const char *cache_dir, uint64_t cache_key, const AtlasPlacement *placements, uint32_t placement_count, const uint32_t *page_w, const uint32_t *page_h,
                              uint32_t page_count, uint8_t **page_pixels) {
    char path[1024];
    (void)snprintf(path, sizeof(path), "%s/atlas_%016llx.bin", cache_dir, (unsigned long long)cache_key);
    char tmp_path[1040];
    (void)snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", path);

    FILE *f = fopen(tmp_path, "wb");
    if (!f) {
        return false;
    }

    (void)fwrite(&placement_count, sizeof(uint32_t), 1, f);
    (void)fwrite(&page_count, sizeof(uint32_t), 1, f);
    (void)fwrite(page_w, sizeof(uint32_t), page_count, f);
    (void)fwrite(page_h, sizeof(uint32_t), page_count, f);
    (void)fwrite(placements, sizeof(AtlasPlacement), placement_count, f);
    for (uint32_t p = 0; p < page_count; p++) {
        size_t pixel_bytes = (size_t)page_w[p] * page_h[p] * 4;
        (void)fwrite(page_pixels[p], 1, pixel_bytes, f);
    }

    (void)fclose(f);

    /* Atomic rename */
#ifdef _WIN32
    if (!MoveFileExA(tmp_path, path, MOVEFILE_REPLACE_EXISTING)) {
        (void)remove(tmp_path);
        return false;
    }
#else
    if (rename(tmp_path, path) != 0) {
        (void)remove(tmp_path);
        return false;
    }
#endif
    return true;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static bool atlas_cache_read(const char *cache_dir, uint64_t cache_key, AtlasPlacement **out_placements, uint32_t *out_placement_count, uint32_t *out_page_w, uint32_t *out_page_h,
                             uint32_t *out_page_count, uint8_t ***out_page_pixels) {
    char path[1024];
    (void)snprintf(path, sizeof(path), "%s/atlas_%016llx.bin", cache_dir, (unsigned long long)cache_key);

    FILE *f = fopen(path, "rb");
    if (!f) {
        return false;
    }

    uint32_t placement_count = 0;
    uint32_t page_count_val = 0;
    if (fread(&placement_count, sizeof(uint32_t), 1, f) != 1 || fread(&page_count_val, sizeof(uint32_t), 1, f) != 1) {
        (void)fclose(f);
        return false;
    }

    if (page_count_val == 0 || page_count_val > ATLAS_MAX_PAGES || placement_count == 0 || placement_count > NT_BUILD_MAX_ASSETS) {
        (void)fclose(f);
        return false;
    }

    if (fread(out_page_w, sizeof(uint32_t), page_count_val, f) != page_count_val || fread(out_page_h, sizeof(uint32_t), page_count_val, f) != page_count_val) {
        (void)fclose(f);
        return false;
    }

    /* Validate page dimensions (max 16384 to bound allocation) */
    for (uint32_t p = 0; p < page_count_val; p++) {
        if (out_page_w[p] == 0 || out_page_w[p] > 16384 || out_page_h[p] == 0 || out_page_h[p] > 16384) {
            (void)fclose(f);
            return false;
        }
    }

    /* NOLINTNEXTLINE(clang-analyzer-optin.taint.TaintedAlloc) -- placement_count bounded above */
    AtlasPlacement *placements = (AtlasPlacement *)malloc((size_t)placement_count * sizeof(AtlasPlacement));
    if (!placements) {
        (void)fclose(f);
        return false;
    }
    if (fread(placements, sizeof(AtlasPlacement), placement_count, f) != placement_count) {
        free(placements);
        (void)fclose(f);
        return false;
    }

    /* NOLINTNEXTLINE(clang-analyzer-optin.taint.TaintedAlloc) -- page_count_val bounded above */
    uint8_t **page_pixels_arr = (uint8_t **)calloc((size_t)page_count_val, sizeof(uint8_t *));
    if (!page_pixels_arr) {
        free(placements);
        (void)fclose(f);
        return false;
    }

    for (uint32_t p = 0; p < page_count_val; p++) {
        size_t pixel_bytes = (size_t)out_page_w[p] * out_page_h[p] * 4;
        /* NOLINTNEXTLINE(clang-analyzer-optin.taint.TaintedAlloc) -- dimensions bounded at 16384 above */
        page_pixels_arr[p] = (uint8_t *)malloc(pixel_bytes);
        if (!page_pixels_arr[p] || fread(page_pixels_arr[p], 1, pixel_bytes, f) != pixel_bytes) {
            /* Cleanup on failure */
            for (uint32_t q = 0; q <= p; q++) {
                free(page_pixels_arr[q]);
            }
            free((void *)page_pixels_arr);
            free(placements);
            (void)fclose(f);
            return false;
        }
    }

    (void)fclose(f);

    *out_placements = placements;
    *out_placement_count = placement_count;
    *out_page_count = page_count_val;
    *out_page_pixels = page_pixels_arr;
    return true;
}

/* --- Duplicate detection sort comparator --- */

typedef struct {
    uint32_t index;
    uint64_t hash;
} DedupSortEntry;

static int dedup_sort_cmp(const void *a, const void *b) {
    const DedupSortEntry *ea = (const DedupSortEntry *)a;
    const DedupSortEntry *eb = (const DedupSortEntry *)b;
    if (ea->hash < eb->hash) {
        return -1;
    }
    if (ea->hash > eb->hash) {
        return 1;
    }
    return 0;
}

/* --- end_atlas: the main atlas pipeline --- */

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void nt_builder_end_atlas(NtBuilderContext *ctx) {
    NT_BUILD_ASSERT(ctx && "end_atlas: ctx is NULL");
    NT_BUILD_ASSERT(ctx->active_atlas && "end_atlas: no active atlas");

    NtBuildAtlasState *state = ctx->active_atlas;
    NT_BUILD_ASSERT(state->sprite_count > 0 && "end_atlas: atlas has no sprites");

    uint32_t sprite_count = state->sprite_count;
    NtAtlasSpriteInput *sprites = state->sprites;
    const nt_atlas_opts_t *opts = &state->opts;

    // #region Step 1: Extract alpha planes + alpha-trim all sprites (ATLAS-05)
    uint32_t *trim_x = (uint32_t *)calloc(sprite_count, sizeof(uint32_t));
    uint32_t *trim_y = (uint32_t *)calloc(sprite_count, sizeof(uint32_t));
    uint32_t *trim_w = (uint32_t *)calloc(sprite_count, sizeof(uint32_t));
    uint32_t *trim_h = (uint32_t *)calloc(sprite_count, sizeof(uint32_t));
    uint8_t **alpha_planes = (uint8_t **)calloc(sprite_count, sizeof(uint8_t *));
    NT_BUILD_ASSERT(trim_x && trim_y && trim_w && trim_h && alpha_planes && "end_atlas: alloc failed");

    for (uint32_t i = 0; i < sprite_count; i++) {
        alpha_planes[i] = alpha_plane_extract(sprites[i].rgba, sprites[i].width, sprites[i].height);
        bool has_pixels = alpha_trim(alpha_planes[i], sprites[i].width, sprites[i].height, opts->alpha_threshold, &trim_x[i], &trim_y[i], &trim_w[i], &trim_h[i]);
        NT_BUILD_ASSERT(has_pixels && "end_atlas: sprite is fully transparent");
    }
    // #endregion

    // #region Atlas cache key computation (D-13)
    state->cache_key = compute_atlas_cache_key(sprites, sprite_count, opts, state->has_compress, &state->compress);
    // #endregion

    // #region Step 2: Duplicate detection (ATLAS-12)
    DedupSortEntry *dedup_entries = (DedupSortEntry *)malloc(sprite_count * sizeof(DedupSortEntry));
    NT_BUILD_ASSERT(dedup_entries && "end_atlas: alloc failed");
    for (uint32_t i = 0; i < sprite_count; i++) {
        dedup_entries[i].index = i;
        dedup_entries[i].hash = sprites[i].decoded_hash;
    }
    qsort(dedup_entries, sprite_count, sizeof(DedupSortEntry), dedup_sort_cmp);

    /* Map duplicate -> original. -1 = unique. */
    int32_t *dedup_map = (int32_t *)malloc(sprite_count * sizeof(int32_t));
    NT_BUILD_ASSERT(dedup_map && "end_atlas: alloc failed");
    for (uint32_t i = 0; i < sprite_count; i++) {
        dedup_map[i] = -1;
    }

    for (uint32_t i = 1; i < sprite_count; i++) {
        if (dedup_entries[i].hash == dedup_entries[i - 1].hash) {
            uint32_t curr_idx = dedup_entries[i].index;
            uint32_t prev_idx = dedup_entries[i - 1].index;
            /* Find the original (follow chain) */
            uint32_t orig = prev_idx;
            while (dedup_map[orig] >= 0) {
                orig = (uint32_t)dedup_map[orig];
            }
            /* Verify trimmed pixels match (dims + byte-level comparison) */
            if (trim_w[curr_idx] == trim_w[orig] && trim_h[curr_idx] == trim_h[orig]) {
                bool pixels_match = true;
                uint32_t tw = trim_w[curr_idx];
                uint32_t th = trim_h[curr_idx];
                for (uint32_t row = 0; row < th && pixels_match; row++) {
                    size_t off_a = (((size_t)(trim_y[curr_idx] + row) * sprites[curr_idx].width) + trim_x[curr_idx]) * 4;
                    size_t off_b = (((size_t)(trim_y[orig] + row) * sprites[orig].width) + trim_x[orig]) * 4;
                    const uint8_t *row_a = sprites[curr_idx].rgba + off_a;
                    const uint8_t *row_b = sprites[orig].rgba + off_b;
                    if (memcmp(row_a, row_b, ((size_t)tw) * 4) != 0) {
                        pixels_match = false;
                    }
                }
                if (pixels_match) {
                    dedup_map[curr_idx] = (int32_t)orig;
                }
            }
        }
    }
    free(dedup_entries);

    /* Count unique sprites */
    uint32_t unique_count = 0;
    uint32_t *unique_indices = (uint32_t *)malloc(sprite_count * sizeof(uint32_t));
    NT_BUILD_ASSERT(unique_indices && "end_atlas: alloc failed");
    for (uint32_t i = 0; i < sprite_count; i++) {
        if (dedup_map[i] < 0) {
            unique_indices[unique_count++] = i;
        }
    }
    // #endregion

    // #region Step 3: Geometry (convex hull + simplification) (ATLAS-06, ATLAS-07)
    /* For each unique sprite, compute hull vertices */
    uint32_t *vertex_counts = (uint32_t *)calloc(sprite_count, sizeof(uint32_t));
    Point2D **hull_vertices = (Point2D **)calloc(sprite_count, sizeof(Point2D *));
    NT_BUILD_ASSERT(vertex_counts && hull_vertices && "end_atlas: alloc failed");

    for (uint32_t ui = 0; ui < unique_count; ui++) {
        uint32_t idx = unique_indices[ui];
        uint32_t tw = trim_w[idx];
        uint32_t th = trim_h[idx];

        if (opts->polygon_mode) {
            /* Extract boundary pixels from alpha plane (dense, cache-friendly) */
            const uint8_t *ap = alpha_planes[idx];
            uint32_t aw = sprites[idx].width;
            /* Collect boundary pixel CORNERS (not centers) so that the convex hull
             * passes along the outer edge of pixels, fully covering all opaque area.
             * Each boundary pixel contributes 4 corner points (x,y), (x+1,y), (x,y+1), (x+1,y+1). */
            uint32_t pt_count = 0;
            Point2D *pts = (Point2D *)malloc((size_t)tw * th * 4 * sizeof(Point2D)); /* 4 corners per pixel */
            NT_BUILD_ASSERT(pts && "end_atlas: alloc failed");

            for (uint32_t y = 0; y < th; y++) {
                for (uint32_t x = 0; x < tw; x++) {
                    uint8_t a = ap[((size_t)(trim_y[idx] + y) * aw) + trim_x[idx] + x];
                    if (a >= opts->alpha_threshold) {
                        bool is_boundary = (x == 0 || y == 0 || x == tw - 1 || y == th - 1);
                        if (!is_boundary) {
                            size_t base = ((size_t)(trim_y[idx] + y) * aw) + trim_x[idx] + x;
                            uint8_t left = ap[base - 1];
                            uint8_t right = ap[base + 1];
                            uint8_t up = ap[base - aw];
                            uint8_t down = ap[base + aw];
                            is_boundary = (left < opts->alpha_threshold || right < opts->alpha_threshold || up < opts->alpha_threshold || down < opts->alpha_threshold);
                        }
                        if (is_boundary) {
                            int32_t px = (int32_t)x;
                            int32_t py = (int32_t)y;
                            pts[pt_count++] = (Point2D){px, py};
                            pts[pt_count++] = (Point2D){px + 1, py};
                            pts[pt_count++] = (Point2D){px, py + 1};
                            pts[pt_count++] = (Point2D){px + 1, py + 1};
                        }
                    }
                }
            }

            if (pt_count < 3) {
                /* Degenerate: use rect */
                free(pts);
                hull_vertices[idx] = (Point2D *)malloc(4 * sizeof(Point2D));
                NT_BUILD_ASSERT(hull_vertices[idx] && "end_atlas: alloc failed");
                hull_vertices[idx][0] = (Point2D){0, 0};
                hull_vertices[idx][1] = (Point2D){(int32_t)tw, 0};
                hull_vertices[idx][2] = (Point2D){(int32_t)tw, (int32_t)th};
                hull_vertices[idx][3] = (Point2D){0, (int32_t)th};
                vertex_counts[idx] = 4;
            } else {
                /* Compute convex hull */
                Point2D *hull = (Point2D *)malloc((size_t)pt_count * 2 * sizeof(Point2D));
                NT_BUILD_ASSERT(hull && "end_atlas: alloc failed");
                uint32_t hull_count = convex_hull(pts, pt_count, hull);

                /* Simplify hull to max_vertices (min-area vertex removal).
                 * Vertex removal makes polygon smaller (chords cut inside).
                 * Then inflate outward until all boundary pixels are covered. */
                Point2D *simplified = (Point2D *)malloc(hull_count * sizeof(Point2D));
                NT_BUILD_ASSERT(simplified && "end_atlas: alloc failed");
                uint32_t simp_count = hull_simplify(hull, hull_count, opts->max_vertices, simplified);
                free(hull);

                /* Determine polygon winding (CCW or CW) via signed area */
                double signed_area = 0.0;
                for (uint32_t si = 0; si < simp_count; si++) {
                    uint32_t sj = (si + 1) % simp_count;
                    signed_area += ((double)simplified[si].x * simplified[sj].y) - ((double)simplified[sj].x * simplified[si].y);
                }
                /* signed_area > 0 = CCW, < 0 = CW. For CCW: cross > 0 = inside. */
                double outside_sign = (signed_area > 0) ? -1.0 : 1.0;

                /* Find max distance from any boundary pixel OUTSIDE simplified polygon.
                 * A point is outside if cross(edge, point) has the "outside" sign. */
                float max_outside_dist = 0.0F;
                for (uint32_t bi = 0; bi < pt_count; bi++) {
                    for (uint32_t si = 0; si < simp_count; si++) {
                        uint32_t sj = (si + 1) % simp_count;
                        double ex = (double)(simplified[sj].x - simplified[si].x);
                        double ey = (double)(simplified[sj].y - simplified[si].y);
                        double len_sq = (ex * ex) + (ey * ey);
                        if (len_sq < 1e-12) {
                            continue;
                        }
                        double cross = (ex * (double)(pts[bi].y - simplified[si].y)) - (ey * (double)(pts[bi].x - simplified[si].x));
                        /* cross * outside_sign > 0 means point is outside this edge */
                        if ((cross * outside_sign) > 0) {
                            double dist = fabs(cross) / sqrt(len_sq);
                            if ((float)dist > max_outside_dist) {
                                max_outside_dist = (float)dist;
                            }
                        }
                    }
                }

                /* Inflate simplified polygon outward by max_outside_dist + 1px margin */
                float inflate_amount = max_outside_dist + 1.0F;
                Point2D *inflated = (Point2D *)malloc(simp_count * sizeof(Point2D));
                NT_BUILD_ASSERT(inflated && "end_atlas: alloc failed");
                uint32_t inf_count = polygon_inflate(simplified, simp_count, inflate_amount, inflated);
                free(simplified);

                /* Note: inflated polygon may extend beyond [0,tw]x[0,th] trimmed rect.
                 * This is intentional — clamping would break convexity and coverage.
                 * Runtime UV mapping handles out-of-bounds coordinates naturally. */

                /* Verify: all boundary pixels must be inside the inflated polygon.
                 * Test via fan triangulation (same as runtime rendering). */
                for (uint32_t bi = 0; bi < pt_count; bi++) {
                    bool inside = false;
                    for (uint32_t ti = 1; ti + 1 < inf_count && !inside; ti++) {
                        /* Triangle: inflated[0], inflated[ti], inflated[ti+1] */
                        int64_t d0 = cross2d(inflated[0], inflated[ti], pts[bi]);
                        int64_t d1 = cross2d(inflated[ti], inflated[ti + 1], pts[bi]);
                        int64_t d2 = cross2d(inflated[ti + 1], inflated[0], pts[bi]);
                        bool all_neg = (d0 <= 0) && (d1 <= 0) && (d2 <= 0);
                        bool all_pos = (d0 >= 0) && (d1 >= 0) && (d2 >= 0);
                        if (all_neg || all_pos) {
                            inside = true;
                        }
                    }
                    if (!inside) {
                        NT_LOG_ERROR("boundary pixel (%d,%d) outside inflated polygon (sprite %ux%u, inflate=%.1f)", pts[bi].x, pts[bi].y, tw, th, (double)inflate_amount);
                    }
                    NT_BUILD_ASSERT(inside && "end_atlas: boundary pixel outside inflated polygon");
                }
                free(pts);

                hull_vertices[idx] = inflated;
                vertex_counts[idx] = inf_count;
            }
        } else {
            /* Rect mode: 4-vertex rect */
            hull_vertices[idx] = (Point2D *)malloc(4 * sizeof(Point2D));
            NT_BUILD_ASSERT(hull_vertices[idx] && "end_atlas: alloc failed");
            hull_vertices[idx][0] = (Point2D){0, 0};
            hull_vertices[idx][1] = (Point2D){(int32_t)tw, 0};
            hull_vertices[idx][2] = (Point2D){(int32_t)tw, (int32_t)th};
            hull_vertices[idx][3] = (Point2D){0, (int32_t)th};
            vertex_counts[idx] = 4;
        }
    }

    /* Copy vertex data for duplicates from their originals */
    for (uint32_t i = 0; i < sprite_count; i++) {
        if (dedup_map[i] >= 0) {
            uint32_t orig = (uint32_t)dedup_map[i];
            vertex_counts[i] = vertex_counts[orig];
            hull_vertices[i] = hull_vertices[orig]; /* shared pointer, don't double-free */
        }
    }
    // #endregion

    // #region Steps 4-5: Tile packing + composition (with atlas cache D-13)
    AtlasPlacement *placements = NULL;
    uint32_t placement_count = 0;
    uint32_t page_count = 0;
    uint32_t page_w[ATLAS_MAX_PAGES];
    uint32_t page_h[ATLAS_MAX_PAGES];
    uint8_t **page_pixels = NULL;
    uint32_t extrude_val = opts->extrude;
    bool cache_hit = false;

    /* Check atlas cache (D-13) */
    if (ctx->cache_dir) {
        cache_hit = atlas_cache_read(ctx->cache_dir, state->cache_key, &placements, &placement_count, page_w, page_h, &page_count, &page_pixels);
        if (cache_hit) {
            NT_LOG_INFO("Atlas cache hit: %s (key %016llx)", state->name, (unsigned long long)state->cache_key);
        }
    }

    if (!cache_hit) {
        // #region Step 4: Tile packing
        // NOLINTNEXTLINE(clang-analyzer-optin.portability.UnixAPI)
        placements = (AtlasPlacement *)malloc(unique_count * sizeof(AtlasPlacement));
        NT_BUILD_ASSERT(placements && "end_atlas: alloc failed");

        /* Build arrays for unique sprites: trim dims + hull polygons */
        uint32_t *u_trim_w = (uint32_t *)malloc(unique_count * sizeof(uint32_t));
        uint32_t *u_trim_h = (uint32_t *)malloc(unique_count * sizeof(uint32_t));
        Point2D **u_hulls = (Point2D **)malloc(unique_count * sizeof(Point2D *));
        uint32_t *u_hull_counts = (uint32_t *)malloc(unique_count * sizeof(uint32_t));
        NT_BUILD_ASSERT(u_trim_w && u_trim_h && u_hulls && u_hull_counts && "end_atlas: alloc failed");
        for (uint32_t i = 0; i < unique_count; i++) {
            uint32_t oi = unique_indices[i];
            u_trim_w[i] = trim_w[oi];
            u_trim_h[i] = trim_h[oi];
            u_hulls[i] = hull_vertices[oi];
            u_hull_counts[i] = vertex_counts[oi];
        }

        placement_count = tile_pack(u_trim_w, u_trim_h, u_hulls, u_hull_counts, unique_count, opts, placements, &page_count, page_w, page_h);

        /* Fill trim offsets and remap sprite_index back to original */
        for (uint32_t i = 0; i < placement_count; i++) {
            uint32_t unique_idx = placements[i].sprite_index;
            uint32_t orig_idx = unique_indices[unique_idx];
            placements[i].sprite_index = orig_idx;
            placements[i].trim_x = trim_x[orig_idx];
            placements[i].trim_y = trim_y[orig_idx];
            placements[i].trimmed_w = trim_w[orig_idx];
            placements[i].trimmed_h = trim_h[orig_idx];
        }

        free(u_trim_w);
        free(u_trim_h);
        free((void *)u_hulls);
        free(u_hull_counts);
        // #endregion

        // #region Step 5: Compose page RGBA + extrude + debug PNG (ATLAS-15, ATLAS-11, D-09)
        page_pixels = (uint8_t **)calloc(page_count, sizeof(uint8_t *));
        NT_BUILD_ASSERT(page_pixels && "end_atlas: alloc failed");

        for (uint32_t p = 0; p < page_count; p++) {
            page_pixels[p] = (uint8_t *)calloc((size_t)page_w[p] * page_h[p] * 4, 1);
            NT_BUILD_ASSERT(page_pixels[p] && "end_atlas: page alloc failed");
        }

        /* Blit each placed sprite */
        for (uint32_t pi = 0; pi < placement_count; pi++) {
            AtlasPlacement *pl = &placements[pi];
            uint32_t idx = pl->sprite_index;
            uint32_t inner_x = pl->x + extrude_val;
            uint32_t inner_y = pl->y + extrude_val;

            /* Blit trimmed pixels */
            blit_sprite(page_pixels[pl->page], page_w[pl->page], sprites[idx].rgba, sprites[idx].width, pl->trim_x, pl->trim_y, pl->trimmed_w, pl->trimmed_h, inner_x, inner_y, pl->rotation);

            /* Extrude edge pixels (ATLAS-11) */
            uint32_t blit_w = (pl->rotation == 1 || pl->rotation == 3) ? pl->trimmed_h : pl->trimmed_w;
            uint32_t blit_h = (pl->rotation == 1 || pl->rotation == 3) ? pl->trimmed_w : pl->trimmed_h;
            extrude_edges(page_pixels[pl->page], page_w[pl->page], page_h[pl->page], inner_x, inner_y, blit_w, blit_h, extrude_val);
        }

        /* Debug PNG output (D-09, D-10) */
        if (opts->debug_png) {
            for (uint32_t p = 0; p < page_count; p++) {
                /* Draw outlines on a copy */
                size_t page_bytes = (size_t)page_w[p] * page_h[p] * 4;
                uint8_t *debug_page = (uint8_t *)malloc(page_bytes);
                NT_BUILD_ASSERT(debug_page && "end_atlas: debug alloc failed");
                memcpy(debug_page, page_pixels[p], page_bytes);

                for (uint32_t pi = 0; pi < placement_count; pi++) {
                    if (placements[pi].page != p) {
                        continue;
                    }
                    uint32_t ix = placements[pi].x + extrude_val;
                    uint32_t iy = placements[pi].y + extrude_val;
                    uint32_t si = placements[pi].sprite_index;

                    if (opts->polygon_mode && hull_vertices[si] && vertex_counts[si] >= 3) {
                        debug_draw_hull_outline(debug_page, page_w[p], page_h[p], hull_vertices[si], vertex_counts[si], ix, iy, trim_w[si], trim_h[si], placements[pi].rotation);
                    } else {
                        uint32_t rw = (placements[pi].rotation == 1 || placements[pi].rotation == 3) ? placements[pi].trimmed_h : placements[pi].trimmed_w;
                        uint32_t rh = (placements[pi].rotation == 1 || placements[pi].rotation == 3) ? placements[pi].trimmed_w : placements[pi].trimmed_h;
                        debug_draw_rect_outline(debug_page, page_w[p], page_h[p], ix, iy, rw, rh);
                    }
                }

                /* Write debug PNG next to the pack output file */
                char debug_path[512];
                const char *slash = strrchr(ctx->output_path, '/');
                const char *bslash = strrchr(ctx->output_path, '\\');
                const char *sep = (bslash > slash) ? bslash : slash;
                if (sep) {
                    size_t dir_len = (size_t)(sep - ctx->output_path) + 1;
                    (void)snprintf(debug_path, sizeof(debug_path), "%.*s%s_page%u.png", (int)dir_len, ctx->output_path, state->name, p);
                } else {
                    (void)snprintf(debug_path, sizeof(debug_path), "%s_page%u.png", state->name, p);
                }
                stbi_write_png(debug_path, (int)page_w[p], (int)page_h[p], 4, debug_page, (int)(page_w[p] * 4));
                NT_LOG_INFO("Debug PNG: %s (%ux%u)", debug_path, page_w[p], page_h[p]);
                free(debug_page);
            }
        }
        // #endregion

        /* Write atlas cache on miss (D-13) */
        if (ctx->cache_dir) {
            if (atlas_cache_write(ctx->cache_dir, state->cache_key, placements, placement_count, page_w, page_h, page_count, page_pixels)) {
                NT_LOG_INFO("Atlas cache stored: %s (key %016llx)", state->name, (unsigned long long)state->cache_key);
            }
        }
    }
    // #endregion

    // #region Step 6: Compute atlas UVs and serialize metadata (REGION-01)
    /* Count total vertices */
    uint32_t total_vertex_count = 0;
    for (uint32_t i = 0; i < sprite_count; i++) {
        total_vertex_count += vertex_counts[i];
    }

    /* Build placement lookup: original_sprite_index -> placement index */
    /* For duplicates, the lookup points to the original's placement */
    uint32_t *placement_lookup = (uint32_t *)malloc(sprite_count * sizeof(uint32_t));
    NT_BUILD_ASSERT(placement_lookup && "end_atlas: alloc failed");
    memset(placement_lookup, 0xFF, sprite_count * sizeof(uint32_t)); /* UINT32_MAX = unset */

    for (uint32_t pi = 0; pi < placement_count; pi++) {
        placement_lookup[placements[pi].sprite_index] = pi;
    }
    /* Fill duplicate lookups */
    for (uint32_t i = 0; i < sprite_count; i++) {
        if (dedup_map[i] >= 0) {
            uint32_t orig = (uint32_t)dedup_map[i];
            placement_lookup[i] = placement_lookup[orig];
        }
    }

    /* Serialize blob: header + texture_resource_ids + regions + vertices */
    uint32_t regions_offset = (uint32_t)sizeof(NtAtlasHeader) + (page_count * (uint32_t)sizeof(uint64_t));
    uint32_t vertex_offset = regions_offset + (sprite_count * (uint32_t)sizeof(NtAtlasRegion));
    uint32_t blob_size = vertex_offset + (total_vertex_count * (uint32_t)sizeof(NtAtlasVertex));
    uint8_t *blob = (uint8_t *)calloc(1, blob_size);
    NT_BUILD_ASSERT(blob && "end_atlas: blob alloc failed");

    /* Header */
    NtAtlasHeader *hdr = (NtAtlasHeader *)blob;
    hdr->magic = NT_ATLAS_MAGIC;
    hdr->version = NT_ATLAS_VERSION;
    hdr->region_count = (uint16_t)sprite_count;
    hdr->page_count = (uint16_t)page_count;
    hdr->_pad = 0;
    hdr->vertex_offset = vertex_offset;
    hdr->total_vertex_count = total_vertex_count;

    /* Texture resource IDs (D-05) -- use memcpy for unaligned access safety */
    uint8_t *tex_ids_ptr = blob + sizeof(NtAtlasHeader);
    for (uint32_t p = 0; p < page_count; p++) {
        char tex_path[512];
        (void)snprintf(tex_path, sizeof(tex_path), "%s/tex%u", state->name, p);
        uint64_t tid = nt_hash64_str(tex_path).value;
        memcpy(tex_ids_ptr + ((size_t)p * sizeof(uint64_t)), &tid, sizeof(uint64_t));
    }

    /* Regions */
    NtAtlasRegion *regions = (NtAtlasRegion *)(blob + regions_offset);
    NtAtlasVertex *vertices = (NtAtlasVertex *)(blob + vertex_offset);
    uint32_t vertex_cursor = 0;

    for (uint32_t i = 0; i < sprite_count; i++) {
        uint32_t pi = placement_lookup[i];
        NT_BUILD_ASSERT(pi != UINT32_MAX && "end_atlas: sprite has no placement");
        AtlasPlacement *pl = &placements[pi];

        NtAtlasRegion *reg = &regions[i];
        reg->name_hash = nt_hash64_str(sprites[i].name).value;
        reg->source_w = (uint16_t)sprites[i].width;
        reg->source_h = (uint16_t)sprites[i].height;
        reg->trim_offset_x = (int16_t)trim_x[i];
        reg->trim_offset_y = (int16_t)trim_y[i];
        reg->origin_x = sprites[i].origin_x;
        reg->origin_y = sprites[i].origin_y;
        reg->vertex_start = (uint16_t)vertex_cursor;
        reg->vertex_count = (uint8_t)vertex_counts[i];
        reg->page_index = (uint8_t)pl->page;
        reg->rotated = pl->rotation;
        memset(reg->_pad, 0, sizeof(reg->_pad));

        /* Write vertices with atlas UVs */
        uint32_t inner_x = pl->x + extrude_val;
        uint32_t inner_y = pl->y + extrude_val;
        uint32_t atlas_w = page_w[pl->page];
        uint32_t atlas_h = page_h[pl->page];

        for (uint32_t v = 0; v < vertex_counts[i]; v++) {
            NtAtlasVertex *vtx = &vertices[vertex_cursor++];
            int32_t lx = hull_vertices[i][v].x;
            int32_t ly = hull_vertices[i][v].y;
            vtx->local_x = (int16_t)lx;
            vtx->local_y = (int16_t)ly;

            /* Transform local position to atlas UV coordinates.
             * Handle rotation: local coords are in trimmed-sprite space.
             * For rotation 0: atlas_px = inner_x + lx
             * For rotation 1 (90CW): atlas_px = inner_x + (trim_h - 1 - ly), atlas_py = inner_y + lx
             * For rotation 2 (180): atlas_px = inner_x + (trim_w - 1 - lx), atlas_py = inner_y + (trim_h - 1 - ly)
             * For rotation 3 (270CW): atlas_px = inner_x + ly, atlas_py = inner_y + (trim_w - 1 - lx) */
            float atlas_px;
            float atlas_py;
            switch (pl->rotation) {
            case 0:
                atlas_px = (float)inner_x + (float)lx;
                atlas_py = (float)inner_y + (float)ly;
                break;
            case 1:
                atlas_px = (float)inner_x + (float)((int32_t)trim_h[pl->sprite_index] - 1 - ly);
                atlas_py = (float)inner_y + (float)lx;
                break;
            case 2:
                atlas_px = (float)inner_x + (float)((int32_t)trim_w[pl->sprite_index] - 1 - lx);
                atlas_py = (float)inner_y + (float)((int32_t)trim_h[pl->sprite_index] - 1 - ly);
                break;
            case 3:
                atlas_px = (float)inner_x + (float)ly;
                atlas_py = (float)inner_y + (float)((int32_t)trim_w[pl->sprite_index] - 1 - lx);
                break;
            default:
                atlas_px = (float)inner_x + (float)lx;
                atlas_py = (float)inner_y + (float)ly;
                break;
            }

            vtx->atlas_u = (uint16_t)(((atlas_px * 65535.0F) / (float)atlas_w) + 0.5F);
            vtx->atlas_v = (uint16_t)(((atlas_py * 65535.0F) / (float)atlas_h) + 0.5F);
        }
    }

    NT_BUILD_ASSERT(vertex_cursor == total_vertex_count && "end_atlas: vertex count mismatch");
    // #endregion

    // #region Step 7: Register atlas metadata entry (D-04)
    uint64_t blob_hash = nt_hash64(blob, blob_size).value;
    nt_builder_add_entry(ctx, state->name, NT_BUILD_ASSET_ATLAS, NULL, blob, blob_size, blob_hash);
    // #endregion

    // #region Step 8: Register texture page entries
    for (uint32_t p = 0; p < page_count; p++) {
        char tex_path[512];
        (void)snprintf(tex_path, sizeof(tex_path), "%s/tex%u", state->name, p);

        uint32_t pixel_bytes = page_w[p] * page_h[p] * 4;
        uint64_t tex_hash = nt_hash64(page_pixels[p], pixel_bytes).value;

        /* Create NtBuildTextureData for the encode pipeline */
        NtBuildTextureData *td = (NtBuildTextureData *)calloc(1, sizeof(NtBuildTextureData));
        NT_BUILD_ASSERT(td && "end_atlas: alloc failed");
        td->width = page_w[p];
        td->height = page_h[p];
        td->opts.format = opts->format;
        td->opts.max_size = 0; /* already at final size */
        td->opts.compress = NULL;
        if (state->has_compress) {
            td->compress = state->compress;
            td->has_compress = true;
        }
        /* Page pixels are decoded_data -- owned by the entry */
        nt_builder_add_entry(ctx, tex_path, NT_BUILD_ASSET_TEXTURE, td, page_pixels[p], pixel_bytes, tex_hash);
        page_pixels[p] = NULL; /* ownership transferred to entry */
    }
    // #endregion

    // #region Step 9: Store region info for codegen (no pack entries needed)
    for (uint32_t i = 0; i < sprite_count; i++) {
        /* Grow region array if needed */
        if (ctx->atlas_region_count >= ctx->atlas_region_capacity) {
            uint32_t new_cap = (ctx->atlas_region_capacity == 0) ? 64 : ctx->atlas_region_capacity * 2;
            NtAtlasRegionCodegen *new_arr = (NtAtlasRegionCodegen *)realloc(ctx->atlas_regions, new_cap * sizeof(NtAtlasRegionCodegen));
            NT_BUILD_ASSERT(new_arr && "end_atlas: region codegen alloc failed");
            ctx->atlas_regions = new_arr;
            ctx->atlas_region_capacity = new_cap;
        }

        char region_path[512];
        (void)snprintf(region_path, sizeof(region_path), "%s/%s", state->name, sprites[i].name);

        NtAtlasRegionCodegen *reg = &ctx->atlas_regions[ctx->atlas_region_count++];
        reg->path = nt_builder_normalize_path(region_path);
        reg->resource_id = nt_hash64_str(reg->path).value;
    }
    // #endregion

    // #region Step 10: Cleanup
    /* Free sprite RGBA pixels and names */
    for (uint32_t i = 0; i < sprite_count; i++) {
        free(sprites[i].rgba);
        sprites[i].rgba = NULL;
        free(sprites[i].name);
        sprites[i].name = NULL;
    }

    /* Free hull vertices (careful: duplicates share pointers) */
    for (uint32_t i = 0; i < sprite_count; i++) {
        if (dedup_map[i] < 0 && hull_vertices[i]) {
            free(hull_vertices[i]);
        }
        hull_vertices[i] = NULL;
    }

    /* Free alpha planes */
    for (uint32_t i = 0; i < sprite_count; i++) {
        free(alpha_planes[i]);
    }
    free((void *)alpha_planes);

    /* Free temporary arrays */
    free(trim_x);
    free(trim_y);
    free(trim_w);
    free(trim_h);
    free(dedup_map);
    free(unique_indices);
    free(vertex_counts);
    free((void *)hull_vertices);
    free(placements);
    free(placement_lookup);

    /* Free remaining page pixels (any not transferred to entries) */
    for (uint32_t p = 0; p < page_count; p++) {
        free(page_pixels[p]);
    }
    free((void *)page_pixels);

    /* Free atlas state */
    free(state->sprites);
    free(state->name);
    free(state);
    ctx->active_atlas = NULL;
    ctx->atlas_count++;
    // #endregion

    NT_LOG_INFO("Atlas packed: %u sprites (%u unique), %u pages", sprite_count, unique_count, page_count);
}
