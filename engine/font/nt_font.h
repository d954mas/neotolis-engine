#ifndef NT_FONT_H
#define NT_FONT_H

#include <stddef.h>

#include "core/nt_types.h"
#include "graphics/nt_gfx.h"
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
    uint16_t max_glyphs;         /* glyph-cache slots incl. tofu, one curve-texture row each (16 KB); 2..2048 */
    uint16_t measure_cache_size; /* POT if non-zero; 0 = disabled; max 32768 */
    uint8_t max_payloads;        /* retained compact payloads; positive, no hidden growth */
} nt_font_create_desc_t;

static inline nt_font_create_desc_t nt_font_create_desc_defaults(void) {
    return (nt_font_create_desc_t){
        .max_glyphs = 256,
        .measure_cache_size = 1024,
        .max_payloads = 8,
    };
}

typedef struct {
    int16_t ascent;
    int16_t descent;
    int16_t line_gap;
    uint16_t units_per_em;
    int16_t line_height;         /* ascent - descent + line_gap */
    int16_t underline_position;  /* v5: top of underline, below baseline (font units) */
    int16_t underline_thickness; /* v5: underline thickness (font units) */
    int16_t strikeout_position;  /* v5: above baseline (font units) */
    int16_t strikeout_size;      /* v5: strikeout thickness */
} nt_font_metrics_t;

typedef struct {
    uint16_t glyphs_cached;
    uint16_t max_glyphs;
    uint32_t curve_texels_used;
    uint32_t curve_texels_total;
} nt_font_stats_t;

typedef struct {
    uint32_t codepoint;
    uint16_t curve_count;
    uint16_t band_row;
    uint8_t band_count; /* NT_FONT_MAX_BANDS, halved until the glyph fits its row */
    int16_t advance;    /* font units */
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

/* Independent of the resource registry. NULL desc, zero capacity, double init and OOM assert. */
void nt_font_init(const nt_font_desc_t *desc);
void nt_font_shutdown(void);
/* Call once per frame after gfx begin_frame, outside a pass and before any text: recovery replaces the
 * GPU texture and clears the glyph cache that this frame's draws will sample. */
void nt_font_step(void);

/* Invalid configuration, pool exhaustion and allocation failure assert. */
nt_font_t nt_font_create(const nt_font_create_desc_t *desc);
void nt_font_destroy(nt_font_t font);
bool nt_font_valid(nt_font_t font);

/* Copies a trusted FONT payload before returning; caller bytes may then be freed.
 * Outside a render pass, before layout. Overlap, incompatible metrics, invalid structure,
 * capacity exhaustion and OOM assert. Existing glyphs stay valid; destroy frees all copies. */
void nt_font_fill(nt_font_t font, const uint8_t *data, uint32_t size);
/* Zero before the first fill; advances on fill only, not glyph-cache eviction or GPU recovery. */
uint32_t nt_font_content_revision(nt_font_t font);

nt_font_metrics_t nt_font_get_metrics(nt_font_t font);
nt_font_stats_t nt_font_get_stats(nt_font_t font);

/* Returns NULL while the texture or font metrics are unavailable. Tofu stands in for a missing glyph and
 * for a miss while every slot is in use this gfx frame (raise max_glyphs).
 * Borrowed cache entry, valid until eviction/clear or font destruction; copy immediately, never free.
 *
 * Per-codepoint loops use nt_font_hot.h after checking both textures are ready. */
const nt_glyph_cache_entry_t *nt_font_lookup_glyph(nt_font_t font, uint32_t codepoint);

nt_texture_t nt_font_get_curve_texture(nt_font_t font);

int16_t nt_font_get_kern(nt_font_t font, uint32_t left_codepoint, uint32_t right_codepoint);

// #region test_access
#ifdef NT_TEST_ACCESS
uint32_t nt_font_test_measure_cache_hits(nt_font_t font);
uint32_t nt_font_test_measure_cache_misses(nt_font_t font);
void nt_font_test_reset_measure_counters(void);

uint16_t nt_font_test_decode_contours(const uint8_t *contour_data, float weight, float *out_curves, uint16_t max_curves);
#if NT_FONT_EMBOLDEN_ENABLED
/* Embolden + offset-resolution test hooks. offset_ring writes the offset
 * point ring (with reflex joins) to a SEPARATE dst ring, returns dst point count; decode
 * fills out_curves flat as [p0x,p0y,p1x,p1y,p2x,p2y] per curve (full resolve), returns count. */
uint16_t nt_font_test_offset_ring(const int32_t *sx, const int32_t *sy, const uint8_t *son, uint16_t n, float weight, int32_t *dx, int32_t *dy, uint8_t *don);
bool nt_font_test_contour_self_intersects(const int32_t *x, const int32_t *y, uint16_t n);
/* The grower opposite-loop dilation-membership decision (the '@' seal-fill fix core): true if the loop
 * (int point ring) is KEPT as a real counter — its pole lies OUTSIDE the true dilation of the ORIGINAL
 * glyph (flat [p0x..p2y] curves) by r_off. false = dropped (fills solid). */
bool nt_font_test_grower_loop_kept(const float *orig_curves, uint16_t orig_n, const int32_t *loop_x, const int32_t *loop_y, uint16_t loop_n, double r_off);
#endif
#endif
// #endregion

#endif /* NT_FONT_H */
