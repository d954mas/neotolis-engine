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

static int64_t cross2d(Point2D o, Point2D a, Point2D b) {
    int64_t ax = (int64_t)a.x - (int64_t)o.x;
    int64_t ay = (int64_t)a.y - (int64_t)o.y;
    int64_t bx = (int64_t)b.x - (int64_t)o.x;
    int64_t by = (int64_t)b.y - (int64_t)o.y;
    return (ax * by) - (ay * bx);
}

static bool point2d_equal(Point2D a, Point2D b) { return a.x == b.x && a.y == b.y; }

static bool point_on_segment(Point2D a, Point2D b, Point2D p) {
    if (cross2d(a, b, p) != 0) {
        return false;
    }
    int32_t min_x = a.x < b.x ? a.x : b.x;
    int32_t max_x = a.x > b.x ? a.x : b.x;
    int32_t min_y = a.y < b.y ? a.y : b.y;
    int32_t max_y = a.y > b.y ? a.y : b.y;
    return p.x >= min_x && p.x <= max_x && p.y >= min_y && p.y <= max_y;
}

static int cross_sign(int64_t value) { return (value > 0) - (value < 0); }

static bool segments_intersect_or_touch(Point2D a, Point2D b, Point2D c, Point2D d) {
    int64_t ab_c = cross2d(a, b, c);
    int64_t ab_d = cross2d(a, b, d);
    int64_t cd_a = cross2d(c, d, a);
    int64_t cd_b = cross2d(c, d, b);
    if (cross_sign(ab_c) != cross_sign(ab_d) && cross_sign(cd_a) != cross_sign(cd_b)) {
        return true;
    }
    return (ab_c == 0 && point_on_segment(a, b, c)) || (ab_d == 0 && point_on_segment(a, b, d)) || (cd_a == 0 && point_on_segment(c, d, a)) || (cd_b == 0 && point_on_segment(c, d, b));
}

static int64_t polygon_signed_twice_area(const Point2D *poly, uint32_t count) {
    int64_t twice_area = 0;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t j = (i + 1U) % count;
        twice_area += ((int64_t)poly[i].x * (int64_t)poly[j].y) - ((int64_t)poly[j].x * (int64_t)poly[i].y);
    }
    return twice_area;
}

static bool polygon_coordinates_widening_safe(const Point2D *poly, uint32_t count) {
    for (uint32_t i = 0; i < count; i++) {
        if (poly[i].x < INT16_MIN || poly[i].x > INT16_MAX || poly[i].y < INT16_MIN || poly[i].y > INT16_MAX) {
            return false;
        }
    }
    return true;
}

nt_polygon_validity_t polygon_validate(const Point2D *poly, uint32_t count) {
    if (!poly || count < 3) {
        return NT_POLYGON_INVALID_TOO_FEW_VERTICES;
    }
    for (uint32_t i = 0; i < count; i++) {
        for (uint32_t j = i + 1U; j < count; j++) {
            if (point2d_equal(poly[i], poly[j])) {
                return NT_POLYGON_INVALID_REPEATED_VERTEX;
            }
        }
    }
    for (uint32_t i = 0; i < count; i++) {
        Point2D a = poly[i];
        Point2D b = poly[(i + 1U) % count];
        for (uint32_t j = i + 1U; j < count; j++) {
            Point2D c = poly[j];
            Point2D d = poly[(j + 1U) % count];
            bool adjacent = ((i + 1U) % count == j) || ((j + 1U) % count == i);
            if (!adjacent && segments_intersect_or_touch(a, b, c, d)) {
                return NT_POLYGON_INVALID_SELF_INTERSECTION;
            }
        }
    }
    int64_t twice_area = polygon_signed_twice_area(poly, count);
    if (twice_area == 0) {
        return NT_POLYGON_INVALID_ZERO_AREA;
    }
    for (uint32_t i = 0; i < count; i++) {
        Point2D a = poly[i];
        Point2D common = poly[(i + 1U) % count];
        Point2D d = poly[(i + 2U) % count];
        if (point_on_segment(a, common, d) || point_on_segment(common, d, a)) {
            return NT_POLYGON_INVALID_SELF_INTERSECTION;
        }
    }
    return twice_area > 0 ? NT_POLYGON_VALID : NT_POLYGON_INVALID_WINDING;
}

static bool point_in_or_on_polygon_scaled(const Point2D *poly, uint32_t count, int64_t px, int64_t py, int64_t scale) {
    int winding = 0;
    for (uint32_t i = 0; i < count; i++) {
        Point2D a = poly[i];
        Point2D b = poly[(i + 1U) % count];
        int64_t ax = (int64_t)a.x * scale;
        int64_t ay = (int64_t)a.y * scale;
        int64_t bx = (int64_t)b.x * scale;
        int64_t by = (int64_t)b.y * scale;
        int64_t cross = ((bx - ax) * (py - ay)) - ((by - ay) * (px - ax));
        if (cross == 0 && px >= (ax < bx ? ax : bx) && px <= (ax > bx ? ax : bx) && py >= (ay < by ? ay : by) && py <= (ay > by ? ay : by)) {
            return true;
        }
        if (ay <= py) {
            if (by > py && cross > 0) {
                winding++;
            }
        } else if (by <= py && cross < 0) {
            winding--;
        }
    }
    return winding != 0;
}

static bool segment_crosses_open_cell(Point2D a, Point2D b, uint32_t x, uint32_t y) {
    int64_t ax = (int64_t)a.x * 2;
    int64_t ay = (int64_t)a.y * 2;
    int64_t bx = (int64_t)b.x * 2;
    int64_t by = (int64_t)b.y * 2;
    int64_t left = (int64_t)x * 2;
    int64_t top = (int64_t)y * 2;
    int64_t right = left + 2;
    int64_t bottom = top + 2;
    int64_t min_x = ax < bx ? ax : bx;
    int64_t max_x = ax > bx ? ax : bx;
    int64_t min_y = ay < by ? ay : by;
    int64_t max_y = ay > by ? ay : by;
    if (max_x <= left || min_x >= right || max_y <= top || min_y >= bottom) {
        return false;
    }

    typedef struct {
        int64_t numerator;
        int64_t denominator;
    } SegmentFraction;
    SegmentFraction lower = {0, 1};
    SegmentFraction upper = {1, 1};
    const int64_t starts[2] = {ax, ay};
    const int64_t deltas[2] = {bx - ax, by - ay};
    const int64_t lows[2] = {left, top};
    const int64_t highs[2] = {right, bottom};
    for (uint32_t axis = 0; axis < 2; axis++) {
        if (deltas[axis] == 0) {
            if (starts[axis] <= lows[axis] || starts[axis] >= highs[axis]) {
                return false;
            }
            continue;
        }
        SegmentFraction axis_lower = {0};
        SegmentFraction axis_upper = {0};
        if (deltas[axis] > 0) {
            axis_lower = (SegmentFraction){lows[axis] - starts[axis], deltas[axis]};
            axis_upper = (SegmentFraction){highs[axis] - starts[axis], deltas[axis]};
        } else {
            axis_lower = (SegmentFraction){starts[axis] - highs[axis], -deltas[axis]};
            axis_upper = (SegmentFraction){starts[axis] - lows[axis], -deltas[axis]};
        }
        if ((lower.numerator * axis_lower.denominator) < (axis_lower.numerator * lower.denominator)) {
            lower = axis_lower;
        }
        if ((axis_upper.numerator * upper.denominator) < (upper.numerator * axis_upper.denominator)) {
            upper = axis_upper;
        }
    }
    return (lower.numerator * upper.denominator) < (upper.numerator * lower.denominator);
}

bool nt_polygon_covers_retained_cells(const Point2D *poly, uint32_t poly_count, const uint8_t *binary, uint32_t tw, uint32_t th, uint32_t *out_retained_cells, uint32_t *out_lost_cells) {
    uint32_t retained_cells = 0;
    uint32_t lost_cells = 0;
    if (!poly || poly_count < 3 || !binary || tw > INT16_MAX || th > INT16_MAX || !polygon_coordinates_widening_safe(poly, poly_count)) {
        if (out_retained_cells) {
            *out_retained_cells = 0;
        }
        if (out_lost_cells) {
            *out_lost_cells = 0;
        }
        return false;
    }

    for (uint32_t y = 0; y < th; y++) {
        for (uint32_t x = 0; x < tw; x++) {
            if (binary[((size_t)y * tw) + x] == 0) {
                continue;
            }
            retained_cells++;
            bool covered = point_in_or_on_polygon_scaled(poly, poly_count, x, y, 1) && point_in_or_on_polygon_scaled(poly, poly_count, (int64_t)x + 1, y, 1) &&
                           point_in_or_on_polygon_scaled(poly, poly_count, (int64_t)x + 1, (int64_t)y + 1, 1) && point_in_or_on_polygon_scaled(poly, poly_count, x, (int64_t)y + 1, 1);
            for (uint32_t edge = 0; covered && edge < poly_count; edge++) {
                covered = !segment_crosses_open_cell(poly[edge], poly[(edge + 1U) % poly_count], x, y);
            }
            lost_cells += covered ? 0U : 1U;
        }
    }

    if (out_retained_cells) {
        *out_retained_cells = retained_cells;
    }
    if (out_lost_cells) {
        *out_lost_cells = lost_cells;
    }
    return retained_cells > 0 && lost_cells == 0;
}

typedef struct {
    uint16_t from;
    uint16_t to;
} TriangleEdge;

static bool polygon_vertices_adjacent(uint16_t a, uint16_t b, uint32_t count) { return ((uint32_t)a + 1U) % count == b || ((uint32_t)b + 1U) % count == a; }

static bool triangle_diagonal_inside(const Point2D *poly, uint32_t count, uint16_t from, uint16_t to) {
    if (polygon_vertices_adjacent(from, to, count)) {
        return true;
    }
    Point2D a = poly[from];
    Point2D b = poly[to];
    if (!point_in_or_on_polygon_scaled(poly, count, (int64_t)a.x + b.x, (int64_t)a.y + b.y, 2)) {
        return false;
    }
    for (uint32_t edge = 0; edge < count; edge++) {
        uint32_t next = (edge + 1U) % count;
        if (edge == from || edge == to || next == from || next == to) {
            continue;
        }
        if (segments_intersect_or_touch(a, b, poly[edge], poly[next])) {
            return false;
        }
    }
    return true;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
bool nt_polygon_triangles_validate(const Point2D *poly, uint32_t poly_count, const uint16_t *indices, uint32_t index_count, uint64_t *out_area2) {
    if (!poly || !indices || poly_count < 3 || poly_count > NT_POLYGON_MAX_VERTICES || !polygon_coordinates_widening_safe(poly, poly_count) || polygon_validate(poly, poly_count) != NT_POLYGON_VALID ||
        index_count != (poly_count - 2U) * 3U) {
        return false;
    }

    TriangleEdge edges[NT_POLYGON_MAX_TRIANGLE_INDICES];
    uint64_t triangle_area2 = 0;
    for (uint32_t triangle = 0; triangle < index_count / 3U; triangle++) {
        uint16_t a = indices[(triangle * 3U) + 0U];
        uint16_t b = indices[(triangle * 3U) + 1U];
        uint16_t c = indices[(triangle * 3U) + 2U];
        if (a >= poly_count || b >= poly_count || c >= poly_count || a == b || b == c || c == a) {
            return false;
        }
        int64_t area2 = cross2d(poly[a], poly[b], poly[c]);
        if (area2 <= 0 || UINT64_MAX - triangle_area2 < (uint64_t)area2) {
            return false;
        }
        triangle_area2 += (uint64_t)area2;
        TriangleEdge tri_edges[3] = {{a, b}, {b, c}, {c, a}};
        for (uint32_t edge = 0; edge < 3; edge++) {
            edges[(triangle * 3U) + edge] = tri_edges[edge];
            if (!triangle_diagonal_inside(poly, poly_count, tri_edges[edge].from, tri_edges[edge].to)) {
                return false;
            }
        }
    }
    if (triangle_area2 != polygon_abs_twice_area(poly, poly_count)) {
        return false;
    }

    bool consumed[NT_POLYGON_MAX_TRIANGLE_INDICES] = {0};
    for (uint32_t i = 0; i < index_count; i++) {
        if (consumed[i]) {
            continue;
        }
        uint32_t same = 0;
        uint32_t reverse = 0;
        for (uint32_t j = i; j < index_count; j++) {
            if ((edges[j].from == edges[i].from && edges[j].to == edges[i].to) || (edges[j].from == edges[i].to && edges[j].to == edges[i].from)) {
                consumed[j] = true;
                same += edges[j].from == edges[i].from && edges[j].to == edges[i].to ? 1U : 0U;
                reverse += edges[j].from == edges[i].to && edges[j].to == edges[i].from ? 1U : 0U;
            }
        }
        bool boundary = ((uint32_t)edges[i].from + 1U) % poly_count == edges[i].to;
        if ((boundary && (same != 1U || reverse != 0U)) || (!boundary && (same != 1U || reverse != 1U))) {
            return false;
        }
    }

    for (uint32_t i = 0; i < index_count; i++) {
        if (polygon_vertices_adjacent(edges[i].from, edges[i].to, poly_count)) {
            continue;
        }
        for (uint32_t j = i + 1U; j < index_count; j++) {
            if (polygon_vertices_adjacent(edges[j].from, edges[j].to, poly_count) || edges[i].from == edges[j].from || edges[i].from == edges[j].to || edges[i].to == edges[j].from ||
                edges[i].to == edges[j].to) {
                continue;
            }
            if (segments_intersect_or_touch(poly[edges[i].from], poly[edges[i].to], poly[edges[j].from], poly[edges[j].to])) {
                return false;
            }
        }
    }

    if (out_area2) {
        *out_area2 = triangle_area2;
    }
    return true;
}

bool nt_polygon_triangulate_validated(const Point2D *poly, uint32_t poly_count, uint16_t *out_indices, uint32_t *out_index_count, uint64_t *out_area2) {
    if (!poly || !out_indices || !out_index_count || poly_count < 3 || poly_count > NT_POLYGON_MAX_VERTICES || !polygon_coordinates_widening_safe(poly, poly_count) ||
        polygon_validate(poly, poly_count) != NT_POLYGON_VALID) {
        return false;
    }
    uint32_t index_count = (poly_count - 2U) * 3U;
    if (poly_count == 3) {
        out_indices[0] = 0;
        out_indices[1] = 1;
        out_indices[2] = 2;
    } else {
        int32_t xy[NT_POLYGON_MAX_VERTICES * 2];
        for (uint32_t i = 0; i < poly_count; i++) {
            xy[(size_t)i * 2U] = poly[i].x;
            xy[((size_t)i * 2U) + 1U] = poly[i].y;
        }
        uint16_t *generated = NULL;
        uint32_t triangle_count = nt_clipper2_triangulate(xy, poly_count, &generated);
        if (!generated || triangle_count != poly_count - 2U) {
            free(generated);
            return false;
        }
        memcpy(out_indices, generated, (size_t)index_count * sizeof(uint16_t));
        free(generated);
        for (uint32_t triangle = 0; triangle < triangle_count; triangle++) {
            uint16_t *a = &out_indices[(triangle * 3U) + 0U];
            uint16_t *b = &out_indices[(triangle * 3U) + 1U];
            uint16_t *c = &out_indices[(triangle * 3U) + 2U];
            if (*a >= poly_count || *b >= poly_count || *c >= poly_count) {
                return false;
            }
            if (cross2d(poly[*a], poly[*b], poly[*c]) < 0) {
                uint16_t swap = *b;
                *b = *c;
                *c = swap;
            }
        }
    }
    if (!nt_polygon_triangles_validate(poly, poly_count, out_indices, index_count, out_area2)) {
        return false;
    }
    *out_index_count = index_count;
    return true;
}

nt_polygon_feasibility_t nt_polygon_feasibility(const Point2D *poly, uint32_t poly_count, const uint8_t *binary, uint32_t tw, uint32_t th, uint32_t max_vertices) {
    nt_polygon_feasibility_t result = {0};
    if (!poly || !binary || poly_count < 3 || poly_count > max_vertices || poly_count > NT_POLYGON_MAX_VERTICES || tw == 0 || th == 0 || tw > INT16_MAX || th > INT16_MAX) {
        return result;
    }
    result.bounds_valid = true;
    for (uint32_t i = 0; i < poly_count; i++) {
        if (poly[i].x < 0 || poly[i].y < 0 || (uint32_t)poly[i].x > tw || (uint32_t)poly[i].y > th) {
            result.bounds_valid = false;
            return result;
        }
    }
    result.topology_valid = polygon_validate(poly, poly_count) == NT_POLYGON_VALID;
    if (!result.topology_valid) {
        return result;
    }
    result.coverage_valid = nt_polygon_covers_retained_cells(poly, poly_count, binary, tw, th, &result.retained_cell_count, &result.lost_retained_cell_count);
    result.retained_area2 = (uint64_t)result.retained_cell_count * 2U;
    result.lost_area2 = (uint64_t)result.lost_retained_cell_count * 2U;
    result.polygon_area2 = polygon_abs_twice_area(poly, poly_count);
    if (!result.coverage_valid) {
        return result;
    }
    result.triangulation_valid = nt_polygon_triangulate_validated(poly, poly_count, result.triangle_indices, &result.triangle_index_count, NULL);
    result.valid = result.bounds_valid && result.topology_valid && result.coverage_valid && result.triangulation_valid;
    return result;
}

static bool selected_geometry_bounds_valid(const Point2D *poly, uint32_t count, uint32_t width, uint32_t height) {
    if (!poly || count < 3 || count > NT_POLYGON_MAX_VERTICES || width == 0 || height == 0 || width > INT16_MAX || height > INT16_MAX) {
        return false;
    }
    for (uint32_t i = 0; i < count; i++) {
        if (poly[i].x < 0 || poly[i].y < 0 || (uint32_t)poly[i].x > width || (uint32_t)poly[i].y > height) {
            return false;
        }
    }
    return true;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
nt_selected_geometry_proof_t nt_selected_geometry_validate(const uint8_t *binary, uint32_t width, uint32_t height, uint64_t claimed_opaque_area2, const Point2D *base_poly, uint32_t base_count,
                                                           uint64_t claimed_base_area2, const uint16_t *base_indices, uint32_t base_index_count, const Point2D *selected_poly, uint32_t selected_count,
                                                           uint64_t claimed_selected_area2, const uint16_t *selected_indices, uint32_t selected_index_count, float max_added_area_percent,
                                                           uint32_t max_vertices) {
    nt_selected_geometry_proof_t proof = {0};
    proof.base_vertex_count = base_count;
    proof.selected_vertex_count = selected_count;
    proof.base_triangle_index_count = base_index_count;
    proof.selected_triangle_index_count = selected_index_count;
    proof.max_vertices = max_vertices;
    proof.max_added_area_percent = max_added_area_percent == 0.0F ? 0.0F : max_added_area_percent;
    proof.opaque_area2 = claimed_opaque_area2;
    proof.base_area2 = claimed_base_area2;
    proof.selected_area2 = claimed_selected_area2;
    proof.inputs_valid = binary && base_poly && base_indices && selected_poly && selected_indices && width > 0 && height > 0 && width <= INT16_MAX && height <= INT16_MAX && max_vertices >= 3 &&
                         max_vertices <= NT_POLYGON_MAX_VERTICES && isfinite(max_added_area_percent) && max_added_area_percent >= 0.0F;
    if (!proof.inputs_valid) {
        return proof;
    }

    uint32_t retained = 0;
    size_t cell_count = (size_t)width * height;
    for (size_t i = 0; i < cell_count; i++) {
        retained += binary[i] != 0 ? 1U : 0U;
    }
    proof.retained_cell_count = retained;
    uint64_t recomputed_opaque_area2 = (uint64_t)retained * 2U;
    proof.opaque_area_valid = retained > 0 && claimed_opaque_area2 == recomputed_opaque_area2;

    proof.base_bounds_valid = selected_geometry_bounds_valid(base_poly, base_count, width, height);
    proof.base_topology_valid = proof.base_bounds_valid && polygon_validate(base_poly, base_count) == NT_POLYGON_VALID;
    proof.base_coverage_valid = proof.base_topology_valid && nt_polygon_covers_retained_cells(base_poly, base_count, binary, width, height, NULL, NULL);
    uint64_t recomputed_base_area2 = 0;
    proof.base_triangulation_valid = proof.base_coverage_valid && nt_polygon_triangles_validate(base_poly, base_count, base_indices, base_index_count, &recomputed_base_area2);
    proof.base_area_valid = proof.base_triangulation_valid && recomputed_base_area2 == claimed_base_area2;

    proof.selected_bounds_valid = selected_geometry_bounds_valid(selected_poly, selected_count, width, height);
    proof.selected_topology_valid = proof.selected_bounds_valid && polygon_validate(selected_poly, selected_count) == NT_POLYGON_VALID;
    proof.selected_coverage_valid = proof.selected_topology_valid && nt_polygon_covers_retained_cells(selected_poly, selected_count, binary, width, height, NULL, NULL);
    uint64_t recomputed_selected_area2 = 0;
    proof.selected_triangulation_valid =
        proof.selected_coverage_valid && nt_polygon_triangles_validate(selected_poly, selected_count, selected_indices, selected_index_count, &recomputed_selected_area2);
    proof.selected_area_valid = proof.selected_triangulation_valid && recomputed_selected_area2 == claimed_selected_area2;

    proof.metric_order_valid =
        proof.opaque_area_valid && proof.base_area_valid && proof.selected_area_valid && claimed_base_area2 >= claimed_opaque_area2 && claimed_selected_area2 >= claimed_base_area2;
    proof.added_area2 = proof.metric_order_valid ? claimed_selected_area2 - claimed_base_area2 : 0;
    proof.allowance_valid = proof.metric_order_valid && ((double)proof.added_area2 * 100.0) <= ((double)claimed_opaque_area2 * (double)max_added_area_percent);
    proof.ceiling_valid = base_count <= max_vertices && selected_count <= max_vertices;
    proof.valid = proof.inputs_valid && proof.opaque_area_valid && proof.base_bounds_valid && proof.base_topology_valid && proof.base_coverage_valid && proof.base_triangulation_valid &&
                  proof.base_area_valid && proof.selected_bounds_valid && proof.selected_topology_valid && proof.selected_coverage_valid && proof.selected_triangulation_valid &&
                  proof.selected_area_valid && proof.metric_order_valid && proof.allowance_valid && proof.ceiling_valid;
    return proof;
}

bool nt_selected_geometry_proof_equal(const nt_selected_geometry_proof_t *left, const nt_selected_geometry_proof_t *right) {
    return left && right && left->inputs_valid == right->inputs_valid && left->opaque_area_valid == right->opaque_area_valid && left->base_bounds_valid == right->base_bounds_valid &&
           left->base_topology_valid == right->base_topology_valid && left->base_coverage_valid == right->base_coverage_valid && left->base_triangulation_valid == right->base_triangulation_valid &&
           left->base_area_valid == right->base_area_valid && left->selected_bounds_valid == right->selected_bounds_valid && left->selected_topology_valid == right->selected_topology_valid &&
           left->selected_coverage_valid == right->selected_coverage_valid && left->selected_triangulation_valid == right->selected_triangulation_valid &&
           left->selected_area_valid == right->selected_area_valid && left->metric_order_valid == right->metric_order_valid && left->allowance_valid == right->allowance_valid &&
           left->ceiling_valid == right->ceiling_valid && left->valid == right->valid && left->retained_cell_count == right->retained_cell_count &&
           left->base_vertex_count == right->base_vertex_count && left->selected_vertex_count == right->selected_vertex_count && left->base_triangle_index_count == right->base_triangle_index_count &&
           left->selected_triangle_index_count == right->selected_triangle_index_count && left->max_vertices == right->max_vertices && left->max_added_area_percent == right->max_added_area_percent &&
           left->opaque_area2 == right->opaque_area2 && left->base_area2 == right->base_area2 && left->selected_area2 == right->selected_area2 && left->added_area2 == right->added_area2;
}

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
/* Iteratively remove the vertex with the smallest adjacent triangle area. */

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

static bool covering_line_intersection(Point2D a, Point2D b, Point2D c, Point2D d, double *out_x, double *out_y) {
    double ab_x = (double)a.x - (double)b.x;
    double ab_y = (double)a.y - (double)b.y;
    double cd_x = (double)c.x - (double)d.x;
    double cd_y = (double)c.y - (double)d.y;
    double det = (ab_x * cd_y) - (ab_y * cd_x);
    if (!isfinite(det) || fabs(det) <= 1e-12) {
        return false;
    }
    double ab_cross = ((double)a.x * (double)b.y) - ((double)a.y * (double)b.x);
    double cd_cross = ((double)c.x * (double)d.y) - ((double)c.y * (double)d.x);
    double x = ((ab_cross * cd_x) - (ab_x * cd_cross)) / det;
    double y = ((ab_cross * cd_y) - (ab_y * cd_cross)) / det;
    if (!isfinite(x) || !isfinite(y) || x < (double)INT32_MIN || x > (double)INT32_MAX || y < (double)INT32_MIN || y > (double)INT32_MAX) {
        return false;
    }
    *out_x = x;
    *out_y = y;
    return true;
}

static void covering_replace_edge(const Point2D *poly, const uint32_t *source_indices, uint32_t count, uint32_t edge, Point2D replacement, Point2D *out, uint32_t *out_source_indices) {
    uint32_t next = (edge + 1) % count;
    uint32_t after = (next + 1) % count;
    out[0] = replacement;
    out_source_indices[0] = source_indices[edge] < source_indices[next] ? source_indices[edge] : source_indices[next];
    for (uint32_t dst = 1, src = after; dst < count - 1; dst++, src = (src + 1) % count) {
        out[dst] = poly[src];
        out_source_indices[dst] = source_indices[src];
    }
}

static bool covering_is_strict_convex(const Point2D *poly, uint32_t count) {
    for (uint32_t i = 0; i < count; i++) {
        Point2D a = poly[i];
        Point2D b = poly[(i + 1) % count];
        Point2D c = poly[(i + 2) % count];
        double cross = (((double)b.x - (double)a.x) * ((double)c.y - (double)b.y)) - (((double)b.y - (double)a.y) * ((double)c.x - (double)b.x));
        if (!(cross > 0.0)) {
            return false;
        }
    }
    return true;
}

static bool covering_encloses(const Point2D *poly, uint32_t poly_count, const Point2D *points, uint32_t point_count) {
    for (uint32_t edge = 0; edge < poly_count; edge++) {
        Point2D a = poly[edge];
        Point2D b = poly[(edge + 1) % poly_count];
        double dx = (double)b.x - (double)a.x;
        double dy = (double)b.y - (double)a.y;
        for (uint32_t i = 0; i < point_count; i++) {
            double px = (double)points[i].x - (double)a.x;
            double py = (double)points[i].y - (double)a.y;
            if ((dx * py) - (dy * px) < -1e-9) {
                return false;
            }
        }
    }
    return true;
}

static uint32_t covering_triangle_parallel_fallback(const Point2D *points, uint32_t count, Point2D out[3]) {
    int32_t min_x = points[0].x;
    int32_t max_x = points[0].x;
    int32_t min_y = points[0].y;
    int32_t max_y = points[0].y;
    for (uint32_t i = 1; i < count; i++) {
        min_x = points[i].x < min_x ? points[i].x : min_x;
        max_x = points[i].x > max_x ? points[i].x : max_x;
        min_y = points[i].y < min_y ? points[i].y : min_y;
        max_y = points[i].y > max_y ? points[i].y : max_y;
    }
    int64_t width = (int64_t)max_x - min_x;
    int64_t height = (int64_t)max_y - min_y;
    if (width <= 0 || height <= 0) {
        return 0;
    }

    const int64_t candidates[4][6] = {
        {min_x, min_y, (int64_t)min_x + (2 * width), min_y, min_x, (int64_t)min_y + (2 * height)},
        {max_x, min_y, max_x, (int64_t)min_y + (2 * height), (int64_t)max_x - (2 * width), min_y},
        {max_x, max_y, (int64_t)max_x - (2 * width), max_y, max_x, (int64_t)max_y - (2 * height)},
        {min_x, max_y, min_x, (int64_t)max_y - (2 * height), (int64_t)min_x + (2 * width), max_y},
    };
    bool found = false;
    uint64_t best_twice_area = UINT64_MAX;
    for (uint32_t candidate_idx = 0; candidate_idx < 4; candidate_idx++) {
        Point2D candidate[3];
        bool in_range = true;
        for (uint32_t i = 0; i < 3; i++) {
            size_t coord = (size_t)i * 2;
            int64_t x = candidates[candidate_idx][coord];
            int64_t y = candidates[candidate_idx][coord + 1];
            if (x < INT32_MIN || x > INT32_MAX || y < INT32_MIN || y > INT32_MAX) {
                in_range = false;
                break;
            }
            candidate[i] = (Point2D){(int32_t)x, (int32_t)y};
        }
        if (!in_range || polygon_validate(candidate, 3) != NT_POLYGON_VALID || !covering_encloses(candidate, 3, points, count)) {
            continue;
        }
        uint64_t twice_area = (uint64_t)polygon_signed_twice_area(candidate, 3);
        if (!found || twice_area < best_twice_area) {
            found = true;
            best_twice_area = twice_area;
            memcpy(out, candidate, sizeof(candidate));
        }
    }
    return found ? 3U : 0U;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
uint32_t hull_simplify_covering(const Point2D *hull, uint32_t n, uint32_t max_vertices, Point2D *out) {
    if (!hull || !out || n < 3 || max_vertices < 3) {
        return 0;
    }
    if (polygon_validate(hull, n) != NT_POLYGON_VALID) {
        return 0;
    }
    if (n <= max_vertices) {
        memcpy(out, hull, (size_t)n * sizeof(Point2D));
        return n;
    }

    Point2D *current = (Point2D *)malloc((size_t)n * sizeof(Point2D));
    Point2D *scratch = (Point2D *)malloc((size_t)n * sizeof(Point2D));
    uint32_t *current_sources = (uint32_t *)malloc((size_t)n * sizeof(uint32_t));
    uint32_t *scratch_sources = (uint32_t *)malloc((size_t)n * sizeof(uint32_t));
    NT_BUILD_ASSERT(current && scratch && current_sources && scratch_sources && "hull_simplify_covering: alloc failed");
    memcpy(current, hull, (size_t)n * sizeof(Point2D));
    for (uint32_t i = 0; i < n; i++) {
        current_sources[i] = i;
    }

    uint32_t count = n;
    while (count > max_vertices) {
        bool found = false;
        double best_error = HUGE_VAL;
        uint32_t best_source = UINT32_MAX;
        uint32_t best_edge = 0;
        Point2D best_point = {0};

        for (uint32_t edge = 0; edge < count; edge++) {
            uint32_t prev = (edge + count - 1) % count;
            uint32_t next = (edge + 1) % count;
            uint32_t after = (next + 1) % count;
            double ix = 0.0;
            double iy = 0.0;
            if (!covering_line_intersection(current[prev], current[edge], current[next], current[after], &ix, &iy)) {
                continue;
            }

            double edge_dx = (double)current[next].x - (double)current[edge].x;
            double edge_dy = (double)current[next].y - (double)current[edge].y;
            double edge_len = sqrt((edge_dx * edge_dx) + (edge_dy * edge_dy));
            if (!(edge_len > 0.0)) {
                continue;
            }
            double error = fabs((((ix - (double)current[edge].x) * edge_dy) - ((iy - (double)current[edge].y) * edge_dx)) / edge_len);

            double rounded_x[2] = {floor(ix), ceil(ix)};
            double rounded_y[2] = {floor(iy), ceil(iy)};
            bool rounded_found = false;
            double rounded_error = HUGE_VAL;
            Point2D rounded_point = {0};
            for (uint32_t yi = 0; yi < 2; yi++) {
                for (uint32_t xi = 0; xi < 2; xi++) {
                    Point2D candidate = {(int32_t)rounded_x[xi], (int32_t)rounded_y[yi]};
                    covering_replace_edge(current, current_sources, count, edge, candidate, scratch, scratch_sources);
                    if (!covering_is_strict_convex(scratch, count - 1) || !covering_encloses(scratch, count - 1, current, count)) {
                        continue;
                    }
                    double rx = (double)candidate.x - ix;
                    double ry = (double)candidate.y - iy;
                    double rounding = (rx * rx) + (ry * ry);
                    if (!rounded_found || rounding < rounded_error) {
                        rounded_found = true;
                        rounded_error = rounding;
                        rounded_point = candidate;
                    }
                }
            }
            if (!rounded_found) {
                continue;
            }

            uint32_t source = current_sources[edge] < current_sources[next] ? current_sources[edge] : current_sources[next];
            if (!found || error < best_error || (error == best_error && source < best_source)) {
                found = true;
                best_error = error;
                best_source = source;
                best_edge = edge;
                best_point = rounded_point;
            }
        }

        if (!found) {
            uint32_t fallback_count = max_vertices == 3 ? covering_triangle_parallel_fallback(hull, n, out) : 0;
            free(scratch_sources);
            free(current_sources);
            free(scratch);
            free(current);
            return fallback_count;
        }

        covering_replace_edge(current, current_sources, count, best_edge, best_point, scratch, scratch_sources);
        Point2D *point_swap = current;
        current = scratch;
        scratch = point_swap;
        uint32_t *source_swap = current_sources;
        current_sources = scratch_sources;
        scratch_sources = source_swap;
        count--;
    }

    memcpy(out, current, (size_t)count * sizeof(Point2D));
    free(scratch_sources);
    free(current_sources);
    free(scratch);
    free(current);
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
 * and publishes only a fully validated result.
 * Input:  polygon vertices (CCW winding), vertex count n.
 * Output: triangle indices (local 0..n-1) written to 'indices'.
 * Returns number of triangles. */
uint32_t ear_clip_triangulate(const Point2D *poly, uint32_t n, uint16_t *indices) {
    uint32_t index_count = 0;
    return nt_polygon_triangulate_validated(poly, n, indices, &index_count, NULL) ? index_count / 3U : 0U;
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
static double polygon_point_edge_distance_sq(const Point2D *poly, uint32_t poly_count, double cx, double cy) {
    double min_d_sq = 1e30;
    for (uint32_t i = 0; i < poly_count; i++) {
        uint32_t j = (i + 1) % poly_count;
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
    return min_d_sq;
}

/* Distance from point (cx,cy) to the nearest polygon edge. */
static double polygon_point_edge_distance(const Point2D *poly, uint32_t poly_count, double cx, double cy) { return sqrt(polygon_point_edge_distance_sq(poly, poly_count, cx, cy)); }

nt_polygon_coverage_metrics_t polygon_coverage_metrics(const Point2D *poly, uint32_t poly_count, const uint8_t *binary, uint32_t tw, uint32_t th) {
    nt_polygon_coverage_metrics_t metrics = {0};
    if (!poly || poly_count < 3 || !binary) {
        return metrics;
    }
    for (uint32_t y = 0; y < th; y++) {
        for (uint32_t x = 0; x < tw; x++) {
            double cx = (double)x + 0.5;
            double cy = (double)y + 0.5;
            bool covered = point_in_polygon_f(poly, poly_count, cx, cy) || polygon_point_edge_distance_sq(poly, poly_count, cx, cy) <= 1e-18;
            bool retained = binary[((size_t)y * tw) + x] != 0;
            metrics.lost_retained_pixels += retained && !covered ? 1U : 0U;
            metrics.extra_covered_pixels += !retained && covered ? 1U : 0U;
        }
    }
    return metrics;
}

typedef struct {
    double a;
    double b;
    double c;
    double begin;
    double end;
} BoundaryDistancePiece;

static void boundary_piece_set_endpoint(BoundaryDistancePiece *piece, Point2D reference_start, Point2D reference_end, Point2D endpoint) {
    double vx = (double)reference_end.x - (double)reference_start.x;
    double vy = (double)reference_end.y - (double)reference_start.y;
    double ox = (double)reference_start.x - (double)endpoint.x;
    double oy = (double)reference_start.y - (double)endpoint.y;
    piece->a = (vx * vx) + (vy * vy);
    piece->b = 2.0 * ((ox * vx) + (oy * vy));
    piece->c = (ox * ox) + (oy * oy);
}

static void boundary_piece_set_line(BoundaryDistancePiece *piece, Point2D reference_start, Point2D reference_end, Point2D edge_start, Point2D edge_end) {
    double vx = (double)reference_end.x - (double)reference_start.x;
    double vy = (double)reference_end.y - (double)reference_start.y;
    double ex = (double)edge_end.x - (double)edge_start.x;
    double ey = (double)edge_end.y - (double)edge_start.y;
    double ox = (double)reference_start.x - (double)edge_start.x;
    double oy = (double)reference_start.y - (double)edge_start.y;
    double edge_len_sq = (ex * ex) + (ey * ey);
    double cross_start = (ox * ey) - (oy * ex);
    double cross_step = (vx * ey) - (vy * ex);
    piece->a = (cross_step * cross_step) / edge_len_sq;
    piece->b = (2.0 * cross_start * cross_step) / edge_len_sq;
    piece->c = (cross_start * cross_start) / edge_len_sq;
}

static uint32_t boundary_edge_pieces(Point2D reference_start, Point2D reference_end, Point2D edge_start, Point2D edge_end, BoundaryDistancePiece *pieces) {
    double ex = (double)edge_end.x - (double)edge_start.x;
    double ey = (double)edge_end.y - (double)edge_start.y;
    double edge_len_sq = (ex * ex) + (ey * ey);
    if (edge_len_sq == 0.0) {
        pieces[0].begin = 0.0;
        pieces[0].end = 1.0;
        boundary_piece_set_endpoint(&pieces[0], reference_start, reference_end, edge_start);
        return 1;
    }

    double vx = (double)reference_end.x - (double)reference_start.x;
    double vy = (double)reference_end.y - (double)reference_start.y;
    double ox = (double)reference_start.x - (double)edge_start.x;
    double oy = (double)reference_start.y - (double)edge_start.y;
    double projection_start = ((ox * ex) + (oy * ey)) / edge_len_sq;
    double projection_step = ((vx * ex) + (vy * ey)) / edge_len_sq;
    double cuts[4] = {0.0, 1.0, 0.0, 0.0};
    uint32_t cut_count = 2;
    if (projection_step != 0.0) {
        double zero_crossing = -projection_start / projection_step;
        double one_crossing = (1.0 - projection_start) / projection_step;
        if (zero_crossing > 0.0 && zero_crossing < 1.0) {
            cuts[cut_count++] = zero_crossing;
        }
        if (one_crossing > 0.0 && one_crossing < 1.0) {
            cuts[cut_count++] = one_crossing;
        }
    }
    for (uint32_t i = 1; i < cut_count; i++) {
        double key = cuts[i];
        uint32_t j = i;
        while (j > 0 && cuts[j - 1] > key) {
            cuts[j] = cuts[j - 1];
            j--;
        }
        cuts[j] = key;
    }

    uint32_t piece_count = 0;
    for (uint32_t i = 0; i + 1 < cut_count; i++) {
        if (cuts[i + 1] <= cuts[i]) {
            continue;
        }
        BoundaryDistancePiece *piece = &pieces[piece_count++];
        piece->begin = cuts[i];
        piece->end = cuts[i + 1];
        double midpoint = (piece->begin + piece->end) * 0.5;
        double projection = projection_start + (projection_step * midpoint);
        if (projection <= 0.0) {
            boundary_piece_set_endpoint(piece, reference_start, reference_end, edge_start);
        } else if (projection >= 1.0) {
            boundary_piece_set_endpoint(piece, reference_start, reference_end, edge_end);
        } else {
            boundary_piece_set_line(piece, reference_start, reference_end, edge_start, edge_end);
        }
    }
    return piece_count;
}

static void boundary_consider_t(const Point2D *candidate, uint32_t candidate_count, Point2D start, Point2D end, double t, double *max_distance_sq) {
    double x = (double)start.x + (t * ((double)end.x - (double)start.x));
    double y = (double)start.y + (t * ((double)end.y - (double)start.y));
    double distance_sq = polygon_point_edge_distance_sq(candidate, candidate_count, x, y);
    if (distance_sq > *max_distance_sq) {
        *max_distance_sq = distance_sq;
    }
}

static void boundary_consider_piece_intersections(const BoundaryDistancePiece *lhs, const BoundaryDistancePiece *rhs, const Point2D *candidate, uint32_t candidate_count, Point2D start, Point2D end,
                                                  double *max_distance_sq) {
    double begin = lhs->begin > rhs->begin ? lhs->begin : rhs->begin;
    double finish = lhs->end < rhs->end ? lhs->end : rhs->end;
    if (finish < begin) {
        return;
    }
    double a = lhs->a - rhs->a;
    double b = lhs->b - rhs->b;
    double c = lhs->c - rhs->c;
    if (fabs(a) < 1e-12) {
        if (fabs(b) < 1e-12) {
            return;
        }
        double root = -c / b;
        if (root >= begin && root <= finish) {
            boundary_consider_t(candidate, candidate_count, start, end, root, max_distance_sq);
        }
        return;
    }
    double discriminant = (b * b) - (4.0 * a * c);
    if (discriminant < 0.0) {
        return;
    }
    double root_span = sqrt(discriminant);
    double roots[2] = {(-b - root_span) / (2.0 * a), (-b + root_span) / (2.0 * a)};
    for (uint32_t i = 0; i < 2; i++) {
        if (roots[i] >= begin && roots[i] <= finish) {
            boundary_consider_t(candidate, candidate_count, start, end, roots[i], max_distance_sq);
        }
    }
}

double polygon_max_boundary_distance(const Point2D *reference, uint32_t reference_count, const Point2D *candidate, uint32_t candidate_count) {
    enum { MAX_CANDIDATE_VERTICES = 16, MAX_DISTANCE_PIECES = MAX_CANDIDATE_VERTICES * 3 };
    NT_BUILD_ASSERT(reference && candidate && reference_count >= 3 && candidate_count >= 3 && candidate_count <= MAX_CANDIDATE_VERTICES);
    double max_distance_sq = 0.0;
    for (uint32_t reference_index = 0; reference_index < reference_count; reference_index++) {
        Point2D start = reference[reference_index];
        Point2D end = reference[(reference_index + 1) % reference_count];
        BoundaryDistancePiece pieces[MAX_DISTANCE_PIECES];
        uint32_t piece_count = 0;
        for (uint32_t candidate_index = 0; candidate_index < candidate_count; candidate_index++) {
            Point2D edge_start = candidate[candidate_index];
            Point2D edge_end = candidate[(candidate_index + 1) % candidate_count];
            piece_count += boundary_edge_pieces(start, end, edge_start, edge_end, &pieces[piece_count]);
        }
        for (uint32_t i = 0; i < piece_count; i++) {
            boundary_consider_t(candidate, candidate_count, start, end, pieces[i].begin, &max_distance_sq);
            boundary_consider_t(candidate, candidate_count, start, end, pieces[i].end, &max_distance_sq);
            for (uint32_t j = i + 1; j < piece_count; j++) {
                boundary_consider_piece_intersections(&pieces[i], &pieces[j], candidate, candidate_count, start, end, &max_distance_sq);
            }
        }
    }
    return sqrt(max_distance_sq);
}

/* Scanline inside/outside classification — computes per-row x-intersections
 * using EXACTLY the same edge-crossing formula as point_in_polygon_f, then
 * sweeps pixels left-to-right for amortized O(1) per pixel instead of O(V).
 *
 * Insertion sort on x_ints is fine: poly_count ≤ 16 → at most 16 crossings. */
static void scanline_sort_crossings(double *x_ints, uint32_t n) {
    for (uint32_t a = 1; a < n; a++) {
        double key = x_ints[a];
        uint32_t b = a;
        while (b > 0 && x_ints[b - 1] > key) {
            x_ints[b] = x_ints[b - 1];
            b--;
        }
        x_ints[b] = key;
    }
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity) — scanline intersection + sweep + distance for outside pixels
double polygon_max_outside_pixel_distance(const Point2D *poly, uint32_t poly_count, const uint8_t *binary, uint32_t tw, uint32_t th) {
    double max_d = 0.0;
    /* Pre-allocate crossing buffer — at most poly_count crossings per row. */
    double *x_ints = (double *)malloc(poly_count * sizeof(double));
    NT_BUILD_ASSERT(x_ints && "polygon_max_outside_pixel_distance: alloc failed");

    for (uint32_t y = 0; y < th; y++) {
        double cy = (double)y + 0.5;
        // #region Compute scanline x-intersections (same formula as point_in_polygon_f)
        uint32_t n_ints = 0;
        for (uint32_t i = 0, j = poly_count - 1; i < poly_count; j = i++) {
            double yi = (double)poly[i].y;
            double yj = (double)poly[j].y;
            if ((yi > cy) != (yj > cy)) {
                double xi = (double)poly[i].x;
                double xj = (double)poly[j].x;
                x_ints[n_ints++] = ((xj - xi) * (cy - yi) / (yj - yi)) + xi;
            }
        }
        scanline_sort_crossings(x_ints, n_ints);
        // #endregion

        // #region Sweep row: classify pixels + distance for outside
        /* Sweep left-to-right, counting crossings to the right of each pixel
         * center (matching point_in_polygon_f's `px < cross_x` logic). With
         * sorted crossings, the count-to-the-right for cx is n_ints - k, where
         * k is the first index with x_ints[k] > cx. We track k incrementally. */
        uint32_t k = 0;
        for (uint32_t x = 0; x < tw; x++) {
            if (binary[((size_t)y * tw) + x] == 0) {
                continue;
            }
            double cx = (double)x + 0.5;
            /* Advance k past crossings <= cx. */
            while (k < n_ints && !(cx < x_ints[k])) {
                k++;
            }
            /* Crossings to the right of cx = n_ints - k. Odd → inside. */
            if ((n_ints - k) & 1) {
                continue;
            }
            double d = polygon_point_edge_distance(poly, poly_count, cx, cy);
            if (d > max_d) {
                max_d = d;
            }
        }
        /* Reset k for next row. */
        // #endregion
    }

    free(x_ints);
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
    /* Content failure: no boundary pixels. Signal to the caller via NULL so it
     * can append a graceful error instead of aborting. */
    if (boundary_pixel_count == 0) {
        *out_count = 0;
        return NULL;
    }

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

    /* Content failure: a degenerate hull (<3 verts) has no usable outline. */
    if (hull_count < 3) {
        free(hull);
        *out_count = 0;
        return NULL;
    }
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
uint32_t trace_contour(const uint8_t *binary, uint32_t tw, uint32_t th, Point2D *out, uint32_t max_out, bool *out_overflow) {
    if (out_overflow) {
        *out_overflow = false;
    }
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
        /* Content failure: vertex budget exceeded. Signal the caller and stop
         * tracing (partial contour) rather than aborting mid-trace. */
        if (count >= max_out) {
            if (out_overflow) {
                *out_overflow = true;
            }
            return count;
        }
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

    if (count < 3) {
        memcpy(out, poly, (size_t)n * sizeof(Point2D));
        return n;
    }
    return count;
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
        x = w - x;
    }
    if (flags & 2) {
        y = h - y;
    }
    *ox = x;
    *oy = y;
}

/* Texel-space variant: maps pixel indices (0..w-1) to pixel indices (0..w-1).
 * Used by blit_sprite where src/dst are pixel coordinates, not polygon corners. */
void transform_point_texel(int32_t sx, int32_t sy, uint8_t flags, int32_t tw, int32_t th, int32_t *ox, int32_t *oy) {
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
    uint64_t twice_area = polygon_abs_twice_area(poly, count);
    return (twice_area >> 1U) + (twice_area & 1U);
}

uint64_t polygon_abs_twice_area(const Point2D *poly, uint32_t count) {
    if (count < 3) {
        return 0;
    }

    uint64_t positive = 0;
    uint64_t negative = 0;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t next = (i + 1U) % count;
        int64_t cross = ((int64_t)poly[i].x * poly[next].y) - ((int64_t)poly[next].x * poly[i].y);
        uint64_t magnitude = cross < 0 ? (uint64_t)(-(cross + 1)) + 1U : (uint64_t)cross;
        uint64_t *sum = cross < 0 ? &negative : &positive;
        NT_BUILD_ASSERT(UINT64_MAX - *sum >= magnitude && "polygon area overflow");
        *sum += magnitude;
    }

    return positive >= negative ? positive - negative : negative - positive;
}
