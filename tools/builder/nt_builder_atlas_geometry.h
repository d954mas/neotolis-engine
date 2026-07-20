#ifndef NT_BUILDER_ATLAS_GEOMETRY_H
#define NT_BUILDER_ATLAS_GEOMETRY_H

/*
 * Atlas geometry primitives — used by the atlas pipeline and the vector
 * packer. Pure routines on Point2D, binary masks, and polygons. No builder
 * context, no side effects besides heap allocation (caller frees).
 */

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 2D integer point. Integer coordinates throughout the atlas pipeline —
 * sub-pixel accuracy is not needed for sprite packing. */
typedef struct {
    int32_t x, y;
} Point2D;

/* --- Alpha plane / trim ---------------------------------------------- */

/* Extract dense 1-byte alpha plane from RGBA source. Caller frees result. */
uint8_t *alpha_plane_extract(const uint8_t *rgba, uint32_t w, uint32_t h);

/* Compute tight bounding box of pixels with alpha >= threshold.
 * Returns false when every pixel is below threshold. */
bool alpha_trim(const uint8_t *alpha, uint32_t w, uint32_t h, uint8_t threshold, uint32_t *out_x, uint32_t *out_y, uint32_t *out_w, uint32_t *out_h);

/* --- Convex hull / simplification ------------------------------------ */

/* Andrew's monotone chain convex hull. out must hold at least 2*n points. */
uint32_t convex_hull(const Point2D *pts, uint32_t n, Point2D *out);

/* May cut inside the source hull; use hull_simplify_covering when enclosure is required. */
uint32_t hull_simplify(const Point2D *hull, uint32_t n, uint32_t max_vertices, Point2D *out);

/* Reduce a CCW convex polygon while keeping every prior point enclosed.
 * Returns 0 when no finite, non-degenerate enclosing reduction exists. */
uint32_t hull_simplify_covering(const Point2D *hull, uint32_t n, uint32_t max_vertices, Point2D *out);

/* Greedy perpendicular-distance simplification. Always produces exactly
 * min(n, max_vertices) vertices. out_max_dev returns the largest seen
 * perp distance during removals (coarse upper bound). */
uint32_t hull_simplify_perp(const Point2D *hull, uint32_t n, uint32_t max_vertices, Point2D *out, double *out_max_dev);

/* --- Triangulation --------------------------------------------------- */

/* --- Contour tracing and polygon simplification ---------------------- */

/* Trace outer boundary of opaque pixels (CCW). Returns vertex count.
 * out_overflow (optional) is set true when max_out is exceeded — the returned
 * contour is then partial and the caller should treat it as a content error. */
uint32_t trace_contour(const uint8_t *binary, uint32_t tw, uint32_t th, Point2D *out, uint32_t max_out, bool *out_overflow);

/* Remove collinear vertices from a closed polygon. */
uint32_t remove_collinear(const Point2D *in, uint32_t n, Point2D *out);

/* Ramer-Douglas-Peucker simplification for a closed polygon. */
uint32_t rdp_simplify(const Point2D *poly, uint32_t n, double epsilon, Point2D *out);

/* --- Polygon predicates and measurements ----------------------------- */

typedef enum {
    NT_POLYGON_VALID = 0,
    NT_POLYGON_INVALID_TOO_FEW_VERTICES,
    NT_POLYGON_INVALID_REPEATED_VERTEX,
    NT_POLYGON_INVALID_SELF_INTERSECTION,
    NT_POLYGON_INVALID_ZERO_AREA,
    NT_POLYGON_INVALID_WINDING,
    NT_POLYGON_INVALID_COORD_RANGE,
} nt_polygon_validity_t;

typedef struct {
    uint32_t lost_retained_pixels;
    uint32_t extra_covered_pixels;
} nt_polygon_coverage_metrics_t;

enum {
    NT_POLYGON_MAX_VERTICES = 16,
    NT_POLYGON_MAX_TRIANGLE_INDICES = (NT_POLYGON_MAX_VERTICES - 2) * 3,
};

typedef struct {
    bool bounds_valid;
    bool topology_valid;
    bool coverage_valid;
    bool triangulation_valid;
    bool valid;
    uint32_t retained_cell_count;
    uint32_t lost_retained_cell_count;
    uint32_t triangle_index_count;
    uint64_t retained_area2;
    uint64_t polygon_area2;
    uint64_t lost_area2;
    uint16_t triangle_indices[NT_POLYGON_MAX_TRIANGLE_INDICES];
} nt_polygon_feasibility_t;

typedef struct {
    bool inputs_valid;
    bool opaque_area_valid;
    bool base_bounds_valid;
    bool base_topology_valid;
    bool base_coverage_valid;
    bool base_triangulation_valid;
    bool base_area_valid;
    bool selected_bounds_valid;
    bool selected_topology_valid;
    bool selected_coverage_valid;
    bool selected_triangulation_valid;
    bool selected_area_valid;
    bool metric_order_valid;
    bool allowance_valid;
    bool ceiling_valid;
    bool valid;
    uint32_t retained_cell_count;
    uint32_t base_vertex_count;
    uint32_t selected_vertex_count;
    uint32_t base_triangle_index_count;
    uint32_t selected_triangle_index_count;
    uint32_t max_vertices;
    float max_added_area_percent;
    uint64_t opaque_area2;
    uint64_t base_area2;
    uint64_t selected_area2;
    uint64_t added_area2;
} nt_selected_geometry_proof_t;

/* Validate a closed CCW ring. Adjacent edges may share only their common endpoint. */
nt_polygon_validity_t polygon_validate(const Point2D *poly, uint32_t count);

/* Count retained centers outside and transparent centers inside the polygon. */
nt_polygon_coverage_metrics_t polygon_coverage_metrics(const Point2D *poly, uint32_t poly_count, const uint8_t *binary, uint32_t tw, uint32_t th);

/* Prove that every retained unit cell is wholly inside or on the polygon. */
bool nt_polygon_covers_retained_cells(const Point2D *poly, uint32_t poly_count, const uint8_t *binary, uint32_t tw, uint32_t th, uint32_t *out_retained_cells, uint32_t *out_lost_cells);

/* Validate an existing triangle list against the polygon's exact topology and area. */
bool nt_polygon_triangles_validate(const Point2D *poly, uint32_t poly_count, const uint16_t *indices, uint32_t index_count, uint64_t *out_area2);

/* Triangulate without fallback and publish only a fully validated triangle list. */
bool nt_polygon_triangulate_validated(const Point2D *poly, uint32_t poly_count, uint16_t *out_indices, uint32_t *out_index_count, uint64_t *out_area2);

/* One fail-closed proof for bounds, topology, coverage, and triangulation. */
nt_polygon_feasibility_t nt_polygon_feasibility(const Point2D *poly, uint32_t poly_count, const uint8_t *binary, uint32_t tw, uint32_t th, uint32_t max_vertices);

/* Validate the complete selected-geometry contract without owning any input. */
nt_selected_geometry_proof_t nt_selected_geometry_validate(const uint8_t *binary, uint32_t width, uint32_t height, uint64_t claimed_opaque_area2, const Point2D *base_poly, uint32_t base_count,
                                                           uint64_t claimed_base_area2, const uint16_t *base_indices, uint32_t base_index_count, const Point2D *selected_poly, uint32_t selected_count,
                                                           uint64_t claimed_selected_area2, const uint16_t *selected_indices, uint32_t selected_index_count, float max_added_area_percent,
                                                           uint32_t max_vertices);

bool nt_selected_geometry_proof_equal(const nt_selected_geometry_proof_t *left, const nt_selected_geometry_proof_t *right);

/* Ray-casting point-in-polygon test (even-odd rule). */
bool point_in_polygon(const Point2D *poly, uint32_t n, Point2D p);

/* Float-coord point-in-polygon — used to test pixel centers (px+0.5, py+0.5)
 * against an integer polygon. Avoids the off-by-one of integer corner tests
 * on polygon boundaries. */
bool point_in_polygon_f(const Point2D *poly, uint32_t n, double px, double py);

/* Max distance from any opaque pixel center outside the polygon to the
 * polygon boundary. Returns 0 if every opaque pixel center is inside. */
double polygon_max_outside_pixel_distance(const Point2D *poly, uint32_t poly_count, const uint8_t *binary, uint32_t tw, uint32_t th);

/* Polygon area in pixels (absolute value). */
uint64_t polygon_area_pixels(const Point2D *poly, uint32_t count);

uint64_t polygon_abs_twice_area(const Point2D *poly, uint32_t count);

/* --- Polygon inflation (Clipper2) ------------------------------------ */

/* Inflate a polygon by `amount` pixels using miter joins. Output buffer
 * must hold at least max(n, 32) entries — Clipper2 may add vertices at
 * concave splits. */
uint32_t polygon_inflate(const Point2D *hull, uint32_t n, float amount, Point2D *out);

/* --- D4 dihedral transforms ------------------------------------------ */

/* 3-bit flags encode the 8 symmetries of a rectangle:
 *   bit 0 (1): flip horizontal
 *   bit 1 (2): flip vertical
 *   bit 2 (4): diagonal flip (swap x,y — applied first)
 * Apply order: diagonal -> flipH -> flipV.
 * If (flags & 4) then output dims are (th, tw), else (tw, th). */
void transform_point(int32_t sx, int32_t sy, uint8_t flags, int32_t tw, int32_t th, int32_t *ox, int32_t *oy);

/* Texel-space variant: maps pixel indices (0..w-1) to pixel indices (0..w-1).
 * Same diagonal + flip logic but uses (w-1-x) instead of (w-x). */
void transform_point_texel(int32_t sx, int32_t sy, uint8_t flags, int32_t tw, int32_t th, int32_t *ox, int32_t *oy);

/* Transform polygon vertices. Restores CCW winding if the transform has
 * odd parity (odd number of reflections). */
void polygon_transform(const Point2D *src, uint32_t n, uint8_t flags, int32_t tw, int32_t th, Point2D *out);

/* --- Binary morphology ----------------------------------------------- */

/* 4-connected dilation by one pixel. in and out must be distinct. */
void binary_dilate_4conn(const uint8_t *in, uint8_t *out, uint32_t tw, uint32_t th);

/* 4-connected erosion by one pixel. in and out must be distinct. */
void binary_erode_4conn(const uint8_t *in, uint8_t *out, uint32_t tw, uint32_t th);

/* Count 4-connected opaque components. Caller provides scratch buffers:
 *   visited[tw*th]
 *   stack[tw*th*2]  (int32 xy pairs) */
uint32_t binary_count_components(const uint8_t *M, uint32_t tw, uint32_t th, uint8_t *visited, int32_t *stack);

/* Build a convex polygon enclosing all opaque pixels of the binary mask,
 * simplified down to at most max_vertices. Caller frees returned array. */
Point2D *binary_build_convex_polygon(const uint8_t *binary, uint32_t tw, uint32_t th, uint32_t max_vertices, uint32_t *out_count);

#ifdef __cplusplus
}
#endif

#endif /* NT_BUILDER_ATLAS_GEOMETRY_H */
