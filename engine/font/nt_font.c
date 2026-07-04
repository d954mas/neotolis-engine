#include "font/nt_font.h"
#include "font/nt_font_hot.h"
#include "font/nt_font_internal.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "core/nt_assert.h"
#include "graphics/nt_gfx.h"
#include "hash/nt_hash.h"
#include "log/nt_log.h"
#include "math/nt_math.h"
#include "nt_font_format.h"
#include "nt_pack_format.h"
#include "pool/nt_pool.h"
#include "resource/nt_resource.h"
#include "utf8/nt_utf8.h"
#ifdef NT_TEST_ACCESS
#include "nt_crc32.h"
#endif

/* ---- Module state ---- */

static nt_font_state_t s_font;

/* Forward declarations for internal helpers used before their definitions. */
static void rebuild_ascii_index(nt_font_slot_t *slot);
static void measure_cache_clear(nt_font_slot_t *slot);
static void clear_glyph_cache(nt_font_slot_t *slot);
#ifdef NT_TEST_ACCESS
static void font_test_shutdown_packs(void);
#endif

// #region Resolve-callback lifecycle — on_resolve/on_cleanup + PIN_BLOB
/* Zero-copy provider: a {blob,size} view into the winning pack blob, held in the slot's user_data.
 * Fonts decode glyphs lazily from the live blob, so the winner PINs its pack (PIN_BLOB); the resolve
 * pass owns the pin, the font never ref/unrefs it. */
typedef struct {
    const uint8_t *data;
    uint32_t size;
} nt_font_provider_t;

/* No-op activator — real work is in font_on_resolve. Returns a UNIQUE handle per activation (font is
 * not aux-backed, so the resolve pass detects a winner-change only via a handle change); a reused
 * handle would leave user_data pointing at a freed blob. The value is never interpreted. */
static uint32_t s_font_activate_seq;
static uint32_t font_activate(const uint8_t *data, uint32_t size) {
    (void)data;
    (void)size;
    if (s_font_activate_seq == 0) {
        s_font_activate_seq = 1; /* 0 signals activation failure to the resource system */
    }
    return s_font_activate_seq++;
}

/* deactivate must NOT touch user_data — font_on_cleanup owns that lifecycle. */
static void font_deactivate(uint32_t runtime_handle) { (void)runtime_handle; }

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void font_provider_clear(void **user_data) {
    nt_font_provider_t *p = (nt_font_provider_t *)*user_data;
    if (p != NULL) {
        p->data = NULL;
        p->size = 0;
    }
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void font_on_resolve(const uint8_t *data, uint32_t size, uint32_t runtime_handle, void **user_data) {
    (void)runtime_handle;
    if (data == NULL) {
        return; /* evicted/absent — keep existing view; PIN_BLOB winner is unpublishable while blob==NULL */
    }
    if (size < sizeof(NtFontAssetHeader)) {
        /* Present but truncated: this pack is now the published winner, so the previous winner's pack is
         * no longer pinned and may be evicted — stop viewing it. Degrade to tofu (control flow, not assert:
         * malformed data is a runtime safety-net path, and NT_ASSERT is a no-op in shipping). */
        font_provider_clear(user_data);
        return;
    }
    /* Runtime safety net — real guard, not assert-only (NT_ASSERT is a no-op in shipping). */
    const NtFontAssetHeader *hdr = (const NtFontAssetHeader *)data;
    NT_ASSERT(hdr->magic == NT_FONT_MAGIC && "font blob: bad magic");
    NT_ASSERT(hdr->version == NT_FONT_VERSION && "font blob: version mismatch — rebuild packs");
    if (hdr->magic != NT_FONT_MAGIC || hdr->version != NT_FONT_VERSION) {
        /* Reject: this corrupt pack is now the published winner, so the previous winner's pack is
         * no longer pinned and may be evicted — stop pointing at it. Degrade to tofu. */
        font_provider_clear(user_data);
        return;
    }
    nt_font_provider_t *p = (nt_font_provider_t *)*user_data;
    if (p == NULL) {
        p = (nt_font_provider_t *)calloc(1, sizeof(*p));
        NT_ASSERT(p);
        *user_data = p;
    }
    /* Point at the CURRENT resident blob; winner-change updates the same holder. */
    p->data = data;
    p->size = size;
}

static void font_on_cleanup(void *user_data) {
    /* Free the holder only — the blob pin is rebuilt from the winners by the resolve pass, so there is nothing to release here. */
    free(user_data);
}

/* Read the winning resource's live blob through its pinned user_data holder
 * (never a raw pointer cached across frames). NULL when the provider is gone. */
static const uint8_t *font_provider_blob(nt_resource_t resource, uint32_t *out_size) {
    const nt_font_provider_t *p = (const nt_font_provider_t *)nt_resource_get_user_data(resource);
    if (p == NULL || p->data == NULL) {
        if (out_size != NULL) {
            *out_size = 0;
        }
        return NULL;
    }
    if (out_size != NULL) {
        *out_size = p->size;
    }
    return p->data;
}
// #endregion

/* Single source of truth for the cleanup that must follow any change in a slot's
 * resolved provider set. Called from the epoch-gated nt_font_step with its own
 * conditional for `flush_glyphs`. */
static void slot_refresh_after_resource_change(nt_font_slot_t *slot, bool flush_glyphs) {
    if (flush_glyphs) {
        clear_glyph_cache(slot);
    }
    rebuild_ascii_index(slot);
    if (slot->measure_cache.key_hashes != NULL) {
        measure_cache_clear(slot);
    }
    bool any_active = false;
    for (uint8_t j = 0; j < slot->resource_count; j++) {
        if (slot->resource_handles[j] != 0) {
            any_active = true;
            break;
        }
    }
    if (!any_active && slot->metrics_set) {
        slot->metrics = (nt_font_metrics_t){0};
        slot->metrics_set = false;
    }
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

/* Cache-key home slot: fmix32(cp ^ off*golden) & mask. Mixing key_offset in
 * spreads (codepoint, weight) variants across the table so a bold 'A' and a
 * regular 'A' land in different homes. MUST be identical in lookup/insert/remove
 * (incl. the backshift home recompute) or the probe chains corrupt on eviction. */
static inline uint16_t key_home(uint16_t mask, uint32_t cp, int16_t off) {
    uint32_t h = cp ^ ((uint32_t)(int32_t)off * 0x9E3779B1U);
    h ^= h >> 16;
    h *= 0x7FEB352DU;
    h ^= h >> 15;
    return (uint16_t)(h & mask);
}

/* Lookup: returns pointer to cache entry for (codepoint, key_offset), or NULL on miss */
static nt_font_cache_slot_t *hash_lookup(nt_font_slot_t *slot, uint32_t codepoint, int16_t key_offset) {
    uint16_t mask = (uint16_t)(slot->hash_table_size - 1);
    uint16_t pos = key_home(mask, codepoint, key_offset);
    for (;;) {
        uint16_t val = slot->hash_table[pos];
        if (val == 0) {
            return NULL;
        }
        nt_font_cache_slot_t *cs = &slot->cache[val - 1];
        if (cs->entry.codepoint == codepoint && cs->key_offset == key_offset) {
            return cs;
        }
        pos = (uint16_t)((pos + 1) & mask);
    }
}

/* Insert: slot_idx is index into cache[]; the slot's entry.codepoint + key_offset are already set */
static void hash_insert(nt_font_slot_t *slot, uint32_t codepoint, int16_t key_offset, uint16_t slot_idx) {
    uint16_t mask = (uint16_t)(slot->hash_table_size - 1);
    uint16_t pos = key_home(mask, codepoint, key_offset);
    for (;;) {
        if (slot->hash_table[pos] == 0) {
            slot->hash_table[pos] = (uint16_t)(slot_idx + 1);
            return;
        }
        pos = (uint16_t)((pos + 1) & mask);
    }
}

/* Remove one (codepoint, key_offset) entry with backshift to keep probe chains intact.
 * Called by evict_lru before clearing the cache slot — O(cluster) instead of O(N) rebuild. */
static void hash_remove(nt_font_slot_t *slot, uint32_t codepoint, int16_t key_offset) {
    uint16_t mask = (uint16_t)(slot->hash_table_size - 1);
    uint16_t pos = key_home(mask, codepoint, key_offset);

    /* Find the entry */
    for (;;) {
        if (slot->hash_table[pos] == 0) {
            return; /* not found — nothing to remove */
        }
        uint16_t idx = (uint16_t)(slot->hash_table[pos] - 1);
        if (slot->cache[idx].entry.codepoint == codepoint && slot->cache[idx].key_offset == key_offset) {
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
        uint16_t home = key_home(mask, slot->cache[idx].entry.codepoint, slot->cache[idx].key_offset);
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

/* Find glyph across all resources (first wins). ASCII codepoints take the
 * precomputed ascii_glyph_idx table (one array lookup); non-ASCII falls
 * through to the per-resource bsearch loop. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static bool find_glyph_in_resources(nt_font_slot_t *slot, uint32_t codepoint, uint8_t *out_resource_index, const NtFontGlyphEntry **out_glyph_entry) {
    if (codepoint < 128U) {
        const uint16_t gi = slot->ascii_glyph_idx[codepoint];
        if (gi != NT_FONT_ASCII_IDX_NONE) {
            const uint8_t ri = slot->ascii_glyph_res[codepoint];
            uint32_t blob_size = 0;
            const uint8_t *blob = font_provider_blob(slot->resources[ri], &blob_size);
            if (blob && blob_size >= sizeof(NtFontAssetHeader)) {
                const NtFontAssetHeader *hdr = (const NtFontAssetHeader *)blob;
                /* gi is bounded by construction in rebuild_ascii_index;
                 * the bounds check guards blob corruption between load and access. */
                NT_ASSERT(gi < hdr->glyph_count);
                if (gi < hdr->glyph_count) {
                    const NtFontGlyphEntry *glyphs = (const NtFontGlyphEntry *)(blob + sizeof(NtFontAssetHeader));
                    *out_resource_index = ri;
                    *out_glyph_entry = glyphs + gi;
                    return true;
                }
            }
        }
        /* ASCII char not in any resource — fall through (tofu fallback). */
    }

    for (uint8_t i = 0; i < slot->resource_count; i++) {
        if (slot->resource_handles[i] == 0) {
            continue; /* not resolved yet */
        }
        uint32_t blob_size = 0;
        const uint8_t *blob = font_provider_blob(slot->resources[i], &blob_size);
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

/* ---- LRU eviction ---- */

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
            continue; /* never evict tofu */
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
    hash_remove(slot, slot->cache[victim].entry.codepoint, slot->cache[victim].key_offset);

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
 * RGBA16F: 4 uint16 per texel. Realistic: 40 curves * 8 bands * 2 * 2 * 4 = 5KB. */
static uint16_t s_curve_upload[NT_FONT_MAX_CURVES_PER_GLYPH * NT_FONT_MAX_BANDS * 2 * 4 * 2];

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

/* Pre-flush callback fires first so consumers can drain staging buffers
 * while texture offsets are still valid. */
static void clear_glyph_cache(nt_font_slot_t *slot) {
    if (s_font.pre_flush_fn) {
        s_font.pre_flush_fn();
    }
    memset(slot->cache, 0, (size_t)slot->max_glyphs * sizeof(nt_font_cache_slot_t));
    memset(slot->hash_table, 0, (size_t)slot->hash_table_size * sizeof(uint16_t));
    free_stack_reset(slot);
    slot->glyphs_cached = 0;
    slot->curve_write_head = 0;
    slot->tofu_generated = false;
    slot->cache_generation++;
}

/* Same reset as clear_glyph_cache, plus the "texture full" warning. */
static void flush_cache_for_overflow(nt_font_slot_t *slot) {
    NT_LOG_WARN("font cache flush: curve texture full (%ux%u), consider larger curve_texture_width/height", slot->curve_tex_width, slot->curve_tex_height);
    clear_glyph_cache(slot);
}

/* Ensure enough texels, flushing if needed.
 * WARNING: flush invalidates ALL cache entries, hash table, and bumps
 * cache_generation. Callers (upload_glyph, generate_tofu) must handle the
 * case where the cache was reset mid-operation. Currently safe because
 * cache_idx is (re)allocated after this call returns. */
static void ensure_curve_space(nt_font_slot_t *slot, uint32_t needed_texels) {
    uint32_t total = (uint32_t)slot->curve_tex_width * slot->curve_tex_height;
    NT_ASSERT(needed_texels <= total); /* single glyph exceeds entire curve texture */
    if (slot->curve_write_head + needed_texels > total) {
        flush_cache_for_overflow(slot);
    }
}

/* ---- Tofu generation ---- */

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
    uint32_t x_curve_offset = curve_offset + (4 * 2); /* after Y-band curves */
    for (int seg = 0; seg < 4; seg++) {
        uint32_t src0 = (uint32_t)seg * 2 * 4;
        uint32_t dst0 = (uint32_t)(4 + seg) * 2 * 4;
        for (uint32_t k = 0; k < 8; k++) {
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
    uint16_t band_data[NT_FONT_MAX_BANDS * 2 * 2] = {0};
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
    cs->entry.codepoint = 0xFFFFFFFFU; /* tofu sentinel */
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
    cs->key_offset = 0; /* tofu is weight-agnostic */
    cs->lru_frame = s_font.frame_counter;

    slot->curve_write_head += needed_texels;
    slot->glyphs_cached++;
    slot->tofu_generated = true;
    hash_insert(slot, 0xFFFFFFFFU, 0, cache_idx);
}

/* ---- Decode contours and upload glyph to GPU ---- */

/* Temporary curve storage for band decomposition */
typedef struct {
    float p0x, p0y, p1x, p1y, p2x, p2y;
} nt_curve_t;

static nt_curve_t s_decode_curves[NT_FONT_MAX_CURVES_PER_GLYPH];

/* Temporary point buffers for contour decoding (static to avoid stack overflow) */
static int32_t s_decode_pts_x[NT_FONT_MAX_POINTS_PER_CONTOUR];
static int32_t s_decode_pts_y[NT_FONT_MAX_POINTS_PER_CONTOUR];
static uint8_t s_decode_pts_on[NT_FONT_MAX_POINTS_PER_CONTOUR];

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

/* Saturating float->int16 for the widened emboldened bbox: an absurd (unclamped) weight can push the
 * extent past INT16, and a bare cast would be C UB. Mirrors nt_font_quantize_weight's clamp. */
static inline int16_t clamp_i16(float v) {
    if (v > 32767.0F) {
        return 32767;
    }
    if (v < -32768.0F) {
        return -32768;
    }
    return (int16_t)v;
}

/* Emit one quadratic curve to the output buffer */
static inline void emit_curve(nt_curve_t *curves, uint16_t *total, uint16_t max_c, float p0x, float p0y, float p1x, float p1y, float p2x, float p2y) {
    if (*total < max_c) {
        curves[*total] = (nt_curve_t){p0x, p0y, p1x, p1y, p2x, p2y};
        (*total)++;
    }
}

/* CPU embolden + offset self-intersection resolution. All scratch is
 * static preallocated (no heap on the decode miss path); caps measured by the offset-resolution benchmark:
 * reflex joins observed <=7/contour, self-crossings <=31 (齒 @0.32em). Overflow degrades
 * to today's rendering (raw offset ring), never crashes. */
#define NT_FONT_OFFSET_MAX_JOINS 64 /* reflex retraction joins/contour; beyond -> plain miter (>= today) */
#define NT_FONT_OFFSET_MAX_XINGS 64 /* proper self-crossings/ring; overflow -> keep raw ring (= today) */
#define NT_FONT_OFFSET_RING_MAX (NT_FONT_MAX_POINTS_PER_CONTOUR + (2 * NT_FONT_OFFSET_MAX_JOINS))
#define NT_FONT_OFFSET_NODE_MAX (NT_FONT_OFFSET_RING_MAX + (2 * NT_FONT_OFFSET_MAX_XINGS))
/* Counter-preserving outline: a counter (hole) keeps >= this fraction of its OWN inradius under
 * any offset, so it never closes on any font. Fraction-of-inradius (not em) => scale/font-
 * invariant, proportionally-consistent openness. Documented default; tune per taste. */
#define NT_FONT_COUNTER_KEEP 0.35F

/* Offset+joins destination ring: offset_with_joins reads the base s_decode_pts_*, writes here,
 * so vertex adjacency reads pristine source coords (no in-place snapshot needed). */
static int32_t s_offset_x[NT_FONT_OFFSET_RING_MAX];
static int32_t s_offset_y[NT_FONT_OFFSET_RING_MAX];
static uint8_t s_offset_on[NT_FONT_OFFSET_RING_MAX];

/* Offset a point ring by W (embolden), writing the result to a SEPARATE dst ring. sign=-1 grows
 * the outer silhouette / shrinks counters for the builder's stbtt winding; miter d capped at
 * d_max=2.0 (geometry guard). No W clamp — set_weight applies W exactly (explicit-over-implicit);
 * the 1e-6 length guard + lrintf keep output finite at any W.
 * At a cap-binding reflex corner of a GROWER ring, emit a Clipper-style retraction join (wall end
 * at exact R, original vertex, wall end) so the converging offset walls cross at the true trim
 * point; resolve_and_emit then excises the pocket (kills the 'W'-valley sealed-dent defect, D3).
 * a0 = signed area of the source ring (winding reference). Thin (W<0) swaps grower/shrinker by
 * symmetry (NOT raster-validated). Returns dst point count. */
static uint16_t offset_with_joins(const int32_t *sx, const int32_t *sy, const uint8_t *son, uint16_t n, int32_t *dx, int32_t *dy, uint8_t *don, float W, double a0) {
    if (W == 0.0F || n < 2) {
        for (uint16_t i = 0; i < n; i++) {
            dx[i] = sx[i];
            dy[i] = sy[i];
            don[i] = son[i];
        }
        return n;
    }
    bool grower = (W > 0.0F) ? (a0 < 0.0) : (a0 > 0.0);
    uint16_t joins = 0;
    uint16_t m = 0;
    for (uint16_t i = 0; i < n; i++) {
        uint16_t p = (i == 0) ? (uint16_t)(n - 1) : (uint16_t)(i - 1);
        uint16_t q = (uint16_t)((i + 1) % n);
        float inx = (float)(sx[i] - sx[p]);
        float iny = (float)(sy[i] - sy[p]);
        float oux = (float)(sx[q] - sx[i]);
        float ouy = (float)(sy[q] - sy[i]);
        float li = sqrtf((inx * inx) + (iny * iny)) + 1e-6F;
        float lo = sqrtf((oux * oux) + (ouy * ouy)) + 1e-6F;
        /* normals: rotate tangent -90 => n=(ty,-tx), normalized */
        float nix = iny / li;
        float niy = -inx / li;
        float nox = ouy / lo;
        float noy = -oux / lo;
        float dot = (nix * nox) + (niy * noy);
        float turn = (inx * ouy) - (iny * oux);
        bool reflex = (turn != 0.0F) && ((turn > 0.0F) != (a0 > 0.0));
        if (grower && son[i] && reflex && dot <= -0.5F && joins < NT_FONT_OFFSET_MAX_JOINS && (uint32_t)m + 3 <= NT_FONT_OFFSET_RING_MAX) {
            /* wall endpoints at exact R = W/2 along each edge normal (sign -1 folded in) */
            dx[m] = sx[i] + (int32_t)lrintf(-nix * W * 0.5F);
            dy[m] = sy[i] + (int32_t)lrintf(-niy * W * 0.5F);
            don[m] = 1;
            m++;
            dx[m] = sx[i]; /* original vertex = retraction anchor */
            dy[m] = sy[i];
            don[m] = 1;
            m++;
            dx[m] = sx[i] + (int32_t)lrintf(-nox * W * 0.5F);
            dy[m] = sy[i] + (int32_t)lrintf(-noy * W * 0.5F);
            don[m] = 1;
            m++;
            joins++;
            continue;
        }
        float ssx = nix + nox;
        float ssy = niy + noy;
        float d = 1.0F / fmaxf(1.0F + dot, 0.25F); /* miter, clamped */
        d = fminf(d, 2.0F);                        /* hard cap: no convex spikes */
        dx[m] = sx[i] + (int32_t)lrintf(-ssx * d * W * 0.5F);
        dy[m] = sy[i] + (int32_t)lrintf(-ssy * d * W * 0.5F);
        don[m] = son[i];
        m++;
    }
    return m;
}

/* Signed area of the closed point ring (shoelace); int64 accum avoids overflow. */
static double contour_signed_area(const int32_t *x, const int32_t *y, uint16_t n) {
    int64_t acc = 0;
    for (uint16_t p = 0; p < n; p++) {
        uint16_t q = (uint16_t)((p + 1) % n);
        acc += ((int64_t)x[p] * y[q]) - ((int64_t)x[q] * y[p]);
    }
    return 0.5 * (double)acc;
}

/* orient sign of (a,b,c): >0 CCW, <0 CW, 0 collinear. int64 avoids int32 overflow. */
static int contour_orient(int32_t ax, int32_t ay, int32_t bx, int32_t by, int32_t cx, int32_t cy) {
    int64_t cross = ((int64_t)(bx - ax) * (cy - ay)) - ((int64_t)(by - ay) * (cx - ax));
    return (cross > 0) - (cross < 0);
}

/* PROPER crossing between segments (a,b) and (c,d): all four orients non-zero and
 * opposite on each edge. Shared vertices (collinear/touching) are NOT crossings. */
static bool seg_proper_cross(int32_t ax, int32_t ay, int32_t bx, int32_t by, int32_t cx, int32_t cy, int32_t dx, int32_t dy) {
    int o1 = contour_orient(ax, ay, bx, by, cx, cy);
    int o2 = contour_orient(ax, ay, bx, by, dx, dy);
    int o3 = contour_orient(cx, cy, dx, dy, ax, ay);
    int o4 = contour_orient(cx, cy, dx, dy, bx, by);
    return o1 != 0 && o2 != 0 && o3 != 0 && o4 != 0 && o1 != o2 && o3 != o4;
}

/* Does the closed ring have any proper self-crossing? Used by the seal-radius search (and the
 * test hook). O(n²), decode miss path only; returns on the first crossing. */
static bool ring_self_intersects(const int32_t *x, const int32_t *y, uint16_t n) {
    if (n < 4) {
        return false;
    }
    for (uint16_t i = 0; i < n; i++) {
        uint16_t i2 = (uint16_t)((i + 1) % n);
        for (uint16_t j = (uint16_t)(i + 1); j < n; j++) {
            uint16_t j2 = (uint16_t)((j + 1) % n);
            if (j == i2 || j2 == i) {
                continue;
            }
            if (seg_proper_cross(x[i], y[i], x[i2], y[i2], x[j], y[j], x[j2], y[j2])) {
                return true;
            }
        }
    }
    return false;
}

// #region Offset self-intersection resolution — uncross + signed-loop filter + survival
/* One recorded self-crossing: the two crossing edges (i,i+1)x(j,j+1) and the rounded
 * intersection point (grid-snapped like the int point ring). */
typedef struct {
    uint16_t ei, ej;
    int32_t px, py;
    double ti, tj;
} nt_offset_xing_t;
static nt_offset_xing_t s_offset_xings[NT_FONT_OFFSET_MAX_XINGS];

/* Record all proper self-crossings of the ring (O(n²), decode miss path only). Returns
 * count, or -1 on overflow (caller keeps the raw ring = today's rendering, never crashes). */
static int find_offset_crossings(const int32_t *x, const int32_t *y, uint16_t n) {
    int cnt = 0;
    if (n < 4) {
        return 0;
    }
    for (uint16_t i = 0; i < n; i++) {
        uint16_t i2 = (uint16_t)((i + 1) % n);
        for (uint16_t j = (uint16_t)(i + 1); j < n; j++) {
            uint16_t j2 = (uint16_t)((j + 1) % n);
            if (j == i2 || j2 == i) {
                continue; /* adjacent edges share an endpoint */
            }
            if (!seg_proper_cross(x[i], y[i], x[i2], y[i2], x[j], y[j], x[j2], y[j2])) {
                continue;
            }
            if (cnt >= NT_FONT_OFFSET_MAX_XINGS) {
                return -1;
            }
            double rx = (double)(x[i2] - x[i]);
            double ry = (double)(y[i2] - y[i]);
            double sxx = (double)(x[j2] - x[j]);
            double syy = (double)(y[j2] - y[j]);
            double den = (rx * syy) - (ry * sxx); /* != 0 for a proper crossing */
            double qx = (double)(x[j] - x[i]);
            double qy = (double)(y[j] - y[i]);
            double t = ((qx * syy) - (qy * sxx)) / den;
            double u = ((qx * ry) - (qy * rx)) / den;
            s_offset_xings[cnt].ei = i;
            s_offset_xings[cnt].ej = j;
            s_offset_xings[cnt].ti = t;
            s_offset_xings[cnt].tj = u;
            s_offset_xings[cnt].px = (int32_t)lrint((double)x[i] + (t * rx));
            s_offset_xings[cnt].py = (int32_t)lrint((double)y[i] + (t * ry));
            cnt++;
        }
    }
    return cnt;
}

/* Distance from (px,py) to the closed ring's boundary (min over edges). */
static double ring_point_dist(double px, double py, const int32_t *x, const int32_t *y, uint16_t n) {
    double best = 1e30;
    for (uint16_t j = 0; j < n; j++) {
        uint16_t j2 = (uint16_t)((j + 1) % n);
        double ax = (double)x[j];
        double ay = (double)y[j];
        double vx = (double)x[j2] - ax;
        double vy = (double)y[j2] - ay;
        double wx = px - ax;
        double wy = py - ay;
        double vv = (vx * vx) + (vy * vy);
        double tt = (vv > 1e-12) ? ((wx * vx) + (wy * vy)) / vv : 0.0;
        if (tt < 0.0) {
            tt = 0.0;
        } else if (tt > 1.0) {
            tt = 1.0;
        }
        double ddx = wx - (tt * vx);
        double ddy = wy - (tt * vy);
        double d2 = (ddx * ddx) + (ddy * ddy);
        if (d2 < best) {
            best = d2;
        }
    }
    return sqrt(best);
}

/* Even-odd containment of (px,py) in the closed ring. */
static bool ring_point_inside(double px, double py, const int32_t *x, const int32_t *y, uint16_t n) {
    bool inside = false;
    for (uint16_t j = 0; j < n; j++) {
        uint16_t j2 = (uint16_t)((j + 1) % n);
        double ay = (double)y[j];
        double cy = (double)y[j2];
        if ((ay > py) == (cy > py)) {
            continue;
        }
        double tt = (py - ay) / (cy - ay);
        double xi = (double)x[j] + (tt * ((double)x[j2] - (double)x[j]));
        if (xi > px) {
            inside = !inside;
        }
    }
    return inside;
}

/* Inradius = radius of the largest inscribed disk (pole of inaccessibility). Polylabel-style
 * quadtree refinement (heap-free bounded DFS): prune cells that cannot beat the running best,
 * refine the promising ones to PREC. A counter's Minkowski erosion by R is non-empty IFF
 * inradius > R — the exact geometric trace/fill boundary, no fudge constant (calibration
 * matches the fine-grid GT on LilitaOne + Roboto + analytic circle/ellipse/square). */
typedef struct {
    double cx, cy, h;
} nt_pa_cell_t;
#define NT_FONT_INRADIUS_STACK 512
#define NT_FONT_INRADIUS_PREC 0.25 /* sub-unit convergence; well below the weight-quantization step */
static nt_pa_cell_t s_pa_stack[NT_FONT_INRADIUS_STACK];
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static double poly_inradius_pole(const int32_t *x, const int32_t *y, uint16_t n, double *out_cx, double *out_cy) {
    if (n < 3) {
        return 0.0;
    }
    double minx = 1e30;
    double miny = 1e30;
    double maxx = -1e30;
    double maxy = -1e30;
    for (uint16_t i = 0; i < n; i++) {
        double px = (double)x[i];
        double py = (double)y[i];
        minx = px < minx ? px : minx;
        maxx = px > maxx ? px : maxx;
        miny = py < miny ? py : miny;
        maxy = py > maxy ? py : maxy;
    }
    double w = maxx - minx;
    double ht = maxy - miny;
    double cell = w < ht ? w : ht;
    if (cell <= 0.0) {
        return 0.0;
    }
    double half = cell * 0.5;
    int nxc = (int)(w / cell) + 1; /* seed a cell grid over the bbox (integer induction) */
    int nyc = (int)(ht / cell) + 1;
    int sp = 0;
    for (int iy = 0; iy < nyc && sp < NT_FONT_INRADIUS_STACK; iy++) {
        for (int ix = 0; ix < nxc && sp < NT_FONT_INRADIUS_STACK; ix++) {
            s_pa_stack[sp++] = (nt_pa_cell_t){minx + half + ((double)ix * cell), miny + half + ((double)iy * cell), half};
        }
    }
    double bcx = minx + (w * 0.5);
    double bcy = miny + (ht * 0.5);
    double best = ring_point_inside(bcx, bcy, x, y, n) ? ring_point_dist(bcx, bcy, x, y, n) : -1.0;
    double best_cx = bcx;
    double best_cy = bcy;
    int guard = 0;
    while (sp > 0 && guard++ < 200000) {
        nt_pa_cell_t c = s_pa_stack[--sp];
        double d = ring_point_inside(c.cx, c.cy, x, y, n) ? ring_point_dist(c.cx, c.cy, x, y, n) : -ring_point_dist(c.cx, c.cy, x, y, n);
        if (d > best) {
            best = d;
            best_cx = c.cx;
            best_cy = c.cy;
        }
        if (c.h < NT_FONT_INRADIUS_PREC) {
            continue;
        }
        if (d + (c.h * 1.4142135623730951) <= best) {
            continue; /* prune: this cell cannot contain a deeper interior point */
        }
        double hh = c.h * 0.5;
        if (sp + 4 <= NT_FONT_INRADIUS_STACK) {
            s_pa_stack[sp++] = (nt_pa_cell_t){c.cx - hh, c.cy - hh, hh};
            s_pa_stack[sp++] = (nt_pa_cell_t){c.cx + hh, c.cy - hh, hh};
            s_pa_stack[sp++] = (nt_pa_cell_t){c.cx - hh, c.cy + hh, hh};
            s_pa_stack[sp++] = (nt_pa_cell_t){c.cx + hh, c.cy + hh, hh};
        }
    }
    if (out_cx != NULL) {
        *out_cx = best_cx;
    }
    if (out_cy != NULL) {
        *out_cy = best_cy;
    }
    return best > 0.0 ? best : 0.0;
}

/* Largest inscribed-disk radius (pole of inaccessibility); pole center discarded. */
static double poly_inradius(const int32_t *x, const int32_t *y, uint16_t n) { return poly_inradius_pole(x, y, n, NULL, NULL); }

/* Whole-glyph ORIGINAL outline (weight-0 curves) + its flat winding/distance tests: the reference
 * for the grower dilation-membership filter in resolve_and_emit. Built once per offset decode. */
static nt_curve_t s_orig_curves[NT_FONT_MAX_CURVES_PER_GLYPH];
static uint16_t s_orig_count;
#define NT_FONT_ORIG_FLAT_K 6 /* sub-segments per quad for the coarse inside/dist membership probe */

/* Nonzero-winding containment of (px,py) in the whole original glyph (all contours, flattened). */
static bool orig_glyph_inside(double px, double py, const nt_curve_t *cv, uint16_t nc) {
    int wind = 0;
    for (uint16_t i = 0; i < nc; i++) {
        double ax = (double)cv[i].p0x;
        double ay = (double)cv[i].p0y;
        for (int k = 1; k <= NT_FONT_ORIG_FLAT_K; k++) {
            double t = (double)k / (double)NT_FONT_ORIG_FLAT_K;
            double u = 1.0 - t;
            double bx = (u * u * (double)cv[i].p0x) + (2.0 * u * t * (double)cv[i].p1x) + (t * t * (double)cv[i].p2x);
            double by = (u * u * (double)cv[i].p0y) + (2.0 * u * t * (double)cv[i].p1y) + (t * t * (double)cv[i].p2y);
            if ((ay > py) != (by > py)) {
                double xint = ax + (((py - ay) / (by - ay)) * (bx - ax));
                if (xint > px) {
                    wind += (by > ay) ? 1 : -1;
                }
            }
            ax = bx;
            ay = by;
        }
    }
    return wind != 0;
}

/* Min distance from (px,py) to the whole original glyph boundary (all contours, flattened). */
static double orig_glyph_dist(double px, double py, const nt_curve_t *cv, uint16_t nc) {
    double best = 1e30;
    for (uint16_t i = 0; i < nc; i++) {
        double ax = (double)cv[i].p0x;
        double ay = (double)cv[i].p0y;
        for (int k = 1; k <= NT_FONT_ORIG_FLAT_K; k++) {
            double t = (double)k / (double)NT_FONT_ORIG_FLAT_K;
            double u = 1.0 - t;
            double bx = (u * u * (double)cv[i].p0x) + (2.0 * u * t * (double)cv[i].p1x) + (t * t * (double)cv[i].p2x);
            double by = (u * u * (double)cv[i].p0y) + (2.0 * u * t * (double)cv[i].p1y) + (t * t * (double)cv[i].p2y);
            double vx = bx - ax;
            double vy = by - ay;
            double wx = px - ax;
            double wy = py - ay;
            double vv = (vx * vx) + (vy * vy);
            double tt = (vv > 1e-12) ? (((wx * vx) + (wy * vy)) / vv) : 0.0;
            if (tt < 0.0) {
                tt = 0.0;
            } else if (tt > 1.0) {
                tt = 1.0;
            }
            double ddx = wx - (tt * vx);
            double ddy = wy - (tt * vy);
            double d2 = (ddx * ddx) + (ddy * ddy);
            if (d2 < best) {
                best = d2;
            }
            ax = bx;
            ay = by;
        }
    }
    return sqrt(best);
}

/* Scratch ring for the seal-radius search (trial offsets; separate from the emit ring). */
static int32_t s_seal_x[NT_FONT_OFFSET_RING_MAX];
static int32_t s_seal_y[NT_FONT_OFFSET_RING_MAX];
static uint8_t s_seal_on[NT_FONT_OFFSET_RING_MAX];

/* Seal radius: the largest inward offset R at which the counter ring does NOT self-intersect.
 * A narrow neck/channel sealing OR a sharp-corner over-shoot both surface as a self-intersection
 * of the offset ring (which the resolver would split into a wrong-sign loop that drops -> fills).
 * Binary search in (0, inradius]; convex counters never self-intersect -> inradius bounds it.
 * wsign = the offset's shrink direction. O(iters*n²) per shrinker, decode miss path only. */
static double counter_seal_radius(const int32_t *bx, const int32_t *by, const uint8_t *bon, uint16_t bn, double a0, float wsign, double inradius) {
    if (inradius <= 0.0) {
        return 0.0;
    }
    double hi = inradius * 0.98;
    uint16_t m = offset_with_joins(bx, by, bon, bn, s_seal_x, s_seal_y, s_seal_on, copysignf((float)(2.0 * hi), wsign), a0);
    if (!ring_self_intersects(s_seal_x, s_seal_y, m)) {
        return inradius; /* convex counter: no seal, inradius bounds the offset */
    }
    double lo = 0.0;
    for (int it = 0; it < 20; it++) {
        double mid = 0.5 * (lo + hi);
        m = offset_with_joins(bx, by, bon, bn, s_seal_x, s_seal_y, s_seal_on, copysignf((float)(2.0 * mid), wsign), a0);
        if (ring_self_intersects(s_seal_x, s_seal_y, m)) {
            hi = mid;
        } else {
            lo = mid;
        }
    }
    return lo; /* largest R with no self-intersection ~ just below the seal onset */
}

/* Doubly-implicit node ring for uncrossing: next[] stays a permutation, so loops are
 * clean cycles. Sized for the offset ring + two split nodes per crossing. */
typedef struct {
    int32_t x, y;
    int next;
    uint8_t on;
    uint8_t used;
} nt_offset_node_t;
static nt_offset_node_t s_offset_nodes[NT_FONT_OFFSET_NODE_MAX];
static int32_t s_offset_lx[NT_FONT_OFFSET_NODE_MAX];
static int32_t s_offset_ly[NT_FONT_OFFSET_NODE_MAX];
static uint8_t s_offset_lon[NT_FONT_OFFSET_NODE_MAX];
// #endregion

/* Walk a closed point ring into quadratic curves (TrueType rules; on-curve = degenerate
 * line quad, off→off = implicit midpoint). Shared by the plain and resolved paths. */
static void convert_point_ring(const int32_t *px, const int32_t *py, const uint8_t *pon, uint16_t n, nt_curve_t *curves, uint16_t *total_curves, uint16_t max_curves) {
    if (n == 0) {
        return;
    }
    uint16_t i = 0;
    uint16_t start = 0;
    while (start < n && !pon[start]) {
        start++;
    }
    if (start == n) {
        start = 0; /* all off-curve: start from implicit midpoint of first two */
    }
    float cur_x;
    float cur_y;
    if (pon[start]) {
        cur_x = (float)px[start];
        cur_y = (float)py[start];
        i = (uint16_t)((start + 1) % n);
    } else {
        cur_x = (float)(px[0] + px[1]) * 0.5F;
        cur_y = (float)(py[0] + py[1]) * 0.5F;
        i = 0;
    }
    uint16_t steps = 0;
    while (steps < n) {
        uint16_t idx = i % n;
        if (pon[idx]) {
            float ex = (float)px[idx];
            float ey = (float)py[idx];
            emit_curve(curves, total_curves, max_curves, cur_x, cur_y, (cur_x + ex) * 0.5F, (cur_y + ey) * 0.5F, ex, ey);
            cur_x = ex;
            cur_y = ey;
            i = (idx + 1) % n;
            steps++;
        } else {
            float cx = (float)px[idx];
            float cy = (float)py[idx];
            uint16_t next = (idx + 1) % n;
            if (pon[next]) {
                float ex = (float)px[next];
                float ey = (float)py[next];
                emit_curve(curves, total_curves, max_curves, cur_x, cur_y, cx, cy, ex, ey);
                cur_x = ex;
                cur_y = ey;
                i = (next + 1) % n;
                steps += 2;
            } else {
                float mx = (cx + (float)px[next]) * 0.5F;
                float my = (cy + (float)py[next]) * 0.5F;
                emit_curve(curves, total_curves, max_curves, cur_x, cur_y, cx, cy, mx, my);
                cur_x = mx;
                cur_y = my;
                i = next; /* don't skip next — it becomes the control for the next segment */
                steps++;
            }
        }
    }
}

/* Resolve an offset ring's self-intersections and emit the surviving loops as curves.
 * SAFETY gate: a shrinker whose erosion by r_off empties (inradius <= r_off) fills
 * solid. With counter-preservation the caller caps r_off < inradius, so this never fires for
 * a real counter — it is the fallback for a degenerate/uncapped path. Then uncross (split
 * crossings, rewire orientation-preserving) → signed-loop filter (drop loops opposing the
 * original winding = D1 notches). base_inradius = poly_inradius(base) computed once by the
 * caller (0 for growers). Winding elsewhere preserved; positive overlaps stay filled. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void resolve_and_emit(const int32_t *ox, const int32_t *oy, const uint8_t *oon, uint16_t on_n, const int32_t *bx, const int32_t *by, uint16_t bn, double a0, bool is_shrinker,
                             double base_inradius, double r_off, nt_curve_t *curves, uint16_t *total_curves, uint16_t max_curves) {
    (void)bx;
    (void)by;
    (void)bn;
    if (is_shrinker && r_off > 0.0 && base_inradius <= r_off) {
        return; /* erosion empty -> fills solid (safety; counter-preserve caps below inradius) */
    }
    int nx = find_offset_crossings(ox, oy, on_n);
    if (nx <= 0) {
        convert_point_ring(ox, oy, oon, on_n, curves, total_curves, max_curves); /* simple ring or overflow: raw */
        return;
    }

    // #region Build node ring with per-edge crossing splits
    for (uint16_t i = 0; i < on_n; i++) {
        s_offset_nodes[i] = (nt_offset_node_t){ox[i], oy[i], (i + 1) % on_n, oon[i], 0};
    }
    int nn = on_n;
    for (int k = 0; k < nx; k++) { /* crossing k owns two coincident nodes, one per strand */
        s_offset_nodes[nn] = (nt_offset_node_t){s_offset_xings[k].px, s_offset_xings[k].py, -1, 1, 0};
        s_offset_nodes[nn + 1] = (nt_offset_node_t){s_offset_xings[k].px, s_offset_xings[k].py, -1, 1, 0};
        nn += 2;
    }
    for (uint16_t e = 0; e < on_n; e++) { /* splice each edge's crossings in ascending-t order */
        int list[8];
        double lt[8];
        int lc = 0;
        for (int k = 0; k < nx; k++) {
            if (s_offset_xings[k].ei == e || s_offset_xings[k].ej == e) {
                if (lc >= 8) {
                    convert_point_ring(ox, oy, oon, on_n, curves, total_curves, max_curves); /* >8 crossings on one edge: bail to raw */
                    return;
                }
                list[lc] = (int)on_n + (2 * k) + (s_offset_xings[k].ej == e ? 1 : 0);
                lt[lc] = (s_offset_xings[k].ej == e) ? s_offset_xings[k].tj : s_offset_xings[k].ti;
                lc++;
            }
        }
        if (lc == 0) {
            continue;
        }
        for (int a = 1; a < lc; a++) { /* insertion sort by t */
            int li = list[a];
            double ta = lt[a];
            int b = a - 1;
            while (b >= 0 && lt[b] > ta) {
                list[b + 1] = list[b];
                lt[b + 1] = lt[b];
                b--;
            }
            list[b + 1] = li;
            lt[b + 1] = ta;
        }
        int tail = s_offset_nodes[e].next;
        int prev = (int)e;
        for (int a = 0; a < lc; a++) {
            s_offset_nodes[prev].next = list[a];
            prev = list[a];
        }
        s_offset_nodes[prev].next = tail;
    }
    for (int k = 0; k < nx; k++) { /* uncross: orientation-preserving smoothing (swap outgoing) */
        int na = (int)on_n + (2 * k);
        int nb = na + 1;
        int tmp = s_offset_nodes[na].next;
        s_offset_nodes[na].next = s_offset_nodes[nb].next;
        s_offset_nodes[nb].next = tmp;
    }
    // #endregion

    // #region Extract loops, filter by orientation sign + erosion survival
    bool orig_pos = a0 > 0.0;
    for (int s = 0; s < nn; s++) {
        if (s_offset_nodes[s].used) {
            continue;
        }
        int cnt = 0;
        int cur = s;
        do {
            s_offset_nodes[cur].used = 1;
            if (cnt == 0 || s_offset_lx[cnt - 1] != s_offset_nodes[cur].x || s_offset_ly[cnt - 1] != s_offset_nodes[cur].y) {
                s_offset_lx[cnt] = s_offset_nodes[cur].x; /* dedupe consecutive coincident points */
                s_offset_ly[cnt] = s_offset_nodes[cur].y;
                s_offset_lon[cnt] = s_offset_nodes[cur].on;
                cnt++;
            }
            cur = s_offset_nodes[cur].next;
        } while (cur != s && cnt <= nn);
        NT_ASSERT(cnt <= nn); /* next[] is a permutation — a longer walk means a corrupt chain */
        while (cnt > 1 && s_offset_lx[cnt - 1] == s_offset_lx[0] && s_offset_ly[cnt - 1] == s_offset_ly[0]) {
            cnt--; /* drop closing duplicate */
        }
        if (cnt < 3) {
            continue;
        }
        double la = contour_signed_area(s_offset_lx, s_offset_ly, (uint16_t)cnt);
        if (fabs(la) < 1.0) {
            continue; /* sliver */
        }
        if ((la > 0.0) != orig_pos) {
            /* Opposite-wound loop → would carve a hole. For a SHRINKER it is a D1 winding-
             * cancellation notch (drop). For a GROWER it may be a legitimate re-entrant counter of
             * a keyhole glyph ('@','&'): keep it ONLY if its deepest interior point lies OUTSIDE the
             * true dilation of the WHOLE original glyph — not inside the original fill AND farther
             * than r_off from any original edge. A pentagram's center loop is inside the original
             * fill → dropped (fills solid); '@'s counter is original background beyond r_off → kept
             * (stays open). Sign alone can't tell them apart; whole-glyph fill is the discriminator. */
            if (is_shrinker) {
                continue;
            }
            double cx = 0.0;
            double cy = 0.0;
            double pole = poly_inradius_pole(s_offset_lx, s_offset_ly, (uint16_t)cnt, &cx, &cy);
            /* No interior pole (degenerate thin loop): cx/cy would fall OUTSIDE the loop, so the
             * membership probe is meaningless — drop it (a sub-pixel sliver either way). */
            if (pole <= 0.0) {
                continue;
            }
            bool in_dilation = orig_glyph_inside(cx, cy, s_orig_curves, s_orig_count) || (orig_glyph_dist(cx, cy, s_orig_curves, s_orig_count) <= r_off);
            if (in_dilation) {
                continue;
            }
        }
        convert_point_ring(s_offset_lx, s_offset_ly, s_offset_lon, (uint16_t)cnt, curves, total_curves, max_curves);
    }
    // #endregion
}

/* Parse one v4 contour's absolute points, advancing *rp; fills pts_x/y/on, returns point_count.
 * Shared by the plain (pass 0) and the two offset passes so re-walking the blob stays consistent. */
static uint16_t parse_contour_points(const uint8_t **rp, int32_t *pts_x, int32_t *pts_y, uint8_t *pts_on) {
    uint16_t point_count;
    memcpy(&point_count, *rp, 2);
    *rp += 2;
    uint32_t flags_bytes = NT_FONT_BITMASK_BYTES(point_count);
    const uint8_t *flags = *rp;
    *rp += flags_bytes;
    int16_t first_x;
    int16_t first_y;
    memcpy(&first_x, *rp, 2);
    *rp += 2;
    memcpy(&first_y, *rp, 2);
    *rp += 2;
    NT_ASSERT(point_count <= NT_FONT_MAX_POINTS_PER_CONTOUR);
    /* Hard cap (OFF-safe, where NT_ASSERT is a no-op): a corrupt pack could record point_count past
     * the static buffer; clamp WRITES to the buffer while still advancing rp over every delta so the
     * later passes/contours stay byte-aligned. Builder guarantees the cap; this is the safety net. */
    uint16_t cap = (point_count < NT_FONT_MAX_POINTS_PER_CONTOUR) ? point_count : NT_FONT_MAX_POINTS_PER_CONTOUR;
    pts_x[0] = first_x;
    pts_y[0] = first_y;
    pts_on[0] = (flags[0] & 1U) != 0;
    int32_t px = first_x;
    int32_t py = first_y;
    for (uint16_t p = 1; p < point_count; p++) {
        int16_t dx = read_varlen_delta(rp);
        int16_t dy = read_varlen_delta(rp);
        px += dx;
        py += dy;
        if (p < cap) {
            pts_x[p] = px;
            pts_y[p] = py;
            pts_on[p] = (flags[p / 8] & (1U << (p % 8))) != 0;
        }
    }
    return cap;
}

/* Decode v4 point-based contour data into absolute float curves. Handles implicit midpoints
 * between consecutive off-curve points. weight == 0 is byte-identical to a plain decode;
 * weight != 0 emboldens each point ring and resolves offset self-intersections.
 *
 * Counter-preserving (non-uniform): the OUTER (grower) offsets by full W (thick outline); a COUNTER
 * (hole/shrinker) caps its inward offset below the SEAL radius so its narrow channel/neck never
 * touches ('@'/'e'/'a' stay open, any width, any font). Beyond that, a grower whose offset ring self-
 * intersects into re-entrant loops (keyhole glyphs '@','&') is filtered against the WHOLE original
 * glyph fill — built here in pass A — so a real counter is kept while a fill-cancellation notch is
 * dropped (resolve_and_emit). Offset runs on a SEPARATE ring so weight==0 stays byte-identical. */
static uint16_t decode_contours(const uint8_t *contour_data, nt_curve_t *curves, uint16_t max_curves, float weight) {
    uint16_t contour_count;
    memcpy(&contour_count, contour_data, 2);
    const uint8_t *body = contour_data + 2;
    uint16_t total_curves = 0;

    if (weight == 0.0F) {
        const uint8_t *rp = body;
        for (uint16_t ci = 0; ci < contour_count; ci++) {
            uint16_t point_count = parse_contour_points(&rp, s_decode_pts_x, s_decode_pts_y, s_decode_pts_on);
            convert_point_ring(s_decode_pts_x, s_decode_pts_y, s_decode_pts_on, point_count, curves, &total_curves, max_curves);
        }
        return total_curves;
    }

    /* Pass A: whole-glyph ORIGINAL outline (weight-0 curves) for the grower dilation-membership
     * filter — needs ALL contours (a keyhole counter is bounded by a different contour's hole).
     * Built unconditionally on every weight!=0 miss but READ only when a grower's offset ring self-
     * intersects into an opposite-wound loop ('@','&'-class); most glyphs never read it. Kept eager
     * for simplicity — it is a plain decode, dwarfed by pass B's O(n^2) crossing scans; lazy-build is
     * a future option if a distinct-weight cache ever thrashes. */
    s_orig_count = 0;
    const uint8_t *rp_a = body;
    for (uint16_t ci = 0; ci < contour_count; ci++) {
        uint16_t point_count = parse_contour_points(&rp_a, s_decode_pts_x, s_decode_pts_y, s_decode_pts_on);
        convert_point_ring(s_decode_pts_x, s_decode_pts_y, s_decode_pts_on, point_count, s_orig_curves, &s_orig_count, NT_FONT_MAX_CURVES_PER_GLYPH);
    }

    /* Pass B: embolden + counter-preserving resolve per contour. */
    const uint8_t *rp = body;
    for (uint16_t ci = 0; ci < contour_count; ci++) {
        int32_t *pts_x = s_decode_pts_x;
        int32_t *pts_y = s_decode_pts_y;
        uint8_t *pts_on = s_decode_pts_on;
        uint16_t point_count = parse_contour_points(&rp, pts_x, pts_y, pts_on);

        double a0 = contour_signed_area(pts_x, pts_y, point_count);
        /* TT winding (builder/stbtt): outer negative area, hole positive. Bold (W>0) shrinks holes;
         * thin (W<0) shrinks outers (symmetry, not raster-validated). */
        bool is_shrinker = (weight > 0.0F) ? (a0 > 0.0) : (a0 < 0.0);
        float w_eff = weight;
        double base_inrad = 0.0;
        if (is_shrinker) {
            base_inrad = poly_inradius(pts_x, pts_y, point_count);
            double rseal = counter_seal_radius(pts_x, pts_y, pts_on, point_count, a0, weight, base_inrad);
            float cap = 2.0F * (1.0F - NT_FONT_COUNTER_KEEP) * (float)rseal; /* keep >= KEEP of the narrowest opening; never seal */
            if (fabsf(weight) > cap) {
                w_eff = copysignf(cap, weight);
            }
        }
        uint16_t offn = offset_with_joins(pts_x, pts_y, pts_on, point_count, s_offset_x, s_offset_y, s_offset_on, w_eff, a0);
        double r_off = 0.5 * (double)fabsf(w_eff);
        resolve_and_emit(s_offset_x, s_offset_y, s_offset_on, offn, pts_x, pts_y, point_count, a0, is_shrinker, base_inrad, r_off, curves, &total_curves, max_curves);
    }
    return total_curves;
}

/* Upload a glyph to GPU textures and fill cache entry.
 * Allocates cache slot internally (after ensure_curve_space) to avoid
 * the flush-invalidates-slot bug. Returns allocated cache_idx. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static uint16_t upload_glyph(nt_font_slot_t *slot, const NtFontGlyphEntry *glyph, const nt_curve_t *curves, uint16_t curve_count, uint8_t resource_index, int16_t key_offset) {
    float bbox_y0 = (float)glyph->bbox_y0;
    float bbox_y1 = (float)glyph->bbox_y1;
    float bbox_x0 = (float)glyph->bbox_x0;
    float bbox_x1 = (float)glyph->bbox_x1;
    NT_ASSERT(slot->band_count <= NT_FONT_MAX_BANDS);

    // #region Precompute per-curve Y and X bounds
    float curve_y_min[NT_FONT_MAX_CURVES_PER_GLYPH];
    float curve_y_max[NT_FONT_MAX_CURVES_PER_GLYPH];
    float curve_x_min[NT_FONT_MAX_CURVES_PER_GLYPH];
    float curve_x_max[NT_FONT_MAX_CURVES_PER_GLYPH];
    float ext_x_min = bbox_x0;
    float ext_x_max = bbox_x1;
    float ext_y_min = bbox_y0;
    float ext_y_max = bbox_y1;
    for (uint16_t ci = 0; ci < curve_count; ci++) {
        float ay = curves[ci].p0y;
        float by = curves[ci].p1y;
        float cy = curves[ci].p2y;
        float loy = ay < by ? ay : by;
        float hiy = ay > by ? ay : by;
        curve_y_min[ci] = loy < cy ? loy : cy;
        curve_y_max[ci] = hiy > cy ? hiy : cy;

        float ax = curves[ci].p0x;
        float bx = curves[ci].p1x;
        float cx = curves[ci].p2x;
        float lox = ax < bx ? ax : bx;
        float hix = ax > bx ? ax : bx;
        curve_x_min[ci] = lox < cx ? lox : cx;
        curve_x_max[ci] = hix > cx ? hix : cx;

        ext_y_min = fminf(ext_y_min, curve_y_min[ci]);
        ext_y_max = fmaxf(ext_y_max, curve_y_max[ci]);
        ext_x_min = fminf(ext_x_min, curve_x_min[ci]);
        ext_x_max = fmaxf(ext_x_max, curve_x_max[ci]);
    }
    // #endregion

    // Offset variants grow past the packed bbox; widen bbox+bands so the emboldened edge isn't clipped (key_offset!=0 only keeps regular byte-identical).
    if (key_offset != 0) {
        bbox_x0 = floorf(ext_x_min);
        bbox_y0 = floorf(ext_y_min);
        bbox_x1 = ceilf(ext_x_max);
        bbox_y1 = ceilf(ext_y_max);
    }
    float band_height = (bbox_y1 - bbox_y0) / (float)slot->band_count;
    float band_width = (bbox_x1 > bbox_x0) ? (bbox_x1 - bbox_x0) / (float)slot->band_count : 0.0F;

    // #region Count Y-band and X-band curve pairs
    /* Epsilon margin on band boundaries to avoid edge-case misses where
     * floating-point rounding places a curve in one band but the shader
     * maps the pixel to the adjacent band. */
    float y_margin = band_height * 0.01F;
    float x_margin = band_width * 0.01F;

    uint16_t yband_counts[NT_FONT_MAX_BANDS] = {0};
    uint16_t xband_counts[NT_FONT_MAX_BANDS] = {0};
    for (uint16_t ci = 0; ci < curve_count; ci++) {
        for (uint8_t b = 0; b < slot->band_count; b++) {
            float ybot = bbox_y0 + ((float)b * band_height) - y_margin;
            float ytop = ybot + band_height + (y_margin * 2.0F);
            if (curve_y_max[ci] >= ybot && curve_y_min[ci] <= ytop) {
                yband_counts[b]++;
            }
            if (band_width > 0.0F) {
                float xleft = bbox_x0 + ((float)b * band_width) - x_margin;
                float xright = xleft + band_width + (x_margin * 2.0F);
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
    uint16_t yband_offsets[NT_FONT_MAX_BANDS] = {0};

    uint32_t local_pos = 0;
    /* Per-band curves sorted DESC by max-x (Y-bands) / max-y (X-bands) so the
     * shader's early-out matches reference Slug (SlugPixelShader.hlsl:187-192). */
    uint16_t band_sorted[NT_FONT_MAX_CURVES_PER_GLYPH];
    for (uint8_t b = 0; b < slot->band_count; b++) {
        yband_offsets[b] = (uint16_t)(local_pos / 2);
        float ybot = bbox_y0 + ((float)b * band_height) - y_margin;
        float ytop = ybot + band_height + (y_margin * 2.0F);
        uint16_t band_n = 0;
        for (uint16_t ci = 0; ci < curve_count; ci++) {
            if (curve_y_max[ci] < ybot || curve_y_min[ci] > ytop) {
                continue;
            }
            band_sorted[band_n++] = ci;
        }
        for (uint16_t i = 1; i < band_n; i++) {
            uint16_t key = band_sorted[i];
            float key_max = curve_x_max[key];
            int32_t j = (int32_t)i - 1;
            while (j >= 0 && curve_x_max[band_sorted[j]] < key_max) {
                band_sorted[j + 1] = band_sorted[j];
                j--;
            }
            band_sorted[j + 1] = key;
        }
        for (uint16_t i = 0; i < band_n; i++) {
            uint16_t ci = band_sorted[i];
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
    uint16_t xband_offsets[NT_FONT_MAX_BANDS] = {0};

    for (uint8_t b = 0; b < slot->band_count; b++) {
        xband_offsets[b] = (uint16_t)((local_pos - y_local_pos) / 2);
        if (band_width > 0.0F) {
            float xleft = bbox_x0 + ((float)b * band_width) - x_margin;
            float xright = xleft + band_width + (x_margin * 2.0F);
            uint16_t band_n = 0;
            for (uint16_t ci = 0; ci < curve_count; ci++) {
                if (curve_x_max[ci] < xleft || curve_x_min[ci] > xright) {
                    continue;
                }
                band_sorted[band_n++] = ci;
            }
            for (uint16_t i = 1; i < band_n; i++) {
                uint16_t key = band_sorted[i];
                float key_max = curve_y_max[key];
                int32_t j = (int32_t)i - 1;
                while (j >= 0 && curve_y_max[band_sorted[j]] < key_max) {
                    band_sorted[j + 1] = band_sorted[j];
                    j--;
                }
                band_sorted[j + 1] = key;
            }
            for (uint16_t i = 0; i < band_n; i++) {
                uint16_t ci = band_sorted[i];
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
    uint16_t band_upload[NT_FONT_MAX_BANDS * 2 * 2] = {0}; /* Y-bands + X-bands, RG16UI each */
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
    if (key_offset != 0) {
        cs->entry.bbox_x0 = clamp_i16(bbox_x0);
        cs->entry.bbox_y0 = clamp_i16(bbox_y0);
        cs->entry.bbox_x1 = clamp_i16(bbox_x1);
        cs->entry.bbox_y1 = clamp_i16(bbox_y1);
    } else {
        cs->entry.bbox_x0 = glyph->bbox_x0;
        cs->entry.bbox_y0 = glyph->bbox_y0;
        cs->entry.bbox_x1 = glyph->bbox_x1;
        cs->entry.bbox_y1 = glyph->bbox_y1;
    }
    cs->entry.is_tofu = false;
    cs->key_offset = key_offset;
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

    /* Fonts are zero-copy consumers — a no-op activator marks the slot READY,
     * on_resolve/on_cleanup manage the {blob,size} view, and PIN_BLOB keeps the
     * winning pack blob resident so live glyph reads never dangle. */
    nt_resource_set_activator(NT_ASSET_FONT, font_activate, font_deactivate);
    nt_resource_set_resolve_callbacks(NT_ASSET_FONT, font_on_resolve, font_on_cleanup);
    nt_resource_set_behavior_flags(NT_ASSET_FONT, NT_RESOURCE_BEHAVIOR_PIN_BLOB);

    s_font.last_resolve_epoch = 0;
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
        free(slot->measure_cache.key_hashes); /* SoA base — frees all 4 sub-arrays; NULL-safe */
        nt_gfx_destroy_texture(slot->curve_texture);
        nt_gfx_destroy_texture(slot->band_texture);
    }
    // #endregion
    free(s_font.slots);
    nt_pool_shutdown(&s_font.pool);
    memset(&s_font, 0, sizeof(s_font));
#ifdef NT_TEST_ACCESS
    font_test_shutdown_packs();
#endif
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

            clear_glyph_cache(slot); /* textures recreated — old cache entries point at stale GPU regions */
        }
    }
    // #endregion

    s_font.frame_counter++;

    /* Epoch-gate the resource rescan: O(1) when no published slot changed and no font's resource set
     * was mutated. The context-restore rebuild above still runs every frame; only the winner/metrics
     * reconciliation is gated. */
    uint32_t epoch = nt_resource_publication_epoch();
    if (epoch == s_font.last_resolve_epoch && !s_font.needs_resource_rescan) {
        return;
    }
    s_font.last_resolve_epoch = epoch;
    s_font.needs_resource_rescan = false;

    for (uint32_t i = 1; i <= s_font.pool.capacity; i++) {
        if (!nt_pool_slot_alive(&s_font.pool, i)) {
            continue;
        }
        nt_font_slot_t *slot = &s_font.slots[i];

        // #region Snapshot the current provider set (winner may have changed pack)
        bool res_now[NT_FONT_MAX_SOURCES_PER_FONT];
        uint32_t handle_now[NT_FONT_MAX_SOURCES_PER_FONT];
        uint8_t active_count = 0;
        for (uint8_t ri = 0; ri < slot->resource_count; ri++) {
            uint32_t bs = 0;
            const uint8_t *blob = font_provider_blob(slot->resources[ri], &bs);
            res_now[ri] = (blob != NULL && bs >= sizeof(NtFontAssetHeader));
            /* Winner-identity token: font_activate returns a UNIQUE seq per activation, so a winner-swap
             * or hot-reload changes the handle even when presence and header metrics are unchanged. */
            handle_now[ri] = res_now[ri] ? nt_resource_get(slot->resources[ri]) : 0U;
            if (res_now[ri]) {
                active_count++;
            }
        }
        // #endregion

        bool changed = false; /* provider set, identity, or metrics changed -> rebuild ascii + measure cache */
        bool need_flush = false;

        // #region Detect provider gain/loss/identity-swap vs the cached winner handles
        for (uint8_t ri = 0; ri < slot->resource_count; ri++) {
            bool was = (slot->resource_handles[ri] != 0);
            if (was != res_now[ri]) {
                changed = true;
                if (was) {
                    need_flush = true; /* lost a provider -> cached glyphs decoded from it are stale */
                }
            } else if (res_now[ri] && slot->resource_handles[ri] != handle_now[ri]) {
                /* Same presence, different activation (same-metrics override/patch pack or hot-reload of a
                 * repacked font): provider now views a different blob -> glyph/measure caches + ASCII index
                 * are decoded from the OLD blob and must be flushed even though metrics match. */
                changed = true;
                need_flush = true;
            }
        }
        // #endregion

        // #region Re-validate shared metrics against the current winning blobs
        const bool had_metrics = slot->metrics_set;
        for (uint8_t ri = 0; ri < slot->resource_count; ri++) {
            if (!res_now[ri]) {
                continue;
            }
            uint32_t bs = 0;
            const uint8_t *blob = font_provider_blob(slot->resources[ri], &bs);
            const NtFontAssetHeader *hdr = (const NtFontAssetHeader *)blob;
            const bool metrics_match = slot->metrics_set && slot->metrics.units_per_em == hdr->units_per_em && slot->metrics.ascent == hdr->ascent && slot->metrics.descent == hdr->descent &&
                                       slot->metrics.line_gap == hdr->line_gap && slot->metrics.underline_position == hdr->underline_position &&
                                       slot->metrics.underline_thickness == hdr->underline_thickness && slot->metrics.strikeout_position == hdr->strikeout_position &&
                                       slot->metrics.strikeout_size == hdr->strikeout_size;
            if (slot->metrics_set && metrics_match) {
                continue;
            }
            if (slot->metrics_set && !metrics_match) {
                /* Single-provider mismatch = hot-swap; multi-provider mismatch breaks the shared-metrics
                 * invariant (builder UPM-normalization prevents it). Flush on ANY mismatch — NT_ASSERT is
                 * a no-op in shipping, so gating the flush on it would keep caches baked against stale metrics. */
                NT_ASSERT(active_count == 1 && "font slot has multiple active resources with mismatched metrics — normalize in the builder");
                need_flush = true;
                changed = true;
            }
            slot->metrics.ascent = hdr->ascent;
            slot->metrics.descent = hdr->descent;
            slot->metrics.line_gap = hdr->line_gap;
            slot->metrics.units_per_em = hdr->units_per_em;
            slot->metrics.line_height = (int16_t)(hdr->ascent - hdr->descent + hdr->line_gap);
            /* v5 decoration metrics — renderer scales by size/units_per_em to place quads. */
            slot->metrics.underline_position = hdr->underline_position;
            slot->metrics.underline_thickness = hdr->underline_thickness;
            slot->metrics.strikeout_position = hdr->strikeout_position;
            slot->metrics.strikeout_size = hdr->strikeout_size;
            slot->metrics_set = true;
        }
        if (!had_metrics && slot->metrics_set) {
            changed = true; /* first resolve -> ascii index needs building */
        }
        // #endregion

        /* Commit the published winner handle (0 = absent) after all detection — one token carries
         * both presence (nonzero) and identity (value), so the next step detects swaps with no ABA. */
        for (uint8_t ri = 0; ri < slot->resource_count; ri++) {
            slot->resource_handles[ri] = handle_now[ri];
        }

        if (changed) {
            slot_refresh_after_resource_change(slot, need_flush);
        }
    }
}

/* Repopulate ascii_glyph_idx[] / ascii_glyph_res[] (per-slot fast-path
 * lookup for codepoints < 128) by scanning every active resource's glyph
 * table. First resource that owns a codepoint wins, matching
 * find_glyph_in_resources precedence. Also recomputes has_any_kern as a
 * by-product of the same scan. */
static void rebuild_ascii_index(nt_font_slot_t *slot) {
    slot->has_any_kern = false;
    for (size_t a = 0; a < (sizeof(slot->ascii_glyph_idx) / sizeof(slot->ascii_glyph_idx[0])); a++) {
        slot->ascii_glyph_idx[a] = NT_FONT_ASCII_IDX_NONE;
        slot->ascii_glyph_res[a] = 0U;
    }
    for (uint8_t k = 0; k < slot->resource_count; k++) {
        if (slot->resource_handles[k] == 0) {
            continue;
        }
        uint32_t scan_bs = 0;
        const uint8_t *scan_blob = font_provider_blob(slot->resources[k], &scan_bs);
        if (!scan_blob || scan_bs < sizeof(NtFontAssetHeader)) {
            continue;
        }
        const NtFontAssetHeader *scan_hdr = (const NtFontAssetHeader *)scan_blob;
        const NtFontGlyphEntry *scan_glyphs = (const NtFontGlyphEntry *)(scan_blob + sizeof(NtFontAssetHeader));
        for (uint32_t gi = 0; gi < scan_hdr->glyph_count; gi++) {
            const uint32_t cp = scan_glyphs[gi].codepoint;
            if (scan_glyphs[gi].kern_count != 0U) {
                slot->has_any_kern = true;
            }
            if (cp < 128U && slot->ascii_glyph_idx[cp] == NT_FONT_ASCII_IDX_NONE && gi < NT_FONT_ASCII_IDX_NONE) {
                slot->ascii_glyph_idx[cp] = (uint16_t)gi;
                slot->ascii_glyph_res[cp] = k;
            }
        }
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

    NT_ASSERT(desc->band_count > 0 && desc->band_count <= NT_FONT_MAX_BANDS);
    uint8_t band_count = desc->band_count;

    uint32_t id = nt_pool_alloc(&s_font.pool);
    if (id == 0) {
        NT_LOG_ERROR("font pool full -- increase max_fonts");
        return NT_FONT_INVALID;
    }

    uint32_t slot_index = nt_pool_slot_index(id);
    nt_font_slot_t *slot = &s_font.slots[slot_index];
    memset(slot, 0, sizeof(*slot));

    /* ASCII fast-path index — sentinel-fill before any resource loads.
     * memset(slot, 0) zeros it, but 0 is a legitimate glyph index, so we
     * need to explicitly set NT_FONT_ASCII_IDX_NONE on every entry. */
    for (size_t i = 0; i < (sizeof(slot->ascii_glyph_idx) / sizeof(slot->ascii_glyph_idx[0])); i++) {
        slot->ascii_glyph_idx[i] = NT_FONT_ASCII_IDX_NONE;
    }

    /* Measure cache — heap-allocated per-font as a single SoA block sized via
     * desc->measure_cache_size. 0 disables the cache entirely (measure_n
     * always runs the full compute path). Non-zero must be power-of-two for
     * the `key_hash & mask` indexing.
     *
     * Layout in one calloc'd block (ordered by descending alignment so each
     * sub-array starts properly aligned given calloc's max-align return):
     *   [ key_hashes : N × 8 B ][ size_bits : N × 4 B ][ values : N × 8 B ][ valid : N × 1 B ]
     * Total 21 N bytes; free via key_hashes (the base pointer). */
    if (desc->measure_cache_size != 0U) {
        const uint32_t cache_size = desc->measure_cache_size;
        NT_ASSERT((cache_size & (cache_size - 1U)) == 0U && "measure_cache_size must be power-of-two");
        NT_ASSERT(cache_size <= 32768U && "measure_cache_size exceeds the 32768 POT cap (uint16_t-sized field)");
        const size_t per_entry_bytes = NT_FONT_MEASURE_CACHE_ENTRY_BYTES;
        uint8_t *block = (uint8_t *)calloc((size_t)cache_size, per_entry_bytes);
        if (!block) {
            NT_LOG_ERROR("font measure_cache allocation failed (size=%u, bytes=%zu)", (unsigned)cache_size, (size_t)cache_size * per_entry_bytes);
            nt_pool_free(&s_font.pool, id);
            return NT_FONT_INVALID;
        }
        /* Partition the byte block into 4 type-cast sub-arrays. Calloc returns
         * memory aligned for any object type (8 B on x64), so the first array
         * (uint64_t) is correctly aligned; the others follow the size of the
         * preceding sub-array (8N, then 8N+4N, then 8N+4N+8N) and stay aligned
         * because each multiplier is a multiple of the next type's alignment. */
        /* NOLINTNEXTLINE(bugprone-casting-through-void) */
        slot->measure_cache.key_hashes = (uint64_t *)(void *)block;
        /* NOLINTNEXTLINE(bugprone-casting-through-void) */
        slot->measure_cache.size_bits = (uint32_t *)(void *)(block + ((size_t)cache_size * sizeof(uint64_t)));
        /* NOLINTNEXTLINE(bugprone-casting-through-void) */
        slot->measure_cache.values = (nt_text_size_t *)(void *)(block + ((size_t)cache_size * (sizeof(uint64_t) + sizeof(uint32_t))));
        slot->measure_cache.valid = block + ((size_t)cache_size * (sizeof(uint64_t) + sizeof(uint32_t) + sizeof(nt_text_size_t)));
        slot->measure_cache_size = cache_size;
        slot->measure_cache_mask = cache_size - 1U;
    }

    // #region Store config
    slot->curve_tex_width = desc->curve_texture_width;
    slot->curve_tex_height = desc->curve_texture_height;
    slot->band_tex_height = desc->band_texture_height;
    slot->band_count = band_count;
    slot->max_glyphs = desc->band_texture_height;
    // #endregion

    // #region Create GPU textures (once, never resized)
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
    free(slot->measure_cache.key_hashes); /* SoA base pointer — frees all 4 arrays in one block. NULL-safe. */
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

    /* Adding an already-published resource won't bump the publication epoch, so
     * force the next step to rescan instead of short-circuiting on the epoch gate. */
    s_font.needs_resource_rescan = true;
}

/* ---- Query ---- */

nt_font_metrics_t nt_font_get_metrics(nt_font_t font) {
    NT_ASSERT(s_font.initialized);
    NT_ASSERT(nt_pool_valid(&s_font.pool, font.id));
    if (!s_font.initialized || !nt_pool_valid(&s_font.pool, font.id)) {
        return (nt_font_metrics_t){0};
    }
    nt_font_slot_t *slot = get_slot(font);
    if (!slot->metrics_set) {
        return (nt_font_metrics_t){0}; /* resources not loaded yet */
    }
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
        .band_texels_used = (uint32_t)slot->glyphs_cached * slot->band_count * 2, /* Y + X bands */
        .band_texels_total = (uint32_t)slot->band_count * 2 * slot->band_tex_height,
    };
}

/* ---- Glyph lookup ----
 *
 * The implementation lives in nt_font_lookup_glyph_in_slot (slot-based);
 * the public handle-based variant resolves the slot once and delegates.
 * Hot-path callers (text renderers) hold the slot for the whole draw to
 * skip the per-codepoint pool_valid + get_slot. */

nt_font_slot_t *nt_font_get_slot(nt_font_t font) {
    if (!s_font.initialized || !nt_pool_valid(&s_font.pool, font.id)) {
        return NULL;
    }
    return get_slot(font);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
const nt_glyph_cache_entry_t *nt_font_lookup_glyph_offset(nt_font_slot_t *slot, uint32_t codepoint, int16_t key_offset) {
    NT_ASSERT(slot != NULL);

    // #region Cache hit check (hash table)
    nt_font_cache_slot_t *hit = hash_lookup(slot, codepoint, key_offset);
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
        nt_font_cache_slot_t *tofu = hash_lookup(slot, 0xFFFFFFFFU, 0);
        return tofu ? &tofu->entry : NULL;
    }
    // #endregion

    // #region Decode contour data
    uint32_t blob_size = 0;
    const uint8_t *blob = font_provider_blob(slot->resources[res_idx], &blob_size);
    NT_ASSERT(blob);
    if (!blob) {
        /* Provider vanished between find and decode — degrade to tofu, never deref NULL. */
        generate_tofu(slot);
        nt_font_cache_slot_t *tofu = hash_lookup(slot, 0xFFFFFFFFU, 0);
        return tofu ? &tofu->entry : NULL;
    }

    /* Contour data is at: data_offset + kern_count * sizeof(NtFontKernEntry) */
    uint32_t contour_offset = glyph_entry->data_offset + ((uint32_t)glyph_entry->kern_count * (uint32_t)sizeof(NtFontKernEntry));
    const uint8_t *contour_data = blob + contour_offset;

    NT_ASSERT(glyph_entry->curve_count <= NT_FONT_MAX_CURVES_PER_GLYPH);
    uint16_t curve_count = 0;
    if (glyph_entry->curve_count > 0) {
        /* Decode weight = the SAME (already quantized+saturated) key_offset the slot is keyed
         * by, so the cached geometry and the cache key can never disagree. */
        curve_count = decode_contours(contour_data, s_decode_curves, NT_FONT_MAX_CURVES_PER_GLYPH, (float)key_offset);
    }
    // #endregion

    // #region Upload, allocate slot, fill cache
    uint16_t cache_idx = upload_glyph(slot, glyph_entry, s_decode_curves, curve_count, res_idx, key_offset);
    hash_insert(slot, codepoint, key_offset, cache_idx);
    // #endregion

    return &slot->cache[cache_idx].entry;
}

const nt_glyph_cache_entry_t *nt_font_lookup_glyph_in_slot(nt_font_slot_t *slot, uint32_t codepoint) { return nt_font_lookup_glyph_offset(slot, codepoint, 0); }

int16_t nt_font_quantize_weight(float weight_units) {
    if (!isfinite(weight_units)) {
        return 0;
    }
    /* Clamp to the int16 range BEFORE lrintf: on 32-bit long (wasm/Win32) lrintf of a value past
     * LONG_MAX is UB, and set_weight is unclamped, so an absurd weight could reach it pre-saturation. */
    if (weight_units > 32767.0F) {
        weight_units = 32767.0F;
    } else if (weight_units < -32768.0F) {
        weight_units = -32768.0F;
    }
    /* Round to the quant step (nearby weights share a slot), then saturate to int16
     * so an absurd weight can never wrap to a colliding bucket. */
    long steps = lrintf(weight_units / (float)NT_FONT_WEIGHT_QUANT_STEP);
    long q = steps * NT_FONT_WEIGHT_QUANT_STEP;
    if (q > 32767L) {
        return 32767;
    }
    if (q < -32768L) {
        return -32768;
    }
    return (int16_t)q;
}

const nt_glyph_cache_entry_t *nt_font_lookup_glyph(nt_font_t font, uint32_t codepoint) {
    NT_ASSERT(s_font.initialized);
    NT_ASSERT(nt_pool_valid(&s_font.pool, font.id));
    return nt_font_lookup_glyph_in_slot(get_slot(font), codepoint);
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

int16_t nt_font_get_kern_in_slot(const nt_font_slot_t *slot, uint32_t left_codepoint, uint32_t right_codepoint) {
    NT_ASSERT(slot != NULL);

    /* Fast-skip: most Latin fonts have no kern table. has_any_kern is set
     * during nt_font_step when a resource loads, so this branch is a single
     * predictable byte read — no resource loop, no bsearch. */
    if (!slot->has_any_kern) {
        return 0;
    }

    /* Find left glyph in resources to access its kern entries */
    for (uint8_t ri = 0; ri < slot->resource_count; ri++) {
        if (slot->resource_handles[ri] == 0) {
            continue;
        }
        uint32_t blob_size = 0;
        const uint8_t *blob = font_provider_blob(slot->resources[ri], &blob_size);
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

int16_t nt_font_get_kern(nt_font_t font, uint32_t left_codepoint, uint32_t right_codepoint) {
    NT_ASSERT(s_font.initialized);
    NT_ASSERT(nt_pool_valid(&s_font.pool, font.id));
    return nt_font_get_kern_in_slot(get_slot(font), left_codepoint, right_codepoint);
}

/* ---- Pre-flush callback ---- */

void nt_font_set_pre_flush_callback(nt_font_pre_flush_fn fn) { s_font.pre_flush_fn = fn; }

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

/* Bit-cast a float size to uint32_t for exact cache-key comparison.
 * float→uint32 round-trip is loss-free (same 4 bytes); memcpy avoids UB
 * from a direct pointer cast (strict-aliasing rule). */
static inline uint32_t measure_size_bits(float size) {
    uint32_t bits;
    memcpy(&bits, &size, sizeof(bits));
    return bits;
}

/* ---- Internal hot-path helpers (skip public-API pool_valid + get_slot) ----
 *
 * The measure / draw inner loop runs once per codepoint. The public
 * nt_font_lookup_metrics / nt_font_get_kern wrap pool_valid + get_slot per
 * call — fine for occasional use, wasteful in tight loops. These internal
 * variants take an already-resolved slot pointer (and an already-found
 * left_entry + blob for kern) and skip the per-call validation.
 *
 * They also enable Opt C: the left_entry from the previous iteration
 * (we just looked up its metrics) is reused for the kern bsearch — saving
 * one of three bsearches per character pair.
 */

typedef struct {
    const NtFontGlyphEntry *entry; /* NULL on tofu fallback */
    const uint8_t *blob;
    uint32_t blob_size;
} nt_font_glyph_lookup_t;

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static nt_font_glyph_lookup_t lookup_glyph_entry_in_slot(const nt_font_slot_t *slot, uint32_t codepoint) {
    nt_font_glyph_lookup_t out = {0};

    /* ASCII fast-path: O(1) array lookup instead of per-resource bsearch. */
    if (codepoint < 128U) {
        const uint16_t gi = slot->ascii_glyph_idx[codepoint];
        if (gi != NT_FONT_ASCII_IDX_NONE) {
            const uint8_t ri = slot->ascii_glyph_res[codepoint];
            uint32_t blob_size = 0;
            const uint8_t *blob = font_provider_blob(slot->resources[ri], &blob_size);
            if (blob && blob_size >= sizeof(NtFontAssetHeader)) {
                const NtFontAssetHeader *hdr = (const NtFontAssetHeader *)blob;
                NT_ASSERT(gi < hdr->glyph_count); /* see find_glyph_in_resources rationale */
                if (gi < hdr->glyph_count) {
                    const NtFontGlyphEntry *glyphs = (const NtFontGlyphEntry *)(blob + sizeof(NtFontAssetHeader));
                    out.entry = glyphs + gi;
                    out.blob = blob;
                    out.blob_size = blob_size;
                    return out;
                }
            }
        }
    }

    for (uint8_t i = 0; i < slot->resource_count; i++) {
        if (slot->resource_handles[i] == 0) {
            continue;
        }
        uint32_t blob_size = 0;
        const uint8_t *blob = font_provider_blob(slot->resources[i], &blob_size);
        if (!blob) {
            continue;
        }
        const NtFontGlyphEntry *entry = find_glyph_in_pack(blob, blob_size, codepoint);
        if (entry) {
            out.entry = entry;
            out.blob = blob;
            out.blob_size = blob_size;
            return out;
        }
    }
    return out;
}

/* Kern lookup that REUSES the already-found left entry/blob from the
 * previous iteration. Skips the leftmost bsearch — 1 of 3 inside the
 * original get_kern. Caller MUST verify left->entry != NULL and
 * left->entry->kern_count > 0 before calling. */
static int16_t kern_with_left_in_slot(const nt_font_glyph_lookup_t *left, uint32_t right_codepoint) {
    /* Right glyph index within the same resource as the left entry. */
    int32_t right_idx = find_glyph_index(left->blob, left->blob_size, right_codepoint);
    if (right_idx < 0) {
        return 0;
    }
    const NtFontKernEntry *kerns = (const NtFontKernEntry *)(left->blob + left->entry->data_offset);
    uint16_t right_glyph_index = (uint16_t)right_idx;
    const NtFontKernEntry *found = (const NtFontKernEntry *)bsearch(&right_glyph_index, kerns, left->entry->kern_count, sizeof(NtFontKernEntry), compare_kern_right);
    if (found) {
        return found->value;
    }
    return 0;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
nt_text_size_t nt_font_measure_n(nt_font_t font, const char *utf8, size_t len, float size, float letter_tracking) {
    nt_text_size_t result = {0.0F, 0.0F};

    /* Edge cases. NULL utf8 with len > 0 is treated as empty input
     * defensively — this function sits at a Clay-callback boundary (system
     * edge per AGENTS.md "validate at boundaries"), and silently returning
     * {0,0} is safer than crashing the UI declaration phase on a malformed
     * Clay_StringSlice. */
    if (len == 0U || utf8 == NULL) {
        return result;
    }
    if (!nt_pool_valid(&s_font.pool, font.id)) {
        return result;
    }

    nt_font_slot_t *slot = get_slot(font);
    if (!slot->metrics_set) {
        return result; /* font not yet resolved */
    }

    /* Non-zero tracking changes geometry per call; cache key doesn't include
     * it (would double the SoA), so we skip the cache. */
    const bool cache_enabled = (slot->measure_cache.key_hashes != NULL) && (letter_tracking == 0.0F);
    const uint64_t key_hash = cache_enabled ? nt_hash64((const void *)utf8, (uint32_t)len).value : 0U;
    const uint32_t size_bits = measure_size_bits(size);
    const uint32_t slot_index = (uint32_t)key_hash & slot->measure_cache_mask;

    /* Cache lookup — direct-mapped, replace-on-collision (NOT LRU). SoA:
     * the three hot bands (key_hashes, size_bits, valid) sit in tight bands
     * across all entries, so 8 adjacent slots' header data fits in one
     * cache line. values[] is only touched on a confirmed hit. */
    if (cache_enabled && slot->measure_cache.valid[slot_index] && slot->measure_cache.key_hashes[slot_index] == key_hash && slot->measure_cache.size_bits[slot_index] == size_bits) {
#ifdef NT_TEST_ACCESS
        slot->test_measure_cache_hits++;
#endif
        return slot->measure_cache.values[slot_index];
    }

#ifdef NT_TEST_ACCESS
    if (cache_enabled) {
        slot->test_measure_cache_misses++;
    }
#endif

    /* Cache miss — run full UTF-8 measurement, length-bounded. Hot loop —
     * Opt A: gate the kern lookup on slot->has_any_kern (font-wide) AND on
     * the prev glyph's local kern_count. Opt B: skipped per-codepoint
     * pool_valid + get_slot by working off the already-resolved slot. Opt C:
     * reuse the previous iteration's left entry/blob — saves 1 of 3
     * bsearches inside the original kern lookup. */
    const float scale = size / (float)slot->metrics.units_per_em;
    const bool font_has_kern = slot->has_any_kern;
    /* Cache tofu metrics — slot-wide constants, recomputed per call but never per codepoint.
     * Compute advance in float (units_per_em * 0.5) instead of integer divide-then-promote;
     * matches nt_font_lookup_metrics for power-of-2 EMs (the common case). */
    const float tofu_advance_px = (float)slot->metrics.units_per_em * 0.5F * scale;
    const float tofu_min_y_px = (float)slot->metrics.descent * scale;
    const float tofu_max_y_px = (float)slot->metrics.ascent * scale;

    uint32_t state = NT_UTF8_ACCEPT;
    uint32_t codepoint = 0;
    nt_font_glyph_lookup_t prev_lookup = {0};
    float pen_x = 0.0F;
    float min_y = 0.0F;
    float max_y = 0.0F;
    bool had_glyph = false;

    const uint8_t *p = (const uint8_t *)utf8;
    const uint8_t *end = p + len;

    for (; p < end; p++) {
        if (nt_utf8_decode(&state, &codepoint, *p) != NT_UTF8_ACCEPT) {
            if (state == NT_UTF8_REJECT) {
                state = NT_UTF8_ACCEPT; /* recover: skip bad byte, continue parsing */
            }
            continue;
        }

        /* Match renderer: \r adds no width AND breaks the kerning chain
         * (nt_text_renderer_draw_n resets prev_cp on \r). Keep had_glyph
         * so letter_tracking still applies on the next glyph. Clay splits
         * on \n so multi-line slices may carry \r from CRLF. */
        if (codepoint == '\r') {
            prev_lookup = (nt_font_glyph_lookup_t){0};
            continue;
        }

        if (had_glyph) {
            pen_x += letter_tracking;
        }

        /* Kern lookup — fully skipped on kerning-less fonts; for fonts that
         * have kerning, also skipped per-pair if the prev glyph has no kern
         * entries of its own. */
        if (font_has_kern && prev_lookup.entry != NULL && prev_lookup.entry->kern_count != 0U) {
            int16_t kern = kern_with_left_in_slot(&prev_lookup, codepoint);
            pen_x += (float)kern * scale;
        }

        nt_font_glyph_lookup_t lookup = lookup_glyph_entry_in_slot(slot, codepoint);
        if (lookup.entry != NULL) {
            const NtFontGlyphEntry *g = lookup.entry;
            const float gy0 = (float)g->bbox_y0 * scale;
            const float gy1 = (float)g->bbox_y1 * scale;
            if (gy0 < min_y) {
                min_y = gy0;
            }
            if (gy1 > max_y) {
                max_y = gy1;
            }
            pen_x += (float)g->advance * scale;
        } else {
            /* Tofu fallback — same dimensions as nt_font_lookup_metrics. */
            if (tofu_min_y_px < min_y) {
                min_y = tofu_min_y_px;
            }
            if (tofu_max_y_px > max_y) {
                max_y = tofu_max_y_px;
            }
            pen_x += tofu_advance_px;
        }

        prev_lookup = lookup;
        had_glyph = true;
    }

    result.width = pen_x;
    result.height = max_y - min_y;
    if (result.height < size) {
        result.height = size; /* Minimum height = requested size */
    }

    /* Cache write — direct-mapped, replace on collision. Skipped when cache
     * is disabled. SoA: each field writes into its own parallel array. */
    if (cache_enabled) {
        slot->measure_cache.key_hashes[slot_index] = key_hash;
        slot->measure_cache.size_bits[slot_index] = size_bits;
        slot->measure_cache.values[slot_index] = result;
        slot->measure_cache.valid[slot_index] = 1U;
    }

    return result;
}

/* Wrapper — NUL-terminated convenience. */
nt_text_size_t nt_font_measure(nt_font_t font, const char *utf8, float size, float letter_tracking) { return nt_font_measure_n(font, utf8, utf8 ? strlen(utf8) : 0U, size, letter_tracking); }
// #endregion

// #region Measure cache invalidation
/* Both variants no-op when the module is uninit or the handle is invalid —
 * callers may invoke between frames or after partial teardown. */

/* One memset over the contiguous SoA block (base = key_hashes). */
static void measure_cache_clear(nt_font_slot_t *slot) { memset(slot->measure_cache.key_hashes, 0, (size_t)slot->measure_cache_size * NT_FONT_MEASURE_CACHE_ENTRY_BYTES); }

void nt_font_measure_invalidate_cache(void) {
    if (!s_font.initialized) {
        return;
    }
    for (uint32_t i = 1; i <= s_font.pool.capacity; i++) {
        if (!nt_pool_slot_alive(&s_font.pool, i)) {
            continue;
        }
        nt_font_slot_t *slot = &s_font.slots[i];
        if (slot->measure_cache.key_hashes == NULL) {
            continue; /* cache disabled for this font */
        }
        measure_cache_clear(slot);
    }
}

void nt_font_measure_invalidate(nt_font_t font) {
    if (!s_font.initialized || !nt_pool_valid(&s_font.pool, font.id)) {
        return;
    }
    nt_font_slot_t *slot = get_slot(font);
    if (slot->measure_cache.key_hashes == NULL) {
        return;
    }
    measure_cache_clear(slot);
}
// #endregion

/* ---- Test-only: register font data for headless testing ---- */

#ifdef NT_TEST_ACCESS

// #region Test-only real-pack registry
/* Fonts read bytes zero-copy from a RESIDENT pack blob (virtual packs carry none), so tests wrap a
 * font blob in a real single-asset .ntpack, mount + parse it, and return a token. parse_pack keeps a
 * zero-copy pointer, so the wrapper is retained until deactivate/shutdown; keyed by token for remount. */

#define NT_FONT_TEST_MAX_PACKS 128
typedef struct {
    uint8_t *pack_blob;
    nt_hash64_t rid;
    nt_hash32_t pid;
    bool mounted;
    bool used;
} nt_font_test_pack_t;
static nt_font_test_pack_t s_test_packs[NT_FONT_TEST_MAX_PACKS];
static uint32_t s_test_pack_count;

static uint8_t *font_test_build_pack(uint64_t rid, const uint8_t *data, uint32_t size, uint32_t *out_size) {
    uint32_t raw_header = (uint32_t)(sizeof(NtPackHeader) + sizeof(NtAssetEntry));
    uint32_t header_size = (raw_header + (NT_PACK_DATA_ALIGN - 1U)) & ~(NT_PACK_DATA_ALIGN - 1U);
    uint32_t aligned_data = (size + (NT_PACK_ASSET_ALIGN - 1U)) & ~(NT_PACK_ASSET_ALIGN - 1U);
    uint32_t total_size = header_size + aligned_data;

    uint8_t *blob = (uint8_t *)calloc(1, total_size);
    NT_ASSERT(blob);

    NtPackHeader *h = (NtPackHeader *)blob;
    h->magic = NT_PACK_MAGIC;
    h->version = NT_PACK_VERSION;
    h->asset_count = 1;
    h->header_size = header_size;
    h->total_size = total_size;

    NtAssetEntry *entry = (NtAssetEntry *)(blob + sizeof(NtPackHeader));
    entry->resource_id = rid;
    entry->format_version = 1;
    entry->asset_type = NT_ASSET_FONT;
    entry->_pad = 0;
    entry->meta_offset = 0;
    entry->offset = header_size;
    entry->size = size;

    memcpy(blob + header_size, data, size);
    h->checksum = nt_crc32(blob + header_size, aligned_data);

    *out_size = total_size;
    return blob;
}

static void font_test_mount_parse(nt_font_test_pack_t *tp, const uint8_t *data, uint32_t size) {
    uint32_t pack_size = 0;
    tp->pack_blob = font_test_build_pack(tp->rid.value, data, size, &pack_size);
    nt_resource_mount(tp->pid, 0);
    nt_resource_parse_pack(tp->pid, tp->pack_blob, pack_size);
    tp->mounted = true;
}

uint32_t nt_font_test_register_data(const uint8_t *data, uint32_t size) {
    NT_ASSERT(s_test_pack_count < NT_FONT_TEST_MAX_PACKS);
    uint32_t idx = s_test_pack_count++;
    nt_font_test_pack_t *tp = &s_test_packs[idx];
    memset(tp, 0, sizeof(*tp));
    char name[32];
    (void)snprintf(name, sizeof(name), "font_test_pack_%u", idx);
    tp->pid = nt_hash32_str(name);
    tp->rid = nt_hash64_str(name);
    tp->used = true;
    font_test_mount_parse(tp, data, size);
    return idx + 1; /* 1-based token (0 = invalid) */
}

nt_resource_t nt_font_test_resource(uint32_t token) {
    NT_ASSERT(token != 0 && token <= s_test_pack_count);
    return nt_resource_request(s_test_packs[token - 1].rid, NT_ASSET_FONT);
}

void nt_font_test_deactivate(uint32_t token) {
    NT_ASSERT(token != 0 && token <= s_test_pack_count);
    nt_font_test_pack_t *tp = &s_test_packs[token - 1];
    if (tp->mounted) {
        nt_resource_unmount(tp->pid);
        tp->mounted = false;
    }
    free(tp->pack_blob);
    tp->pack_blob = NULL;
}

uint32_t nt_font_test_register_shared_rid(uint32_t base_token, const uint8_t *data, uint32_t size) {
    NT_ASSERT(base_token != 0 && base_token <= s_test_pack_count);
    NT_ASSERT(s_test_pack_count < NT_FONT_TEST_MAX_PACKS);
    uint32_t idx = s_test_pack_count++;
    nt_font_test_pack_t *tp = &s_test_packs[idx];
    memset(tp, 0, sizeof(*tp));
    char name[32];
    (void)snprintf(name, sizeof(name), "font_test_pack_%u", idx);
    tp->pid = nt_hash32_str(name);              /* fresh pack id */
    tp->rid = s_test_packs[base_token - 1].rid; /* shared resource id -> both packs publish one resource */
    tp->used = true;
    font_test_mount_parse(tp, data, size);
    return idx + 1;
}

void nt_font_test_reregister(uint32_t token, const uint8_t *data, uint32_t size) {
    NT_ASSERT(token != 0 && token <= s_test_pack_count);
    nt_font_test_pack_t *tp = &s_test_packs[token - 1];
    if (tp->mounted) {
        nt_resource_unmount(tp->pid);
        tp->mounted = false;
    }
    free(tp->pack_blob);
    tp->pack_blob = NULL;
    font_test_mount_parse(tp, data, size); /* same pid+rid -> re-resolves through the existing slot */
}

static void font_test_shutdown_packs(void) {
    /* Free the caller-owned pack wrappers. Safe regardless of tearDown ordering:
     * these packs are io_type == NT_IO_NONE, so nt_resource_shutdown/unmount never
     * frees the blob (no double-free) and never dereferences it (no dangling read). */
    for (uint32_t i = 0; i < s_test_pack_count; i++) {
        free(s_test_packs[i].pack_blob);
        s_test_packs[i].pack_blob = NULL;
        s_test_packs[i].mounted = false;
    }
    s_test_pack_count = 0;
}
// #endregion

uint32_t nt_font_test_measure_cache_hits(nt_font_t font) {
    if (!s_font.initialized || !nt_pool_valid(&s_font.pool, font.id)) {
        return 0U;
    }
    return get_slot(font)->test_measure_cache_hits;
}

uint32_t nt_font_test_measure_cache_misses(nt_font_t font) {
    if (!s_font.initialized || !nt_pool_valid(&s_font.pool, font.id)) {
        return 0U;
    }
    return get_slot(font)->test_measure_cache_misses;
}

void nt_font_test_reset_measure_counters(void) {
    if (!s_font.initialized) {
        return;
    }
    for (uint32_t i = 1; i <= s_font.pool.capacity; i++) {
        if (!nt_pool_slot_alive(&s_font.pool, i)) {
            continue;
        }
        s_font.slots[i].test_measure_cache_hits = 0U;
        s_font.slots[i].test_measure_cache_misses = 0U;
    }
}

void nt_font_test_set_metrics(nt_font_t font, uint16_t units_per_em, int16_t ascent, int16_t descent, int16_t line_height) {
    NT_ASSERT(nt_pool_valid(&s_font.pool, font.id) && "nt_font_test_set_metrics: invalid font handle");
    nt_font_slot_t *slot = get_slot(font);
    slot->metrics.units_per_em = units_per_em;
    slot->metrics.ascent = ascent;
    slot->metrics.descent = descent;
    slot->metrics.line_height = line_height;
    slot->metrics_set = true;
}

uint16_t nt_font_test_offset_ring(const int32_t *sx, const int32_t *sy, const uint8_t *son, uint16_t n, float weight, int32_t *dx, int32_t *dy, uint8_t *don) {
    double a0 = contour_signed_area(sx, sy, n);
    return offset_with_joins(sx, sy, son, n, dx, dy, don, weight, a0);
}

bool nt_font_test_contour_self_intersects(const int32_t *x, const int32_t *y, uint16_t n) { return ring_self_intersects(x, y, n); }

bool nt_font_test_grower_loop_kept(const float *orig_curves, uint16_t orig_n, const int32_t *loop_x, const int32_t *loop_y, uint16_t loop_n, double r_off) {
    double cx = 0.0;
    double cy = 0.0;
    if (poly_inradius_pole(loop_x, loop_y, loop_n, &cx, &cy) <= 0.0) {
        return false; /* no interior pole → dropped (mirrors resolve_and_emit) */
    }
    const nt_curve_t *cv = (const nt_curve_t *)orig_curves; /* [p0x..p2y] == nt_curve_t layout */
    bool in_dilation = orig_glyph_inside(cx, cy, cv, orig_n) || (orig_glyph_dist(cx, cy, cv, orig_n) <= r_off);
    return !in_dilation;
}

uint16_t nt_font_test_decode_contours(const uint8_t *contour_data, float weight, float *out_curves, uint16_t max_curves) {
    uint16_t count = decode_contours(contour_data, s_decode_curves, NT_FONT_MAX_CURVES_PER_GLYPH, weight);
    if (count > max_curves) {
        count = max_curves;
    }
    for (uint16_t i = 0; i < count; i++) {
        float *o = out_curves + ((size_t)i * 6);
        o[0] = s_decode_curves[i].p0x;
        o[1] = s_decode_curves[i].p0y;
        o[2] = s_decode_curves[i].p1x;
        o[3] = s_decode_curves[i].p1y;
        o[4] = s_decode_curves[i].p2x;
        o[5] = s_decode_curves[i].p2y;
    }
    return count;
}
#endif
