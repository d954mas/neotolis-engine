#include "font/nt_font.h"
#include "font/nt_font_internal.h"

#include <stdlib.h>
#include <string.h>

#include "core/nt_assert.h"
#include "graphics/nt_gfx.h"
#include "log/nt_log.h"
#include "nt_font_format.h"
#include "nt_pack_format.h"
#include "pool/nt_pool.h"
#include "resource/nt_resource.h"

/* ---- Float16 conversion (runtime port from builder, per D-02) ---- */

static uint16_t nt_float32_to_float16(float value) {
    union {
        float f;
        uint32_t u;
    } conv;
    conv.f = value;
    uint32_t sign = (conv.u >> 16) & 0x8000U;
    int32_t exponent = (int32_t)((conv.u >> 23) & 0xFFU) - 127 + 15;
    uint32_t mantissa = conv.u & 0x007FFFFFU;
    if (exponent <= 0) {
        return (uint16_t)sign;
    }
    if (exponent >= 31) {
        return (uint16_t)(sign | 0x7C00U);
    }
    return (uint16_t)(sign | ((uint32_t)exponent << 10) | (mantissa >> 13));
}

/* ---- Bitmask byte size (matches builder's BITMASK_BYTES) ---- */

#define BITMASK_BYTES(n) ((((uint32_t)(n) + 15U) / 8U) & ~1U)

/* ---- Font data storage (side table for pack blobs accessed via activator) ---- */

#define NT_FONT_MAX_DATA_ENTRIES 64

typedef struct {
    const uint8_t *data;
    uint32_t size;
} nt_font_data_entry_t;

static struct {
    nt_font_data_entry_t entries[NT_FONT_MAX_DATA_ENTRIES];
    uint32_t count;
} s_font_data;

/* ---- Module state ---- */

static nt_font_state_t s_font;

/* ---- Font activator callbacks ---- */

static uint32_t activate_font(const uint8_t *data, uint32_t size) {
    /* Store pointer to pack data for later access by font module.
     * The data pointer is valid as long as the pack remains mounted. */
    NT_ASSERT(s_font_data.count < NT_FONT_MAX_DATA_ENTRIES);
    uint32_t idx = s_font_data.count++;
    s_font_data.entries[idx].data = data;
    s_font_data.entries[idx].size = size;
    return idx + 1; /* 1-based handle (0 = failure in resource system) */
}

static void deactivate_font(uint32_t runtime_handle) {
    if (runtime_handle == 0 || runtime_handle > s_font_data.count) {
        return;
    }
    uint32_t idx = runtime_handle - 1;
    s_font_data.entries[idx].data = NULL;
    s_font_data.entries[idx].size = 0;
}

/* Get font data from activation handle */
static const uint8_t *get_font_data(uint32_t runtime_handle, uint32_t *out_size) {
    if (runtime_handle == 0 || runtime_handle > s_font_data.count) {
        if (out_size) {
            *out_size = 0;
        }
        return NULL;
    }
    uint32_t idx = runtime_handle - 1;
    if (out_size) {
        *out_size = s_font_data.entries[idx].size;
    }
    return s_font_data.entries[idx].data;
}

/* ---- Internal helpers ---- */

static nt_font_slot_t *get_slot(nt_font_t font) {
    uint32_t idx = nt_pool_slot_index(font.id);
    return &s_font.slots[idx];
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
        if (slot->resource_versions[i] == 0) {
            continue; /* not loaded yet */
        }
        uint32_t blob_size = 0;
        const uint8_t *blob = get_font_data(slot->resource_versions[i], &blob_size);
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
    uint32_t min_frame = UINT32_MAX;
    uint16_t victim = 0;
    for (uint16_t i = 0; i < slot->max_glyphs; i++) {
        nt_font_cache_slot_t *cs = &slot->cache[i];
        if (cs->entry.codepoint == 0) {
            continue; /* empty slot */
        }
        if (cs->entry.is_tofu) {
            continue; /* never evict tofu (D-23) */
        }
        if (cs->lru_frame < min_frame) {
            min_frame = cs->lru_frame;
            victim = i;
        }
    }
    NT_ASSERT(min_frame != UINT32_MAX); /* no evictable entry found */

    /* Clear victim */
    memset(&slot->cache[victim], 0, sizeof(nt_font_cache_slot_t));
    slot->glyphs_cached--;
    return victim;
}

/* ---- Curve texture compaction (D-12) ---- */

static void compact_curve_texture(nt_font_slot_t *slot) {
    /* Collect active entries sorted by curve_offset, repack to left */
    uint32_t write_pos = 0;
    uint32_t tex_width = slot->curve_tex_width;

    /* Temporary staging for reorder -- work in-place by scanning left to right */
    for (uint16_t pass = 0; pass < slot->max_glyphs; pass++) {
        /* Find entry with smallest curve_offset >= write_pos */
        uint32_t best_offset = UINT32_MAX;
        uint16_t best_idx = 0;
        for (uint16_t i = 0; i < slot->max_glyphs; i++) {
            nt_font_cache_slot_t *cs = &slot->cache[i];
            if (cs->entry.codepoint == 0 || cs->entry.curve_count == 0) {
                continue;
            }
            if (cs->entry.curve_offset >= write_pos && cs->entry.curve_offset < best_offset) {
                best_offset = cs->entry.curve_offset;
                best_idx = i;
            }
        }
        if (best_offset == UINT32_MAX) {
            break; /* no more active entries */
        }

        nt_font_cache_slot_t *cs = &slot->cache[best_idx];
        uint32_t texels_used = (uint32_t)cs->entry.curve_count * 2;

        if (best_offset != write_pos) {
            /* Move curve data in staging buffer */
            memmove(&slot->curve_staging[(size_t)write_pos * 4], &slot->curve_staging[(size_t)best_offset * 4], (size_t)texels_used * 4 * sizeof(uint16_t));
            cs->entry.curve_offset = write_pos;
        }
        write_pos += texels_used;
    }
    slot->curve_write_head = write_pos;

    /* Upload compacted region to GPU */
    if (write_pos > 0) {
        uint16_t rows = (uint16_t)((write_pos + tex_width - 1) / tex_width);
        nt_gfx_update_texture(slot->curve_texture, 0, 0, slot->curve_tex_width, rows, slot->curve_staging);
    }
}

/* Ensure enough texels for new curve data, evicting and compacting as needed */
static void ensure_curve_space(nt_font_slot_t *slot, uint32_t needed_texels) {
    uint32_t total = (uint32_t)slot->curve_tex_width * slot->curve_tex_height;
    while (slot->curve_write_head + needed_texels > total) {
        evict_lru(slot);
        compact_curve_texture(slot);
    }
}

/* ---- Tofu generation (D-22, D-23) ---- */

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void generate_tofu(nt_font_slot_t *slot) {
    if (slot->tofu_generated) {
        return;
    }

    /* Tofu dimensions from metrics */
    int16_t tofu_w = (int16_t)(slot->metrics.units_per_em / 2);
    int16_t y0 = slot->metrics.descent;
    int16_t y1 = slot->metrics.ascent;

    /* 4 line segments forming a rectangle: bottom, right, top, left
     * Each line promoted to degenerate quadratic: p1 = midpoint(p0, p2)
     * 4 curves x 2 texels = 8 texels needed */
    uint32_t needed_texels = 4 * 2;

    /* Ensure we have cache slot space */
    uint16_t cache_idx;
    if (slot->glyphs_cached < slot->max_glyphs) {
        /* Find empty slot */
        cache_idx = 0;
        for (uint16_t i = 0; i < slot->max_glyphs; i++) {
            if (slot->cache[i].entry.codepoint == 0) {
                cache_idx = i;
                break;
            }
        }
    } else {
        cache_idx = evict_lru(slot);
    }

    ensure_curve_space(slot, needed_texels);

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
    uint16_t *staging = slot->curve_staging;

    for (int seg = 0; seg < 4; seg++) {
        float p0x = lines[seg][0];
        float p0y = lines[seg][1];
        float p2x = lines[seg][2];
        float p2y = lines[seg][3];
        /* Degenerate quadratic: p1 = midpoint(p0, p2) */
        float p1x = (p0x + p2x) * 0.5F;
        float p1y = (p0y + p2y) * 0.5F;

        uint32_t texel0 = (curve_offset + (uint32_t)seg * 2) * 4;
        uint32_t texel1 = texel0 + 4;
        staging[texel0 + 0] = nt_float32_to_float16(p0x);
        staging[texel0 + 1] = nt_float32_to_float16(p0y);
        staging[texel0 + 2] = nt_float32_to_float16(p1x);
        staging[texel0 + 3] = nt_float32_to_float16(p1y);
        staging[texel1 + 0] = nt_float32_to_float16(p2x);
        staging[texel1 + 1] = nt_float32_to_float16(p2y);
        staging[texel1 + 2] = 0;
        staging[texel1 + 3] = 0;
    }

    /* Upload curve data for tofu */
    uint32_t upload_texels = needed_texels;
    uint16_t upload_row = (uint16_t)(curve_offset / slot->curve_tex_width);
    uint16_t upload_x = (uint16_t)(curve_offset % slot->curve_tex_width);
    if (upload_x + upload_texels <= slot->curve_tex_width) {
        nt_gfx_update_texture(slot->curve_texture, upload_x, upload_row, (uint16_t)upload_texels, 1, &staging[(size_t)curve_offset * 4]);
    } else {
        /* Spans multiple rows -- upload row by row */
        uint32_t remaining = upload_texels;
        uint32_t src_offset = curve_offset;
        while (remaining > 0) {
            uint16_t row = (uint16_t)(src_offset / slot->curve_tex_width);
            uint16_t col = (uint16_t)(src_offset % slot->curve_tex_width);
            uint16_t w = (uint16_t)(slot->curve_tex_width - col);
            if (w > remaining) {
                w = (uint16_t)remaining;
            }
            nt_gfx_update_texture(slot->curve_texture, col, row, w, 1, &staging[(size_t)src_offset * 4]);
            remaining -= w;
            src_offset += w;
        }
    }

    /* Upload band data for tofu -- all 4 curves are in each band (simple rectangle) */
    uint16_t *band_data = (uint16_t *)calloc((size_t)slot->band_count * 2, sizeof(uint16_t));
    NT_ASSERT(band_data);
    for (uint8_t b = 0; b < slot->band_count; b++) {
        band_data[(b * 2) + 0] = (uint16_t)curve_offset; /* curve_start */
        band_data[(b * 2) + 1] = 4;                      /* curve_count (all 4 segments) */
    }
    nt_gfx_update_texture(slot->band_texture, 0, cache_idx, slot->band_count, 1, band_data);
    free(band_data);

    /* Fill cache entry */
    nt_font_cache_slot_t *cs = &slot->cache[cache_idx];
    cs->entry.codepoint = 0xFFFFFFFFU; /* sentinel (D-22) */
    cs->entry.curve_offset = curve_offset;
    cs->entry.curve_count = 4;
    cs->entry.band_row = cache_idx;
    cs->entry.advance = tofu_w;
    cs->entry.bbox_x0 = 0;
    cs->entry.bbox_y0 = y0;
    cs->entry.bbox_x1 = tofu_w;
    cs->entry.bbox_y1 = y1;
    cs->entry.is_tofu = true;
    cs->lru_frame = slot->frame_counter;

    slot->curve_write_head += needed_texels;
    slot->glyphs_cached++;
    slot->tofu_generated = true;
}

/* ---- Decode contours and upload glyph to GPU ---- */

/* Temporary curve storage for band decomposition */
typedef struct {
    float p0x, p0y, p1x, p1y, p2x, p2y;
} nt_curve_t;

#define NT_FONT_MAX_CURVES_PER_GLYPH 256

/* Decode delta-encoded contour data into absolute float curves */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static uint16_t decode_contours(const uint8_t *contour_data, nt_curve_t *curves, uint16_t max_curves) {
    const uint8_t *rp = contour_data;
    uint16_t contour_count;
    memcpy(&contour_count, rp, 2);
    rp += 2;

    uint16_t total_curves = 0;

    for (uint16_t ci = 0; ci < contour_count; ci++) {
        uint16_t seg_count;
        memcpy(&seg_count, rp, 2);
        rp += 2;

        int16_t start_x;
        int16_t start_y;
        memcpy(&start_x, rp, 2);
        rp += 2;
        memcpy(&start_y, rp, 2);
        rp += 2;

        uint32_t bitmask_bytes = BITMASK_BYTES(seg_count);
        const uint8_t *bitmask = rp;
        rp += bitmask_bytes;

        int32_t prev_x = start_x;
        int32_t prev_y = start_y;

        for (uint16_t s = 0; s < seg_count; s++) {
            bool is_quad = (bitmask[s / 8] & (1U << (s % 8))) != 0;

            float fp0x = (float)prev_x;
            float fp0y = (float)prev_y;

            if (is_quad) {
                // #region Quadratic curve
                int16_t dp1x;
                int16_t dp1y;
                int16_t dp2x;
                int16_t dp2y;
                memcpy(&dp1x, rp, 2);
                rp += 2;
                memcpy(&dp1y, rp, 2);
                rp += 2;
                memcpy(&dp2x, rp, 2);
                rp += 2;
                memcpy(&dp2y, rp, 2);
                rp += 2;

                float fp1x = (float)(prev_x + dp1x);
                float fp1y = (float)(prev_y + dp1y);
                float fp2x = (float)(prev_x + dp2x);
                float fp2y = (float)(prev_y + dp2y);

                if (total_curves < max_curves) {
                    curves[total_curves] = (nt_curve_t){fp0x, fp0y, fp1x, fp1y, fp2x, fp2y};
                    total_curves++;
                }
                prev_x = prev_x + dp2x;
                prev_y = prev_y + dp2y;
                // #endregion
            } else {
                // #region Line (promote to degenerate quadratic)
                int16_t dp2x;
                int16_t dp2y;
                memcpy(&dp2x, rp, 2);
                rp += 2;
                memcpy(&dp2y, rp, 2);
                rp += 2;

                float fp2x = (float)(prev_x + dp2x);
                float fp2y = (float)(prev_y + dp2y);
                /* Degenerate quadratic: p1 = midpoint(p0, p2) */
                float fp1x = (fp0x + fp2x) * 0.5F;
                float fp1y = (fp0y + fp2y) * 0.5F;

                if (total_curves < max_curves) {
                    curves[total_curves] = (nt_curve_t){fp0x, fp0y, fp1x, fp1y, fp2x, fp2y};
                    total_curves++;
                }
                prev_x = prev_x + dp2x;
                prev_y = prev_y + dp2y;
                // #endregion
            }
        }
    }
    return total_curves;
}

/* Upload a glyph to GPU textures and fill cache entry */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void upload_glyph(nt_font_slot_t *slot, uint16_t cache_idx, const NtFontGlyphEntry *glyph, const nt_curve_t *curves, uint16_t curve_count, uint8_t resource_index) {
    // #region Band decomposition (D-05, D-03)
    /* Divide glyph bbox into band_count horizontal bands.
     * Assign each curve to all overlapping bands. */
    float bbox_y0 = (float)glyph->bbox_y0;
    float bbox_y1 = (float)glyph->bbox_y1;
    float band_height = (bbox_y1 - bbox_y0) / (float)slot->band_count;

    /* Per-band curve lists */
    uint16_t band_curve_counts[32] = {0};
    uint16_t band_curve_indices[32][NT_FONT_MAX_CURVES_PER_GLYPH];
    NT_ASSERT(slot->band_count <= 32);

    for (uint16_t ci = 0; ci < curve_count; ci++) {
        /* Compute Y extent of curve */
        float cy_min = curves[ci].p0y;
        if (curves[ci].p1y < cy_min) {
            cy_min = curves[ci].p1y;
        }
        if (curves[ci].p2y < cy_min) {
            cy_min = curves[ci].p2y;
        }
        float cy_max = curves[ci].p0y;
        if (curves[ci].p1y > cy_max) {
            cy_max = curves[ci].p1y;
        }
        if (curves[ci].p2y > cy_max) {
            cy_max = curves[ci].p2y;
        }

        /* Assign to overlapping bands */
        for (uint8_t b = 0; b < slot->band_count; b++) {
            float band_bot = bbox_y0 + ((float)b * band_height);
            float band_top = band_bot + band_height;
            if (cy_max >= band_bot && cy_min <= band_top) {
                if (band_curve_counts[b] < NT_FONT_MAX_CURVES_PER_GLYPH) {
                    band_curve_indices[b][band_curve_counts[b]] = ci;
                    band_curve_counts[b]++;
                }
            }
        }
    }
    // #endregion

    // #region Calculate total texels needed
    uint32_t total_band_curves = 0;
    for (uint8_t b = 0; b < slot->band_count; b++) {
        total_band_curves += band_curve_counts[b];
    }
    uint32_t needed_texels = total_band_curves * 2; /* 2 texels per curve */
    // #endregion

    ensure_curve_space(slot, needed_texels);

    // #region Write curve data to staging buffer and record band offsets
    uint32_t curve_offset = slot->curve_write_head;
    uint16_t *staging = slot->curve_staging;
    uint16_t band_offsets[32] = {0};
    uint16_t band_counts_final[32] = {0};

    uint32_t write_pos = curve_offset;
    for (uint8_t b = 0; b < slot->band_count; b++) {
        band_offsets[b] = (uint16_t)write_pos;
        band_counts_final[b] = band_curve_counts[b];

        for (uint16_t ci = 0; ci < band_curve_counts[b]; ci++) {
            const nt_curve_t *c = &curves[band_curve_indices[b][ci]];
            uint32_t t0 = write_pos * 4;
            uint32_t t1 = t0 + 4;
            staging[t0 + 0] = nt_float32_to_float16(c->p0x);
            staging[t0 + 1] = nt_float32_to_float16(c->p0y);
            staging[t0 + 2] = nt_float32_to_float16(c->p1x);
            staging[t0 + 3] = nt_float32_to_float16(c->p1y);
            staging[t1 + 0] = nt_float32_to_float16(c->p2x);
            staging[t1 + 1] = nt_float32_to_float16(c->p2y);
            staging[t1 + 2] = 0;
            staging[t1 + 3] = 0;
            write_pos += 2;
        }
    }
    // #endregion

    // #region Upload curve data to GPU
    if (needed_texels > 0) {
        uint32_t remaining = needed_texels;
        uint32_t src = curve_offset;
        while (remaining > 0) {
            uint16_t row = (uint16_t)(src / slot->curve_tex_width);
            uint16_t col = (uint16_t)(src % slot->curve_tex_width);
            uint16_t w = (uint16_t)(slot->curve_tex_width - col);
            if (w > remaining) {
                w = (uint16_t)remaining;
            }
            nt_gfx_update_texture(slot->curve_texture, col, row, w, 1, &staging[(size_t)src * 4]);
            remaining -= w;
            src += w;
        }
    }
    // #endregion

    // #region Upload band data to GPU (RG16UI: curve_start, curve_count per band)
    if (slot->band_count > 0) {
        size_t band_alloc_count = (size_t)slot->band_count * 2;
        uint16_t *band_upload = (uint16_t *)calloc(band_alloc_count, sizeof(uint16_t));
        NT_ASSERT(band_upload);
        if (band_upload) {
            for (uint8_t b = 0; b < slot->band_count; b++) {
                band_upload[(b * 2) + 0] = band_offsets[b];
                band_upload[(b * 2) + 1] = band_counts_final[b];
            }
            nt_gfx_update_texture(slot->band_texture, 0, cache_idx, slot->band_count, 1, band_upload);
            free(band_upload);
        }
    }
    // #endregion

    // #region Fill cache entry
    nt_font_cache_slot_t *cs = &slot->cache[cache_idx];
    cs->entry.codepoint = glyph->codepoint;
    cs->entry.curve_offset = curve_offset;
    cs->entry.curve_count = (uint16_t)total_band_curves;
    cs->entry.band_row = cache_idx;
    cs->entry.advance = glyph->advance;
    cs->entry.bbox_x0 = glyph->bbox_x0;
    cs->entry.bbox_y0 = glyph->bbox_y0;
    cs->entry.bbox_x1 = glyph->bbox_x1;
    cs->entry.bbox_y1 = glyph->bbox_y1;
    cs->entry.is_tofu = false;
    cs->lru_frame = slot->frame_counter;
    cs->resource_index = resource_index;
    // #endregion

    slot->curve_write_head = write_pos;
    slot->glyphs_cached++;
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

    /* Register font activator for NT_ASSET_FONT resources */
    nt_resource_set_activator(NT_ASSET_FONT, activate_font, deactivate_font);

    /* Reset font data storage */
    memset(&s_font_data, 0, sizeof(s_font_data));

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
        free(slot->curve_staging);
        nt_gfx_destroy_texture(slot->curve_texture);
        nt_gfx_destroy_texture(slot->band_texture);
    }
    // #endregion
    free(s_font.slots);
    nt_pool_shutdown(&s_font.pool);
    memset(&s_font, 0, sizeof(s_font));
    memset(&s_font_data, 0, sizeof(s_font_data));
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void nt_font_step(void) {
    NT_ASSERT(s_font.initialized);
    if (!s_font.initialized) {
        return;
    }

    for (uint32_t i = 1; i <= s_font.pool.capacity; i++) {
        if (!nt_pool_slot_alive(&s_font.pool, i)) {
            continue;
        }

        nt_font_slot_t *slot = &s_font.slots[i];
        slot->frame_counter++;

        // #region Resolve resources
        for (uint8_t ri = 0; ri < slot->resource_count; ri++) {
            uint32_t ver = nt_resource_get(slot->resources[ri]);
            if (ver == 0) {
                continue; /* resource not ready yet */
            }
            if (ver == slot->resource_versions[ri]) {
                continue; /* no change */
            }

            /* Resource changed -- parse font header */
            uint32_t blob_size = 0;
            const uint8_t *blob = get_font_data(ver, &blob_size);
            if (!blob || blob_size < sizeof(NtFontAssetHeader)) {
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
                NT_ASSERT(slot->metrics.units_per_em == hdr->units_per_em);
                NT_ASSERT(slot->metrics.ascent == hdr->ascent);
                NT_ASSERT(slot->metrics.descent == hdr->descent);
                NT_ASSERT(slot->metrics.line_gap == hdr->line_gap);
            }
            // #endregion

            /* If version changed (reload), invalidate cache */
            if (slot->resource_versions[ri] != 0) {
                memset(slot->cache, 0, (size_t)slot->max_glyphs * sizeof(nt_font_cache_slot_t));
                slot->glyphs_cached = 0;
                slot->curve_write_head = 0;
                slot->tofu_generated = false;
            }

            slot->resource_versions[ri] = ver;
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

    uint8_t band_count = desc->band_count > 0 ? desc->band_count : 8; /* default 8 per D-03 */

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
        .width = band_count,
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

    // #region Allocate cache and staging buffer
    slot->cache = (nt_font_cache_slot_t *)calloc(desc->band_texture_height, sizeof(nt_font_cache_slot_t));
    NT_ASSERT(slot->cache);

    /* CPU staging buffer for curve texture (RGBA16F = 4 x uint16 per texel) per D-12 */
    size_t total_texels = (size_t)desc->curve_texture_width * desc->curve_texture_height;
    slot->curve_staging = (uint16_t *)calloc(total_texels * 4, sizeof(uint16_t));
    NT_ASSERT(slot->curve_staging);
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
    free(slot->curve_staging);
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

void nt_font_add(nt_font_t font, nt_resource_t resource) {
    NT_ASSERT(s_font.initialized);
    NT_ASSERT(nt_pool_valid(&s_font.pool, font.id));

    nt_font_slot_t *slot = get_slot(font);
    NT_ASSERT(slot->resource_count < NT_FONT_MAX_RESOURCES);

    slot->resources[slot->resource_count] = resource;
    slot->resource_versions[slot->resource_count] = 0; /* resolved in step */
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

    // #region Cache hit check
    for (uint16_t i = 0; i < slot->max_glyphs; i++) {
        if (slot->cache[i].entry.codepoint == codepoint) {
            slot->cache[i].lru_frame = slot->frame_counter;
            return &slot->cache[i].entry;
        }
    }
    // #endregion

    // #region Cache miss -- find glyph in resources
    uint8_t res_idx = 0;
    const NtFontGlyphEntry *glyph_entry = NULL;
    bool found = find_glyph_in_resources(slot, codepoint, &res_idx, &glyph_entry);

    if (!found) {
        /* Generate tofu if needed, then return tofu entry */
        generate_tofu(slot);
        for (uint16_t i = 0; i < slot->max_glyphs; i++) {
            if (slot->cache[i].entry.is_tofu) {
                return &slot->cache[i].entry;
            }
        }
        return NULL; /* should not happen after tofu generation */
    }
    // #endregion

    // #region Decode contour data
    uint32_t blob_size = 0;
    const uint8_t *blob = get_font_data(slot->resource_versions[res_idx], &blob_size);
    NT_ASSERT(blob);

    /* Contour data is at: data_offset + kern_count * sizeof(NtFontKernEntry) */
    uint32_t contour_offset = glyph_entry->data_offset + ((uint32_t)glyph_entry->kern_count * (uint32_t)sizeof(NtFontKernEntry));
    const uint8_t *contour_data = blob + contour_offset;

    nt_curve_t curves[NT_FONT_MAX_CURVES_PER_GLYPH];
    uint16_t curve_count = decode_contours(contour_data, curves, NT_FONT_MAX_CURVES_PER_GLYPH);
    // #endregion

    // #region Allocate cache slot
    uint16_t cache_idx;
    if (slot->glyphs_cached < slot->max_glyphs) {
        /* Find empty slot */
        cache_idx = 0;
        for (uint16_t i = 0; i < slot->max_glyphs; i++) {
            if (slot->cache[i].entry.codepoint == 0) {
                cache_idx = i;
                break;
            }
        }
    } else {
        cache_idx = evict_lru(slot);
    }
    // #endregion

    // #region Upload and fill cache
    upload_glyph(slot, cache_idx, glyph_entry, curves, curve_count, res_idx);
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

/* ---- Kern pair lookup ---- */

int16_t nt_font_get_kern(nt_font_t font, uint32_t left_codepoint, uint32_t right_codepoint) {
    NT_ASSERT(s_font.initialized);
    NT_ASSERT(nt_pool_valid(&s_font.pool, font.id));

    nt_font_slot_t *slot = get_slot(font);

    /* Find left glyph in resources to access its kern entries */
    for (uint8_t ri = 0; ri < slot->resource_count; ri++) {
        if (slot->resource_versions[ri] == 0) {
            continue;
        }
        uint32_t blob_size = 0;
        const uint8_t *blob = get_font_data(slot->resource_versions[ri], &blob_size);
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
