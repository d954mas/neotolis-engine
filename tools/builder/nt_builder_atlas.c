/* clang-format off */
#include "nt_builder_internal.h"
#include "nt_builder_atlas_geometry.h"
#include "nt_builder_atlas_test.h"
#include "nt_builder_atlas_vpack.h"
#include "nt_clipper2_bridge.h"
#include "hash/nt_hash.h"
#include "nt_atlas_format.h"
#include "time/nt_time.h"
#include "stb_image.h"
#include "stb_image_write.h"
/* clang-format on */

#include <ctype.h>
#include <limits.h>
#include <math.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

/* --- Content-error channel --- */

/* Keep both ends and a hash so truncated diagnostics stay identifiable. */
static void error_copy_name(char *dst, const char *src) {
    if (!src) {
        dst[0] = '\0';
        return;
    }
    size_t len = strlen(src);
    if (len < NT_BUILD_ERR_NAME_MAX) {
        memcpy(dst, src, len + 1);
        return;
    }
    uint64_t hash = nt_hash64_str(src).value;
    (void)snprintf(dst, NT_BUILD_ERR_NAME_MAX, "%.48s...%.48s#%016llx", src, src + len - 48, (unsigned long long)hash);
}

static void atlas_push_error(NtAtlasBuild *atlas, uint32_t seq, const nt_build_error_t *err) {
    if (atlas->error_count >= NT_BUILD_MAX_ERRORS) {
        atlas->errors_truncated = true;
        atlas->failed = true;
        /* Keep the add-order prefix: an earlier error evicts the current tail
         * (highest seq). A later/equal error stays outside the prefix — drop it. */
        if (atlas->error_seq[NT_BUILD_MAX_ERRORS - 1] <= seq) {
            return;
        }
        atlas->error_count = NT_BUILD_MAX_ERRORS - 1;
    }
    /* Insert keeping error_seq[] non-decreasing, after equal-seq entries
     * (stable) — so errors[] stays add-sorted and errors[0] is the earliest
     * offender. O(n) shift on the failure path only, n <= NT_BUILD_MAX_ERRORS. */
    uint32_t pos = atlas->error_count;
    while (pos > 0 && atlas->error_seq[pos - 1] > seq) {
        atlas->errors[pos] = atlas->errors[pos - 1];
        atlas->error_seq[pos] = atlas->error_seq[pos - 1];
        pos--;
    }
    atlas->errors[pos] = *err;
    atlas->error_seq[pos] = seq;
    atlas->error_count++;
    atlas->failed = true;
}

/* Fill atlas+sprite names and append a content error.
 * sprite may be NULL for atlas-level errors. */
static void push_content_error(NtAtlasBuild *atlas, uint32_t seq, const char *sprite, nt_build_error_kind kind, uint32_t w, uint32_t h) {
    nt_build_error_t e = {.kind = kind, .w = w, .h = h};
    error_copy_name(e.atlas, atlas->name);
    error_copy_name(e.sprite, sprite);
    atlas_push_error(atlas, seq, &e);
}

static nt_build_result_t atlas_merge_errors(NtAtlasBuild *atlas) {
    NtBuilderContext *ctx = atlas->ctx;
    uint32_t available = NT_BUILD_MAX_ERRORS - ctx->error_count;
    for (uint32_t i = 0; i < atlas->error_count; i++) {
        if (ctx->error_count >= NT_BUILD_MAX_ERRORS) {
            ctx->errors_truncated = true;
            break;
        }
        ctx->errors[ctx->error_count++] = atlas->errors[i];
    }
    if (atlas->errors_truncated || atlas->error_count > available) {
        ctx->errors_truncated = true;
    }
    ctx->failed = true;
    nt_build_result_t result = nt_builder_result_from_error(atlas->error_count ? &atlas->errors[0] : NULL);
    nt_build_result_t invalidate_result = nt_builder_invalidate_outputs(ctx);
    return invalidate_result == NT_BUILD_OK ? result : invalidate_result;
}

void nt_build_error_format(const nt_build_error_t *err, char *buf, size_t len) {
    NT_BUILD_ASSERT(err && buf && "error_format: err and buf must be non-NULL");
    switch (err->kind) {
    case NT_BUILD_ERR_KIND_CORRUPT_IMAGE:
        (void)snprintf(buf, len, "atlas '%s', sprite '%s': corrupt or undecodable image", err->atlas, err->sprite);
        break;
    case NT_BUILD_ERR_KIND_ZERO_DIM:
        (void)snprintf(buf, len, "atlas '%s', sprite '%s': zero width or height", err->atlas, err->sprite);
        break;
    case NT_BUILD_ERR_KIND_IMAGE_TOO_LARGE:
        /* stbi_info leaves dims unwritten on an oversized-header reject — omit the
         * bogus 0x0 when either dimension is unknown. */
        if (err->w == 0 || err->h == 0) {
            (void)snprintf(buf, len, "atlas '%s', sprite '%s': image exceeds decode limit (dimensions too large to read)", err->atlas, err->sprite);
        } else {
            (void)snprintf(buf, len, "atlas '%s', sprite '%s': image %ux%u exceeds decode limit", err->atlas, err->sprite, err->w, err->h);
        }
        break;
    case NT_BUILD_ERR_KIND_ATLAS_TRANSPARENT_AFTER_TRIM:
        (void)snprintf(buf, len, "atlas '%s', sprite '%s': fully transparent after trim", err->atlas, err->sprite);
        break;
    case NT_BUILD_ERR_KIND_ATLAS_SLICE9_TOO_BIG:
        (void)snprintf(buf, len, "atlas '%s', sprite '%s': slice9 borders (%u) >= source extent (%u)", err->atlas, err->sprite, err->detail_a, err->detail_b);
        break;
    case NT_BUILD_ERR_KIND_ATLAS_DEGENERATE_HULL:
        (void)snprintf(buf, len, "atlas '%s', sprite '%s': degenerate hull (no usable outline)", err->atlas, err->sprite);
        break;
    case NT_BUILD_ERR_KIND_ATLAS_HULL_INFEASIBLE: {
        const char *shape = "concave";
        if (err->detail_b == NT_ATLAS_SHAPE_RECT) {
            shape = "rect";
        } else if (err->detail_b == NT_ATLAS_SHAPE_CONVEX_HULL) {
            shape = "convex";
        }
        (void)snprintf(buf, len, "atlas '%s', sprite '%s': no covering %s hull fits the hard ceiling of %u vertices", err->atlas, err->sprite, shape, err->detail_a);
        break;
    }
    case NT_BUILD_ERR_KIND_ATLAS_CONTOUR_VERTEX_OVERFLOW:
        (void)snprintf(buf, len, "atlas '%s', sprite '%s': contour exceeds vertex budget", err->atlas, err->sprite);
        break;
    case NT_BUILD_ERR_KIND_ATLAS_DUPLICATE_REGION_NAME:
        (void)snprintf(buf, len, "atlas '%s': duplicate region name '%s'", err->atlas, err->sprite);
        break;
    case NT_BUILD_ERR_KIND_ATLAS_TOO_MANY_REGIONS:
        (void)snprintf(buf, len, "atlas '%s': region count exceeds 65535", err->atlas);
        break;
    case NT_BUILD_ERR_KIND_ATLAS_SPRITE_TOO_LARGE:
        (void)snprintf(buf, len, "atlas '%s', sprite '%s': %ux%u exceeds 65535px", err->atlas, err->sprite, err->w, err->h);
        break;
    case NT_BUILD_ERR_KIND_ATLAS_TRIM_OFFSET_OVERFLOW:
        (void)snprintf(buf, len, "atlas '%s', sprite '%s': trim offset exceeds int16 range (x=%u, y=%u)", err->atlas, err->sprite, err->w, err->h);
        break;
    case NT_BUILD_ERR_KIND_ATLAS_PAGES_EXHAUSTED:
        (void)snprintf(buf, len, "atlas '%s': ran out of pages (max_size=%u)", err->atlas, err->max_size);
        break;
    case NT_BUILD_ERR_KIND_ATLAS_UNFITTABLE:
        (void)snprintf(buf, len, "atlas '%s', sprite '%s': %ux%u + padding %u + margin %u + extrude %u does not fit an empty page of max_size %u", err->atlas, err->sprite, err->w, err->h,
                       err->padding, err->margin, err->detail_a, err->max_size);
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
 * -- Pipeline (nt_atlas_commit) ---------------------------------------
 *
 *  pipeline_alpha_trim     Extract alpha plane, find tight bounding box
 *  pipeline_cache_check    Hash inputs, load cached placement/pages
 *  pipeline_dedup          Detect duplicate sprites (hash + pixels)
 *  pipeline_geometry       Polygon outline -> simplify -> inflate (convex hull or concave contour)
 *  pipeline_validate       Non-mutating pre-pack checks (fit, dup names, caps) — reports all bad sprites
 *  --- skip on cache hit ---
 *  pipeline_tile_pack      Place sprites via vector_pack (NFP/Minkowski)
 *  pipeline_compose        Blit trimmed pixels + extrude edges
 *  --- always ---
 *  pipeline_serialize      Compute atlas UVs, write binary blob
 *  pipeline_cache_write    Store result for next build
 *  pipeline_debug_png      Optional outline visualization
 *  pipeline_publish_outputs Register atlas, pages, metadata and regions
 *  pipeline_cleanup        Free all temporary allocations
 *
 * -- File layout ------------------------------------------------------
 *
 *  Toolbox regions contain helper functions grouped by concern.
 *  Pipeline step functions (pipeline_*) and the orchestrator
 *  (nt_atlas_commit) are at the bottom of the file.
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

// #region Duplicate detection — identify identical sprites by content
/* --- Duplicate detection sort comparator --- */

typedef struct {
    uint32_t index;
    uint64_t dim_key; /* D4-invariant post-trim dimension pair */
    uint64_t hash;    /* canonical post-trim content hash */
} DedupSortEntry;

/* Sorting by dim_key first keeps the orientation hashes confined to buckets that can
 * actually fold; the index tie-break is what makes the first entry of a run its
 * lowest-add-index member, which is the group root. */
static int dedup_sort_cmp(const void *a, const void *b) {
    const DedupSortEntry *ea = (const DedupSortEntry *)a;
    const DedupSortEntry *eb = (const DedupSortEntry *)b;
    if (ea->dim_key != eb->dim_key) {
        return ea->dim_key < eb->dim_key ? -1 : 1;
    }
    if (ea->hash != eb->hash) {
        return ea->hash < eb->hash ? -1 : 1;
    }
    if (ea->index != eb->index) {
        return ea->index < eb->index ? -1 : 1;
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
                        uint32_t dest_y, uint8_t rotation, uint8_t alpha_threshold) {
    /* Only retained pixels may touch the page; polygon packing leaves neighbor
     * pixels inside the same trim rect unowned. */
    // #region Rotation 0 fast path: row-wise scan with opaque span memcpy
    if (rotation == 0) {
        for (uint32_t sy = 0; sy < trim_h; sy++) {
            const uint8_t *src_row = &sprite_rgba[((size_t)(trim_y + sy) * sprite_w + trim_x) * 4];
            uint8_t *dst_row = &page[((size_t)(dest_y + sy) * page_w + dest_x) * 4];
            uint32_t sx = 0;
            while (sx < trim_w) {
                while (sx < trim_w && src_row[(sx * 4) + 3] < alpha_threshold) {
                    sx++;
                }
                if (sx >= trim_w) {
                    break;
                }
                uint32_t run_start = sx;
                while (sx < trim_w && src_row[(sx * 4) + 3] >= alpha_threshold) {
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
            if (src[3] < alpha_threshold) {
                continue;
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

enum { ATLAS_CACHE_KEY_VERSION = 22 };

/* Per-sprite key record: hash + source dims + origin, then the raw override block. */
enum { KEY_OV_OFF = sizeof(uint64_t) + (2 * sizeof(uint32_t)) + (2 * sizeof(float)) };
enum { PER_SPRITE_SIZE = KEY_OV_OFF + (4 * sizeof(uint16_t)) + 5 + sizeof(float) + (4 * sizeof(uint8_t)) };
/* Nothing else ties the hand-summed size to the hand-computed write offsets;
 * a mismatch truncates or over-reads the key input silently. */
_Static_assert(PER_SPRITE_SIZE == KEY_OV_OFF + 17 + sizeof(float), "PER_SPRITE_SIZE must equal the last per-sprite write offset + 1");

static void atlas_key_write_sprite(uint8_t *out, const NtAtlasSpriteInput *s) {
    size_t off = 0;
    memcpy(out + off, &s->decoded_hash, sizeof(uint64_t));
    off += sizeof(uint64_t);
    memcpy(out + off, &s->width, sizeof(uint32_t));
    off += sizeof(uint32_t);
    memcpy(out + off, &s->height, sizeof(uint32_t));
    off += sizeof(uint32_t);
    memcpy(out + off, &s->origin_x, sizeof(float));
    off += sizeof(float);
    memcpy(out + off, &s->origin_y, sizeof(float));
    const size_t ov_off = KEY_OV_OFF;
    memcpy(out + ov_off, &s->slice9_left, sizeof(uint16_t));
    memcpy(out + ov_off + 2, &s->slice9_right, sizeof(uint16_t));
    memcpy(out + ov_off + 4, &s->slice9_top, sizeof(uint16_t));
    memcpy(out + ov_off + 6, &s->slice9_bottom, sizeof(uint16_t));
    out[ov_off + 8] = s->shape_override;
    out[ov_off + 9] = s->transforms_override;
    out[ov_off + 10] = s->max_verts_override;
    out[ov_off + 11] = s->margin_override;
    out[ov_off + 12] = s->extrude_override;
    float area_percent = s->has_max_added_area_percent_override ? s->max_added_area_percent_override : 0.0F;
    memcpy(out + ov_off + 13, &area_percent, sizeof(float));
    out[ov_off + 13 + sizeof(float)] = s->alpha_threshold_override;
    out[ov_off + 14 + sizeof(float)] = s->has_alpha_threshold_override ? 1 : 0;
    out[ov_off + 15 + sizeof(float)] = s->has_max_added_area_percent_override ? 1 : 0;
    /* Tri-state, so the value already carries its own presence. */
    out[ov_off + 16 + sizeof(float)] = s->dedup_override;
}

static uint64_t compute_atlas_cache_key(const NtAtlasSpriteInput *sprites, uint32_t sprite_count, const nt_atlas_opts_t *opts) {
    /* Atlas cache stores raw page pixels + placements; post-pack texture
     * encoding knobs belong to the texture cache. */
    /* Bump when a change alters packed output — a stale cache must miss and rebuild. */
    /* Per-sprite records stay in add-order, NOT sorted — cached placements store
     * sprite_index in add-order, so the key must be order-sensitive to avoid
     * mismatching placements after reordering. */
    size_t per_sprite_bytes = (size_t)sprite_count * PER_SPRITE_SIZE;
    uint8_t *sprite_buf = (uint8_t *)malloc(per_sprite_bytes);
    NT_BUILD_ASSERT(sprite_buf && "compute_atlas_cache_key: alloc failed");
    for (uint32_t i = 0; i < sprite_count; i++) {
        atlas_key_write_sprite(sprite_buf + ((size_t)i * PER_SPRITE_SIZE), &sprites[i]);
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
    memcpy(opts_buf + pos, &opts->max_added_area_percent, sizeof(opts->max_added_area_percent));
    pos += (uint32_t)sizeof(opts->max_added_area_percent);
    memcpy(opts_buf + pos, &opts->max_vertices, sizeof(opts->max_vertices));
    pos += (uint32_t)sizeof(opts->max_vertices);
    opts_buf[pos++] = opts->alpha_threshold;
    /* Only pack/compose-affecting opts go here. Post-pack fields (format,
     * premultiplied, debug_png, compress) are handled by the texture cache
     * and must NOT appear — otherwise changing e.g. premultiplied triggers
     * a full re-pack when only re-encode is needed. */
    uint8_t flags = (uint8_t)(opts->power_of_two ? 2 : 0);
    opts_buf[pos++] = flags;
    opts_buf[pos++] = opts->allowed_transforms;
    opts_buf[pos++] = (uint8_t)opts->shape;
    opts_buf[pos++] = opts->dedup ? 1 : 0;
    opts_buf[pos++] = (uint8_t)ATLAS_CACHE_KEY_VERSION;
    NT_BUILD_ASSERT(pos <= sizeof(opts_buf) && "compute_atlas_cache_key: opts_buf too small");

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

static void atlas_grow_sprites(NtAtlasBuild *state) {
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

/* Validate every caller contract before allocating the transaction. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity) — NT_BUILD_ASSERT expansions inflate the count
static nt_texture_pixel_format_t atlas_assert_opts(const nt_atlas_opts_t *opts) {
    // #region scalar + shape/extrude
    /* Lower bound 3: hull_simplify reduces to max_vertices AFTER the <3 hull
     * guard, so max_vertices 1|2 yields a degenerate (line/point) polygon. */
    NT_BUILD_ASSERT(opts->max_vertices >= 4 && opts->max_vertices <= 16 &&
                    "nt_atlas_begin: max_vertices must be 4..16 (a trim-clamped triangle cannot cover a full-perimeter mask; NFP buffer limit nA+nB <= 32)");
    NT_BUILD_ASSERT(opts->max_size > 0 && opts->max_size <= 16384 && "nt_atlas_begin: max_size must be 1..16384");
    NT_BUILD_ASSERT(opts->padding <= opts->max_size && "nt_atlas_begin: padding exceeds max_size");
    NT_BUILD_ASSERT(opts->margin <= opts->max_size && "nt_atlas_begin: margin exceeds max_size");
    NT_BUILD_ASSERT(opts->extrude <= opts->max_size && "nt_atlas_begin: extrude exceeds max_size");
    NT_BUILD_ASSERT(isfinite(opts->max_added_area_percent) && opts->max_added_area_percent >= 0.0F && "nt_atlas_begin: max_added_area_percent must be finite and non-negative");
    /* unsigned cast catches a negative value cast into the enum too. */
    NT_BUILD_ASSERT((unsigned)opts->shape <= (unsigned)NT_ATLAS_SHAPE_CONCAVE_CONTOUR && "nt_atlas_begin: opts.shape out of range");
    /* Simple AABB edge extrude needs a rectangular footprint; polygon packing
     * inflates hulls, so an AABB extrude band could spill and collide. */
    NT_BUILD_ASSERT((opts->shape == NT_ATLAS_SHAPE_RECT || opts->extrude == 0) &&
                    "nt_atlas_begin: opts.extrude > 0 requires shape == NT_ATLAS_SHAPE_RECT — polygon modes reserve space for the silhouette envelope, not for an AABB extrude band");
    /* Validate scale before accepting any transaction inputs. */
    NT_BUILD_ASSERT(opts->pixels_per_unit > 0.0F && isfinite(opts->pixels_per_unit) && "nt_atlas_begin: pixels_per_unit must be > 0 and finite");
    // #endregion
    nt_tex_opts_t texture_opts = {
        .format = opts->format,
        .premultiplied = opts->premultiplied,
        .filter_min = opts->filter_min,
        .filter_mag = opts->filter_mag,
        .wrap_u = opts->wrap_u,
        .wrap_v = opts->wrap_v,
        .gen_mipmaps = opts->gen_mipmaps,
    };
    return nt_builder_assert_texture_opts(&texture_opts, opts->compress);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
NtAtlasBuild *nt_atlas_begin(NtBuilderContext *ctx, const char *name, const nt_atlas_opts_t *opts) {
    NT_BUILD_ASSERT(ctx && "nt_atlas_begin: ctx is NULL");
    NT_BUILD_ASSERT(name && "nt_atlas_begin: name is NULL");
    NT_BUILD_ASSERT(!ctx->active_atlas && "nt_atlas_begin: nested atlas not allowed");
    nt_atlas_opts_t resolved = opts ? *opts : nt_atlas_opts_defaults();
    resolved.format = atlas_assert_opts(&resolved);
    /* -0.0F -> +0.0F: the cache key hashes raw float bytes. */
    if (resolved.max_added_area_percent == 0.0F) {
        resolved.max_added_area_percent = 0.0F;
    }
    if (resolved.compress) {
        resolved.gen_mipmaps = true;
    }
    NtAtlasBuild *state = (NtAtlasBuild *)calloc(1, sizeof(NtAtlasBuild));
    NT_BUILD_ASSERT(state && "nt_atlas_begin: alloc failed");

    /* Assign early so nt_builder_free_pack can clean up if a validation
     * assert below fires and the test harness longjmps out. */
    ctx->active_atlas = state;
    state->ctx = ctx;

    state->name = strdup(name);
    NT_BUILD_ASSERT(state->name && "nt_atlas_begin: strdup failed");

    state->opts = resolved;
    if (resolved.compress) {
        state->compress = *resolved.compress;
        if (state->compress.mode == NT_TEX_COMPRESS_UASTC) {
            state->compress.selector_rdo_quality = 0.0F;
        }
        state->has_compress = true;
    }
    state->opts.compress = NULL; /* zeroed -- use has_compress flag */

    /* Initialize sprite array */
    state->sprite_capacity = 64;
    state->sprites = (NtAtlasSpriteInput *)calloc(state->sprite_capacity, sizeof(NtAtlasSpriteInput));
    NT_BUILD_ASSERT(state->sprites && "nt_atlas_begin: alloc failed");
    state->sprite_count = 0;
    return state;
}

/* NOLINT: macro-expanded asserts look branch-heavy to clang-tidy. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static nt_atlas_sprite_opts_t atlas_resolve_sprite_opts(const nt_atlas_sprite_opts_t *opts) {
    nt_atlas_sprite_opts_t resolved = opts ? *opts : nt_atlas_sprite_opts_defaults();
    NT_BUILD_ASSERT(isfinite(resolved.origin_x) && isfinite(resolved.origin_y) && "atlas_add*: origin must be finite (no NaN/inf)");
    if (resolved.has_max_added_area_percent) {
        NT_BUILD_ASSERT(isfinite(resolved.max_added_area_percent) && resolved.max_added_area_percent >= 0.0F && "atlas_add*: max_added_area_percent must be finite and non-negative");
    }
    /* Unused payload zeroed; -0.0F -> +0.0F: cache/dedup identity hashes raw float bytes. */
    if (!resolved.has_max_added_area_percent || resolved.max_added_area_percent == 0.0F) {
        resolved.max_added_area_percent = 0.0F;
    }
    if (!resolved.has_alpha_threshold) {
        resolved.alpha_threshold = 0;
    }
    return resolved;
}

/* Pure sprite-opts asserts independent of content validation. */
static void atlas_assert_sprite_opts(const nt_atlas_sprite_opts_t *sopts) {
    NT_BUILD_ASSERT(sopts->shape <= NT_ATLAS_SPRITE_SHAPE_CONCAVE && "invalid shape override value");
    /* allowed_transforms: any uint8 mask is legal (identity floor applied downstream). */
    NT_BUILD_ASSERT((sopts->max_vertices == 0 || (sopts->max_vertices >= 4 && sopts->max_vertices <= 16)) && "max_vertices override must be 0 (atlas default) or 4..16");
    NT_BUILD_ASSERT(sopts->dedup <= NT_ATLAS_SPRITE_DEDUP_OFF && "dedup override must be 0 (atlas default), NT_ATLAS_SPRITE_DEDUP_ON or _OFF");
}

/* Atlas-shape-dependent sprite cross-field asserts (slice9→RECT, extrude>0→RECT
 * effective shape). Pure asserts — no mutation. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity) — NT_BUILD_ASSERT macro expansions inflate the count
static void atlas_assert_sprite_cross_field(const nt_atlas_sprite_opts_t *sopts, const nt_atlas_opts_t *atlas_opts) {
    /* Slice9 auto-forces RECT — check its precondition before the extrude check. */
    bool has_slice9 = sopts->slice9_left || sopts->slice9_right || sopts->slice9_top || sopts->slice9_bottom;
    uint8_t effective_override = sopts->shape;
    if (has_slice9) {
        NT_BUILD_ASSERT((sopts->shape == 0 || sopts->shape == NT_ATLAS_SPRITE_SHAPE_RECT) && "slice9 sprite must use RECT shape");
        NT_BUILD_ASSERT((sopts->allowed_transforms == 0 || sopts->allowed_transforms == NT_ATLAS_TRANSFORMS_IDENTITY) && "slice9 sprite must not allow non-identity transforms");
        effective_override = NT_ATLAS_SPRITE_SHAPE_RECT;
    }
    uint32_t effective_extrude = sopts->extrude ? sopts->extrude : atlas_opts->extrude;
    if (effective_extrude > 0) {
        /* Sprite and atlas shape values belong to different enum domains. */
        bool effective_is_rect = effective_override ? (effective_override == NT_ATLAS_SPRITE_SHAPE_RECT) : (atlas_opts->shape == NT_ATLAS_SHAPE_RECT);
        NT_BUILD_ASSERT(effective_is_rect && "effective extrude > 0 requires effective shape == RECT");
    }
}

/* Copy per-sprite overrides from resolved opts into NtAtlasSpriteInput.
 * Slice9 borders auto-force RECT shape + identity-only transform mask. The cross-field
 * contract is asserted earlier on the add path (before content checks), not here. */
static void atlas_apply_sprite_overrides(NtAtlasSpriteInput *sprite, const nt_atlas_sprite_opts_t *sopts, const nt_atlas_opts_t *atlas_opts) {
    sprite->slice9_left = sopts->slice9_left;
    sprite->slice9_right = sopts->slice9_right;
    sprite->slice9_top = sopts->slice9_top;
    sprite->slice9_bottom = sopts->slice9_bottom;
    sprite->max_added_area_percent_override = sopts->max_added_area_percent;
    sprite->has_max_added_area_percent_override = sopts->has_max_added_area_percent;
    sprite->effective_max_added_area_percent = sopts->has_max_added_area_percent ? sopts->max_added_area_percent : atlas_opts->max_added_area_percent;
    sprite->alpha_threshold_override = sopts->alpha_threshold;
    sprite->has_alpha_threshold_override = sopts->has_alpha_threshold;
    sprite->shape_override = sopts->shape;
    sprite->transforms_override = sopts->allowed_transforms;
    sprite->max_verts_override = sopts->max_vertices;
    sprite->margin_override = sopts->margin;
    sprite->extrude_override = sopts->extrude;
    sprite->dedup_override = sopts->dedup;
    /* Slice9 auto-force: RECT shape + identity-only transform mask. */
    bool has_slice9 = sopts->slice9_left || sopts->slice9_right || sopts->slice9_top || sopts->slice9_bottom;
    if (has_slice9) {
        sprite->shape_override = NT_ATLAS_SPRITE_SHAPE_RECT;
        sprite->transforms_override = NT_ATLAS_TRANSFORMS_IDENTITY;
    }
}

/* OOM is fatal; recognized size failures remain actionable content errors. */
static nt_build_error_kind classify_stbi_null(const char *reason) {
    if (reason && strstr(reason, "outofmem")) {
        NT_BUILD_ASSERT(0 && "atlas_add: image decode out of memory");
    }
    if (reason && strstr(reason, "too large")) {
        return NT_BUILD_ERR_KIND_IMAGE_TOO_LARGE;
    }
    if (reason && (strstr(reason, "0-pixel image") || strstr(reason, "0 width"))) {
        return NT_BUILD_ERR_KIND_ZERO_DIM;
    }
    return NT_BUILD_ERR_KIND_CORRUPT_IMAGE;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void nt_atlas_add(NtAtlasBuild *atlas, const char *path, const nt_atlas_sprite_opts_t *opts) {
    NT_BUILD_ASSERT(atlas && atlas->ctx && atlas->ctx->active_atlas == atlas && "atlas_add: invalid atlas handle");
    NtBuilderContext *ctx = atlas->ctx;
    NT_BUILD_ASSERT(ctx && path && "atlas_add: invalid args");
    /* Caller arguments are validated before content decoding. */
    nt_atlas_sprite_opts_t sopts = atlas_resolve_sprite_opts(opts);
    atlas_assert_sprite_opts(&sopts);
    NtAtlasBuild *state = atlas;

    /* Programmer errors take precedence over content errors. */
    atlas_assert_sprite_cross_field(&sopts, &state->opts);

    uint32_t add_seq = state->add_seq_counter++;
    const char *region_name = sopts.name ? sopts.name : extract_filename(path);

    /* Missing or unreadable files are environment errors, not content errors. */
    char *resolved_path = nt_builder_find_file(path, NULL, ctx);
    const char *read_path = resolved_path ? resolved_path : path;
    uint32_t file_size = 0;
    bool file_too_large = false;
    uint8_t *file_data = (uint8_t *)nt_builder_read_file_bounded(read_path, INT_MAX, &file_size, &file_too_large);
    free(resolved_path);
    if (file_too_large) {
        push_content_error(state, add_seq, region_name, NT_BUILD_ERR_KIND_IMAGE_TOO_LARGE, 0, 0);
        return;
    }
    NT_BUILD_ASSERT(file_data && "atlas_add: failed to read file");

    /* Decode once so a recognized format's failure reason is not overwritten. */
    int w = 0;
    int h = 0;
    int channels = 0;
    uint8_t *pixels = stbi_load_from_memory(file_data, (int)file_size, &w, &h, &channels, 4);
    free(file_data);
    if (!pixels) {
        nt_build_error_kind kind = classify_stbi_null(stbi_failure_reason());
        push_content_error(state, add_seq, region_name, kind, 0, 0);
        return;
    }
    if (w <= 0 || h <= 0) {
        stbi_image_free(pixels);
        push_content_error(state, add_seq, region_name, NT_BUILD_ERR_KIND_ZERO_DIM, 0, 0);
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
    sprite->add_seq = add_seq;
    atlas_apply_sprite_overrides(sprite, &sopts, &state->opts);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void nt_atlas_add_raw(NtAtlasBuild *atlas, const uint8_t *rgba_pixels, uint32_t width, uint32_t height, const nt_atlas_sprite_opts_t *opts) {
    NT_BUILD_ASSERT(atlas && atlas->ctx && atlas->ctx->active_atlas == atlas && "atlas_add_raw: invalid atlas");
    /* Caller arguments are validated before content checks. */
    nt_atlas_sprite_opts_t sopts = atlas_resolve_sprite_opts(opts);
    NT_BUILD_ASSERT(sopts.name && "atlas_add_raw: opts->name is required for raw pixels (no path to derive from)");
    atlas_assert_sprite_opts(&sopts);
    NtAtlasBuild *state = atlas;

    /* Caller-contract cross-field check runs BEFORE the dim checks so a programmer
     * bug (extrude/slice9 vs non-RECT shape) always traps and is never masked by a
     * graceful ZERO_DIM/TOO_LARGE content error. */
    atlas_assert_sprite_cross_field(&sopts, &state->opts);

    /* Stamp the add order (raw adds never fail to decode, but keep the counter
     * in lockstep with atlas_add so seqs stay globally monotonic). */
    uint32_t add_seq = state->add_seq_counter++;

    /* Zero/oversized dims are graceful content errors on the open atlas (same
     * channel as atlas_add), not caller-contract asserts — matches the spec. */
    if (width == 0 || height == 0) {
        push_content_error(state, add_seq, sopts.name, NT_BUILD_ERR_KIND_ZERO_DIM, 0, 0);
        return;
    }
    if ((uint64_t)width * height > (UINT32_MAX / 4)) {
        push_content_error(state, add_seq, sopts.name, NT_BUILD_ERR_KIND_IMAGE_TOO_LARGE, width, height);
        return;
    }
    if (width > UINT16_MAX || height > UINT16_MAX) {
        push_content_error(state, add_seq, sopts.name, NT_BUILD_ERR_KIND_ATLAS_SPRITE_TOO_LARGE, width, height);
        return;
    }
    NT_BUILD_ASSERT(rgba_pixels && "atlas_add_raw: pixels are NULL");

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
    sprite->add_seq = add_seq;
    atlas_apply_sprite_overrides(sprite, &sopts, &state->opts);
}

/* --- Glob callback for atlas --- */

typedef struct {
    NtAtlasBuild *atlas;
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
    nt_atlas_add(d->atlas, full_path, &per_file);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void nt_atlas_add_glob(NtAtlasBuild *atlas, const char *pattern, const nt_atlas_sprite_opts_t *opts) {
    NT_BUILD_ASSERT(atlas && atlas->ctx && atlas->ctx->active_atlas == atlas && pattern && "atlas_add_glob: invalid args");
    /* Validate glob-wide arguments before enumerating files. */
    if (opts) {
        nt_atlas_sprite_opts_t sopts = atlas_resolve_sprite_opts(opts);
        atlas_assert_sprite_opts(&sopts);
        atlas_assert_sprite_cross_field(&sopts, &atlas->opts);
    }
    /* If opts is non-NULL, name MUST be NULL — a single name can't apply to
     * N matched files without hash collisions. Each file derives its own name
     * from its path. Per-file override requires calling glob_iterate + atlas_add
     * manually (build_packs.c shows the pattern). */
    NT_BUILD_ASSERT((!opts || opts->name == NULL) && "atlas_add_glob: opts->name must be NULL (each file derives its name from path)");
    /* Validate finite origin values up front so failures point at the glob call site. */
    if (opts) {
        NT_BUILD_ASSERT(isfinite(opts->origin_x) && isfinite(opts->origin_y) && "atlas_add_glob: origin must be finite (no NaN/inf)");
    }

    /* Glob overflow and no-match are environment/programmer errors. */
    AtlasGlobData data = {.atlas = atlas, .sprite_opts = opts, .match_count = 0};
    NT_BUILD_ASSERT(nt_builder_glob_iterate(pattern, atlas_glob_callback, &data) && "atlas_add_glob: glob overflow");
    NT_BUILD_ASSERT(data.match_count > 0 && "atlas_add_glob: no files matched pattern");
}
// #endregion

// #region Main pipeline — nt_atlas_commit

/* --- Pipeline state: carries data between pipeline steps --- */

typedef struct {
    uint8_t effective_alpha_threshold;
    float max_added_area_percent;
    uint8_t max_vertices;
    nt_atlas_shape_t shape;
} AtlasGeometryOpts;

static AtlasGeometryOpts resolve_geometry_opts(const NtAtlasSpriteInput *sprite, const nt_atlas_opts_t *atlas) {
    AtlasGeometryOpts resolved = {
        .effective_alpha_threshold = sprite->has_alpha_threshold_override ? sprite->alpha_threshold_override : atlas->alpha_threshold,
        .max_added_area_percent = sprite->effective_max_added_area_percent,
        .max_vertices = sprite->max_verts_override ? sprite->max_verts_override : atlas->max_vertices,
        .shape = atlas->shape,
    };
    if (sprite->shape_override == NT_ATLAS_SPRITE_SHAPE_RECT) {
        resolved.shape = NT_ATLAS_SHAPE_RECT;
    } else if (sprite->shape_override == NT_ATLAS_SPRITE_SHAPE_CONVEX) {
        resolved.shape = NT_ATLAS_SHAPE_CONVEX_HULL;
    } else if (sprite->shape_override == NT_ATLAS_SPRITE_SHAPE_CONCAVE) {
        resolved.shape = NT_ATLAS_SHAPE_CONCAVE_CONTOUR;
    }
    return resolved;
}

typedef struct {
    NtBuilderContext *ctx;
    NtAtlasBuild *state;
    NtAtlasSpriteInput *sprites;
    const nt_atlas_opts_t *opts;

    /* Alpha trim */
    uint32_t *trim_x, *trim_y, *trim_w, *trim_h;
    uint8_t **alpha_planes;
    AtlasGeometryOpts *geometry_opts;

    /* Dedup */
    int32_t *dedup_map;
    uint32_t *unique_indices;
    /* Orientation sprite i's own local space is stored at inside the shared placement
     * rectangle, i.e. root_bitmap == alias_rel[i](sprite_i_bitmap) — same meaning as
     * pl->transform. Identity everywhere until the D4 stage populates it. */
    uint8_t *alias_rel;

    /* Geometry */
    uint32_t *vertex_counts;
    Point2D **hull_vertices;
    uint16_t **triangle_indices;
    uint32_t *triangle_index_counts;
    Point2D **baseline_vertices;
    uint32_t *baseline_vertex_counts;
    uint16_t **baseline_triangle_indices;
    uint32_t *baseline_triangle_index_counts;
    nt_selected_geometry_proof_t *geometry_proofs;

    /* Packing + composition */
    AtlasPlacement *placements;
    uint8_t **page_pixels;

    uint8_t *atlas_blob;
    uint32_t atlas_blob_size;
    uint64_t atlas_blob_hash;

    /* Packing statistics (per-call, no static globals) */
    PackStats stats;
    uint32_t sprite_count;
    uint32_t unique_count;
    /* Blocks folded by byte equality in pipeline_serialize — the only observable
     * proving that sharing actually happens. */
    uint32_t vertex_blocks_shared;
    uint32_t placement_count;
    uint32_t page_count;
    uint32_t thread_count;
    uint32_t page_w[ATLAS_MAX_PAGES];
    uint32_t page_h[ATLAS_MAX_PAGES];
    bool cache_hit;
} AtlasPipeline;

/* Single expression for the effective D4 mask, shared by the packer, the group
 * narrowing and the output check. A non-zero sprite mask replaces the atlas
 * default; identity stays mandatory. */
static uint8_t atlas_sprite_effective_mask(const AtlasPipeline *p, uint32_t i) {
    const uint8_t sm = p->sprites[i].transforms_override;
    return (uint8_t)((sm ? sm : p->opts->allowed_transforms) | NT_ATLAS_TRANSFORMS_IDENTITY);
}

/* Raise-only: a per-sprite margin below the atlas margin is clamped up. */
static uint32_t atlas_sprite_resolved_margin(const AtlasPipeline *p, uint32_t i) {
    const uint32_t sm = p->sprites[i].margin_override;
    return sm > p->opts->margin ? sm : p->opts->margin;
}

/* A per-sprite extrude replaces the atlas value in either direction; 0 inherits,
 * so a zero bleed cannot be expressed per sprite. */
static uint32_t atlas_sprite_resolved_extrude(const AtlasPipeline *p, uint32_t i) { return p->sprites[i].extrude_override ? p->sprites[i].extrude_override : p->opts->extrude; }

static void pipeline_resolve_geometry_opts(AtlasPipeline *p) {
    p->geometry_opts = (AtlasGeometryOpts *)malloc((size_t)p->sprite_count * sizeof(AtlasGeometryOpts));
    NT_BUILD_ASSERT(p->geometry_opts && "pipeline_resolve_geometry_opts: alloc failed");
    for (uint32_t i = 0; i < p->sprite_count; i++) {
        p->geometry_opts[i] = resolve_geometry_opts(&p->sprites[i], p->opts);
    }
}

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
        p->alpha_planes[i] = alpha_plane_extract(p->sprites[i].rgba, p->sprites[i].width, p->sprites[i].height);
        bool has_pixels =
            alpha_trim(p->alpha_planes[i], p->sprites[i].width, p->sprites[i].height, p->geometry_opts[i].effective_alpha_threshold, &p->trim_x[i], &p->trim_y[i], &p->trim_w[i], &p->trim_h[i]);
        if (!has_pixels) {
            /* Accumulate and keep validating the rest of this atlas. */
            nt_build_error_t e = {.kind = NT_BUILD_ERR_KIND_ATLAS_TRANSPARENT_AFTER_TRIM, .w = p->sprites[i].width, .h = p->sprites[i].height};
            error_copy_name(e.atlas, p->state->name);
            error_copy_name(e.sprite, p->sprites[i].name);
            atlas_push_error(p->state, p->sprites[i].add_seq, &e);
            continue;
        }
        /* Slice9 requires untrimmed source rect — runtime asserts trim_offset == 0 */
        bool has_s9 = p->sprites[i].slice9_left || p->sprites[i].slice9_right || p->sprites[i].slice9_top || p->sprites[i].slice9_bottom;
        if (has_s9) {
            uint32_t lr = (uint32_t)p->sprites[i].slice9_left + (uint32_t)p->sprites[i].slice9_right;
            uint32_t tb = (uint32_t)p->sprites[i].slice9_top + (uint32_t)p->sprites[i].slice9_bottom;
            /* Reject before serialization. */
            if (lr >= p->sprites[i].width) {
                nt_build_error_t e = {.kind = NT_BUILD_ERR_KIND_ATLAS_SLICE9_TOO_BIG, .w = p->sprites[i].width, .h = p->sprites[i].height, .detail_a = lr, .detail_b = p->sprites[i].width};
                error_copy_name(e.atlas, p->state->name);
                error_copy_name(e.sprite, p->sprites[i].name);
                atlas_push_error(p->state, p->sprites[i].add_seq, &e);
                continue;
            }
            if (tb >= p->sprites[i].height) {
                nt_build_error_t e = {.kind = NT_BUILD_ERR_KIND_ATLAS_SLICE9_TOO_BIG, .w = p->sprites[i].width, .h = p->sprites[i].height, .detail_a = tb, .detail_b = p->sprites[i].height};
                error_copy_name(e.atlas, p->state->name);
                error_copy_name(e.sprite, p->sprites[i].name);
                atlas_push_error(p->state, p->sprites[i].add_seq, &e);
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
            p->ctx->atlas_cache_hit = true;
            NT_LOG_INFO("Atlas cache hit: %s (key %016llx)", p->state->name, (unsigned long long)p->state->cache_key);
        }
    }
}

/* --- pipeline_dedup: detect duplicate sprites by hash + pixel comparison --- */

/* Resolved effective values only: two spellings of the same packing decision are the
 * same content. The slice9 borders are per-region metadata that never reaches the
 * packed pixels, and the transform mask is admission policy — comparing it would
 * make every group mask-uniform and the group intersection dead code. */
static bool pipeline_dedup_meta_match(const AtlasPipeline *p, uint32_t curr_idx, uint32_t orig_idx) {
    const AtlasGeometryOpts *gc = &p->geometry_opts[curr_idx];
    const AtlasGeometryOpts *go = &p->geometry_opts[orig_idx];
    return gc->shape == go->shape && gc->max_vertices == go->max_vertices && gc->effective_alpha_threshold == go->effective_alpha_threshold &&
           gc->max_added_area_percent == go->max_added_area_percent && atlas_sprite_resolved_margin(p, curr_idx) == atlas_sprite_resolved_margin(p, orig_idx) &&
           atlas_sprite_resolved_extrude(p, curr_idx) == atlas_sprite_resolved_extrude(p, orig_idx);
}

static bool pipeline_dedup_pixels_match(const AtlasPipeline *p, uint32_t curr_idx, uint32_t orig_idx) {
    if (p->trim_w[curr_idx] != p->trim_w[orig_idx] || p->trim_h[curr_idx] != p->trim_h[orig_idx]) {
        return false;
    }
    const NtAtlasSpriteInput *sc = &p->sprites[curr_idx];
    const NtAtlasSpriteInput *so = &p->sprites[orig_idx];
    const uint32_t tw = p->trim_w[curr_idx];
    const uint32_t th = p->trim_h[curr_idx];
    for (uint32_t row = 0; row < th; row++) {
        size_t off_a = (((size_t)(p->trim_y[curr_idx] + row) * sc->width) + p->trim_x[curr_idx]) * 4;
        size_t off_b = (((size_t)(p->trim_y[orig_idx] + row) * so->width) + p->trim_x[orig_idx]) * 4;
        const uint8_t *row_a = sc->rgba + off_a;
        const uint8_t *row_b = so->rgba + off_b;
        if (memcmp(row_a, row_b, ((size_t)tw) * 4) != 0) {
            return false;
        }
    }
    return true;
}

/* Unordered post-trim dimension pair: D4-invariant, so any two sprites related by a
 * transform share it, and it costs no raster pass. */
static uint64_t atlas_sprite_dim_key(const AtlasPipeline *p, uint32_t i) {
    const uint32_t w = p->trim_w[i];
    const uint32_t h = p->trim_h[i];
    return ((uint64_t)(w < h ? w : h) << 32U) | (w < h ? h : w);
}

/* Minimum of the eight orientation hashes of the post-trim RGBA. Mask-independent by
 * construction: a mask is admission policy, so keying on it would give two sprites with
 * identical content different buckets. Strictly supersedes decoded_hash as the grouping key,
 * which keeps its global-asset-cache and atlas-cache-key roles. */
static uint64_t atlas_sprite_canonical_hash(const AtlasPipeline *p, uint32_t i, uint8_t *scratch) {
    const uint32_t tw = p->trim_w[i];
    const uint32_t th = p->trim_h[i];
    if (tw == 0 || th == 0) {
        return 0; /* Transparent after trim — already a content error. */
    }
    const NtAtlasSpriteInput *s = &p->sprites[i];
    const size_t src_stride = (size_t)s->width * 4U;
    const uint8_t *src = s->rgba + ((size_t)p->trim_y[i] * src_stride) + ((size_t)p->trim_x[i] * 4U);
    uint64_t best = UINT64_MAX;
    for (uint8_t t = 0; t < 8U; t++) {
        uint32_t ow = 0;
        uint32_t oh = 0;
        d4_dims_after(t, tw, th, &ow, &oh);
        for (uint32_t y = 0; y < th; y++) {
            const uint8_t *row = src + ((size_t)y * src_stride);
            if (t == 0) {
                memcpy(scratch + ((size_t)y * tw * 4U), row, (size_t)tw * 4U);
                continue;
            }
            for (uint32_t x = 0; x < tw; x++) {
                int32_t tx = 0;
                int32_t ty = 0;
                /* Texel variant: the corner variant maps 0..w and is off by one on every mirror. */
                transform_point_texel((int32_t)x, (int32_t)y, t, (int32_t)tw, (int32_t)th, &tx, &ty);
                memcpy(scratch + ((((size_t)(uint32_t)ty * ow) + (uint32_t)tx) * 4U), row + ((size_t)x * 4U), 4U);
            }
        }
        const uint64_t h = nt_hash64(scratch, tw * th * 4U).value;
        best = h < best ? h : best;
    }
    return best;
}

/* End of the dim_key bucket starting at lo. */
static uint32_t dedup_bucket_end(const DedupSortEntry *e, uint32_t count, uint32_t lo) {
    uint32_t hi = lo + 1;
    while (hi < count && e[hi].dim_key == e[lo].dim_key) {
        hi++;
    }
    return hi;
}

/* End of the equal-content run starting at lo. */
static uint32_t dedup_group_end(const DedupSortEntry *e, uint32_t count, uint32_t lo) {
    uint32_t hi = lo + 1;
    while (hi < count && e[hi].dim_key == e[lo].dim_key && e[hi].hash == e[lo].hash) {
        hi++;
    }
    return hi;
}

/* Largest post-trim area among buckets that can actually fold — every member of a bucket
 * shares its dimension pair, so one entry sizes the whole bucket. */
static size_t dedup_scratch_bytes(const AtlasPipeline *p, const DedupSortEntry *e) {
    size_t bytes = 0;
    for (uint32_t lo = 0; lo < p->sprite_count;) {
        const uint32_t hi = dedup_bucket_end(e, p->sprite_count, lo);
        if (hi - lo >= 2) {
            const uint32_t idx = e[lo].index;
            const size_t need = (size_t)p->trim_w[idx] * p->trim_h[idx] * 4U;
            bytes = need > bytes ? need : bytes;
        }
        lo = hi;
    }
    return bytes;
}

static void dedup_hash_bucket(const AtlasPipeline *p, DedupSortEntry *e, uint32_t lo, uint32_t hi, uint8_t *scratch) {
    for (uint32_t k = lo; k < hi; k++) {
        e[k].hash = atlas_sprite_canonical_hash(p, e[k].index, scratch);
    }
}

/* Entries must already be dim_key-sorted. Only buckets that can fold pay the eight
 * orientation hashes; a singleton keeps hash 0 and cannot meet anything. */
static void pipeline_dedup_hash_buckets(const AtlasPipeline *p, DedupSortEntry *e) {
    const size_t scratch_bytes = dedup_scratch_bytes(p, e);
    if (scratch_bytes == 0) {
        return;
    }
    /* One buffer for the whole pass — a per-sprite allocation in an O(8N) loop is wrong
     * even in an offline tool. */
    uint8_t *scratch = (uint8_t *)malloc(scratch_bytes);
    NT_BUILD_ASSERT(scratch && "pipeline_dedup: orientation scratch alloc failed");
    for (uint32_t lo = 0; lo < p->sprite_count;) {
        const uint32_t hi = dedup_bucket_end(e, p->sprite_count, lo);
        if (hi - lo >= 2) {
            dedup_hash_bucket(p, e, lo, hi, scratch);
        }
        lo = hi;
    }
    free(scratch);
}

/* One search over an equal-content run, walked in add order. The run's first eligible
 * member is the root, so the root is the lowest add index — after the D4 stage the root
 * decides every alias's relative transform and emitted block, so this is a correctness
 * requirement, not a nicety. */
static void pipeline_dedup_fold_run(AtlasPipeline *p, const DedupSortEntry *e, uint32_t lo, uint32_t hi) {
    for (uint32_t m = lo + 1; m < hi; m++) {
        const uint32_t a = e[m].index;
        for (uint32_t r = lo; r < m; r++) {
            const uint32_t root = e[r].index;
            /* Only roots are targets; a non-root's own root sits earlier in the run and was
             * already tried, so no chain-follow is needed. */
            if (p->dedup_map[root] >= 0) {
                continue;
            }
            if (pipeline_dedup_meta_match(p, a, root) && pipeline_dedup_pixels_match(p, a, root)) {
                p->dedup_map[a] = (int32_t)root;
                break;
            }
        }
    }
}

static DedupSortEntry *pipeline_dedup_sorted_entries(const AtlasPipeline *p) {
    DedupSortEntry *e = (DedupSortEntry *)calloc(p->sprite_count, sizeof(DedupSortEntry));
    NT_BUILD_ASSERT(e && "pipeline_dedup: alloc failed");
    for (uint32_t i = 0; i < p->sprite_count; i++) {
        /* The add-order root rule rests on the sprite array being in add order. A rejected
         * add consumes an add_seq without appending, so the two only rise together. */
        NT_BUILD_ASSERT((i == 0 || p->sprites[i].add_seq > p->sprites[i - 1].add_seq) && "pipeline_dedup: sprite array is not in add order");
        e[i].index = i;
        e[i].dim_key = atlas_sprite_dim_key(p, i);
    }
    qsort(e, p->sprite_count, sizeof(DedupSortEntry), dedup_sort_cmp);
    pipeline_dedup_hash_buckets(p, e);
    qsort(e, p->sprite_count, sizeof(DedupSortEntry), dedup_sort_cmp);
    return e;
}

static void pipeline_dedup_collect_unique(AtlasPipeline *p) {
    p->unique_count = 0;
    p->unique_indices = (uint32_t *)malloc(p->sprite_count * sizeof(uint32_t));
    NT_BUILD_ASSERT(p->unique_indices && "pipeline_dedup: alloc failed");
    for (uint32_t i = 0; i < p->sprite_count; i++) {
        if (p->dedup_map[i] < 0) {
            p->unique_indices[p->unique_count++] = i;
            continue;
        }
        NT_BUILD_ASSERT(p->dedup_map[(uint32_t)p->dedup_map[i]] < 0 && "pipeline_dedup: every alias must point at a root");
    }
}

static void pipeline_dedup(AtlasPipeline *p) {
    DedupSortEntry *dedup_entries = pipeline_dedup_sorted_entries(p);

    /* Map duplicate -> original. -1 = unique. */
    p->dedup_map = (int32_t *)malloc(p->sprite_count * sizeof(int32_t));
    p->alias_rel = (uint8_t *)calloc(p->sprite_count, sizeof(uint8_t));
    NT_BUILD_ASSERT(p->dedup_map && p->alias_rel && "pipeline_dedup: alloc failed");
    for (uint32_t i = 0; i < p->sprite_count; i++) {
        p->dedup_map[i] = -1;
    }

    for (uint32_t lo = 0; lo < p->sprite_count;) {
        const uint32_t hi = dedup_group_end(dedup_entries, p->sprite_count, lo);
        pipeline_dedup_fold_run(p, dedup_entries, lo, hi);
        lo = hi;
    }
    free(dedup_entries);

    pipeline_dedup_collect_unique(p);
}

/* Candidate ownership transfers only through the frontier. */

typedef struct {
    Point2D *poly; /* heap-allocated, caller frees if not adopted */
    uint16_t *triangle_indices;
    uint32_t triangle_index_count;
    uint32_t count; /* vertex count */
    uint32_t generator_ordinal;
    uint64_t exact_abs_twice_area;
    bool valid; /* false = strategy declined / produced degenerate output */
} GeometryCandidate;

static void geometry_candidate_discard(GeometryCandidate *candidate);

static void geometry_candidate_discard(GeometryCandidate *candidate) {
    free(candidate->poly);
    free(candidate->triangle_indices);
    *candidate = (GeometryCandidate){0};
}

typedef struct {
    GeometryCandidate slots[NT_POLYGON_MAX_VERTICES + 1];
    const uint8_t *binary;
    uint32_t width;
    uint32_t height;
    uint32_t max_vertices;
    uint64_t opaque_area2;
} GeometryFrontier;

typedef struct {
    GeometryCandidate baseline;
    GeometryCandidate selected;
    nt_selected_geometry_proof_t proof;
    bool valid;
} GeometrySelection;

static bool geometry_point_less(Point2D left, Point2D right) { return left.x != right.x ? left.x < right.x : left.y < right.y; }

static void geometry_candidate_canonicalize(GeometryCandidate *candidate) {
    uint32_t first = 0;
    for (uint32_t i = 1; i < candidate->count; i++) {
        if (geometry_point_less(candidate->poly[i], candidate->poly[first])) {
            first = i;
        }
    }
    if (first == 0) {
        return;
    }
    Point2D canonical[NT_POLYGON_MAX_VERTICES];
    for (uint32_t i = 0; i < candidate->count; i++) {
        canonical[i] = candidate->poly[(first + i) % candidate->count];
    }
    memcpy(candidate->poly, canonical, (size_t)candidate->count * sizeof(Point2D));
}

static bool geometry_frontier_finalize(GeometryFrontier *frontier, GeometryCandidate *candidate) {
    if (!candidate->valid || !candidate->poly || candidate->count < 3 || candidate->count > frontier->max_vertices || candidate->count > NT_POLYGON_MAX_VERTICES) {
        return false;
    }
    geometry_candidate_canonicalize(candidate);
    nt_polygon_feasibility_t feasibility = nt_polygon_feasibility(candidate->poly, candidate->count, frontier->binary, frontier->width, frontier->height, frontier->max_vertices);
    if (!feasibility.valid) {
        return false;
    }
    uint16_t *indices = (uint16_t *)malloc((size_t)feasibility.triangle_index_count * sizeof(uint16_t));
    NT_BUILD_ASSERT(indices && "geometry_frontier_finalize: alloc failed");
    memcpy(indices, feasibility.triangle_indices, (size_t)feasibility.triangle_index_count * sizeof(uint16_t));
    free(candidate->triangle_indices);
    candidate->triangle_indices = indices;
    candidate->triangle_index_count = feasibility.triangle_index_count;
    candidate->exact_abs_twice_area = feasibility.polygon_area2;
    candidate->valid = true;
    return true;
}

static int geometry_frontier_tuple_compare(const GeometryCandidate *left, const GeometryCandidate *right) {
    for (uint32_t i = 0; i < left->count; i++) {
        if (left->poly[i].x != right->poly[i].x) {
            return left->poly[i].x < right->poly[i].x ? -1 : 1;
        }
        if (left->poly[i].y != right->poly[i].y) {
            return left->poly[i].y < right->poly[i].y ? -1 : 1;
        }
    }
    for (uint32_t i = 0; i < left->triangle_index_count; i++) {
        if (left->triangle_indices[i] != right->triangle_indices[i]) {
            return left->triangle_indices[i] < right->triangle_indices[i] ? -1 : 1;
        }
    }
    return 0;
}

static bool geometry_frontier_candidate_better(const GeometryCandidate *candidate, const GeometryCandidate *current) {
    if (!current->valid) {
        return true;
    }
    if (candidate->exact_abs_twice_area != current->exact_abs_twice_area) {
        return candidate->exact_abs_twice_area < current->exact_abs_twice_area;
    }
    if (candidate->generator_ordinal != current->generator_ordinal) {
        return candidate->generator_ordinal < current->generator_ordinal;
    }
    return geometry_frontier_tuple_compare(candidate, current) < 0;
}

static int32_t geometry_clamp_i32(int32_t value, int32_t min_value, int32_t max_value) {
    if (value < min_value) {
        return min_value;
    }
    if (value > max_value) {
        return max_value;
    }
    return value;
}

static void geometry_frontier_clamp_to_trim(const GeometryFrontier *frontier, GeometryCandidate *candidate) {
    if (!candidate->poly || candidate->count < 3U || candidate->count > NT_POLYGON_MAX_VERTICES) {
        return;
    }
    Point2D bounded[NT_POLYGON_MAX_VERTICES];
    Point2D compact[NT_POLYGON_MAX_VERTICES];
    for (uint32_t i = 0; i < candidate->count; i++) {
        bounded[i].x = geometry_clamp_i32(candidate->poly[i].x, 0, (int32_t)frontier->width);
        bounded[i].y = geometry_clamp_i32(candidate->poly[i].y, 0, (int32_t)frontier->height);
    }
    const uint32_t compact_count = remove_collinear(bounded, candidate->count, compact);
    if (compact_count >= 3U) {
        memcpy(candidate->poly, compact, (size_t)compact_count * sizeof(Point2D));
        candidate->count = compact_count;
    }
}

static void geometry_frontier_adopt(GeometryFrontier *frontier, GeometryCandidate *source) {
    geometry_frontier_clamp_to_trim(frontier, source);
    geometry_candidate_canonicalize(source);
    GeometryCandidate *slot = source->count <= NT_POLYGON_MAX_VERTICES ? &frontier->slots[source->count] : NULL;
    /* Area-first reject: feasibility never changes area, so a candidate that cannot
     * beat its slot skips the O(cells*verts) coverage proof entirely. */
    if (slot && slot->valid && source->poly && polygon_validate(source->poly, source->count) == NT_POLYGON_VALID) {
        source->exact_abs_twice_area = polygon_abs_twice_area(source->poly, source->count);
        if (!geometry_frontier_candidate_better(source, slot)) {
            geometry_candidate_discard(source);
            return;
        }
        source->exact_abs_twice_area = 0U;
    }
    if (!geometry_frontier_finalize(frontier, source)) {
        geometry_candidate_discard(source);
        return;
    }
    slot = &frontier->slots[source->count];
    if (!geometry_frontier_candidate_better(source, slot)) {
        geometry_candidate_discard(source);
        return;
    }
    if (slot->valid) {
        geometry_candidate_discard(slot);
    }
    *slot = *source;
    // NOLINTNEXTLINE(clang-analyzer-unix.Malloc) -- ownership moved into the frontier slot.
    *source = (GeometryCandidate){0};
}

static void geometry_frontier_enumerate_covering_removals(GeometryFrontier *frontier, const GeometryCandidate *source, uint32_t generator_ordinal) {
    if (!source->valid || source->count <= 3) {
        return;
    }
    for (uint32_t removed = 0; removed < source->count; removed++) {
        GeometryCandidate candidate = {0};
        candidate.count = source->count - 1U;
        candidate.poly = (Point2D *)malloc((size_t)candidate.count * sizeof(Point2D));
        NT_BUILD_ASSERT(candidate.poly && "geometry_frontier_enumerate_covering_removals: alloc failed");
        uint32_t out = 0;
        for (uint32_t i = 0; i < source->count; i++) {
            if (i != removed) {
                candidate.poly[out++] = source->poly[i];
            }
        }
        candidate.generator_ordinal = generator_ordinal;
        candidate.valid = true;
        geometry_frontier_adopt(frontier, &candidate);
    }
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void geometry_frontier_enumerate_covering_pair_removals(GeometryFrontier *frontier, const GeometryCandidate *source, uint32_t generator_ordinal) {
    if (!source->valid || source->count <= 4) {
        return;
    }
    for (uint32_t first = 0; first + 1U < source->count; first++) {
        for (uint32_t second = first + 1U; second < source->count; second++) {
            GeometryCandidate candidate = {0};
            candidate.count = source->count - 2U;
            candidate.poly = (Point2D *)malloc((size_t)candidate.count * sizeof(Point2D));
            NT_BUILD_ASSERT(candidate.poly && "geometry_frontier_enumerate_covering_pair_removals: alloc failed");
            uint32_t out = 0;
            for (uint32_t i = 0; i < source->count; i++) {
                if (i != first && i != second) {
                    candidate.poly[out++] = source->poly[i];
                }
            }
            candidate.generator_ordinal = generator_ordinal;
            candidate.valid = true;
            geometry_frontier_adopt(frontier, &candidate);
        }
    }
}

static uint64_t geometry_frontier_base_area2(const GeometryFrontier *frontier) {
    uint64_t base = UINT64_MAX;
    for (uint32_t count = 3; count <= frontier->max_vertices; count++) {
        if (frontier->slots[count].valid && frontier->slots[count].exact_abs_twice_area < base) {
            base = frontier->slots[count].exact_abs_twice_area;
        }
    }
    return base;
}

static uint32_t geometry_frontier_select(const GeometryFrontier *frontier, double max_added_area_percent) {
    uint64_t base = geometry_frontier_base_area2(frontier);
    if (base == UINT64_MAX || frontier->opaque_area2 == 0 || !isfinite(max_added_area_percent) || max_added_area_percent < 0.0) {
        return UINT32_MAX;
    }
    for (uint32_t count = 3; count <= frontier->max_vertices; count++) {
        const GeometryCandidate *candidate = &frontier->slots[count];
        if (!candidate->valid) {
            continue;
        }
        NT_BUILD_ASSERT(candidate->exact_abs_twice_area >= base && "frontier slot below computed base area");
        double added_area = (double)(candidate->exact_abs_twice_area - base) * 100.0;
        double allowed_area = (double)frontier->opaque_area2 * max_added_area_percent;
        if (added_area <= allowed_area) {
            return count;
        }
    }
    return UINT32_MAX;
}

static void geometry_frontier_destroy(GeometryFrontier *frontier) {
    for (uint32_t count = 3; count <= NT_POLYGON_MAX_VERTICES; count++) {
        if (frontier->slots[count].valid) {
            geometry_candidate_discard(&frontier->slots[count]);
        }
    }
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static GeometrySelection geometry_frontier_take_selection(GeometryFrontier *frontier, float max_added_area_percent) {
    GeometrySelection result = {0};
    uint64_t base_area2 = geometry_frontier_base_area2(frontier);
    uint32_t selected_count = geometry_frontier_select(frontier, (double)max_added_area_percent);
    if (base_area2 == UINT64_MAX || selected_count == UINT32_MAX) {
        return result;
    }
    uint32_t base_count = UINT32_MAX;
    for (uint32_t count = 3; count <= frontier->max_vertices; count++) {
        if (frontier->slots[count].valid && frontier->slots[count].exact_abs_twice_area == base_area2) {
            base_count = count;
            break;
        }
    }
    NT_BUILD_ASSERT(base_count != UINT32_MAX && "geometry frontier base candidate missing");

    const GeometryCandidate *base = &frontier->slots[base_count];
    const GeometryCandidate *selected = &frontier->slots[selected_count];
    result.proof = nt_selected_geometry_validate(frontier->binary, frontier->width, frontier->height, frontier->opaque_area2, base->poly, base->count, base->exact_abs_twice_area,
                                                 base->triangle_indices, base->triangle_index_count, selected->poly, selected->count, selected->exact_abs_twice_area, selected->triangle_indices,
                                                 selected->triangle_index_count, max_added_area_percent, frontier->max_vertices);
    NT_BUILD_ASSERT(result.proof.valid && "selected geometry proof mismatch");

    result.baseline.count = base->count;
    result.baseline.exact_abs_twice_area = base->exact_abs_twice_area;
    result.baseline.poly = (Point2D *)malloc((size_t)base->count * sizeof(Point2D));
    result.baseline.triangle_indices = (uint16_t *)malloc((size_t)base->triangle_index_count * sizeof(uint16_t));
    NT_BUILD_ASSERT(result.baseline.poly && result.baseline.triangle_indices && "geometry frontier baseline copy alloc failed");
    memcpy(result.baseline.poly, base->poly, (size_t)base->count * sizeof(Point2D));
    memcpy(result.baseline.triangle_indices, base->triangle_indices, (size_t)base->triangle_index_count * sizeof(uint16_t));
    result.baseline.triangle_index_count = base->triangle_index_count;
    result.baseline.valid = true;

    result.selected = *selected;
    frontier->slots[selected_count] = (GeometryCandidate){0};
    result.valid = true;
    return result;
}

#ifdef NT_TEST_ACCESS
void nt_atlas_test_frontier_selection_proof_mismatch(void) {
    const uint8_t binary[1] = {1};
    Point2D quad[4] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
    uint16_t corrupt_indices[6] = {0};
    GeometryFrontier frontier = {
        .binary = binary,
        .width = 1,
        .height = 1,
        .max_vertices = 4,
        .opaque_area2 = 2,
    };
    frontier.slots[4] = (GeometryCandidate){
        .poly = quad,
        .triangle_indices = corrupt_indices,
        .triangle_index_count = 6,
        .count = 4,
        .exact_abs_twice_area = 2,
        .valid = true,
    };
    (void)geometry_frontier_take_selection(&frontier, 0.0F);
}
#endif

static void geometry_selection_discard(GeometrySelection *selection) {
    geometry_candidate_discard(&selection->baseline);
    geometry_candidate_discard(&selection->selected);
    *selection = (GeometrySelection){0};
}

static uint64_t geometry_retained_area2(const uint8_t *binary, uint32_t width, uint32_t height) {
    uint64_t retained = 0;
    for (uint32_t y = 0; y < height; y++) {
        for (uint32_t x = 0; x < width; x++) {
            retained += binary[((size_t)y * width) + x] != 0 ? 1U : 0U;
        }
    }
    return retained * 2U;
}

#ifdef NT_TEST_ACCESS
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
bool nt_atlas_test_frontier_evaluate(const Point2D *const *polygons, const uint32_t *counts, uint32_t candidate_count, const uint8_t *binary, uint32_t width, uint32_t height, uint32_t max_vertices,
                                     float max_added_area_percent, NtAtlasFrontierTestResult *out_result) {
    if (!polygons || !counts || !binary || !out_result || max_vertices < 3 || max_vertices > NT_POLYGON_MAX_VERTICES) {
        return false;
    }
    *out_result = (NtAtlasFrontierTestResult){0};
    GeometryFrontier frontier = {.binary = binary, .width = width, .height = height, .max_vertices = max_vertices, .opaque_area2 = geometry_retained_area2(binary, width, height)};
    for (uint32_t i = 0; i < candidate_count; i++) {
        GeometryCandidate candidate = {.count = counts[i], .valid = true};
        if (counts[i] > 0) {
            candidate.poly = (Point2D *)malloc((size_t)counts[i] * sizeof(Point2D));
            NT_BUILD_ASSERT(candidate.poly && "nt_atlas_test_frontier_evaluate: alloc failed");
            memcpy(candidate.poly, polygons[i], (size_t)counts[i] * sizeof(Point2D));
        }
        geometry_frontier_adopt(&frontier, &candidate);
    }

    uint64_t base = geometry_frontier_base_area2(&frontier);
    uint32_t selected_count = geometry_frontier_select(&frontier, (double)max_added_area_percent);
    out_result->opaque_area2 = frontier.opaque_area2;
    out_result->base_area2 = base;
    for (uint32_t count = 3; count <= max_vertices; count++) {
        if (!frontier.slots[count].valid) {
            continue;
        }
        out_result->slot_mask |= 1U << count;
        out_result->slot_area2[count] = frontier.slots[count].exact_abs_twice_area;
    }
    if (selected_count != UINT32_MAX) {
        const GeometryCandidate *selected = &frontier.slots[selected_count];
        NT_BUILD_ASSERT(base >= frontier.opaque_area2 && selected->exact_abs_twice_area >= base);
        if (!selected->poly || !selected->triangle_indices) {
            NT_BUILD_ASSERT(false && "selected frontier storage missing");
            geometry_frontier_destroy(&frontier);
            return false;
        }
        out_result->selected_count = selected_count;
        out_result->selected_index_count = selected->triangle_index_count;
        out_result->selected_area2 = selected->exact_abs_twice_area;
        memcpy(out_result->selected_poly, selected->poly, (size_t)selected->count * sizeof(Point2D));
        memcpy(out_result->selected_indices, selected->triangle_indices, (size_t)selected->triangle_index_count * sizeof(uint16_t));
        out_result->base_overdraw_percent = ((double)(base - frontier.opaque_area2) * 100.0) / (double)frontier.opaque_area2;
        out_result->added_area_percent = ((double)(selected->exact_abs_twice_area - base) * 100.0) / (double)frontier.opaque_area2;
        out_result->total_overdraw_percent = ((double)(selected->exact_abs_twice_area - frontier.opaque_area2) * 100.0) / (double)frontier.opaque_area2;
    }
    geometry_frontier_destroy(&frontier);
    return selected_count != UINT32_MAX;
}

uint32_t nt_atlas_test_frontier_select_areas(const uint64_t *slot_area2, uint32_t slot_mask, uint64_t opaque_area2, uint32_t max_vertices, float max_added_area_percent) {
    if (!slot_area2 || max_vertices < 3 || max_vertices > NT_POLYGON_MAX_VERTICES) {
        return UINT32_MAX;
    }
    GeometryFrontier frontier = {.max_vertices = max_vertices, .opaque_area2 = opaque_area2};
    for (uint32_t count = 3; count <= max_vertices; count++) {
        if ((slot_mask & (1U << count)) != 0) {
            frontier.slots[count].valid = true;
            frontier.slots[count].exact_abs_twice_area = slot_area2[count];
        }
    }
    return geometry_frontier_select(&frontier, (double)max_added_area_percent);
}

#endif

static bool geometry_reduce_to_budget(Point2D **poly, uint32_t *count, uint32_t max_vertices) {
    if (*count <= max_vertices) {
        return true;
    }
    Point2D *reduced = (Point2D *)malloc((size_t)*count * sizeof(Point2D));
    NT_BUILD_ASSERT(reduced && "geometry_reduce_to_budget: alloc failed");
    double ignored_error = 0.0;
    uint32_t reduced_count = hull_simplify_perp(*poly, *count, max_vertices, reduced, &ignored_error);
    free(*poly);
    *poly = reduced;
    *count = reduced_count;
    return reduced_count >= 3 && reduced_count <= max_vertices;
}

static bool geometry_inflate_candidate(Point2D **poly, uint32_t *count, double amount, uint32_t max_vertices) {
    int32_t *source_xy = (int32_t *)malloc((size_t)*count * 2 * sizeof(int32_t));
    NT_BUILD_ASSERT(source_xy && "geometry_inflate_candidate: alloc failed");
    for (uint32_t i = 0; i < *count; i++) {
        source_xy[(size_t)i * 2] = (*poly)[i].x;
        source_xy[((size_t)i * 2) + 1] = (*poly)[i].y;
    }

    int32_t *inflated_xy = NULL;
    uint32_t inflated_count = nt_clipper2_inflate(source_xy, *count, amount, &inflated_xy);
    free(source_xy);
    if (inflated_count < 3 || !inflated_xy) {
        free(inflated_xy);
        return false;
    }

    Point2D *inflated = (Point2D *)malloc((size_t)inflated_count * sizeof(Point2D));
    NT_BUILD_ASSERT(inflated && "geometry_inflate_candidate: alloc failed");
    for (uint32_t i = 0; i < inflated_count; i++) {
        inflated[i] = (Point2D){inflated_xy[(size_t)i * 2], inflated_xy[((size_t)i * 2) + 1]};
    }
    free(inflated_xy);
    free(*poly);
    *poly = inflated;
    *count = inflated_count;
    return geometry_reduce_to_budget(poly, count, max_vertices);
}

static bool geometry_finalize_candidate(GeometryCandidate *candidate, const uint8_t *binary_source, uint32_t tw, uint32_t th, uint32_t max_vertices) {
    if (!candidate->valid || candidate->count < 3 || candidate->count > max_vertices || polygon_validate(candidate->poly, candidate->count) != NT_POLYGON_VALID) {
        geometry_candidate_discard(candidate);
        return false;
    }

    double outside = polygon_max_outside_pixel_distance(candidate->poly, candidate->count, binary_source, tw, th);
    if (outside > 0.0 && !geometry_inflate_candidate(&candidate->poly, &candidate->count, outside, max_vertices)) {
        geometry_candidate_discard(candidate);
        return false;
    }
    if (polygon_validate(candidate->poly, candidate->count) != NT_POLYGON_VALID) {
        geometry_candidate_discard(candidate);
        return false;
    }
    outside = polygon_max_outside_pixel_distance(candidate->poly, candidate->count, binary_source, tw, th);
    if (outside > 0.0 && !geometry_inflate_candidate(&candidate->poly, &candidate->count, outside + 1.0, max_vertices)) {
        geometry_candidate_discard(candidate);
        return false;
    }
    if (polygon_validate(candidate->poly, candidate->count) != NT_POLYGON_VALID || polygon_coverage_metrics(candidate->poly, candidate->count, binary_source, tw, th).lost_retained_pixels != 0) {
        geometry_candidate_discard(candidate);
        return false;
    }

    candidate->exact_abs_twice_area = polygon_abs_twice_area(candidate->poly, candidate->count);
    return true;
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
    result.valid = true;
    return result;
}

/* Strategy 2: greedy perpendicular-distance simplification, exactly target verts. */
static GeometryCandidate strategy_perp(const Point2D *clean, uint32_t clean_count, uint32_t target) {
    GeometryCandidate result = {0};
    if (clean_count < 3 || target < 3) {
        return result;
    }
    Point2D *poly = (Point2D *)malloc(clean_count * sizeof(Point2D));
    NT_BUILD_ASSERT(poly && "strategy_perp: alloc failed");

    double dummy_dev = 0.0;
    uint32_t count = hull_simplify_perp(clean, clean_count, target, poly, &dummy_dev);

    result.poly = poly;
    result.count = count;
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
    result.valid = true;
    return result;
}

/* Strategy 4: convex hull of the binary mask via Andrew's monotone chain,
 * simplified down to target vertices. Wins on convex-ish shapes where the
 * hull is already within max_vertices. */
static GeometryCandidate strategy_convex(const Point2D *source, uint32_t source_count, uint32_t target) {
    GeometryCandidate result = {0};
    if (!source || source_count < 3U) {
        return result; /* invalid */
    }
    Point2D *poly = (Point2D *)malloc((size_t)source_count * sizeof(Point2D));
    NT_BUILD_ASSERT(poly && "strategy_convex: alloc failed");
    uint32_t count = source_count;
    if (source_count > target) {
        count = hull_simplify(source, source_count, target, poly);
    } else {
        memcpy(poly, source, (size_t)source_count * sizeof(Point2D));
    }

    result.poly = poly;
    result.count = count;
    result.valid = true;
    return result;
}

static uint32_t geometry_corner_cut_count(uint32_t mask) {
    uint32_t count = 0U;
    for (uint32_t bit = 0U; bit < 4U; bit++) {
        count += (mask & (1U << bit)) != 0U ? 1U : 0U;
    }
    return count;
}

static uint32_t geometry_write_corner_cut_polygon(Point2D *poly, uint32_t mask, int32_t width, int32_t height, int32_t depth) {
    uint32_t out = 0U;
    poly[out++] = (mask & 1U) != 0U ? (Point2D){depth, 0} : (Point2D){0, 0};
    if ((mask & 2U) != 0U) {
        poly[out++] = (Point2D){width - depth, 0};
        poly[out++] = (Point2D){width, depth};
    } else {
        poly[out++] = (Point2D){width, 0};
    }
    if ((mask & 4U) != 0U) {
        poly[out++] = (Point2D){width, height - depth};
        poly[out++] = (Point2D){width - depth, height};
    } else {
        poly[out++] = (Point2D){width, height};
    }
    if ((mask & 8U) != 0U) {
        poly[out++] = (Point2D){depth, height};
        poly[out++] = (Point2D){0, height - depth};
    } else {
        poly[out++] = (Point2D){0, height};
    }
    if ((mask & 1U) != 0U) {
        poly[out++] = (Point2D){0, depth};
    }
    return out;
}

static void geometry_frontier_add_corner_cut_depth(GeometryFrontier *frontier, uint32_t target_count, uint32_t mask, uint32_t depth) {
    GeometryCandidate candidate = {.count = target_count, .generator_ordinal = 6U, .valid = true};
    candidate.poly = (Point2D *)malloc((size_t)target_count * sizeof(Point2D));
    NT_BUILD_ASSERT(candidate.poly && "geometry_frontier_add_corner_cut_depth: alloc failed");
    const uint32_t out = geometry_write_corner_cut_polygon(candidate.poly, mask, (int32_t)frontier->width, (int32_t)frontier->height, (int32_t)depth);
    NT_BUILD_ASSERT(out == target_count);
    geometry_frontier_adopt(frontier, &candidate);
}

static bool geometry_frontier_corner_cut_depth_is_feasible(GeometryFrontier *frontier, uint32_t target_count, uint32_t mask, uint32_t depth) {
    Point2D poly[NT_POLYGON_MAX_VERTICES];
    const uint32_t count = geometry_write_corner_cut_polygon(poly, mask, (int32_t)frontier->width, (int32_t)frontier->height, (int32_t)depth);
    NT_BUILD_ASSERT(count == target_count);
    return nt_polygon_feasibility(poly, count, frontier->binary, frontier->width, frontier->height, frontier->max_vertices).valid;
}

static void geometry_frontier_add_corner_cut_mask(GeometryFrontier *frontier, uint32_t target_count, uint32_t mask, uint32_t max_depth) {
    if (max_depth == 0U || !geometry_frontier_corner_cut_depth_is_feasible(frontier, target_count, mask, 1U)) {
        return;
    }
    uint32_t low = 1U;
    uint32_t high = max_depth;
    while (low < high) {
        const uint32_t middle = low + ((high - low + 1U) / 2U);
        if (geometry_frontier_corner_cut_depth_is_feasible(frontier, target_count, mask, middle)) {
            low = middle;
        } else {
            high = middle - 1U;
        }
    }
    geometry_frontier_add_corner_cut_depth(frontier, target_count, mask, low);
}

static void geometry_frontier_add_corner_cuts(GeometryFrontier *frontier, uint32_t target_count) {
    if (target_count < 5U || target_count > 8U) {
        return;
    }
    const uint32_t cuts = target_count - 4U;
    const uint32_t max_depth = (frontier->width < frontier->height ? frontier->width : frontier->height) / 2U;
    for (uint32_t mask = 1U; mask < 16U; mask++) {
        if (geometry_corner_cut_count(mask) != cuts) {
            continue;
        }
        geometry_frontier_add_corner_cut_mask(frontier, target_count, mask, max_depth);
    }
}

#ifdef NT_TEST_ACCESS
static GeometrySelection geometry_build_concave_frontier_impl(const Point2D *clean, uint32_t clean_count, const uint8_t *binary_source, uint32_t tw, uint32_t th, uint32_t max_vertices,
                                                              float max_added_area_percent, NtAtlasFrontierTestResult *out_frontier) {
#else
static GeometrySelection geometry_build_concave_frontier_impl(const Point2D *clean, uint32_t clean_count, const uint8_t *binary_source, uint32_t tw, uint32_t th, uint32_t max_vertices,
                                                              float max_added_area_percent) {
#endif
    GeometryFrontier frontier = {
        .binary = binary_source,
        .width = tw,
        .height = th,
        .max_vertices = max_vertices,
        .opaque_area2 = geometry_retained_area2(binary_source, tw, th),
    };
    uint32_t convex_source_count = 0U;
    Point2D *convex_source = binary_build_convex_polygon(binary_source, tw, th, UINT32_MAX, &convex_source_count);
    if (max_vertices >= 4U) {
        GeometryCandidate rect = strategy_rect(tw, th);
        rect.generator_ordinal = 2;
        geometry_frontier_adopt(&frontier, &rect);
    }
    for (uint32_t target = 3; target <= max_vertices; target++) {
        GeometryCandidate rdp = strategy_rdp(clean, clean_count, target);
        rdp.generator_ordinal = 0;
        (void)geometry_finalize_candidate(&rdp, binary_source, tw, th, max_vertices);
        geometry_frontier_adopt(&frontier, &rdp);

        GeometryCandidate perp = strategy_perp(clean, clean_count, target);
        perp.generator_ordinal = 1;
        (void)geometry_finalize_candidate(&perp, binary_source, tw, th, max_vertices);
        geometry_frontier_adopt(&frontier, &perp);

        GeometryCandidate convex = strategy_convex(convex_source, convex_source_count, target);
        convex.generator_ordinal = 3;
        (void)geometry_finalize_candidate(&convex, binary_source, tw, th, max_vertices);
        geometry_frontier_adopt(&frontier, &convex);

        geometry_frontier_add_corner_cuts(&frontier, target);
    }
    free(convex_source);
    for (uint32_t count = max_vertices; count > 3; count--) {
        geometry_frontier_enumerate_covering_removals(&frontier, &frontier.slots[count], 4);
        geometry_frontier_enumerate_covering_pair_removals(&frontier, &frontier.slots[count], 5);
    }
#ifdef NT_TEST_ACCESS
    if (out_frontier) {
        *out_frontier = (NtAtlasFrontierTestResult){
            .opaque_area2 = frontier.opaque_area2,
            .base_area2 = geometry_frontier_base_area2(&frontier),
        };
        for (uint32_t count = 3; count <= max_vertices; count++) {
            if (frontier.slots[count].valid) {
                out_frontier->slot_mask |= 1U << count;
                out_frontier->slot_area2[count] = frontier.slots[count].exact_abs_twice_area;
            }
        }
    }
#endif
    GeometrySelection selected = geometry_frontier_take_selection(&frontier, max_added_area_percent);
    geometry_frontier_destroy(&frontier);
    return selected;
}

static GeometrySelection geometry_build_concave_frontier(const Point2D *clean, uint32_t clean_count, const uint8_t *binary_source, uint32_t tw, uint32_t th, uint32_t max_vertices,
                                                         float max_added_area_percent) {
#ifdef NT_TEST_ACCESS
    return geometry_build_concave_frontier_impl(clean, clean_count, binary_source, tw, th, max_vertices, max_added_area_percent, NULL);
#else
    return geometry_build_concave_frontier_impl(clean, clean_count, binary_source, tw, th, max_vertices, max_added_area_percent);
#endif
}

static GeometrySelection geometry_build_concave_fallback_frontier(const uint8_t *binary_source, uint32_t tw, uint32_t th, uint32_t max_vertices, float max_added_area_percent) {
    GeometryFrontier frontier = {
        .binary = binary_source,
        .width = tw,
        .height = th,
        .max_vertices = max_vertices,
        .opaque_area2 = geometry_retained_area2(binary_source, tw, th),
    };
    uint32_t convex_source_count = 0U;
    Point2D *convex_source = binary_build_convex_polygon(binary_source, tw, th, UINT32_MAX, &convex_source_count);
    if (max_vertices >= 4U) {
        GeometryCandidate rect = strategy_rect(tw, th);
        rect.generator_ordinal = 2;
        geometry_frontier_adopt(&frontier, &rect);
    }
    for (uint32_t target = 3; target <= max_vertices; target++) {
        GeometryCandidate convex = strategy_convex(convex_source, convex_source_count, target);
        convex.generator_ordinal = 3;
        (void)geometry_finalize_candidate(&convex, binary_source, tw, th, max_vertices);
        geometry_frontier_adopt(&frontier, &convex);
    }
    free(convex_source);
    GeometrySelection selected = geometry_frontier_take_selection(&frontier, max_added_area_percent);
    geometry_frontier_destroy(&frontier);
    return selected;
}

#ifdef NT_TEST_ACCESS
uint32_t nt_atlas_test_concave_frontier_slot_mask(const uint8_t *binary, uint32_t width, uint32_t height, uint32_t max_vertices) {
    uint32_t retained_count = 0;
    for (size_t i = 0; i < (size_t)width * height; i++) {
        retained_count += binary[i] != 0 ? 1U : 0U;
    }
    uint32_t contour_capacity = (2U * retained_count) + 2U;
    Point2D *contour = (Point2D *)malloc((size_t)contour_capacity * sizeof(Point2D));
    Point2D *clean = (Point2D *)malloc((size_t)contour_capacity * sizeof(Point2D));
    NT_BUILD_ASSERT(contour && clean && "nt_atlas_test_concave_frontier_slot_mask: alloc failed");
    bool overflow = false;
    uint32_t contour_count = trace_contour(binary, width, height, contour, contour_capacity, &overflow);
    uint32_t clean_count = overflow ? 0 : remove_collinear(contour, contour_count, clean);
    NtAtlasFrontierTestResult result = {0};
    GeometrySelection selection = geometry_build_concave_frontier_impl(clean, clean_count, binary, width, height, max_vertices, 0.0F, &result);
    geometry_selection_discard(&selection);
    free(clean);
    free(contour);
    return result.slot_mask;
}

#endif

static GeometryCandidate geometry_convex_reduction_candidate(const Point2D *reference, uint32_t reference_count, uint32_t target) {
    GeometryCandidate result = {0};
    Point2D *poly = (Point2D *)malloc((size_t)reference_count * sizeof(Point2D));
    NT_BUILD_ASSERT(poly && "geometry_convex_reduction_candidate: alloc failed");
    double ignored_error = 0.0;
    uint32_t count = hull_simplify_perp(reference, reference_count, target, poly, &ignored_error);
    if (count < 3 || polygon_area_pixels(poly, count) == 0) {
        free(poly);
        return result;
    }
    result.poly = poly;
    result.count = count;
    result.valid = true;
    return result;
}

static GeometryCandidate geometry_convex_covering_candidate(const Point2D *reference, uint32_t reference_count, uint32_t max_vertices) {
    GeometryCandidate result = {0};
    Point2D *poly = (Point2D *)malloc((size_t)reference_count * sizeof(Point2D));
    NT_BUILD_ASSERT(poly && "geometry_convex_covering_candidate: alloc failed");
    uint32_t count = reference_count;
    if (reference_count <= max_vertices) {
        memcpy(poly, reference, (size_t)reference_count * sizeof(Point2D));
    } else {
        count = hull_simplify_covering(reference, reference_count, max_vertices, poly);
    }
    if (count < 3 || count > max_vertices || polygon_area_pixels(poly, count) == 0) {
        free(poly);
        return result;
    }
    result.poly = poly;
    result.count = count;
    result.valid = true;
    return result;
}

static GeometrySelection geometry_build_convex_frontier(const uint8_t *binary_source, uint32_t tw, uint32_t th, uint32_t max_vertices, float max_added_area_percent) {
    uint32_t reference_count = 0;
    Point2D *reference = binary_build_convex_polygon(binary_source, tw, th, UINT32_MAX, &reference_count);
    if (!reference || reference_count < 3) {
        free(reference);
        return (GeometrySelection){0};
    }
    GeometryFrontier frontier = {
        .binary = binary_source,
        .width = tw,
        .height = th,
        .max_vertices = max_vertices,
        .opaque_area2 = geometry_retained_area2(binary_source, tw, th),
    };
    if (max_vertices >= 4U) {
        GeometryCandidate rect = strategy_rect(tw, th);
        rect.generator_ordinal = 2;
        geometry_frontier_adopt(&frontier, &rect);
    }
    for (uint32_t target = 3; target <= max_vertices; target++) {
        GeometryCandidate reduced = geometry_convex_reduction_candidate(reference, reference_count, target);
        reduced.generator_ordinal = 0;
        (void)geometry_finalize_candidate(&reduced, binary_source, tw, th, max_vertices);
        geometry_frontier_adopt(&frontier, &reduced);

        GeometryCandidate covering = geometry_convex_covering_candidate(reference, reference_count, target);
        covering.generator_ordinal = 1;
        geometry_frontier_adopt(&frontier, &covering);
    }
    free(reference);
    GeometrySelection selected = geometry_frontier_take_selection(&frontier, max_added_area_percent);
    geometry_frontier_destroy(&frontier);
    return selected;
}

static GeometrySelection geometry_build_rect_frontier(const uint8_t *binary_source, uint32_t tw, uint32_t th, uint32_t max_vertices, float max_added_area_percent) {
    GeometryFrontier frontier = {
        .binary = binary_source,
        .width = tw,
        .height = th,
        .max_vertices = max_vertices,
        .opaque_area2 = geometry_retained_area2(binary_source, tw, th),
    };
    GeometryCandidate rect = strategy_rect(tw, th);
    geometry_frontier_adopt(&frontier, &rect);
    GeometrySelection selected = geometry_frontier_take_selection(&frontier, max_added_area_percent);
    geometry_frontier_destroy(&frontier);
    return selected;
}

static bool pipeline_install_geometry_selection(AtlasPipeline *p, uint32_t idx, GeometrySelection *selection) {
    if (!selection->valid) {
        geometry_selection_discard(selection);
        /* The rect candidate makes selection total at max_vertices >= 4; only an
         * int16-local overflow can leave it invalid, and pipeline_validate reports
         * that as the actionable UNFITTABLE error. */
        NT_BUILD_ASSERT((p->trim_w[idx] > INT16_MAX || p->trim_h[idx] > INT16_MAX) && "geometry selection must be total for max_vertices >= 4");
        return false;
    }
    NT_BUILD_ASSERT(selection->proof.valid && "pipeline geometry selection proof missing");
    p->hull_vertices[idx] = selection->selected.poly;
    p->vertex_counts[idx] = selection->selected.count;
    p->triangle_indices[idx] = selection->selected.triangle_indices;
    p->triangle_index_counts[idx] = selection->selected.triangle_index_count;
    p->baseline_vertices[idx] = selection->baseline.poly;
    p->baseline_vertex_counts[idx] = selection->baseline.count;
    p->baseline_triangle_indices[idx] = selection->baseline.triangle_indices;
    p->baseline_triangle_index_counts[idx] = selection->baseline.triangle_index_count;
    p->geometry_proofs[idx] = selection->proof;
    selection->selected.poly = NULL;
    selection->selected.triangle_indices = NULL;
    selection->baseline.poly = NULL;
    selection->baseline.triangle_indices = NULL;
    geometry_selection_discard(selection);
    return true;
}

static uint8_t *pipeline_geometry_binary_mask(const AtlasPipeline *p, uint32_t idx);
static void swap_triangle_winding(uint16_t *indices, uint32_t index_count);

/* Odd popcount of the three flag bits = orientation-reversing transform. */
static uint32_t d4_parity(uint8_t v) { return (uint32_t)(((v & 1U) + ((v >> 1) & 1U) + ((v >> 2) & 1U)) & 1U); }

/* polygon_transform reverses the ring on odd parity, so every index moves to n-1-idx;
 * that reversal also flips each triangle, hence the swap back to PNG-CCW. */
static void alias_remap_reversed_indices(const uint16_t *src, uint32_t index_count, uint32_t n, uint16_t *out) {
    for (uint32_t k = 0; k < index_count; k++) {
        uint16_t idx = src[k];
        NT_BUILD_ASSERT(idx < n && "alias geometry: triangle index outside the vertex ring");
        if (idx >= n) {
            idx = 0; /* Hard bound — NT_ASSERT_MODE=OFF compiles the assert away. */
        }
        out[k] = (uint16_t)(n - 1U - idx);
    }
    swap_triangle_winding(out, index_count);
}

/* Re-prove an alias's already-derived geometry against the alias's OWN mask. This pass is
 * what catches a wrong relative transform or a wrong derivation, so it must never be
 * short-circuited by the area-preservation argument that makes the root's scalars valid
 * claims here. */
static void pipeline_reprove_alias_geometry(AtlasPipeline *p, uint32_t i, uint32_t orig) {
    const nt_selected_geometry_proof_t *root_proof = &p->geometry_proofs[orig];
    uint8_t *binary = pipeline_geometry_binary_mask(p, i);
    nt_selected_geometry_proof_t proof =
        nt_selected_geometry_validate(binary, p->trim_w[i], p->trim_h[i], root_proof->opaque_area2, p->baseline_vertices[i], p->baseline_vertex_counts[i], root_proof->base_area2,
                                      p->baseline_triangle_indices[i], p->baseline_triangle_index_counts[i], p->hull_vertices[i], p->vertex_counts[i], root_proof->selected_area2,
                                      p->triangle_indices[i], p->triangle_index_counts[i], p->geometry_opts[i].max_added_area_percent, p->geometry_opts[i].max_vertices);
    free(binary);
    p->geometry_proofs[i] = proof;
    NT_BUILD_ASSERT(proof.valid && nt_selected_geometry_proof_equal(&proof, root_proof) && "alias geometry proof mismatch");
}

/* Derive an alias's geometry as the exact integer D4 pre-image of its root's.
 *
 * alias_rel maps the alias's local space onto the root's, so the alias's own hull is the
 * root's pulled back through d4_inverse — with the ROOT's dims, which is the space that
 * pull-back starts in. Re-tracing instead would let the frontier pick a hull that is not
 * the exact D4 image of the root's and therefore no longer fits the shared rectangle. */
static void pipeline_derive_alias_geometry(AtlasPipeline *p, uint32_t i, uint32_t orig) {
    /* pipeline_dedup chain-follows to a root before comparing, so no composition along a
     * chain is ever needed. */
    NT_BUILD_ASSERT(p->dedup_map[orig] < 0 && "alias geometry: dedup_map must point at a root, never at another alias");
    if (p->hull_vertices[orig] == NULL) {
        return; /* Root produced no geometry (content error / degenerate trim). */
    }

    const uint8_t inv = d4_inverse(p->alias_rel[i]);
    const uint32_t rw = p->trim_w[orig];
    const uint32_t rh = p->trim_h[orig];
    uint32_t aw = 0;
    uint32_t ah = 0;
    d4_dims_after(inv, rw, rh, &aw, &ah);
    NT_BUILD_ASSERT(aw == p->trim_w[i] && ah == p->trim_h[i] && "alias geometry: relative transform disagrees with the alias trim dims");

    /* Vertex and index counts are D4-invariant. */
    const uint32_t n = p->vertex_counts[orig];
    const uint32_t idx_count = p->triangle_index_counts[orig];
    const uint32_t bn = p->baseline_vertex_counts[orig];
    const uint32_t bidx_count = p->baseline_triangle_index_counts[orig];
    p->vertex_counts[i] = n;
    p->triangle_index_counts[i] = idx_count;
    p->baseline_vertex_counts[i] = bn;
    p->baseline_triangle_index_counts[i] = bidx_count;

    Point2D *hull = (Point2D *)malloc((size_t)n * sizeof(Point2D));
    uint16_t *tris = (uint16_t *)malloc((size_t)idx_count * sizeof(uint16_t));
    Point2D *base = (Point2D *)malloc((size_t)bn * sizeof(Point2D));
    uint16_t *base_tris = (uint16_t *)malloc((size_t)bidx_count * sizeof(uint16_t));
    NT_BUILD_ASSERT(hull && tris && base && base_tris && "pipeline_derive_alias_geometry: alloc failed");

    polygon_transform(p->hull_vertices[orig], n, inv, (int32_t)rw, (int32_t)rh, hull);
    polygon_transform(p->baseline_vertices[orig], bn, inv, (int32_t)rw, (int32_t)rh, base);
    if (d4_parity(inv)) {
        alias_remap_reversed_indices(p->triangle_indices[orig], idx_count, n, tris);
        alias_remap_reversed_indices(p->baseline_triangle_indices[orig], bidx_count, bn, base_tris);
    } else {
        memcpy(tris, p->triangle_indices[orig], (size_t)idx_count * sizeof(uint16_t));
        memcpy(base_tris, p->baseline_triangle_indices[orig], (size_t)bidx_count * sizeof(uint16_t));
    }

    p->hull_vertices[i] = hull;
    p->triangle_indices[i] = tris;
    p->baseline_vertices[i] = base;
    p->baseline_triangle_indices[i] = base_tris;
    pipeline_reprove_alias_geometry(p, i, orig);
}

static bool geometry_merge_disjoint_components(uint8_t *binary, uint32_t width, uint32_t height, uint32_t *out_pass_count) {
    enum { MAX_PASS_COUNT = 8U };
    const uint32_t padded_width = width + (MAX_PASS_COUNT * 2U);
    const uint32_t padded_height = height + (MAX_PASS_COUNT * 2U);
    const size_t padded_size = (size_t)padded_width * padded_height;
    uint8_t *padded = (uint8_t *)calloc(padded_size, 1);
    uint8_t *scratch = (uint8_t *)calloc(padded_size, 1);
    uint8_t *visited = (uint8_t *)calloc(padded_size, 1);
    int32_t *stack = (int32_t *)malloc(padded_size * 2U * sizeof(int32_t));
    NT_BUILD_ASSERT(padded && scratch && visited && stack && "component closing scratch alloc failed");
    for (uint32_t y = 0; y < height; y++) {
        memcpy(padded + ((size_t)(y + MAX_PASS_COUNT) * padded_width) + MAX_PASS_COUNT, binary + ((size_t)y * width), width);
    }

    uint32_t component_count = binary_count_components(padded, padded_width, padded_height, visited, stack);
    uint32_t pass_count = 0U;
    uint8_t *current = padded;
    uint8_t *next = scratch;
    while (component_count > 1U && pass_count < MAX_PASS_COUNT) {
        binary_dilate_4conn(current, next, padded_width, padded_height);
        uint8_t *swap = current;
        current = next;
        next = swap;
        pass_count++;
        component_count = binary_count_components(current, padded_width, padded_height, visited, stack);
    }
    if (component_count <= 1U) {
        for (uint32_t pass = 0; pass < pass_count; pass++) {
            binary_erode_4conn(current, next, padded_width, padded_height);
            uint8_t *swap = current;
            current = next;
            next = swap;
        }
        component_count = binary_count_components(current, padded_width, padded_height, visited, stack);
        if (component_count <= 1U) {
            for (uint32_t y = 0; y < height; y++) {
                memcpy(binary + ((size_t)y * width), current + ((size_t)(y + MAX_PASS_COUNT) * padded_width) + MAX_PASS_COUNT, width);
            }
        }
    }
    free(stack);
    free(visited);
    free(scratch);
    free(padded);
    if (out_pass_count) {
        *out_pass_count = pass_count;
    }
    return component_count <= 1U;
}

#ifdef NT_TEST_ACCESS
bool nt_atlas_test_merge_disjoint_components(uint8_t *binary, uint32_t width, uint32_t height, uint32_t *out_pass_count) {
    return geometry_merge_disjoint_components(binary, width, height, out_pass_count);
}
#endif

/* --- pipeline_geometry: contour trace + simplification + inflation per unique sprite --- */

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void pipeline_geometry(AtlasPipeline *p) {
    p->vertex_counts = (uint32_t *)calloc(p->sprite_count, sizeof(uint32_t));
    p->hull_vertices = (Point2D **)calloc(p->sprite_count, sizeof(Point2D *));
    p->triangle_indices = (uint16_t **)calloc(p->sprite_count, sizeof(uint16_t *));
    p->triangle_index_counts = (uint32_t *)calloc(p->sprite_count, sizeof(uint32_t));
    p->baseline_vertices = (Point2D **)calloc(p->sprite_count, sizeof(Point2D *));
    p->baseline_vertex_counts = (uint32_t *)calloc(p->sprite_count, sizeof(uint32_t));
    p->baseline_triangle_indices = (uint16_t **)calloc(p->sprite_count, sizeof(uint16_t *));
    p->baseline_triangle_index_counts = (uint32_t *)calloc(p->sprite_count, sizeof(uint32_t));
    p->geometry_proofs = (nt_selected_geometry_proof_t *)calloc(p->sprite_count, sizeof(nt_selected_geometry_proof_t));
    NT_BUILD_ASSERT(p->vertex_counts && p->hull_vertices && p->triangle_indices && p->triangle_index_counts && p->baseline_vertices && p->baseline_vertex_counts && p->baseline_triangle_indices &&
                    p->baseline_triangle_index_counts && p->geometry_proofs && "pipeline_geometry: alloc failed");

    for (uint32_t ui = 0; ui < p->unique_count; ui++) {
        uint32_t idx = p->unique_indices[ui];
        uint32_t tw = p->trim_w[idx];
        uint32_t th = p->trim_h[idx];

        /* Skip a sprite that failed alpha_trim (degenerate 0-size trim). It was
         * already reported (transparent-after-trim / slice9) so building a hull
         * here would double-report it as a degenerate hull. Never hit on the
         * successful path — trim is otherwise always non-zero. */
        if (tw == 0 || th == 0) {
            continue;
        }

        const AtlasGeometryOpts *geometry_opts = &p->geometry_opts[idx];
        nt_atlas_shape_t effective_shape = geometry_opts->shape;
        uint8_t effective_max_verts = geometry_opts->max_vertices;

        if (effective_shape == NT_ATLAS_SHAPE_RECT) {
            uint8_t *rect_binary = pipeline_geometry_binary_mask(p, idx);
            GeometrySelection selection = geometry_build_rect_frontier(rect_binary, tw, th, effective_max_verts, geometry_opts->max_added_area_percent);
            (void)pipeline_install_geometry_selection(p, idx, &selection);
            free(rect_binary);
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
                    if (a >= geometry_opts->effective_alpha_threshold) {
                        binary[((size_t)y * tw) + x] = 1;
                    }
                }
            }

            if (effective_shape == NT_ATLAS_SHAPE_CONVEX_HULL) {
                GeometrySelection selection = geometry_build_convex_frontier(binary, tw, th, effective_max_verts, geometry_opts->max_added_area_percent);
                (void)pipeline_install_geometry_selection(p, idx, &selection);
                free(binary);
                continue;
            }

            // #region Morphological closing — merge disjoint components into one
            /* Padding avoids trim-edge clipping; components still split after closing use the convex fallback. */
            uint8_t *binary_source = (uint8_t *)malloc((size_t)tw * th);
            NT_BUILD_ASSERT(binary_source && "pipeline_geometry: alloc failed");
            memcpy(binary_source, binary, (size_t)tw * th);
            const char *convex_reason = NULL;
            if (!geometry_merge_disjoint_components(binary, tw, th, NULL)) {
                convex_reason = "disjoint components";
            }
            // #endregion

            if (!convex_reason) {
                /* Contour vertex bound for a single connected component:
                 * P <= 2*N + 2 for an N-pixel mask. */
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
                    /* Contour-capacity overflow is a content error, not a fallback candidate. */
                    free(contour);
                    free(binary_source);
                    free(binary);
                    push_content_error(p->state, p->sprites[idx].add_seq, p->sprites[idx].name, NT_BUILD_ERR_KIND_ATLAS_CONTOUR_VERTEX_OVERFLOW, tw, th);
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

                    GeometrySelection selection = geometry_build_concave_frontier(clean, clean_count, binary_source, tw, th, effective_max_verts, geometry_opts->max_added_area_percent);
                    free(clean);
                    (void)pipeline_install_geometry_selection(p, idx, &selection);
                    free(binary_source);
                    free(binary);
                    continue;
                }
            }
            if (convex_reason) {
                NT_LOG_WARN("pipeline_geometry: sprite '%s' using convex fallback (%s)", p->sprites[idx].name, convex_reason);
                GeometrySelection selection = geometry_build_concave_fallback_frontier(binary_source, tw, th, effective_max_verts, geometry_opts->max_added_area_percent);
                (void)pipeline_install_geometry_selection(p, idx, &selection);
            }
            free(binary_source);
            free(binary);
        }
    }

    // #region alias geometry — every alias owns a derived copy, never a borrowed pointer
    for (uint32_t i = 0; i < p->sprite_count; i++) {
        if (p->dedup_map[i] >= 0) {
            pipeline_derive_alias_geometry(p, i, (uint32_t)p->dedup_map[i]);
        }
    }
    // #endregion
}

static uint8_t *pipeline_geometry_binary_mask(const AtlasPipeline *p, uint32_t idx) {
    uint32_t tw = p->trim_w[idx];
    uint32_t th = p->trim_h[idx];
    uint32_t source_w = p->sprites[idx].width;
    uint8_t *binary = (uint8_t *)calloc((size_t)tw * th, 1);
    NT_BUILD_ASSERT(binary && "pipeline geometry mask alloc failed");
    for (uint32_t y = 0; y < th; y++) {
        for (uint32_t x = 0; x < tw; x++) {
            uint8_t alpha = p->alpha_planes[idx][((size_t)(p->trim_y[idx] + y) * source_w) + p->trim_x[idx] + x];
            binary[((size_t)y * tw) + x] = alpha >= p->geometry_opts[idx].effective_alpha_threshold ? 1 : 0;
        }
    }
    return binary;
}

/* Collect independent content errors before packing or publication. */

/* Match the packing footprint, including larger per-sprite overrides. */
static void atlas_fit_hull(const AtlasPipeline *p, uint32_t oi, Point2D quad[4], const Point2D **out_hull, uint32_t *out_count) {
    uint32_t extra_margin = atlas_sprite_resolved_margin(p, oi) - p->opts->margin;
    uint32_t sprite_extrude = atlas_sprite_resolved_extrude(p, oi);
    uint32_t extra_extrude = (sprite_extrude > p->opts->extrude) ? (sprite_extrude - p->opts->extrude) : 0;
    if (extra_margin > 0 || extra_extrude > 0 || p->hull_vertices[oi] == NULL) {
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
    /* Match vector_pack's empty-page fit test before entering the packer. */
    bool *unfittable = (bool *)calloc(p->sprite_count, sizeof(bool));
    NT_BUILD_ASSERT(unfittable && "pipeline_validate: alloc failed");
    for (uint32_t i = 0; i < p->unique_count; i++) {
        uint32_t oi = p->unique_indices[i];
        /* A sprite that failed alpha_trim has no hull (already reported); skip it
         * so the fit test never dereferences a NULL/degenerate hull. */
        if (p->trim_w[oi] == 0 || p->trim_h[oi] == 0) {
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
    /* Report canonical sprites and dedup aliases in add order. */
    for (uint32_t k = 0; k < p->sprite_count; k++) {
        uint32_t c = p->dedup_map[k] < 0 ? k : (uint32_t)p->dedup_map[k];
        if (!unfittable[c]) {
            continue;
        }
        /* Report the spacing actually reserved by the packer. */
        uint32_t effective_margin = atlas_sprite_resolved_margin(p, c);
        uint32_t sprite_extrude = atlas_sprite_resolved_extrude(p, c);
        uint32_t effective_extrude = sprite_extrude > p->opts->extrude ? sprite_extrude : p->opts->extrude;
        nt_build_error_t e = {.kind = NT_BUILD_ERR_KIND_ATLAS_UNFITTABLE,
                              .w = p->trim_w[c],
                              .h = p->trim_h[c],
                              .padding = p->opts->padding,
                              .margin = effective_margin,
                              .max_size = p->opts->max_size,
                              .detail_a = effective_extrude};
        error_copy_name(e.atlas, p->state->name);
        error_copy_name(e.sprite, p->sprites[k].name);
        atlas_push_error(p->state, p->sprites[k].add_seq, &e);
    }
    free(unfittable);

    /* Region cap and duplicate names are independent content errors. */
    if (p->sprite_count > UINT16_MAX) {
        push_content_error(p->state, p->state->add_seq_counter, NULL, NT_BUILD_ERR_KIND_ATLAS_TOO_MANY_REGIONS, 0, 0);
    }
    if (p->sprite_count > 0) {
        /* Hash-sort names, then confirm collisions with strcmp. */
        NameHashEntry *ents = (NameHashEntry *)malloc(p->sprite_count * sizeof(NameHashEntry));
        bool *is_dup = (bool *)calloc(p->sprite_count, sizeof(bool));
        NT_BUILD_ASSERT(ents && is_dup && "pipeline_validate: alloc failed");
        for (uint32_t i = 0; i < p->sprite_count; i++) {
            ents[i].hash = nt_hash64_str(p->sprites[i].name).value;
            ents[i].index = i;
        }
        qsort(ents, p->sprite_count, sizeof(NameHashEntry), name_hash_cmp);
        /* The earliest exact name in each equal-hash run is canonical. */
        for (uint32_t a = 1; a < p->sprite_count; a++) {
            for (uint32_t b = a; b > 0 && ents[b - 1].hash == ents[a].hash; b--) {
                if (strcmp(p->sprites[ents[b - 1].index].name, p->sprites[ents[a].index].name) == 0) {
                    is_dup[ents[a].index] = true;
                    break;
                }
            }
        }
        /* Report in add order so truncation stays deterministic. */
        for (uint32_t j = 0; j < p->sprite_count; j++) {
            if (is_dup[j]) {
                push_content_error(p->state, p->sprites[j].add_seq, p->sprites[j].name, NT_BUILD_ERR_KIND_ATLAS_DUPLICATE_REGION_NAME, 0, 0);
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
        if (p->sprites[i].width > UINT16_MAX || p->sprites[i].height > UINT16_MAX) {
            push_content_error(p->state, p->sprites[i].add_seq, p->sprites[i].name, NT_BUILD_ERR_KIND_ATLAS_SPRITE_TOO_LARGE, p->sprites[i].width, p->sprites[i].height);
            continue;
        }
        int32_t trim_offset_y_up = (int32_t)p->sprites[i].height - (int32_t)p->trim_y[i] - (int32_t)p->trim_h[i];
        if (p->trim_x[i] > INT16_MAX || trim_offset_y_up < INT16_MIN || trim_offset_y_up > INT16_MAX) {
            push_content_error(p->state, p->sprites[i].add_seq, p->sprites[i].name, NT_BUILD_ERR_KIND_ATLAS_TRIM_OFFSET_OVERFLOW, p->trim_x[i], (uint32_t)trim_offset_y_up);
        }
    }
}

/* --- pipeline_tile_pack: sort sprites by area, place on atlas pages via tile-grid collision --- */

/* Free the per-unique scratch pipeline_tile_pack allocates (incl. the override
 * scratch hulls). Shared by the normal tail and the pages-exhausted early-out
 * so every exit frees the same set. */
static void tile_pack_free_scratch(AtlasPipeline *p, uint32_t *u_trim_w, uint32_t *u_trim_h, Point2D **u_hulls, uint32_t *u_hull_counts, uint8_t *u_eff_transforms) {
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
    free(u_eff_transforms);
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
        uint32_t extra_margin = atlas_sprite_resolved_margin(p, oi) - p->opts->margin;
        uint32_t sprite_extrude = atlas_sprite_resolved_extrude(p, oi);
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

    uint8_t *u_eff_transforms = (uint8_t *)calloc(p->unique_count, sizeof(uint8_t));
    NT_BUILD_ASSERT(u_eff_transforms && "pipeline_tile_pack: alloc failed");
    for (uint32_t i = 0; i < p->unique_count; i++) {
        u_eff_transforms[i] = atlas_sprite_effective_mask(p, p->unique_indices[i]);
    }

    // #region group placement mask
    /* The packer orients a whole alias group at once, so the placement must be legal
     * for every member — one rule that covers mixed masks and slice9 with no special
     * case, since a nine-patch resolves to identity-only. */
    uint32_t *unique_slot = (uint32_t *)malloc((size_t)p->sprite_count * sizeof(uint32_t));
    NT_BUILD_ASSERT(unique_slot && "pipeline_tile_pack: alloc failed");
    for (uint32_t i = 0; i < p->sprite_count; i++) {
        unique_slot[i] = UINT32_MAX;
    }
    for (uint32_t u = 0; u < p->unique_count; u++) {
        unique_slot[p->unique_indices[u]] = u;
    }
    for (uint32_t i = 0; i < p->sprite_count; i++) {
        if (p->dedup_map[i] < 0) {
            continue;
        }
        const uint32_t root = (uint32_t)p->dedup_map[i];
        NT_BUILD_ASSERT(root < p->sprite_count && "pipeline_tile_pack: alias root out of range");
        /* A consumer may define NT_BUILD_ASSERT away, so bound the read itself. */
        if (root >= p->sprite_count) {
            continue;
        }
        const uint32_t u = unique_slot[root];
        NT_BUILD_ASSERT(u < p->unique_count && "pipeline_tile_pack: alias root is not a unique sprite");
        if (u >= p->unique_count) {
            continue;
        }
        u_eff_transforms[u] &= atlas_sprite_effective_mask(p, i);
        /* A mask is generally not closed under composition, so a group carrying a
         * non-identity relative is placed at identity and each alias region's
         * transform is literally its relative. */
        if (p->alias_rel[i] != 0) {
            u_eff_transforms[u] = NT_ATLAS_TRANSFORMS_IDENTITY;
        }
        u_eff_transforms[u] |= NT_ATLAS_TRANSFORMS_IDENTITY;
    }
    free(unique_slot);
    // #endregion

    /* Empty-page fit is validated up front in pipeline_validate (every sprite +
     * dedup alias), so by here every sprite provably fits — vector_pack's "empty
     * page should accept placement" invariant cannot trip. */
    NT_LOG_INFO("  vector_pack: %u sprites (NFP mode)", p->unique_count);
    bool pages_exhausted = false;
    p->placement_count = vector_pack(u_trim_w, u_trim_h, u_hulls, u_hull_counts, p->unique_count, p->opts, u_eff_transforms, p->placements, &p->page_count, p->page_w, p->page_h, &p->stats,
                                     p->thread_count, &pages_exhausted);
    if (pages_exhausted) {
        /* ATLAS_MAX_PAGES exhausted — vector_pack already joined its worker pool
         * and freed its buffers; report gracefully and bail. */
        nt_build_error_t e = {.kind = NT_BUILD_ERR_KIND_ATLAS_PAGES_EXHAUSTED, .max_size = p->opts->max_size, .detail_a = ATLAS_MAX_PAGES};
        error_copy_name(e.atlas, p->state->name);
        atlas_push_error(p->state, p->state->add_seq_counter, &e);
        p->placement_count = 0;
        /* vector_pack freed its pages; compose is skipped so page_pixels stays NULL
         * and pipeline_cleanup's guard handles the (nonzero) page_count safely. */
        tile_pack_free_scratch(p, u_trim_w, u_trim_h, u_hulls, u_hull_counts, u_eff_transforms);
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

    tile_pack_free_scratch(p, u_trim_w, u_trim_h, u_hulls, u_hull_counts, u_eff_transforms);
}

/* --- pipeline_compose: blit trimmed pixels onto pages + extrude edges --- */

/* Per-sprite margin above the atlas baseline. tile_pack grows the footprint by
 * 2*extra on each axis; compose/serialize must shift content by `extra` so the
 * surplus splits evenly left/top + right/bottom instead of piling on one edge. */
static uint32_t sprite_extra_margin(const AtlasPipeline *p, uint32_t si) { return atlas_sprite_resolved_margin(p, si) - p->opts->margin; }

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
        uint32_t sprite_extrude = atlas_sprite_resolved_extrude(p, idx);
        uint32_t extra_margin = sprite_extra_margin(p, idx);
        uint32_t inner_x = pl->x + sprite_extrude + extra_margin;
        uint32_t inner_y = pl->y + sprite_extrude + extra_margin;

        blit_sprite(p->page_pixels[pl->page], p->page_w[pl->page], p->sprites[idx].rgba, p->sprites[idx].width, pl->trim_x, pl->trim_y, pl->trimmed_w, pl->trimmed_h, inner_x, inner_y, pl->transform,
                    p->geometry_opts[idx].effective_alpha_threshold);

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
            uint32_t sprite_extrude = atlas_sprite_resolved_extrude(p, si);
            uint32_t extra_margin = sprite_extra_margin(p, si);
            uint32_t ix = p->placements[pi].x + sprite_extrude + extra_margin;
            uint32_t iy = p->placements[pi].y + sprite_extrude + extra_margin;

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

/* QUAD_* patterns match PNG-space CCW triangulation before winding swap. */
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
 * Builder triangulation output is PNG-space CCW. The blob writer Y-flips
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

static char *atlas_page_normalized_path(const char *atlas_name, uint32_t page);

/* --- pipeline_serialize: compute atlas UVs, write binary blob --- */

/* One sprite's serialized geometry, emitted before the blob is sized. Hashed and
 * compared as raw bytes, so padding would make block identity non-deterministic. */
typedef struct {
    NtAtlasVertex vertices[NT_POLYGON_MAX_VERTICES];
    uint16_t indices[NT_POLYGON_MAX_TRIANGLE_INDICES];
} SerializeBlock;
_Static_assert(sizeof(SerializeBlock) == (NT_POLYGON_MAX_VERTICES * sizeof(NtAtlasVertex)) + (NT_POLYGON_MAX_TRIANGLE_INDICES * sizeof(uint16_t)), "SerializeBlock must have no padding");

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void pipeline_serialize(AtlasPipeline *p) {
    const uint32_t max_region_tri_count = (uint32_t)(UINT8_MAX / 3U);
    /* Duplicate names, region-count cap and per-sprite trim-dim limits are all
     * validated pre-pack in pipeline_validate, so serialize is only ever reached
     * with every sprite within uint16 bounds. */

    /* Bound every sprite's block, alias included — each one owns its geometry and
     * is emitted from it. The blob is sized afterwards, from the byte-deduplicated
     * block totals.
     * region->index_count is uint8_t (max 255) → cap triangles per region at 85. */
    for (uint32_t i = 0; i < p->sprite_count; i++) {
        NT_BUILD_ASSERT(p->vertex_counts[i] <= UINT8_MAX && "pipeline_serialize: region vertex_count exceeds uint8_t");
        NT_BUILD_ASSERT(p->geometry_proofs[i].valid && "pipeline_serialize: selected geometry proof missing");
        uint32_t tri = p->triangle_index_counts[i] / 3U;
        NT_BUILD_ASSERT(p->triangle_index_counts[i] == (p->vertex_counts[i] - 2U) * 3U && "pipeline_serialize: selected triangle span mismatch");
        NT_BUILD_ASSERT(tri <= max_region_tri_count && "pipeline_serialize: region index_count exceeds uint8_t");
        /* The emit stage writes into fixed-size per-sprite storage; geometry
         * selection bounds both counts, so this can only fire on a builder bug. */
        NT_BUILD_ASSERT(p->vertex_counts[i] <= NT_POLYGON_MAX_VERTICES && p->triangle_index_counts[i] <= NT_POLYGON_MAX_TRIANGLE_INDICES &&
                        "pipeline_serialize: block exceeds per-sprite scratch bound");
    }

    /* Build placement lookup: original_sprite_index -> placement index.
     * commit rejects empty sprite sets, so sprite_count > 0 here, but
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

    /* Vertices + indices + regions.
     *
     * Every sprite's block — alias included — is emitted from its own geometry, its
     * own relative transform and its own index array. Byte-identical blocks then
     * share one byte range in the blob; pass 2 fills one NtAtlasRegion per sprite
     * from the assigned offsets. */
    SerializeBlock *blocks = (SerializeBlock *)calloc(p->sprite_count, sizeof(SerializeBlock));
    uint32_t *sprite_vertex_start = (uint32_t *)malloc(p->sprite_count * sizeof(uint32_t));
    uint32_t *sprite_index_start = (uint32_t *)malloc(p->sprite_count * sizeof(uint32_t));
    uint32_t *sprite_idx_count = (uint32_t *)malloc(p->sprite_count * sizeof(uint32_t));
    uint8_t *sprite_flags = (uint8_t *)calloc(p->sprite_count, sizeof(uint8_t));
    uint8_t *region_transform = (uint8_t *)calloc(p->sprite_count, sizeof(uint8_t));
    NT_BUILD_ASSERT(blocks && sprite_vertex_start && sprite_index_start && sprite_idx_count && sprite_flags && region_transform && "pipeline_serialize: alloc failed");

    // #region serialize emit
    for (uint32_t i = 0; i < p->sprite_count; i++) {
        uint32_t pi = placement_lookup[i];
        NT_BUILD_ASSERT(pi != UINT32_MAX && "pipeline_serialize: sprite has no placement");
        AtlasPlacement *pl = &p->placements[pi];
        /* An alias borrows its root's placement, so only an original's placement
         * points back at itself. */
        NT_BUILD_ASSERT((pl->sprite_index == i || p->dedup_map[i] >= 0) && "pipeline_serialize: placement belongs to another sprite");
        /* The anchor rule forces a group to identity placement as soon as any
         * member's relative is non-identity, so this is never a D4 composition. */
        NT_BUILD_ASSERT((p->alias_rel[i] == 0 || pl->transform == 0) && "pipeline_serialize: non-identity alias_rel requires identity placement");
        uint8_t rt = p->alias_rel[i] ? p->alias_rel[i] : pl->transform;
        region_transform[i] = rt;

        uint32_t vertex_count = p->vertex_counts[i];
        uint32_t idx_count = p->triangle_index_counts[i];
        sprite_idx_count[i] = idx_count;

        uint16_t local_indices[NT_POLYGON_MAX_TRIANGLE_INDICES];
        memcpy(local_indices, p->triangle_indices[i], (size_t)idx_count * sizeof(uint16_t));
        /* Flag detection runs on this sprite's own PNG-CCW pattern — must precede
         * the winding swap below or every QUAD_* match would miss. */
        sprite_flags[i] = atlas_region_flags_from_indices(vertex_count, idx_count, local_indices);
        swap_triangle_winding(local_indices, idx_count);
        /* Blob indices are local (0..vertex_count-1) and world-CCW after Y-flip. */
        memcpy(blocks[i].indices, local_indices, (size_t)idx_count * sizeof(uint16_t));

        uint32_t s_extrude = atlas_sprite_resolved_extrude(p, i);
        uint32_t extra_margin = sprite_extra_margin(p, i);
        uint32_t inner_x = pl->x + s_extrude + extra_margin;
        uint32_t inner_y = pl->y + s_extrude + extra_margin;
        uint32_t atlas_w = p->page_w[pl->page];
        uint32_t atlas_h = p->page_h[pl->page];

        for (uint32_t v = 0; v < vertex_count; v++) {
            NtAtlasVertex *vtx = &blocks[i].vertices[v];
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
            /* The UV is baked through this sprite's own orientation inside the
             * shared rectangle, against its own trim dims. */
            transform_point(lx, ly, rt, (int32_t)p->trim_w[i], (int32_t)p->trim_h[i], &tx, &ty);
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

        /* Re-prove the emitted bytes against this sprite's own mask: the check that
         * catches a wrong relative transform before it can ship. */
        Point2D reconstructed[NT_POLYGON_MAX_VERTICES];
        uint16_t reconstructed_indices[NT_POLYGON_MAX_TRIANGLE_INDICES];
        for (uint32_t v = 0; v < vertex_count; v++) {
            const NtAtlasVertex *serialized = &blocks[i].vertices[v];
            reconstructed[v].x = serialized->local_x;
            reconstructed[v].y = (int32_t)p->trim_h[i] - serialized->local_y;
        }
        memcpy(reconstructed_indices, blocks[i].indices, (size_t)idx_count * sizeof(uint16_t));
        swap_triangle_winding(reconstructed_indices, idx_count);
        uint8_t *binary = pipeline_geometry_binary_mask(p, i);
        nt_selected_geometry_proof_t serialized_proof =
            nt_selected_geometry_validate(binary, p->trim_w[i], p->trim_h[i], p->geometry_proofs[i].opaque_area2, p->baseline_vertices[i], p->baseline_vertex_counts[i],
                                          p->geometry_proofs[i].base_area2, p->baseline_triangle_indices[i], p->baseline_triangle_index_counts[i], reconstructed, vertex_count,
                                          p->geometry_proofs[i].selected_area2, reconstructed_indices, idx_count, p->geometry_opts[i].max_added_area_percent, p->geometry_opts[i].max_vertices);
        free(binary);
        NT_BUILD_ASSERT(serialized_proof.valid && nt_selected_geometry_proof_equal(&serialized_proof, &p->geometry_proofs[i]) && "serialized geometry proof mismatch");
    }
    // #endregion

    // #region serialize block dedup
    /* Assign each block a byte range, first writer in add order wins — which makes
     * the assignment deterministic without any tie-break rule. The hash only
     * narrows the search; identity is decided by memcmp, so a collision can share
     * nothing.
     * vertex_start/index_start are uint32_t in v3 — no practical bound until 4G. */
    uint32_t slot_capacity = 16;
    while (slot_capacity < p->sprite_count * 2U) {
        slot_capacity <<= 1U;
    }
    uint64_t *slot_hash = (uint64_t *)calloc(slot_capacity, sizeof(uint64_t));
    uint32_t *slot_owner = (uint32_t *)malloc((size_t)slot_capacity * sizeof(uint32_t));
    int32_t *block_owner = (int32_t *)malloc(p->sprite_count * sizeof(int32_t));
    NT_BUILD_ASSERT(slot_hash && slot_owner && block_owner && "pipeline_serialize: alloc failed");
    memset(slot_owner, 0xFF, (size_t)slot_capacity * sizeof(uint32_t));

    uint32_t vertex_cursor = 0;
    uint32_t index_cursor = 0;
    for (uint32_t i = 0; i < p->sprite_count; i++) {
        uint64_t hash = nt_hash64(&blocks[i], (uint32_t)sizeof(SerializeBlock)).value;
        uint32_t slot = (uint32_t)(hash & (slot_capacity - 1U));
        block_owner[i] = -1;
        while (slot_owner[slot] != UINT32_MAX) {
            uint32_t candidate = slot_owner[slot];
            if (slot_hash[slot] == hash && p->vertex_counts[candidate] == p->vertex_counts[i] && sprite_idx_count[candidate] == sprite_idx_count[i] &&
                memcmp(&blocks[candidate], &blocks[i], sizeof(SerializeBlock)) == 0) {
                block_owner[i] = (int32_t)candidate;
                break;
            }
            slot = (slot + 1U) & (slot_capacity - 1U);
        }
        if (block_owner[i] >= 0) {
            uint32_t owner = (uint32_t)block_owner[i];
            sprite_vertex_start[i] = sprite_vertex_start[owner];
            sprite_index_start[i] = sprite_index_start[owner];
            p->vertex_blocks_shared++;
            continue;
        }
        slot_hash[slot] = hash;
        slot_owner[slot] = i;
        sprite_vertex_start[i] = vertex_cursor;
        sprite_index_start[i] = index_cursor;
        vertex_cursor += p->vertex_counts[i];
        index_cursor += sprite_idx_count[i];
    }
    free(slot_hash);
    free(slot_owner);

    /* An identity relative emits the same bytes as its original, so it must land on
     * the original's range — this is what keeps packs byte-compatible. */
    for (uint32_t i = 0; i < p->sprite_count; i++) {
        if (p->dedup_map[i] >= 0 && p->alias_rel[i] == 0) {
            uint32_t orig = (uint32_t)p->dedup_map[i];
            NT_BUILD_ASSERT(sprite_vertex_start[i] == sprite_vertex_start[orig] && sprite_index_start[i] == sprite_index_start[orig] &&
                            "pipeline_serialize: identity alias did not share its original's block");
        }
    }

    uint32_t total_vertex_count = vertex_cursor;
    uint32_t total_index_count = index_cursor;
    // #endregion

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
        char *tex_path = atlas_page_normalized_path(p->state->name, pg);
        uint64_t tid = nt_hash64_str(tex_path).value;
        free(tex_path);
        memcpy(tex_ids_ptr + ((size_t)pg * sizeof(uint64_t)), &tid, sizeof(uint64_t));
    }

    NtAtlasRegion *regions = (NtAtlasRegion *)(blob + regions_offset);
    NtAtlasVertex *vertices = (NtAtlasVertex *)(blob + vertex_offset);
    uint16_t *indices = (uint16_t *)(blob + index_offset);

    /* Copy the winning blocks into the range they were assigned. */
    uint32_t copied_vertex_count = 0;
    uint32_t copied_index_count = 0;
    for (uint32_t i = 0; i < p->sprite_count; i++) {
        if (block_owner[i] >= 0) {
            continue; /* shares an earlier sprite's byte-identical block */
        }
        NT_BUILD_ASSERT(sprite_vertex_start[i] + p->vertex_counts[i] <= total_vertex_count && sprite_index_start[i] + sprite_idx_count[i] <= total_index_count &&
                        "pipeline_serialize: block overruns the sized blob");
        memcpy(&vertices[sprite_vertex_start[i]], blocks[i].vertices, (size_t)p->vertex_counts[i] * sizeof(NtAtlasVertex));
        memcpy(&indices[sprite_index_start[i]], blocks[i].indices, (size_t)sprite_idx_count[i] * sizeof(uint16_t));
        copied_vertex_count += p->vertex_counts[i];
        copied_index_count += sprite_idx_count[i];
    }
    NT_BUILD_ASSERT(copied_vertex_count == total_vertex_count && "pipeline_serialize: vertex count mismatch");
    NT_BUILD_ASSERT(copied_index_count == total_index_count && "pipeline_serialize: index count mismatch");

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
        reg->transform = region_transform[i];
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
        reg->_pad0 = 0;
        reg->slice9_lrtb[0] = sl;
        reg->slice9_lrtb[1] = sr;
        reg->slice9_lrtb[2] = st;
        reg->slice9_lrtb[3] = sb;
        memset(reg->_reserved2, 0, sizeof(reg->_reserved2));
    }

    free(blocks);
    free(block_owner);
    free(sprite_vertex_start);
    free(sprite_index_start);
    free(sprite_idx_count);
    free(sprite_flags);
    free(region_transform);

    p->atlas_blob = blob;
    p->atlas_blob_size = blob_size;
    p->atlas_blob_hash = nt_hash64(blob, blob_size).value;

    free(placement_lookup);
}

/* --- pipeline_publish_outputs: publish atlas, pages, metadata and codegen --- */

// NOLINTNEXTLINE(readability-function-cognitive-complexity) — bounded formatting and allocation asserts expand into branches
static char *atlas_page_normalized_path(const char *atlas_name, uint32_t page) {
    size_t atlas_len = strlen(atlas_name);
    NT_BUILD_ASSERT(atlas_len <= SIZE_MAX - sizeof("/tex4294967295") && "atlas page path too long");
    size_t size = atlas_len + sizeof("/tex4294967295");
    char *path = (char *)malloc(size);
    NT_BUILD_ASSERT(path && "atlas page path alloc failed");
    int written = snprintf(path, size, "%s/tex%u", atlas_name, page);
    NT_BUILD_ASSERT(written >= 0 && (size_t)written < size && "atlas page path formatting failed");
    char *normalized = nt_builder_normalize_path(path);
    free(path);
    NT_BUILD_ASSERT(normalized && "atlas page path normalization failed");
    return normalized;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity) — three overflow/allocation asserts expand into branches
static char *atlas_region_path(const char *atlas_name, const char *sprite_name) {
    size_t atlas_len = strlen(atlas_name);
    size_t sprite_len = strlen(sprite_name);
    NT_BUILD_ASSERT(sprite_len <= SIZE_MAX - 2 && "atlas region path too long");
    NT_BUILD_ASSERT(atlas_len <= SIZE_MAX - sprite_len - 2 && "atlas region path too long");
    size_t size = atlas_len + sprite_len + 2;
    char *path = (char *)malloc(size);
    NT_BUILD_ASSERT(path && "atlas region path alloc failed");
    int written = snprintf(path, size, "%s/%s", atlas_name, sprite_name);
    NT_BUILD_ASSERT(written >= 0 && (size_t)written < size && "atlas region path formatting failed");
    return path;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity) — registers N page textures and N region codegen entries in one pass; splitting would just shuffle locals
static void pipeline_publish_outputs(AtlasPipeline *p) {
    NtBuilderContext *ctx = p->ctx;
    nt_builder_add_entry(ctx, p->state->name, NT_BUILD_ASSET_ATLAS, NULL, p->atlas_blob, p->atlas_blob_size, p->atlas_blob_hash);
    p->atlas_blob = NULL;

    for (uint32_t pg = 0; pg < p->page_count; pg++) {
        char *tex_path = atlas_page_normalized_path(p->state->name, pg);

        size_t pixel_bytes = (size_t)p->page_w[pg] * p->page_h[pg] * 4;
        NT_BUILD_ASSERT(pixel_bytes <= UINT32_MAX && "pipeline_publish_outputs: page too large for nt_hash64 length");
        uint64_t tex_hash = nt_hash64(p->page_pixels[pg], (uint32_t)pixel_bytes).value;

        NtBuildTextureData *td = (NtBuildTextureData *)calloc(1, sizeof(NtBuildTextureData));
        NT_BUILD_ASSERT(td && "pipeline_publish_outputs: alloc failed");
        td->width = p->page_w[pg];
        td->height = p->page_h[pg];
        td->opts.format = p->opts->format;
        td->opts.max_size = 0;
        td->opts.compress = NULL;
        td->opts.premultiplied = p->opts->premultiplied;
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
        nt_builder_add_entry(ctx, tex_path, NT_BUILD_ASSET_TEXTURE, td, p->page_pixels[pg], (uint32_t)pixel_bytes, tex_hash);
        p->page_pixels[pg] = NULL;
        free(tex_path);
    }

    char *atlas_norm_path = nt_builder_normalize_path(p->state->name);
    NT_BUILD_ASSERT(atlas_norm_path && "atlas metadata path normalization failed");
    uint64_t atlas_resource_id = nt_hash64_str(atlas_norm_path).value;
    free(atlas_norm_path);
    uint64_t kind_ppu = nt_hash64_str("pixels_per_unit").value;
    nt_builder_add_meta(ctx, atlas_resource_id, kind_ppu, &p->opts->pixels_per_unit, sizeof(p->opts->pixels_per_unit));

    uint32_t required_regions = ctx->atlas_region_count + p->sprite_count;
    if (required_regions > ctx->atlas_region_capacity) {
        uint32_t new_capacity = ctx->atlas_region_capacity ? ctx->atlas_region_capacity : 64;
        while (new_capacity < required_regions) {
            new_capacity *= 2;
        }
        NtAtlasRegionCodegen *regions = (NtAtlasRegionCodegen *)realloc(ctx->atlas_regions, (size_t)new_capacity * sizeof(NtAtlasRegionCodegen));
        NT_BUILD_ASSERT(regions && "atlas region reserve failed");
        ctx->atlas_regions = regions;
        ctx->atlas_region_capacity = new_capacity;
    }
    for (uint32_t i = 0; i < p->sprite_count; i++) {
        /* path includes atlas prefix for unique C identifiers in codegen
         * (ASSET_ATLAS_REGION_SPINEBOY_HEAD_PNG), but resource_id hashes only
         * the sprite name — runtime looks up regions within a specific atlas
         * by name_hash, not by the full atlas/sprite path. */
        char *region_path = atlas_region_path(p->state->name, p->sprites[i].name);

        NtAtlasRegionCodegen *reg = &ctx->atlas_regions[ctx->atlas_region_count++];
        reg->path = nt_builder_normalize_path(region_path);
        free(region_path);
        NT_BUILD_ASSERT(reg->path && "atlas region path failed");
        reg->resource_id = nt_hash64_str(p->sprites[i].name).value;
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

    /* Every sprite owns its geometry — an alias carries a derived copy, not a borrowed pointer. */
    if (p->hull_vertices && p->triangle_indices && p->baseline_vertices && p->baseline_triangle_indices) {
        for (uint32_t i = 0; i < p->sprite_count; i++) {
            free(p->hull_vertices[i]);
            free(p->triangle_indices[i]);
            free(p->baseline_vertices[i]);
            free(p->baseline_triangle_indices[i]);
            p->hull_vertices[i] = NULL;
            p->triangle_indices[i] = NULL;
            p->baseline_vertices[i] = NULL;
            p->baseline_triangle_indices[i] = NULL;
        }
    }

    /* Free alpha planes */
    if (p->alpha_planes) {
        for (uint32_t i = 0; i < p->sprite_count; i++) {
            free(p->alpha_planes[i]);
        }
    }
    free((void *)p->alpha_planes);

    free(p->trim_x);
    free(p->trim_y);
    free(p->trim_w);
    free(p->trim_h);
    free(p->geometry_opts);
    free(p->dedup_map);
    free(p->unique_indices);
    free(p->alias_rel);
    free(p->vertex_counts);
    free((void *)p->hull_vertices);
    free((void *)p->triangle_indices);
    free(p->triangle_index_counts);
    free((void *)p->baseline_vertices);
    free(p->baseline_vertex_counts);
    free((void *)p->baseline_triangle_indices);
    free(p->baseline_triangle_index_counts);
    free(p->geometry_proofs);
    free(p->placements);

    /* Free page pixels not transferred to published entries. */
    if (p->page_pixels) {
        for (uint32_t pg = 0; pg < p->page_count; pg++) {
            free(p->page_pixels[pg]);
        }
        free((void *)p->page_pixels);
    }
    free(p->atlas_blob);

    /* Published entry counters already include the atlas metadata blob. */
    free(p->state->sprites);
    free(p->state->name);
    free(p->state);
    p->ctx->active_atlas = NULL;
}

/* --- nt_atlas_commit: orchestrator — calls pipeline steps in order --- */

// NOLINTNEXTLINE(readability-function-cognitive-complexity) — linear sequence of pipeline_* stage calls with shared cleanup; "high" complexity reflects stage count, not control-flow depth
nt_build_result_t nt_atlas_commit(NtAtlasBuild *atlas) {
    NT_BUILD_ASSERT(atlas && atlas->ctx && atlas->ctx->active_atlas == atlas && "atlas_commit: invalid atlas handle");
    NT_BUILD_ASSERT(atlas->add_seq_counter > 0 && "atlas_commit: atlas has no sprites");
    NtBuilderContext *ctx = atlas->ctx;
    NtAtlasBuild *state = atlas;
    AtlasPipeline p = {0};
    p.ctx = ctx;
    p.state = state;
    p.sprite_count = state->sprite_count;
    p.sprites = state->sprites;
    p.opts = &state->opts;
    p.thread_count = ctx->thread_count;

    /* Validate surviving sprites even when earlier inputs had content errors. */
    if (state->failed && state->sprite_count == 0) {
        nt_build_result_t result = atlas_merge_errors(state);
        pipeline_cleanup(&p);
        return result;
    }

    NT_BUILD_ASSERT(state->sprite_count > 0 && "atlas_commit: atlas has no sprites");

    /* Warn on non-premultiplied atlases. Bilinear filtering at sprite gaps
     * mixes opaque pixels with transparent (0,0,0,0) background, producing
     * dark fringes. Valid use cases (NEAREST filter, fully opaque sprites)
     * exist but are rare — keep the user aware. */
    if (!state->opts.premultiplied) {
        NT_LOG_WARN("atlas '%s': premultiplied=false — bilinear filter will cause dark fringes at sprite edges. Use only with NEAREST filter or fully opaque sprites.", state->name);
    }

    NT_LOG_INFO("  atlas_commit: %u sprites, starting pipeline...", p.sprite_count);
    double t0 = nt_time_now();
    double t_total = t0;

    pipeline_resolve_geometry_opts(&p);
    pipeline_alpha_trim(&p);
    double bench_alpha_trim = nt_time_now() - t0;

    /* Geometry still runs on survivors to collect cross-stage content errors. */
    if (!state->failed) {
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
     * after earlier content errors, so every bad sprite is reported at once. */
    pipeline_validate(&p);

    /* First hard gate: a content error from trim, geometry or validate skips all
     * packing, compose, staging and publish — fall through to the single
     * cleanup. Everything downstream provably has only fittable, in-range,
     * uniquely-named sprites. */
    if (state->failed) {
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
        if (state->failed) {
            goto cleanup;
        }

        t0 = nt_time_now();
        pipeline_compose(&p);
        bench_compose = nt_time_now() - t0;
    }

    t0 = nt_time_now();
    pipeline_serialize(&p);
    double bench_serialize = nt_time_now() - t0;

    if (!p.cache_hit) {
        pipeline_cache_write(&p);
    }
    t0 = nt_time_now();
    pipeline_debug_png(&p);
    bench_debug_png = nt_time_now() - t0;
    pipeline_publish_outputs(&p);

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

cleanup:;
    nt_build_result_t result = state->failed ? atlas_merge_errors(state) : NT_BUILD_OK;
    pipeline_cleanup(&p);
    return result;
}

// #endregion

/* --- Test-access wrapper (atlas internals remain static) ---
 * Thin pass-through so unit tests can exercise extrude_edges without
 * making it non-static. Builder is a developer tool, not a shippable
 * runtime, so the extra symbol has no practical cost. */
void nt_atlas_test_extrude_edges(uint8_t *page, uint32_t page_w, uint32_t page_h, uint32_t px, uint32_t py, uint32_t sw, uint32_t sh, uint32_t extrude_count) {
    extrude_edges(page, page_w, page_h, px, py, sw, sh, extrude_count);
}
