#ifndef NT_CLIPPER2_BRIDGE_H
#define NT_CLIPPER2_BRIDGE_H

/*
 * Thin C API bridge to Clipper2 (C++17 library).
 * Provides polygon inflate, CDT triangulation, and Minkowski sum
 * using flat int32_t arrays (our Point2D-compatible format).
 *
 * All returned arrays are heap-allocated. Caller must free() them.
 */

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* --- Polygon inflate/deflate ---
 * Inflates a closed polygon by 'delta' pixels using miter joins.
 * Handles concave corners correctly (unlike naive edge-offset).
 * Input:  xy pairs as int32_t[n*2] = {x0,y0, x1,y1, ...}
 * Output: xy pairs as int32_t[out_n*2], heap-allocated. Caller frees.
 * Returns number of vertices in output polygon (0 on failure). */
uint32_t nt_clipper2_inflate(const int32_t *xy_in, uint32_t n, double delta, int32_t **xy_out);

/* --- Constrained Delaunay Triangulation ---
 * Triangulates a simple polygon (convex or concave).
 * Input:  xy pairs as int32_t[n*2]
 * Output: triangle indices as uint16_t[tri_count*3], heap-allocated. Caller frees.
 *         Indices are local (0..n-1).
 * Returns number of triangles (0 on failure). */
uint32_t nt_clipper2_triangulate(const int32_t *xy_in, uint32_t n, uint16_t **indices_out);

/* --- Minkowski Sum ---
 * Computes Minkowski sum of two polygons (handles concave).
 * Result may be multiple paths (union of convolutions).
 * Input:  pattern xy[np*2], path xy[n*2]
 * Output: single merged polygon xy[out_n*2], heap-allocated. Caller frees.
 *         (Takes the largest path from the result.)
 * Returns number of vertices in output (0 on failure). */
uint32_t nt_clipper2_minkowski_sum(const int32_t *pattern_xy, uint32_t np, const int32_t *path_xy, uint32_t n, int32_t **xy_out);

#ifdef __cplusplus
}
#endif

#endif /* NT_CLIPPER2_BRIDGE_H */
