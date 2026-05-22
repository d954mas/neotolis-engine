#ifndef NT_FONT_H
#define NT_FONT_H

#include <stddef.h>

#include "core/nt_types.h"
#include "graphics/nt_gfx.h"
#include "resource/nt_resource.h"

#define NT_FONT_MAX_SOURCES_PER_FONT 8
#define NT_FONT_MAX_BANDS 16

typedef struct {
    uint32_t id;
} nt_font_t;

#define NT_FONT_INVALID ((nt_font_t){0})

typedef struct {
    uint16_t max_fonts;
} nt_font_desc_t;

static inline nt_font_desc_t nt_font_desc_defaults(void) {
    return (nt_font_desc_t){
        .max_fonts = 16,
    };
}

typedef struct {
    uint16_t curve_texture_width; /* POT recommended */
    uint16_t curve_texture_height;
    uint16_t band_texture_height; /* = max_glyphs */
    uint8_t band_count;
    uint16_t measure_cache_size; /* POT if non-zero; 0 = disabled; max 32768 */
} nt_font_create_desc_t;

static inline nt_font_create_desc_t nt_font_create_desc_defaults(void) {
    return (nt_font_create_desc_t){
        .curve_texture_width = 1024,
        .curve_texture_height = 512,
        .band_texture_height = 256,
        .band_count = 8,
        .measure_cache_size = 1024,
    };
}

typedef struct {
    int16_t ascent;
    int16_t descent;
    int16_t line_gap;
    uint16_t units_per_em;
    int16_t line_height; /* ascent - descent + line_gap */
} nt_font_metrics_t;

typedef struct {
    uint16_t glyphs_cached;
    uint16_t max_glyphs;
    uint32_t curve_texels_used;
    uint32_t curve_texels_total;
    uint32_t band_texels_used;
    uint32_t band_texels_total;
} nt_font_stats_t;

typedef struct {
    uint32_t codepoint;
    uint32_t curve_offset;   /* Y-band curve texels */
    uint32_t curve_offset_x; /* X-band curve texels */
    uint16_t curve_count;
    uint16_t band_row;
    int16_t advance; /* font units */
    int16_t bbox_x0;
    int16_t bbox_y0;
    int16_t bbox_x1;
    int16_t bbox_y1;
    bool is_tofu;
} nt_glyph_cache_entry_t;

typedef struct {
    int16_t advance;
    int16_t bbox_x0;
    int16_t bbox_y0;
    int16_t bbox_x1;
    int16_t bbox_y1;
    bool found; /* false = tofu fallback metrics */
} nt_glyph_metrics_t;

/* CPU-only metric lookup — no GPU, no cache touch. Missing → tofu metrics. */
nt_glyph_metrics_t nt_font_lookup_metrics(nt_font_t font, uint32_t codepoint);

typedef struct {
    float width;
    float height;
} nt_text_size_t;

/* Length-aware (Clay_StringSlice contract). NULL/len=0 → {0,0}; UTF-8 cut at
 * `len` boundary → trailing partial codepoint dropped; embedded NUL is a
 * normal codepoint (the NUL-terminated wrapper below stops at it via strlen).
 * Single-line measurement (no \n handling -- Clay tokenizes per line).
 *
 * letter_tracking: EXTRA px added between glyphs (additive, NOT absolute).
 *   0 = font's natural glyph advance. Positive = loose, negative = tight.
 *   Adds (N-1) * tracking px for N visible codepoints.
 *
 * Non-zero tracking bypasses the measure cache. */
nt_text_size_t nt_font_measure_n(nt_font_t font, const char *utf8, size_t len, float size, float letter_tracking);
nt_text_size_t nt_font_measure(nt_font_t font, const char *utf8, float size, float letter_tracking);

/* Both no-op on invalid/destroyed handles — safe to call from teardown. */
void nt_font_measure_invalidate_cache(void);
void nt_font_measure_invalidate(nt_font_t font);

nt_result_t nt_font_init(const nt_font_desc_t *desc);
void nt_font_shutdown(void);
void nt_font_step(void);

nt_font_t nt_font_create(const nt_font_create_desc_t *desc);
void nt_font_destroy(nt_font_t font);
bool nt_font_valid(nt_font_t font);

void nt_font_add(nt_font_t font, nt_resource_t resource);

nt_font_metrics_t nt_font_get_metrics(nt_font_t font);
nt_font_stats_t nt_font_get_stats(nt_font_t font);

/* Pointer into internal cache — valid until next eviction/flush. Copy
 * immediately. Per-codepoint loops should use the slot variant in nt_font_hot.h. */
const nt_glyph_cache_entry_t *nt_font_lookup_glyph(nt_font_t font, uint32_t codepoint);

nt_texture_t nt_font_get_curve_texture(nt_font_t font);
nt_texture_t nt_font_get_band_texture(nt_font_t font);
uint8_t nt_font_get_band_count(nt_font_t font);
uint16_t nt_font_get_curve_texture_width(nt_font_t font);

uint32_t nt_font_get_cache_generation(nt_font_t font);

/* Fires before the glyph cache wipes — consumers must drain staging buffers
 * holding glyph texture offsets before they go stale. */
typedef void (*nt_font_pre_flush_fn)(void);
void nt_font_set_pre_flush_callback(nt_font_pre_flush_fn fn);

int16_t nt_font_get_kern(nt_font_t font, uint32_t left_codepoint, uint32_t right_codepoint);

// #region test_access
#ifdef NT_TEST_ACCESS
uint32_t nt_font_test_register_data(const uint8_t *data, uint32_t size);

/* Imitate the FILE-pack deactivator path (skipped for VIRTUAL packs). */
void nt_font_test_deactivate(uint32_t runtime_handle);

uint32_t nt_font_test_measure_cache_hits(nt_font_t font);
uint32_t nt_font_test_measure_cache_misses(nt_font_t font);
void nt_font_test_reset_measure_counters(void);
#endif
// #endregion

#endif /* NT_FONT_H */
