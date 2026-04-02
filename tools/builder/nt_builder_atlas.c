/* clang-format off */
#include "nt_builder_internal.h"
#include "hash/nt_hash.h"
#include "nt_atlas_format.h"
#include "stb_image.h"
#include "stb_image_write.h"
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

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static uint32_t tile_pack(const uint32_t *trim_w, const uint32_t *trim_h, uint32_t sprite_count, const nt_atlas_opts_t *opts, AtlasPlacement *out_placements, uint32_t *out_page_count,
                          uint32_t *out_page_w, uint32_t *out_page_h) {
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

/* --- Edge extrude: duplicate border pixels outward (ATLAS-11) --- */

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
                memcpy(&page[((dst_y * page_w) + x) * 4], &page[((py * page_w) + x) * 4], 4);
            }
        }
        /* Bottom edge: duplicate row py+sh-1 to row py+sh-1+e */
        uint32_t src_y = py + sh - 1;
        uint32_t dst_y = src_y + e;
        if (dst_y < page_h) {
            for (uint32_t x = px; x < px + sw && x < page_w; x++) {
                memcpy(&page[((dst_y * page_w) + x) * 4], &page[((src_y * page_w) + x) * 4], 4);
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
                memcpy(&page[((y * page_w) + dst_x) * 4], &page[((y * page_w) + px) * 4], 4);
            }
        }
        /* Right edge */
        uint32_t src_x = px + sw - 1;
        uint32_t dst_x = src_x + e;
        if (dst_x < page_w) {
            for (uint32_t y = y_start; y < y_end; y++) {
                memcpy(&page[((y * page_w) + dst_x) * 4], &page[((y * page_w) + src_x) * 4], 4);
            }
        }
    }
    // #endregion
}

/* --- Debug PNG: draw 2px outline around region (D-09, D-10) --- */

static void debug_draw_rect_outline(uint8_t *page, uint32_t page_w, uint32_t page_h, uint32_t rx, uint32_t ry, uint32_t rw, uint32_t rh) {
    /* Bright magenta outline: 255,0,255,255 */
    static const uint8_t color[4] = {255, 0, 255, 255};
    for (uint32_t t = 0; t < 2; t++) { /* 2px border */
        /* Top edge */
        if (ry + t < page_h) {
            for (uint32_t x = rx; x < rx + rw && x < page_w; x++) {
                memcpy(&page[(((ry + t) * page_w) + x) * 4], color, 4);
            }
        }
        /* Bottom edge */
        uint32_t by = ry + rh - 1 - t;
        if (by < page_h && by >= ry) {
            for (uint32_t x = rx; x < rx + rw && x < page_w; x++) {
                memcpy(&page[((by * page_w) + x) * 4], color, 4);
            }
        }
        /* Left edge */
        if (rx + t < page_w) {
            for (uint32_t y = ry; y < ry + rh && y < page_h; y++) {
                memcpy(&page[((y * page_w) + rx + t) * 4], color, 4);
            }
        }
        /* Right edge */
        uint32_t bx = rx + rw - 1 - t;
        if (bx < page_w && bx >= rx) {
            for (uint32_t y = ry; y < ry + rh && y < page_h; y++) {
                memcpy(&page[((y * page_w) + bx) * 4], color, 4);
            }
        }
    }
}

/* --- Blit trimmed sprite pixels to atlas page --- */

static void blit_sprite(uint8_t *page, uint32_t page_w, const uint8_t *sprite_rgba, uint32_t sprite_w, uint32_t trim_x, uint32_t trim_y, uint32_t trim_w, uint32_t trim_h, uint32_t dest_x,
                        uint32_t dest_y, uint8_t rotation) {
    /* For rotation 0: copy trimmed rect directly.
     * For rotation 1 (90CW): source(x,y) -> dest(trim_h-1-y, x)
     * For rotation 2 (180): source(x,y) -> dest(trim_w-1-x, trim_h-1-y)
     * For rotation 3 (270CW): source(x,y) -> dest(y, trim_w-1-x) */
    for (uint32_t sy = 0; sy < trim_h; sy++) {
        for (uint32_t sx = 0; sx < trim_w; sx++) {
            const uint8_t *src = &sprite_rgba[(((trim_y + sy) * sprite_w) + trim_x + sx) * 4];
            uint32_t dx, dy;
            switch (rotation) {
            case 0:
                dx = dest_x + sx;
                dy = dest_y + sy;
                break;
            case 1: /* 90 CW */
                dx = dest_x + (trim_h - 1 - sy);
                dy = dest_y + sx;
                break;
            case 2: /* 180 */
                dx = dest_x + (trim_w - 1 - sx);
                dy = dest_y + (trim_h - 1 - sy);
                break;
            case 3: /* 270 CW */
                dx = dest_x + sy;
                dy = dest_y + (trim_w - 1 - sx);
                break;
            default:
                dx = dest_x + sx;
                dy = dest_y + sy;
                break;
            }
            memcpy(&page[((dy * page_w) + dx) * 4], src, 4);
        }
    }
}

/* --- Atlas API --- */

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

    /* Initialize sprite array */
    state->sprite_capacity = 64;
    state->sprites = (NtAtlasSpriteInput *)calloc(state->sprite_capacity, sizeof(NtAtlasSpriteInput));
    NT_BUILD_ASSERT(state->sprites && "begin_atlas: alloc failed");
    state->sprite_count = 0;

    ctx->active_atlas = state;
}

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
    int w = 0, h = 0, channels = 0;
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

void nt_builder_atlas_add_glob(NtBuilderContext *ctx, const char *pattern) {
    NT_BUILD_ASSERT(ctx && pattern && "atlas_add_glob: invalid args");
    NT_BUILD_ASSERT(ctx->active_atlas && "atlas_add_glob: no active atlas");

    AtlasGlobData data = {.ctx = ctx, .match_count = 0};
    NT_BUILD_ASSERT(nt_builder_glob_iterate(pattern, atlas_glob_callback, &data) && "atlas_add_glob: glob overflow");
    NT_BUILD_ASSERT(data.match_count > 0 && "atlas_add_glob: no files matched pattern");
}

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

/* --- end_atlas: the main atlas pipeline --- */

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void nt_builder_end_atlas(NtBuilderContext *ctx) {
    NT_BUILD_ASSERT(ctx && "end_atlas: ctx is NULL");
    NT_BUILD_ASSERT(ctx->active_atlas && "end_atlas: no active atlas");

    NtBuildAtlasState *state = ctx->active_atlas;
    NT_BUILD_ASSERT(state->sprite_count > 0 && "end_atlas: atlas has no sprites");

    uint32_t sprite_count = state->sprite_count;
    NtAtlasSpriteInput *sprites = state->sprites;
    const nt_atlas_opts_t *opts = &state->opts;

    // #region Step 1: Alpha-trim all sprites (ATLAS-05)
    uint32_t *trim_x = (uint32_t *)calloc(sprite_count, sizeof(uint32_t));
    uint32_t *trim_y = (uint32_t *)calloc(sprite_count, sizeof(uint32_t));
    uint32_t *trim_w = (uint32_t *)calloc(sprite_count, sizeof(uint32_t));
    uint32_t *trim_h = (uint32_t *)calloc(sprite_count, sizeof(uint32_t));
    NT_BUILD_ASSERT(trim_x && trim_y && trim_w && trim_h && "end_atlas: alloc failed");

    for (uint32_t i = 0; i < sprite_count; i++) {
        bool has_pixels = alpha_trim(sprites[i].rgba, sprites[i].width, sprites[i].height, opts->alpha_threshold, &trim_x[i], &trim_y[i], &trim_w[i], &trim_h[i]);
        NT_BUILD_ASSERT(has_pixels && "end_atlas: sprite is fully transparent");
    }
    // #endregion

    // #region Step 2: Duplicate detection (ATLAS-12)
    DedupSortEntry *dedup_entries = (DedupSortEntry *)malloc(sprite_count * sizeof(DedupSortEntry));
    NT_BUILD_ASSERT(dedup_entries && "end_atlas: alloc failed");
    for (uint32_t i = 0; i < sprite_count; i++) {
        dedup_entries[i].index = i;
        dedup_entries[i].hash = sprites[i].decoded_hash;
    }
    qsort(dedup_entries, sprite_count, sizeof(DedupSortEntry), dedup_sort_cmp);

    /* Map duplicate -> original. -1 = unique. */
    int32_t *dedup_map = (int32_t *)malloc(sprite_count * sizeof(int32_t));
    NT_BUILD_ASSERT(dedup_map && "end_atlas: alloc failed");
    for (uint32_t i = 0; i < sprite_count; i++) {
        dedup_map[i] = -1;
    }

    for (uint32_t i = 1; i < sprite_count; i++) {
        if (dedup_entries[i].hash == dedup_entries[i - 1].hash) {
            uint32_t curr_idx = dedup_entries[i].index;
            uint32_t prev_idx = dedup_entries[i - 1].index;
            /* Find the original (follow chain) */
            uint32_t orig = prev_idx;
            while (dedup_map[orig] >= 0) {
                orig = (uint32_t)dedup_map[orig];
            }
            /* Verify trimmed pixels match */
            if (trim_w[curr_idx] == trim_w[orig] && trim_h[curr_idx] == trim_h[orig]) {
                dedup_map[curr_idx] = (int32_t)orig;
            }
        }
    }
    free(dedup_entries);

    /* Count unique sprites */
    uint32_t unique_count = 0;
    uint32_t *unique_indices = (uint32_t *)malloc(sprite_count * sizeof(uint32_t));
    NT_BUILD_ASSERT(unique_indices && "end_atlas: alloc failed");
    for (uint32_t i = 0; i < sprite_count; i++) {
        if (dedup_map[i] < 0) {
            unique_indices[unique_count++] = i;
        }
    }
    // #endregion

    // #region Step 3: Geometry (convex hull + simplification) (ATLAS-06, ATLAS-07)
    /* For each unique sprite, compute hull vertices */
    uint32_t *vertex_counts = (uint32_t *)calloc(sprite_count, sizeof(uint32_t));
    Point2D **hull_vertices = (Point2D **)calloc(sprite_count, sizeof(Point2D *));
    NT_BUILD_ASSERT(vertex_counts && hull_vertices && "end_atlas: alloc failed");

    for (uint32_t ui = 0; ui < unique_count; ui++) {
        uint32_t idx = unique_indices[ui];
        uint32_t tw = trim_w[idx];
        uint32_t th = trim_h[idx];

        if (opts->polygon_mode) {
            /* Extract boundary pixels from alpha mask */
            uint32_t pt_count = 0;
            Point2D *pts = (Point2D *)malloc(tw * th * sizeof(Point2D)); /* worst case */
            NT_BUILD_ASSERT(pts && "end_atlas: alloc failed");

            for (uint32_t y = 0; y < th; y++) {
                for (uint32_t x = 0; x < tw; x++) {
                    uint8_t alpha = sprites[idx].rgba[(((trim_y[idx] + y) * sprites[idx].width) + trim_x[idx] + x) * 4 + 3];
                    if (alpha >= opts->alpha_threshold) {
                        /* Check if this is a boundary pixel */
                        bool is_boundary = (x == 0 || y == 0 || x == tw - 1 || y == th - 1);
                        if (!is_boundary) {
                            /* Check 4 neighbors */
                            uint8_t left = sprites[idx].rgba[(((trim_y[idx] + y) * sprites[idx].width) + trim_x[idx] + x - 1) * 4 + 3];
                            uint8_t right = sprites[idx].rgba[(((trim_y[idx] + y) * sprites[idx].width) + trim_x[idx] + x + 1) * 4 + 3];
                            uint8_t up = sprites[idx].rgba[(((trim_y[idx] + y - 1) * sprites[idx].width) + trim_x[idx] + x) * 4 + 3];
                            uint8_t down = sprites[idx].rgba[(((trim_y[idx] + y + 1) * sprites[idx].width) + trim_x[idx] + x) * 4 + 3];
                            is_boundary = (left < opts->alpha_threshold || right < opts->alpha_threshold || up < opts->alpha_threshold || down < opts->alpha_threshold);
                        }
                        if (is_boundary) {
                            pts[pt_count].x = (int32_t)x;
                            pts[pt_count].y = (int32_t)y;
                            pt_count++;
                        }
                    }
                }
            }

            if (pt_count < 3) {
                /* Degenerate: use rect */
                free(pts);
                hull_vertices[idx] = (Point2D *)malloc(4 * sizeof(Point2D));
                NT_BUILD_ASSERT(hull_vertices[idx] && "end_atlas: alloc failed");
                hull_vertices[idx][0] = (Point2D){0, 0};
                hull_vertices[idx][1] = (Point2D){(int32_t)tw, 0};
                hull_vertices[idx][2] = (Point2D){(int32_t)tw, (int32_t)th};
                hull_vertices[idx][3] = (Point2D){0, (int32_t)th};
                vertex_counts[idx] = 4;
            } else {
                /* Compute convex hull */
                Point2D *hull = (Point2D *)malloc(pt_count * 2 * sizeof(Point2D));
                NT_BUILD_ASSERT(hull && "end_atlas: alloc failed");
                uint32_t hull_count = convex_hull(pts, pt_count, hull);
                free(pts);

                /* Simplify to max_vertices */
                Point2D *simplified = (Point2D *)malloc(hull_count * sizeof(Point2D));
                NT_BUILD_ASSERT(simplified && "end_atlas: alloc failed");
                uint32_t simp_count = rdp_simplify(hull, hull_count, opts->max_vertices, simplified);
                free(hull);

                hull_vertices[idx] = simplified;
                vertex_counts[idx] = simp_count;
            }
        } else {
            /* Rect mode: 4-vertex rect */
            hull_vertices[idx] = (Point2D *)malloc(4 * sizeof(Point2D));
            NT_BUILD_ASSERT(hull_vertices[idx] && "end_atlas: alloc failed");
            hull_vertices[idx][0] = (Point2D){0, 0};
            hull_vertices[idx][1] = (Point2D){(int32_t)tw, 0};
            hull_vertices[idx][2] = (Point2D){(int32_t)tw, (int32_t)th};
            hull_vertices[idx][3] = (Point2D){0, (int32_t)th};
            vertex_counts[idx] = 4;
        }
    }

    /* Copy vertex data for duplicates from their originals */
    for (uint32_t i = 0; i < sprite_count; i++) {
        if (dedup_map[i] >= 0) {
            uint32_t orig = (uint32_t)dedup_map[i];
            vertex_counts[i] = vertex_counts[orig];
            hull_vertices[i] = hull_vertices[orig]; /* shared pointer, don't double-free */
        }
    }
    // #endregion

    // #region Step 4: Tile packing
    AtlasPlacement *placements = (AtlasPlacement *)malloc(unique_count * sizeof(AtlasPlacement));
    NT_BUILD_ASSERT(placements && "end_atlas: alloc failed");

    /* Build trim arrays for unique sprites only */
    uint32_t *unique_trim_w = (uint32_t *)malloc(sprite_count * sizeof(uint32_t));
    uint32_t *unique_trim_h = (uint32_t *)malloc(sprite_count * sizeof(uint32_t));
    NT_BUILD_ASSERT(unique_trim_w && unique_trim_h && "end_atlas: alloc failed");
    memcpy(unique_trim_w, trim_w, sprite_count * sizeof(uint32_t));
    memcpy(unique_trim_h, trim_h, sprite_count * sizeof(uint32_t));

    uint32_t page_count = 0;
    uint32_t page_w[ATLAS_MAX_PAGES];
    uint32_t page_h[ATLAS_MAX_PAGES];
    /* tile_pack takes all sprites but only packs unique ones -- we pass unique trim data */
    /* Actually tile_pack takes arrays indexed by sprite_index, so pass full arrays */
    uint32_t placement_count = tile_pack(trim_w, trim_h, unique_count, opts, placements, &page_count, page_w, page_h);

    /* Remap placement sprite_index from unique_indices order to actual sprite indices */
    /* tile_pack's sorted order uses indices 0..unique_count-1, referencing into the trim arrays via sorted[si].index.
     * The placement sprite_index is already the correct index into the original arrays since tile_pack
     * operates on the trim_w/trim_h arrays we provided (full sprite-count sized arrays).
     * However we only passed unique_count sprites... we need to remap. */
    /* Actually let me reconsider: we passed trim_w/trim_h of size sprite_count but only unique_count items.
     * The tile_pack uses sorted[si].index as the index into trim_w/trim_h. So we need to pass
     * only the unique sprite trim data or remap. Let me create proper arrays for unique sprites. */

    /* Re-pack with properly indexed arrays for unique sprites */
    free(unique_trim_w);
    free(unique_trim_h);
    uint32_t *u_trim_w = (uint32_t *)malloc(unique_count * sizeof(uint32_t));
    uint32_t *u_trim_h = (uint32_t *)malloc(unique_count * sizeof(uint32_t));
    NT_BUILD_ASSERT(u_trim_w && u_trim_h && "end_atlas: alloc failed");
    for (uint32_t i = 0; i < unique_count; i++) {
        u_trim_w[i] = trim_w[unique_indices[i]];
        u_trim_h[i] = trim_h[unique_indices[i]];
    }

    placement_count = tile_pack(u_trim_w, u_trim_h, unique_count, opts, placements, &page_count, page_w, page_h);

    /* Fill trim offsets and remap sprite_index back to original */
    for (uint32_t i = 0; i < placement_count; i++) {
        uint32_t unique_idx = placements[i].sprite_index; /* index into unique arrays */
        uint32_t orig_idx = unique_indices[unique_idx];
        placements[i].sprite_index = orig_idx;
        placements[i].trim_x = trim_x[orig_idx];
        placements[i].trim_y = trim_y[orig_idx];
        placements[i].trimmed_w = trim_w[orig_idx];
        placements[i].trimmed_h = trim_h[orig_idx];
    }

    free(u_trim_w);
    free(u_trim_h);
    // #endregion

    // #region Step 5: Compose page RGBA + extrude + debug PNG (ATLAS-15, ATLAS-11, D-09)
    uint8_t **page_pixels = (uint8_t **)calloc(page_count, sizeof(uint8_t *));
    NT_BUILD_ASSERT(page_pixels && "end_atlas: alloc failed");

    for (uint32_t p = 0; p < page_count; p++) {
        page_pixels[p] = (uint8_t *)calloc((size_t)page_w[p] * page_h[p] * 4, 1);
        NT_BUILD_ASSERT(page_pixels[p] && "end_atlas: page alloc failed");
    }

    /* Blit each placed sprite */
    uint32_t extrude_val = opts->extrude;
    for (uint32_t pi = 0; pi < placement_count; pi++) {
        AtlasPlacement *pl = &placements[pi];
        uint32_t idx = pl->sprite_index;
        uint32_t inner_x = pl->x + extrude_val;
        uint32_t inner_y = pl->y + extrude_val;

        /* Blit trimmed pixels */
        blit_sprite(page_pixels[pl->page], page_w[pl->page], sprites[idx].rgba, sprites[idx].width, pl->trim_x, pl->trim_y, pl->trimmed_w, pl->trimmed_h, inner_x, inner_y, pl->rotation);

        /* Extrude edge pixels (ATLAS-11) */
        uint32_t blit_w = (pl->rotation == 1 || pl->rotation == 3) ? pl->trimmed_h : pl->trimmed_w;
        uint32_t blit_h = (pl->rotation == 1 || pl->rotation == 3) ? pl->trimmed_w : pl->trimmed_h;
        extrude_edges(page_pixels[pl->page], page_w[pl->page], page_h[pl->page], inner_x, inner_y, blit_w, blit_h, extrude_val);
    }

    /* Debug PNG output (D-09, D-10) */
    if (opts->debug_png) {
        for (uint32_t p = 0; p < page_count; p++) {
            /* Draw outlines on a copy */
            size_t page_bytes = (size_t)page_w[p] * page_h[p] * 4;
            uint8_t *debug_page = (uint8_t *)malloc(page_bytes);
            NT_BUILD_ASSERT(debug_page && "end_atlas: debug alloc failed");
            memcpy(debug_page, page_pixels[p], page_bytes);

            for (uint32_t pi = 0; pi < placement_count; pi++) {
                if (placements[pi].page != p) {
                    continue;
                }
                uint32_t rx = placements[pi].x + extrude_val;
                uint32_t ry = placements[pi].y + extrude_val;
                uint32_t rw = (placements[pi].rotation == 1 || placements[pi].rotation == 3) ? placements[pi].trimmed_h : placements[pi].trimmed_w;
                uint32_t rh = (placements[pi].rotation == 1 || placements[pi].rotation == 3) ? placements[pi].trimmed_w : placements[pi].trimmed_h;
                debug_draw_rect_outline(debug_page, page_w[p], page_h[p], rx, ry, rw, rh);
            }

            char debug_path[512];
            (void)snprintf(debug_path, sizeof(debug_path), "%s_page%u.png", state->name, p);
            stbi_write_png(debug_path, (int)page_w[p], (int)page_h[p], 4, debug_page, (int)(page_w[p] * 4));
            NT_LOG_INFO("Debug PNG: %s (%ux%u)", debug_path, page_w[p], page_h[p]);
            free(debug_page);
        }
    }
    // #endregion

    // #region Step 6: Compute atlas UVs and serialize metadata (REGION-01)
    /* Count total vertices */
    uint32_t total_vertex_count = 0;
    for (uint32_t i = 0; i < sprite_count; i++) {
        total_vertex_count += vertex_counts[i];
    }

    /* Build placement lookup: original_sprite_index -> placement index */
    /* For duplicates, the lookup points to the original's placement */
    uint32_t *placement_lookup = (uint32_t *)malloc(sprite_count * sizeof(uint32_t));
    NT_BUILD_ASSERT(placement_lookup && "end_atlas: alloc failed");
    memset(placement_lookup, 0xFF, sprite_count * sizeof(uint32_t)); /* UINT32_MAX = unset */

    for (uint32_t pi = 0; pi < placement_count; pi++) {
        placement_lookup[placements[pi].sprite_index] = pi;
    }
    /* Fill duplicate lookups */
    for (uint32_t i = 0; i < sprite_count; i++) {
        if (dedup_map[i] >= 0) {
            uint32_t orig = (uint32_t)dedup_map[i];
            placement_lookup[i] = placement_lookup[orig];
        }
    }

    /* Serialize blob: header + texture_resource_ids + regions + vertices */
    uint32_t regions_offset = (uint32_t)sizeof(NtAtlasHeader) + page_count * (uint32_t)sizeof(uint64_t);
    uint32_t vertex_offset = regions_offset + sprite_count * (uint32_t)sizeof(NtAtlasRegion);
    uint32_t blob_size = vertex_offset + total_vertex_count * (uint32_t)sizeof(NtAtlasVertex);
    uint8_t *blob = (uint8_t *)calloc(1, blob_size);
    NT_BUILD_ASSERT(blob && "end_atlas: blob alloc failed");

    /* Header */
    NtAtlasHeader *hdr = (NtAtlasHeader *)blob;
    hdr->magic = NT_ATLAS_MAGIC;
    hdr->version = NT_ATLAS_VERSION;
    hdr->region_count = (uint16_t)sprite_count;
    hdr->page_count = (uint16_t)page_count;
    hdr->_pad = 0;
    hdr->vertex_offset = vertex_offset;
    hdr->total_vertex_count = total_vertex_count;

    /* Texture resource IDs (D-05) */
    uint64_t *tex_ids = (uint64_t *)(blob + sizeof(NtAtlasHeader));
    for (uint32_t p = 0; p < page_count; p++) {
        char tex_path[512];
        (void)snprintf(tex_path, sizeof(tex_path), "%s/tex%u", state->name, p);
        tex_ids[p] = nt_hash64_str(tex_path).value;
    }

    /* Regions */
    NtAtlasRegion *regions = (NtAtlasRegion *)(blob + regions_offset);
    NtAtlasVertex *vertices = (NtAtlasVertex *)(blob + vertex_offset);
    uint32_t vertex_cursor = 0;

    for (uint32_t i = 0; i < sprite_count; i++) {
        uint32_t pi = placement_lookup[i];
        NT_BUILD_ASSERT(pi != UINT32_MAX && "end_atlas: sprite has no placement");
        AtlasPlacement *pl = &placements[pi];

        NtAtlasRegion *reg = &regions[i];
        reg->name_hash = nt_hash64_str(sprites[i].name).value;
        reg->source_w = (uint16_t)sprites[i].width;
        reg->source_h = (uint16_t)sprites[i].height;
        reg->trim_offset_x = (int16_t)trim_x[i];
        reg->trim_offset_y = (int16_t)trim_y[i];
        reg->origin_x = sprites[i].origin_x;
        reg->origin_y = sprites[i].origin_y;
        reg->vertex_start = (uint16_t)vertex_cursor;
        reg->vertex_count = (uint8_t)vertex_counts[i];
        reg->page_index = (uint8_t)pl->page;
        reg->rotated = pl->rotation;
        memset(reg->_pad, 0, sizeof(reg->_pad));

        /* Write vertices with atlas UVs */
        uint32_t inner_x = pl->x + extrude_val;
        uint32_t inner_y = pl->y + extrude_val;
        uint32_t atlas_w = page_w[pl->page];
        uint32_t atlas_h = page_h[pl->page];

        for (uint32_t v = 0; v < vertex_counts[i]; v++) {
            NtAtlasVertex *vtx = &vertices[vertex_cursor++];
            int32_t lx = hull_vertices[i][v].x;
            int32_t ly = hull_vertices[i][v].y;
            vtx->local_x = (int16_t)lx;
            vtx->local_y = (int16_t)ly;

            /* Transform local position to atlas UV coordinates.
             * Handle rotation: local coords are in trimmed-sprite space.
             * For rotation 0: atlas_px = inner_x + lx
             * For rotation 1 (90CW): atlas_px = inner_x + (trim_h - 1 - ly), atlas_py = inner_y + lx
             * For rotation 2 (180): atlas_px = inner_x + (trim_w - 1 - lx), atlas_py = inner_y + (trim_h - 1 - ly)
             * For rotation 3 (270CW): atlas_px = inner_x + ly, atlas_py = inner_y + (trim_w - 1 - lx) */
            float atlas_px, atlas_py;
            switch (pl->rotation) {
            case 0:
                atlas_px = (float)inner_x + (float)lx;
                atlas_py = (float)inner_y + (float)ly;
                break;
            case 1:
                atlas_px = (float)inner_x + (float)((int32_t)trim_h[pl->sprite_index] - 1 - ly);
                atlas_py = (float)inner_y + (float)lx;
                break;
            case 2:
                atlas_px = (float)inner_x + (float)((int32_t)trim_w[pl->sprite_index] - 1 - lx);
                atlas_py = (float)inner_y + (float)((int32_t)trim_h[pl->sprite_index] - 1 - ly);
                break;
            case 3:
                atlas_px = (float)inner_x + (float)ly;
                atlas_py = (float)inner_y + (float)((int32_t)trim_w[pl->sprite_index] - 1 - lx);
                break;
            default:
                atlas_px = (float)inner_x + (float)lx;
                atlas_py = (float)inner_y + (float)ly;
                break;
            }

            vtx->atlas_u = (uint16_t)((atlas_px * 65535.0F) / (float)atlas_w + 0.5F);
            vtx->atlas_v = (uint16_t)((atlas_py * 65535.0F) / (float)atlas_h + 0.5F);
        }
    }

    NT_BUILD_ASSERT(vertex_cursor == total_vertex_count && "end_atlas: vertex count mismatch");
    // #endregion

    // #region Step 7: Register atlas metadata entry (D-04)
    uint64_t blob_hash = nt_hash64(blob, blob_size).value;
    nt_builder_add_entry(ctx, state->name, NT_BUILD_ASSET_ATLAS, NULL, blob, blob_size, blob_hash);
    // #endregion

    // #region Step 8: Register texture page entries
    for (uint32_t p = 0; p < page_count; p++) {
        char tex_path[512];
        (void)snprintf(tex_path, sizeof(tex_path), "%s/tex%u", state->name, p);

        uint32_t pixel_bytes = page_w[p] * page_h[p] * 4;
        uint64_t tex_hash = nt_hash64(page_pixels[p], pixel_bytes).value;

        /* Create NtBuildTextureData for the encode pipeline */
        NtBuildTextureData *td = (NtBuildTextureData *)calloc(1, sizeof(NtBuildTextureData));
        NT_BUILD_ASSERT(td && "end_atlas: alloc failed");
        td->width = page_w[p];
        td->height = page_h[p];
        td->opts.format = opts->format;
        td->opts.max_size = 0; /* already at final size */
        td->opts.compress = NULL;
        if (state->has_compress) {
            td->compress = state->compress;
            td->has_compress = true;
        }
        /* Page pixels are decoded_data -- owned by the entry */
        nt_builder_add_entry(ctx, tex_path, NT_BUILD_ASSET_TEXTURE, td, page_pixels[p], pixel_bytes, tex_hash);
        page_pixels[p] = NULL; /* ownership transferred to entry */
    }
    // #endregion

    // #region Step 9: Register codegen-only region entries (D-12)
    for (uint32_t i = 0; i < sprite_count; i++) {
        char region_path[512];
        (void)snprintf(region_path, sizeof(region_path), "%s/%s", state->name, sprites[i].name);

        /* Region entries: codegen-only, no pack data.
         * Use dedup_original = 0 so encode pipeline skips them. */
        uint64_t region_hash = nt_hash64_str(sprites[i].name).value;

        /* Create a minimal entry with the atlas blob as decoded_data (size 0 signals codegen-only) */
        /* Actually: we set dedup_original on the entry manually after add_entry. */
        /* We need to create a small placeholder decoded_data for the entry system. */
        uint8_t *placeholder = (uint8_t *)calloc(1, sizeof(uint64_t));
        NT_BUILD_ASSERT(placeholder && "end_atlas: alloc failed");
        memcpy(placeholder, &region_hash, sizeof(uint64_t));

        nt_builder_add_entry(ctx, region_path, NT_BUILD_ASSET_ATLAS_REGION, NULL, placeholder, (uint32_t)sizeof(uint64_t), region_hash);

        /* Mark as dedup so encode pipeline skips it -- point to entry 0 (atlas metadata) */
        NtBuildEntry *re = &ctx->pending[ctx->pending_count - 1];
        re->dedup_original = 0; /* index of any existing entry -- encode skips */
    }
    // #endregion

    // #region Step 10: Cleanup
    /* Free sprite RGBA pixels and names */
    for (uint32_t i = 0; i < sprite_count; i++) {
        free(sprites[i].rgba);
        sprites[i].rgba = NULL;
        free(sprites[i].name);
        sprites[i].name = NULL;
    }

    /* Free hull vertices (careful: duplicates share pointers) */
    for (uint32_t i = 0; i < sprite_count; i++) {
        if (dedup_map[i] < 0 && hull_vertices[i]) {
            free(hull_vertices[i]);
        }
        hull_vertices[i] = NULL;
    }

    /* Free temporary arrays */
    free(trim_x);
    free(trim_y);
    free(trim_w);
    free(trim_h);
    free(dedup_map);
    free(unique_indices);
    free(vertex_counts);
    free(hull_vertices);
    free(placements);
    free(placement_lookup);

    /* Free remaining page pixels (any not transferred to entries) */
    for (uint32_t p = 0; p < page_count; p++) {
        free(page_pixels[p]);
    }
    free(page_pixels);

    /* Free atlas state */
    free(state->sprites);
    free(state->name);
    free(state);
    ctx->active_atlas = NULL;
    ctx->atlas_count++;
    // #endregion

    NT_LOG_INFO("Atlas packed: %u sprites (%u unique), %u pages", sprite_count, unique_count, page_count);
}
