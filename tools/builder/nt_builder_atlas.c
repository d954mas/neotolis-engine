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

/* --- Content-error channel --- */

/* Bounded copy into a fixed error-name buffer (src may be NULL). */
static void error_copy_name(char *dst, const char *src) {
    if (!src) {
        dst[0] = '\0';
        return;
    }
    (void)snprintf(dst, NT_BUILD_ERR_NAME_MAX, "%s", src);
}

void nt_builder_push_error(NtBuilderContext *ctx, const nt_build_error_t *err) {
    if (ctx->error_count >= NT_BUILD_MAX_ERRORS) {
        ctx->errors_truncated = true;
        ctx->poisoned = true; /* poison even when the list is full */
        /* Keep the add-order prefix: an earlier error evicts the current tail
         * (highest seq). A later/equal error stays outside the prefix — drop it. */
        if (ctx->error_seq[NT_BUILD_MAX_ERRORS - 1] <= ctx->cur_error_seq) {
            return;
        }
        ctx->error_count = NT_BUILD_MAX_ERRORS - 1; /* evict tail, fall through to stable insert */
    }
    /* Insert keeping error_seq[] non-decreasing, after equal-seq entries
     * (stable) — so errors[] stays add-sorted and errors[0] is the earliest
     * offender. O(n) shift on the failure path only, n <= NT_BUILD_MAX_ERRORS. */
    uint32_t seq = ctx->cur_error_seq;
    uint32_t pos = ctx->error_count;
    while (pos > 0 && ctx->error_seq[pos - 1] > seq) {
        ctx->errors[pos] = ctx->errors[pos - 1];
        ctx->error_seq[pos] = ctx->error_seq[pos - 1];
        pos--;
    }
    ctx->errors[pos] = *err;
    ctx->error_seq[pos] = seq;
    ctx->error_count++;
    ctx->poisoned = true;
}

/* Fill atlas+sprite names and append a content error.
 * sprite may be NULL for atlas-level caps (region/page count). */
static void push_content_error(NtBuilderContext *ctx, const char *atlas, const char *sprite, nt_build_error_kind kind, uint32_t w, uint32_t h) {
    nt_build_error_t e = {.kind = kind, .w = w, .h = h};
    error_copy_name(e.atlas, atlas);
    error_copy_name(e.sprite, sprite);
    nt_builder_push_error(ctx, &e);
}

const nt_build_error_t *nt_builder_get_errors(const NtBuilderContext *ctx, uint32_t *out_count) {
    NT_BUILD_ASSERT(ctx && "get_errors: ctx is NULL");
    if (out_count) {
        *out_count = ctx->error_count;
    }
    return ctx->errors;
}

bool nt_builder_errors_truncated(const NtBuilderContext *ctx) {
    NT_BUILD_ASSERT(ctx && "errors_truncated: ctx is NULL");
    return ctx->errors_truncated;
}

void nt_build_error_format(const nt_build_error_t *err, char *buf, size_t len) {
    NT_BUILD_ASSERT(err && buf && "error_format: err and buf must be non-NULL");
    switch (err->kind) {
    case NT_BUILD_ERR_KIND_CORRUPT_IMAGE:
        (void)snprintf(buf, len, "sprite '%s': corrupt or undecodable image", err->sprite);
        break;
    case NT_BUILD_ERR_KIND_ZERO_DIM:
        (void)snprintf(buf, len, "sprite '%s': zero width or height", err->sprite);
        break;
    case NT_BUILD_ERR_KIND_IMAGE_TOO_LARGE:
        (void)snprintf(buf, len, "sprite '%s': image %ux%u exceeds decode limit", err->sprite, err->w, err->h);
        break;
    case NT_BUILD_ERR_KIND_TRANSPARENT_AFTER_TRIM:
        (void)snprintf(buf, len, "sprite '%s': fully transparent after trim", err->sprite);
        break;
    case NT_BUILD_ERR_KIND_SLICE9_TOO_BIG:
        (void)snprintf(buf, len, "sprite '%s': slice9 borders (%u) >= source extent (%u)", err->sprite, err->detail_a, err->detail_b);
        break;
    case NT_BUILD_ERR_KIND_DEGENERATE_HULL:
        (void)snprintf(buf, len, "sprite '%s': degenerate hull (no usable outline)", err->sprite);
        break;
    case NT_BUILD_ERR_KIND_CONTOUR_VERTEX_OVERFLOW:
        (void)snprintf(buf, len, "sprite '%s': contour exceeds vertex budget", err->sprite);
        break;
    case NT_BUILD_ERR_KIND_DUPLICATE_NAME:
        (void)snprintf(buf, len, "atlas '%s': duplicate region name '%s'", err->atlas, err->sprite);
        break;
    case NT_BUILD_ERR_KIND_TOO_MANY_REGIONS:
        (void)snprintf(buf, len, "atlas '%s': region count exceeds 65535", err->atlas);
        break;
    case NT_BUILD_ERR_KIND_TOO_MANY_PAGES:
        (void)snprintf(buf, len, "atlas '%s': page count exceeds 65535", err->atlas);
        break;
    case NT_BUILD_ERR_KIND_SPRITE_TOO_LARGE:
        (void)snprintf(buf, len, "sprite '%s': %ux%u exceeds 65535px", err->sprite, err->w, err->h);
        break;
    case NT_BUILD_ERR_KIND_TRIM_OFFSET_OVERFLOW:
        (void)snprintf(buf, len, "sprite '%s': trim offset overflow", err->sprite);
        break;
    case NT_BUILD_ERR_KIND_PAGES_EXHAUSTED:
        (void)snprintf(buf, len, "atlas '%s': ran out of pages (max_size=%u)", err->atlas, err->max_size);
        break;
    case NT_BUILD_ERR_KIND_UNFITTABLE:
        (void)snprintf(buf, len, "sprite '%s': %ux%u + padding %u + margin %u + extrude %u does not fit an empty page of max_size %u", err->sprite, err->w, err->h, err->padding, err->margin,
                       err->detail_a, err->max_size);
        break;
    case NT_BUILD_ERR_KIND_NONE:
    default:
        (void)snprintf(buf, len, "no error");
        break;
    }
}

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
 *  pipeline_validate       Non-mutating pre-pack checks (fit, dup names, caps) — reports all bad sprites
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
            transform_point_texel((int32_t)sx, (int32_t)sy, rotation, (int32_t)trim_w, (int32_t)trim_h, &tx, &ty);
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
/* --- Debug PNG: draw 2px outline around region --- */

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
/* --- Atlas cache key computation --- */

static uint64_t compute_atlas_cache_key(const NtAtlasSpriteInput *sprites, uint32_t sprite_count, const nt_atlas_opts_t *opts) {
    /* Bump on any change to the byte layout below, the flag-bit ordering, or
     * the shape enum ordering — otherwise cached atlases would silently bind
     * to a different pack behaviour. v8: remove format, premultiplied,
     * debug_png, compress from key — those are post-pack and handled by the
     * texture cache (nt_builder_compute_opts_hash). The atlas cache stores
     * raw RGBA page pixels + placements; encoding options don't affect them.
     * Note: NT_BUILDER_VERSION is ALSO mixed into the key below, so content
     * changes inside the atlas pipeline (blit/extrude/compose tweaks that
     * don't touch the byte layout of this hash input) only need a
     * NT_BUILDER_VERSION bump — same policy as nt_builder_cache.c. */
    enum { ATLAS_CACHE_KEY_VERSION = 9 };

    /* Per-sprite data: hash + origin + overrides (in add-order, NOT sorted —
     * cached placements store sprite_index in add-order, so the key must be
     * order-sensitive to avoid mismatching placements after reordering). */
    enum { PER_SPRITE_SIZE = sizeof(uint64_t) + (2 * sizeof(float)) + (4 * sizeof(uint16_t)) + 5 };
    size_t per_sprite_bytes = (size_t)sprite_count * PER_SPRITE_SIZE;
    uint8_t *sprite_buf = (uint8_t *)malloc(per_sprite_bytes);
    NT_BUILD_ASSERT(sprite_buf && "compute_atlas_cache_key: alloc failed");
    for (uint32_t i = 0; i < sprite_count; i++) {
        size_t off = (size_t)i * PER_SPRITE_SIZE;
        memcpy(sprite_buf + off, &sprites[i].decoded_hash, sizeof(uint64_t));
        memcpy(sprite_buf + off + sizeof(uint64_t), &sprites[i].origin_x, sizeof(float));
        memcpy(sprite_buf + off + sizeof(uint64_t) + sizeof(float), &sprites[i].origin_y, sizeof(float));
        size_t ov_off = off + sizeof(uint64_t) + (2 * sizeof(float));
        memcpy(sprite_buf + ov_off, &sprites[i].slice9_left, sizeof(uint16_t));
        memcpy(sprite_buf + ov_off + 2, &sprites[i].slice9_right, sizeof(uint16_t));
        memcpy(sprite_buf + ov_off + 4, &sprites[i].slice9_top, sizeof(uint16_t));
        memcpy(sprite_buf + ov_off + 6, &sprites[i].slice9_bottom, sizeof(uint16_t));
        sprite_buf[ov_off + 8] = sprites[i].shape_override;
        sprite_buf[ov_off + 9] = sprites[i].rotate_override;
        sprite_buf[ov_off + 10] = sprites[i].max_verts_override;
        sprite_buf[ov_off + 11] = sprites[i].margin_override;
        sprite_buf[ov_off + 12] = sprites[i].extrude_override;
    }

    /* Build key buffer: per-sprite data + serialized opts */
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
    /* Only pack/compose-affecting opts go here. Post-pack fields (format,
     * premultiplied, debug_png, compress) are handled by the texture cache
     * and must NOT appear — otherwise changing e.g. premultiplied triggers
     * a full re-pack when only re-encode is needed. */
    uint8_t flags = (uint8_t)((opts->allow_transform ? 1 : 0) | (opts->power_of_two ? 2 : 0));
    opts_buf[pos++] = flags;
    opts_buf[pos++] = (uint8_t)opts->shape;
    opts_buf[pos++] = (uint8_t)ATLAS_CACHE_KEY_VERSION;

    /* Combine into single buffer and hash */
    size_t total = per_sprite_bytes + pos;
    uint8_t *buf = (uint8_t *)malloc(total);
    NT_BUILD_ASSERT(buf && "compute_atlas_cache_key: alloc failed");
    memcpy(buf, sprite_buf, per_sprite_bytes);
    memcpy(buf + per_sprite_bytes, opts_buf, pos);
    free(sprite_buf);

    nt_hash64_t key = nt_hash64(buf, (uint32_t)total);
    free(buf);
    return key.value;
}

/* --- Atlas cache file I/O --- */

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

    bool ok = true;
    ok = ok && fwrite(&placement_count, sizeof(uint32_t), 1, f) == 1;
    ok = ok && fwrite(&page_count, sizeof(uint32_t), 1, f) == 1;
    ok = ok && fwrite(page_w, sizeof(uint32_t), page_count, f) == page_count;
    ok = ok && fwrite(page_h, sizeof(uint32_t), page_count, f) == page_count;
    ok = ok && fwrite(placements, sizeof(AtlasPlacement), placement_count, f) == placement_count;
    for (uint32_t p = 0; p < page_count && ok; p++) {
        size_t pixel_bytes = (size_t)page_w[p] * page_h[p] * 4;
        ok = ok && fwrite(page_pixels[p], 1, pixel_bytes, f) == pixel_bytes;
    }

    (void)fclose(f);

    if (!ok) {
        (void)remove(tmp_path);
        NT_LOG_WARN("atlas_cache_write: fwrite failed (disk full?) — build continues without cache");
        return false;
    }

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
static bool atlas_cache_read(const char *cache_dir, uint64_t cache_key, uint32_t sprite_count, AtlasPlacement **out_placements, uint32_t *out_placement_count, uint32_t *out_page_w,
                             uint32_t *out_page_h, uint32_t *out_page_count, uint8_t ***out_page_pixels) {
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
        (void)fclose(f); // NOLINT(clang-analyzer-unix.Malloc)
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

    /* Validate per-placement fields — a corrupted cache file with valid outer
     * counts but garbage placement records would cause OOB access downstream
     * (placement_lookup, page_pixels indexing, blit_sprite bounds).
     * Fail gracefully → rebuild. Catch corruption HERE with a clear "cache
     * invalid" path, not later in serialize/compose where it looks like a
     * packer bug.
     * Note: sprite_index is remapped to the full sprite array (not unique-only),
     * so the bound is sprite_count, not placement_count. */
    for (uint32_t i = 0; i < placement_count; i++) {
        uint32_t pg = placements[i].page;
        if (placements[i].sprite_index >= sprite_count || pg >= page_count_val) {
            free(placements);
            (void)fclose(f);
            return false;
        }
        /* Bounds-check placement against its page dimensions */
        if (placements[i].x >= out_page_w[pg] || placements[i].y >= out_page_h[pg] || placements[i].trimmed_w > out_page_w[pg] || placements[i].trimmed_h > out_page_h[pg] ||
            placements[i].transform > 7) {
            free(placements);
            (void)fclose(f);
            return false;
        }
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

/* --- Extract filename with extension from path --- */

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
    /* Caller-supplied opts are validated before the poison no-op so a
     * programmer error still traps on a poisoned pack. NULL opts is always
     * valid (uses defaults). extrude>0 shape check is opts-only here. */
    if (opts) {
        NT_BUILD_ASSERT(opts->max_vertices <= 16 && "begin_atlas: max_vertices must be <= 16 (NFP buffer limit: nA+nB <= 32)");
        NT_BUILD_ASSERT(opts->max_size > 0 && opts->max_size <= 16384 && "begin_atlas: max_size must be 1..16384");
        NT_BUILD_ASSERT(opts->padding <= opts->max_size && "begin_atlas: padding exceeds max_size");
        NT_BUILD_ASSERT(opts->margin <= opts->max_size && "begin_atlas: margin exceeds max_size");
        /* premultiplied alpha only meaningful for RGBA8 — no alpha channel otherwise. */
        NT_BUILD_ASSERT((!opts->premultiplied || opts->format == NT_TEXTURE_FORMAT_RGBA8) && "begin_atlas: premultiplied=true requires NT_TEXTURE_FORMAT_RGBA8");
        /* Simple AABB edge extrude needs a rectangular footprint; polygon
         * packing inflates hulls, so an AABB extrude band could spill and
         * collide. Polygon modes must use padding-only. */
        NT_BUILD_ASSERT((opts->shape == NT_ATLAS_SHAPE_RECT || opts->extrude == 0) &&
                        "begin_atlas: opts.extrude > 0 requires shape == NT_ATLAS_SHAPE_RECT — polygon modes reserve space for the silhouette envelope, not for an AABB extrude band");
    }
    /* Between-atlas hard stop: after poison, open no further atlases
     * (active_atlas is NULL here). Programmer invariants above still fire. */
    if (ctx->poisoned) {
        return;
    }

    NtBuildAtlasState *state = (NtBuildAtlasState *)calloc(1, sizeof(NtBuildAtlasState));
    NT_BUILD_ASSERT(state && "begin_atlas: alloc failed");

    /* Assign early so nt_builder_free_pack can clean up if a validation
     * assert below fires and the test harness longjmps out. */
    ctx->active_atlas = state;

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

/* Pure sprite-opts asserts — independent of atlas state, so they can run
 * before the poison no-op (a poisoned pack must still trap caller bugs). */
static void atlas_assert_sprite_opts(const nt_atlas_sprite_opts_t *sopts) {
    NT_BUILD_ASSERT(sopts->shape <= NT_ATLAS_SPRITE_SHAPE_CONCAVE && "invalid shape override value");
    NT_BUILD_ASSERT((sopts->allow_rotate == 0 || sopts->allow_rotate == NT_ATLAS_SPRITE_ROTATE_NO) && "invalid rotate override value (only 0 or NO)");
    NT_BUILD_ASSERT(sopts->max_vertices <= 16 && "max_vertices override must be <= 16");
}

/* Copy per-sprite overrides from resolved opts into NtAtlasSpriteInput.
 * Slice9 borders auto-force RECT shape + no rotation. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void atlas_apply_sprite_overrides(NtAtlasSpriteInput *sprite, const nt_atlas_sprite_opts_t *sopts, const nt_atlas_opts_t *atlas_opts) {
    sprite->slice9_left = sopts->slice9_left;
    sprite->slice9_right = sopts->slice9_right;
    sprite->slice9_top = sopts->slice9_top;
    sprite->slice9_bottom = sopts->slice9_bottom;
    sprite->shape_override = sopts->shape;
    sprite->rotate_override = sopts->allow_rotate;
    sprite->max_verts_override = sopts->max_vertices;
    sprite->margin_override = sopts->margin;
    sprite->extrude_override = sopts->extrude;
    /* These shape asserts need the effective atlas shape, so they run only for an
     * active atlas (this fn is unreachable on a poisoned-skipped atlas). */
    /* Slice9 auto-force FIRST — sets shape=RECT before extrude check needs it. */
    bool has_slice9 = sopts->slice9_left || sopts->slice9_right || sopts->slice9_top || sopts->slice9_bottom;
    if (has_slice9) {
        NT_BUILD_ASSERT((sprite->shape_override == 0 || sprite->shape_override == NT_ATLAS_SPRITE_SHAPE_RECT) && "slice9 sprite must use RECT shape");
        NT_BUILD_ASSERT((sprite->rotate_override == 0 || sprite->rotate_override == NT_ATLAS_SPRITE_ROTATE_NO) && "slice9 sprite must not allow rotation");
        sprite->shape_override = NT_ATLAS_SPRITE_SHAPE_RECT;
        sprite->rotate_override = NT_ATLAS_SPRITE_ROTATE_NO;
    }
    /* Per-sprite extrude > 0 requires effective shape == RECT. */
    if (sprite->extrude_override > 0) {
        uint8_t effective_shape = sprite->shape_override ? sprite->shape_override : (uint8_t)atlas_opts->shape;
        NT_BUILD_ASSERT((effective_shape == NT_ATLAS_SPRITE_SHAPE_RECT) && "per-sprite extrude > 0 requires effective shape == RECT");
    }
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void nt_builder_atlas_add(NtBuilderContext *ctx, const char *path, const nt_atlas_sprite_opts_t *opts) {
    NT_BUILD_ASSERT(ctx && path && "atlas_add: invalid args");
    /* Pure caller-arg validation runs before the poison no-op below. */
    nt_atlas_sprite_opts_t sopts = atlas_resolve_sprite_opts(opts);
    atlas_assert_sprite_opts(&sopts);

    /* Read the file + assert it exists BEFORE the poison no-op: a missing/unreadable
     * file is an env/programmer error that must always trap, even on a skipped atlas.
     * The extra read on the poisoned-skip path is accepted (rare cold path). */
    char *resolved_path = nt_builder_find_file(path, NULL, ctx);
    const char *read_path = resolved_path ? resolved_path : path;
    uint32_t file_size = 0;
    uint8_t *file_data = (uint8_t *)nt_builder_read_file(read_path, &file_size);
    NT_BUILD_ASSERT(file_data && "atlas_add: failed to read file");
    free(resolved_path);

    /* A prior atlas poisoned the pack, so begin_atlas no-oped and left no active
     * atlas — make this add a no-op too instead of asserting (the file is already
     * validated above). An OPEN poisoned atlas still has active_atlas, so it keeps
     * collecting. */
    if (ctx->poisoned && !ctx->active_atlas) {
        free(file_data);
        return;
    }
    NT_BUILD_ASSERT(ctx->active_atlas && "atlas_add: no active atlas (call begin_atlas first)");

    NtBuildAtlasState *state = ctx->active_atlas;

    /* Stamp the add order now so a decode error below carries this add's seq. */
    ctx->cur_error_seq = ctx->add_seq_counter++;

    /* Determine region name. opts->name takes precedence; NULL falls back to
     * basename of the source path. Needed early to name a decode error. */
    const char *region_name = sopts.name ? sopts.name : extract_filename(path);

    /* Preflight header dims before the full decode: a valid but oversized image
     * fails stbi_load with a NULL alloc, which would masquerade as CORRUPT_IMAGE
     * and lose the real IMAGE_TOO_LARGE classification. Content errors append and
     * skip the sprite; the open atlas keeps decoding later adds. */
    int iw = 0;
    int ih = 0;
    int ic = 0;
    if (!stbi_info_from_memory(file_data, (int)file_size, &iw, &ih, &ic)) {
        /* A single dimension past STBI_MAX_DIMENSIONS (1<<24) makes stbi_info fail
         * with the literal "too large" reason — classify it as oversized, not a
         * generic decode failure. */
        const char *reason = stbi_failure_reason();
        nt_build_error_kind kind = (reason && strstr(reason, "too large")) ? NT_BUILD_ERR_KIND_IMAGE_TOO_LARGE : NT_BUILD_ERR_KIND_CORRUPT_IMAGE;
        push_content_error(ctx, state->name, region_name, kind, 0, 0);
        free(file_data);
        return;
    }
    if (iw <= 0 || ih <= 0) {
        push_content_error(ctx, state->name, region_name, NT_BUILD_ERR_KIND_ZERO_DIM, 0, 0);
        free(file_data);
        return;
    }
    if ((uint64_t)iw * (uint64_t)ih > (UINT32_MAX / 4)) {
        push_content_error(ctx, state->name, region_name, NT_BUILD_ERR_KIND_IMAGE_TOO_LARGE, (uint32_t)iw, (uint32_t)ih);
        free(file_data);
        return;
    }

    /* Decode PNG via stb_image (force RGBA). A NULL here is now a genuine decode
     * failure — the header already passed the preflight above. */
    int w = 0;
    int h = 0;
    int channels = 0;
    uint8_t *pixels = stbi_load_from_memory(file_data, (int)file_size, &w, &h, &channels, 4);
    free(file_data);
    if (!pixels) {
        push_content_error(ctx, state->name, region_name, NT_BUILD_ERR_KIND_CORRUPT_IMAGE, 0, 0);
        return;
    }

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
    sprite->add_seq = ctx->cur_error_seq;
    atlas_apply_sprite_overrides(sprite, &sopts, &state->opts);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void nt_builder_atlas_add_raw(NtBuilderContext *ctx, const uint8_t *rgba_pixels, uint32_t width, uint32_t height, const nt_atlas_sprite_opts_t *opts) {
    NT_BUILD_ASSERT(ctx && rgba_pixels && "atlas_add_raw: invalid args");
    /* Pure caller-arg validation runs before the poison no-op below. */
    nt_atlas_sprite_opts_t sopts = atlas_resolve_sprite_opts(opts);
    NT_BUILD_ASSERT(sopts.name && "atlas_add_raw: opts->name is required for raw pixels (no path to derive from)");
    atlas_assert_sprite_opts(&sopts);
    /* No-op after a prior atlas poisoned the pack (see nt_builder_atlas_add). */
    if (ctx->poisoned && !ctx->active_atlas) {
        return;
    }
    NT_BUILD_ASSERT(ctx->active_atlas && "atlas_add_raw: no active atlas (call begin_atlas first)");

    NtBuildAtlasState *state = ctx->active_atlas;

    /* Stamp the add order (raw adds never fail to decode, but keep the counter
     * in lockstep with atlas_add so seqs stay globally monotonic). */
    ctx->cur_error_seq = ctx->add_seq_counter++;

    /* Zero/oversized dims are graceful content errors on the open atlas (same
     * channel as atlas_add), not caller-contract asserts — matches the spec. */
    if (width == 0 || height == 0) {
        push_content_error(ctx, state->name, sopts.name, NT_BUILD_ERR_KIND_ZERO_DIM, 0, 0);
        return;
    }
    if ((uint64_t)width * height > (UINT32_MAX / 4)) {
        push_content_error(ctx, state->name, sopts.name, NT_BUILD_ERR_KIND_IMAGE_TOO_LARGE, width, height);
        return;
    }

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
    sprite->add_seq = ctx->cur_error_seq;
    atlas_apply_sprite_overrides(sprite, &sopts, &state->opts);
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
    /* Pure caller-arg validation runs before the poison no-op below. */
    if (opts) {
        nt_atlas_sprite_opts_t sopts = atlas_resolve_sprite_opts(opts);
        atlas_assert_sprite_opts(&sopts);
    }
    /* If opts is non-NULL, name MUST be NULL — a single name can't apply to
     * N matched files without hash collisions. Each file derives its own name
     * from its path. Per-file override requires calling glob_iterate + atlas_add
     * manually (build_packs.c shows the pattern). Caller-contract asserts run
     * before the poison no-op so a poisoned pack still traps the programmer bug. */
    NT_BUILD_ASSERT((!opts || opts->name == NULL) && "atlas_add_glob: opts->name must be NULL (each file derives its name from path)");
    /* Validate finite origin values up front so failures point at the glob call site. */
    if (opts) {
        NT_BUILD_ASSERT(isfinite(opts->origin_x) && isfinite(opts->origin_y) && "atlas_add_glob: origin must be finite (no NaN/inf)");
    }

    /* Run the glob + validate the pattern (overflow / no-match are env/programmer
     * errors) BEFORE the poison no-op: a bad pattern must trap even on a skipped
     * atlas. Each matched atlas_add individually no-ops when poisoned, so files are
     * counted (match_count) but not added. */
    AtlasGlobData data = {.ctx = ctx, .sprite_opts = opts, .match_count = 0};
    NT_BUILD_ASSERT(nt_builder_glob_iterate(pattern, atlas_glob_callback, &data) && "atlas_add_glob: glob overflow");
    NT_BUILD_ASSERT(data.match_count > 0 && "atlas_add_glob: no files matched pattern");

    /* No-op after a prior atlas poisoned the pack (see nt_builder_atlas_add). */
    if (ctx->poisoned && !ctx->active_atlas) {
        return;
    }
    /* Guards the non-poison path only — a legitimately-skipped (poisoned-closed)
     * glob already returned above. */
    NT_BUILD_ASSERT(ctx->active_atlas && "atlas_add_glob: no active atlas");
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

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void pipeline_alpha_trim(AtlasPipeline *p) {
    p->trim_x = (uint32_t *)calloc(p->sprite_count, sizeof(uint32_t));
    p->trim_y = (uint32_t *)calloc(p->sprite_count, sizeof(uint32_t));
    p->trim_w = (uint32_t *)calloc(p->sprite_count, sizeof(uint32_t));
    p->trim_h = (uint32_t *)calloc(p->sprite_count, sizeof(uint32_t));
    p->alpha_planes = (uint8_t **)calloc(p->sprite_count, sizeof(uint8_t *));
    NT_BUILD_ASSERT(p->trim_x && p->trim_y && p->trim_w && p->trim_h && p->alpha_planes && "pipeline_alpha_trim: alloc failed");

    for (uint32_t i = 0; i < p->sprite_count; i++) {
        p->ctx->cur_error_seq = p->sprites[i].add_seq; /* stamp add order on any error below */
        p->alpha_planes[i] = alpha_plane_extract(p->sprites[i].rgba, p->sprites[i].width, p->sprites[i].height);
        bool has_pixels = alpha_trim(p->alpha_planes[i], p->sprites[i].width, p->sprites[i].height, p->opts->alpha_threshold, &p->trim_x[i], &p->trim_y[i], &p->trim_w[i], &p->trim_h[i]);
        if (!has_pixels) {
            /* Accumulate and keep validating the rest of this atlas. */
            nt_build_error_t e = {.kind = NT_BUILD_ERR_KIND_TRANSPARENT_AFTER_TRIM, .w = p->sprites[i].width, .h = p->sprites[i].height};
            error_copy_name(e.atlas, p->state->name);
            error_copy_name(e.sprite, p->sprites[i].name);
            nt_builder_push_error(p->ctx, &e);
            continue;
        }
        /* Slice9 requires untrimmed source rect — runtime asserts trim_offset == 0 */
        bool has_s9 = p->sprites[i].slice9_left || p->sprites[i].slice9_right || p->sprites[i].slice9_top || p->sprites[i].slice9_bottom;
        if (has_s9) {
            uint32_t lr = (uint32_t)p->sprites[i].slice9_left + (uint32_t)p->sprites[i].slice9_right;
            uint32_t tb = (uint32_t)p->sprites[i].slice9_top + (uint32_t)p->sprites[i].slice9_bottom;
            /* Report once here; the serialize re-check is defense-in-depth. */
            if (lr >= p->sprites[i].width) {
                nt_build_error_t e = {.kind = NT_BUILD_ERR_KIND_SLICE9_TOO_BIG, .w = p->sprites[i].width, .h = p->sprites[i].height, .detail_a = lr, .detail_b = p->sprites[i].width};
                error_copy_name(e.atlas, p->state->name);
                error_copy_name(e.sprite, p->sprites[i].name);
                nt_builder_push_error(p->ctx, &e);
                continue;
            }
            if (tb >= p->sprites[i].height) {
                nt_build_error_t e = {.kind = NT_BUILD_ERR_KIND_SLICE9_TOO_BIG, .w = p->sprites[i].width, .h = p->sprites[i].height, .detail_a = tb, .detail_b = p->sprites[i].height};
                error_copy_name(e.atlas, p->state->name);
                error_copy_name(e.sprite, p->sprites[i].name);
                nt_builder_push_error(p->ctx, &e);
                continue;
            }
            p->trim_x[i] = 0;
            p->trim_y[i] = 0;
            p->trim_w[i] = p->sprites[i].width;
            p->trim_h[i] = p->sprites[i].height;
        }
    }
}

/* --- pipeline_cache_check: compute cache key and try loading cached result --- */

static void pipeline_cache_check(AtlasPipeline *p) {
    p->state->cache_key = compute_atlas_cache_key(p->sprites, p->sprite_count, p->opts);

    if (p->ctx->cache_dir) {
        p->cache_hit = atlas_cache_read(p->ctx->cache_dir, p->state->cache_key, p->sprite_count, &p->placements, &p->placement_count, p->page_w, p->page_h, &p->page_count, &p->page_pixels);
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
            /* Verify trimmed pixels + pack-affecting metadata match */
            const NtAtlasSpriteInput *sc = &p->sprites[curr_idx];
            const NtAtlasSpriteInput *so = &p->sprites[orig];
            /* Different slice9/shape/rotate constraints require separate placement */
            bool meta_match = sc->slice9_left == so->slice9_left && sc->slice9_right == so->slice9_right && sc->slice9_top == so->slice9_top && sc->slice9_bottom == so->slice9_bottom &&
                              sc->shape_override == so->shape_override && sc->rotate_override == so->rotate_override && sc->max_verts_override == so->max_verts_override &&
                              sc->margin_override == so->margin_override && sc->extrude_override == so->extrude_override;
            if (meta_match && p->trim_w[curr_idx] == p->trim_w[orig] && p->trim_h[curr_idx] == p->trim_h[orig]) {
                bool pixels_match = true;
                uint32_t tw = p->trim_w[curr_idx];
                uint32_t th = p->trim_h[curr_idx];
                for (uint32_t row = 0; row < th && pixels_match; row++) {
                    size_t off_a = (((size_t)(p->trim_y[curr_idx] + row) * sc->width) + p->trim_x[curr_idx]) * 4;
                    size_t off_b = (((size_t)(p->trim_y[orig] + row) * so->width) + p->trim_x[orig]) * 4;
                    const uint8_t *row_a = sc->rgba + off_a;
                    const uint8_t *row_b = so->rgba + off_b;
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
        p->ctx->cur_error_seq = p->sprites[idx].add_seq; /* stamp add order on any error below */
        uint32_t tw = p->trim_w[idx];
        uint32_t th = p->trim_h[idx];

        /* Skip a sprite that failed alpha_trim (degenerate 0-size trim). It was
         * already reported (transparent-after-trim / slice9) so building a hull
         * here would double-report it as a degenerate hull. Never hit on the
         * happy path — trim is always non-zero when nothing poisoned. */
        if (tw == 0 || th == 0) {
            continue;
        }

        /* Resolve per-sprite shape override (0 = atlas default) */
        nt_atlas_shape_t effective_shape = p->opts->shape;
        uint8_t effective_max_verts = p->opts->max_vertices;
        if (p->sprites[idx].shape_override != 0) {
            /* Map NT_ATLAS_SPRITE_SHAPE_* to nt_atlas_shape_t */
            if (p->sprites[idx].shape_override == NT_ATLAS_SPRITE_SHAPE_RECT) {
                effective_shape = NT_ATLAS_SHAPE_RECT;
            } else if (p->sprites[idx].shape_override == NT_ATLAS_SPRITE_SHAPE_CONVEX) {
                effective_shape = NT_ATLAS_SHAPE_CONVEX_HULL;
            } else if (p->sprites[idx].shape_override == NT_ATLAS_SPRITE_SHAPE_CONCAVE) {
                effective_shape = NT_ATLAS_SHAPE_CONCAVE_CONTOUR;
            }
        }
        if (p->sprites[idx].max_verts_override != 0) {
            effective_max_verts = p->sprites[idx].max_verts_override;
        }

        if (effective_shape == NT_ATLAS_SHAPE_RECT) {
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

            if (effective_shape == NT_ATLAS_SHAPE_CONVEX_HULL) {
                /* Convex hull mode: skip morphological closing, contour trace, and
                 * the 4-strategy concave pipeline. The convex hull of opaque pixels
                 * trivially contains disjoint components and every opaque pixel, so
                 * none of that machinery is needed. */
                p->hull_vertices[idx] = binary_build_convex_polygon(binary, tw, th, effective_max_verts, &p->vertex_counts[idx]);
                free(binary);
                if (!p->hull_vertices[idx]) {
                    /* Empty mask / degenerate hull — graceful error, skip sprite. */
                    push_content_error(p->ctx, p->state->name, p->sprites[idx].name, NT_BUILD_ERR_KIND_DEGENERATE_HULL, tw, th);
                }
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
                bool contour_overflow = false;
                uint32_t contour_count = trace_contour(binary, tw, th, contour, max_contour, &contour_overflow);
                if (contour_overflow) {
                    /* Vertex-budget overflow routes to a graceful error, not the
                     * silent convex fallback used for a degenerate contour. */
                    free(contour);
                    free(binary_source);
                    free(binary);
                    push_content_error(p->ctx, p->state->name, p->sprites[idx].name, NT_BUILD_ERR_KIND_CONTOUR_VERTEX_OVERFLOW, tw, th);
                    continue;
                }

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
                    uint32_t target = effective_max_verts;
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

                        uint32_t final_target = effective_max_verts;
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
                        if (inf_count <= effective_max_verts) {
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
                p->hull_vertices[idx] = binary_build_convex_polygon(binary_source, tw, th, effective_max_verts, &p->vertex_counts[idx]);
                if (!p->hull_vertices[idx]) {
                    /* Even the convex fallback found no usable outline — graceful error. */
                    push_content_error(p->ctx, p->state->name, p->sprites[idx].name, NT_BUILD_ERR_KIND_DEGENERATE_HULL, tw, th);
                }
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

/* --- pipeline_validate: non-mutating pre-pack checks that report every bad
 * sprite at once ------------------------------------------------------------
 *
 * Runs after geometry, before the first output gate. Everything here depends
 * only on trim dims / geometry hulls / names — never on packing — so it can run
 * even when a survivor already poisoned the atlas. Packing-dependent caps
 * (PAGES_EXHAUSTED, TOO_MANY_PAGES) stay in tile_pack / serialize. */

/* Build the empty-page fit-test hull for a unique sprite: its geometry hull, or
 * a scratch quad expanded to cover a per-sprite margin/extrude override — this
 * mirrors the packing footprint pipeline_tile_pack builds so the fit verdict
 * matches. quad must hold 4 Point2D (used only for the override case). */
static void atlas_fit_hull(const AtlasPipeline *p, uint32_t oi, Point2D quad[4], const Point2D **out_hull, uint32_t *out_count) {
    uint32_t sprite_margin = p->sprites[oi].margin_override ? p->sprites[oi].margin_override : p->opts->margin;
    uint32_t sprite_extrude = p->sprites[oi].extrude_override ? p->sprites[oi].extrude_override : p->opts->extrude;
    uint32_t extra_margin = (sprite_margin > p->opts->margin) ? (sprite_margin - p->opts->margin) : 0;
    uint32_t extra_extrude = (sprite_extrude > p->opts->extrude) ? (sprite_extrude - p->opts->extrude) : 0;
    if (extra_margin > 0 || extra_extrude > 0) {
        uint32_t tw = p->trim_w[oi] + ((extra_margin + extra_extrude) * 2);
        uint32_t th = p->trim_h[oi] + ((extra_margin + extra_extrude) * 2);
        quad[0] = (Point2D){0, 0};
        quad[1] = (Point2D){(int32_t)tw, 0};
        quad[2] = (Point2D){(int32_t)tw, (int32_t)th};
        quad[3] = (Point2D){0, (int32_t)th};
        *out_hull = quad;
        *out_count = 4;
    } else {
        *out_hull = p->hull_vertices[oi];
        *out_count = p->vertex_counts[oi];
    }
}

/* Sort key for O(n) duplicate-name detection: primary hash asc, tie-break index
 * asc so an equal-hash run is scanned oldest-first (canonical = smallest index). */
typedef struct {
    uint64_t hash;
    uint32_t index;
} NameHashEntry;

static int name_hash_cmp(const void *a, const void *b) {
    const NameHashEntry *ea = (const NameHashEntry *)a;
    const NameHashEntry *eb = (const NameHashEntry *)b;
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

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void pipeline_validate(AtlasPipeline *p) {
    /* Empty-page fit: reject any unique sprite that cannot fit an empty max_size
     * page, using the exact vector_pack fit test — an unfittable sprite becomes a
     * graceful UNFITTABLE instead of tripping vector_pack's placement invariant. */
    /* Pass 1: flag each unfittable UNIQUE sprite. O(unique). */
    bool *unfittable = (bool *)calloc(p->sprite_count, sizeof(bool));
    NT_BUILD_ASSERT(unfittable && "pipeline_validate: alloc failed");
    for (uint32_t i = 0; i < p->unique_count; i++) {
        uint32_t oi = p->unique_indices[i];
        /* A sprite that failed alpha_trim has no hull (already reported); skip it
         * so the fit test never dereferences a NULL/degenerate hull. */
        if (p->trim_w[oi] == 0 || p->trim_h[oi] == 0) {
            continue;
        }
        /* A geometry-failed survivor (DEGENERATE_HULL / CONTOUR_VERTEX_OVERFLOW)
         * has no hull and is already reported; skip it so the fit test never
         * memcpy's a NULL hull and the scratch-quad override path can't resurrect
         * it as a duplicate UNFITTABLE. */
        if (p->hull_vertices[oi] == NULL) {
            continue;
        }
        Point2D quad[4];
        const Point2D *hull = NULL;
        uint32_t hull_count = 0;
        atlas_fit_hull(p, oi, quad, &hull, &hull_count);
        if (vpack_sprite_fits_empty_page(hull, hull_count, p->opts)) {
            continue;
        }
        unfittable[oi] = true;
    }
    /* Pass 2: report the canonical sprite AND every dedup alias, in add order.
     * dedup_map stores the fully-resolved canonical (no chain walk). O(n). */
    for (uint32_t k = 0; k < p->sprite_count; k++) {
        uint32_t c = p->dedup_map[k] < 0 ? k : (uint32_t)p->dedup_map[k];
        if (!unfittable[c]) {
            continue;
        }
        /* Report the EFFECTIVE spacing the packer used: a per-sprite override only
         * RAISES the atlas baseline (a smaller override is clamped up), so the
         * record must not show a below-atlas request the fit never applied. These
         * match across dedup aliases (dedup requires equal overrides). */
        uint32_t sprite_margin = p->sprites[c].margin_override ? p->sprites[c].margin_override : p->opts->margin;
        uint32_t sprite_extrude = p->sprites[c].extrude_override ? p->sprites[c].extrude_override : p->opts->extrude;
        uint32_t effective_margin = sprite_margin > p->opts->margin ? sprite_margin : p->opts->margin;
        uint32_t effective_extrude = sprite_extrude > p->opts->extrude ? sprite_extrude : p->opts->extrude;
        p->ctx->cur_error_seq = p->sprites[k].add_seq;
        nt_build_error_t e = {.kind = NT_BUILD_ERR_KIND_UNFITTABLE,
                              .w = p->trim_w[c],
                              .h = p->trim_h[c],
                              .padding = p->opts->padding,
                              .margin = effective_margin,
                              .max_size = p->opts->max_size,
                              .detail_a = effective_extrude};
        error_copy_name(e.atlas, p->state->name);
        error_copy_name(e.sprite, p->sprites[k].name);
        nt_builder_push_error(p->ctx, &e);
    }
    free(unfittable);

    /* Region-count cap: extreme content only (>65535 sprites). Checked BEFORE the
     * duplicate-name scan — an over-cap atlas is already rejected, so the scan
     * would only add work with no new diagnostic. Atlas-level seq sorts it after
     * this atlas's per-sprite errors. */
    if (p->sprite_count > UINT16_MAX) {
        p->ctx->cur_error_seq = p->ctx->add_seq_counter;
        push_content_error(p->ctx, p->state->name, NULL, NT_BUILD_ERR_KIND_TOO_MANY_REGIONS, 0, 0);
    } else if (p->sprite_count > 0) {
        /* Duplicate region names produce ambiguous runtime name_hash lookups.
         * Hash-sort names to detect collisions in amortized O(n) — a valid
         * 65535-region atlas would otherwise hang on ~2.1B strcmp. strcmp confirms
         * only on a hash collision. Report each offending sprite once; the first
         * (smallest-index) occurrence of a name is canonical and not reported. */
        NameHashEntry *ents = (NameHashEntry *)malloc(p->sprite_count * sizeof(NameHashEntry));
        bool *is_dup = (bool *)calloc(p->sprite_count, sizeof(bool));
        NT_BUILD_ASSERT(ents && is_dup && "pipeline_validate: alloc failed");
        for (uint32_t i = 0; i < p->sprite_count; i++) {
            ents[i].hash = nt_hash64_str(p->sprites[i].name).value;
            ents[i].index = i;
        }
        qsort(ents, p->sprite_count, sizeof(NameHashEntry), name_hash_cmp);
        /* Within each equal-hash run (sorted by index asc), a sprite is a duplicate
         * if an earlier-index entry shares its exact name. */
        for (uint32_t a = 1; a < p->sprite_count; a++) {
            for (uint32_t b = a; b > 0 && ents[b - 1].hash == ents[a].hash; b--) {
                if (strcmp(p->sprites[ents[b - 1].index].name, p->sprites[ents[a].index].name) == 0) {
                    is_dup[ents[a].index] = true;
                    break;
                }
            }
        }
        /* Report in add order so seqs and truncation behavior stay deterministic. */
        for (uint32_t j = 0; j < p->sprite_count; j++) {
            if (is_dup[j]) {
                p->ctx->cur_error_seq = p->sprites[j].add_seq;
                push_content_error(p->ctx, p->state->name, p->sprites[j].name, NT_BUILD_ERR_KIND_DUPLICATE_NAME, 0, 0);
            }
        }
        free(ents);
        free(is_dup);
    }

    /* Per-sprite trim-dim limits (independent of packing): source dims must fit
     * the uint16 region fields, trim offsets the int16 fields. Skip sprites that
     * failed alpha_trim — already reported, and their trim dims are 0. */
    for (uint32_t i = 0; i < p->sprite_count; i++) {
        if (p->trim_w[i] == 0 || p->trim_h[i] == 0) {
            continue;
        }
        p->ctx->cur_error_seq = p->sprites[i].add_seq;
        if (p->sprites[i].width > UINT16_MAX || p->sprites[i].height > UINT16_MAX) {
            push_content_error(p->ctx, p->state->name, p->sprites[i].name, NT_BUILD_ERR_KIND_SPRITE_TOO_LARGE, p->sprites[i].width, p->sprites[i].height);
            continue;
        }
        int32_t trim_offset_y_up = (int32_t)p->sprites[i].height - (int32_t)p->trim_y[i] - (int32_t)p->trim_h[i];
        if (p->trim_x[i] > INT16_MAX || trim_offset_y_up < INT16_MIN || trim_offset_y_up > INT16_MAX) {
            push_content_error(p->ctx, p->state->name, p->sprites[i].name, NT_BUILD_ERR_KIND_TRIM_OFFSET_OVERFLOW, 0, 0);
        }
    }
}

/* --- pipeline_tile_pack: sort sprites by area, place on atlas pages via tile-grid collision --- */

/* Free the per-unique scratch pipeline_tile_pack allocates (incl. the override
 * scratch hulls). Shared by the normal tail and the pages-exhausted early-out
 * so every exit frees the same set. */
static void tile_pack_free_scratch(AtlasPipeline *p, uint32_t *u_trim_w, uint32_t *u_trim_h, Point2D **u_hulls, uint32_t *u_hull_counts, bool *u_no_rotate) {
    for (uint32_t i = 0; i < p->unique_count; i++) {
        uint32_t oi = p->unique_indices[i];
        if (u_hulls[i] != p->hull_vertices[oi]) {
            free(u_hulls[i]);
        }
    }
    free(u_trim_w);
    free(u_trim_h);
    free((void *)u_hulls);
    free(u_hull_counts);
    free(u_no_rotate);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
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

    /* Per-sprite margin/extrude override: expand trim dims so the packing
     * footprint covers the full content + extrude band + margin.  vector_pack
     * inflates all hulls by atlas-level (extrude + padding/2), so any per-sprite
     * extrude that exceeds the atlas default needs extra tile space here.
     * Use a scratch hull so the original hull_vertices stay untouched for
     * serialization. */
    for (uint32_t i = 0; i < p->unique_count; i++) {
        uint32_t oi = p->unique_indices[i];
        uint32_t sprite_margin = p->sprites[oi].margin_override ? p->sprites[oi].margin_override : p->opts->margin;
        uint32_t sprite_extrude = p->sprites[oi].extrude_override ? p->sprites[oi].extrude_override : p->opts->extrude;
        uint32_t extra_margin = (sprite_margin > p->opts->margin) ? (sprite_margin - p->opts->margin) : 0;
        uint32_t extra_extrude = (sprite_extrude > p->opts->extrude) ? (sprite_extrude - p->opts->extrude) : 0;
        if (extra_margin > 0 || extra_extrude > 0) {
            u_trim_w[i] += (extra_margin + extra_extrude) * 2;
            u_trim_h[i] += (extra_margin + extra_extrude) * 2;
            /* Allocate scratch hull — original hull_vertices must survive for serialize */
            Point2D *scratch = (Point2D *)malloc(4 * sizeof(Point2D));
            NT_BUILD_ASSERT(scratch && "pipeline_tile_pack: scratch hull alloc failed");
            scratch[0] = (Point2D){0, 0};
            scratch[1] = (Point2D){(int32_t)u_trim_w[i], 0};
            scratch[2] = (Point2D){(int32_t)u_trim_w[i], (int32_t)u_trim_h[i]};
            scratch[3] = (Point2D){0, (int32_t)u_trim_h[i]};
            u_hulls[i] = scratch;
            u_hull_counts[i] = 4; /* scratch is a 4-vertex quad; keep count in sync or downstream reads OOB */
        }
    }

    /* Per-sprite rotation override for vector_pack. */
    bool *u_no_rotate = (bool *)calloc(p->unique_count, sizeof(bool));
    NT_BUILD_ASSERT(u_no_rotate && "pipeline_tile_pack: alloc failed");
    for (uint32_t i = 0; i < p->unique_count; i++) {
        uint32_t oi = p->unique_indices[i];
        if (p->sprites[oi].rotate_override == NT_ATLAS_SPRITE_ROTATE_NO) {
            u_no_rotate[i] = true;
        }
    }

    /* Empty-page fit is validated up front in pipeline_validate (every sprite +
     * dedup alias), so by here every sprite provably fits — vector_pack's "empty
     * page should accept placement" invariant cannot trip. */
    NT_LOG_INFO("  vector_pack: %u sprites (NFP mode)", p->unique_count);
    bool pages_exhausted = false;
    p->placement_count = vector_pack(u_trim_w, u_trim_h, u_hulls, u_hull_counts, p->unique_count, p->opts, u_no_rotate, p->placements, &p->page_count, p->page_w, p->page_h, &p->stats, p->thread_count,
                                     &pages_exhausted);
    if (pages_exhausted) {
        /* ATLAS_MAX_PAGES exhausted — vector_pack already joined its worker pool
         * and freed its buffers; report gracefully and bail. */
        p->ctx->cur_error_seq = p->ctx->add_seq_counter; /* atlas-level: sort after this atlas's sprite errors */
        nt_build_error_t e = {.kind = NT_BUILD_ERR_KIND_PAGES_EXHAUSTED, .max_size = p->opts->max_size, .detail_a = ATLAS_MAX_PAGES};
        error_copy_name(e.atlas, p->state->name);
        nt_builder_push_error(p->ctx, &e);
        p->placement_count = 0;
        /* vector_pack freed its pages; compose is skipped so page_pixels stays NULL
         * and pipeline_cleanup's guard handles the (nonzero) page_count safely. */
        tile_pack_free_scratch(p, u_trim_w, u_trim_h, u_hulls, u_hull_counts, u_no_rotate);
        return;
    }
    pack_stats_measure_payload(&p->stats, u_trim_w, u_trim_h, u_hulls, u_hull_counts, p->unique_count, p->opts);

    /* Fill trim offsets and remap sprite_index back to original. */
    for (uint32_t i = 0; i < p->placement_count; i++) {
        uint32_t unique_idx = p->placements[i].sprite_index;
        uint32_t orig_idx = p->unique_indices[unique_idx];
        p->placements[i].sprite_index = orig_idx;
        p->placements[i].trim_x = p->trim_x[orig_idx];
        p->placements[i].trim_y = p->trim_y[orig_idx];
        p->placements[i].trimmed_w = p->trim_w[orig_idx];
        p->placements[i].trimmed_h = p->trim_h[orig_idx];
    }

    tile_pack_free_scratch(p, u_trim_w, u_trim_h, u_hulls, u_hull_counts, u_no_rotate);
}

/* --- pipeline_compose: blit trimmed pixels onto pages + extrude edges --- */

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void pipeline_compose(AtlasPipeline *p) {
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
        uint32_t sprite_extrude = p->sprites[idx].extrude_override ? p->sprites[idx].extrude_override : p->opts->extrude;
        uint32_t inner_x = pl->x + sprite_extrude;
        uint32_t inner_y = pl->y + sprite_extrude;

        blit_sprite(p->page_pixels[pl->page], p->page_w[pl->page], p->sprites[idx].rgba, p->sprites[idx].width, pl->trim_x, pl->trim_y, pl->trimmed_w, pl->trimmed_h, inner_x, inner_y, pl->transform);

        uint32_t blit_w = (pl->transform & 4) ? pl->trimmed_h : pl->trimmed_w;
        uint32_t blit_h = (pl->transform & 4) ? pl->trimmed_w : pl->trimmed_h;
        extrude_edges(p->page_pixels[pl->page], p->page_w[pl->page], p->page_h[pl->page], inner_x, inner_y, blit_w, blit_h, sprite_extrude);
    }
}

/* --- pipeline_debug_png: optional outline visualization --- */

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void pipeline_debug_png(AtlasPipeline *p) {
    if (!p->opts->debug_png) {
        return;
    }

    for (uint32_t pg = 0; pg < p->page_count; pg++) {
        size_t page_bytes = (size_t)p->page_w[pg] * p->page_h[pg] * 4;
        uint8_t *debug_page = (uint8_t *)malloc(page_bytes);
        NT_BUILD_ASSERT(debug_page && "pipeline_debug_png: alloc failed");
        memcpy(debug_page, p->page_pixels[pg], page_bytes);

        for (uint32_t pi = 0; pi < p->placement_count; pi++) {
            if (p->placements[pi].page != pg) {
                continue;
            }
            uint32_t si = p->placements[pi].sprite_index;
            uint32_t sprite_extrude = p->sprites[si].extrude_override ? p->sprites[si].extrude_override : p->opts->extrude;
            uint32_t ix = p->placements[pi].x + sprite_extrude;
            uint32_t iy = p->placements[pi].y + sprite_extrude;

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

/* QUAD_* flag names describe the ear-clip / fan output BEFORE winding swap —
 * detection runs on PNG-space CCW patterns. The blob stores the swapped form
 * (see swap_triangle_winding below); runtime emit_one's hardcoded pattern is
 * the swapped one too. Renaming the flags would churn pack format v4 for no
 * gain, so the names stay as-is and this comment carries the contract. */
static uint8_t atlas_region_flags_from_indices(uint32_t vertex_count, uint32_t index_count, const uint16_t *indices) {
    if (vertex_count != 4 || index_count != 6 || indices == NULL) {
        return 0;
    }
    if (indices[0] == 0 && indices[1] == 1 && indices[2] == 2 && indices[3] == 0 && indices[4] == 2 && indices[5] == 3) {
        return NT_ATLAS_REGION_FLAG_QUAD_012023;
    }
    if (indices[0] == 0 && indices[1] == 1 && indices[2] == 2 && indices[3] == 1 && indices[4] == 3 && indices[5] == 0) {
        return NT_ATLAS_REGION_FLAG_QUAD_012130;
    }
    if (indices[0] == 0 && indices[1] == 1 && indices[2] == 2 && indices[3] == 1 && indices[4] == 3 && indices[5] == 2) {
        return NT_ATLAS_REGION_FLAG_QUAD_012132;
    }
    return 0;
}

/* Swap winding of every triangle in a triangle-list index buffer.
 *
 * Builder ear-clip / fan output is PNG-space CCW. The blob writer Y-flips
 * each vertex's local_y to y-up before serialising (see local_y assignment
 * in pipeline_serialize) — that reflection inverts the cross-product sign,
 * turning the original CCW into CW in world-space. Pre-swapping each
 * triangle (a,b,c)→(a,c,b) here compensates: blob indices read CW in
 * PNG-space and CCW in world-space, so GL cull_mode = BACK works without
 * per-game opt-outs. UVs are not flipped — they index raw pixel rows. */
static void swap_triangle_winding(uint16_t *indices, uint32_t index_count) {
    NT_BUILD_ASSERT(index_count % 3U == 0 && "swap_triangle_winding: index_count must be a multiple of 3 (triangle list)");
    for (uint32_t i = 0; i + 2U < index_count; i += 3U) {
        uint16_t tmp = indices[i + 1U];
        indices[i + 1U] = indices[i + 2U];
        indices[i + 2U] = tmp;
    }
}

/* --- pipeline_serialize: compute atlas UVs, write binary blob --- */

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void pipeline_serialize(AtlasPipeline *p) {
    const uint32_t max_region_tri_count = (uint32_t)(UINT8_MAX / 3U);
    /* Duplicate names, region-count cap and per-sprite trim-dim limits are all
     * validated pre-pack in pipeline_validate, so serialize is only ever reached
     * unpoisoned with every sprite within uint16 bounds. */

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
    /* Page-count cap needs the packed page_count, so it stays here (region count
     * is checked pre-pack in pipeline_validate). Extreme content only (>65535
     * pages). Push and proceed — the truncated blob is never written (finish_pack
     * poison gate). Atlas-level seq sorts it after this atlas's sprite errors. */
    if (p->page_count > UINT16_MAX) {
        p->ctx->cur_error_seq = p->ctx->add_seq_counter;
        push_content_error(p->ctx, p->state->name, NULL, NT_BUILD_ERR_KIND_TOO_MANY_PAGES, 0, 0);
    }
    hdr->region_count = (uint16_t)p->sprite_count;
    hdr->page_count = (uint16_t)p->page_count;
    hdr->_pad = 0;
    hdr->vertex_offset = vertex_offset;
    hdr->total_vertex_count = total_vertex_count;
    hdr->index_offset = index_offset;
    hdr->total_index_count = total_index_count;

    /* Texture resource IDs */
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
    uint8_t *sprite_flags = (uint8_t *)calloc(p->sprite_count, sizeof(uint8_t));
    NT_BUILD_ASSERT(sprite_vertex_start && sprite_index_start && sprite_idx_count && sprite_flags && "pipeline_serialize: alloc failed");

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
        /* Flag detection runs on PNG-CCW pattern — must precede the winding
         * swap below or every QUAD_* match would miss. */
        sprite_flags[i] = atlas_region_flags_from_indices(p->vertex_counts[i], idx_count, local_indices);
        swap_triangle_winding(local_indices, idx_count);

        /* Write triangle indices (local: 0..vertex_count-1, world-CCW after Y-flip) */
        memcpy(&indices[index_cursor], local_indices, idx_count * sizeof(uint16_t));
        index_cursor += idx_count;

        uint32_t s_extrude = p->sprites[i].extrude_override ? p->sprites[i].extrude_override : p->opts->extrude;
        uint32_t inner_x = pl->x + s_extrude;
        uint32_t inner_y = pl->y + s_extrude;
        uint32_t atlas_w = p->page_w[pl->page];
        uint32_t atlas_h = p->page_h[pl->page];

        for (uint32_t v = 0; v < p->vertex_counts[i]; v++) {
            NtAtlasVertex *vtx = &vertices[vertex_cursor++];
            int32_t lx = p->hull_vertices[i][v].x;
            int32_t ly = p->hull_vertices[i][v].y;
            /* Y-flip vertex into y-up local space at the blob boundary (v5).
             * Builder's hull/triangulator/UV math all operate in PNG y-down;
             * only the on-disk vertex flips so runtime can read it as-is in
             * the engine's y-up world. UV.v stays y-down on purpose — it
             * indexes raw pixel rows, which the GL upload still receives
             * top-row-first. transform_point gets the original PNG-space ly
             * because it computes atlas_v, not local_y. */
            int32_t ly_up = (int32_t)p->trim_h[i] - ly;
            NT_BUILD_ASSERT(lx >= INT16_MIN && lx <= INT16_MAX && "pipeline_serialize: local_x overflows int16_t");
            NT_BUILD_ASSERT(ly_up >= INT16_MIN && ly_up <= INT16_MAX && "pipeline_serialize: local_y overflows int16_t after y-up flip");
            vtx->local_x = (int16_t)lx;
            vtx->local_y = (int16_t)ly_up;

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
            sprite_flags[i] = sprite_flags[orig];
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
        NT_BUILD_ASSERT(reg->name_hash != 0xFFFFFFFFFFFFFFFFULL && "pipeline_serialize: region name_hash collides with runtime tombstone sentinel");
        /* Sprite dims and trim offsets are validated pre-pack in pipeline_validate
         * (SPRITE_TOO_LARGE / TRIM_OFFSET_OVERFLOW), so the uint16/int16 casts
         * below are provably in range here. */
        reg->source_w = (uint16_t)p->sprites[i].width;
        reg->source_h = (uint16_t)p->sprites[i].height;
        /* trim_offset_y_up = pixels stripped from the BOTTOM edge in y-up
         * source space. Builder's alpha-trim records trim_y as pixels stripped
         * from the PNG-top, so the y-up conversion is source_h - trim_y - trim_h. */
        int32_t trim_offset_y_up = (int32_t)p->sprites[i].height - (int32_t)p->trim_y[i] - (int32_t)p->trim_h[i];
        reg->trim_offset_x = (int16_t)p->trim_x[i];
        reg->trim_offset_y = (int16_t)trim_offset_y_up;
        reg->origin_x = p->sprites[i].origin_x;
        /* origin_y in y-up: 0 = bottom edge, 1 = top edge. PNG convention is
         * 0 = top, so flip at write time. Values outside [0,1] (off-frame
         * pivots) flip symmetrically. */
        reg->origin_y = 1.0F - p->sprites[i].origin_y;
        reg->vertex_start = sprite_vertex_start[i];
        reg->index_start = sprite_index_start[i];
        reg->vertex_count = (uint8_t)p->vertex_counts[i];
        reg->page_index = (uint8_t)pl->page;
        reg->transform = pl->transform;
        reg->index_count = (uint8_t)sprite_idx_count[i];
        reg->flags = sprite_flags[i];
        /* Builder-side invariant: any QUAD_* flag implies vertex_count==4 +
         * index_count==6. atlas_region_flags_from_indices already returns 0
         * unless those hold, but assert here so future flag additions can't
         * silently break the runtime contract (which trusts the bit). */
        NT_BUILD_ASSERT(((reg->flags & NT_ATLAS_REGION_FLAG_QUAD_MASK) == 0 || (reg->vertex_count == 4 && reg->index_count == 6)) &&
                        "atlas region: QUAD_* flag set but vertex_count/index_count don't match — builder bug");
        /* Slice9 borders */
        uint16_t sl = p->sprites[i].slice9_left;
        uint16_t sr = p->sprites[i].slice9_right;
        uint16_t st = p->sprites[i].slice9_top;
        uint16_t sb = p->sprites[i].slice9_bottom;
        if (sl || sr || st || sb) {
            /* pipeline_alpha_trim already reports the slice9 case; this
             * defense-in-depth re-check keeps direct pipeline_serialize calls safe. */
            if ((uint32_t)sl + (uint32_t)sr >= p->sprites[i].width || (uint32_t)st + (uint32_t)sb >= p->sprites[i].height) {
                p->ctx->cur_error_seq = p->sprites[i].add_seq;
                push_content_error(p->ctx, p->state->name, p->sprites[i].name, NT_BUILD_ERR_KIND_SLICE9_TOO_BIG, p->sprites[i].width, p->sprites[i].height);
                continue;
            }
        }
        reg->_pad0 = 0;
        reg->slice9_lrtb[0] = sl;
        reg->slice9_lrtb[1] = sr;
        reg->slice9_lrtb[2] = st;
        reg->slice9_lrtb[3] = sb;
        memset(reg->_reserved2, 0, sizeof(reg->_reserved2));
    }

    free(sprite_vertex_start);
    free(sprite_index_start);
    free(sprite_idx_count);
    free(sprite_flags);

    /* Register atlas metadata entry */
    uint64_t blob_hash = nt_hash64(blob, blob_size).value;
    nt_builder_add_entry(p->ctx, p->state->name, NT_BUILD_ASSET_ATLAS, NULL, blob, blob_size, blob_hash);

    // #region pixels_per_unit metadata
    /* Write pixels_per_unit as a 4-byte resource metadata blob keyed by
     * hash64_str("pixels_per_unit"). Atlas binary format (v5) is unchanged —
     * runtime reads this via nt_atlas_get_pixels_per_unit and bakes 1/ppu
     * into cached_pos so HD packs render at the same on-screen size as SD
     * packs sharing the same Transform.
     *
     * Written unconditionally (even when value == 1.0F) — uniform code path,
     * 4 bytes are negligible, and keeps the round-trip tests symmetric. The
     * resource_id must mirror nt_builder_add_entry's hashing exactly: the
     * normalized atlas state name. */
    NT_BUILD_ASSERT(p->opts->pixels_per_unit > 0.0F && isfinite(p->opts->pixels_per_unit));
    char *atlas_norm_path = nt_builder_normalize_path(p->state->name);
    NT_BUILD_ASSERT(atlas_norm_path);
    const uint64_t atlas_resource_id = nt_hash64_str(atlas_norm_path).value;
    free(atlas_norm_path);
    const uint64_t kind_ppu = nt_hash64_str("pixels_per_unit").value;
    const float ppu = p->opts->pixels_per_unit;
    nt_builder_add_meta(p->ctx, atlas_resource_id, kind_ppu, &ppu, sizeof(float));
    // #endregion

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
        /* Propagate atlas-level sampler defaults to the page texture header so
         * the activator creates the right sampler for this atlas page. */
        td->opts.filter_min = p->opts->filter_min;
        td->opts.filter_mag = p->opts->filter_mag;
        td->opts.wrap_u = p->opts->wrap_u;
        td->opts.wrap_v = p->opts->wrap_v;
        td->opts.gen_mipmaps = p->opts->gen_mipmaps;
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

        /* path includes atlas prefix for unique C identifiers in codegen
         * (ASSET_ATLAS_REGION_SPINEBOY_HEAD_PNG), but resource_id hashes only
         * the sprite name — runtime looks up regions within a specific atlas
         * by name_hash, not by the full atlas/sprite path. */
        char region_path[512];
        (void)snprintf(region_path, sizeof(region_path), "%s/%s", p->state->name, p->sprites[i].name);

        NtAtlasRegionCodegen *reg = &p->ctx->atlas_regions[p->ctx->atlas_region_count++];
        reg->path = nt_builder_normalize_path(region_path);
        reg->resource_id = nt_hash64_str(p->sprites[i].name).value;
    }
}

/* Free an atlas state's owned buffers. Used by the poison early-out in
 * end_atlas, before any pipeline scratch is allocated. */
static void atlas_state_free(NtBuildAtlasState *state) {
    for (uint32_t i = 0; i < state->sprite_count; i++) {
        free(state->sprites[i].rgba);
        free(state->sprites[i].name);
    }
    free(state->sprites);
    free(state->name);
    free(state);
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

    /* Free hull vertices (duplicates share pointers via dedup_map).
     * NULL-guarded: an early-poison exit skips dedup+geometry, so neither array
     * exists yet. */
    if (p->dedup_map && p->hull_vertices) {
        for (uint32_t i = 0; i < p->sprite_count; i++) {
            if (p->dedup_map[i] < 0) {
                free(p->hull_vertices[i]);
            }
            p->hull_vertices[i] = NULL;
        }
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

    /* Free remaining page pixels (any not transferred to entries). page_pixels is
     * NULL when a poisoned atlas skipped compose — page_count is reset to 0 there,
     * but guard anyway so cleanup never derefs a NULL page array. */
    if (p->page_pixels) {
        for (uint32_t pg = 0; pg < p->page_count; pg++) {
            free(p->page_pixels[pg]);
        }
        free((void *)p->page_pixels);
    }

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

    /* After poison, begin_atlas no-ops so subsequent atlases have no
     * active state — make end a no-op too instead of asserting. */
    if (ctx->poisoned && !ctx->active_atlas) {
        return;
    }
    NT_BUILD_ASSERT(ctx->active_atlas && "end_atlas: no active atlas");

    NtBuildAtlasState *state = ctx->active_atlas;

    /* When poisoned mid-adds, still run the pre-packing VALIDATION stages on the
     * surviving good sprites so every bad sprite in this atlas is reported — not
     * just the one that first poisoned. Only bail early when nothing remains to
     * validate; packing/compose/serialize stay gated off below. */
    if (ctx->poisoned && state->sprite_count == 0) {
        atlas_state_free(state);
        ctx->active_atlas = NULL;
        return;
    }

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

    /* No poison gate here: geometry must still run on the surviving sprites so it
     * can report bad hulls too (cross-stage collect-all). The first hard gate
     * sits after geometry, before packing. Skip the cache probe once poisoned —
     * the result is discarded and its "cache hit" log would mislead. */
    if (!ctx->poisoned) {
        pipeline_cache_check(&p);
    }

    t0 = nt_time_now();
    pipeline_dedup(&p);
    double bench_dedup = nt_time_now() - t0;

    NT_LOG_INFO("  prep: %u sprites (%u unique), starting geometry...", p.sprite_count, p.unique_count);
    t0 = nt_time_now();
    pipeline_geometry(&p);
    double bench_geometry = nt_time_now() - t0;
    NT_LOG_INFO("  geometry done in %.1fs", bench_geometry);

    /* Non-mutating pre-pack validation on every surviving sprite (empty-page fit
     * incl. dedup aliases, duplicate names, region/trim-dim caps) — runs even
     * when a survivor already poisoned, so every bad sprite is reported at once. */
    pipeline_validate(&p);

    /* First hard gate: a content error from trim, geometry or validate skips all
     * packing, compose, serialize and register — fall through to the single
     * cleanup. Everything downstream provably has only fittable, in-range,
     * uniquely-named sprites. */
    if (ctx->poisoned) {
        goto cleanup;
    }

    double bench_tile_pack = 0.0;
    double bench_compose = 0.0;
    double bench_debug_png = 0.0;
    if (!p.cache_hit) {
        t0 = nt_time_now();
        pipeline_tile_pack(&p);
        bench_tile_pack = nt_time_now() - t0;

        /* A PAGES_EXHAUSTED (vector_pack) content error skips compose/serialize
         * and falls through to the one cleanup block. */
        if (ctx->poisoned) {
            goto cleanup;
        }

        t0 = nt_time_now();
        pipeline_compose(&p);
        bench_compose = nt_time_now() - t0;

        pipeline_cache_write(&p);
    }

    /* debug_png runs regardless of cache — page_pixels are available from
     * either compose (miss) or cache read (hit). Not part of cache key. */
    t0 = nt_time_now();
    pipeline_debug_png(&p);
    bench_debug_png = nt_time_now() - t0;

    t0 = nt_time_now();
    pipeline_serialize(&p);
    /* A content error found during serialize (duplicate name / size-limit) must
     * not register texture pages — route to the single cleanup exit. */
    if (ctx->poisoned) {
        goto cleanup;
    }
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

cleanup:
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
