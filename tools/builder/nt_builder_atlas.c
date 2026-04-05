/* clang-format off */
#include "nt_builder_internal.h"
#include "hash/nt_hash.h"
#include "nt_atlas_format.h"
#include "time/nt_time.h"
#include "stb_image.h"
#include "stb_image_write.h"
/* clang-format on */

#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

/* ===================================================================
 * Atlas Builder — sprite atlas packing pipeline
 * ===================================================================
 *
 * Packs individual sprite images into atlas texture pages.
 * Produces binary atlas metadata (NtAtlasHeader + regions + vertices)
 * and RGBA texture pages (fed into the texture encode pipeline).
 *
 * -- Pipeline (nt_builder_end_atlas) ----------------------------------
 *
 *  pipeline_alpha_trim     Extract alpha plane, find tight bounding box
 *  pipeline_cache_check    Hash inputs, return early on cache hit
 *  pipeline_dedup          Detect duplicate sprites (hash + pixels)
 *  pipeline_geometry       Convex hull -> simplify -> inflate polygon
 *  --- skip on cache hit ---
 *  pipeline_tile_pack      Place sprites on atlas pages (tile-grid collision)
 *  pipeline_compose        Blit trimmed pixels + extrude edges
 *  pipeline_debug_png      Optional outline visualization
 *  pipeline_cache_write    Store result for next build
 *  --- always ---
 *  pipeline_serialize      Compute atlas UVs, write binary blob
 *  pipeline_register       Add atlas + texture entries to context
 *  pipeline_cleanup        Free all temporary allocations
 *
 * -- File layout ------------------------------------------------------
 *
 *  Toolbox regions contain helper functions grouped by concern.
 *  Pipeline step functions (pipeline_*) and the orchestrator
 *  (nt_builder_end_atlas) are at the bottom of the file.
 *
 * -- Packing strategy -------------------------------------------------
 *
 *  Sprites sorted by area (descending), placed one by one.
 *  Each sprite alpha silhouette is:
 *    1. Convex hull (Andrews monotone chain)
 *    2. Simplified to max_vertices (min-area vertex removal)
 *    3. Inflated to cover all boundary pixels
 *    4. Rasterized onto tile grid (1 bit per tile_size x tile_size cell)
 *
 *  Placement uses multi-level acceleration:
 *    - Coarse grid (8x8 tile blocks) for y-skip and x-skip
 *    - x4 downsampled OR-mask for cheap ty rejection
 *    - Sparse table OR-mask for O(row_words) exact column check
 *    - Word-level AND (tgrid_test) for final collision with skip
 *
 *  Pages grow dynamically (double smaller dimension).
 *  New pages only when max_size exhausted (ATLAS-18).
 * =================================================================== */

// #region Alpha trim — extract alpha plane, find tight bounding box
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
// #endregion

// #region Duplicate detection — identify identical sprites by hash
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
// #endregion

// #region Geometry — convex hull, simplification, polygon operations
/* --- Internal point type for geometry operations --- */

typedef struct {
    int32_t x, y;
} Point2D;
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
// #endregion

// #region Tile grid — 2D bitset occupancy grid for sprite placement

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
                    /* Advance past consecutive occupied atlas tiles using word-level scan */
                    uint32_t skip_to = hit_col + 1;
                    uint32_t sw2 = skip_to >> 6;
                    uint32_t sb2 = skip_to & 63;
                    while (sw2 < atlas->row_words) {
                        uint64_t inv = ~a_row[sw2];
                        if (sb2 > 0) {
                            inv &= UINT64_MAX << sb2;
                        }
                        if (inv) {
                            skip_to = (sw2 * 64) + tgrid_ctz64(inv);
                            break;
                        }
                        sw2++;
                        sb2 = 0;
                    }
                    if (sw2 >= atlas->row_words) {
                        skip_to = atlas->tw;
                    }
                    *out_skip = skip_to - hit_col;
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
/* --- x4 downsampled TileGrid (LOD) --- */

/* Build a x4 downsampled TileGrid from src: each dst bit = OR of 4×4 src tiles.
 * Used for cheap OR-mask computation (16× fewer rows×words) and fast collision pre-test. */
static TileGrid tgrid_downsample_x4(const TileGrid *src) {
    uint32_t dst_tw = (src->tw + 3) / 4;
    uint32_t dst_th = (src->th + 3) / 4;
    TileGrid dst = tgrid_create(dst_tw, dst_th, src->tile_size * 4);

    for (uint32_t dy = 0; dy < dst_th; dy++) {
        uint64_t *dst_row = dst.rows + ((size_t)dy * dst.row_words);
        uint32_t sy0 = dy * 4;
        uint32_t sy1 = sy0 + 4;
        if (sy1 > src->th) {
            sy1 = src->th;
        }
        for (uint32_t dx = 0; dx < dst_tw; dx++) {
            uint32_t sx0 = dx * 4;
            uint32_t sx1 = sx0 + 4;
            if (sx1 > src->tw) {
                sx1 = src->tw;
            }
            /* OR all bits in the 4×4 source block */
            bool any_set = false;
            for (uint32_t sy = sy0; sy < sy1 && !any_set; sy++) {
                const uint64_t *src_row = src->rows + ((size_t)sy * src->row_words);
                for (uint32_t sx = sx0; sx < sx1; sx++) {
                    if (src_row[sx >> 6] & (1ULL << (sx & 63))) {
                        any_set = true;
                        break;
                    }
                }
            }
            if (any_set) {
                dst_row[dx >> 6] |= (1ULL << (dx & 63));
            }
        }
    }
    return dst;
}

/* Update x4 grid after stamping at tile rect [tx0..tx0+stw) × [ty0..ty0+sth).
 * Only reclassifies affected x4 cells. */
static void tgrid_x4_update(TileGrid *x4, const TileGrid *src, int32_t tx0, int32_t ty0, uint32_t stw, uint32_t sth) {
    uint32_t dx0 = (tx0 >= 0) ? ((uint32_t)tx0 / 4) : 0;
    uint32_t dy0 = (ty0 >= 0) ? ((uint32_t)ty0 / 4) : 0;
    uint32_t dx1 = ((uint32_t)tx0 + stw + 3) / 4;
    uint32_t dy1 = ((uint32_t)ty0 + sth + 3) / 4;
    if (dx1 > x4->tw) {
        dx1 = x4->tw;
    }
    if (dy1 > x4->th) {
        dy1 = x4->th;
    }
    for (uint32_t dy = dy0; dy < dy1; dy++) {
        uint64_t *dst_row = x4->rows + ((size_t)dy * x4->row_words);
        uint32_t sy0 = dy * 4;
        uint32_t sy1 = sy0 + 4;
        if (sy1 > src->th) {
            sy1 = src->th;
        }
        for (uint32_t dx = dx0; dx < dx1; dx++) {
            uint32_t sx0 = dx * 4;
            uint32_t sx1 = sx0 + 4;
            if (sx1 > src->tw) {
                sx1 = src->tw;
            }
            bool any_set = false;
            for (uint32_t sy = sy0; sy < sy1 && !any_set; sy++) {
                const uint64_t *src_row = src->rows + ((size_t)sy * src->row_words);
                for (uint32_t sx = sx0; sx < sx1; sx++) {
                    if (src_row[sx >> 6] & (1ULL << (sx & 63))) {
                        any_set = true;
                        break;
                    }
                }
            }
            if (any_set) {
                dst_row[dx >> 6] |= (1ULL << (dx & 63));
            } else {
                dst_row[dx >> 6] &= ~(1ULL << (dx & 63));
            }
        }
    }
}
// #endregion

// #region Scan acceleration — coarse grid, sparse OR table, scan loop
/* --- Coarse block grid for scan acceleration --- */

/* Flat 2D grid overlaying a TileGrid at COARSE_SIZE × COARSE_SIZE tile blocks.
 * Each block is classified as EMPTY (all tiles free), FULL (all tiles occupied),
 * or MIXED. Per-row nonfull counts enable O(1) "is this row entirely full?" checks
 * for fast y-skip in scan_region. */

#define COARSE_SHIFT 3                   /* log2(COARSE_SIZE) */
#define COARSE_SIZE (1U << COARSE_SHIFT) /* 8 tiles per block */

#define BLOCK_EMPTY 0
#define BLOCK_MIXED 1
#define BLOCK_FULL 2

typedef struct {
    uint8_t *cells;        /* bw × bh array: BLOCK_EMPTY / BLOCK_MIXED / BLOCK_FULL */
    uint32_t bw, bh;       /* block grid dimensions */
    uint16_t *row_nonfull; /* per block-row: count of cells that are NOT FULL */
} CoarseGrid;

static CoarseGrid coarse_create(uint32_t tw, uint32_t th) {
    CoarseGrid cg;
    cg.bw = (tw + COARSE_SIZE - 1) >> COARSE_SHIFT;
    cg.bh = (th + COARSE_SIZE - 1) >> COARSE_SHIFT;
    cg.cells = (uint8_t *)calloc((size_t)cg.bw * cg.bh, 1); /* all EMPTY */
    cg.row_nonfull = (uint16_t *)malloc(cg.bh * sizeof(uint16_t));
    NT_BUILD_ASSERT(cg.cells && cg.row_nonfull && "coarse_create: alloc failed");
    for (uint32_t r = 0; r < cg.bh; r++) {
        cg.row_nonfull[r] = (uint16_t)cg.bw; /* all blocks are EMPTY → all are nonfull */
    }
    return cg;
}

static void coarse_free(CoarseGrid *cg) {
    free(cg->cells);
    free(cg->row_nonfull);
    cg->cells = NULL;
    cg->row_nonfull = NULL;
}

/* Classify a single block from the tile grid bitset.
 * Block (bx, by) covers tiles [bx*8 .. bx*8+7] × [by*8 .. by*8+7]. */
static uint8_t coarse_classify_block(const TileGrid *g, uint32_t bx, uint32_t by) {
    uint32_t tx0 = bx << COARSE_SHIFT;
    uint32_t ty0 = by << COARSE_SHIFT;
    if (tx0 >= g->tw || ty0 >= g->th) {
        return BLOCK_EMPTY;
    }
    uint32_t tx1 = tx0 + COARSE_SIZE;
    uint32_t ty1 = ty0 + COARSE_SIZE;
    if (tx1 > g->tw) {
        tx1 = g->tw;
    }
    if (ty1 > g->th) {
        ty1 = g->th;
    }
    bool has_set = false;
    bool has_clear = false;
    for (uint32_t ty = ty0; ty < ty1; ty++) {
        const uint64_t *row = g->rows + ((size_t)ty * g->row_words);
        for (uint32_t tx = tx0; tx < tx1; tx++) {
            if (row[tx >> 6] & (1ULL << (tx & 63))) {
                has_set = true;
            } else {
                has_clear = true;
            }
            if (has_set && has_clear) {
                return BLOCK_MIXED;
            }
        }
    }
    return has_set ? BLOCK_FULL : BLOCK_EMPTY;
}

/* Rebuild entire coarse grid from tile grid (after growth). */
static void coarse_rebuild(CoarseGrid *cg, const TileGrid *g) {
    for (uint32_t by = 0; by < cg->bh; by++) {
        uint16_t nonfull = 0;
        for (uint32_t bx = 0; bx < cg->bw; bx++) {
            uint8_t state = coarse_classify_block(g, bx, by);
            cg->cells[by * cg->bw + bx] = state;
            if (state != BLOCK_FULL) {
                nonfull++;
            }
        }
        cg->row_nonfull[by] = nonfull;
    }
}

/* Update coarse grid after stamping a sprite at tile rect [tx0..tx0+stw) × [ty0..ty0+sth). */
static void coarse_update(CoarseGrid *cg, const TileGrid *g, int32_t tx0, int32_t ty0, uint32_t stw, uint32_t sth) {
    uint32_t bx0 = (tx0 >= 0) ? ((uint32_t)tx0 >> COARSE_SHIFT) : 0;
    uint32_t by0 = (ty0 >= 0) ? ((uint32_t)ty0 >> COARSE_SHIFT) : 0;
    uint32_t bx1 = ((uint32_t)tx0 + stw + COARSE_SIZE - 1) >> COARSE_SHIFT;
    uint32_t by1 = ((uint32_t)ty0 + sth + COARSE_SIZE - 1) >> COARSE_SHIFT;
    if (bx1 > cg->bw) {
        bx1 = cg->bw;
    }
    if (by1 > cg->bh) {
        by1 = cg->bh;
    }
    for (uint32_t by = by0; by < by1; by++) {
        for (uint32_t bx = bx0; bx < bx1; bx++) {
            uint32_t idx = by * cg->bw + bx;
            uint8_t old = cg->cells[idx];
            uint8_t cur = coarse_classify_block(g, bx, by);
            cg->cells[idx] = cur;
            if (old != BLOCK_FULL && cur == BLOCK_FULL) {
                cg->row_nonfull[by]--;
            } else if (old == BLOCK_FULL && cur != BLOCK_FULL) {
                cg->row_nonfull[by]++;
            }
        }
    }
}

/* Grow coarse grid to match a grown tile grid. Rebuilds from scratch. */
static void coarse_grow(CoarseGrid *cg, const TileGrid *g) {
    coarse_free(cg);
    *cg = coarse_create(g->tw, g->th);
    coarse_rebuild(cg, g);
}
/* --- Sparse table for O(1) OR range queries on tile grid rows ---
 * Precompute sparse[k][y] = OR of rows [y, y+2^k) for each level k.
 * Query OR([y0, y0+h)): k = floor(log2(h)), result = sparse[k][y0] | sparse[k][y0+h-2^k].
 * Since OR is idempotent, overlapping ranges give exact results. */

#define OR_SPARSE_MAX_K 12 /* log2(4096) = 12, supports sprites up to 4096 tiles tall */

typedef struct {
    uint64_t *levels[OR_SPARSE_MAX_K]; /* levels[k] = row_words * th entries */
    uint32_t row_words;
    uint32_t th;
    uint32_t max_k; /* highest level built */
} OrSparseTable;

static OrSparseTable or_sparse_build(const TileGrid *g, uint32_t max_h) {
    OrSparseTable st;
    st.row_words = g->row_words;
    st.th = g->th;
    st.max_k = 0;
    memset(st.levels, 0, sizeof(st.levels));

    /* Level 0: each entry is a single row */
    size_t level_bytes = (size_t)g->row_words * g->th * sizeof(uint64_t);
    st.levels[0] = (uint64_t *)malloc(level_bytes);
    NT_BUILD_ASSERT(st.levels[0] && "or_sparse_build: alloc failed");
    memcpy(st.levels[0], g->rows, level_bytes);

    /* Build higher levels until 2^k covers max_h */
    for (uint32_t k = 1; (1U << k) <= max_h && k < OR_SPARSE_MAX_K; k++) {
        uint32_t half = 1U << (k - 1);
        st.levels[k] = (uint64_t *)malloc(level_bytes);
        NT_BUILD_ASSERT(st.levels[k] && "or_sparse_build: alloc failed");
        uint32_t valid_rows = (g->th >= half) ? g->th - half : 0;
        for (uint32_t y = 0; y < g->th; y++) {
            uint64_t *dst = st.levels[k] + (size_t)y * g->row_words;
            const uint64_t *a = st.levels[k - 1] + (size_t)y * g->row_words;
            if (y + half < g->th) {
                const uint64_t *b = st.levels[k - 1] + (size_t)(y + half) * g->row_words;
                for (uint32_t w = 0; w < g->row_words; w++) {
                    dst[w] = a[w] | b[w];
                }
            } else {
                memcpy(dst, a, (size_t)g->row_words * sizeof(uint64_t));
            }
        }
        st.max_k = k;
    }
    return st;
}

static void or_sparse_free(OrSparseTable *st) {
    for (uint32_t k = 0; k <= st->max_k; k++) {
        free(st->levels[k]);
    }
    memset(st, 0, sizeof(*st));
}

/* Incrementally update sparse table after stamping rows [y0, y0+h).
 * Only recomputes entries whose range overlaps the stamped region. */
static void or_sparse_update(OrSparseTable *st, const TileGrid *g, uint32_t stamp_y0, uint32_t stamp_h) {
    uint32_t stamp_y1 = stamp_y0 + stamp_h;
    if (stamp_y1 > g->th) {
        stamp_y1 = g->th;
    }
    uint32_t rw = st->row_words;

    /* Level 0: copy affected rows from tile grid */
    for (uint32_t y = stamp_y0; y < stamp_y1; y++) {
        memcpy(st->levels[0] + (size_t)y * rw, g->rows + (size_t)y * rw, (size_t)rw * sizeof(uint64_t));
    }

    /* Higher levels: recompute entries whose range [y, y+2^k) overlaps [stamp_y0, stamp_y1) */
    for (uint32_t k = 1; k <= st->max_k; k++) {
        uint32_t half = 1U << (k - 1);
        /* Entry y is affected if [y, y+2^k) overlaps [stamp_y0, stamp_y1).
         * That means: y < stamp_y1 AND y + 2^k > stamp_y0.
         * So: y >= max(0, stamp_y0 - 2^k + 1) AND y < stamp_y1. */
        uint32_t upd_y0 = (stamp_y0 >= (1U << k)) ? stamp_y0 - (1U << k) + 1 : 0;
        uint32_t upd_y1 = stamp_y1;
        if (upd_y1 > g->th) {
            upd_y1 = g->th;
        }
        for (uint32_t y = upd_y0; y < upd_y1; y++) {
            uint64_t *dst = st->levels[k] + (size_t)y * rw;
            const uint64_t *a = st->levels[k - 1] + (size_t)y * rw;
            if (y + half < g->th) {
                const uint64_t *b = st->levels[k - 1] + (size_t)(y + half) * rw;
                for (uint32_t w = 0; w < rw; w++) {
                    dst[w] = a[w] | b[w];
                }
            } else {
                memcpy(dst, a, (size_t)rw * sizeof(uint64_t));
            }
        }
    }
}

/* Query OR of rows [y0, y0+h). Result written to out (row_words uint64_t). */
static void or_sparse_query(const OrSparseTable *st, uint32_t y0, uint32_t h, uint64_t *out) {
    if (h == 0 || y0 >= st->th) {
        memset(out, 0, (size_t)st->row_words * sizeof(uint64_t));
        return;
    }
    uint32_t y1 = y0 + h;
    if (y1 > st->th) {
        y1 = st->th;
        h = y1 - y0;
    }
    if (h == 0) {
        memset(out, 0, (size_t)st->row_words * sizeof(uint64_t));
        return;
    }
    /* Find k such that 2^k <= h */
    uint32_t k = 0;
    while ((1U << (k + 1)) <= h && k + 1 <= st->max_k) {
        k++;
    }
    const uint64_t *a = st->levels[k] + (size_t)y0 * st->row_words;
    const uint64_t *b = st->levels[k] + (size_t)(y1 - (1U << k)) * st->row_words;
    for (uint32_t w = 0; w < st->row_words; w++) {
        out[w] = a[w] | b[w];
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
/* Find first SET bit in OR-mask within [from, from+width). Returns from+width if all free.
 * Word-level scan: O(width/64 + 2). Used to reject positions where the sprite's
 * full tile width doesn't fit in the OR-mask gap. */
static uint32_t tgrid_or_first_set_in_range(const uint64_t *mask, uint32_t mask_words, uint32_t from, uint32_t width) {
    uint32_t end = from + width;
    uint32_t w = from >> 6;
    uint32_t b = from & 63;

    /* First partial word */
    if (b > 0 && w < mask_words) {
        uint64_t bits = mask[w] >> b;
        uint32_t avail = 64 - b;
        if (avail > width) {
            bits &= (1ULL << width) - 1;
        }
        if (bits) {
            return from + tgrid_ctz64(bits);
        }
        w++;
        from = w * 64;
    }

    /* Full words */
    while (w < mask_words && (w * 64 + 64) <= end) {
        if (mask[w]) {
            return (w * 64) + tgrid_ctz64(mask[w]);
        }
        w++;
    }

    /* Last partial word */
    if (w < mask_words && (w * 64) < end) {
        uint32_t tail = end - (w * 64);
        uint64_t bits = mask[w] & ((1ULL << tail) - 1);
        if (bits) {
            return (w * 64) + tgrid_ctz64(bits);
        }
    }

    return end; /* all free */
}

/* --- Scan a rectangular region of the atlas grid for a valid placement --- */

/* Per-call packing statistics (thread-safe: no static globals) */
typedef struct {
    bool enabled;
    double slow_scan_sec;
    double progress_sec;
} AtlasTraceConfig;

typedef struct {
    uint64_t or_count;
    uint64_t test_count;
    uint64_t yskip_count;
    AtlasTraceConfig trace;
} PackStats;

typedef struct {
    const AtlasTraceConfig *cfg;
    const char *stage;
    uint32_t sprite_order;
    uint32_t sprite_count;
    uint32_t sprite_index;
    uint32_t page_index;
    uint32_t tx_min;
    uint32_t ty_min;
    uint32_t tx_max;
    uint32_t ty_max;
    uint32_t max_stw;
    uint32_t max_sth;
    uint32_t page_used_tw;
    uint32_t page_used_th;
} ScanTraceParams;

typedef struct {
    uint64_t rotation_passes;
    uint64_t ty_rows;
    uint64_t tx_steps;
    uint64_t x4_full_rows;
    uint64_t run_skip_count;
    uint64_t run_skip_tiles;
    uint64_t or_width_skip_count;
    uint64_t or_width_skip_tiles;
    uint64_t tests;
    uint64_t collision_hits;
    uint64_t collision_skip_one;
    uint64_t collision_skip_many;
    uint64_t collision_skip_tiles;
    uint64_t fits;
    uint32_t last_rot;
    uint32_t last_ty;
    uint32_t last_tx;
} ScanTraceStats;

static bool atlas_trace_str_ieq(const char *a, const char *b) {
    while (*a != '\0' && *b != '\0') {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) {
            return false;
        }
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

static bool atlas_trace_env_truthy(const char *value) {
    if (!value || value[0] == '\0') {
        return false;
    }
    if ((value[0] == '0' && value[1] == '\0') || atlas_trace_str_ieq(value, "false") || atlas_trace_str_ieq(value, "off") || atlas_trace_str_ieq(value, "no")) {
        return false;
    }
    return true;
}

static double atlas_trace_env_seconds(const char *name, double fallback) {
    const char *value = getenv(name);
    if (!value || value[0] == '\0') {
        return fallback;
    }
    char *end = NULL;
    double parsed = strtod(value, &end);
    if (end == value || parsed < 0.0) {
        return fallback;
    }
    return parsed;
}

static AtlasTraceConfig atlas_trace_config_get(void) {
    static bool initialized = false;
    static AtlasTraceConfig cfg;
    if (!initialized) {
        cfg.enabled = atlas_trace_env_truthy(getenv("NT_ATLAS_TRACE"));
        cfg.slow_scan_sec = atlas_trace_env_seconds("NT_ATLAS_TRACE_SLOW_SEC", 1.0);
        cfg.progress_sec = atlas_trace_env_seconds("NT_ATLAS_TRACE_PROGRESS_SEC", 2.0);
        initialized = true;
    }
    return cfg;
}

static void coarse_count_states(const CoarseGrid *cg, uint32_t *out_empty, uint32_t *out_mixed, uint32_t *out_full) {
    uint32_t empty = 0;
    uint32_t mixed = 0;
    uint32_t full = 0;
    if (cg) {
        uint32_t count = cg->bw * cg->bh;
        for (uint32_t i = 0; i < count; i++) {
            uint8_t cell = cg->cells[i];
            if (cell == BLOCK_EMPTY) {
                empty++;
            } else if (cell == BLOCK_FULL) {
                full++;
            } else {
                mixed++;
            }
        }
    }
    if (out_empty) {
        *out_empty = empty;
    }
    if (out_mixed) {
        *out_mixed = mixed;
    }
    if (out_full) {
        *out_full = full;
    }
}

static void scan_trace_format_best(char *dst, size_t dst_size, bool found, uint32_t best_tx, uint32_t best_ty, uint8_t best_rot) {
    if (found) {
        (void)snprintf(dst, dst_size, "%u,%u r%u", best_tx, best_ty, (unsigned)best_rot);
    } else {
        (void)snprintf(dst, dst_size, "none");
    }
}

static void scan_trace_log_progress(const ScanTraceParams *trace, const ScanTraceStats *diag, double elapsed, bool found, uint32_t best_tx, uint32_t best_ty, uint8_t best_rot, uint64_t yskip_rows) {
    char best_buf[32];
    scan_trace_format_best(best_buf, sizeof(best_buf), found, best_tx, best_ty, best_rot);
    NT_LOG_INFO("  TRACE progress sprite %u/%u idx=%u page=%u stage=%s elapsed=%.1fs cursor=r%u ty=%u tx=%u range=%ux%u used=%ux%u best=%s ty_rows=%llu tx_steps=%llu yskip=%llu x4=%llu "
                "run=%llu(+%llu) or=%llu(+%llu) tests=%llu hits=%llu skip1=%llu skipN=%llu(+%llu)",
                trace->sprite_order, trace->sprite_count, trace->sprite_index, trace->page_index, trace->stage ? trace->stage : "scan", elapsed, diag->last_rot, diag->last_ty, diag->last_tx,
                trace->tx_max - trace->tx_min, trace->ty_max - trace->ty_min, trace->page_used_tw, trace->page_used_th, best_buf, (unsigned long long)diag->ty_rows, (unsigned long long)diag->tx_steps,
                (unsigned long long)yskip_rows, (unsigned long long)diag->x4_full_rows, (unsigned long long)diag->run_skip_count, (unsigned long long)diag->run_skip_tiles,
                (unsigned long long)diag->or_width_skip_count, (unsigned long long)diag->or_width_skip_tiles, (unsigned long long)diag->tests, (unsigned long long)diag->collision_hits,
                (unsigned long long)diag->collision_skip_one, (unsigned long long)diag->collision_skip_many, (unsigned long long)diag->collision_skip_tiles);
}

static void scan_trace_log_summary(const ScanTraceParams *trace, const ScanTraceStats *diag, const TileGrid *atlas, const CoarseGrid *cg, double elapsed, bool found, uint32_t best_tx,
                                   uint32_t best_ty, uint8_t best_rot, uint64_t yskip_rows) {
    uint32_t empty_blocks = 0;
    uint32_t mixed_blocks = 0;
    uint32_t full_blocks = 0;
    char best_buf[32];
    scan_trace_format_best(best_buf, sizeof(best_buf), found, best_tx, best_ty, best_rot);
    coarse_count_states(cg, &empty_blocks, &mixed_blocks, &full_blocks);
    NT_LOG_INFO("  TRACE summary sprite %u/%u idx=%u page=%u stage=%s result=%s elapsed=%.1fs atlas=%ux%u range=%ux%u used=%ux%u sprite=%ux%u best=%s ty_rows=%llu tx_steps=%llu yskip=%llu x4=%llu "
                "run=%llu(+%llu) or=%llu(+%llu) tests=%llu hits=%llu skip1=%llu skipN=%llu(+%llu) coarse[e=%u m=%u f=%u]",
                trace->sprite_order, trace->sprite_count, trace->sprite_index, trace->page_index, trace->stage ? trace->stage : "scan", found ? "placed" : "miss", elapsed, atlas->tw, atlas->th,
                trace->tx_max - trace->tx_min, trace->ty_max - trace->ty_min, trace->page_used_tw, trace->page_used_th, trace->max_stw, trace->max_sth, best_buf, (unsigned long long)diag->ty_rows,
                (unsigned long long)diag->tx_steps, (unsigned long long)yskip_rows, (unsigned long long)diag->x4_full_rows, (unsigned long long)diag->run_skip_count,
                (unsigned long long)diag->run_skip_tiles, (unsigned long long)diag->or_width_skip_count, (unsigned long long)diag->or_width_skip_tiles, (unsigned long long)diag->tests,
                (unsigned long long)diag->collision_hits, (unsigned long long)diag->collision_skip_one, (unsigned long long)diag->collision_skip_many, (unsigned long long)diag->collision_skip_tiles,
                empty_blocks, mixed_blocks, full_blocks);
}

/* Try to place a sprite (with given rotation variants) within [ty_min..ty_max) x [tx_min..tx_max).
 * Returns true if placed, with best position in out_tx/out_ty/out_rot.
 * Uses x4 LOD for cheap OR-mask, coarse grid for y-skip/x-skip, sparse table OR-mask + tgrid_test as fallback. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static bool scan_region(const TileGrid *atlas, const CoarseGrid *cg, const TileGrid *atlas_x4, const OrSparseTable *or_st, const TileGrid *rot_sg, const TileGrid *rot_sg_x4, const int32_t *rot_ox,
                        const int32_t *rot_oy, uint32_t rot_count, uint32_t tx_min, uint32_t ty_min, uint32_t tx_max, uint32_t ty_max, uint32_t *out_tx, uint32_t *out_ty, uint8_t *out_rot,
                        const ScanTraceParams *trace, PackStats *stats) {
    uint32_t best_tx = UINT32_MAX;
    uint32_t best_ty = UINT32_MAX;
    bool found = false;
    bool trace_enabled = trace && trace->cfg && trace->cfg->enabled;
    ScanTraceStats trace_stats;
    memset(&trace_stats, 0, sizeof(trace_stats));
    double trace_start = 0.0;
    double next_progress_time = 0.0;
    uint64_t yskip_before = stats->yskip_count;
    if (trace_enabled) {
        trace_start = nt_time_now();
        if (trace->cfg->progress_sec > 0.0) {
            next_progress_time = trace_start + trace->cfg->progress_sec;
        }
    }

    uint64_t *or_mask = (uint64_t *)malloc((size_t)atlas->row_words * sizeof(uint64_t));
    NT_BUILD_ASSERT(or_mask && "scan_region: alloc failed");

    /* x4 OR-mask: computed on the downsampled grid (16× cheaper per ty) */
    uint64_t *or_mask_x4 = NULL;
    if (atlas_x4) {
        or_mask_x4 = (uint64_t *)malloc((size_t)atlas_x4->row_words * sizeof(uint64_t));
        NT_BUILD_ASSERT(or_mask_x4 && "scan_region: alloc x4 or_mask");
    }

    uint32_t max_bw = cg ? cg->bw : 0;
    uint8_t *col_free = NULL;
    uint32_t *run_from = NULL;
    if (max_bw > 0) {
        col_free = (uint8_t *)malloc(max_bw);
        run_from = (uint32_t *)malloc(max_bw * sizeof(uint32_t));
        NT_BUILD_ASSERT(col_free && run_from && "scan_region: alloc");
    }

    uint8_t *by0_has_room = NULL;
    if (cg && cg->bh > 0) {
        by0_has_room = (uint8_t *)malloc(cg->bh);
        NT_BUILD_ASSERT(by0_has_room && "scan_region: alloc by0_has_room failed");
    }

    for (uint32_t r = 0; r < rot_count; r++) {
        if (trace_enabled) {
            trace_stats.rotation_passes++;
            trace_stats.last_rot = r;
            trace_stats.last_ty = ty_min;
            trace_stats.last_tx = tx_min;
        }
        uint32_t eff_ty_max = found ? (best_ty + 1) : ty_max;
        if (eff_ty_max > ty_max) {
            eff_ty_max = ty_max;
        }

        uint32_t s_tw = rot_sg[r].tw;
        uint32_t s_th = rot_sg[r].th;
        uint32_t sprite_bw = (s_tw + COARSE_SIZE - 1) >> COARSE_SHIFT;

        // #region Precompute by0_has_room: which block-row bands can fit the sprite?
        /* For each possible starting block-row by0, check whether any contiguous run
         * of non-FULL block columns across the band [by0..by0+band_bh) is wide enough
         * for the sprite. This eliminates ~80-90% of ty values at high fill levels. */
        uint32_t band_bh = (s_th + COARSE_SIZE - 1) >> COARSE_SHIFT;
        if (by0_has_room) {
            for (uint32_t by0 = 0; by0 < cg->bh; by0++) {
                uint32_t by1 = by0 + band_bh;
                if (by1 > cg->bh) {
                    by1 = cg->bh;
                }
                uint32_t max_run = 0;
                uint32_t cur_run = 0;
                for (uint32_t bx = 0; bx < cg->bw; bx++) {
                    bool col_has_space = false;
                    for (uint32_t by = by0; by < by1; by++) {
                        if (cg->cells[by * cg->bw + bx] != BLOCK_FULL) {
                            col_has_space = true;
                            break;
                        }
                    }
                    if (col_has_space) {
                        cur_run++;
                        if (cur_run > max_run) {
                            max_run = cur_run;
                        }
                    } else {
                        cur_run = 0;
                    }
                }
                by0_has_room[by0] = (max_run >= sprite_bw) ? 1 : 0;
            }
        }
        // #endregion

        uint32_t cached_by0 = UINT32_MAX;
        uint32_t cached_by1 = UINT32_MAX;

        /* OR-mask is now precomputed via sparse table — O(row_words) query per ty */

        for (uint32_t ty = ty_min; ty < eff_ty_max; ty++) {
            if (trace_enabled) {
                trace_stats.ty_rows++;
                trace_stats.last_ty = ty;
                trace_stats.last_tx = tx_min;
                if (next_progress_time > 0.0 && (trace_stats.ty_rows & 255ULL) == 0) {
                    double now = nt_time_now();
                    if (now >= next_progress_time) {
                        scan_trace_log_progress(trace, &trace_stats, now - trace_start, found, best_tx, best_ty, *out_rot, stats->yskip_count - yskip_before);
                        next_progress_time = now + trace->cfg->progress_sec;
                    }
                }
            }
            int32_t ay0_s = (int32_t)ty + rot_oy[r];
            int32_t ay1_s = ay0_s + (int32_t)s_th;
            uint32_t ay0 = (ay0_s >= 0) ? (uint32_t)ay0_s : 0;
            uint32_t ay1 = ((uint32_t)ay1_s > atlas->th) ? atlas->th : (uint32_t)ay1_s;
            uint32_t by0 = ay0 >> COARSE_SHIFT;
            uint32_t by1 = (ay1 + COARSE_SIZE - 1) >> COARSE_SHIFT;
            if (by1 > cg->bh) {
                by1 = cg->bh;
            }

            // #region Y-skip: precomputed band check
            if (by0_has_room && by0 < cg->bh && !by0_has_room[by0]) {
                stats->yskip_count++;
                uint32_t next_tile_y = ((by0 + 1) << COARSE_SHIFT);
                int32_t next_ty = (int32_t)next_tile_y - rot_oy[r];
                if (next_ty > (int32_t)ty) {
                    ty = (uint32_t)next_ty - 1;
                }
                continue;
            }
            // #endregion

            // #region Recompute col_free + run_from when band shifts
            if (cg && (by0 != cached_by0 || by1 != cached_by1)) {
                cached_by0 = by0;
                cached_by1 = by1;
                for (uint32_t bx = 0; bx < cg->bw; bx++) {
                    col_free[bx] = 0;
                    for (uint32_t by = by0; by < by1; by++) {
                        if (cg->cells[by * cg->bw + bx] != BLOCK_FULL) {
                            col_free[bx] = 1;
                            break;
                        }
                    }
                }
                for (int32_t bx = (int32_t)cg->bw - 1; bx >= 0; bx--) {
                    if (col_free[bx]) {
                        run_from[bx] = ((uint32_t)bx + 1 < cg->bw) ? run_from[bx + 1] + 1 : 1;
                    } else {
                        run_from[bx] = 0;
                    }
                }
            }
            // #endregion

            // #region x4 OR-mask pre-check: skip ty if x4 OR-mask is all occupied
            if (or_mask_x4 && atlas_x4) {
                uint32_t x4_y0 = ay0 / 4;
                uint32_t x4_h = (ay1 - ay0 + 3) / 4;
                stats->or_count++;
                tgrid_row_or(atlas_x4, x4_y0, x4_h, or_mask_x4, atlas_x4->row_words);

                /* Check if x4 OR-mask is completely full across scan range → skip ty */
                bool x4_all_full = true;
                uint32_t x4_tx_min = tx_min / 4;
                uint32_t x4_tx_max = (tx_max + 3) / 4;
                if (x4_tx_max > atlas_x4->tw) {
                    x4_tx_max = atlas_x4->tw;
                }
                for (uint32_t c = x4_tx_min; c < x4_tx_max; c++) {
                    if (!(or_mask_x4[c >> 6] & (1ULL << (c & 63)))) {
                        x4_all_full = false;
                        break;
                    }
                }
                if (x4_all_full) {
                    if (trace_enabled) {
                        trace_stats.x4_full_rows++;
                    }
                    continue;
                }
            }
            // #endregion

            // #region Sparse table OR-mask query: O(row_words) exact lookup
            stats->or_count++;
            or_sparse_query(or_st, ay0, ay1 - ay0, or_mask);
            // #endregion

            for (uint32_t tx = tx_min; tx < tx_max;) {
                if (trace_enabled) {
                    trace_stats.tx_steps++;
                    trace_stats.last_tx = tx;
                    if (next_progress_time > 0.0 && (trace_stats.tx_steps & 4095ULL) == 0) {
                        double now = nt_time_now();
                        if (now >= next_progress_time) {
                            scan_trace_log_progress(trace, &trace_stats, now - trace_start, found, best_tx, best_ty, *out_rot, stats->yskip_count - yskip_before);
                            next_progress_time = now + trace->cfg->progress_sec;
                        }
                    }
                }
                // #region X-skip: jump past block columns without a wide enough free-run
                if (cg) {
                    int32_t ax = (int32_t)tx + rot_ox[r];
                    if (ax >= 0) {
                        uint32_t bx = (uint32_t)ax >> COARSE_SHIFT;
                        if (bx < cg->bw && run_from[bx] < sprite_bw) {
                            uint32_t nbx = bx + 1;
                            while (nbx < cg->bw && run_from[nbx] < sprite_bw) {
                                nbx++;
                            }
                            int32_t next_tx = (int32_t)(nbx << COARSE_SHIFT) - rot_ox[r];
                            if (next_tx > (int32_t)tx) {
                                if (trace_enabled) {
                                    trace_stats.run_skip_count++;
                                    trace_stats.run_skip_tiles += (uint64_t)(next_tx - (int32_t)tx);
                                }
                                tx = (uint32_t)next_tx;
                                continue;
                            }
                        }
                    }
                }
                // #endregion

                /* Quick rejection via x4 OR-mask: check sprite's first x4-column */
                if (or_mask_x4) {
                    int32_t col0_x4 = ((int32_t)tx + rot_ox[r]) / 4;
                    if (col0_x4 >= 0 && (uint32_t)col0_x4 < atlas_x4->tw) {
                        if (or_mask_x4[(uint32_t)col0_x4 >> 6] & (1ULL << ((uint32_t)col0_x4 & 63))) {
                            /* x4 column occupied — but might have free tiles within.
                             * Fall through to x1 OR-mask check. */
                        } else {
                            /* x4 column FREE → 4 tile columns are all free → promising! */
                            goto x1_check;
                        }
                    }
                }

                /* Quick rejection: x1 OR-mask width check.
                 * Check that the sprite's full tile width has consecutive free columns
                 * in the OR-mask. Skips positions where a gap is too narrow — avoids
                 * expensive tgrid_test calls that would inevitably collide. */
                {
                    int32_t col0 = (int32_t)tx + rot_ox[r];
                    if (col0 >= 0 && (uint32_t)col0 < atlas->tw) {
                        uint32_t check_w = s_tw;
                        if ((uint32_t)col0 + check_w > atlas->tw) {
                            check_w = atlas->tw - (uint32_t)col0;
                        }
                        uint32_t first_set = tgrid_or_first_set_in_range(or_mask, atlas->row_words, (uint32_t)col0, check_w);
                        if (first_set < (uint32_t)col0 + check_w) {
                            /* Gap too narrow — skip past the obstacle */
                            uint32_t next_free = tgrid_or_next_free(or_mask, atlas->row_words, atlas->tw, first_set + 1);
                            int32_t next_tx = (int32_t)next_free - rot_ox[r];
                            if (next_tx <= (int32_t)tx) {
                                next_tx = (int32_t)tx + 1;
                            }
                            if (trace_enabled) {
                                trace_stats.or_width_skip_count++;
                                trace_stats.or_width_skip_tiles += (uint64_t)(next_tx - (int32_t)tx);
                            }
                            tx = (uint32_t)next_tx;
                            continue;
                        }
                    }
                }

            x1_check:;
                uint32_t skip = 0;
                stats->test_count++;
                if (trace_enabled) {
                    trace_stats.tests++;
                }
                if (!tgrid_test(atlas, &rot_sg[r], (int32_t)tx, (int32_t)ty, rot_ox[r], rot_oy[r], &skip)) {
                    int32_t abs_min_tx = (int32_t)tx + rot_ox[r];
                    int32_t abs_min_ty = (int32_t)ty + rot_oy[r];
                    int32_t abs_max_tx = abs_min_tx + (int32_t)s_tw;
                    int32_t abs_max_ty = abs_min_ty + (int32_t)s_th;
                    if (abs_min_tx >= 0 && abs_min_ty >= 0 && (uint32_t)abs_max_tx <= atlas->tw && (uint32_t)abs_max_ty <= atlas->th) {
                        if (ty < best_ty || (ty == best_ty && tx < best_tx)) {
                            best_tx = tx;
                            best_ty = ty;
                            *out_rot = (uint8_t)r;
                            found = true;
                            if (trace_enabled) {
                                trace_stats.fits++;
                            }
                        }
                        break;
                    }
                    if ((uint32_t)abs_max_tx > atlas->tw || (uint32_t)abs_max_ty > atlas->th) {
                        break;
                    }
                    tx++;
                    continue;
                }
                if (trace_enabled) {
                    trace_stats.collision_hits++;
                    if (skip > 1) {
                        trace_stats.collision_skip_many++;
                        trace_stats.collision_skip_tiles += skip;
                    } else {
                        trace_stats.collision_skip_one++;
                    }
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

    if (by0_has_room)
        free(by0_has_room);
    free(run_from);
    free(col_free);
    free(or_mask_x4);
    free(or_mask);

    if (trace_enabled) {
        double elapsed = nt_time_now() - trace_start;
        if (elapsed >= trace->cfg->slow_scan_sec) {
            scan_trace_log_summary(trace, &trace_stats, atlas, cg, elapsed, found, best_tx, best_ty, *out_rot, stats->yskip_count - yskip_before);
        }
    }

    if (found) {
        *out_tx = best_tx;
        *out_ty = best_ty;
    }
    return found;
}
// #endregion

// #region Tile packing — sort sprites by area, place on atlas pages

/* Sort entry for area-descending sort (ATLAS-03) */
typedef struct {
    uint32_t index; /* index into sprites[] */
    uint32_t area;  /* trimmed_w * trimmed_h */
} AreaSortEntry;
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

#include "nt_builder_vector.inl"

/* --- Tile packer with tile-grid collision (ATLAS-02, ATLAS-03, ATLAS-04, ATLAS-18) --- */

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static uint32_t tile_pack(const uint32_t *trim_w, const uint32_t *trim_h, Point2D **hull_verts, const uint32_t *hull_counts, uint32_t sprite_count, const nt_atlas_opts_t *opts,
                          AtlasPlacement *out_placements, uint32_t *out_page_count, uint32_t *out_page_w, uint32_t *out_page_h, PackStats *stats) {
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
    uint32_t global_max_sth = 0; /* max sprite tile height (any rotation) for sparse table */
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
        if (sprite_grids[i].th > global_max_sth) {
            global_max_sth = sprite_grids[i].th;
        }
        if (sprite_grids[i].tw > global_max_sth) {
            global_max_sth = sprite_grids[i].tw; /* rotation can swap w/h */
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
    TileGrid page_x4[ATLAS_MAX_PAGES]; /* x4 downsampled LOD for each page */
    CoarseGrid page_coarse[ATLAS_MAX_PAGES];
    uint32_t page_count = 1;
    uint32_t page_w[ATLAS_MAX_PAGES];
    uint32_t page_h[ATLAS_MAX_PAGES];
    uint32_t page_max_x[ATLAS_MAX_PAGES];
    uint32_t page_max_y[ATLAS_MAX_PAGES];
    uint32_t page_used_tw[ATLAS_MAX_PAGES];
    uint32_t page_used_th[ATLAS_MAX_PAGES];
    memset(page_max_x, 0, sizeof(page_max_x));
    memset(page_max_y, 0, sizeof(page_max_y));
    memset(page_used_tw, 0, sizeof(page_used_tw));
    memset(page_used_th, 0, sizeof(page_used_th));
    memset(page_coarse, 0, sizeof(page_coarse));
    memset(page_x4, 0, sizeof(page_x4));

    for (uint32_t p = 0; p < ATLAS_MAX_PAGES; p++) {
        page_w[p] = init_dim;
        page_h[p] = init_dim;
    }
    page_grids[0] = tgrid_create(atlas_tw, atlas_th, ts);
    page_coarse[0] = coarse_create(atlas_tw, atlas_th);
    page_x4[0] = tgrid_downsample_x4(&page_grids[0]);
    OrSparseTable page_or_st[ATLAS_MAX_PAGES];
    memset(page_or_st, 0, sizeof(page_or_st));
    page_or_st[0] = or_sparse_build(&page_grids[0], global_max_sth);

    uint32_t margin_tiles = (margin + ts - 1) / ts;
    uint32_t placement_count = 0;
    NT_LOG_INFO("  tile_pack: %u sprites, grid %ux%u tiles (ts=%u), init=%upx", sprite_count, atlas_tw, atlas_th, ts, init_dim);
    double pack_start_time = nt_time_now();
    stats->or_count = 0;
    stats->test_count = 0;
    stats->yskip_count = 0;
    stats->trace = atlas_trace_config_get();
    if (stats->trace.enabled) {
        NT_LOG_INFO("  TRACE atlas scan enabled (slow=%.1fs progress=%.1fs)", stats->trace.slow_scan_sec, stats->trace.progress_sec);
    }

    for (uint32_t si = 0; si < sprite_count; si++) {
        /* Progress log every 10 seconds */
        if (si % 50 == 0 || si == sprite_count - 1) {
            NT_LOG_INFO("  placing sprite %u/%u (idx=%u, stw=%u sth=%u, grid=%ux%u, used=%ux%u)", si, sprite_count, sorted[si].index, sprite_grids[sorted[si].index].tw,
                        sprite_grids[sorted[si].index].th, page_grids[0].tw, page_grids[0].th, page_used_tw[0], page_used_th[0]);
        }
        double sprite_start = nt_time_now();
        uint64_t or_before = stats->or_count;
        uint64_t test_before = stats->test_count;
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

        /* Build x4 downsampled sprite grids for LOD */
        TileGrid rot_sg_x4[4];
        for (uint32_t r = 0; r < rot_count; r++) {
            rot_sg_x4[r] = tgrid_downsample_x4(&rot_sg[r]);
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
            ScanTraceParams trace_existing = {
                .cfg = &stats->trace,
                .stage = "existing",
                .sprite_order = si + 1,
                .sprite_count = sprite_count,
                .sprite_index = idx,
                .page_index = pi,
                .tx_min = margin_tiles,
                .ty_min = margin_tiles,
                .tx_max = scan_tw,
                .ty_max = scan_th,
                .max_stw = max_stw,
                .max_sth = max_sth,
                .page_used_tw = page_used_tw[pi],
                .page_used_th = page_used_th[pi],
            };
            placed = scan_region(&page_grids[pi], &page_coarse[pi], &page_x4[pi], &page_or_st[pi], rot_sg, rot_sg_x4, rot_ox, rot_oy, rot_count, margin_tiles, margin_tiles, scan_tw, scan_th, &best_tx,
                                 &best_ty, &best_rot, &trace_existing, stats);
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
                coarse_grow(&page_coarse[pi], &page_grids[pi]);
                tgrid_free(&page_x4[pi]);
                page_x4[pi] = tgrid_downsample_x4(&page_grids[pi]);
                or_sparse_free(&page_or_st[pi]);
                page_or_st[pi] = or_sparse_build(&page_grids[pi], global_max_sth);
                page_w[pi] = page_grids[pi].tw * ts;
                page_h[pi] = page_grids[pi].th * ts;
                if (stats->trace.enabled) {
                    NT_LOG_INFO("  TRACE grow page=%u sprite=%u/%u idx=%u old=%ux%u new=%ux%u used=%ux%u", pi, si + 1, sprite_count, idx, old_tw, old_th, page_grids[pi].tw, page_grids[pi].th,
                                page_used_tw[pi], page_used_th[pi]);
                }

                /* Scan full grid: growth added empty space at the edges.
                 * OR-mask skip will jump past the full old area in O(row_words). */
                ScanTraceParams trace_grow = {
                    .cfg = &stats->trace,
                    .stage = "grow",
                    .sprite_order = si + 1,
                    .sprite_count = sprite_count,
                    .sprite_index = idx,
                    .page_index = pi,
                    .tx_min = margin_tiles,
                    .ty_min = margin_tiles,
                    .tx_max = page_grids[pi].tw,
                    .ty_max = page_grids[pi].th,
                    .max_stw = max_stw,
                    .max_sth = max_sth,
                    .page_used_tw = page_used_tw[pi],
                    .page_used_th = page_used_th[pi],
                };
                placed = scan_region(&page_grids[pi], &page_coarse[pi], &page_x4[pi], &page_or_st[pi], rot_sg, rot_sg_x4, rot_ox, rot_oy, rot_count, margin_tiles, margin_tiles, page_grids[pi].tw,
                                     page_grids[pi].th, &best_tx, &best_ty, &best_rot, &trace_grow, stats);

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
            page_coarse[new_page] = coarse_create(atlas_tw, atlas_th);
            page_x4[new_page] = tgrid_downsample_x4(&page_grids[new_page]);
            page_or_st[new_page] = or_sparse_build(&page_grids[new_page], global_max_sth);
            page_w[new_page] = init_dim;
            page_h[new_page] = init_dim;
            if (stats->trace.enabled) {
                NT_LOG_INFO("  TRACE new page=%u sprite=%u/%u idx=%u size=%ux%u", new_page, si + 1, sprite_count, idx, page_grids[new_page].tw, page_grids[new_page].th);
            }

            ScanTraceParams trace_new_page = {
                .cfg = &stats->trace,
                .stage = "new_page",
                .sprite_order = si + 1,
                .sprite_count = sprite_count,
                .sprite_index = idx,
                .page_index = new_page,
                .tx_min = margin_tiles,
                .ty_min = margin_tiles,
                .tx_max = page_grids[new_page].tw,
                .ty_max = page_grids[new_page].th,
                .max_stw = max_stw,
                .max_sth = max_sth,
                .page_used_tw = page_used_tw[new_page],
                .page_used_th = page_used_th[new_page],
            };
            placed = scan_region(&page_grids[new_page], &page_coarse[new_page], &page_x4[new_page], &page_or_st[new_page], rot_sg, rot_sg_x4, rot_ox, rot_oy, rot_count, margin_tiles, margin_tiles,
                                 page_grids[new_page].tw, page_grids[new_page].th, &best_tx, &best_ty, &best_rot, &trace_new_page, stats);
            if (!placed) {
                /* New page too small — grow it */
                while (!placed && (page_grids[new_page].tw < max_tw || page_grids[new_page].th < max_th)) {
                    uint32_t old_tw = 0;
                    uint32_t old_th = 0;
                    tgrid_grow(&page_grids[new_page], max_tw, max_th, &old_tw, &old_th);
                    coarse_grow(&page_coarse[new_page], &page_grids[new_page]);
                    tgrid_free(&page_x4[new_page]);
                    page_x4[new_page] = tgrid_downsample_x4(&page_grids[new_page]);
                    or_sparse_free(&page_or_st[new_page]);
                    page_or_st[new_page] = or_sparse_build(&page_grids[new_page], global_max_sth);
                    page_w[new_page] = page_grids[new_page].tw * ts;
                    page_h[new_page] = page_grids[new_page].th * ts;
                    if (stats->trace.enabled) {
                        NT_LOG_INFO("  TRACE grow page=%u sprite=%u/%u idx=%u old=%ux%u new=%ux%u used=%ux%u", new_page, si + 1, sprite_count, idx, old_tw, old_th, page_grids[new_page].tw,
                                    page_grids[new_page].th, page_used_tw[new_page], page_used_th[new_page]);
                    }
                    ScanTraceParams trace_new_page_grow = {
                        .cfg = &stats->trace,
                        .stage = "new_page_grow",
                        .sprite_order = si + 1,
                        .sprite_count = sprite_count,
                        .sprite_index = idx,
                        .page_index = new_page,
                        .tx_min = margin_tiles,
                        .ty_min = margin_tiles,
                        .tx_max = page_grids[new_page].tw,
                        .ty_max = page_grids[new_page].th,
                        .max_stw = max_stw,
                        .max_sth = max_sth,
                        .page_used_tw = page_used_tw[new_page],
                        .page_used_th = page_used_th[new_page],
                    };
                    placed = scan_region(&page_grids[new_page], &page_coarse[new_page], &page_x4[new_page], &page_or_st[new_page], rot_sg, rot_sg_x4, rot_ox, rot_oy, rot_count, margin_tiles,
                                         margin_tiles, page_grids[new_page].tw, page_grids[new_page].th, &best_tx, &best_ty, &best_rot, &trace_new_page_grow, stats);
                }
            }
            if (placed) {
                best_page = new_page;
            }
        }
        // #endregion

        NT_BUILD_ASSERT(placed && "tile_pack: failed to place sprite");

        {
            double sprite_elapsed = nt_time_now() - sprite_start;
            if (sprite_elapsed > 1.0) {
                NT_LOG_INFO("  SLOW sprite #%u/%u: %.1fs (or=%llu test=%llu grid=%ux%u stw=%u sth=%u)", si, sprite_count, sprite_elapsed, (unsigned long long)(stats->or_count - or_before),
                            (unsigned long long)(stats->test_count - test_before), page_grids[best_page].tw, page_grids[best_page].th, rot_sg[best_rot].tw, rot_sg[best_rot].th);
            }
        }

        /* Stamp onto page grid and update coarse grid + sparse table */
        tgrid_stamp(&page_grids[best_page], &rot_sg[best_rot], (int32_t)best_tx, (int32_t)best_ty, rot_ox[best_rot], rot_oy[best_rot]);
        {
            int32_t stamp_tx = (int32_t)best_tx + rot_ox[best_rot];
            int32_t stamp_ty = (int32_t)best_ty + rot_oy[best_rot];
            uint32_t stamp_sth = rot_sg[best_rot].th;
            coarse_update(&page_coarse[best_page], &page_grids[best_page], stamp_tx, stamp_ty, rot_sg[best_rot].tw, stamp_sth);
            tgrid_x4_update(&page_x4[best_page], &page_grids[best_page], stamp_tx, stamp_ty, rot_sg[best_rot].tw, stamp_sth);
            /* Incremental sparse table update — only recompute rows affected by stamp */
            if (page_or_st[best_page].levels[0]) {
                uint32_t sty = (stamp_ty >= 0) ? (uint32_t)stamp_ty : 0;
                or_sparse_update(&page_or_st[best_page], &page_grids[best_page], sty, stamp_sth);
            }
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
        for (uint32_t fr = 0; fr < rot_count; fr++) {
            tgrid_free(&rot_sg_x4[fr]);
        }
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
        or_sparse_free(&page_or_st[p]);
        tgrid_free(&page_grids[p]);
        coarse_free(&page_coarse[p]);
        tgrid_free(&page_x4[p]);
    }
    for (uint32_t i = 0; i < sprite_count; i++) {
        tgrid_free(&sprite_grids[i]);
    }
    free(sprite_grids);
    free(grid_ox);
    free(grid_oy);
    free(sorted);

    double pack_elapsed = nt_time_now() - pack_start_time;
    NT_LOG_INFO("  tile_pack done: %u placed on %u pages in %.1fs (or=%llu test=%llu yskip=%llu)", placement_count, page_count, pack_elapsed, (unsigned long long)stats->or_count,
                (unsigned long long)stats->test_count, (unsigned long long)stats->yskip_count);

    *out_page_count = page_count;
    return placement_count;
}
// #endregion

// #region Composition — blit trimmed pixels, extrude edges
/* --- Blit trimmed sprite pixels to atlas page --- */

static void blit_sprite(uint8_t *page, uint32_t page_w, const uint8_t *sprite_rgba, uint32_t sprite_w, uint32_t trim_x, uint32_t trim_y, uint32_t trim_w, uint32_t trim_h, uint32_t dest_x,
                        uint32_t dest_y, uint8_t rotation) {
    /* Blit only non-transparent pixels to avoid overwriting neighbors in polygon mode.
     * Rotation 0: fast row-scan with run-length memcpy for opaque spans.
     * Rotations 1/2/3: pixel-by-pixel with coordinate transform + alpha skip. */
    // #region Rotation 0 fast path: row-wise scan with opaque span memcpy
    if (rotation == 0) {
        for (uint32_t sy = 0; sy < trim_h; sy++) {
            const uint8_t *src_row = &sprite_rgba[((size_t)(trim_y + sy) * sprite_w + trim_x) * 4];
            uint8_t *dst_row = &page[((size_t)(dest_y + sy) * page_w + dest_x) * 4];
            uint32_t sx = 0;
            while (sx < trim_w) {
                /* Skip transparent pixels */
                while (sx < trim_w && src_row[sx * 4 + 3] == 0) {
                    sx++;
                }
                if (sx >= trim_w) {
                    break;
                }
                /* Find end of opaque run */
                uint32_t run_start = sx;
                while (sx < trim_w && src_row[sx * 4 + 3] != 0) {
                    sx++;
                }
                memcpy(&dst_row[run_start * 4], &src_row[run_start * 4], (size_t)(sx - run_start) * 4);
            }
        }
        return;
    }
    // #endregion
    for (uint32_t sy = 0; sy < trim_h; sy++) {
        for (uint32_t sx = 0; sx < trim_w; sx++) {
            const uint8_t *src = &sprite_rgba[((size_t)(trim_y + sy) * sprite_w + trim_x + sx) * 4];
            if (src[3] == 0) {
                continue; /* skip fully transparent pixels */
            }
            uint32_t dx;
            uint32_t dy;
            switch (rotation) {
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
// #endregion

// #region Debug PNG — optional outline visualization
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
// #endregion

// #region Atlas cache — disk caching for incremental builds
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
// #endregion

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

// #region Atlas public API — begin, add, end
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
// #endregion

// #region Main pipeline — nt_builder_end_atlas

/* --- Pipeline state: carries data between pipeline steps --- */

typedef struct {
    NtBuilderContext *ctx;
    NtBuildAtlasState *state;
    uint32_t sprite_count;
    NtAtlasSpriteInput *sprites;
    const nt_atlas_opts_t *opts;

    /* Alpha trim */
    uint32_t *trim_x, *trim_y, *trim_w, *trim_h;
    uint8_t **alpha_planes;

    /* Dedup */
    int32_t *dedup_map;
    uint32_t *unique_indices;
    uint32_t unique_count;

    /* Geometry */
    uint32_t *vertex_counts;
    Point2D **hull_vertices;

    /* Packing + composition */
    AtlasPlacement *placements;
    uint32_t placement_count;
    uint32_t page_count;
    uint32_t page_w[ATLAS_MAX_PAGES];
    uint32_t page_h[ATLAS_MAX_PAGES];
    uint8_t **page_pixels;
    bool cache_hit;

    /* Packing statistics (per-call, no static globals) */
    PackStats stats;
} AtlasPipeline;

/* --- pipeline_alpha_trim: extract alpha planes + find tight bounding box --- */

static void pipeline_alpha_trim(AtlasPipeline *p) {
    p->trim_x = (uint32_t *)calloc(p->sprite_count, sizeof(uint32_t));
    p->trim_y = (uint32_t *)calloc(p->sprite_count, sizeof(uint32_t));
    p->trim_w = (uint32_t *)calloc(p->sprite_count, sizeof(uint32_t));
    p->trim_h = (uint32_t *)calloc(p->sprite_count, sizeof(uint32_t));
    p->alpha_planes = (uint8_t **)calloc(p->sprite_count, sizeof(uint8_t *));
    NT_BUILD_ASSERT(p->trim_x && p->trim_y && p->trim_w && p->trim_h && p->alpha_planes && "pipeline_alpha_trim: alloc failed");

    for (uint32_t i = 0; i < p->sprite_count; i++) {
        p->alpha_planes[i] = alpha_plane_extract(p->sprites[i].rgba, p->sprites[i].width, p->sprites[i].height);
        bool has_pixels = alpha_trim(p->alpha_planes[i], p->sprites[i].width, p->sprites[i].height, p->opts->alpha_threshold, &p->trim_x[i], &p->trim_y[i], &p->trim_w[i], &p->trim_h[i]);
        NT_BUILD_ASSERT(has_pixels && "pipeline_alpha_trim: sprite is fully transparent");
    }
}

/* --- pipeline_cache_check: compute cache key and try loading cached result --- */

static void pipeline_cache_check(AtlasPipeline *p) {
    p->state->cache_key = compute_atlas_cache_key(p->sprites, p->sprite_count, p->opts, p->state->has_compress, &p->state->compress);

    if (p->ctx->cache_dir) {
        p->cache_hit = atlas_cache_read(p->ctx->cache_dir, p->state->cache_key, &p->placements, &p->placement_count, p->page_w, p->page_h, &p->page_count, &p->page_pixels);
        if (p->cache_hit) {
            NT_LOG_INFO("Atlas cache hit: %s (key %016llx)", p->state->name, (unsigned long long)p->state->cache_key);
        }
    }
}

/* --- pipeline_dedup: detect duplicate sprites by hash + pixel comparison --- */

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void pipeline_dedup(AtlasPipeline *p) {
    DedupSortEntry *dedup_entries = (DedupSortEntry *)malloc(p->sprite_count * sizeof(DedupSortEntry));
    NT_BUILD_ASSERT(dedup_entries && "pipeline_dedup: alloc failed");
    for (uint32_t i = 0; i < p->sprite_count; i++) {
        dedup_entries[i].index = i;
        dedup_entries[i].hash = p->sprites[i].decoded_hash;
    }
    qsort(dedup_entries, p->sprite_count, sizeof(DedupSortEntry), dedup_sort_cmp);

    /* Map duplicate -> original. -1 = unique. */
    p->dedup_map = (int32_t *)malloc(p->sprite_count * sizeof(int32_t));
    NT_BUILD_ASSERT(p->dedup_map && "pipeline_dedup: alloc failed");
    for (uint32_t i = 0; i < p->sprite_count; i++) {
        p->dedup_map[i] = -1;
    }

    for (uint32_t i = 1; i < p->sprite_count; i++) {
        if (dedup_entries[i].hash == dedup_entries[i - 1].hash) {
            uint32_t curr_idx = dedup_entries[i].index;
            uint32_t prev_idx = dedup_entries[i - 1].index;
            /* Find the original (follow chain) */
            uint32_t orig = prev_idx;
            while (p->dedup_map[orig] >= 0) {
                orig = (uint32_t)p->dedup_map[orig];
            }
            /* Verify trimmed pixels match (dims + byte-level comparison) */
            if (p->trim_w[curr_idx] == p->trim_w[orig] && p->trim_h[curr_idx] == p->trim_h[orig]) {
                bool pixels_match = true;
                uint32_t tw = p->trim_w[curr_idx];
                uint32_t th = p->trim_h[curr_idx];
                for (uint32_t row = 0; row < th && pixels_match; row++) {
                    size_t off_a = (((size_t)(p->trim_y[curr_idx] + row) * p->sprites[curr_idx].width) + p->trim_x[curr_idx]) * 4;
                    size_t off_b = (((size_t)(p->trim_y[orig] + row) * p->sprites[orig].width) + p->trim_x[orig]) * 4;
                    const uint8_t *row_a = p->sprites[curr_idx].rgba + off_a;
                    const uint8_t *row_b = p->sprites[orig].rgba + off_b;
                    if (memcmp(row_a, row_b, ((size_t)tw) * 4) != 0) {
                        pixels_match = false;
                    }
                }
                if (pixels_match) {
                    p->dedup_map[curr_idx] = (int32_t)orig;
                }
            }
        }
    }
    free(dedup_entries);

    /* Count unique sprites */
    p->unique_count = 0;
    p->unique_indices = (uint32_t *)malloc(p->sprite_count * sizeof(uint32_t));
    NT_BUILD_ASSERT(p->unique_indices && "pipeline_dedup: alloc failed");
    for (uint32_t i = 0; i < p->sprite_count; i++) {
        if (p->dedup_map[i] < 0) {
            p->unique_indices[p->unique_count++] = i;
        }
    }
}

/* --- pipeline_geometry: convex hull + simplification + inflation per unique sprite --- */

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void pipeline_geometry(AtlasPipeline *p) {
    p->vertex_counts = (uint32_t *)calloc(p->sprite_count, sizeof(uint32_t));
    p->hull_vertices = (Point2D **)calloc(p->sprite_count, sizeof(Point2D *));
    NT_BUILD_ASSERT(p->vertex_counts && p->hull_vertices && "pipeline_geometry: alloc failed");

    for (uint32_t ui = 0; ui < p->unique_count; ui++) {
        uint32_t idx = p->unique_indices[ui];
        uint32_t tw = p->trim_w[idx];
        uint32_t th = p->trim_h[idx];

        if (p->opts->polygon_mode) {
            /* Extract boundary pixels from alpha plane (dense, cache-friendly) */
            const uint8_t *ap = p->alpha_planes[idx];
            uint32_t aw = p->sprites[idx].width;
            /* Collect boundary pixel CORNERS (not centers) so that the convex hull
             * passes along the outer edge of pixels, fully covering all opaque area.
             * Each boundary pixel contributes 4 corner points (x,y), (x+1,y), (x,y+1), (x+1,y+1). */
            uint32_t pt_count = 0;
            Point2D *pts = (Point2D *)malloc((size_t)tw * th * 4 * sizeof(Point2D)); /* 4 corners per pixel */
            NT_BUILD_ASSERT(pts && "pipeline_geometry: alloc failed");

            for (uint32_t y = 0; y < th; y++) {
                for (uint32_t x = 0; x < tw; x++) {
                    uint8_t a = ap[((size_t)(p->trim_y[idx] + y) * aw) + p->trim_x[idx] + x];
                    if (a >= p->opts->alpha_threshold) {
                        bool is_boundary = (x == 0 || y == 0 || x == tw - 1 || y == th - 1);
                        if (!is_boundary) {
                            size_t base = ((size_t)(p->trim_y[idx] + y) * aw) + p->trim_x[idx] + x;
                            uint8_t left = ap[base - 1];
                            uint8_t right = ap[base + 1];
                            uint8_t up = ap[base - aw];
                            uint8_t down = ap[base + aw];
                            is_boundary = (left < p->opts->alpha_threshold || right < p->opts->alpha_threshold || up < p->opts->alpha_threshold || down < p->opts->alpha_threshold);
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
                p->hull_vertices[idx] = (Point2D *)malloc(4 * sizeof(Point2D));
                NT_BUILD_ASSERT(p->hull_vertices[idx] && "pipeline_geometry: alloc failed");
                p->hull_vertices[idx][0] = (Point2D){0, 0};
                p->hull_vertices[idx][1] = (Point2D){(int32_t)tw, 0};
                p->hull_vertices[idx][2] = (Point2D){(int32_t)tw, (int32_t)th};
                p->hull_vertices[idx][3] = (Point2D){0, (int32_t)th};
                p->vertex_counts[idx] = 4;
            } else {
                Point2D *hull = (Point2D *)malloc((size_t)pt_count * 2 * sizeof(Point2D));
                NT_BUILD_ASSERT(hull && "pipeline_geometry: alloc failed");
                uint32_t hull_count = convex_hull(pts, pt_count, hull);

                Point2D *simplified = (Point2D *)malloc(hull_count * sizeof(Point2D));
                NT_BUILD_ASSERT(simplified && "pipeline_geometry: alloc failed");
                uint32_t simp_count = hull_simplify(hull, hull_count, p->opts->max_vertices, simplified);
                free(hull);

                /* Determine polygon winding (CCW or CW) via signed area */
                double signed_area = 0.0;
                for (uint32_t si = 0; si < simp_count; si++) {
                    uint32_t sj = (si + 1) % simp_count;
                    signed_area += ((double)simplified[si].x * simplified[sj].y) - ((double)simplified[sj].x * simplified[si].y);
                }
                double outside_sign = (signed_area > 0) ? -1.0 : 1.0;

                /* Find max distance from any boundary pixel OUTSIDE simplified polygon */
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
                NT_BUILD_ASSERT(inflated && "pipeline_geometry: alloc failed");
                uint32_t inf_count = polygon_inflate(simplified, simp_count, inflate_amount, inflated);
                free(simplified);

                /* Verify: all boundary pixels must be inside the inflated polygon */
                for (uint32_t bi = 0; bi < pt_count; bi++) {
                    bool inside = false;
                    for (uint32_t ti = 1; ti + 1 < inf_count && !inside; ti++) {
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
                    NT_BUILD_ASSERT(inside && "pipeline_geometry: boundary pixel outside inflated polygon");
                }
                free(pts);

                p->hull_vertices[idx] = inflated;
                p->vertex_counts[idx] = inf_count;
            }
        } else {
            /* Rect mode: 4-vertex rect */
            p->hull_vertices[idx] = (Point2D *)malloc(4 * sizeof(Point2D));
            NT_BUILD_ASSERT(p->hull_vertices[idx] && "pipeline_geometry: alloc failed");
            p->hull_vertices[idx][0] = (Point2D){0, 0};
            p->hull_vertices[idx][1] = (Point2D){(int32_t)tw, 0};
            p->hull_vertices[idx][2] = (Point2D){(int32_t)tw, (int32_t)th};
            p->hull_vertices[idx][3] = (Point2D){0, (int32_t)th};
            p->vertex_counts[idx] = 4;
        }
    }

    /* Copy vertex data for duplicates from their originals */
    for (uint32_t i = 0; i < p->sprite_count; i++) {
        if (p->dedup_map[i] >= 0) {
            uint32_t orig = (uint32_t)p->dedup_map[i];
            p->vertex_counts[i] = p->vertex_counts[orig];
            p->hull_vertices[i] = p->hull_vertices[orig]; /* shared pointer, don't double-free */
        }
    }
}

/* --- pipeline_tile_pack: sort sprites by area, place on atlas pages via tile-grid collision --- */

static void pipeline_tile_pack(AtlasPipeline *p) {
    // NOLINTNEXTLINE(clang-analyzer-optin.portability.UnixAPI)
    p->placements = (AtlasPlacement *)malloc(p->unique_count * sizeof(AtlasPlacement));
    NT_BUILD_ASSERT(p->placements && "pipeline_tile_pack: alloc failed");

    /* Build arrays for unique sprites: trim dims + hull polygons */
    uint32_t *u_trim_w = (uint32_t *)malloc(p->unique_count * sizeof(uint32_t));
    uint32_t *u_trim_h = (uint32_t *)malloc(p->unique_count * sizeof(uint32_t));
    Point2D **u_hulls = (Point2D **)malloc(p->unique_count * sizeof(Point2D *));
    uint32_t *u_hull_counts = (uint32_t *)malloc(p->unique_count * sizeof(uint32_t));
    NT_BUILD_ASSERT(u_trim_w && u_trim_h && u_hulls && u_hull_counts && "pipeline_tile_pack: alloc failed");
    for (uint32_t i = 0; i < p->unique_count; i++) {
        uint32_t oi = p->unique_indices[i];
        u_trim_w[i] = p->trim_w[oi];
        u_trim_h[i] = p->trim_h[oi];
        u_hulls[i] = p->hull_vertices[oi];
        u_hull_counts[i] = p->vertex_counts[oi];
    }

    if (p->opts->vector_pack) {
        NT_LOG_INFO("  vector_pack: %u sprites (NFP mode)", p->unique_count);
        p->placement_count = vector_pack(u_trim_w, u_trim_h, u_hulls, u_hull_counts, p->unique_count, p->opts, p->placements, &p->page_count, p->page_w, p->page_h, &p->stats);
    } else {
        p->placement_count = tile_pack(u_trim_w, u_trim_h, u_hulls, u_hull_counts, p->unique_count, p->opts, p->placements, &p->page_count, p->page_w, p->page_h, &p->stats);
    }

    /* Fill trim offsets and remap sprite_index back to original */
    for (uint32_t i = 0; i < p->placement_count; i++) {
        uint32_t unique_idx = p->placements[i].sprite_index;
        uint32_t orig_idx = p->unique_indices[unique_idx];
        p->placements[i].sprite_index = orig_idx;
        p->placements[i].trim_x = p->trim_x[orig_idx];
        p->placements[i].trim_y = p->trim_y[orig_idx];
        p->placements[i].trimmed_w = p->trim_w[orig_idx];
        p->placements[i].trimmed_h = p->trim_h[orig_idx];
    }

    free(u_trim_w);
    free(u_trim_h);
    free((void *)u_hulls);
    free(u_hull_counts);
}

/* --- pipeline_compose: blit trimmed pixels onto pages + extrude edges --- */

static void pipeline_compose(AtlasPipeline *p) {
    uint32_t extrude_val = p->opts->extrude;

    p->page_pixels = (uint8_t **)calloc(p->page_count, sizeof(uint8_t *));
    NT_BUILD_ASSERT(p->page_pixels && "pipeline_compose: alloc failed");

    for (uint32_t pg = 0; pg < p->page_count; pg++) {
        p->page_pixels[pg] = (uint8_t *)calloc((size_t)p->page_w[pg] * p->page_h[pg] * 4, 1);
        NT_BUILD_ASSERT(p->page_pixels[pg] && "pipeline_compose: page alloc failed");
    }

    /* Blit each placed sprite */
    for (uint32_t pi = 0; pi < p->placement_count; pi++) {
        AtlasPlacement *pl = &p->placements[pi];
        uint32_t idx = pl->sprite_index;
        uint32_t inner_x = pl->x + extrude_val;
        uint32_t inner_y = pl->y + extrude_val;

        blit_sprite(p->page_pixels[pl->page], p->page_w[pl->page], p->sprites[idx].rgba, p->sprites[idx].width, pl->trim_x, pl->trim_y, pl->trimmed_w, pl->trimmed_h, inner_x, inner_y, pl->rotation);

        uint32_t blit_w = (pl->rotation == 1 || pl->rotation == 3) ? pl->trimmed_h : pl->trimmed_w;
        uint32_t blit_h = (pl->rotation == 1 || pl->rotation == 3) ? pl->trimmed_w : pl->trimmed_h;
        extrude_edges(p->page_pixels[pl->page], p->page_w[pl->page], p->page_h[pl->page], inner_x, inner_y, blit_w, blit_h, extrude_val);
    }
}

/* --- pipeline_debug_png: optional outline visualization --- */

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void pipeline_debug_png(AtlasPipeline *p) {
    if (!p->opts->debug_png) {
        return;
    }
    uint32_t extrude_val = p->opts->extrude;

    for (uint32_t pg = 0; pg < p->page_count; pg++) {
        size_t page_bytes = (size_t)p->page_w[pg] * p->page_h[pg] * 4;
        uint8_t *debug_page = (uint8_t *)malloc(page_bytes);
        NT_BUILD_ASSERT(debug_page && "pipeline_debug_png: alloc failed");
        memcpy(debug_page, p->page_pixels[pg], page_bytes);

        for (uint32_t pi = 0; pi < p->placement_count; pi++) {
            if (p->placements[pi].page != pg) {
                continue;
            }
            uint32_t ix = p->placements[pi].x + extrude_val;
            uint32_t iy = p->placements[pi].y + extrude_val;
            uint32_t si = p->placements[pi].sprite_index;

            if (p->opts->polygon_mode && p->hull_vertices[si] && p->vertex_counts[si] >= 3) {
                debug_draw_hull_outline(debug_page, p->page_w[pg], p->page_h[pg], p->hull_vertices[si], p->vertex_counts[si], ix, iy, p->trim_w[si], p->trim_h[si], p->placements[pi].rotation);
            } else {
                uint32_t rw = (p->placements[pi].rotation == 1 || p->placements[pi].rotation == 3) ? p->placements[pi].trimmed_h : p->placements[pi].trimmed_w;
                uint32_t rh = (p->placements[pi].rotation == 1 || p->placements[pi].rotation == 3) ? p->placements[pi].trimmed_w : p->placements[pi].trimmed_h;
                debug_draw_rect_outline(debug_page, p->page_w[pg], p->page_h[pg], ix, iy, rw, rh);
            }
        }

        char debug_path[512];
        const char *slash = strrchr(p->ctx->output_path, '/');
        const char *bslash = strrchr(p->ctx->output_path, '\\');
        const char *sep = (bslash > slash) ? bslash : slash;
        if (sep) {
            size_t dir_len = (size_t)(sep - p->ctx->output_path) + 1;
            (void)snprintf(debug_path, sizeof(debug_path), "%.*s%s_page%u.png", (int)dir_len, p->ctx->output_path, p->state->name, pg);
        } else {
            (void)snprintf(debug_path, sizeof(debug_path), "%s_page%u.png", p->state->name, pg);
        }
        stbi_write_png(debug_path, (int)p->page_w[pg], (int)p->page_h[pg], 4, debug_page, (int)(p->page_w[pg] * 4));
        NT_LOG_INFO("Debug PNG: %s (%ux%u)", debug_path, p->page_w[pg], p->page_h[pg]);
        free(debug_page);
    }
}

/* --- pipeline_cache_write: store packing result for next build --- */

static void pipeline_cache_write(AtlasPipeline *p) {
    if (p->ctx->cache_dir) {
        if (atlas_cache_write(p->ctx->cache_dir, p->state->cache_key, p->placements, p->placement_count, p->page_w, p->page_h, p->page_count, p->page_pixels)) {
            NT_LOG_INFO("Atlas cache stored: %s (key %016llx)", p->state->name, (unsigned long long)p->state->cache_key);
        }
    }
}

/* --- pipeline_serialize: compute atlas UVs, write binary blob --- */

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void pipeline_serialize(AtlasPipeline *p) {
    uint32_t extrude_val = p->opts->extrude;

    /* Count total vertices */
    uint32_t total_vertex_count = 0;
    for (uint32_t i = 0; i < p->sprite_count; i++) {
        total_vertex_count += p->vertex_counts[i];
    }

    /* Build placement lookup: original_sprite_index -> placement index */
    uint32_t *placement_lookup = (uint32_t *)malloc(p->sprite_count * sizeof(uint32_t));
    NT_BUILD_ASSERT(placement_lookup && "pipeline_serialize: alloc failed");
    memset(placement_lookup, 0xFF, p->sprite_count * sizeof(uint32_t));

    for (uint32_t pi = 0; pi < p->placement_count; pi++) {
        placement_lookup[p->placements[pi].sprite_index] = pi;
    }
    for (uint32_t i = 0; i < p->sprite_count; i++) {
        if (p->dedup_map[i] >= 0) {
            uint32_t orig = (uint32_t)p->dedup_map[i];
            placement_lookup[i] = placement_lookup[orig];
        }
    }

    /* Serialize blob: header + texture_resource_ids + regions + vertices */
    uint32_t regions_offset = (uint32_t)sizeof(NtAtlasHeader) + (p->page_count * (uint32_t)sizeof(uint64_t));
    uint32_t vertex_offset = regions_offset + (p->sprite_count * (uint32_t)sizeof(NtAtlasRegion));
    uint32_t blob_size = vertex_offset + (total_vertex_count * (uint32_t)sizeof(NtAtlasVertex));
    uint8_t *blob = (uint8_t *)calloc(1, blob_size);
    NT_BUILD_ASSERT(blob && "pipeline_serialize: blob alloc failed");

    /* Header */
    NtAtlasHeader *hdr = (NtAtlasHeader *)blob;
    hdr->magic = NT_ATLAS_MAGIC;
    hdr->version = NT_ATLAS_VERSION;
    hdr->region_count = (uint16_t)p->sprite_count;
    hdr->page_count = (uint16_t)p->page_count;
    hdr->_pad = 0;
    hdr->vertex_offset = vertex_offset;
    hdr->total_vertex_count = total_vertex_count;

    /* Texture resource IDs (D-05) */
    uint8_t *tex_ids_ptr = blob + sizeof(NtAtlasHeader);
    for (uint32_t pg = 0; pg < p->page_count; pg++) {
        char tex_path[512];
        (void)snprintf(tex_path, sizeof(tex_path), "%s/tex%u", p->state->name, pg);
        uint64_t tid = nt_hash64_str(tex_path).value;
        memcpy(tex_ids_ptr + ((size_t)pg * sizeof(uint64_t)), &tid, sizeof(uint64_t));
    }

    /* Regions + vertices */
    NtAtlasRegion *regions = (NtAtlasRegion *)(blob + regions_offset);
    NtAtlasVertex *vertices = (NtAtlasVertex *)(blob + vertex_offset);
    uint32_t vertex_cursor = 0;

    for (uint32_t i = 0; i < p->sprite_count; i++) {
        uint32_t pi = placement_lookup[i];
        NT_BUILD_ASSERT(pi != UINT32_MAX && "pipeline_serialize: sprite has no placement");
        AtlasPlacement *pl = &p->placements[pi];

        NtAtlasRegion *reg = &regions[i];
        reg->name_hash = nt_hash64_str(p->sprites[i].name).value;
        reg->source_w = (uint16_t)p->sprites[i].width;
        reg->source_h = (uint16_t)p->sprites[i].height;
        reg->trim_offset_x = (int16_t)p->trim_x[i];
        reg->trim_offset_y = (int16_t)p->trim_y[i];
        reg->origin_x = p->sprites[i].origin_x;
        reg->origin_y = p->sprites[i].origin_y;
        reg->vertex_start = (uint16_t)vertex_cursor;
        reg->vertex_count = (uint8_t)p->vertex_counts[i];
        reg->page_index = (uint8_t)pl->page;
        reg->rotated = pl->rotation;
        memset(reg->_pad, 0, sizeof(reg->_pad));

        uint32_t inner_x = pl->x + extrude_val;
        uint32_t inner_y = pl->y + extrude_val;
        uint32_t atlas_w = p->page_w[pl->page];
        uint32_t atlas_h = p->page_h[pl->page];

        for (uint32_t v = 0; v < p->vertex_counts[i]; v++) {
            NtAtlasVertex *vtx = &vertices[vertex_cursor++];
            int32_t lx = p->hull_vertices[i][v].x;
            int32_t ly = p->hull_vertices[i][v].y;
            vtx->local_x = (int16_t)lx;
            vtx->local_y = (int16_t)ly;

            float atlas_px;
            float atlas_py;
            switch (pl->rotation) {
            case 0:
                atlas_px = (float)inner_x + (float)lx;
                atlas_py = (float)inner_y + (float)ly;
                break;
            case 1:
                atlas_px = (float)inner_x + (float)((int32_t)p->trim_h[pl->sprite_index] - 1 - ly);
                atlas_py = (float)inner_y + (float)lx;
                break;
            case 2:
                atlas_px = (float)inner_x + (float)((int32_t)p->trim_w[pl->sprite_index] - 1 - lx);
                atlas_py = (float)inner_y + (float)((int32_t)p->trim_h[pl->sprite_index] - 1 - ly);
                break;
            case 3:
                atlas_px = (float)inner_x + (float)ly;
                atlas_py = (float)inner_y + (float)((int32_t)p->trim_w[pl->sprite_index] - 1 - lx);
                break;
            default:
                atlas_px = (float)inner_x + (float)lx;
                atlas_py = (float)inner_y + (float)ly;
                break;
            }

            float tmp_u = ((atlas_px * 65535.0F) / (float)atlas_w) + 0.5F;
            float tmp_v = ((atlas_py * 65535.0F) / (float)atlas_h) + 0.5F;
            if (tmp_u < 0.0F) { tmp_u = 0.0F; }
            if (tmp_v < 0.0F) { tmp_v = 0.0F; }
            if (tmp_u > 65535.0F) { tmp_u = 65535.0F; }
            if (tmp_v > 65535.0F) { tmp_v = 65535.0F; }
            vtx->atlas_u = (uint16_t)tmp_u;
            vtx->atlas_v = (uint16_t)tmp_v;
        }
    }

    NT_BUILD_ASSERT(vertex_cursor == total_vertex_count && "pipeline_serialize: vertex count mismatch");

    /* Register atlas metadata entry (D-04) */
    uint64_t blob_hash = nt_hash64(blob, blob_size).value;
    nt_builder_add_entry(p->ctx, p->state->name, NT_BUILD_ASSET_ATLAS, NULL, blob, blob_size, blob_hash);

    free(placement_lookup);
}

/* --- pipeline_register: add texture page entries + codegen info --- */

static void pipeline_register(AtlasPipeline *p) {
    /* Register texture page entries */
    for (uint32_t pg = 0; pg < p->page_count; pg++) {
        char tex_path[512];
        (void)snprintf(tex_path, sizeof(tex_path), "%s/tex%u", p->state->name, pg);

        uint32_t pixel_bytes = p->page_w[pg] * p->page_h[pg] * 4;
        uint64_t tex_hash = nt_hash64(p->page_pixels[pg], pixel_bytes).value;

        NtBuildTextureData *td = (NtBuildTextureData *)calloc(1, sizeof(NtBuildTextureData));
        NT_BUILD_ASSERT(td && "pipeline_register: alloc failed");
        td->width = p->page_w[pg];
        td->height = p->page_h[pg];
        td->opts.format = p->opts->format;
        td->opts.max_size = 0;
        td->opts.compress = NULL;
        if (p->state->has_compress) {
            td->compress = p->state->compress;
            td->has_compress = true;
        }
        nt_builder_add_entry(p->ctx, tex_path, NT_BUILD_ASSET_TEXTURE, td, p->page_pixels[pg], pixel_bytes, tex_hash);
        p->page_pixels[pg] = NULL; /* ownership transferred */
    }

    /* Store region info for codegen */
    for (uint32_t i = 0; i < p->sprite_count; i++) {
        if (p->ctx->atlas_region_count >= p->ctx->atlas_region_capacity) {
            uint32_t new_cap = (p->ctx->atlas_region_capacity == 0) ? 64 : p->ctx->atlas_region_capacity * 2;
            NtAtlasRegionCodegen *new_arr = (NtAtlasRegionCodegen *)realloc(p->ctx->atlas_regions, new_cap * sizeof(NtAtlasRegionCodegen));
            NT_BUILD_ASSERT(new_arr && "pipeline_register: alloc failed");
            p->ctx->atlas_regions = new_arr;
            p->ctx->atlas_region_capacity = new_cap;
        }

        char region_path[512];
        (void)snprintf(region_path, sizeof(region_path), "%s/%s", p->state->name, p->sprites[i].name);

        NtAtlasRegionCodegen *reg = &p->ctx->atlas_regions[p->ctx->atlas_region_count++];
        reg->path = nt_builder_normalize_path(region_path);
        reg->resource_id = nt_hash64_str(reg->path).value;
    }
}

/* --- pipeline_cleanup: free all temporary allocations --- */

static void pipeline_cleanup(AtlasPipeline *p) {
    /* Free sprite RGBA pixels and names */
    for (uint32_t i = 0; i < p->sprite_count; i++) {
        free(p->sprites[i].rgba);
        p->sprites[i].rgba = NULL;
        free(p->sprites[i].name);
        p->sprites[i].name = NULL;
    }

    /* Free hull vertices (careful: duplicates share pointers) */
    for (uint32_t i = 0; i < p->sprite_count; i++) {
        if (p->dedup_map[i] < 0 && p->hull_vertices[i]) {
            free(p->hull_vertices[i]);
        }
        p->hull_vertices[i] = NULL;
    }

    /* Free alpha planes */
    for (uint32_t i = 0; i < p->sprite_count; i++) {
        free(p->alpha_planes[i]);
    }
    free((void *)p->alpha_planes);

    free(p->trim_x);
    free(p->trim_y);
    free(p->trim_w);
    free(p->trim_h);
    free(p->dedup_map);
    free(p->unique_indices);
    free(p->vertex_counts);
    free((void *)p->hull_vertices);
    free(p->placements);

    /* Free remaining page pixels (any not transferred to entries) */
    for (uint32_t pg = 0; pg < p->page_count; pg++) {
        free(p->page_pixels[pg]);
    }
    free((void *)p->page_pixels);

    /* Free atlas state */
    free(p->state->sprites);
    free(p->state->name);
    free(p->state);
    p->ctx->active_atlas = NULL;
    p->ctx->atlas_count++;
}

/* --- nt_builder_end_atlas: orchestrator — calls pipeline steps in order --- */

void nt_builder_end_atlas(NtBuilderContext *ctx) {
    NT_BUILD_ASSERT(ctx && "end_atlas: ctx is NULL");
    NT_BUILD_ASSERT(ctx->active_atlas && "end_atlas: no active atlas");

    NtBuildAtlasState *state = ctx->active_atlas;
    NT_BUILD_ASSERT(state->sprite_count > 0 && "end_atlas: atlas has no sprites");

    AtlasPipeline p = {0};
    p.ctx = ctx;
    p.state = state;
    p.sprite_count = state->sprite_count;
    p.sprites = state->sprites;
    p.opts = &state->opts;

    NT_LOG_INFO("  end_atlas: %u sprites, starting pipeline...", p.sprite_count);
    double t0 = nt_time_now();
    double t_total = t0;

    pipeline_alpha_trim(&p);
    double bench_alpha_trim = nt_time_now() - t0;

    pipeline_cache_check(&p);

    t0 = nt_time_now();
    pipeline_dedup(&p);
    double bench_dedup = nt_time_now() - t0;

    NT_LOG_INFO("  prep: %u sprites (%u unique), starting geometry...", p.sprite_count, p.unique_count);
    t0 = nt_time_now();
    pipeline_geometry(&p);
    double bench_geometry = nt_time_now() - t0;
    NT_LOG_INFO("  geometry done in %.1fs", bench_geometry);

    double bench_tile_pack = 0.0;
    double bench_compose = 0.0;
    double bench_debug_png = 0.0;
    if (!p.cache_hit) {
        t0 = nt_time_now();
        pipeline_tile_pack(&p);
        bench_tile_pack = nt_time_now() - t0;

        t0 = nt_time_now();
        pipeline_compose(&p);
        bench_compose = nt_time_now() - t0;

        t0 = nt_time_now();
        pipeline_debug_png(&p);
        bench_debug_png = nt_time_now() - t0;

        pipeline_cache_write(&p);
    }

    t0 = nt_time_now();
    pipeline_serialize(&p);
    pipeline_register(&p);
    double bench_serialize = nt_time_now() - t0;

    double bench_total = nt_time_now() - t_total;
    NT_LOG_INFO("Atlas packed: %u sprites (%u unique), %u pages", p.sprite_count, p.unique_count, p.page_count);
    NT_LOG_INFO("BENCH alpha_trim=%.1f dedup=%.1f geometry=%.1f tile_pack=%.1f compose=%.1f debug_png=%.1f serialize=%.1f total=%.1f pages=%u or_ops=%llu test_ops=%llu", bench_alpha_trim * 1000.0,
                bench_dedup * 1000.0, bench_geometry * 1000.0, bench_tile_pack * 1000.0, bench_compose * 1000.0, bench_debug_png * 1000.0, bench_serialize * 1000.0, bench_total * 1000.0, p.page_count,
                (unsigned long long)p.stats.or_count, (unsigned long long)p.stats.test_count);

    pipeline_cleanup(&p);
}
// #endregion
