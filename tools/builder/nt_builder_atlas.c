/* clang-format off */
#include "nt_builder_internal.h"
#include "hash/nt_hash.h"
#include "nt_atlas_format.h"
/* clang-format on */

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* --- Internal point type for geometry operations --- */

typedef struct {
    int32_t x, y;
} Point2D;

/* --- Alpha trim: find tight bounding box of non-transparent pixels --- */

static bool alpha_trim(const uint8_t *rgba, uint32_t w, uint32_t h, uint8_t threshold, uint32_t *out_x, uint32_t *out_y, uint32_t *out_w, uint32_t *out_h) {
    uint32_t min_x = w;
    uint32_t min_y = h;
    uint32_t max_x = 0;
    uint32_t max_y = 0;
    bool found = false;

    for (uint32_t y = 0; y < h; y++) {
        for (uint32_t x = 0; x < w; x++) {
            uint8_t alpha = rgba[(((y * w) + x) * 4) + 3];
            if (alpha >= threshold) {
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

/* --- Perpendicular distance from point to line segment (squared) --- */

static double perp_distance_sq(Point2D p, Point2D a, Point2D b) {
    double dx = (double)(b.x - a.x);
    double dy = (double)(b.y - a.y);
    double len_sq = (dx * dx) + (dy * dy);
    if (len_sq < 1e-12) {
        /* Degenerate segment: distance to point a */
        double px = (double)(p.x - a.x);
        double py = (double)(p.y - a.y);
        return (px * px) + (py * py);
    }
    /* Cross product gives area of parallelogram; divide by segment length for distance */
    double cross = (dx * (double)(p.y - a.y)) - (dy * (double)(p.x - a.x));
    return (cross * cross) / len_sq;
}

/* --- RDP simplification: iteratively remove vertex with smallest distance --- */

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static uint32_t rdp_simplify(const Point2D *hull, uint32_t n, uint32_t max_vertices, Point2D *out) {
    if (n <= max_vertices) {
        memcpy(out, hull, n * sizeof(Point2D));
        return n;
    }

    /* Boolean keep[] array -- start with all vertices kept */
    bool *keep = (bool *)malloc(n * sizeof(bool));
    NT_BUILD_ASSERT(keep && "rdp_simplify: alloc failed");
    for (uint32_t i = 0; i < n; i++) {
        keep[i] = true;
    }

    uint32_t current_count = n;

    // #region Iterative vertex removal
    while (current_count > max_vertices) {
        /* Find the vertex with smallest perpendicular distance to its neighbor edge */
        double min_dist = 1e30;
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
                continue; /* shouldn't happen with count > 2 */
            }

            double dist = perp_distance_sq(hull[i], hull[prev], hull[next]);
            if (dist < min_dist) {
                min_dist = dist;
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
#define ATLAS_MAX_PAGES 16

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

/* Binary mask for collision detection */
typedef struct {
    uint8_t *bits;      /* 1 bit per pixel, row-major */
    uint32_t width;     /* atlas page width */
    uint32_t height;    /* atlas page height */
    uint32_t row_bytes; /* bytes per row: (width + 7) / 8 */
} AtlasMask;

/* Sort entry for area-descending sort (ATLAS-03) */
typedef struct {
    uint32_t index; /* index into sprites[] */
    uint32_t area;  /* trimmed_w * trimmed_h */
} AreaSortEntry;

/* --- Mask functions --- */

static AtlasMask mask_create(uint32_t w, uint32_t h) {
    AtlasMask m;
    m.width = w;
    m.height = h;
    m.row_bytes = (w + 7) / 8;
    size_t total = (size_t)m.row_bytes * h;
    m.bits = (uint8_t *)calloc(1, total);
    NT_BUILD_ASSERT(m.bits && "mask_create: alloc failed");
    return m;
}

static void mask_free(AtlasMask *m) {
    free(m->bits);
    m->bits = NULL;
    m->width = 0;
    m->height = 0;
    m->row_bytes = 0;
}

static void mask_set_rect(AtlasMask *m, uint32_t rx, uint32_t ry, uint32_t rw, uint32_t rh) {
    for (uint32_t y = ry; y < ry + rh && y < m->height; y++) {
        for (uint32_t x = rx; x < rx + rw && x < m->width; x++) {
            m->bits[y * m->row_bytes + x / 8] |= (uint8_t)(1u << (x % 8));
        }
    }
}

/* Test if any bit in rectangle is set. Returns true on collision.
 * out_skip_x: if collision found, set to the x position after the colliding bit (skip-ahead, ATLAS-02). */
static bool mask_test_rect(const AtlasMask *m, uint32_t rx, uint32_t ry, uint32_t rw, uint32_t rh, uint32_t *out_skip_x) {
    for (uint32_t y = ry; y < ry + rh && y < m->height; y++) {
        for (uint32_t x = rx; x < rx + rw && x < m->width; x++) {
            if ((m->bits[y * m->row_bytes + x / 8] >> (x % 8)) & 1) {
                /* Skip-ahead: find the end of the occupied run on this row */
                if (out_skip_x) {
                    uint32_t skip = x + 1;
                    while (skip < m->width && ((m->bits[y * m->row_bytes + skip / 8] >> (skip % 8)) & 1)) {
                        skip++;
                    }
                    *out_skip_x = skip;
                }
                return true;
            }
        }
    }
    return false;
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

/* --- Tile packer (ATLAS-02, ATLAS-03, ATLAS-04, ATLAS-18) --- */

/* Used by end_atlas (implemented below) */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
__attribute__((unused)) static uint32_t tile_pack(const uint32_t *trim_w, const uint32_t *trim_h, uint32_t sprite_count, const nt_atlas_opts_t *opts, AtlasPlacement *out_placements,
                                                  uint32_t *out_page_count, uint32_t *out_page_w, uint32_t *out_page_h) {
    uint32_t max_size = opts->max_size;
    uint32_t padding = opts->padding;
    uint32_t margin = opts->margin;
    uint32_t extrude = opts->extrude;
    bool allow_rotate = opts->allow_rotate;
    bool pot = opts->power_of_two;

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
        uint32_t cw = trim_w[i] + 2 * extrude + padding;
        uint32_t ch = trim_h[i] + 2 * extrude + padding;
        total_area += (uint64_t)cw * ch;
        if (cw > max_cell_w) {
            max_cell_w = cw;
        }
        if (ch > max_cell_h) {
            max_cell_h = ch;
        }
    }
    /* Headroom factor 1.2 */
    double side = sqrt((double)total_area * 1.2);
    uint32_t init_dim = (uint32_t)(side + 0.5);
    /* Clamp to at least the largest cell dimension + margins */
    uint32_t min_dim = (max_cell_w > max_cell_h ? max_cell_w : max_cell_h) + 2 * margin;
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

    // #region Pack sprites onto pages
    AtlasMask masks[ATLAS_MAX_PAGES];
    uint32_t page_count = 1;
    uint32_t page_w[ATLAS_MAX_PAGES];
    uint32_t page_h[ATLAS_MAX_PAGES];
    /* Track bounding box per page for final sizing */
    uint32_t page_max_x[ATLAS_MAX_PAGES];
    uint32_t page_max_y[ATLAS_MAX_PAGES];
    memset(page_max_x, 0, sizeof(page_max_x));
    memset(page_max_y, 0, sizeof(page_max_y));

    /* Initialize page dimensions to initial estimate */
    for (uint32_t p = 0; p < ATLAS_MAX_PAGES; p++) {
        page_w[p] = init_dim;
        page_h[p] = init_dim;
    }
    masks[0] = mask_create(init_dim, init_dim);

    uint32_t placement_count = 0;

    for (uint32_t si = 0; si < sprite_count; si++) {
        uint32_t idx = sorted[si].index;
        uint32_t tw = trim_w[idx];
        uint32_t th = trim_h[idx];

        /* Validate that each sprite can fit within max_size (Pitfall 4) */
        uint32_t min_cell = tw + 2 * extrude + padding + 2 * margin;
        uint32_t min_cell_h = th + 2 * extrude + padding + 2 * margin;
        NT_BUILD_ASSERT(min_cell <= max_size && min_cell_h <= max_size && "sprite too large for max_size");

        /* Try rotations (ATLAS-04) */
        uint32_t rot_count = allow_rotate ? 4 : 1;
        bool placed = false;
        uint32_t best_page = 0;
        uint32_t best_x = UINT32_MAX;
        uint32_t best_y = UINT32_MAX;
        uint8_t best_rot = 0;
        uint32_t best_cw = 0;
        uint32_t best_ch = 0;

        for (uint32_t pi = 0; pi < page_count && !placed; pi++) {
            for (uint32_t r = 0; r < rot_count; r++) {
                uint32_t cw, ch;
                if (r == 0 || r == 2) {
                    /* 0 or 180: no dimension swap */
                    cw = tw + 2 * extrude + padding;
                    ch = th + 2 * extrude + padding;
                } else {
                    /* 90 or 270: swap dimensions */
                    cw = th + 2 * extrude + padding;
                    ch = tw + 2 * extrude + padding;
                }

                uint32_t scan_max_x = page_w[pi] - margin;
                uint32_t scan_max_y = page_h[pi] - margin;

                /* Scan placement positions left-to-right, top-to-bottom */
                for (uint32_t sy = margin; sy + ch <= scan_max_y; sy++) {
                    for (uint32_t sx = margin; sx + cw <= scan_max_x;) {
                        uint32_t skip_x = 0;
                        if (!mask_test_rect(&masks[pi], sx, sy, cw, ch, &skip_x)) {
                            /* Found valid placement -- check if better than current best */
                            if (sy < best_y || (sy == best_y && sx < best_x)) {
                                best_page = pi;
                                best_x = sx;
                                best_y = sy;
                                best_rot = (uint8_t)r;
                                best_cw = cw;
                                best_ch = ch;
                                placed = true;
                            }
                            break; /* Found placement for this rotation, try next */
                        }
                        /* Skip-ahead (ATLAS-02) */
                        if (skip_x > sx + 1) {
                            sx = skip_x;
                        } else {
                            sx++;
                        }
                    }
                    if (placed) {
                        break;
                    }
                }
            }
        }

        // #region Multi-page overflow (ATLAS-18)
        if (!placed) {
            NT_BUILD_ASSERT(page_count < ATLAS_MAX_PAGES && "atlas: too many pages");
            uint32_t new_page = page_count;
            page_count++;
            masks[new_page] = mask_create(init_dim, init_dim);
            page_w[new_page] = init_dim;
            page_h[new_page] = init_dim;

            /* Place on new page at (margin, margin) with no rotation */
            uint32_t cw = tw + 2 * extrude + padding;
            uint32_t ch = th + 2 * extrude + padding;
            best_page = new_page;
            best_x = margin;
            best_y = margin;
            best_rot = 0;
            best_cw = cw;
            best_ch = ch;
            placed = true;
        }
        // #endregion

        NT_BUILD_ASSERT(placed && "tile_pack: failed to place sprite");

        /* Mark mask */
        mask_set_rect(&masks[best_page], best_x, best_y, best_cw, best_ch);

        /* Track bounding box */
        uint32_t right = best_x + best_cw;
        uint32_t bottom = best_y + best_ch;
        if (right > page_max_x[best_page]) {
            page_max_x[best_page] = right;
        }
        if (bottom > page_max_y[best_page]) {
            page_max_y[best_page] = bottom;
        }

        /* Record placement */
        AtlasPlacement *pl = &out_placements[placement_count++];
        pl->sprite_index = idx;
        pl->page = best_page;
        pl->x = best_x;
        pl->y = best_y;
        pl->trimmed_w = tw;
        pl->trimmed_h = th;
        pl->trim_x = 0; /* Caller fills in trim offsets */
        pl->trim_y = 0;
        pl->rotation = best_rot;
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
        /* Clamp to max_size */
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
        mask_free(&masks[p]);
    }
    free(sorted);

    *out_page_count = page_count;
    return placement_count;
}

/* --- Test-access wrappers (geometry algorithms are static, tests call these) --- */

#ifdef NT_BUILDER_ATLAS_TEST_ACCESS
bool nt_atlas_test_alpha_trim(const uint8_t *rgba, uint32_t w, uint32_t h, uint8_t threshold, uint32_t *ox, uint32_t *oy, uint32_t *ow, uint32_t *oh) {
    return alpha_trim(rgba, w, h, threshold, ox, oy, ow, oh);
}
uint32_t nt_atlas_test_convex_hull(const void *pts, uint32_t n, void *out) { return convex_hull((const Point2D *)pts, n, (Point2D *)out); }
uint32_t nt_atlas_test_rdp_simplify(const void *hull, uint32_t n, uint32_t max_v, void *out) { return rdp_simplify((const Point2D *)hull, n, max_v, (Point2D *)out); }
uint32_t nt_atlas_test_fan_triangulate(uint32_t vc, uint16_t *idx) { return fan_triangulate(vc, idx); }
#endif

/* --- Atlas API stubs (fully implemented in Plan 02) --- */

void nt_builder_begin_atlas(NtBuilderContext *ctx, const char *name, const nt_atlas_opts_t *opts) {
    (void)ctx;
    (void)name;
    (void)opts;
    NT_BUILD_ASSERT(0 && "nt_builder_begin_atlas: not yet implemented");
}

void nt_builder_atlas_add(NtBuilderContext *ctx, const char *path, const char *name_override) {
    (void)ctx;
    (void)path;
    (void)name_override;
    NT_BUILD_ASSERT(0 && "nt_builder_atlas_add: not yet implemented");
}

void nt_builder_atlas_add_raw(NtBuilderContext *ctx, const uint8_t *rgba_pixels, uint32_t width, uint32_t height, const char *name) {
    (void)ctx;
    (void)rgba_pixels;
    (void)width;
    (void)height;
    (void)name;
    NT_BUILD_ASSERT(0 && "nt_builder_atlas_add_raw: not yet implemented");
}

void nt_builder_atlas_add_glob(NtBuilderContext *ctx, const char *pattern) {
    (void)ctx;
    (void)pattern;
    NT_BUILD_ASSERT(0 && "nt_builder_atlas_add_glob: not yet implemented");
}

void nt_builder_end_atlas(NtBuilderContext *ctx) {
    (void)ctx;
    NT_BUILD_ASSERT(0 && "nt_builder_end_atlas: not yet implemented");
}
