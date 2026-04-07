/* clang-format off */
#include "nt_builder_internal.h"
#include "nt_clipper2_bridge.h"
#include "hash/nt_hash.h"
#include "nt_atlas_format.h"
#include "time/nt_time.h"
#include "stb_image.h"
#include "stb_image_write.h"
/* clang-format on */

#include <ctype.h>
#include <math.h>
#include <stdatomic.h>
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
 *  pipeline_geometry       Polygon outline -> simplify -> inflate (convex hull or concave contour)
 *  --- skip on cache hit ---
 *  pipeline_tile_pack      Place sprites via vector_pack (NFP/Minkowski)
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
 *    1. Convex hull or concave contour (alpha-mask trace + RDP simplify)
 *    2. Inflated by extrude+padding to cover boundary pixels
 *    3. Fed to vector_pack (Minkowski-sum NFP collision)
 *
 *  vector_pack uses a No-Fit Polygon approach: for each candidate
 *  position the incoming polygon is tested against the union of NFPs
 *  of all already-placed sprites. Sub-pixel exact, supports concave
 *  outlines and 8-orientation D4 transforms. See nt_builder_vector.inl.
 *
 *  Pages grow dynamically as needed (vector_pack handles its own
 *  page creation when no fit on existing pages).
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
    if (ea->index < eb->index) {
        return -1;
    }
    if (ea->index > eb->index) {
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

/* Greedy perpendicular-distance simplification: iteratively removes the vertex with
 * smallest perpendicular distance to its prev-next chord until exactly max_vertices
 * remain. Always produces exactly min(n, max_vertices) vertices (no RDP discontinuity).
 * out_max_dev returns the largest perpendicular deviation seen during removals — but
 * callers that need the exact inflate amount should post-compute it from the final
 * polygon (this tracker is only a coarse upper bound). */
static uint32_t hull_simplify_perp(const Point2D *hull, uint32_t n, uint32_t max_vertices, Point2D *out, double *out_max_dev) {
    if (n <= max_vertices) {
        memcpy(out, hull, n * sizeof(Point2D));
        *out_max_dev = 0.0;
        return n;
    }
    bool *keep = (bool *)malloc(n * sizeof(bool));
    NT_BUILD_ASSERT(keep && "hull_simplify_perp: alloc failed");
    for (uint32_t i = 0; i < n; i++) {
        keep[i] = true;
    }
    uint32_t current_count = n;
    double max_dev = 0.0;
    while (current_count > max_vertices) {
        double min_dev = 1e30;
        uint32_t min_idx = 0;
        for (uint32_t i = 0; i < n; i++) {
            if (!keep[i]) {
                continue;
            }
            uint32_t prev = i;
            do {
                prev = (prev == 0) ? (n - 1) : (prev - 1);
            } while (!keep[prev] && prev != i);
            uint32_t next = i;
            do {
                next = (next + 1) % n;
            } while (!keep[next] && next != i);
            if (prev == i || next == i) {
                continue;
            }
            double dx = (double)(hull[next].x - hull[prev].x);
            double dy = (double)(hull[next].y - hull[prev].y);
            double base = sqrt((dx * dx) + (dy * dy));
            double cross = fabs(((double)(hull[i].x - hull[prev].x) * dy) - ((double)(hull[i].y - hull[prev].y) * dx));
            double dev = (base > 0.0) ? (cross / base) : 0.0;
            if (dev < min_dev) {
                min_dev = dev;
                min_idx = i;
            }
        }
        if (min_dev > max_dev) {
            max_dev = min_dev;
        }
        keep[min_idx] = false;
        current_count--;
    }
    uint32_t count = 0;
    for (uint32_t i = 0; i < n; i++) {
        if (keep[i]) {
            out[count++] = hull[i];
        }
    }
    free(keep);
    *out_max_dev = max_dev;
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
/* --- Point-in-triangle test (for ear-clipping) --- */

static bool point_in_triangle(Point2D a, Point2D b, Point2D c, Point2D p) {
    int64_t d1 = cross2d(a, b, p);
    int64_t d2 = cross2d(b, c, p);
    int64_t d3 = cross2d(c, a, p);
    bool has_neg = (d1 < 0) || (d2 < 0) || (d3 < 0);
    bool has_pos = (d1 > 0) || (d2 > 0) || (d3 > 0);
    return !(has_neg && has_pos);
}

/* --- Triangulation via Clipper2 Constrained Delaunay Triangulation --- */

/* Triangulates a simple polygon (convex or concave). Uses Clipper2 CDT
 * for better triangle quality than ear-clipping. Falls back to fan_triangulate
 * on failure (e.g., self-intersecting input).
 * Input:  polygon vertices (CCW winding), vertex count n.
 * Output: triangle indices (local 0..n-1) written to 'indices'.
 * Returns number of triangles. */
static uint32_t ear_clip_triangulate(const Point2D *poly, uint32_t n, uint16_t *indices) {
    if (n < 3) {
        return 0;
    }
    if (n == 3) {
        indices[0] = 0;
        indices[1] = 1;
        indices[2] = 2;
        return 1;
    }
    /* Convert to flat xy and call Clipper2 CDT via bridge */
    int32_t stack_xy[64];
    int32_t *xy = (n <= 32) ? stack_xy : (int32_t *)malloc(n * 2 * sizeof(int32_t));
    NT_BUILD_ASSERT(xy && "triangulate: alloc failed");
    for (uint32_t i = 0; i < n; i++) {
        xy[i * 2] = poly[i].x;
        xy[i * 2 + 1] = poly[i].y;
    }
    uint16_t *cdt_indices = NULL;
    uint32_t tri_count = nt_clipper2_triangulate(xy, n, &cdt_indices);
    if (xy != stack_xy) {
        free(xy);
    }
    if (tri_count == 0 || !cdt_indices) {
        /* Clipper2 CDT failed (degenerate/self-intersecting) — fallback to fan */
        free(cdt_indices);
        return fan_triangulate(n, indices);
    }
    memcpy(indices, cdt_indices, tri_count * 3 * sizeof(uint16_t));
    free(cdt_indices);
    return tri_count;
}

/* --- Point-in-polygon test (ray casting, even-odd rule) --- */

static bool point_in_polygon(const Point2D *poly, uint32_t n, Point2D p) {
    bool inside = false;
    for (uint32_t i = 0, j = n - 1; i < n; j = i++) {
        if (((poly[i].y > p.y) != (poly[j].y > p.y)) && (p.x < (int32_t)(((int64_t)(poly[j].x - poly[i].x) * (int64_t)(p.y - poly[i].y)) / (int64_t)(poly[j].y - poly[i].y) + poly[i].x))) {
            inside = !inside;
        }
    }
    return inside;
}

/* Float-coord point-in-polygon (even-odd rule) — used to test pixel centers
 * (which live at non-integer (x+0.5, y+0.5) positions) against an integer
 * polygon. */
static bool point_in_polygon_f(const Point2D *poly, uint32_t n, double px, double py) {
    bool inside = false;
    for (uint32_t i = 0, j = n - 1; i < n; j = i++) {
        double xi = (double)poly[i].x;
        double yi = (double)poly[i].y;
        double xj = (double)poly[j].x;
        double yj = (double)poly[j].y;
        if (((yi > py) != (yj > py)) && (px < ((xj - xi) * (py - yi) / (yj - yi)) + xi)) {
            inside = !inside;
        }
    }
    return inside;
}

/* Computes the maximum distance from any opaque pixel center (alpha >= threshold)
 * to the candidate polygon's boundary, for pixel centers that lie OUTSIDE the
 * polygon. Returns 0 if every opaque pixel center is inside the polygon.
 * Used to determine the minimum Clipper2 inflate amount needed so the inflated
 * polygon fully encloses every opaque pixel center (stricter than the clean
 * contour vertices, which sit at integer pixel corners and miss +0.5 offsets). */
static double polygon_max_outside_pixel_distance(const Point2D *poly, uint32_t poly_count, const uint8_t *binary, uint32_t tw, uint32_t th) {
    double max_d = 0.0;
    for (uint32_t y = 0; y < th; y++) {
        for (uint32_t x = 0; x < tw; x++) {
            if (binary[((size_t)y * tw) + x] == 0) {
                continue;
            }
            double cx = (double)x + 0.5;
            double cy = (double)y + 0.5;
            if (point_in_polygon_f(poly, poly_count, cx, cy)) {
                continue;
            }
            /* Outside: compute distance to nearest polygon edge. */
            double min_d_sq = 1e30;
            for (uint32_t i = 0; i < poly_count; i++) {
                uint32_t j = (i + 1) % poly_count;
                /* Distance from (cx,cy) to segment (poly[i], poly[j]). */
                double ax = (double)poly[i].x;
                double ay = (double)poly[i].y;
                double bx = (double)poly[j].x;
                double by = (double)poly[j].y;
                double dx = bx - ax;
                double dy = by - ay;
                double len_sq = (dx * dx) + (dy * dy);
                double t = (len_sq > 0.0) ? (((cx - ax) * dx) + ((cy - ay) * dy)) / len_sq : 0.0;
                if (t < 0.0) {
                    t = 0.0;
                } else if (t > 1.0) {
                    t = 1.0;
                }
                double qx = ax + (t * dx);
                double qy = ay + (t * dy);
                double pdx = cx - qx;
                double pdy = cy - qy;
                double d_sq = (pdx * pdx) + (pdy * pdy);
                if (d_sq < min_d_sq) {
                    min_d_sq = d_sq;
                }
            }
            double d = sqrt(min_d_sq);
            if (d > max_d) {
                max_d = d;
            }
        }
    }
    return max_d;
}

/* --- Binary morphology: 4-connected dilation (one step) --- */

/* Writes 'out' as the 4-connected dilation of 'in' by one pixel.
 * 'in' and 'out' must be distinct (tw*th bytes each). */
static void binary_dilate_4conn(const uint8_t *in, uint8_t *out, uint32_t tw, uint32_t th) {
    for (uint32_t y = 0; y < th; y++) {
        for (uint32_t x = 0; x < tw; x++) {
            size_t i = ((size_t)y * tw) + x;
            uint8_t v = in[i];
            uint8_t left = (x > 0) ? in[i - 1] : 0;
            uint8_t right = (x + 1 < tw) ? in[i + 1] : 0;
            uint8_t up = (y > 0) ? in[i - tw] : 0;
            uint8_t down = (y + 1 < th) ? in[i + tw] : 0;
            out[i] = (v | left | right | up | down) ? (uint8_t)1 : (uint8_t)0;
        }
    }
}

/* --- Binary morphology: count 4-connected components via flood fill --- */

/* Floods one component starting from (sx, sy). Marks reached cells in 'visited'.
 * Uses caller-provided 'stack' (tw*th*2 int32_t entries) for DFS. */
static void binary_flood_one_component(const uint8_t *M, uint32_t tw, uint32_t th, uint32_t sx, uint32_t sy, uint8_t *visited, int32_t *stack) {
    size_t sp = 0;
    stack[sp * 2] = (int32_t)sx;
    stack[(sp * 2) + 1] = (int32_t)sy;
    sp++;
    visited[((size_t)sy * tw) + sx] = 1;
    while (sp > 0) {
        sp--;
        int32_t cx = stack[sp * 2];
        int32_t cy = stack[(sp * 2) + 1];
        static const int32_t dx[4] = {1, -1, 0, 0};
        static const int32_t dy[4] = {0, 0, 1, -1};
        for (int d = 0; d < 4; d++) {
            int32_t nx = cx + dx[d];
            int32_t ny = cy + dy[d];
            if (nx < 0 || ny < 0 || nx >= (int32_t)tw || ny >= (int32_t)th) {
                continue;
            }
            size_t ni = ((size_t)ny * tw) + (size_t)nx;
            if (M[ni] && !visited[ni]) {
                visited[ni] = 1;
                stack[sp * 2] = nx;
                stack[(sp * 2) + 1] = ny;
                sp++;
            }
        }
    }
}

/* Returns the number of 4-connected opaque components in 'M'.
 * Caller must provide scratch buffers 'visited' (tw*th bytes, will be overwritten)
 * and 'stack' (tw*th*2 int32_t entries). */
static uint32_t binary_count_components(const uint8_t *M, uint32_t tw, uint32_t th, uint8_t *visited, int32_t *stack) {
    memset(visited, 0, (size_t)tw * th);
    uint32_t comp_count = 0;
    for (uint32_t sy = 0; sy < th; sy++) {
        for (uint32_t sx = 0; sx < tw; sx++) {
            size_t si = ((size_t)sy * tw) + sx;
            if (!M[si] || visited[si]) {
                continue;
            }
            comp_count++;
            binary_flood_one_component(M, tw, th, sx, sy, visited, stack);
        }
    }
    return comp_count;
}

static bool binary_is_boundary_pixel(const uint8_t *binary, uint32_t tw, uint32_t th, uint32_t x, uint32_t y) {
    size_t i = ((size_t)y * tw) + x;
    if (!binary[i]) {
        return false;
    }
    if (x == 0 || !binary[i - 1]) {
        return true;
    }
    if (x + 1 >= tw || !binary[i + 1]) {
        return true;
    }
    if (y == 0 || !binary[i - tw]) {
        return true;
    }
    if (y + 1 >= th || !binary[i + tw]) {
        return true;
    }
    return false;
}

static Point2D *binary_build_convex_polygon(const uint8_t *binary, uint32_t tw, uint32_t th, uint32_t max_vertices, uint32_t *out_count) {
    uint32_t boundary_pixel_count = 0;
    for (uint32_t y = 0; y < th; y++) {
        for (uint32_t x = 0; x < tw; x++) {
            if (binary_is_boundary_pixel(binary, tw, th, x, y)) {
                boundary_pixel_count++;
            }
        }
    }
    NT_BUILD_ASSERT(boundary_pixel_count > 0 && "binary_build_convex_polygon: empty mask");

    uint32_t point_count = boundary_pixel_count * 4;
    Point2D *points = (Point2D *)malloc((size_t)point_count * sizeof(Point2D));
    NT_BUILD_ASSERT(points && "binary_build_convex_polygon: alloc failed");

    uint32_t p = 0;
    for (uint32_t y = 0; y < th; y++) {
        for (uint32_t x = 0; x < tw; x++) {
            if (!binary_is_boundary_pixel(binary, tw, th, x, y)) {
                continue;
            }
            points[p++] = (Point2D){(int32_t)x, (int32_t)y};
            points[p++] = (Point2D){(int32_t)x + 1, (int32_t)y};
            points[p++] = (Point2D){(int32_t)x + 1, (int32_t)y + 1};
            points[p++] = (Point2D){(int32_t)x, (int32_t)y + 1};
        }
    }

    Point2D *hull = (Point2D *)malloc((size_t)point_count * 2 * sizeof(Point2D));
    NT_BUILD_ASSERT(hull && "binary_build_convex_polygon: alloc failed");
    uint32_t hull_count = convex_hull(points, point_count, hull);
    free(points);

    NT_BUILD_ASSERT(hull_count >= 3 && "binary_build_convex_polygon: convex hull is degenerate");
    if (hull_count > max_vertices) {
        Point2D *reduced = (Point2D *)malloc((size_t)hull_count * sizeof(Point2D));
        NT_BUILD_ASSERT(reduced && "binary_build_convex_polygon: alloc failed");
        hull_count = hull_simplify(hull, hull_count, max_vertices, reduced);
        free(hull);
        hull = reduced;
    }

    Point2D *result = (Point2D *)malloc((size_t)hull_count * sizeof(Point2D));
    NT_BUILD_ASSERT(result && "binary_build_convex_polygon: alloc failed");
    memcpy(result, hull, (size_t)hull_count * sizeof(Point2D));
    free(hull);

    *out_count = hull_count;
    return result;
}

/* --- Contour tracing: extract alpha boundary as CCW polygon --- */

/* Traces the outer boundary of opaque pixels on a binary grid.
 * Uses CW edge-following (inside on the right) with right-turn priority.
 * Returns vertex count. Output polygon is CCW (reversed from CW trace). */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static uint32_t trace_contour(const uint8_t *binary, uint32_t tw, uint32_t th, Point2D *out, uint32_t max_out) {
    /* Find topmost-leftmost opaque pixel */
    int32_t sx = -1;
    int32_t sy = -1;
    for (uint32_t y = 0; y < th && sx < 0; y++) {
        for (uint32_t x = 0; x < tw; x++) {
            if (binary[(y * tw) + x]) {
                sx = (int32_t)x;
                sy = (int32_t)y;
                break;
            }
        }
    }
    if (sx < 0) {
        return 0;
    }

/* Pixel lookup macro (out-of-bounds = 0) */
#define BIN_AT(px, py) (((px) >= 0 && (py) >= 0 && (uint32_t)(px) < tw && (uint32_t)(py) < th) ? binary[((uint32_t)(py) * tw) + (uint32_t)(px)] : 0)

    /* Check if an outgoing edge exists in direction d at corner (cx, cy).
     * An edge exists when the two pixels on either side of that edge differ.
     *   d=0 right: TR vs BR   d=1 down: BL vs BR
     *   d=2 left:  TL vs BL   d=3 up:   TL vs TR */
    // clang-format off
#define EDGE_EXISTS(cx, cy, d) ( \
    ((d) == 0) ? (BIN_AT((cx), (cy) - 1) != BIN_AT((cx), (cy))) : \
    ((d) == 1) ? (BIN_AT((cx) - 1, (cy)) != BIN_AT((cx), (cy))) : \
    ((d) == 2) ? (BIN_AT((cx) - 1, (cy) - 1) != BIN_AT((cx) - 1, (cy))) : \
                 (BIN_AT((cx) - 1, (cy) - 1) != BIN_AT((cx), (cy) - 1)))
    // clang-format on

    /* Start at top-left corner of start pixel.
     * The topmost-leftmost opaque pixel guarantees this corner is not a saddle
     * (TL and TR are transparent), so position-only stop check is safe. */
    int32_t cx = sx;
    int32_t cy = sy;
    int dir = 0; /* initial: right (top edge of start pixel is a boundary) */
    uint32_t count = 0;

    /* Record starting corner and take first step */
    out[count++] = (Point2D){cx, cy};

    /* Pick direction and step — inline to keep the loop compact */
    int r_ = (dir + 1) & 3;
    int s_ = dir;
    int l_ = (dir + 3) & 3;
    int b_ = (dir + 2) & 3;
    // clang-format off
    if      (EDGE_EXISTS(cx, cy, r_)) dir = r_;
    else if (EDGE_EXISTS(cx, cy, s_)) dir = s_;
    else if (EDGE_EXISTS(cx, cy, l_)) dir = l_;
    else                              dir = b_;
    // clang-format on
    if (dir == 0) {
        cx++;
    } else if (dir == 1) {
        cy++;
    } else if (dir == 2) {
        cx--;
    } else {
        cy--;
    }

    /* Trace until we return to the starting position */
    while (cx != sx || cy != sy) {
        NT_BUILD_ASSERT(count < max_out && "trace_contour: exceeded max vertices");
        out[count++] = (Point2D){cx, cy};
        r_ = (dir + 1) & 3;
        s_ = dir;
        l_ = (dir + 3) & 3;
        b_ = (dir + 2) & 3;
        // clang-format off
        if      (EDGE_EXISTS(cx, cy, r_)) dir = r_;
        else if (EDGE_EXISTS(cx, cy, s_)) dir = s_;
        else if (EDGE_EXISTS(cx, cy, l_)) dir = l_;
        else                              dir = b_;
        // clang-format on
        if (dir == 0) {
            cx++;
        } else if (dir == 1) {
            cy++;
        } else if (dir == 2) {
            cx--;
        } else {
            cy--;
        }
    }

#undef EDGE_EXISTS
#undef BIN_AT

    /* Reverse to CCW (trace was CW) */
    for (uint32_t i = 0; i < count / 2; i++) {
        Point2D tmp = out[i];
        out[i] = out[count - 1 - i];
        out[count - 1 - i] = tmp;
    }

    return count;
}

/* --- Remove collinear vertices from a polygon --- */

static uint32_t remove_collinear(const Point2D *in, uint32_t n, Point2D *out) {
    if (n < 3) {
        memcpy(out, in, n * sizeof(Point2D));
        return n;
    }
    uint32_t count = 0;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t prev = (i + n - 1) % n;
        uint32_t next = (i + 1) % n;
        int64_t cross = cross2d(in[prev], in[i], in[next]);
        if (cross != 0) {
            out[count++] = in[i];
        }
    }
    return (count >= 3) ? count : n; /* keep all if degenerate */
}

/* --- Ramer-Douglas-Peucker simplification for closed polygons --- */

/* Squared perpendicular distance from point p to line segment (a, b). */
static double rdp_dist_sq(Point2D a, Point2D b, Point2D p) {
    double dx = (double)(b.x - a.x);
    double dy = (double)(b.y - a.y);
    double len_sq = (dx * dx) + (dy * dy);
    if (len_sq < 1e-12) {
        double ex = (double)(p.x - a.x);
        double ey = (double)(p.y - a.y);
        return (ex * ex) + (ey * ey);
    }
    double cross = (dx * (double)(p.y - a.y)) - (dy * (double)(p.x - a.x));
    return (cross * cross) / len_sq;
}

/* Recursive RDP on an open sub-range [start..end] of a polygon. */
static void rdp_recurse(const Point2D *pts, bool *keep, uint32_t start, uint32_t end, double eps_sq) {
    if (end <= start + 1) {
        return;
    }
    double max_dist = 0.0;
    uint32_t max_idx = start;
    for (uint32_t i = start + 1; i < end; i++) {
        double d = rdp_dist_sq(pts[start], pts[end], pts[i]);
        if (d > max_dist) {
            max_dist = d;
            max_idx = i;
        }
    }
    if (max_dist > eps_sq) {
        keep[max_idx] = true;
        rdp_recurse(pts, keep, start, max_idx, eps_sq);
        rdp_recurse(pts, keep, max_idx, end, eps_sq);
    }
}

/* RDP simplification for a closed polygon. epsilon controls max deviation in pixels.
 * Output polygon has at least 3 vertices. Returns vertex count. */
static uint32_t rdp_simplify(const Point2D *poly, uint32_t n, double epsilon, Point2D *out) {
    if (n <= 3) {
        memcpy(out, poly, n * sizeof(Point2D));
        return n;
    }
    double eps_sq = epsilon * epsilon;

    /* Find the two most distant vertices to use as split points */
    uint32_t i0 = 0;
    uint32_t i1 = 0;
    int64_t max_dist_sq = 0;
    for (uint32_t i = 0; i < n; i++) {
        for (uint32_t j = i + 1; j < n; j++) {
            int64_t dx = (int64_t)(poly[j].x - poly[i].x);
            int64_t dy = (int64_t)(poly[j].y - poly[i].y);
            int64_t d = (dx * dx) + (dy * dy);
            if (d > max_dist_sq) {
                max_dist_sq = d;
                i0 = i;
                i1 = j;
            }
        }
    }

    /* Linearize closed polygon into two open chains: i0→i1 and i1→i0 (wrapping) */
    uint32_t chain_len = n + 1; /* worst case: entire polygon + wrap */
    Point2D *chain = (Point2D *)malloc(chain_len * sizeof(Point2D));
    bool *keep = (bool *)calloc(chain_len, sizeof(bool));
    NT_BUILD_ASSERT(chain && keep && "rdp_simplify: alloc failed");

    /* Chain A: i0 → i1 */
    uint32_t a_len = 0;
    uint32_t *a_map = (uint32_t *)malloc(chain_len * sizeof(uint32_t));
    NT_BUILD_ASSERT(a_map && "rdp_simplify: alloc failed");
    for (uint32_t i = i0;; i = (i + 1) % n) {
        a_map[a_len] = i;
        chain[a_len] = poly[i];
        a_len++;
        if (i == i1) {
            break;
        }
    }

    bool *keep_a = (bool *)calloc(a_len, sizeof(bool));
    NT_BUILD_ASSERT(keep_a && "rdp_simplify: alloc failed");
    keep_a[0] = true;
    keep_a[a_len - 1] = true;
    rdp_recurse(chain, keep_a, 0, a_len - 1, eps_sq);

    /* Chain B: i1 → i0 (wrapping) */
    uint32_t b_len = 0;
    uint32_t *b_map = (uint32_t *)malloc(chain_len * sizeof(uint32_t));
    NT_BUILD_ASSERT(b_map && "rdp_simplify: alloc failed");
    for (uint32_t i = i1;; i = (i + 1) % n) {
        b_map[b_len] = i;
        chain[b_len] = poly[i];
        b_len++;
        if (i == i0) {
            break;
        }
    }

    bool *keep_b = (bool *)calloc(b_len, sizeof(bool));
    NT_BUILD_ASSERT(keep_b && "rdp_simplify: alloc failed");
    keep_b[0] = true;
    keep_b[b_len - 1] = true;
    rdp_recurse(chain, keep_b, 0, b_len - 1, eps_sq);

    /* Merge: mark kept vertices in original polygon */
    bool *keep_final = (bool *)calloc(n, sizeof(bool));
    NT_BUILD_ASSERT(keep_final && "rdp_simplify: alloc failed");
    for (uint32_t i = 0; i < a_len; i++) {
        if (keep_a[i]) {
            keep_final[a_map[i]] = true;
        }
    }
    for (uint32_t i = 0; i < b_len; i++) {
        if (keep_b[i]) {
            keep_final[b_map[i]] = true;
        }
    }

    /* Collect kept vertices in order */
    uint32_t count = 0;
    for (uint32_t i = 0; i < n; i++) {
        if (keep_final[i]) {
            out[count++] = poly[i];
        }
    }

    free(keep_final);
    free(keep_b);
    free(b_map);
    free(keep_a);
    free(a_map);
    free(keep);
    free(chain);

    return (count >= 3) ? count : n;
}

/* --- Polygon inflation via Clipper2 (handles concave corners correctly) --- */

/* Inflates a polygon by 'amount' pixels. Output buffer 'out' must hold at least
 * max(n, 32) Point2D entries — Clipper2 may add vertices at concave splits,
 * but we cap the result to fit downstream stack arrays. */
static uint32_t polygon_inflate(const Point2D *hull, uint32_t n, float amount, Point2D *out) {
    if (n < 3 || amount <= 0.0F) {
        memcpy(out, hull, n * sizeof(Point2D));
        return n;
    }
    /* Convert Point2D → flat xy array for bridge */
    int32_t stack_xy[64];
    int32_t *xy = (n <= 32) ? stack_xy : (int32_t *)malloc(n * 2 * sizeof(int32_t));
    NT_BUILD_ASSERT(xy && "polygon_inflate: alloc failed");
    for (uint32_t i = 0; i < n; i++) {
        xy[i * 2] = hull[i].x;
        xy[i * 2 + 1] = hull[i].y;
    }
    int32_t *inflated_xy = NULL;
    uint32_t out_count = nt_clipper2_inflate(xy, n, (double)amount, &inflated_xy);
    if (xy != stack_xy) {
        free(xy);
    }
    if (out_count == 0 || !inflated_xy) {
        /* Clipper2 failed (e.g. polygon collapsed on deflate) — copy unchanged */
        free(inflated_xy);
        memcpy(out, hull, n * sizeof(Point2D));
        return n;
    }
    /* Cap output at 32 vertices — downstream uses stack arrays of this size.
     * If Clipper2 returned more (rare), the caller's out buffer must be sized accordingly. */
    if (out_count > 32) {
        out_count = 32;
    }
    /* Sanity check: inflated coordinates must be within reasonable bounds (~input range + amount).
     * If Clipper2 returned something weird, fall back to the input. */
    int32_t in_min_x = hull[0].x, in_max_x = hull[0].x;
    int32_t in_min_y = hull[0].y, in_max_y = hull[0].y;
    for (uint32_t i = 1; i < n; i++) {
        if (hull[i].x < in_min_x)
            in_min_x = hull[i].x;
        if (hull[i].x > in_max_x)
            in_max_x = hull[i].x;
        if (hull[i].y < in_min_y)
            in_min_y = hull[i].y;
        if (hull[i].y > in_max_y)
            in_max_y = hull[i].y;
    }
    int32_t margin = (int32_t)(amount * 4.0F) + 16;
    bool sane = true;
    for (uint32_t i = 0; i < out_count; i++) {
        int32_t x = (int32_t)inflated_xy[i * 2];
        int32_t y = (int32_t)inflated_xy[i * 2 + 1];
        if (x < in_min_x - margin || x > in_max_x + margin || y < in_min_y - margin || y > in_max_y + margin) {
            sane = false;
            break;
        }
    }
    if (!sane) {
        NT_LOG_WARN("polygon_inflate: Clipper2 returned out-of-bounds vertices, using input unchanged");
        free(inflated_xy);
        memcpy(out, hull, n * sizeof(Point2D));
        return n;
    }
    for (uint32_t i = 0; i < out_count; i++) {
        out[i].x = inflated_xy[i * 2];
        out[i].y = inflated_xy[i * 2 + 1];
    }
    free(inflated_xy);
    return out_count;
}

/* 3-bit transform flags (dihedral group D4 — all 8 symmetries of a rectangle).
 *   bit 0 (1): flip horizontal
 *   bit 1 (2): flip vertical
 *   bit 2 (4): diagonal flip (swap x,y — applied first)
 * Apply order: diagonal → flip H → flip V.
 *
 * Mapping from old rotation values:
 *   rot=0 → flags=0 (identity)        rot=1 (90°CW)  → flags=5
 *   rot=2 (180°) → flags=3            rot=3 (270°CW) → flags=6
 * New transforms: 1=flipH, 2=flipV, 4=diagonal, 7=anti-diagonal
 *
 * Dimension swap: if (flags & 4) then output dims are (th, tw), else (tw, th). */

static inline void transform_point(int32_t sx, int32_t sy, uint8_t flags, int32_t tw, int32_t th, int32_t *ox, int32_t *oy) {
    int32_t x = sx;
    int32_t y = sy;
    if (flags & 4) {
        int32_t t = x;
        x = y;
        y = t;
    }
    int32_t w = (flags & 4) ? th : tw;
    int32_t h = (flags & 4) ? tw : th;
    if (flags & 1) {
        x = w - 1 - x;
    }
    if (flags & 2) {
        y = h - 1 - y;
    }
    *ox = x;
    *oy = y;
}

/* Transform polygon vertices using 3-bit flags. tw/th = original trimmed sprite dims.
 * If the transform reverses winding (odd popcount), vertices are reversed to restore CCW. */
static void polygon_transform(const Point2D *src, uint32_t n, uint8_t flags, int32_t tw, int32_t th, Point2D *out) {
    for (uint32_t i = 0; i < n; i++) {
        transform_point(src[i].x, src[i].y, flags, tw, th, &out[i].x, &out[i].y);
    }
    /* Odd number of reflections reverses winding — reverse vertex order to restore CCW */
    uint32_t parity = (uint32_t)((flags & 1) + ((flags >> 1) & 1) + ((flags >> 2) & 1));
    if ((parity & 1) && n > 1) {
        for (uint32_t i = 0; i < n / 2; i++) {
            Point2D tmp = out[i];
            out[i] = out[n - 1 - i];
            out[n - 1 - i] = tmp;
        }
    }
}
// #endregion

// #region Atlas types — page limits and placement record

/* Maximum number of atlas pages (each page = one texture) */
#ifndef ATLAS_MAX_PAGES
#define ATLAS_MAX_PAGES 64
#endif

/* Sprite placement result after packing.
 * rotation field is a 3-bit D4 transform mask (bit0=flipH, bit1=flipV, bit2=diagonal). */
typedef struct {
    uint32_t sprite_index; /* index into original sprite array */
    uint32_t page;         /* which atlas page (0-based) */
    uint32_t x, y;         /* placement position in atlas (top-left of cell including extrude) */
    uint32_t trimmed_w;    /* trimmed sprite width */
    uint32_t trimmed_h;    /* trimmed sprite height */
    uint32_t trim_x;       /* trim offset from source image left */
    uint32_t trim_y;       /* trim offset from source image top */
    uint8_t rotation;      /* D4 transform flags */
} AtlasPlacement;
// #endregion

// #region Pack stats — counters shared by packers

/* Per-call packing statistics (thread-safe: no static globals) */
typedef struct {
    uint64_t or_count;
    uint64_t test_count;
    uint64_t yskip_count;
    uint64_t used_area;
    uint64_t frontier_area;
    uint64_t trim_area;
    uint64_t poly_area;
    uint64_t page_scan_count;
    uint64_t page_prune_count;
    uint64_t page_existing_hit_count;
    uint64_t page_backfill_count;
    uint64_t page_new_count;
    uint64_t relevant_count;
    uint64_t candidate_count;
    uint64_t grid_fallback_count;
    uint64_t nfp_cache_hit_count;
    uint64_t nfp_cache_miss_count;
    uint64_t nfp_cache_collision_count;
    uint64_t orient_dedup_saved_count;
    uint64_t dirty_cell_count;
} PackStats;

static uint64_t polygon_area_pixels(const Point2D *poly, uint32_t count) {
    if (count < 3) {
        return 0;
    }

    int64_t twice_area = 0;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t j = (i + 1 == count) ? 0 : i + 1;
        twice_area += ((int64_t)poly[i].x * (int64_t)poly[j].y) - ((int64_t)poly[j].x * (int64_t)poly[i].y);
    }
    if (twice_area < 0) {
        twice_area = -twice_area;
    }

    return ((uint64_t)twice_area + 1ULL) >> 1U;
}

static void pack_stats_measure_payload(PackStats *stats, const uint32_t *trim_w, const uint32_t *trim_h, Point2D **hull_verts, const uint32_t *hull_counts, uint32_t sprite_count,
                                       const nt_atlas_opts_t *opts) {
    float dilate = (float)opts->extrude + ((float)opts->padding * 0.5F);

    stats->trim_area = 0;
    stats->poly_area = 0;

    for (uint32_t i = 0; i < sprite_count; i++) {
        stats->trim_area += (uint64_t)trim_w[i] * (uint64_t)trim_h[i];

        if (!hull_verts[i] || hull_counts[i] == 0) {
            continue;
        }

        Point2D inflated[32];
        uint32_t inflated_count = hull_counts[i];
        if (dilate > 0.0F) {
            inflated_count = polygon_inflate(hull_verts[i], hull_counts[i], dilate, inflated);
        } else {
            memcpy(inflated, hull_verts[i], hull_counts[i] * sizeof(Point2D));
        }
        stats->poly_area += polygon_area_pixels(inflated, inflated_count);
    }
}

/* Case-insensitive ASCII string compare for env-var truthy check. */
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
// #endregion

// #region Vector packing — NFP/Minkowski packer (vector_pack)

/* Sort entry for area-descending sort (ATLAS-03) */
typedef struct {
    uint32_t index; /* index into sprites[] */
    uint32_t area;  /* trimmed_w * trimmed_h */
} AreaSortEntry;

/* Area-descending qsort comparator (ATLAS-03) */
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
    if (ea->index < eb->index) {
        return -1;
    }
    if (ea->index > eb->index) {
        return 1;
    }
    return 0;
}

#include "tinycthread.h"
/* nt_builder_vector.inl uses tinycthread declarations above. */
#include "nt_builder_vector.inl"

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
            int32_t tx;
            int32_t ty;
            transform_point((int32_t)sx, (int32_t)sy, rotation, (int32_t)trim_w, (int32_t)trim_h, &tx, &ty);
            uint32_t dx = dest_x + (uint32_t)tx;
            uint32_t dy = dest_y + (uint32_t)ty;
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
     * sw, sh = trimmed sprite dimensions.
     *
     * Skip-transparent on source pixel: only duplicate edge pixels that have
     * alpha > 0. Otherwise we'd write transparent into the surrounding area
     * and could overwrite opaque pixels of a neighboring sprite that has been
     * placed nearby (polygon packing allows tighter neighbor placement). */

    // #region Top and bottom edge extrusion
    for (uint32_t e = 1; e <= extrude_count; e++) {
        /* Top edge: duplicate row py to row py - e */
        if (py >= e) {
            uint32_t dst_y = py - e;
            for (uint32_t x = px; x < px + sw && x < page_w; x++) {
                const uint8_t *src_pix = &page[((size_t)py * page_w + x) * 4];
                if (src_pix[3] != 0) {
                    memcpy(&page[((size_t)dst_y * page_w + x) * 4], src_pix, 4);
                }
            }
        }
        /* Bottom edge: duplicate row py+sh-1 to row py+sh-1+e */
        uint32_t src_y = py + sh - 1;
        uint32_t dst_y = src_y + e;
        if (dst_y < page_h) {
            for (uint32_t x = px; x < px + sw && x < page_w; x++) {
                const uint8_t *src_pix = &page[((size_t)src_y * page_w + x) * 4];
                if (src_pix[3] != 0) {
                    memcpy(&page[((size_t)dst_y * page_w + x) * 4], src_pix, 4);
                }
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
                const uint8_t *src_pix = &page[((size_t)y * page_w + px) * 4];
                if (src_pix[3] != 0) {
                    memcpy(&page[((size_t)y * page_w + dst_x) * 4], src_pix, 4);
                }
            }
        }
        /* Right edge */
        uint32_t src_x = px + sw - 1;
        uint32_t dst_x = src_x + e;
        if (dst_x < page_w) {
            for (uint32_t y = y_start; y < y_end; y++) {
                const uint8_t *src_pix = &page[((size_t)y * page_w + src_x) * 4];
                if (src_pix[3] != 0) {
                    memcpy(&page[((size_t)y * page_w + dst_x) * 4], src_pix, 4);
                }
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
        int32_t tx0;
        int32_t ty0;
        int32_t tx1;
        int32_t ty1;
        transform_point(hull[i].x, hull[i].y, rotation, (int32_t)trim_w, (int32_t)trim_h, &tx0, &ty0);
        transform_point(hull[next].x, hull[next].y, rotation, (int32_t)trim_w, (int32_t)trim_h, &tx1, &ty1);
        int32_t ax0 = (int32_t)inner_x + tx0;
        int32_t ay0 = (int32_t)inner_y + ty0;
        int32_t ax1 = (int32_t)inner_x + tx1;
        int32_t ay1 = (int32_t)inner_y + ty1;
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
    enum { ATLAS_CACHE_KEY_VERSION = 3 };

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
    uint8_t flags = (uint8_t)((opts->allow_rotate ? 1 : 0) | (opts->power_of_two ? 2 : 0) | (opts->polygon_mode ? 4 : 0) | (opts->debug_png ? 8 : 0));
    opts_buf[pos++] = flags;
    opts_buf[pos++] = (uint8_t)ATLAS_CACHE_KEY_VERSION;
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
uint32_t nt_atlas_test_ear_clip(const void *poly, uint32_t n, uint16_t *idx) { return ear_clip_triangulate((const Point2D *)poly, n, idx); }
uint32_t nt_atlas_test_trace_contour(const uint8_t *bin, uint32_t tw, uint32_t th, void *out, uint32_t max_out) { return trace_contour(bin, tw, th, (Point2D *)out, max_out); }
uint32_t nt_atlas_test_remove_collinear(const void *in, uint32_t n, void *out) { return remove_collinear((const Point2D *)in, n, (Point2D *)out); }
bool nt_atlas_test_point_in_polygon(const void *poly, uint32_t n, int32_t px, int32_t py) { return point_in_polygon((const Point2D *)poly, n, (Point2D){px, py}); }
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
    NT_BUILD_ASSERT(state->opts.max_vertices <= 16 && "begin_atlas: max_vertices must be <= 16 (NFP buffer limit: nA+nB <= 32)");

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
    /* Pre-triangulated mesh (NULL for normal polygon sprites; non-NULL for
     * multi-component sprites where the "hull" is a triangle soup, not an outline). */
    uint16_t **pre_tri_indices;
    uint32_t *pre_tri_counts;

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
    uint32_t thread_count;
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

/* --- pipeline_geometry: contour trace + simplification + inflation per unique sprite --- */

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void pipeline_geometry(AtlasPipeline *p) {
    p->vertex_counts = (uint32_t *)calloc(p->sprite_count, sizeof(uint32_t));
    p->hull_vertices = (Point2D **)calloc(p->sprite_count, sizeof(Point2D *));
    p->pre_tri_indices = (uint16_t **)calloc(p->sprite_count, sizeof(uint16_t *));
    p->pre_tri_counts = (uint32_t *)calloc(p->sprite_count, sizeof(uint32_t));
    NT_BUILD_ASSERT(p->vertex_counts && p->hull_vertices && p->pre_tri_indices && p->pre_tri_counts && "pipeline_geometry: alloc failed");

    for (uint32_t ui = 0; ui < p->unique_count; ui++) {
        uint32_t idx = p->unique_indices[ui];
        uint32_t tw = p->trim_w[idx];
        uint32_t th = p->trim_h[idx];

        if (p->opts->polygon_mode) {
            const uint8_t *ap = p->alpha_planes[idx];
            uint32_t aw = p->sprites[idx].width;

            /* Build binary mask for trimmed region */
            uint8_t *binary = (uint8_t *)calloc((size_t)tw * th, 1);
            NT_BUILD_ASSERT(binary && "pipeline_geometry: alloc failed");
            for (uint32_t y = 0; y < th; y++) {
                for (uint32_t x = 0; x < tw; x++) {
                    uint8_t a = ap[((size_t)(p->trim_y[idx] + y) * aw) + p->trim_x[idx] + x];
                    if (a >= p->opts->alpha_threshold) {
                        binary[((size_t)y * tw) + x] = 1;
                    }
                }
            }

            // #region Morphological closing — merge disjoint components into one
            /* If sprite has multiple disjoint opaque regions, iteratively dilate the
             * binary mask until they form one connected component, so a single contour
             * trace can produce one simple polygon containing all pixels.
             * The resulting polygon will be K pixels wider on every side (acceptable —
             * acts like extra padding). Limit K to avoid pathological cases. */
            uint8_t *binary_source = (uint8_t *)malloc((size_t)tw * th);
            NT_BUILD_ASSERT(binary_source && "pipeline_geometry: alloc failed");
            memcpy(binary_source, binary, (size_t)tw * th);
            const char *convex_reason = NULL;
            uint8_t *cc_visited = (uint8_t *)calloc((size_t)tw * th, 1);
            int32_t *cc_stack = (int32_t *)malloc((size_t)tw * th * 2 * sizeof(int32_t));
            NT_BUILD_ASSERT(cc_visited && cc_stack && "pipeline_geometry: alloc failed");
            uint32_t comp_count = binary_count_components(binary, tw, th, cc_visited, cc_stack);
            uint32_t closing_k = 0;
            if (comp_count > 1) {
                uint8_t *scratch = (uint8_t *)malloc((size_t)tw * th);
                NT_BUILD_ASSERT(scratch && "pipeline_geometry: alloc failed");
                uint32_t max_iter = (tw > th ? tw : th) / 2 + 1;
                while (comp_count > 1 && closing_k < max_iter) {
                    binary_dilate_4conn(binary, scratch, tw, th);
                    memcpy(binary, scratch, (size_t)tw * th);
                    closing_k++;
                    comp_count = binary_count_components(binary, tw, th, cc_visited, cc_stack);
                }
                free(scratch);
                if (comp_count > 1) {
                    convex_reason = "disjoint components";
                }
            }
            free(cc_stack);
            free(cc_visited);
            // #endregion

            if (!convex_reason) {
                /* Trace outer contour (CCW). Worst-case contour length is
                 * 2*(tw*th) for maximally jagged shapes (checkerboard). */
                uint32_t max_contour = 2 * tw * th + 4;
                Point2D *contour = (Point2D *)malloc(max_contour * sizeof(Point2D));
                NT_BUILD_ASSERT(contour && "pipeline_geometry: alloc failed");
                uint32_t contour_count = trace_contour(binary, tw, th, contour, max_contour);

                if (contour_count < 3) {
                    free(contour);
                    convex_reason = "degenerate contour";
                } else {
                    /* Remove collinear vertices */
                    Point2D *clean = (Point2D *)malloc(contour_count * sizeof(Point2D));
                    NT_BUILD_ASSERT(clean && "pipeline_geometry: alloc failed");
                    uint32_t clean_count = remove_collinear(contour, contour_count, clean);
                    free(contour);

                    /* Simplify with RDP. Start with small epsilon for coverage, grow if needed.
                     * The Clipper2 inflate amount below MUST be >= RDP epsilon to guarantee
                     * the inflated polygon covers all pixels the RDP might have cut. */
                    Point2D *simplified = (Point2D *)malloc(clean_count * sizeof(Point2D));
                    NT_BUILD_ASSERT(simplified && "pipeline_geometry: alloc failed");
                    double eps = 0.5;
                    uint32_t simp_count = rdp_simplify(clean, clean_count, eps, simplified);
                    uint32_t target = p->opts->max_vertices;
                    double prev_eps = -1.0;
                    while (simp_count > target && eps < 100.0) {
                        prev_eps = eps;
                        eps *= 1.5;
                        simp_count = rdp_simplify(clean, clean_count, eps, simplified);
                    }
                    /* Bisect [prev_eps, eps] for smallest eps that fits target. */
                    if (simp_count <= target && prev_eps > 0.0 && (eps - prev_eps) > 0.5) {
                        double lo = prev_eps;
                        double hi = eps;
                        for (int bs = 0; bs < 12; bs++) {
                            double mid = (lo + hi) * 0.5;
                            uint32_t mid_count = rdp_simplify(clean, clean_count, mid, simplified);
                            if (mid_count <= target) {
                                hi = mid;
                            } else {
                                lo = mid;
                            }
                        }
                        eps = hi;
                        simp_count = rdp_simplify(clean, clean_count, eps, simplified);
                    }
                    double inflate_amt = eps + 1.0;
                    /* Multi-strategy: try several simplification algorithms, keep the one
                     * that produces the smallest final inflated polygon area. */

                    /* Helper to compute final inflated polygon area estimate. */
                    double best_perim = 0.0;
                    {
                        for (uint32_t v = 0; v < simp_count; v++) {
                            uint32_t vn = (v + 1) % simp_count;
                            double dx = (double)(simplified[vn].x - simplified[v].x);
                            double dy = (double)(simplified[vn].y - simplified[v].y);
                            best_perim += sqrt((dx * dx) + (dy * dy));
                        }
                    }
                    double best_est = (double)polygon_area_pixels(simplified, simp_count) + (best_perim * inflate_amt) + (3.14159 * inflate_amt * inflate_amt);

                    /* Strategy 2: greedy perpendicular-distance simplification, exactly target verts. */
                    {
                        Point2D *alt = (Point2D *)malloc(clean_count * sizeof(Point2D));
                        NT_BUILD_ASSERT(alt && "pipeline_geometry: alloc failed");
                        double dummy_dev = 0.0;
                        uint32_t alt_count = hull_simplify_perp(clean, clean_count, target, alt, &dummy_dev);
                        /* True inflate = max distance from any OUTSIDE opaque pixel center
                         * to the candidate polygon's boundary. Testing pixel centers (not
                         * clean vertices) catches cases where the simplified polygon cuts
                         * between two in-polygon vertices. */
                        double max_outside_dist = polygon_max_outside_pixel_distance(alt, alt_count, binary_source, tw, th);
                        double alt_inflate = max_outside_dist + 1.0;
                        double alt_perim = 0.0;
                        for (uint32_t v = 0; v < alt_count; v++) {
                            uint32_t vn = (v + 1) % alt_count;
                            double dx = (double)(alt[vn].x - alt[v].x);
                            double dy = (double)(alt[vn].y - alt[v].y);
                            alt_perim += sqrt((dx * dx) + (dy * dy));
                        }
                        double alt_est = (double)polygon_area_pixels(alt, alt_count) + (alt_perim * alt_inflate) + (3.14159 * alt_inflate * alt_inflate);
                        if (alt_est < best_est) {
                            free(simplified);
                            simplified = alt;
                            simp_count = alt_count;
                            inflate_amt = alt_inflate;
                            best_est = alt_est;
                        } else {
                            free(alt);
                        }
                    }

                    /* Strategy 3a: trim bounding rectangle. Always 4 vertices, trivially
                     * contains all alpha pixels (since they live inside the trim bbox by
                     * definition). For nearly-rectangular alphas (muzzle, tiles) this is
                     * the tightest possible polygon. */
                    {
                        Point2D rect[4] = {
                            {0, 0},
                            {(int32_t)tw, 0},
                            {(int32_t)tw, (int32_t)th},
                            {0, (int32_t)th},
                        };
                        double rect_area = (double)tw * (double)th;
                        double rect_perim = 2.0 * ((double)tw + (double)th);
                        double rect_inflate = 1.0;
                        double rect_est = rect_area + (rect_perim * rect_inflate) + (3.14159 * rect_inflate * rect_inflate);
                        if (rect_est < best_est) {
                            free(simplified);
                            simplified = (Point2D *)malloc(4 * sizeof(Point2D));
                            NT_BUILD_ASSERT(simplified && "pipeline_geometry: alloc failed");
                            memcpy(simplified, rect, 4 * sizeof(Point2D));
                            simp_count = 4;
                            inflate_amt = rect_inflate;
                            best_est = rect_est;
                        }
                    }

                    /* Strategy 3: convex hull of binary mask. For mostly-rectangular alphas
                     * (muzzle, icons, tiles) this is ~4 vertices ≈ trim bbox, zero inflate. */
                    {
                        uint32_t hull_count = 0;
                        Point2D *hull = binary_build_convex_polygon(binary_source, tw, th, target, &hull_count);
                        if (hull && hull_count >= 3) {
                            double max_outside_dist = polygon_max_outside_pixel_distance(hull, hull_count, binary_source, tw, th);
                            double hull_inflate = max_outside_dist + 1.0;
                            double hull_perim = 0.0;
                            for (uint32_t v = 0; v < hull_count; v++) {
                                uint32_t vn = (v + 1) % hull_count;
                                double dx = (double)(hull[vn].x - hull[v].x);
                                double dy = (double)(hull[vn].y - hull[v].y);
                                hull_perim += sqrt((dx * dx) + (dy * dy));
                            }
                            double hull_est = (double)polygon_area_pixels(hull, hull_count) + (hull_perim * hull_inflate) + (3.14159 * hull_inflate * hull_inflate);
                            if (hull_est < best_est) {
                                free(simplified);
                                simplified = hull;
                                simp_count = hull_count;
                                inflate_amt = hull_inflate;
                                best_est = hull_est;
                            } else {
                                free(hull);
                            }
                        } else if (hull) {
                            free(hull);
                        }
                    }

                    free(clean);
                    int32_t *simp_xy = (int32_t *)malloc(simp_count * 2 * sizeof(int32_t));
                    NT_BUILD_ASSERT(simp_xy && "pipeline_geometry: alloc failed");
                    for (uint32_t v = 0; v < simp_count; v++) {
                        simp_xy[v * 2] = simplified[v].x;
                        simp_xy[v * 2 + 1] = simplified[v].y;
                    }
                    free(simplified);

                    int32_t *inflated_xy = NULL;
                    uint32_t inf_count = nt_clipper2_inflate(simp_xy, simp_count, inflate_amt, &inflated_xy);
                    free(simp_xy);

                    /* Trust Clipper2 — only fail on obvious degenerate output (too few vertices). */
                    bool sane_result = (inf_count >= 3 && inflated_xy != NULL);

                    if (sane_result) {
                        /* If Clipper2 produced too many vertices (edge splits at concave corners),
                         * apply RDP again to get under max_vertices. */
                        Point2D *result = (Point2D *)malloc(inf_count * sizeof(Point2D));
                        NT_BUILD_ASSERT(result && "pipeline_geometry: alloc failed");
                        for (uint32_t v = 0; v < inf_count; v++) {
                            result[v].x = inflated_xy[v * 2];
                            result[v].y = inflated_xy[v * 2 + 1];
                        }
                        free(inflated_xy);

                        uint32_t final_target = p->opts->max_vertices;
                        if (inf_count > final_target) {
                            Point2D *reduced = (Point2D *)malloc(inf_count * sizeof(Point2D));
                            NT_BUILD_ASSERT(reduced && "pipeline_geometry: alloc failed");
                            double eps2 = 1.0;
                            uint32_t red_count = rdp_simplify(result, inf_count, eps2, reduced);
                            while (red_count > final_target && eps2 < 100.0) {
                                eps2 *= 1.5;
                                red_count = rdp_simplify(result, inf_count, eps2, reduced);
                            }
                            free(result);
                            result = reduced;
                            inf_count = red_count;
                        }
                        if (inf_count <= p->opts->max_vertices) {
                            /* Post-verify: every opaque pixel center must lie inside the
                             * final polygon. Secondary RDP can cut vertices and shrink the
                             * polygon; if that leaves any opaque pixel outside, fall back
                             * to the trim bounding rectangle (guaranteed correct). */
                            double post_max = polygon_max_outside_pixel_distance(result, inf_count, binary_source, tw, th);
                            if (post_max <= 0.0) {
                                p->hull_vertices[idx] = result;
                                p->vertex_counts[idx] = inf_count;
                            } else {
                                /* Polygon lost pixels — fall back to trim bbox (4 verts,
                                 * trivially contains everything). */
                                free(result);
                                p->hull_vertices[idx] = (Point2D *)malloc(4 * sizeof(Point2D));
                                NT_BUILD_ASSERT(p->hull_vertices[idx] && "pipeline_geometry: alloc failed");
                                p->hull_vertices[idx][0] = (Point2D){0, 0};
                                p->hull_vertices[idx][1] = (Point2D){(int32_t)tw, 0};
                                p->hull_vertices[idx][2] = (Point2D){(int32_t)tw, (int32_t)th};
                                p->hull_vertices[idx][3] = (Point2D){0, (int32_t)th};
                                p->vertex_counts[idx] = 4;
                            }
                        } else {
                            free(result);
                            convex_reason = "inflate simplification exceeded max_vertices";
                        }
                    } else {
                        /* Clipper2 inflate failed — fallback to rect */
                        free(inflated_xy);
                        convex_reason = "Clipper2 inflate failed";
                    }
                }
            }
            if (convex_reason) {
                NT_LOG_WARN("pipeline_geometry: sprite '%s' using convex fallback (%s)", p->sprites[idx].name, convex_reason);
                p->hull_vertices[idx] = binary_build_convex_polygon(binary_source, tw, th, p->opts->max_vertices, &p->vertex_counts[idx]);
            }
            free(binary_source);
            free(binary);
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
            p->pre_tri_indices[i] = p->pre_tri_indices[orig];
            p->pre_tri_counts[i] = p->pre_tri_counts[orig];
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

    NT_LOG_INFO("  vector_pack: %u sprites (NFP mode)", p->unique_count);
    p->placement_count = vector_pack(u_trim_w, u_trim_h, u_hulls, u_hull_counts, p->unique_count, p->opts, p->placements, &p->page_count, p->page_w, p->page_h, &p->stats, p->thread_count);
    pack_stats_measure_payload(&p->stats, u_trim_w, u_trim_h, u_hulls, u_hull_counts, p->unique_count, p->opts);

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

        uint32_t blit_w = (pl->rotation & 4) ? pl->trimmed_h : pl->trimmed_w;
        uint32_t blit_h = (pl->rotation & 4) ? pl->trimmed_w : pl->trimmed_h;
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
                uint32_t rw = (p->placements[pi].rotation & 4) ? p->placements[pi].trimmed_h : p->placements[pi].trimmed_w;
                uint32_t rh = (p->placements[pi].rotation & 4) ? p->placements[pi].trimmed_w : p->placements[pi].trimmed_h;
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
    const uint32_t max_region_tri_count = (uint32_t)(UINT8_MAX / 3U);

    /* Count total vertices and indices for UNIQUE sprites only.
     * Duplicates are sprites with identical pixel data — they share placement with
     * their original (occupying the same atlas position), so they share vertex_start
     * and index_start in the blob. This both saves space and fits big atlases (4812+
     * sprites) within uint16 vertex/index_start limits.
     * Pre-triangulated sprites (multi-component) have an exact triangle count;
     * single-component polygons use fan/ear-clip triangulation = (n - 2) triangles.
     * region->index_count is uint8_t (max 255) → cap triangles per region at 85. */
    uint32_t total_vertex_count = 0;
    uint32_t total_index_count = 0;
    for (uint32_t i = 0; i < p->sprite_count; i++) {
        NT_BUILD_ASSERT(p->vertex_counts[i] <= UINT8_MAX && "pipeline_serialize: region vertex_count exceeds uint8_t");
        if (p->dedup_map[i] >= 0) {
            continue; /* Duplicate — its vertex/index storage is shared with the original */
        }
        total_vertex_count += p->vertex_counts[i];
        uint32_t tri = 0;
        if (p->pre_tri_indices[i] && p->pre_tri_counts[i] > 0) {
            tri = p->pre_tri_counts[i];
        } else if (p->vertex_counts[i] >= 3) {
            tri = p->vertex_counts[i] - 2;
        }
        NT_BUILD_ASSERT(tri <= max_region_tri_count && "pipeline_serialize: region index_count exceeds uint8_t");
        total_index_count += tri * 3;
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

    /* Serialize blob: header + texture_resource_ids + regions + vertices + indices */
    uint32_t regions_offset = (uint32_t)sizeof(NtAtlasHeader) + (p->page_count * (uint32_t)sizeof(uint64_t));
    uint32_t vertex_offset = regions_offset + (p->sprite_count * (uint32_t)sizeof(NtAtlasRegion));
    uint32_t index_offset = vertex_offset + (total_vertex_count * (uint32_t)sizeof(NtAtlasVertex));
    uint32_t blob_size = index_offset + (total_index_count * (uint32_t)sizeof(uint16_t));
    uint8_t *blob = (uint8_t *)calloc(1, blob_size);
    NT_BUILD_ASSERT(blob && "pipeline_serialize: blob alloc failed");

    /* Header */
    NtAtlasHeader *hdr = (NtAtlasHeader *)blob;
    hdr->magic = NT_ATLAS_MAGIC;
    hdr->version = NT_ATLAS_VERSION;
    NT_BUILD_ASSERT(p->sprite_count <= UINT16_MAX && "pipeline_serialize: region_count exceeds uint16_t");
    NT_BUILD_ASSERT(p->page_count <= UINT16_MAX && "pipeline_serialize: page_count exceeds uint16_t");
    hdr->region_count = (uint16_t)p->sprite_count;
    hdr->page_count = (uint16_t)p->page_count;
    hdr->_pad = 0;
    hdr->vertex_offset = vertex_offset;
    hdr->total_vertex_count = total_vertex_count;
    hdr->index_offset = index_offset;
    hdr->total_index_count = total_index_count;

    /* Texture resource IDs (D-05) */
    uint8_t *tex_ids_ptr = blob + sizeof(NtAtlasHeader);
    for (uint32_t pg = 0; pg < p->page_count; pg++) {
        char tex_path[512];
        (void)snprintf(tex_path, sizeof(tex_path), "%s/tex%u", p->state->name, pg);
        uint64_t tid = nt_hash64_str(tex_path).value;
        memcpy(tex_ids_ptr + ((size_t)pg * sizeof(uint64_t)), &tid, sizeof(uint64_t));
    }

    /* Regions + vertices + indices.
     * Two-pass: pass 1 writes vertex/index data only for unique sprites and records
     * their start offsets. Pass 2 fills NtAtlasRegion structures, with duplicates
     * sharing vertex_start/index_start with their original.
     *
     * Sharing is correct because duplicates have identical pixel data and are placed
     * at the SAME atlas position (placement_lookup propagates orig's placement to
     * duplicates), so they have the same atlas_u/v and same local geometry. */
    NtAtlasRegion *regions = (NtAtlasRegion *)(blob + regions_offset);
    NtAtlasVertex *vertices = (NtAtlasVertex *)(blob + vertex_offset);
    uint16_t *indices = (uint16_t *)(blob + index_offset);
    uint32_t vertex_cursor = 0;
    uint32_t index_cursor = 0;

    /* Per-sprite recorded start offsets — populated for originals in pass 1, then
     * propagated from original to duplicates before pass 2. */
    uint32_t *sprite_vertex_start = (uint32_t *)malloc(p->sprite_count * sizeof(uint32_t));
    uint32_t *sprite_index_start = (uint32_t *)malloc(p->sprite_count * sizeof(uint32_t));
    uint32_t *sprite_idx_count = (uint32_t *)malloc(p->sprite_count * sizeof(uint32_t));
    NT_BUILD_ASSERT(sprite_vertex_start && sprite_index_start && sprite_idx_count && "pipeline_serialize: alloc failed");

    /* Pass 1: write vertex/index data only for unique sprites */
    for (uint32_t i = 0; i < p->sprite_count; i++) {
        if (p->dedup_map[i] >= 0) {
            continue; /* duplicate — handled in propagation step */
        }
        uint32_t pi = placement_lookup[i];
        NT_BUILD_ASSERT(pi != UINT32_MAX && "pipeline_serialize: sprite has no placement");
        AtlasPlacement *pl = &p->placements[pi];

        uint16_t local_indices[256];
        const uint16_t *src_indices;
        uint32_t tri_count;
        if (p->pre_tri_indices[i] && p->pre_tri_counts[i] > 0) {
            src_indices = p->pre_tri_indices[i];
            tri_count = p->pre_tri_counts[i];
        } else {
            tri_count = ear_clip_triangulate(p->hull_vertices[i], p->vertex_counts[i], local_indices);
            src_indices = local_indices;
        }
        uint32_t idx_count = tri_count * 3;
        NT_BUILD_ASSERT(idx_count <= UINT8_MAX && "pipeline_serialize: region index_count exceeds uint8_t");
        NT_BUILD_ASSERT(vertex_cursor <= UINT16_MAX && "pipeline_serialize: vertex_start exceeds uint16_t");
        NT_BUILD_ASSERT(index_cursor <= UINT16_MAX && "pipeline_serialize: index_start exceeds uint16_t");

        sprite_vertex_start[i] = vertex_cursor;
        sprite_index_start[i] = index_cursor;
        sprite_idx_count[i] = idx_count;

        /* Write triangle indices (local: 0..vertex_count-1) */
        memcpy(&indices[index_cursor], src_indices, idx_count * sizeof(uint16_t));
        index_cursor += idx_count;

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

            int32_t tx;
            int32_t ty;
            transform_point(lx, ly, pl->rotation, (int32_t)p->trim_w[pl->sprite_index], (int32_t)p->trim_h[pl->sprite_index], &tx, &ty);
            float atlas_px = (float)inner_x + (float)tx;
            float atlas_py = (float)inner_y + (float)ty;

            float tmp_u = ((atlas_px * 65535.0F) / (float)atlas_w) + 0.5F;
            float tmp_v = ((atlas_py * 65535.0F) / (float)atlas_h) + 0.5F;
            if (tmp_u < 0.0F) {
                tmp_u = 0.0F;
            }
            if (tmp_v < 0.0F) {
                tmp_v = 0.0F;
            }
            if (tmp_u > 65535.0F) {
                tmp_u = 65535.0F;
            }
            if (tmp_v > 65535.0F) {
                tmp_v = 65535.0F;
            }
            vtx->atlas_u = (uint16_t)tmp_u;
            vtx->atlas_v = (uint16_t)tmp_v;
        }
    }

    NT_BUILD_ASSERT(vertex_cursor == total_vertex_count && "pipeline_serialize: vertex count mismatch");
    NT_BUILD_ASSERT(index_cursor == total_index_count && "pipeline_serialize: index count mismatch");

    /* Propagate offsets from originals to duplicates */
    for (uint32_t i = 0; i < p->sprite_count; i++) {
        if (p->dedup_map[i] >= 0) {
            uint32_t orig = (uint32_t)p->dedup_map[i];
            sprite_vertex_start[i] = sprite_vertex_start[orig];
            sprite_index_start[i] = sprite_index_start[orig];
            sprite_idx_count[i] = sprite_idx_count[orig];
        }
    }

    /* Pass 2: fill region structures (one per sprite, including duplicates) */
    for (uint32_t i = 0; i < p->sprite_count; i++) {
        uint32_t pi = placement_lookup[i];
        NT_BUILD_ASSERT(pi != UINT32_MAX && "pipeline_serialize: sprite has no placement");
        AtlasPlacement *pl = &p->placements[pi];
        NT_BUILD_ASSERT(pl->page <= UINT8_MAX && "pipeline_serialize: page_index exceeds uint8_t");

        NtAtlasRegion *reg = &regions[i];
        reg->name_hash = nt_hash64_str(p->sprites[i].name).value;
        reg->source_w = (uint16_t)p->sprites[i].width;
        reg->source_h = (uint16_t)p->sprites[i].height;
        reg->trim_offset_x = (int16_t)p->trim_x[i];
        reg->trim_offset_y = (int16_t)p->trim_y[i];
        reg->origin_x = p->sprites[i].origin_x;
        reg->origin_y = p->sprites[i].origin_y;
        reg->vertex_start = (uint16_t)sprite_vertex_start[i];
        reg->vertex_count = (uint8_t)p->vertex_counts[i];
        reg->page_index = (uint8_t)pl->page;
        reg->rotated = pl->rotation;
        reg->index_start = (uint16_t)sprite_index_start[i];
        reg->index_count = (uint8_t)sprite_idx_count[i];
    }

    free(sprite_vertex_start);
    free(sprite_index_start);
    free(sprite_idx_count);

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

    /* Free hull vertices and pre-triangulation (careful: duplicates share pointers) */
    for (uint32_t i = 0; i < p->sprite_count; i++) {
        if (p->dedup_map[i] < 0) {
            free(p->hull_vertices[i]);
            free(p->pre_tri_indices[i]);
        }
        p->hull_vertices[i] = NULL;
        p->pre_tri_indices[i] = NULL;
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
    free((void *)p->pre_tri_indices);
    free(p->pre_tri_counts);
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
    p.thread_count = ctx->thread_count;

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
    p.stats.used_area = 0;
    for (uint32_t i = 0; i < p.page_count; i++) {
        p.stats.used_area += (uint64_t)p.page_w[i] * (uint64_t)p.page_h[i];
    }
    uint64_t pot_waste_area = (p.stats.used_area > p.stats.frontier_area) ? (p.stats.used_area - p.stats.frontier_area) : 0;
    double poly_frontier_fill = (p.stats.frontier_area > 0) ? ((double)p.stats.poly_area / (double)p.stats.frontier_area) : 0.0;
    double poly_texture_fill = (p.stats.used_area > 0) ? ((double)p.stats.poly_area / (double)p.stats.used_area) : 0.0;
    NT_LOG_INFO("Atlas packed: %u sprites (%u unique), %u pages", p.sprite_count, p.unique_count, p.page_count);
    NT_LOG_INFO("BENCH alpha_trim=%.1f dedup=%.1f geometry=%.1f pack=%.1f compose=%.1f debug_png=%.1f serialize=%.1f total=%.1f pages=%u used_area=%llu frontier_area=%llu trim_area=%llu "
                "poly_area=%llu pot_waste=%llu poly_frontier_fill=%.4f poly_texture_fill=%.4f or_ops=%llu test_ops=%llu page_scans=%llu page_prunes=%llu page_existing=%llu page_backfills=%llu "
                "page_new=%llu relevant=%llu candidates=%llu grid_fallbacks=%llu",
                bench_alpha_trim * 1000.0, bench_dedup * 1000.0, bench_geometry * 1000.0, bench_tile_pack * 1000.0, bench_compose * 1000.0, bench_debug_png * 1000.0, bench_serialize * 1000.0,
                bench_total * 1000.0, p.page_count, (unsigned long long)p.stats.used_area, (unsigned long long)p.stats.frontier_area, (unsigned long long)p.stats.trim_area,
                (unsigned long long)p.stats.poly_area, (unsigned long long)pot_waste_area, poly_frontier_fill, poly_texture_fill, (unsigned long long)p.stats.or_count,
                (unsigned long long)p.stats.test_count, (unsigned long long)p.stats.page_scan_count, (unsigned long long)p.stats.page_prune_count, (unsigned long long)p.stats.page_existing_hit_count,
                (unsigned long long)p.stats.page_backfill_count, (unsigned long long)p.stats.page_new_count, (unsigned long long)p.stats.relevant_count, (unsigned long long)p.stats.candidate_count,
                (unsigned long long)p.stats.grid_fallback_count);
    NT_LOG_INFO("BENCH_VPACK nfp_cache_hits=%llu nfp_cache_misses=%llu nfp_cache_collisions=%llu orient_dedup_saved=%llu dirty_cells=%llu", (unsigned long long)p.stats.nfp_cache_hit_count,
                (unsigned long long)p.stats.nfp_cache_miss_count, (unsigned long long)p.stats.nfp_cache_collision_count, (unsigned long long)p.stats.orient_dedup_saved_count,
                (unsigned long long)p.stats.dirty_cell_count);

    pipeline_cleanup(&p);
}
// #endregion
