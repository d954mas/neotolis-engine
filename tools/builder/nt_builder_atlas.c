/* clang-format off */
#include "nt_builder_internal.h"
#include "nt_builder_atlas_geometry.h"
#include "nt_builder_atlas_vpack.h"
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
 *  outlines and 8-orientation D4 transforms. See nt_builder_atlas_vpack.c.
 *
 *  Pages grow dynamically as needed (vector_pack handles its own
 *  page creation when no fit on existing pages).
 *  New pages only when max_size exhausted (ATLAS-18).
 * =================================================================== */

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

// #region Pack stats payload — fill ratio measurement after packing

/* Compute trim_area and poly_area on the unique sprite set so the BENCH
 * line can report poly_frontier_fill / poly_texture_fill. */
static void pack_stats_measure_payload(PackStats *stats, const uint32_t *trim_w, const uint32_t *trim_h, Point2D **hull_verts, const uint32_t *hull_counts, uint32_t sprite_count,
                                       const nt_atlas_opts_t *opts) {
    float dilate = (float)opts->extrude + ((float)opts->padding * 0.5F);

    stats->trim_area = 0;
    stats->poly_area = 0;

    for (uint32_t i = 0; i < sprite_count; i++) {
        // NOLINTNEXTLINE(clang-analyzer-core.UndefinedBinaryOperatorResult) — caller populates trim_w/trim_h before entry; analyzer can't trace through pipeline_tile_pack init
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
// #endregion

// #region Composition — blit trimmed pixels, extrude edges
/* --- Blit trimmed sprite pixels to atlas page --- */

// NOLINTNEXTLINE(readability-function-cognitive-complexity) — two-path blit (rotation 0 fast path + generic) is inherent to the hot-path design
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
                while (sx < trim_w && src_row[(sx * 4) + 3] == 0) {
                    sx++;
                }
                if (sx >= trim_w) {
                    break;
                }
                /* Find end of opaque run */
                uint32_t run_start = sx;
                while (sx < trim_w && src_row[(sx * 4) + 3] != 0) {
                    sx++;
                }
                memcpy(&dst_row[(size_t)run_start * 4], &src_row[(size_t)run_start * 4], (size_t)(sx - run_start) * 4);
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

/* --- Edge extrude: AABB row/column duplication (Unity-style) ---
 *
 * This is the classic atlas bleed used by most engines and packers:
 * duplicate the trimmed sprite rect's outermost rows/columns outward by
 * `extrude_count` pixels. It is intentionally rectangle-based, not
 * silhouette-aware — transparent pixels inside the trim rect stay untouched,
 * and transparent corners on anti-aliased shapes remain transparent.
 *
 * The builder still uses polygon packing when enabled, but compose keeps the
 * bleed logic simple and predictable. When no visual gap between neighboring
 * extrude bands is desired, increase `opts.padding`; `margin` only affects
 * the page border. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity) — four edge-copy loops (top/bot/left/right) fused into one function; splitting would obscure intent
static void extrude_edges(uint8_t *page, uint32_t page_w, uint32_t page_h, uint32_t px, uint32_t py, uint32_t sw, uint32_t sh, uint32_t extrude_count) {
    if (extrude_count == 0) {
        return;
    }

    // #region Top and bottom edge extrusion
    for (uint32_t e = 1; e <= extrude_count; e++) {
        if (py >= e) {
            uint32_t dst_y = py - e;
            for (uint32_t x = px; x < px + sw && x < page_w; x++) {
                memcpy(&page[((size_t)dst_y * page_w + x) * 4], &page[((size_t)py * page_w + x) * 4], 4);
            }
        }

        uint32_t src_y = py + sh - 1;
        uint32_t dst_y = src_y + e;
        if (dst_y < page_h) {
            for (uint32_t x = px; x < px + sw && x < page_w; x++) {
                memcpy(&page[((size_t)dst_y * page_w + x) * 4], &page[((size_t)src_y * page_w + x) * 4], 4);
            }
        }
    }
    // #endregion

    // #region Left and right edge extrusion (includes corner band)
    for (uint32_t e = 1; e <= extrude_count; e++) {
        uint32_t y_start = (py >= extrude_count) ? py - extrude_count : 0;
        uint32_t y_end = py + sh + extrude_count;
        if (y_end > page_h) {
            y_end = page_h;
        }

        if (px >= e) {
            uint32_t dst_x = px - e;
            for (uint32_t y = y_start; y < y_end; y++) {
                memcpy(&page[((size_t)y * page_w + dst_x) * 4], &page[((size_t)y * page_w + px) * 4], 4);
            }
        }

        uint32_t src_x = px + sw - 1;
        uint32_t dst_x = src_x + e;
        if (dst_x < page_w) {
            for (uint32_t y = y_start; y < y_end; y++) {
                memcpy(&page[((size_t)y * page_w + dst_x) * 4], &page[((size_t)y * page_w + src_x) * 4], 4);
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
    /* Bump on any change to the byte layout below, the flag-bit ordering, or
     * the shape enum ordering — otherwise cached atlases would silently bind
     * to a different pack behaviour. v6: polygon_mode bool → shape enum.
     * Note: NT_BUILDER_VERSION is ALSO mixed into the key below, so content
     * changes inside the atlas pipeline (blit/extrude/compose tweaks that
     * don't touch the byte layout of this hash input) only need a
     * NT_BUILDER_VERSION bump — same policy as nt_builder_cache.c. */
    enum { ATLAS_CACHE_KEY_VERSION = 6 };

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
    /* Builder version — mirrors nt_builder_cache.c:nt_builder_compute_opts_hash
     * so any NT_BUILDER_VERSION bump automatically invalidates all atlas cache
     * files. Covers content-level changes (e.g. blit/extrude/compose) that
     * don't touch the opts struct layout and therefore wouldn't otherwise
     * trigger a cache miss. */
    uint32_t builder_version = NT_BUILDER_VERSION;
    memcpy(opts_buf + pos, &builder_version, sizeof(builder_version));
    pos += (uint32_t)sizeof(builder_version);
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
    /* Bit 4 (value 4) was polygon_mode in v5; freed in v6 since shape is
     * serialised as a dedicated byte below. Do not re-use it without bumping
     * ATLAS_CACHE_KEY_VERSION. */
    uint8_t flags = (uint8_t)((opts->allow_transform ? 1 : 0) | (opts->power_of_two ? 2 : 0) | (opts->debug_png ? 8 : 0) | (opts->premultiplied ? 16 : 0));
    opts_buf[pos++] = flags;
    opts_buf[pos++] = (uint8_t)opts->shape;
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

    // NOLINTNEXTLINE(clang-analyzer-unix.Malloc) — no allocation alive at this point; analyzer false positive on the short-circuit OR path
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
    /* premultiplied alpha only meaningful for RGBA8. Setting it true with RGB8/RG8/R8
     * is a caller bug — there's no alpha channel to multiply by. */
    NT_BUILD_ASSERT((!state->opts.premultiplied || state->opts.format == NT_TEXTURE_FORMAT_RGBA8) && "begin_atlas: premultiplied=true requires NT_TEXTURE_FORMAT_RGBA8");

    /* Simple AABB edge extrude is only safe when packing also reserves a
     * rectangular footprint. Tight/polygon packing uses inflated sprite
     * hulls, so writing a trim-AABB extrude band could spill outside the
     * reserved area and collide with neighbors. This is a config error:
     * polygon packing must use padding-only, or rect packing if AABB
     * extrude is needed. */
    NT_BUILD_ASSERT((state->opts.shape == NT_ATLAS_SHAPE_RECT || state->opts.extrude == 0) &&
                    "begin_atlas: opts.extrude > 0 requires shape == NT_ATLAS_SHAPE_RECT — polygon modes reserve space for the silhouette envelope, not for an AABB extrude band");

    /* Initialize sprite array */
    state->sprite_capacity = 64;
    state->sprites = (NtAtlasSpriteInput *)calloc(state->sprite_capacity, sizeof(NtAtlasSpriteInput));
    NT_BUILD_ASSERT(state->sprites && "begin_atlas: alloc failed");
    state->sprite_count = 0;

    ctx->active_atlas = state;
}

/* Resolve effective per-sprite opts: fall back to defaults when caller
 * passes NULL, then validate the origin values. Always returns a struct
 * whose fields are safe to use downstream. Trivial function; clang-tidy
 * counts the NT_BUILD_ASSERT macro expansion as high cognitive complexity. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static nt_atlas_sprite_opts_t atlas_resolve_sprite_opts(const nt_atlas_sprite_opts_t *opts) {
    nt_atlas_sprite_opts_t resolved = opts ? *opts : nt_atlas_sprite_opts_defaults();
    NT_BUILD_ASSERT(isfinite(resolved.origin_x) && isfinite(resolved.origin_y) && "atlas_add*: origin must be finite (no NaN/inf)");
    return resolved;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void nt_builder_atlas_add(NtBuilderContext *ctx, const char *path, const nt_atlas_sprite_opts_t *opts) {
    NT_BUILD_ASSERT(ctx && path && "atlas_add: invalid args");
    NT_BUILD_ASSERT(ctx->active_atlas && "atlas_add: no active atlas (call begin_atlas first)");

    nt_atlas_sprite_opts_t sopts = atlas_resolve_sprite_opts(opts);
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

    /* Determine region name (D-06, D-07). opts->name takes precedence; NULL falls
     * back to basename of the source path. */
    const char *region_name = sopts.name ? sopts.name : extract_filename(path);

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
    sprite->origin_x = sopts.origin_x;
    sprite->origin_y = sopts.origin_y;
    sprite->decoded_hash = decoded_hash;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void nt_builder_atlas_add_raw(NtBuilderContext *ctx, const uint8_t *rgba_pixels, uint32_t width, uint32_t height, const nt_atlas_sprite_opts_t *opts) {
    NT_BUILD_ASSERT(ctx && rgba_pixels && "atlas_add_raw: invalid args");
    NT_BUILD_ASSERT(ctx->active_atlas && "atlas_add_raw: no active atlas (call begin_atlas first)");

    nt_atlas_sprite_opts_t sopts = atlas_resolve_sprite_opts(opts);
    NT_BUILD_ASSERT(sopts.name && "atlas_add_raw: opts->name is required for raw pixels (no path to derive from)");

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
    sprite->name = strdup(sopts.name);
    NT_BUILD_ASSERT(sprite->name && "atlas_add_raw: strdup failed");
    sprite->origin_x = sopts.origin_x;
    sprite->origin_y = sopts.origin_y;
    sprite->decoded_hash = decoded_hash;
}

/* --- Glob callback for atlas --- */

typedef struct {
    NtBuilderContext *ctx;
    const nt_atlas_sprite_opts_t *sprite_opts; /* propagated origin; name is always per-file */
    uint32_t match_count;
} AtlasGlobData;

static void atlas_glob_callback(const char *full_path, void *user) {
    AtlasGlobData *d = (AtlasGlobData *)user;
    d->match_count++;
    /* Per-file name is derived from the path — pass NULL so atlas_add extracts
     * the basename. The origin fields propagate from the glob-level opts. */
    nt_atlas_sprite_opts_t per_file = d->sprite_opts ? *d->sprite_opts : nt_atlas_sprite_opts_defaults();
    per_file.name = NULL;
    nt_builder_atlas_add(d->ctx, full_path, &per_file);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void nt_builder_atlas_add_glob(NtBuilderContext *ctx, const char *pattern, const nt_atlas_sprite_opts_t *opts) {
    NT_BUILD_ASSERT(ctx && pattern && "atlas_add_glob: invalid args");
    NT_BUILD_ASSERT(ctx->active_atlas && "atlas_add_glob: no active atlas");
    /* If opts is non-NULL, name MUST be NULL — a single name can't apply to
     * N matched files without hash collisions. Each file derives its own name
     * from its path. Per-file override requires calling glob_iterate + atlas_add
     * manually (build_packs.c shows the pattern). */
    NT_BUILD_ASSERT((!opts || opts->name == NULL) && "atlas_add_glob: opts->name must be NULL (each file derives its name from path)");
    /* Validate finite origin values up front so failures point at the glob call site. */
    if (opts) {
        NT_BUILD_ASSERT(isfinite(opts->origin_x) && isfinite(opts->origin_y) && "atlas_add_glob: origin must be finite (no NaN/inf)");
    }

    AtlasGlobData data = {.ctx = ctx, .sprite_opts = opts, .match_count = 0};
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

/* --- pipeline_geometry: strategy framework ---------------------------------
 *
 * pipeline_geometry runs four simplification strategies on each unique sprite
 * contour and keeps whichever produces the smallest estimated final inflated
 * polygon area. Each strategy returns a GeometryCandidate that owns its polygon
 * heap allocation until the caller either adopts it (transfers ownership) or
 * frees it via geometry_maybe_adopt().
 *
 * Score = polygon_area + perimeter * d + pi * d^2, where d is the required
 * Clipper2 inflate amount. Same formula for all strategies so they're
 * directly comparable. */

typedef struct {
    Point2D *poly;      /* heap-allocated, caller frees if not adopted */
    uint32_t count;     /* vertex count */
    double inflate_amt; /* required Clipper2 inflate amount (pixels) */
    double est_area;    /* scoring key — lower is better */
    bool valid;         /* false = strategy declined / produced degenerate output */
} GeometryCandidate;

static double geometry_estimate_inflated_area(const Point2D *poly, uint32_t count, double inflate_amt) {
    double perim = 0.0;
    for (uint32_t v = 0; v < count; v++) {
        uint32_t vn = (v + 1) % count;
        double dx = (double)(poly[vn].x - poly[v].x);
        double dy = (double)(poly[vn].y - poly[v].y);
        perim += sqrt((dx * dx) + (dy * dy));
    }
    return (double)polygon_area_pixels(poly, count) + (perim * inflate_amt) + (3.14159 * inflate_amt * inflate_amt);
}

/* If candidate is better than current, free current->poly and take candidate.
 * Otherwise free candidate->poly. Either way ownership is resolved on return. */
static void geometry_maybe_adopt(GeometryCandidate *current, GeometryCandidate candidate) {
    if (!candidate.valid) {
        return;
    }
    if (!current->valid || candidate.est_area < current->est_area) {
        free(current->poly);
        *current = candidate;
    } else {
        free(candidate.poly);
    }
}

/* Strategy 1: Ramer-Douglas-Peucker with epsilon growth + bisection to hit target.
 * Baseline — RDP preserves contour shape best for organic sprites. */
static GeometryCandidate strategy_rdp(const Point2D *clean, uint32_t clean_count, uint32_t target) {
    GeometryCandidate result = {0};
    Point2D *poly = (Point2D *)malloc(clean_count * sizeof(Point2D));
    NT_BUILD_ASSERT(poly && "strategy_rdp: alloc failed");

    double eps = 0.5;
    uint32_t count = rdp_simplify(clean, clean_count, eps, poly);
    double prev_eps = -1.0;
    while (count > target && eps < 100.0) {
        prev_eps = eps;
        eps *= 1.5;
        count = rdp_simplify(clean, clean_count, eps, poly);
    }
    /* Bisect [prev_eps, eps] for the smallest eps that still fits target. */
    if (count <= target && prev_eps > 0.0 && (eps - prev_eps) > 0.5) {
        double lo = prev_eps;
        double hi = eps;
        for (int bs = 0; bs < 12; bs++) {
            double mid = (lo + hi) * 0.5;
            uint32_t mid_count = rdp_simplify(clean, clean_count, mid, poly);
            if (mid_count <= target) {
                hi = mid;
            } else {
                lo = mid;
            }
        }
        eps = hi;
        count = rdp_simplify(clean, clean_count, eps, poly);
    }

    result.poly = poly;
    result.count = count;
    result.inflate_amt = eps + 1.0;
    result.est_area = geometry_estimate_inflated_area(poly, count, result.inflate_amt);
    result.valid = true;
    return result;
}

/* Strategy 2: greedy perpendicular-distance simplification, exactly target verts.
 * Inflate amount comes from measuring actual pixel coverage loss, not eps. */
static GeometryCandidate strategy_perp(const Point2D *clean, uint32_t clean_count, uint32_t target, const uint8_t *binary_source, uint32_t tw, uint32_t th) {
    GeometryCandidate result = {0};
    Point2D *poly = (Point2D *)malloc(clean_count * sizeof(Point2D));
    NT_BUILD_ASSERT(poly && "strategy_perp: alloc failed");

    double dummy_dev = 0.0;
    uint32_t count = hull_simplify_perp(clean, clean_count, target, poly, &dummy_dev);

    /* True inflate = max distance from any opaque pixel center outside the
     * candidate polygon to the polygon boundary. Catches cases where the
     * simplified polygon cut between two clean contour vertices. */
    double max_outside = polygon_max_outside_pixel_distance(poly, count, binary_source, tw, th);

    result.poly = poly;
    result.count = count;
    result.inflate_amt = max_outside + 1.0;
    result.est_area = geometry_estimate_inflated_area(poly, count, result.inflate_amt);
    result.valid = true;
    return result;
}

/* Strategy 3: trim bounding rectangle. Always 4 vertices, trivially contains
 * every alpha pixel (they live inside the trim bbox by construction). For
 * mostly-rectangular sprites (muzzle, icons, tiles) this is optimal. */
static GeometryCandidate strategy_rect(uint32_t tw, uint32_t th) {
    GeometryCandidate result = {0};
    Point2D *poly = (Point2D *)malloc(4 * sizeof(Point2D));
    NT_BUILD_ASSERT(poly && "strategy_rect: alloc failed");
    poly[0] = (Point2D){0, 0};
    poly[1] = (Point2D){(int32_t)tw, 0};
    poly[2] = (Point2D){(int32_t)tw, (int32_t)th};
    poly[3] = (Point2D){0, (int32_t)th};

    result.poly = poly;
    result.count = 4;
    result.inflate_amt = 1.0;
    result.est_area = geometry_estimate_inflated_area(poly, 4, result.inflate_amt);
    result.valid = true;
    return result;
}

/* Strategy 4: convex hull of the binary mask via Andrew's monotone chain,
 * simplified down to target vertices. Wins on convex-ish shapes where the
 * hull is already within max_vertices. */
static GeometryCandidate strategy_convex(const uint8_t *binary_source, uint32_t tw, uint32_t th, uint32_t target) {
    GeometryCandidate result = {0};
    uint32_t count = 0;
    Point2D *poly = binary_build_convex_polygon(binary_source, tw, th, target, &count);
    if (!poly || count < 3) {
        free(poly);
        return result; /* invalid */
    }
    double max_outside = polygon_max_outside_pixel_distance(poly, count, binary_source, tw, th);

    result.poly = poly;
    result.count = count;
    result.inflate_amt = max_outside + 1.0;
    result.est_area = geometry_estimate_inflated_area(poly, count, result.inflate_amt);
    result.valid = true;
    return result;
}

/* --- pipeline_geometry: contour trace + simplification + inflation per unique sprite --- */

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void pipeline_geometry(AtlasPipeline *p) {
    p->vertex_counts = (uint32_t *)calloc(p->sprite_count, sizeof(uint32_t));
    p->hull_vertices = (Point2D **)calloc(p->sprite_count, sizeof(Point2D *));
    NT_BUILD_ASSERT(p->vertex_counts && p->hull_vertices && "pipeline_geometry: alloc failed");

    for (uint32_t ui = 0; ui < p->unique_count; ui++) {
        uint32_t idx = p->unique_indices[ui];
        uint32_t tw = p->trim_w[idx];
        uint32_t th = p->trim_h[idx];

        if (p->opts->shape == NT_ATLAS_SHAPE_RECT) {
            /* Rect mode: 4-vertex trim bounding box. */
            p->hull_vertices[idx] = (Point2D *)malloc(4 * sizeof(Point2D));
            NT_BUILD_ASSERT(p->hull_vertices[idx] && "pipeline_geometry: alloc failed");
            p->hull_vertices[idx][0] = (Point2D){0, 0};
            p->hull_vertices[idx][1] = (Point2D){(int32_t)tw, 0};
            p->hull_vertices[idx][2] = (Point2D){(int32_t)tw, (int32_t)th};
            p->hull_vertices[idx][3] = (Point2D){0, (int32_t)th};
            p->vertex_counts[idx] = 4;
            continue;
        }

        /* Polygon modes (CONVEX_HULL or CONCAVE_CONTOUR): extract a binary alpha
         * mask for the trimmed region, then dispatch on shape. */
        {
            const uint8_t *ap = p->alpha_planes[idx];
            uint32_t aw = p->sprites[idx].width;

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

            if (p->opts->shape == NT_ATLAS_SHAPE_CONVEX_HULL) {
                /* Convex hull mode: skip morphological closing, contour trace, and
                 * the 4-strategy concave pipeline. The convex hull of opaque pixels
                 * trivially contains disjoint components and every opaque pixel, so
                 * none of that machinery is needed. */
                p->hull_vertices[idx] = binary_build_convex_polygon(binary, tw, th, p->opts->max_vertices, &p->vertex_counts[idx]);
                free(binary);
                continue;
            }

            /* NT_ATLAS_SHAPE_CONCAVE_CONTOUR: full concave pipeline with convex
             * fallback when morphological closing or contour trace cannot produce
             * a valid simple polygon. */

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
                uint32_t max_iter = ((tw > th ? tw : th) / 2) + 1;
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
                /* Trace outer contour (CCW).
                 *
                 * Tight upper bound on contour length for a single-component
                 * mask: P ≤ 2*N + 2, where N is the opaque-pixel count.
                 * Derivation: total pixel edges = 4*N; let I be the count of
                 * edges shared between two opaque pixels, then P = 4*N − 2*I.
                 * A connected component has I ≥ N − 1 (minimum: spanning
                 * tree), so P ≤ 4*N − 2*(N − 1) = 2*N + 2. Connectivity is
                 * already guaranteed by the binary_count_components check
                 * above, so this bound holds.
                 *
                 * The old bound 2*tw*th assumed N = tw*th (a solid fill),
                 * but a solid shape is a rectangle with perimeter 2*(tw+th),
                 * not 2*tw*th. The old formula overshoots by 1/(2*fill_ratio)
                 * — 10-100× more memory than needed for real sprites. */
                uint32_t opaque_count = 0;
                for (size_t bi = 0; bi < (size_t)tw * th; bi++) {
                    if (binary[bi]) {
                        opaque_count++;
                    }
                }
                uint32_t max_contour = (2 * opaque_count) + 2;
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

                    /* Run 4 simplification strategies, keep lowest estimated
                     * final inflated area. Each strategy returns a candidate
                     * polygon + required inflate amount; geometry_maybe_adopt
                     * handles ownership (frees the loser each step). */
                    uint32_t target = p->opts->max_vertices;
                    GeometryCandidate best = strategy_rdp(clean, clean_count, target);
                    geometry_maybe_adopt(&best, strategy_perp(clean, clean_count, target, binary_source, tw, th));
                    geometry_maybe_adopt(&best, strategy_rect(tw, th));
                    geometry_maybe_adopt(&best, strategy_convex(binary_source, tw, th, target));
                    NT_BUILD_ASSERT(best.valid && "pipeline_geometry: RDP baseline should never be invalid");

                    Point2D *simplified = best.poly;
                    uint32_t simp_count = best.count;
                    double inflate_amt = best.inflate_amt;

                    free(clean);
                    int32_t *simp_xy = (int32_t *)malloc((size_t)simp_count * 2 * sizeof(int32_t));
                    NT_BUILD_ASSERT(simp_xy && "pipeline_geometry: alloc failed");
                    for (size_t v = 0; v < simp_count; v++) {
                        simp_xy[v * 2] = simplified[v].x;
                        simp_xy[(v * 2) + 1] = simplified[v].y;
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
                        Point2D *result = (Point2D *)malloc((size_t)inf_count * sizeof(Point2D));
                        NT_BUILD_ASSERT(result && "pipeline_geometry: alloc failed");
                        for (size_t v = 0; v < inf_count; v++) {
                            result[v].x = inflated_xy[v * 2];
                            result[v].y = inflated_xy[(v * 2) + 1];
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
        }
    }

    /* Copy vertex data for duplicates from their originals */
    for (uint32_t i = 0; i < p->sprite_count; i++) {
        if (p->dedup_map[i] >= 0) {
            uint32_t orig = (uint32_t)p->dedup_map[i];
            p->vertex_counts[i] = p->vertex_counts[orig];
            p->hull_vertices[i] = p->hull_vertices[orig]; /* shared pointer, don't double-free */
        }
    }
}

/* --- pipeline_tile_pack: sort sprites by area, place on atlas pages via tile-grid collision --- */

static void pipeline_tile_pack(AtlasPipeline *p) {
    /* calloc so trailing padding in AtlasPlacement is deterministic — the struct
     * is fwrite'd directly into the atlas cache file and uninitialized padding
     * produces noisy diffs between otherwise identical builds. */
    // NOLINTNEXTLINE(clang-analyzer-optin.portability.UnixAPI)
    p->placements = (AtlasPlacement *)calloc(p->unique_count, sizeof(AtlasPlacement));
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

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void pipeline_compose(AtlasPipeline *p) {
    uint32_t extrude_val = p->opts->extrude;

    p->page_pixels = (uint8_t **)calloc(p->page_count, sizeof(uint8_t *));
    NT_BUILD_ASSERT(p->page_pixels && "pipeline_compose: alloc failed");

    for (uint32_t pg = 0; pg < p->page_count; pg++) {
        p->page_pixels[pg] = (uint8_t *)calloc((size_t)p->page_w[pg] * p->page_h[pg] * 4, 1);
        NT_BUILD_ASSERT(p->page_pixels[pg] && "pipeline_compose: page alloc failed");
    }

    /* Blit each placed sprite, then duplicate the trim AABB edges outward. */
    for (uint32_t pi = 0; pi < p->placement_count; pi++) {
        AtlasPlacement *pl = &p->placements[pi];
        uint32_t idx = pl->sprite_index;
        uint32_t inner_x = pl->x + extrude_val;
        uint32_t inner_y = pl->y + extrude_val;

        blit_sprite(p->page_pixels[pl->page], p->page_w[pl->page], p->sprites[idx].rgba, p->sprites[idx].width, pl->trim_x, pl->trim_y, pl->trimmed_w, pl->trimmed_h, inner_x, inner_y, pl->transform);

        uint32_t blit_w = (pl->transform & 4) ? pl->trimmed_h : pl->trimmed_w;
        uint32_t blit_h = (pl->transform & 4) ? pl->trimmed_w : pl->trimmed_h;
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

            if (p->opts->shape != NT_ATLAS_SHAPE_RECT && p->hull_vertices[si] && p->vertex_counts[si] >= 3) {
                debug_draw_hull_outline(debug_page, p->page_w[pg], p->page_h[pg], p->hull_vertices[si], p->vertex_counts[si], ix, iy, p->trim_w[si], p->trim_h[si], p->placements[pi].transform);
            } else {
                uint32_t rw = (p->placements[pi].transform & 4) ? p->placements[pi].trimmed_h : p->placements[pi].trimmed_w;
                uint32_t rh = (p->placements[pi].transform & 4) ? p->placements[pi].trimmed_w : p->placements[pi].trimmed_h;
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
     * and index_start in the blob. This saves space for very large atlases.
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
        /* Single-component fan/ear-clip triangulation: (n - 2) triangles. */
        uint32_t tri = (p->vertex_counts[i] >= 3) ? p->vertex_counts[i] - 2 : 0;
        NT_BUILD_ASSERT(tri <= max_region_tri_count && "pipeline_serialize: region index_count exceeds uint8_t");
        total_index_count += tri * 3;
    }

    /* Build placement lookup: original_sprite_index -> placement index.
     * begin_atlas rejects empty sprite sets, so sprite_count > 0 here, but
     * guard explicitly so the analyzer can prove malloc is not called with 0. */
    NT_BUILD_ASSERT(p->sprite_count > 0 && "pipeline_serialize: sprite_count == 0");
    uint32_t *placement_lookup = (uint32_t *)malloc((size_t)p->sprite_count * sizeof(uint32_t));
    NT_BUILD_ASSERT(placement_lookup && "pipeline_serialize: alloc failed");
    memset(placement_lookup, 0xFF, (size_t)p->sprite_count * sizeof(uint32_t));

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
        /* Pass 1 only runs on originals, and originals have pl->sprite_index == i
         * (the packer remaps back before returning). Use i directly — cheaper and
         * doesn't rely on the invariant holding through future refactors. */
        NT_BUILD_ASSERT(pl->sprite_index == i && "pipeline_serialize: Pass 1 invariant broken (non-original placement)");

        uint16_t local_indices[256];
        uint32_t tri_count = ear_clip_triangulate(p->hull_vertices[i], p->vertex_counts[i], local_indices);
        uint32_t idx_count = tri_count * 3;
        NT_BUILD_ASSERT(idx_count <= UINT8_MAX && "pipeline_serialize: region index_count exceeds uint8_t");
        /* vertex_start/index_start are uint32_t in v3 — no practical bound until 4G entries. */

        sprite_vertex_start[i] = vertex_cursor;
        sprite_index_start[i] = index_cursor;
        sprite_idx_count[i] = idx_count;

        /* Write triangle indices (local: 0..vertex_count-1) */
        memcpy(&indices[index_cursor], local_indices, idx_count * sizeof(uint16_t));
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
            transform_point(lx, ly, pl->transform, (int32_t)p->trim_w[i], (int32_t)p->trim_h[i], &tx, &ty);
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
        reg->vertex_start = sprite_vertex_start[i];
        reg->index_start = sprite_index_start[i];
        reg->vertex_count = (uint8_t)p->vertex_counts[i];
        reg->page_index = (uint8_t)pl->page;
        reg->transform = pl->transform;
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

// NOLINTNEXTLINE(readability-function-cognitive-complexity) — registers N page textures and N region codegen entries in one pass; splitting would just shuffle locals
static void pipeline_register(AtlasPipeline *p) {
    /* Register texture page entries */
    for (uint32_t pg = 0; pg < p->page_count; pg++) {
        char tex_path[512];
        (void)snprintf(tex_path, sizeof(tex_path), "%s/tex%u", p->state->name, pg);

        size_t pixel_bytes = (size_t)p->page_w[pg] * p->page_h[pg] * 4;
        NT_BUILD_ASSERT(pixel_bytes <= UINT32_MAX && "pipeline_register: page too large for nt_hash64 length");
        uint64_t tex_hash = nt_hash64(p->page_pixels[pg], (uint32_t)pixel_bytes).value;

        NtBuildTextureData *td = (NtBuildTextureData *)calloc(1, sizeof(NtBuildTextureData));
        NT_BUILD_ASSERT(td && "pipeline_register: alloc failed");
        td->width = p->page_w[pg];
        td->height = p->page_h[pg];
        td->opts.format = p->opts->format;
        td->opts.max_size = 0;
        td->opts.compress = NULL;
        td->opts.premultiplied = p->opts->premultiplied; /* propagate to texture encoder (validated in begin_atlas) */
        if (p->state->has_compress) {
            td->compress = p->state->compress;
            td->has_compress = true;
        }
        nt_builder_add_entry(p->ctx, tex_path, NT_BUILD_ASSET_TEXTURE, td, p->page_pixels[pg], (uint32_t)pixel_bytes, tex_hash);
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

    /* Free hull vertices (duplicates share pointers via dedup_map) */
    for (uint32_t i = 0; i < p->sprite_count; i++) {
        if (p->dedup_map[i] < 0) {
            free(p->hull_vertices[i]);
        }
        p->hull_vertices[i] = NULL;
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
    free(p->placements);

    /* Free remaining page pixels (any not transferred to entries) */
    for (uint32_t pg = 0; pg < p->page_count; pg++) {
        free(p->page_pixels[pg]);
    }
    free((void *)p->page_pixels);

    /* Free atlas state. atlas_count is bumped by increment_kind_counter when
     * pipeline_register calls nt_builder_add_entry for the atlas metadata
     * blob — no additional bump here (would double-count). */
    free(p->state->sprites);
    free(p->state->name);
    free(p->state);
    p->ctx->active_atlas = NULL;
}

/* --- nt_builder_end_atlas: orchestrator — calls pipeline steps in order --- */

// NOLINTNEXTLINE(readability-function-cognitive-complexity) — linear sequence of pipeline_* stage calls with shared cleanup; "high" complexity reflects stage count, not control-flow depth
void nt_builder_end_atlas(NtBuilderContext *ctx) {
    NT_BUILD_ASSERT(ctx && "end_atlas: ctx is NULL");
    NT_BUILD_ASSERT(ctx->active_atlas && "end_atlas: no active atlas");

    NtBuildAtlasState *state = ctx->active_atlas;
    NT_BUILD_ASSERT(state->sprite_count > 0 && "end_atlas: atlas has no sprites");

    /* Warn on non-premultiplied atlases. Bilinear filtering at sprite gaps
     * mixes opaque pixels with transparent (0,0,0,0) background, producing
     * dark fringes. Valid use cases (NEAREST filter, fully opaque sprites)
     * exist but are rare — keep the user aware. */
    if (!state->opts.premultiplied) {
        NT_LOG_WARN("atlas '%s': premultiplied=false — bilinear filter will cause dark fringes at sprite edges. Use only with NEAREST filter or fully opaque sprites.", state->name);
    }

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
    NT_LOG_INFO("BENCH alpha_trim=%.1f dedup=%.1f geometry=%.1f pack=%.1f compose=%.1f debug_png=%.1f serialize=%.1f total=%.1f pages=%u "
                "used_area=%llu frontier_area=%llu trim_area=%llu poly_area=%llu pot_waste=%llu fill_frontier=%.4f fill_texture=%.4f "
                "or_ops=%llu test_ops=%llu page_scans=%llu page_existing=%llu page_new=%llu cache_hits=%llu cache_misses=%llu",
                bench_alpha_trim * 1000.0, bench_dedup * 1000.0, bench_geometry * 1000.0, bench_tile_pack * 1000.0, bench_compose * 1000.0, bench_debug_png * 1000.0, bench_serialize * 1000.0,
                bench_total * 1000.0, p.page_count, (unsigned long long)p.stats.used_area, (unsigned long long)p.stats.frontier_area, (unsigned long long)p.stats.trim_area,
                (unsigned long long)p.stats.poly_area, (unsigned long long)pot_waste_area, poly_frontier_fill, poly_texture_fill, (unsigned long long)p.stats.or_count,
                (unsigned long long)p.stats.test_count, (unsigned long long)p.stats.page_scan_count, (unsigned long long)p.stats.page_existing_hit_count, (unsigned long long)p.stats.page_new_count,
                (unsigned long long)p.stats.nfp_cache_hit_count, (unsigned long long)p.stats.nfp_cache_miss_count);

    pipeline_cleanup(&p);
}
// #endregion

/* --- Test-access wrapper (atlas internals remain static) ---
 * Thin pass-through so unit tests can exercise extrude_edges without
 * making it non-static. Builder is a developer tool, not a shippable
 * runtime, so the extra symbol has no practical cost. */
void nt_atlas_test_extrude_edges(uint8_t *page, uint32_t page_w, uint32_t page_h, uint32_t px, uint32_t py, uint32_t sw, uint32_t sh, uint32_t extrude_count) {
    extrude_edges(page, page_w, page_h, px, py, sw, sh, extrude_count);
}
