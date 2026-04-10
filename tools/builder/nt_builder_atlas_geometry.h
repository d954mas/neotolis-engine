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

/* Min-area vertex removal simplification of a convex polygon.
 * Result still contains the original polygon (every removal grows area). */
uint32_t hull_simplify(const Point2D *hull, uint32_t n, uint32_t max_vertices, Point2D *out);

/* Greedy perpendicular-distance simplification. Always produces exactly
 * min(n, max_vertices) vertices. out_max_dev returns the largest seen
 * perp distance during removals (coarse upper bound). */
uint32_t hull_simplify_perp(const Point2D *hull, uint32_t n, uint32_t max_vertices, Point2D *out, double *out_max_dev);

/* --- Triangulation --------------------------------------------------- */

/* Fan triangulation from vertex 0. Returns triangle count. */
uint32_t fan_triangulate(uint32_t vertex_count, uint16_t *indices);

/* Triangulate via Clipper2 CDT, fall back to fan on failure.
 * Returns triangle count; indices holds tri_count*3 local indices. */
uint32_t ear_clip_triangulate(const Point2D *poly, uint32_t n, uint16_t *indices);

/* --- Contour tracing and polygon simplification ---------------------- */

/* Trace outer boundary of opaque pixels (CCW). Returns vertex count. */
uint32_t trace_contour(const uint8_t *binary, uint32_t tw, uint32_t th, Point2D *out, uint32_t max_out);

/* Remove collinear vertices from a closed polygon. */
uint32_t remove_collinear(const Point2D *in, uint32_t n, Point2D *out);

/* Ramer-Douglas-Peucker simplification for a closed polygon. */
uint32_t rdp_simplify(const Point2D *poly, uint32_t n, double epsilon, Point2D *out);

/* --- Polygon predicates and measurements ----------------------------- */

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
