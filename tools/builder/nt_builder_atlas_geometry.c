/*
 * Atlas geometry primitives — moved out of nt_builder_atlas.c.
 * Pure routines on Point2D, binary masks, polygons. Used by atlas
 * pipeline (nt_builder_atlas.c) and vector packer (nt_builder_atlas_vpack.c).
 */

/* clang-format off */
#include "nt_builder_atlas_geometry.h"
#include "nt_builder.h"          /* NT_BUILD_ASSERT */
#include "nt_clipper2_bridge.h"  /* nt_clipper2_inflate, nt_clipper2_triangulate */
#include "log/nt_log.h"          /* NT_LOG_WARN */
/* clang-format on */

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* --- Alpha plane: extract dense 1-byte alpha from RGBA --- */

uint8_t *alpha_plane_extract(const uint8_t *rgba, uint32_t w, uint32_t h) {
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

bool alpha_trim(const uint8_t *alpha, uint32_t w, uint32_t h, uint8_t threshold, uint32_t *out_x, uint32_t *out_y, uint32_t *out_w, uint32_t *out_h) {
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

/* Point2D is defined in nt_builder_atlas_geometry.h. */

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
uint32_t convex_hull(const Point2D *pts, uint32_t n, Point2D *out) {
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
uint32_t hull_simplify(const Point2D *hull, uint32_t n, uint32_t max_vertices, Point2D *out) {
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
            double area = fabs(((double)(hull[i].x - hull[prev].x) * (double)(hull[next].y - hull[prev].y)) - ((double)(hull[i].y - hull[prev].y) * (double)(hull[next].x - hull[prev].x)));
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
// NOLINTNEXTLINE(readability-function-cognitive-complexity) — greedy perpendicular-distance simplification with per-step min search; O(n²) control flow is inherent
uint32_t hull_simplify_perp(const Point2D *hull, uint32_t n, uint32_t max_vertices, Point2D *out, double *out_max_dev) {
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

uint32_t fan_triangulate(uint32_t vertex_count, uint16_t *indices) {
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
/* --- Triangulation via Clipper2 Constrained Delaunay Triangulation --- */

/* Triangulates a simple polygon (convex or concave). Uses Clipper2 CDT
 * for better triangle quality than ear-clipping. Falls back to fan_triangulate
 * on failure (e.g., self-intersecting input).
 * Input:  polygon vertices (CCW winding), vertex count n.
 * Output: triangle indices (local 0..n-1) written to 'indices'.
 * Returns number of triangles. */
uint32_t ear_clip_triangulate(const Point2D *poly, uint32_t n, uint16_t *indices) {
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
    int32_t *xy = (n <= 32) ? stack_xy : (int32_t *)malloc((size_t)n * 2 * sizeof(int32_t));
    NT_BUILD_ASSERT(xy && "triangulate: alloc failed");
    for (size_t i = 0; i < n; i++) {
        xy[i * 2] = poly[i].x;
        xy[(i * 2) + 1] = poly[i].y;
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
    memcpy(indices, cdt_indices, (size_t)tri_count * 3 * sizeof(uint16_t));
    free(cdt_indices);
    return tri_count;
}

/* --- Point-in-polygon test (ray casting, even-odd rule) --- */

bool point_in_polygon(const Point2D *poly, uint32_t n, Point2D p) {
    bool inside = false;
    for (uint32_t i = 0, j = n - 1; i < n; j = i++) {
        if (((poly[i].y > p.y) != (poly[j].y > p.y)) && (p.x < (int32_t)((((int64_t)(poly[j].x - poly[i].x) * (int64_t)(p.y - poly[i].y)) / (int64_t)(poly[j].y - poly[i].y)) + poly[i].x))) {
            inside = !inside;
        }
    }
    return inside;
}

/* Float-coord point-in-polygon (even-odd rule) — used to test pixel centers
 * (which live at non-integer (x+0.5, y+0.5) positions) against an integer
 * polygon. */
bool point_in_polygon_f(const Point2D *poly, uint32_t n, double px, double py) {
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
// NOLINTNEXTLINE(readability-function-cognitive-complexity) — pixel-grid scan with per-edge projection for every opaque pixel; loop nesting is the natural shape
double polygon_max_outside_pixel_distance(const Point2D *poly, uint32_t poly_count, const uint8_t *binary, uint32_t tw, uint32_t th) {
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
void binary_dilate_4conn(const uint8_t *in, uint8_t *out, uint32_t tw, uint32_t th) {
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
uint32_t binary_count_components(const uint8_t *M, uint32_t tw, uint32_t th, uint8_t *visited, int32_t *stack) {
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

// NOLINTNEXTLINE(readability-function-cognitive-complexity) — iterative convex hull + ensure-fully-enclosed refinement loop; refactoring into helpers would obscure the termination invariant
Point2D *binary_build_convex_polygon(const uint8_t *binary, uint32_t tw, uint32_t th, uint32_t max_vertices, uint32_t *out_count) {
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
uint32_t trace_contour(const uint8_t *binary, uint32_t tw, uint32_t th, Point2D *out, uint32_t max_out) {
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
    if      (EDGE_EXISTS(cx, cy, r_)) { dir = r_;
    } else if (EDGE_EXISTS(cx, cy, s_)) { dir = s_;
    } else if (EDGE_EXISTS(cx, cy, l_)) { dir = l_;
    } else {                              dir = b_;
}
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
        if      (EDGE_EXISTS(cx, cy, r_)) { dir = r_;
        } else if (EDGE_EXISTS(cx, cy, s_)) { dir = s_;
        } else if (EDGE_EXISTS(cx, cy, l_)) { dir = l_;
        } else {                              dir = b_;
}
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

uint32_t remove_collinear(const Point2D *in, uint32_t n, Point2D *out) {
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
// NOLINTNEXTLINE(misc-no-recursion) — Ramer-Douglas-Peucker is a recursive divide-and-conquer algorithm by definition; iterative version would need an explicit stack and be less clear
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
// NOLINTNEXTLINE(readability-function-cognitive-complexity) — closed-loop RDP with double-pivot splitting; step count reflects the algorithm, not tangled control flow
uint32_t rdp_simplify(const Point2D *poly, uint32_t n, double epsilon, Point2D *out) {
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
    NT_BUILD_ASSERT(chain && "rdp_simplify: alloc failed");

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
    free(chain);

    return (count >= 3) ? count : n;
}

/* --- Polygon inflation via Clipper2 (handles concave corners correctly) --- */

/* Inflates a polygon by 'amount' pixels. Output buffer 'out' must hold at least
 * max(n, 32) Point2D entries — Clipper2 may add vertices at concave splits,
 * but we cap the result to fit downstream stack arrays. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity) — Clipper2 bridge plumbing + result simplification; subroutines would just shuffle locals across functions
uint32_t polygon_inflate(const Point2D *hull, uint32_t n, float amount, Point2D *out) {
    if (n < 3 || amount <= 0.0F) {
        memcpy(out, hull, (size_t)n * sizeof(Point2D));
        return n;
    }
    /* Convert Point2D → flat xy array for bridge */
    int32_t stack_xy[64];
    int32_t *xy = (n <= 32) ? stack_xy : (int32_t *)malloc((size_t)n * 2 * sizeof(int32_t));
    NT_BUILD_ASSERT(xy && "polygon_inflate: alloc failed");
    for (size_t i = 0; i < n; i++) {
        xy[i * 2] = hull[i].x;
        xy[(i * 2) + 1] = hull[i].y;
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
    /* Downstream callers size the output buffer for 32 vertices (the NFP packer
     * has hard-coded stack arrays). If Clipper2 introduced more vertices at
     * concave splits, silently truncating would drop geometry and could leave
     * alpha pixels uncovered. Warn and fall back to the input polygon — callers
     * that really need the inflated result have a post-verify pass that will
     * catch the coverage loss and trigger a bbox fallback. */
    if (out_count > 32) {
        NT_LOG_WARN("polygon_inflate: Clipper2 produced %u vertices (cap 32), using input unchanged", out_count);
        free(inflated_xy);
        memcpy(out, hull, n * sizeof(Point2D));
        return n;
    }
    /* Sanity check: inflated coordinates must be within reasonable bounds (~input range + amount).
     * If Clipper2 returned something weird, fall back to the input. */
    int32_t in_min_x = hull[0].x;
    int32_t in_max_x = hull[0].x;
    int32_t in_min_y = hull[0].y;
    int32_t in_max_y = hull[0].y;
    for (uint32_t i = 1; i < n; i++) {
        if (hull[i].x < in_min_x) {
            in_min_x = hull[i].x;
        }
        if (hull[i].x > in_max_x) {
            in_max_x = hull[i].x;
        }
        if (hull[i].y < in_min_y) {
            in_min_y = hull[i].y;
        }
        if (hull[i].y > in_max_y) {
            in_max_y = hull[i].y;
        }
    }
    int32_t margin = (int32_t)(amount * 4.0F) + 16;
    bool sane = true;
    for (size_t i = 0; i < out_count; i++) {
        int32_t x = inflated_xy[i * 2];
        int32_t y = inflated_xy[(i * 2) + 1];
        if (x < in_min_x - margin || x > in_max_x + margin || y < in_min_y - margin || y > in_max_y + margin) {
            sane = false;
            break;
        }
    }
    if (!sane) {
        NT_LOG_WARN("polygon_inflate: Clipper2 returned out-of-bounds vertices, using input unchanged");
        free(inflated_xy);
        memcpy(out, hull, (size_t)n * sizeof(Point2D));
        return n;
    }
    for (size_t i = 0; i < out_count; i++) {
        out[i].x = inflated_xy[i * 2];
        out[i].y = inflated_xy[(i * 2) + 1];
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

void transform_point(int32_t sx, int32_t sy, uint8_t flags, int32_t tw, int32_t th, int32_t *ox, int32_t *oy) {
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
void polygon_transform(const Point2D *src, uint32_t n, uint8_t flags, int32_t tw, int32_t th, Point2D *out) {
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

uint64_t polygon_area_pixels(const Point2D *poly, uint32_t count) {
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
