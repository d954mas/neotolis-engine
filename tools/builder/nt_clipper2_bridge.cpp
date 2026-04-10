/* Thin C++ bridge to Clipper2 library — exposes flat C API for the builder.
 *
 * Bridge sits beneath nt_builder in the static-lib dep tree (nt_builder links
 * us, not the other way around). Including nt_builder.h here would pull in
 * NT_BUILD_ASSERT, but the handler symbol lives in nt_builder.c which lld-link
 * processes before our .lib — backward symbol resolution doesn't happen on
 * Windows, so the link fails. We deliberately keep the bridge standalone.
 *
 * BRIDGE_ASSERT below mirrors NT_BUILD_ASSERT's observable behavior
 * (always-on fprintf + abort) but without the test handler hook. The hook
 * is only needed for tests that exercise nt_builder assertions directly;
 * our single bridge assert guards a contract we've empirically verified
 * cannot trigger on real inputs, so there's no test that needs to recover
 * from it. If a future Clipper2 upgrade actually breaks the contract,
 * the developer sees a fatal abort in their build, which is exactly the
 * "fail early" behavior AGENTS.md requires. */
#include "nt_clipper2_bridge.h"

#include "clipper2/clipper.h"
#include "clipper2/clipper.minkowski.h"
#include "clipper2/clipper.triangulation.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#define BRIDGE_ASSERT(cond, msg)                                                                                                                                                                       \
    do {                                                                                                                                                                                               \
        if (!(cond)) {                                                                                                                                                                                 \
            std::fprintf(stderr, "FATAL: %s:%d: %s\n", __FILE__, __LINE__, msg);                                                                                                                       \
            std::abort();                                                                                                                                                                              \
        }                                                                                                                                                                                              \
    } while (0)

using namespace Clipper2Lib;

/* Convert int32_t xy pairs to Clipper2 Path64 */
static Path64 to_path64(const int32_t *xy, uint32_t n) {
    Path64 p;
    p.reserve(n);
    for (uint32_t i = 0; i < n; i++) {
        p.emplace_back(xy[i * 2], xy[i * 2 + 1]);
    }
    return p;
}

/* Convert Clipper2 Path64 to heap-allocated int32_t xy pairs */
static uint32_t from_path64(const Path64 &p, int32_t **out) {
    uint32_t n = static_cast<uint32_t>(p.size());
    if (n == 0) {
        *out = nullptr;
        return 0;
    }
    *out = static_cast<int32_t *>(malloc(n * 2 * sizeof(int32_t)));
    for (uint32_t i = 0; i < n; i++) {
        (*out)[i * 2] = static_cast<int32_t>(p[i].x);
        (*out)[i * 2 + 1] = static_cast<int32_t>(p[i].y);
    }
    return n;
}

extern "C" uint32_t nt_clipper2_inflate(const int32_t *xy_in, uint32_t n, double delta, int32_t **xy_out) {
    if (n < 3 || !xy_in || !xy_out)
        return 0;

    Path64 input = to_path64(xy_in, n);
    /* Our polygons are in screen-space (Y-down). Clipper2 uses Cartesian convention
     * where positive area = CCW. If our polygon has negative area (CCW in screen-space
     * = CW in Cartesian), we need to reverse it so inflation works in the expected direction. */
    if (Area(input) < 0) {
        std::reverse(input.begin(), input.end());
    }
    Paths64 result = InflatePaths({input}, delta, JoinType::Miter, EndType::Polygon, 2.0, 0.0);

    if (result.empty())
        return 0;

    /* Pick the largest path (by area) — inflation of a single polygon should give one path */
    size_t best = 0;
    double best_area = 0;
    for (size_t i = 0; i < result.size(); i++) {
        double a = std::abs(Area(result[i]));
        if (a > best_area) {
            best_area = a;
            best = i;
        }
    }
    return from_path64(result[best], xy_out);
}

extern "C" uint32_t nt_clipper2_triangulate(const int32_t *xy_in, uint32_t n, uint16_t **indices_out) {
    if (n < 3 || !xy_in || !indices_out)
        return 0;

    Path64 input = to_path64(xy_in, n);
    Paths64 triangles;
    TriangulateResult res = Triangulate({input}, triangles, true /* use Delaunay */);
    if (res != TriangulateResult::success || triangles.empty())
        return 0;

    uint32_t tri_count = static_cast<uint32_t>(triangles.size());

    /* Build vertex-to-index lookup from original vertices.
     *
     * Invariant: Clipper2 Triangulate NEVER inserts Steiner points — every
     * output triangle vertex must equal one of the input vertices. Verified
     * by reading deps/clipper2/CPP/Clipper2Lib/src/clipper.triangulation.cpp:
     *
     *   - AddPath() is the only place that creates Vertex2 objects, and it
     *     copies path[i] coordinates directly (line ~1101).
     *   - RemoveIntersection() snaps edge intersections to the closest of
     *     the four existing edge endpoints, bails out with paths_intersect
     *     if the closest endpoint is further than 1.0 unit (line ~529).
     *   - SplitEdge() reuses shortE->vT (an existing vertex) as the split
     *     point, never creates a new Vertex2 (line ~592).
     *   - MergeDupOrCollinearVertices() explicitly comments "this procedure
     *     may add new edges ... but it won't add or delete vertices".
     *
     * All our inputs are int32 Point2D copies (pipeline_geometry produces
     * them from contour tracing, RDP simplification, or Clipper2 inflate).
     * int32 → Path64 (Point64 constructor) → Triangulate → back to int64
     * is a lossless round-trip because the values never leave int32 range.
     *
     * Empirical check: bigatlas (4812 sprites, 2236 unique concave shapes,
     * ~2M Clipper2 calls) never produced an unmatched vertex.
     *
     * If a future Clipper2 upgrade breaks this contract, the NT_BUILD_ASSERT
     * below catches it at the first triangulate call — always-on, never
     * compiled out. The cost is one comparison per triangle vertex in a
     * code path already bounded by (max_vertices-2)*3 ≤ 42 indices per
     * region. Noise level, worth the safety. */
    *indices_out = static_cast<uint16_t *>(malloc(tri_count * 3 * sizeof(uint16_t)));

    for (uint32_t t = 0; t < tri_count; t++) {
        for (int v = 0; v < 3; v++) {
            int64_t tx = triangles[t][v].x;
            int64_t ty = triangles[t][v].y;
            /* Find matching input vertex */
            uint16_t idx = 0;
            bool found = false;
            for (uint32_t k = 0; k < n; k++) {
                if (xy_in[k * 2] == static_cast<int32_t>(tx) && xy_in[k * 2 + 1] == static_cast<int32_t>(ty)) {
                    idx = static_cast<uint16_t>(k);
                    found = true;
                    break;
                }
            }
            BRIDGE_ASSERT(found, "clipper2 Triangulate returned a vertex not in input -- Steiner point inserted, contract broken (likely clipper2 upgrade regression)");
            (*indices_out)[t * 3 + v] = idx;
        }
    }
    return tri_count;
}

extern "C" uint32_t nt_clipper2_minkowski_nfp(const int32_t *pattern_xy, uint32_t np, const int32_t *path_xy, uint32_t n, int32_t **verts_out, uint32_t **ring_lengths_out, uint32_t *ring_count_out) {
    if (verts_out)
        *verts_out = nullptr;
    if (ring_lengths_out)
        *ring_lengths_out = nullptr;
    if (ring_count_out)
        *ring_count_out = 0;
    if (np < 3 || n < 3 || !pattern_xy || !path_xy || !verts_out || !ring_lengths_out || !ring_count_out)
        return 0;

    Path64 pattern = to_path64(pattern_xy, np);
    Path64 path = to_path64(path_xy, n);

    /* Normalize to Cartesian CCW (positive area) — required for MinkowskiSum + Union. */
    if (Area(pattern) < 0)
        std::reverse(pattern.begin(), pattern.end());
    if (Area(path) < 0)
        std::reverse(path.begin(), path.end());

    /* MinkowskiSum returns convolution loops for concave inputs. To get the actual
     * NFP boundary, union them with NonZero fill rule. May have multiple disjoint
     * forbidden zones (rings) for concave inputs.
     * Optimization: for convex inputs, MinkowskiSum produces exactly one convolution
     * loop — skip the expensive Union step in that case. */
    Paths64 convolution = MinkowskiSum(pattern, path, true /* is_closed */);
    if (convolution.empty())
        return 0;

    const Paths64 &nfp = (convolution.size() == 1) ? convolution : Union(convolution, FillRule::NonZero);
    if (nfp.empty())
        return 0;

    uint32_t total_verts = 0;
    for (const Path64 &ring : nfp) {
        total_verts += static_cast<uint32_t>(ring.size());
    }
    if (total_verts == 0)
        return 0;

    auto *verts = static_cast<int32_t *>(malloc(total_verts * 2 * sizeof(int32_t)));
    auto *ring_lengths = static_cast<uint32_t *>(malloc(nfp.size() * sizeof(uint32_t)));
    if (!verts || !ring_lengths) {
        free(verts);
        free(ring_lengths);
        return 0;
    }

    uint32_t v_cursor = 0;
    for (size_t r = 0; r < nfp.size(); r++) {
        const Path64 &ring = nfp[r];
        ring_lengths[r] = static_cast<uint32_t>(ring.size());
        for (size_t i = 0; i < ring.size(); i++) {
            verts[v_cursor * 2] = static_cast<int32_t>(ring[i].x);
            verts[(v_cursor * 2) + 1] = static_cast<int32_t>(ring[i].y);
            v_cursor++;
        }
    }

    *verts_out = verts;
    *ring_lengths_out = ring_lengths;
    *ring_count_out = static_cast<uint32_t>(nfp.size());
    return total_verts;
}
