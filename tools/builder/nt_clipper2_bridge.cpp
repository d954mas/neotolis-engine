/* Thin C++ bridge to Clipper2 library — exposes flat C API for the builder. */
#include "nt_clipper2_bridge.h"

#include "clipper2/clipper.h"
#include "clipper2/clipper.minkowski.h"
#include "clipper2/clipper.triangulation.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>

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
     * CDT output vertices should match input (integer coordinates). */
    *indices_out = static_cast<uint16_t *>(malloc(tri_count * 3 * sizeof(uint16_t)));

    for (uint32_t t = 0; t < tri_count; t++) {
        for (int v = 0; v < 3; v++) {
            int64_t tx = triangles[t][v].x;
            int64_t ty = triangles[t][v].y;
            /* Find matching input vertex */
            uint16_t idx = 0;
            for (uint32_t k = 0; k < n; k++) {
                if (xy_in[k * 2] == static_cast<int32_t>(tx) && xy_in[k * 2 + 1] == static_cast<int32_t>(ty)) {
                    idx = static_cast<uint16_t>(k);
                    break;
                }
            }
            (*indices_out)[t * 3 + v] = idx;
        }
    }
    return tri_count;
}

extern "C" uint32_t nt_clipper2_minkowski_sum(const int32_t *pattern_xy, uint32_t np, const int32_t *path_xy, uint32_t n, int32_t **xy_out) {
    if (np < 3 || n < 3 || !pattern_xy || !path_xy || !xy_out)
        return 0;

    Path64 pattern = to_path64(pattern_xy, np);
    Path64 path = to_path64(path_xy, n);
    Paths64 result = MinkowskiSum(pattern, path, true /* is_closed */);

    if (result.empty())
        return 0;

    /* Pick the largest path */
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
