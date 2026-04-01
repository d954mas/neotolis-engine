#include "font/nt_font.h"
#include "font/nt_font_internal.h"

#include <stdlib.h>
#include <string.h>

#include "core/nt_assert.h"
#include "core/nt_utf8.h"
#include "graphics/nt_gfx.h"
#include "log/nt_log.h"
#include "math/nt_math.h"
#include "nt_font_format.h"
#include "nt_pack_format.h"
#include "pool/nt_pool.h"
#include "resource/nt_resource.h"

/* ---- Font data storage (side table for pack blobs accessed via activator) ---- */

typedef struct {
    const uint8_t *data;
    uint32_t size;
} nt_font_data_entry_t;

/* ---- Module state ---- */

static nt_font_state_t s_font;

/* ---- Font activator callbacks ---- */

static nt_font_data_entry_t *font_data_entries(void) { return (nt_font_data_entry_t *)s_font.data_entries; }

static uint32_t activate_font(const uint8_t *data, uint32_t size) {
    /* Store pointer to pack data for later access by font module.
     * The data pointer is valid as long as the pack remains mounted. */
    nt_font_data_entry_t *entries = font_data_entries();

    /* Reuse freed slot if available */
    for (uint32_t i = 0; i < s_font.data_count; i++) {
        if (entries[i].data == NULL) {
            entries[i].data = data;
            entries[i].size = size;
            return i + 1;
        }
    }

    NT_ASSERT(s_font.data_count < s_font.data_capacity);
    uint32_t idx = s_font.data_count++;
    entries[idx].data = data;
    entries[idx].size = size;
    return idx + 1; /* 1-based handle (0 = failure in resource system) */
}

static void deactivate_font(uint32_t runtime_handle) {
    if (runtime_handle == 0 || runtime_handle > s_font.data_count) {
        return;
    }
    nt_font_data_entry_t *entries = font_data_entries();
    uint32_t idx = runtime_handle - 1;
    entries[idx].data = NULL;
    entries[idx].size = 0;
}

/* Get font data from activation handle */
static const uint8_t *get_font_data(uint32_t runtime_handle, uint32_t *out_size) {
    if (runtime_handle == 0 || runtime_handle > s_font.data_count) {
        if (out_size) {
            *out_size = 0;
        }
        return NULL;
    }
    nt_font_data_entry_t *entries = font_data_entries();
    uint32_t idx = runtime_handle - 1;
    if (out_size) {
        *out_size = entries[idx].size;
    }
    return entries[idx].data;
}

/* ---- Internal helpers ---- */

static nt_font_slot_t *get_slot(nt_font_t font) {
    uint32_t idx = nt_pool_slot_index(font.id);
    return &s_font.slots[idx];
}

/* ---- Codepoint hash table (open addressing) ---- */

/* Round up to next power of two */
static uint16_t next_pot16(uint16_t v) {
    v--;
    v |= v >> 1;
    v |= v >> 2;
    v |= v >> 4;
    v |= v >> 8;
    return (uint16_t)(v + 1);
}

/* Lookup: returns pointer to cache entry, or NULL on miss */
static nt_font_cache_slot_t *hash_lookup(nt_font_slot_t *slot, uint32_t codepoint) {
    uint16_t mask = (uint16_t)(slot->hash_table_size - 1);
    uint16_t pos = (uint16_t)(codepoint & mask);
    for (;;) {
        uint16_t val = slot->hash_table[pos];
        if (val == 0) {
            return NULL;
        }
        nt_font_cache_slot_t *cs = &slot->cache[val - 1];
        if (cs->entry.codepoint == codepoint) {
            return cs;
        }
        pos = (uint16_t)((pos + 1) & mask);
    }
}

/* Insert: slot_idx is index into cache[] */
static void hash_insert(nt_font_slot_t *slot, uint32_t codepoint, uint16_t slot_idx) {
    uint16_t mask = (uint16_t)(slot->hash_table_size - 1);
    uint16_t pos = (uint16_t)(codepoint & mask);
    for (;;) {
        if (slot->hash_table[pos] == 0) {
            slot->hash_table[pos] = (uint16_t)(slot_idx + 1);
            return;
        }
        pos = (uint16_t)((pos + 1) & mask);
    }
}

/* Remove one entry by codepoint with backshift to keep probe chains intact.
 * Called by evict_lru before clearing the cache slot — O(cluster) instead of O(N) rebuild. */
static void hash_remove(nt_font_slot_t *slot, uint32_t codepoint) {
    uint16_t mask = (uint16_t)(slot->hash_table_size - 1);
    uint16_t pos = (uint16_t)(codepoint & mask);

    /* Find the entry */
    for (;;) {
        if (slot->hash_table[pos] == 0) {
            return; /* not found — nothing to remove */
        }
        uint16_t idx = (uint16_t)(slot->hash_table[pos] - 1);
        if (slot->cache[idx].entry.codepoint == codepoint) {
            break;
        }
        pos = (uint16_t)((pos + 1) & mask);
    }

    /* Backshift: move subsequent entries back to fill the gap */
    uint16_t empty = pos;
    for (;;) {
        pos = (uint16_t)((pos + 1) & mask);
        if (slot->hash_table[pos] == 0) {
            break; /* end of cluster */
        }
        uint16_t idx = (uint16_t)(slot->hash_table[pos] - 1);
        uint16_t home = (uint16_t)(slot->cache[idx].entry.codepoint & mask);
        /* Check if this entry's home is at or before the empty slot (wrapping) */
        bool should_move = (empty <= pos) ? (home <= empty || home > pos) : (home <= empty && home > pos);
        if (should_move) {
            slot->hash_table[empty] = slot->hash_table[pos];
            empty = pos;
        }
    }
    slot->hash_table[empty] = 0;
}

/* ---- Glyph bsearch comparator ---- */

static int compare_glyph_codepoint(const void *key, const void *elem) {
    uint32_t cp = *(const uint32_t *)key;
    const NtFontGlyphEntry *entry = (const NtFontGlyphEntry *)elem;
    if (cp < entry->codepoint) {
        return -1;
    }
    if (cp > entry->codepoint) {
        return 1;
    }
    return 0;
}

/* Find glyph entry in a single pack blob by codepoint via bsearch */
static const NtFontGlyphEntry *find_glyph_in_pack(const uint8_t *blob, uint32_t blob_size, uint32_t codepoint) {
    if (blob_size < sizeof(NtFontAssetHeader)) {
        return NULL;
    }
    const NtFontAssetHeader *hdr = (const NtFontAssetHeader *)blob;
    if (hdr->glyph_count == 0) {
        return NULL;
    }
    const NtFontGlyphEntry *glyphs = (const NtFontGlyphEntry *)(blob + sizeof(NtFontAssetHeader));
    return (const NtFontGlyphEntry *)bsearch(&codepoint, glyphs, hdr->glyph_count, sizeof(NtFontGlyphEntry), compare_glyph_codepoint);
}

/* Find glyph across all resources (first wins, per D-18) */
static bool find_glyph_in_resources(nt_font_slot_t *slot, uint32_t codepoint, uint8_t *out_resource_index, const NtFontGlyphEntry **out_glyph_entry) {
    for (uint8_t i = 0; i < slot->resource_count; i++) {
        if (slot->resource_handles[i] == 0) {
            continue; /* not loaded yet */
        }
        uint32_t blob_size = 0;
        const uint8_t *blob = get_font_data(slot->resource_handles[i], &blob_size);
        if (!blob) {
            continue;
        }
        const NtFontGlyphEntry *entry = find_glyph_in_pack(blob, blob_size, codepoint);
        if (entry) {
            *out_resource_index = i;
            *out_glyph_entry = entry;
            return true;
        }
    }
    return false;
}

/* Find glyph index (position in glyph table) for a codepoint in a blob */
static int32_t find_glyph_index(const uint8_t *blob, uint32_t blob_size, uint32_t codepoint) {
    if (blob_size < sizeof(NtFontAssetHeader)) {
        return -1;
    }
    const NtFontGlyphEntry *glyphs = (const NtFontGlyphEntry *)(blob + sizeof(NtFontAssetHeader));
    const NtFontGlyphEntry *found = find_glyph_in_pack(blob, blob_size, codepoint);
    if (!found) {
        return -1;
    }
    return (int32_t)(found - glyphs);
}

/* ---- Kern bsearch comparator ---- */

static int compare_kern_right(const void *key, const void *elem) {
    uint16_t right = *(const uint16_t *)key;
    const NtFontKernEntry *entry = (const NtFontKernEntry *)elem;
    if (right < entry->right_glyph_index) {
        return -1;
    }
    if (right > entry->right_glyph_index) {
        return 1;
    }
    return 0;
}

/* ---- LRU eviction (D-15, D-23) ---- */

static uint16_t evict_lru(nt_font_slot_t *slot) {
    uint32_t max_age = 0;
    uint16_t victim = 0;
    bool found = false;
    for (uint16_t i = 0; i < slot->max_glyphs; i++) {
        nt_font_cache_slot_t *cs = &slot->cache[i];
        if (cs->entry.codepoint == 0) {
            continue; /* empty slot */
        }
        if (cs->entry.is_tofu) {
            continue; /* never evict tofu (D-23) */
        }
        uint32_t age = s_font.frame_counter - cs->lru_frame; /* unsigned wrap-safe */
        if (age >= max_age) {
            max_age = age;
            victim = i;
            found = true;
        }
    }
    NT_ASSERT(found); /* no evictable entry found */

    /* Remove from hash table before clearing slot */
    hash_remove(slot, slot->cache[victim].entry.codepoint);

    /* Clear victim */
    memset(&slot->cache[victim], 0, sizeof(nt_font_cache_slot_t));
    slot->glyphs_cached--;
    return victim;
}

/* ---- Curve texture space management ---- */

/* Max curves per glyph (256 handles any real-world glyph, override via -D) */
#ifndef NT_FONT_MAX_CURVES_PER_GLYPH
#define NT_FONT_MAX_CURVES_PER_GLYPH 256
#endif

/* Static temp buffer for GPU upload (no CPU mirror needed).
 * Max texels per glyph: NT_FONT_MAX_CURVES_PER_GLYPH * bands * 2 * 2 (Y+X bands).
 * 256 curves * 32 bands * 2 texels * 2 axes * 4 uint16 = 256KB worst case.
 * Realistic: 40 curves * 8 bands * 2 * 2 * 4 = 5KB. */
static uint16_t s_curve_upload[NT_FONT_MAX_CURVES_PER_GLYPH * 32 * 2 * 4 * 2];

/* Reset free stack to all slots available (0..max_glyphs-1) */
static void free_stack_reset(nt_font_slot_t *slot) {
    for (uint16_t i = 0; i < slot->max_glyphs; i++) {
        slot->free_stack[i] = (uint16_t)(slot->max_glyphs - 1 - i); /* top of stack = 0 */
    }
    slot->free_top = slot->max_glyphs;
}

static uint16_t free_stack_pop(nt_font_slot_t *slot) {
    NT_ASSERT(slot->free_top > 0);
    return slot->free_stack[--slot->free_top];
}

/* Flush entire cache when curve texture is full */
static void flush_cache(nt_font_slot_t *slot) {
    NT_LOG_WARN("font cache flush: curve texture full (%ux%u), consider larger curve_texture_width/height", slot->curve_tex_width, slot->curve_tex_height);
    memset(slot->cache, 0, (size_t)slot->max_glyphs * sizeof(nt_font_cache_slot_t));
    memset(slot->hash_table, 0, (size_t)slot->hash_table_size * sizeof(uint16_t));
    free_stack_reset(slot);
    slot->glyphs_cached = 0;
    slot->curve_write_head = 0;
    slot->tofu_generated = false;
    slot->cache_generation++;
}

/* Ensure enough texels, flushing if needed.
 * WARNING: flush_cache invalidates ALL cache entries, hash table, and bumps
 * cache_generation. Callers (upload_glyph, generate_tofu) must handle the
 * case where the cache was reset mid-operation. Currently safe because
 * cache_idx is (re)allocated after this call returns. */
static void ensure_curve_space(nt_font_slot_t *slot, uint32_t needed_texels) {
    uint32_t total = (uint32_t)slot->curve_tex_width * slot->curve_tex_height;
    NT_ASSERT(needed_texels <= total); /* single glyph exceeds entire curve texture */
    if (slot->curve_write_head + needed_texels > total) {
        flush_cache(slot);
    }
}

/* ---- Tofu generation (D-22, D-23) ---- */

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void generate_tofu(nt_font_slot_t *slot) {
    if (slot->tofu_generated) {
        return;
    }
    if (!slot->metrics_set) {
        return; /* resources not loaded yet — tofu impossible, lookup returns NULL */
    }

    /* Tofu dimensions from metrics */
    int16_t tofu_w = (int16_t)(slot->metrics.units_per_em / 2);
    int16_t y0 = slot->metrics.descent;
    int16_t y1 = slot->metrics.ascent;

    /* 4 line segments forming a rectangle: bottom, right, top, left
     * Each line promoted to degenerate quadratic: p1 = midpoint(p0, p2)
     * 4 curves x 2 texels x 2 (Y-bands + X-bands) = 16 texels needed */
    uint32_t needed_texels = 4 * 2 * 2;

    /* Ensure we have cache slot space */
    ensure_curve_space(slot, needed_texels);
    uint16_t cache_idx;
    if (slot->glyphs_cached < slot->max_glyphs) {
        cache_idx = free_stack_pop(slot);
    } else {
        cache_idx = evict_lru(slot);
    }

    /* Rectangle corners: (0, y0), (tofu_w, y0), (tofu_w, y1), (0, y1) */
    /* clang-format off */
    float lines[4][4] = {
        {0,       (float)y0, (float)tofu_w, (float)y0}, /* bottom: left to right */
        {(float)tofu_w, (float)y0, (float)tofu_w, (float)y1}, /* right: bottom to top */
        {(float)tofu_w, (float)y1, 0,       (float)y1}, /* top: right to left */
        {0,       (float)y1, 0,       (float)y0}, /* left: top to bottom */
    };
    /* clang-format on */

    uint32_t curve_offset = slot->curve_write_head;

    for (int seg = 0; seg < 4; seg++) {
        float p0x = lines[seg][0];
        float p0y = lines[seg][1];
        float p2x = lines[seg][2];
        float p2y = lines[seg][3];
        /* Degenerate quadratic: p1 = midpoint(p0, p2) */
        float p1x = (p0x + p2x) * 0.5F;
        float p1y = (p0y + p2y) * 0.5F;

        uint32_t t0 = (uint32_t)seg * 2 * 4;
        uint32_t t1 = t0 + 4;
        s_curve_upload[t0 + 0] = nt_float32_to_float16(p0x);
        s_curve_upload[t0 + 1] = nt_float32_to_float16(p0y);
        s_curve_upload[t0 + 2] = nt_float32_to_float16(p1x);
        s_curve_upload[t0 + 3] = nt_float32_to_float16(p1y);
        s_curve_upload[t1 + 0] = nt_float32_to_float16(p2x);
        s_curve_upload[t1 + 1] = nt_float32_to_float16(p2y);
        s_curve_upload[t1 + 2] = 0;
        s_curve_upload[t1 + 3] = 0;
    }

    /* Duplicate same 4 curves for X-bands (appended after Y-band data) */
    uint32_t x_curve_offset = curve_offset + 4 * 2; /* after Y-band curves */
    for (int seg = 0; seg < 4; seg++) {
        uint32_t src0 = (uint32_t)seg * 2 * 4;
        uint32_t dst0 = (uint32_t)(4 + seg) * 2 * 4;
        for (int k = 0; k < 8; k++) {
            s_curve_upload[dst0 + k] = s_curve_upload[src0 + k];
        }
    }

    /* Upload all curve data (Y + X) to GPU */
    uint32_t remaining = needed_texels;
    uint32_t src_texel = 0;
    uint32_t dst_texel = curve_offset;
    while (remaining > 0) {
        uint16_t row2 = (uint16_t)(dst_texel / slot->curve_tex_width);
        uint16_t col2 = (uint16_t)(dst_texel % slot->curve_tex_width);
        uint16_t w2 = (uint16_t)(slot->curve_tex_width - col2);
        if (w2 > remaining) {
            w2 = (uint16_t)remaining;
        }
        nt_gfx_update_texture(slot->curve_texture, col2, row2, w2, 1, &s_curve_upload[(size_t)src_texel * 4]);
        remaining -= w2;
        src_texel += w2;
        dst_texel += w2;
    }

    /* Upload band data for tofu -- all 4 curves in every Y-band and X-band */
    uint16_t band_data[64 * 2] = {0};
    for (uint8_t b = 0; b < slot->band_count; b++) {
        band_data[(b * 2) + 0] = 0; /* Y-band: curve_start */
        band_data[(b * 2) + 1] = 4; /* Y-band: curve_count */
    }
    uint8_t xoff = slot->band_count;
    for (uint8_t b = 0; b < slot->band_count; b++) {
        band_data[((xoff + b) * 2) + 0] = 0; /* X-band: curve_start */
        band_data[((xoff + b) * 2) + 1] = 4; /* X-band: curve_count */
    }
    nt_gfx_update_texture(slot->band_texture, 0, cache_idx, (uint16_t)(slot->band_count * 2), 1, band_data);

    /* Fill cache entry */
    nt_font_cache_slot_t *cs = &slot->cache[cache_idx];
    cs->entry.codepoint = 0xFFFFFFFFU; /* sentinel (D-22) */
    cs->entry.curve_offset = curve_offset;
    cs->entry.curve_offset_x = x_curve_offset;
    cs->entry.curve_count = 8;
    cs->entry.band_row = cache_idx;
    cs->entry.advance = tofu_w;
    cs->entry.bbox_x0 = 0;
    cs->entry.bbox_y0 = y0;
    cs->entry.bbox_x1 = tofu_w;
    cs->entry.bbox_y1 = y1;
    cs->entry.is_tofu = true;
    cs->lru_frame = s_font.frame_counter;

    slot->curve_write_head += needed_texels;
    slot->glyphs_cached++;
    slot->tofu_generated = true;
    hash_insert(slot, 0xFFFFFFFFU, cache_idx);
}

/* ---- Decode contours and upload glyph to GPU ---- */

/* Temporary curve storage for band decomposition */
typedef struct {
    float p0x, p0y, p1x, p1y, p2x, p2y;
} nt_curve_t;

static nt_curve_t s_decode_curves[NT_FONT_MAX_CURVES_PER_GLYPH];

/* Temporary point buffers for contour decoding (static to avoid ~72 KB on stack) */
static int32_t s_decode_pts_x[8192];
static int32_t s_decode_pts_y[8192];
static uint8_t s_decode_pts_on[8192];

/* Read variable-length delta: int8 or sentinel + int16 */
static inline int16_t read_varlen_delta(const uint8_t **rp) {
    uint8_t b = **rp;
    (*rp)++;
    if (b != NT_FONT_DELTA_SENTINEL) {
        return (int16_t)(int8_t)b;
    }
    int16_t val;
    memcpy(&val, *rp, 2);
    (*rp) += 2;
    return val;
}

/* Emit one quadratic curve to the output buffer */
static inline void emit_curve(nt_curve_t *curves, uint16_t *total, uint16_t max_c, float p0x, float p0y, float p1x, float p1y, float p2x, float p2y) {
    if (*total < max_c) {
        curves[*total] = (nt_curve_t){p0x, p0y, p1x, p1y, p2x, p2y};
        (*total)++;
    }
}

/* Decode v4 point-based contour data into absolute float curves.
 * Handles implicit midpoints between consecutive off-curve points. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static uint16_t decode_contours(const uint8_t *contour_data, nt_curve_t *curves, uint16_t max_curves) {
    const uint8_t *rp = contour_data;
    uint16_t contour_count;
    memcpy(&contour_count, rp, 2);
    rp += 2;

    uint16_t total_curves = 0;

    for (uint16_t ci = 0; ci < contour_count; ci++) {
        uint16_t point_count;
        memcpy(&point_count, rp, 2);
        rp += 2;

        uint32_t flags_bytes = NT_FONT_BITMASK_BYTES(point_count);
        const uint8_t *flags = rp;
        rp += flags_bytes;

        // #region Read all points (absolute coordinates)
        /* First point is absolute int16 */
        int16_t first_x;
        int16_t first_y;
        memcpy(&first_x, rp, 2);
        rp += 2;
        memcpy(&first_y, rp, 2);
        rp += 2;

        /* Decode all points into static buffers (avoids ~72 KB on stack) */
        int32_t *pts_x = s_decode_pts_x;
        int32_t *pts_y = s_decode_pts_y;
        uint8_t *pts_on = s_decode_pts_on;
        NT_ASSERT(point_count <= 8192);

        pts_x[0] = first_x;
        pts_y[0] = first_y;
        pts_on[0] = (flags[0] & 1U) != 0;

        int32_t px = first_x;
        int32_t py = first_y;
        for (uint16_t p = 1; p < point_count; p++) {
            int16_t dx = read_varlen_delta(&rp);
            int16_t dy = read_varlen_delta(&rp);
            px += dx;
            py += dy;
            pts_x[p] = px;
            pts_y[p] = py;
            pts_on[p] = (flags[p / 8] & (1U << (p % 8))) != 0;
        }
        // #endregion

        // #region Convert points to quadratic curves (TrueType rules)
        /* Walk the closed contour: point[0] → point[1] → ... → point[n-1] → point[0] */
        uint16_t n = point_count;
        uint16_t i = 0;

        /* Find first on-curve point to start from */
        uint16_t start = 0;
        while (start < n && !pts_on[start]) {
            start++;
        }
        if (start == n) {
            /* All off-curve: start from implicit midpoint of first two */
            start = 0;
        }

        /* Current on-curve position */
        float cur_x;
        float cur_y;
        if (pts_on[start]) {
            cur_x = (float)pts_x[start];
            cur_y = (float)pts_y[start];
            i = (start + 1) % n;
        } else {
            /* All off-curve: midpoint between point[0] and point[1] */
            cur_x = (float)(pts_x[0] + pts_x[1]) * 0.5F;
            cur_y = (float)(pts_y[0] + pts_y[1]) * 0.5F;
            i = 0;
        }

        uint16_t steps = 0;
        while (steps < n) {
            uint16_t idx = i % n;

            if (pts_on[idx]) {
                /* On-curve → Line segment (degenerate quad) */
                float ex = (float)pts_x[idx];
                float ey = (float)pts_y[idx];
                emit_curve(curves, &total_curves, max_curves, cur_x, cur_y, (cur_x + ex) * 0.5F, (cur_y + ey) * 0.5F, ex, ey);
                cur_x = ex;
                cur_y = ey;
                i = (idx + 1) % n;
                steps++;
            } else {
                /* Off-curve: control point for quadratic */
                float cx = (float)pts_x[idx];
                float cy = (float)pts_y[idx];
                uint16_t next = (idx + 1) % n;

                if (pts_on[next]) {
                    /* off → on: standard quad */
                    float ex = (float)pts_x[next];
                    float ey = (float)pts_y[next];
                    emit_curve(curves, &total_curves, max_curves, cur_x, cur_y, cx, cy, ex, ey);
                    cur_x = ex;
                    cur_y = ey;
                    i = (next + 1) % n;
                    steps += 2;
                } else {
                    /* off → off: implicit midpoint is the endpoint */
                    float mx = (cx + (float)pts_x[next]) * 0.5F;
                    float my = (cy + (float)pts_y[next]) * 0.5F;
                    emit_curve(curves, &total_curves, max_curves, cur_x, cur_y, cx, cy, mx, my);
                    cur_x = mx;
                    cur_y = my;
                    i = next; /* don't skip next — it becomes the control for the next segment */
                    steps++;
                }
            }
        }
        // #endregion
    }
    return total_curves;
}

/* Upload a glyph to GPU textures and fill cache entry.
 * Allocates cache slot internally (after ensure_curve_space) to avoid
 * the flush-invalidates-slot bug. Returns allocated cache_idx. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static uint16_t upload_glyph(nt_font_slot_t *slot, const NtFontGlyphEntry *glyph, const nt_curve_t *curves, uint16_t curve_count, uint8_t resource_index) {
    float bbox_y0 = (float)glyph->bbox_y0;
    float bbox_y1 = (float)glyph->bbox_y1;
    float bbox_x0 = (float)glyph->bbox_x0;
    float bbox_x1 = (float)glyph->bbox_x1;
    float band_height = (bbox_y1 - bbox_y0) / (float)slot->band_count;
    float band_width = (bbox_x1 > bbox_x0) ? (bbox_x1 - bbox_x0) / (float)slot->band_count : 0.0F;
    NT_ASSERT(slot->band_count <= 32);

    // #region Precompute per-curve Y and X bounds
    float curve_y_min[NT_FONT_MAX_CURVES_PER_GLYPH];
    float curve_y_max[NT_FONT_MAX_CURVES_PER_GLYPH];
    float curve_x_min[NT_FONT_MAX_CURVES_PER_GLYPH];
    float curve_x_max[NT_FONT_MAX_CURVES_PER_GLYPH];
    for (uint16_t ci = 0; ci < curve_count; ci++) {
        float ay = curves[ci].p0y, by = curves[ci].p1y, cy = curves[ci].p2y;
        float loy = ay < by ? ay : by;
        float hiy = ay > by ? ay : by;
        curve_y_min[ci] = loy < cy ? loy : cy;
        curve_y_max[ci] = hiy > cy ? hiy : cy;

        float ax = curves[ci].p0x, bx = curves[ci].p1x, cx = curves[ci].p2x;
        float lox = ax < bx ? ax : bx;
        float hix = ax > bx ? ax : bx;
        curve_x_min[ci] = lox < cx ? lox : cx;
        curve_x_max[ci] = hix > cx ? hix : cx;
    }
    // #endregion

    // #region Count Y-band and X-band curve pairs
    /* Epsilon margin on band boundaries to avoid edge-case misses where
     * floating-point rounding places a curve in one band but the shader
     * maps the pixel to the adjacent band. */
    float y_margin = band_height * 0.01F;
    float x_margin = band_width * 0.01F;

    uint16_t yband_counts[32] = {0};
    uint16_t xband_counts[32] = {0};
    for (uint16_t ci = 0; ci < curve_count; ci++) {
        for (uint8_t b = 0; b < slot->band_count; b++) {
            float ybot = bbox_y0 + ((float)b * band_height) - y_margin;
            float ytop = ybot + band_height + y_margin * 2.0F;
            if (curve_y_max[ci] >= ybot && curve_y_min[ci] <= ytop) {
                yband_counts[b]++;
            }
            if (band_width > 0.0F) {
                float xleft = bbox_x0 + ((float)b * band_width) - x_margin;
                float xright = xleft + band_width + x_margin * 2.0F;
                if (curve_x_max[ci] >= xleft && curve_x_min[ci] <= xright) {
                    xband_counts[b]++;
                }
            }
        }
    }

    uint32_t y_total = 0;
    uint32_t x_total = 0;
    for (uint8_t b = 0; b < slot->band_count; b++) {
        y_total += yband_counts[b];
        x_total += xband_counts[b];
    }
    uint32_t needed_texels = (y_total + x_total) * 2;
    // #endregion

    ensure_curve_space(slot, needed_texels);

    // #region Allocate cache slot (after ensure_curve_space to avoid flush-invalidates-slot)
    uint16_t cache_idx;
    if (slot->glyphs_cached < slot->max_glyphs) {
        cache_idx = free_stack_pop(slot);
    } else {
        cache_idx = evict_lru(slot);
    }
    // #endregion

    // #region Write Y-band curves to temp buffer
    uint32_t curve_offset_y = slot->curve_write_head;
    uint16_t yband_offsets[32] = {0};

    uint32_t local_pos = 0;
    for (uint8_t b = 0; b < slot->band_count; b++) {
        yband_offsets[b] = (uint16_t)(local_pos / 2);
        float ybot = bbox_y0 + ((float)b * band_height) - y_margin;
        float ytop = ybot + band_height + y_margin * 2.0F;
        for (uint16_t ci = 0; ci < curve_count; ci++) {
            if (curve_y_max[ci] < ybot || curve_y_min[ci] > ytop) {
                continue;
            }
            uint32_t t0 = local_pos * 4;
            uint32_t t1 = t0 + 4;
            s_curve_upload[t0 + 0] = nt_float32_to_float16(curves[ci].p0x);
            s_curve_upload[t0 + 1] = nt_float32_to_float16(curves[ci].p0y);
            s_curve_upload[t0 + 2] = nt_float32_to_float16(curves[ci].p1x);
            s_curve_upload[t0 + 3] = nt_float32_to_float16(curves[ci].p1y);
            s_curve_upload[t1 + 0] = nt_float32_to_float16(curves[ci].p2x);
            s_curve_upload[t1 + 1] = nt_float32_to_float16(curves[ci].p2y);
            s_curve_upload[t1 + 2] = 0;
            s_curve_upload[t1 + 3] = 0;
            local_pos += 2;
        }
    }
    // #endregion

    // #region Write X-band curves to temp buffer (after Y-band data)
    uint32_t y_local_pos = local_pos;
    uint32_t curve_offset_x = curve_offset_y + y_local_pos;
    uint16_t xband_offsets[32] = {0};

    for (uint8_t b = 0; b < slot->band_count; b++) {
        xband_offsets[b] = (uint16_t)((local_pos - y_local_pos) / 2);
        if (band_width > 0.0F) {
            float xleft = bbox_x0 + ((float)b * band_width) - x_margin;
            float xright = xleft + band_width + x_margin * 2.0F;
            for (uint16_t ci = 0; ci < curve_count; ci++) {
                if (curve_x_max[ci] < xleft || curve_x_min[ci] > xright) {
                    continue;
                }
                uint32_t t0 = local_pos * 4;
                uint32_t t1 = t0 + 4;
                s_curve_upload[t0 + 0] = nt_float32_to_float16(curves[ci].p0x);
                s_curve_upload[t0 + 1] = nt_float32_to_float16(curves[ci].p0y);
                s_curve_upload[t0 + 2] = nt_float32_to_float16(curves[ci].p1x);
                s_curve_upload[t0 + 3] = nt_float32_to_float16(curves[ci].p1y);
                s_curve_upload[t1 + 0] = nt_float32_to_float16(curves[ci].p2x);
                s_curve_upload[t1 + 1] = nt_float32_to_float16(curves[ci].p2y);
                s_curve_upload[t1 + 2] = 0;
                s_curve_upload[t1 + 3] = 0;
                local_pos += 2;
            }
        }
    }
    // #endregion

    // #region Upload curve data to GPU from temp buffer
    if (needed_texels > 0) {
        uint32_t remaining = needed_texels;
        uint32_t src_texel = 0;
        uint32_t dst_texel = curve_offset_y;
        while (remaining > 0) {
            uint16_t row = (uint16_t)(dst_texel / slot->curve_tex_width);
            uint16_t col = (uint16_t)(dst_texel % slot->curve_tex_width);
            uint16_t w = (uint16_t)(slot->curve_tex_width - col);
            if (w > remaining) {
                w = (uint16_t)remaining;
            }
            nt_gfx_update_texture(slot->curve_texture, col, row, w, 1, &s_curve_upload[(size_t)src_texel * 4]);
            remaining -= w;
            src_texel += w;
            dst_texel += w;
        }
    }
    // #endregion

    // #region Upload band data to GPU (Y-bands + X-bands in one row)
    uint16_t band_upload[64 * 2] = {0}; /* 32 Y-bands + 32 X-bands, RG16UI each */
    for (uint8_t b = 0; b < slot->band_count; b++) {
        band_upload[(b * 2) + 0] = yband_offsets[b];
        band_upload[(b * 2) + 1] = yband_counts[b];
    }
    uint8_t xoff = slot->band_count;
    for (uint8_t b = 0; b < slot->band_count; b++) {
        band_upload[((xoff + b) * 2) + 0] = xband_offsets[b];
        band_upload[((xoff + b) * 2) + 1] = xband_counts[b];
    }
    nt_gfx_update_texture(slot->band_texture, 0, cache_idx, (uint16_t)(slot->band_count * 2), 1, band_upload);
    // #endregion

    // #region Fill cache entry
    nt_font_cache_slot_t *cs = &slot->cache[cache_idx];
    cs->entry.codepoint = glyph->codepoint;
    cs->entry.curve_offset = curve_offset_y;
    cs->entry.curve_offset_x = curve_offset_x;
    cs->entry.curve_count = (uint16_t)(y_total + x_total);
    cs->entry.band_row = cache_idx;
    cs->entry.advance = glyph->advance;
    cs->entry.bbox_x0 = glyph->bbox_x0;
    cs->entry.bbox_y0 = glyph->bbox_y0;
    cs->entry.bbox_x1 = glyph->bbox_x1;
    cs->entry.bbox_y1 = glyph->bbox_y1;
    cs->entry.is_tofu = false;
    cs->lru_frame = s_font.frame_counter;
    cs->resource_index = resource_index;
    // #endregion

    slot->curve_write_head = curve_offset_y + local_pos;
    slot->glyphs_cached++;
    return cache_idx;
}

/* ---- Lifecycle ---- */

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
nt_result_t nt_font_init(const nt_font_desc_t *desc) {
    NT_ASSERT(!s_font.initialized);
    NT_ASSERT(desc);
    NT_ASSERT(desc->max_fonts > 0);
    if (s_font.initialized || !desc || desc->max_fonts == 0) {
        return NT_ERR_INIT_FAILED;
    }

    nt_pool_init(&s_font.pool, desc->max_fonts);

    s_font.slots = (nt_font_slot_t *)calloc((size_t)desc->max_fonts + 1, sizeof(nt_font_slot_t));
    NT_ASSERT(s_font.slots);

    /* Font data side table: max_fonts * max_resources_per_font */
    s_font.data_capacity = (uint32_t)desc->max_fonts * NT_FONT_MAX_SOURCES_PER_FONT;
    s_font.data_entries = calloc(s_font.data_capacity, sizeof(nt_font_data_entry_t));
    NT_ASSERT(s_font.data_entries);
    s_font.data_count = 0;

    /* Register font activator for NT_ASSET_FONT resources */
    nt_resource_set_activator(NT_ASSET_FONT, activate_font, deactivate_font);

    s_font.initialized = true;
    return NT_OK;
}

void nt_font_shutdown(void) {
    if (!s_font.initialized) {
        return;
    }
    // #region Per-slot cleanup
    for (uint32_t i = 1; i <= s_font.pool.capacity; i++) {
        if (!nt_pool_slot_alive(&s_font.pool, i)) {
            continue;
        }
        nt_font_slot_t *slot = &s_font.slots[i];
        free(slot->cache);
        free(slot->free_stack);
        free(slot->hash_table);
        nt_gfx_destroy_texture(slot->curve_texture);
        nt_gfx_destroy_texture(slot->band_texture);
    }
    // #endregion
    free(s_font.slots);
    free(s_font.data_entries);
    nt_pool_shutdown(&s_font.pool);
    memset(&s_font, 0, sizeof(s_font));
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void nt_font_step(void) {
    NT_ASSERT(s_font.initialized);
    if (!s_font.initialized) {
        return;
    }

    // #region Context restore: re-create GPU textures
    if (g_nt_gfx.context_restored) {
        for (uint32_t i = 1; i <= s_font.pool.capacity; i++) {
            if (!nt_pool_slot_alive(&s_font.pool, i)) {
                continue;
            }
            nt_font_slot_t *slot = &s_font.slots[i];

            nt_gfx_destroy_texture(slot->curve_texture);
            nt_gfx_destroy_texture(slot->band_texture);

            slot->curve_texture = nt_gfx_make_texture(&(nt_texture_desc_t){
                .width = slot->curve_tex_width,
                .height = slot->curve_tex_height,
                .format = NT_PIXEL_RGBA16F,
                .min_filter = NT_FILTER_NEAREST,
                .mag_filter = NT_FILTER_NEAREST,
                .wrap_u = NT_WRAP_CLAMP_TO_EDGE,
                .wrap_v = NT_WRAP_CLAMP_TO_EDGE,
                .label = "font_curve",
            });
            slot->band_texture = nt_gfx_make_texture(&(nt_texture_desc_t){
                .width = (uint16_t)(slot->band_count * 2),
                .height = slot->band_tex_height,
                .format = NT_PIXEL_RG16UI,
                .min_filter = NT_FILTER_NEAREST,
                .mag_filter = NT_FILTER_NEAREST,
                .wrap_u = NT_WRAP_CLAMP_TO_EDGE,
                .wrap_v = NT_WRAP_CLAMP_TO_EDGE,
                .label = "font_band",
            });

            flush_cache(slot);
        }
    }
    // #endregion

    s_font.frame_counter++;

    for (uint32_t i = 1; i <= s_font.pool.capacity; i++) {
        if (!nt_pool_slot_alive(&s_font.pool, i)) {
            continue;
        }

        nt_font_slot_t *slot = &s_font.slots[i];

        // #region Resolve resources
        for (uint8_t ri = 0; ri < slot->resource_count; ri++) {
            uint32_t ver = nt_resource_get(slot->resources[ri]);
            if (ver == 0) {
                continue; /* resource not ready yet */
            }
            if (ver == slot->resource_handles[ri]) {
                continue; /* no change */
            }

            /* Resource changed -- parse font header */
            uint32_t blob_size = 0;
            const uint8_t *blob = get_font_data(ver, &blob_size);
            if (!blob || blob_size < sizeof(NtFontAssetHeader)) {
                NT_LOG_WARN("font resource %u: activation returned invalid data (blob=%p, size=%u)", (unsigned)ri, (const void *)blob, (unsigned)blob_size);
                continue;
            }

            const NtFontAssetHeader *hdr = (const NtFontAssetHeader *)blob;
            NT_ASSERT(hdr->magic == NT_FONT_MAGIC);
            NT_ASSERT(hdr->version == NT_FONT_VERSION);

            // #region Metrics validation (D-19)
            if (!slot->metrics_set) {
                slot->metrics.ascent = hdr->ascent;
                slot->metrics.descent = hdr->descent;
                slot->metrics.line_gap = hdr->line_gap;
                slot->metrics.units_per_em = hdr->units_per_em;
                slot->metrics.line_height = (int16_t)(hdr->ascent - hdr->descent + hdr->line_gap);
                slot->metrics_set = true;
            } else {
                /* All resources on one nt_font_t must share identical metrics.
                 * If combining Latin + CJK, normalize in the builder. */
                NT_ASSERT(slot->metrics.units_per_em == hdr->units_per_em);
                NT_ASSERT(slot->metrics.ascent == hdr->ascent);
                NT_ASSERT(slot->metrics.descent == hdr->descent);
                NT_ASSERT(slot->metrics.line_gap == hdr->line_gap);
            }
            // #endregion

            /* If version changed (reload), invalidate cache + hash table */
            if (slot->resource_handles[ri] != 0) {
                flush_cache(slot);
            }

            slot->resource_handles[ri] = ver;
        }
        // #endregion
    }
}

/* ---- Create / Destroy / Valid ---- */

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
nt_font_t nt_font_create(const nt_font_create_desc_t *desc) {
    NT_ASSERT(s_font.initialized);
    NT_ASSERT(desc);
    if (!s_font.initialized || !desc) {
        return NT_FONT_INVALID;
    }

    NT_ASSERT(desc->curve_texture_width > 0);
    NT_ASSERT(desc->curve_texture_height > 0);
    NT_ASSERT(desc->band_texture_height > 0);

    NT_ASSERT(desc->band_count > 0 && desc->band_count <= 32);
    uint8_t band_count = desc->band_count;

    uint32_t id = nt_pool_alloc(&s_font.pool);
    if (id == 0) {
        NT_LOG_ERROR("font pool full -- increase max_fonts");
        return NT_FONT_INVALID;
    }

    uint32_t slot_index = nt_pool_slot_index(id);
    nt_font_slot_t *slot = &s_font.slots[slot_index];
    memset(slot, 0, sizeof(*slot));

    // #region Store config
    slot->curve_tex_width = desc->curve_texture_width;
    slot->curve_tex_height = desc->curve_texture_height;
    slot->band_tex_height = desc->band_texture_height;
    slot->band_count = band_count;
    slot->max_glyphs = desc->band_texture_height; /* D-08 */
    // #endregion

    // #region Create GPU textures (D-11: once, never resized)
    slot->curve_texture = nt_gfx_make_texture(&(nt_texture_desc_t){
        .width = desc->curve_texture_width,
        .height = desc->curve_texture_height,
        .format = NT_PIXEL_RGBA16F,
        .min_filter = NT_FILTER_NEAREST,
        .mag_filter = NT_FILTER_NEAREST,
        .wrap_u = NT_WRAP_CLAMP_TO_EDGE,
        .wrap_v = NT_WRAP_CLAMP_TO_EDGE,
        .data = NULL,
        .label = "font_curve",
    });

    slot->band_texture = nt_gfx_make_texture(&(nt_texture_desc_t){
        .width = (uint16_t)(band_count * 2), /* Y-bands + X-bands */
        .height = desc->band_texture_height,
        .format = NT_PIXEL_RG16UI,
        .min_filter = NT_FILTER_NEAREST,
        .mag_filter = NT_FILTER_NEAREST,
        .wrap_u = NT_WRAP_CLAMP_TO_EDGE,
        .wrap_v = NT_WRAP_CLAMP_TO_EDGE,
        .data = NULL,
        .label = "font_band",
    });
    // #endregion

    // #region Allocate cache, free stack, hash table
    slot->cache = (nt_font_cache_slot_t *)calloc(desc->band_texture_height, sizeof(nt_font_cache_slot_t));
    NT_ASSERT(slot->cache);

    slot->free_stack = (uint16_t *)calloc(desc->band_texture_height, sizeof(uint16_t));
    NT_ASSERT(slot->free_stack);
    free_stack_reset(slot);

    /* Codepoint hash table: POT size, load factor ≤ 0.5 */
    slot->hash_table_size = next_pot16((uint16_t)(desc->band_texture_height * 2));
    slot->hash_table = (uint16_t *)calloc(slot->hash_table_size, sizeof(uint16_t));
    NT_ASSERT(slot->hash_table);
    // #endregion

    return (nt_font_t){id};
}

void nt_font_destroy(nt_font_t font) {
    NT_ASSERT(s_font.initialized);
    if (!s_font.initialized || !nt_pool_valid(&s_font.pool, font.id)) {
        return;
    }

    nt_font_slot_t *slot = get_slot(font);
    free(slot->cache);
    free(slot->free_stack);
    free(slot->hash_table);
    nt_gfx_destroy_texture(slot->curve_texture);
    nt_gfx_destroy_texture(slot->band_texture);
    memset(slot, 0, sizeof(*slot));
    nt_pool_free(&s_font.pool, font.id);
}

bool nt_font_valid(nt_font_t font) {
    if (!s_font.initialized) {
        return false;
    }
    return nt_pool_valid(&s_font.pool, font.id);
}

/* ---- Resource management ---- */

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void nt_font_add(nt_font_t font, nt_resource_t resource) {
    NT_ASSERT(s_font.initialized);
    NT_ASSERT(nt_pool_valid(&s_font.pool, font.id));

    nt_font_slot_t *slot = get_slot(font);
    NT_ASSERT(slot->resource_count < NT_FONT_MAX_SOURCES_PER_FONT);
    for (uint8_t i = 0; i < slot->resource_count; i++) {
        NT_ASSERT(slot->resources[i].id != resource.id); /* duplicate resource */
    }

    slot->resources[slot->resource_count] = resource;
    slot->resource_handles[slot->resource_count] = 0; /* resolved in step */
    slot->resource_count++;
}

/* ---- Query ---- */

nt_font_metrics_t nt_font_get_metrics(nt_font_t font) {
    NT_ASSERT(s_font.initialized);
    NT_ASSERT(nt_pool_valid(&s_font.pool, font.id));
    if (!s_font.initialized || !nt_pool_valid(&s_font.pool, font.id)) {
        return (nt_font_metrics_t){0};
    }
    nt_font_slot_t *slot = get_slot(font);
    NT_ASSERT(slot->metrics_set);
    return slot->metrics;
}

nt_font_stats_t nt_font_get_stats(nt_font_t font) {
    NT_ASSERT(s_font.initialized);
    NT_ASSERT(nt_pool_valid(&s_font.pool, font.id));
    if (!s_font.initialized || !nt_pool_valid(&s_font.pool, font.id)) {
        return (nt_font_stats_t){0};
    }
    nt_font_slot_t *slot = get_slot(font);
    return (nt_font_stats_t){
        .glyphs_cached = slot->glyphs_cached,
        .max_glyphs = slot->max_glyphs,
        .curve_texels_used = slot->curve_write_head,
        .curve_texels_total = (uint32_t)slot->curve_tex_width * slot->curve_tex_height,
        .band_texels_used = (uint32_t)slot->glyphs_cached * slot->band_count,
        .band_texels_total = (uint32_t)slot->band_count * slot->band_tex_height,
    };
}

/* ---- Glyph lookup (D-05, D-13, D-14, D-15) ---- */

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
const nt_glyph_cache_entry_t *nt_font_lookup_glyph(nt_font_t font, uint32_t codepoint) {
    NT_ASSERT(s_font.initialized);
    NT_ASSERT(nt_pool_valid(&s_font.pool, font.id));

    nt_font_slot_t *slot = get_slot(font);

    // #region Cache hit check (hash table)
    nt_font_cache_slot_t *hit = hash_lookup(slot, codepoint);
    if (hit) {
        hit->lru_frame = s_font.frame_counter;
        return &hit->entry;
    }
    // #endregion

    // #region Cache miss -- find glyph in resources
    uint8_t res_idx = 0;
    const NtFontGlyphEntry *glyph_entry = NULL;
    bool found = find_glyph_in_resources(slot, codepoint, &res_idx, &glyph_entry);

    if (!found) {
        generate_tofu(slot);
        nt_font_cache_slot_t *tofu = hash_lookup(slot, 0xFFFFFFFFU);
        return tofu ? &tofu->entry : NULL;
    }
    // #endregion

    // #region Decode contour data
    uint32_t blob_size = 0;
    const uint8_t *blob = get_font_data(slot->resource_handles[res_idx], &blob_size);
    NT_ASSERT(blob);

    /* Contour data is at: data_offset + kern_count * sizeof(NtFontKernEntry) */
    uint32_t contour_offset = glyph_entry->data_offset + ((uint32_t)glyph_entry->kern_count * (uint32_t)sizeof(NtFontKernEntry));
    const uint8_t *contour_data = blob + contour_offset;

    NT_ASSERT(glyph_entry->curve_count <= NT_FONT_MAX_CURVES_PER_GLYPH);
    uint16_t curve_count = 0;
    if (glyph_entry->curve_count > 0) {
        curve_count = decode_contours(contour_data, s_decode_curves, NT_FONT_MAX_CURVES_PER_GLYPH);
    }
    // #endregion

    // #region Upload, allocate slot, fill cache
    uint16_t cache_idx = upload_glyph(slot, glyph_entry, s_decode_curves, curve_count, res_idx);
    hash_insert(slot, codepoint, cache_idx);
    // #endregion

    return &slot->cache[cache_idx].entry;
}

/* ---- GPU texture access ---- */

nt_texture_t nt_font_get_curve_texture(nt_font_t font) {
    NT_ASSERT(s_font.initialized);
    NT_ASSERT(nt_pool_valid(&s_font.pool, font.id));
    if (!s_font.initialized || !nt_pool_valid(&s_font.pool, font.id)) {
        return (nt_texture_t){0};
    }
    return get_slot(font)->curve_texture;
}

nt_texture_t nt_font_get_band_texture(nt_font_t font) {
    NT_ASSERT(s_font.initialized);
    NT_ASSERT(nt_pool_valid(&s_font.pool, font.id));
    if (!s_font.initialized || !nt_pool_valid(&s_font.pool, font.id)) {
        return (nt_texture_t){0};
    }
    return get_slot(font)->band_texture;
}

uint8_t nt_font_get_band_count(nt_font_t font) {
    NT_ASSERT(s_font.initialized);
    NT_ASSERT(nt_pool_valid(&s_font.pool, font.id));
    if (!s_font.initialized || !nt_pool_valid(&s_font.pool, font.id)) {
        return 0;
    }
    return get_slot(font)->band_count;
}

uint16_t nt_font_get_curve_texture_width(nt_font_t font) {
    NT_ASSERT(s_font.initialized);
    NT_ASSERT(nt_pool_valid(&s_font.pool, font.id));
    if (!s_font.initialized || !nt_pool_valid(&s_font.pool, font.id)) {
        return 0;
    }
    return get_slot(font)->curve_tex_width;
}

/* ---- Cache generation ---- */

uint32_t nt_font_get_cache_generation(nt_font_t font) {
    NT_ASSERT(s_font.initialized);
    NT_ASSERT(nt_pool_valid(&s_font.pool, font.id));
    if (!s_font.initialized || !nt_pool_valid(&s_font.pool, font.id)) {
        return 0;
    }
    return get_slot(font)->cache_generation;
}

/* ---- Kern pair lookup ---- */

int16_t nt_font_get_kern(nt_font_t font, uint32_t left_codepoint, uint32_t right_codepoint) {
    NT_ASSERT(s_font.initialized);
    NT_ASSERT(nt_pool_valid(&s_font.pool, font.id));

    nt_font_slot_t *slot = get_slot(font);

    /* Find left glyph in resources to access its kern entries */
    for (uint8_t ri = 0; ri < slot->resource_count; ri++) {
        if (slot->resource_handles[ri] == 0) {
            continue;
        }
        uint32_t blob_size = 0;
        const uint8_t *blob = get_font_data(slot->resource_handles[ri], &blob_size);
        if (!blob) {
            continue;
        }

        const NtFontGlyphEntry *left_entry = find_glyph_in_pack(blob, blob_size, left_codepoint);
        if (!left_entry || left_entry->kern_count == 0) {
            continue;
        }

        /* Find right glyph index in this resource */
        int32_t right_idx = find_glyph_index(blob, blob_size, right_codepoint);
        if (right_idx < 0) {
            continue;
        }

        /* Kern entries at data_offset */
        const NtFontKernEntry *kerns = (const NtFontKernEntry *)(blob + left_entry->data_offset);
        uint16_t right_glyph_index = (uint16_t)right_idx;

        const NtFontKernEntry *found = (const NtFontKernEntry *)bsearch(&right_glyph_index, kerns, left_entry->kern_count, sizeof(NtFontKernEntry), compare_kern_right);
        if (found) {
            return found->value;
        }
    }
    return 0;
}

// #region Metrics-only lookup (pure CPU, no GPU, no cache)
nt_glyph_metrics_t nt_font_lookup_metrics(nt_font_t font, uint32_t codepoint) {
    NT_ASSERT(s_font.initialized);
    NT_ASSERT(nt_pool_valid(&s_font.pool, font.id));

    nt_font_slot_t *slot = get_slot(font);

    /* Search glyph entry across all loaded resources */
    uint8_t res_idx = 0;
    const NtFontGlyphEntry *entry = NULL;
    bool found = find_glyph_in_resources(slot, codepoint, &res_idx, &entry);

    if (found) {
        return (nt_glyph_metrics_t){
            .advance = entry->advance,
            .bbox_x0 = entry->bbox_x0,
            .bbox_y0 = entry->bbox_y0,
            .bbox_x1 = entry->bbox_x1,
            .bbox_y1 = entry->bbox_y1,
            .found = true,
        };
    }

    /* Tofu fallback — same dimensions as generate_tofu() */
    int16_t tofu_w = (int16_t)(slot->metrics.units_per_em / 2);
    return (nt_glyph_metrics_t){
        .advance = tofu_w,
        .bbox_x0 = 0,
        .bbox_y0 = slot->metrics.descent,
        .bbox_x1 = tofu_w,
        .bbox_y1 = slot->metrics.ascent,
        .found = false,
    };
}
// #endregion

// #region Measurement (pure CPU, no GPU calls)
nt_text_size_t nt_font_measure(nt_font_t font, const char *utf8, float size) {
    nt_text_size_t result = {0.0F, 0.0F};
    if (!utf8 || !*utf8) {
        return result;
    }

    nt_font_metrics_t metrics = nt_font_get_metrics(font);
    if (metrics.units_per_em == 0) {
        return result;
    }
    float scale = size / (float)metrics.units_per_em;

    uint32_t state = NT_UTF8_ACCEPT;
    uint32_t codepoint = 0;
    uint32_t prev_cp = 0;
    float pen_x = 0.0F;
    float min_y = 0.0F;
    float max_y = 0.0F;

    for (const uint8_t *p = (const uint8_t *)utf8; *p; p++) {
        if (nt_utf8_decode(&state, &codepoint, *p) != NT_UTF8_ACCEPT) {
            continue;
        }

        if (prev_cp != 0) {
            int16_t kern = nt_font_get_kern(font, prev_cp, codepoint);
            pen_x += (float)kern * scale;
        }

        nt_glyph_metrics_t g = nt_font_lookup_metrics(font, codepoint);
        if ((float)g.bbox_y0 * scale < min_y) {
            min_y = (float)g.bbox_y0 * scale;
        }
        if ((float)g.bbox_y1 * scale > max_y) {
            max_y = (float)g.bbox_y1 * scale;
        }
        pen_x += (float)g.advance * scale;
        prev_cp = codepoint;
    }

    result.width = pen_x;
    result.height = max_y - min_y;
    if (result.height < size) {
        result.height = size; /* Minimum height = requested size */
    }
    return result;
}
// #endregion

/* ---- Test-only: register font data for headless testing ---- */

#ifdef NT_FONT_TEST_ACCESS
uint32_t nt_font_test_register_data(const uint8_t *data, uint32_t size) { return activate_font(data, size); }
#endif
