#ifndef NT_FONT_H
#define NT_FONT_H

#include "core/nt_types.h"
#include "graphics/nt_gfx.h"
#include "resource/nt_resource.h"

/* ---- Compile-time limits ---- */

#define NT_FONT_MAX_RESOURCES 8

/* ---- Handle type ---- */

typedef struct {
    uint32_t id;
} nt_font_t;

#define NT_FONT_INVALID ((nt_font_t){0})

/* ---- Init descriptor ---- */

typedef struct {
    uint16_t max_fonts;
} nt_font_desc_t;

static inline nt_font_desc_t nt_font_desc_defaults(void) {
    return (nt_font_desc_t){
        .max_fonts = 16,
    };
}

/* ---- Create descriptor ---- */

typedef struct {
    uint16_t curve_texture_width;  /* RGBA16F texture width (POT recommended) */
    uint16_t curve_texture_height; /* RGBA16F texture height */
    uint16_t band_texture_height;  /* RG16UI texture height = max_glyphs */
    uint8_t band_count;            /* bands per glyph (default: 8) */
} nt_font_create_desc_t;

/* ---- Metrics (from font asset header) ---- */

typedef struct {
    int16_t ascent;
    int16_t descent;
    int16_t line_gap;
    uint16_t units_per_em;
    int16_t line_height; /* computed: ascent - descent + line_gap */
} nt_font_metrics_t;

/* ---- Stats (cache utilization) ---- */

typedef struct {
    uint16_t glyphs_cached;
    uint16_t max_glyphs;
    uint32_t curve_texels_used;
    uint32_t curve_texels_total;
    uint32_t band_texels_used;
    uint32_t band_texels_total;
} nt_font_stats_t;

/* ---- Glyph cache entry (public for nt_text in Phase 45) ---- */

typedef struct {
    uint32_t codepoint;
    uint32_t curve_offset; /* texel offset into curve texture */
    uint16_t curve_count;  /* number of curve texels used */
    uint16_t band_row;     /* row in band texture */
    int16_t advance;       /* horizontal advance (font units) */
    int16_t bbox_x0;
    int16_t bbox_y0;
    int16_t bbox_x1;
    int16_t bbox_y1;
    bool is_tofu;
} nt_glyph_cache_entry_t;

/* ---- Lifecycle ---- */

nt_result_t nt_font_init(const nt_font_desc_t *desc);
void nt_font_shutdown(void);
void nt_font_step(void); /* resolve resources, batch uploads */

/* ---- Create / Destroy / Valid ---- */

nt_font_t nt_font_create(const nt_font_create_desc_t *desc);
void nt_font_destroy(nt_font_t font);
bool nt_font_valid(nt_font_t font);

/* ---- Resource management ---- */

void nt_font_add(nt_font_t font, nt_resource_t resource);

/* ---- Query ---- */

nt_font_metrics_t nt_font_get_metrics(nt_font_t font);
nt_font_stats_t nt_font_get_stats(nt_font_t font);

/* ---- Glyph lookup (exposed for nt_text in Phase 45) ---- */

/* Returns pointer into internal cache. Valid until next lookup that triggers
 * eviction or flush. Copy needed data immediately — do not store the pointer. */
const nt_glyph_cache_entry_t *nt_font_lookup_glyph(nt_font_t font, uint32_t codepoint);

/* ---- GPU texture access (for nt_text to bind before draw) ---- */

nt_texture_t nt_font_get_curve_texture(nt_font_t font);
nt_texture_t nt_font_get_band_texture(nt_font_t font);
uint8_t nt_font_get_band_count(nt_font_t font);
uint16_t nt_font_get_curve_texture_width(nt_font_t font);

/* ---- Cache generation (for Phase 45 batch invalidation) ---- */

uint32_t nt_font_get_cache_generation(nt_font_t font);

/* ---- Kern pair lookup (for nt_text shaping in Phase 45) ---- */

int16_t nt_font_get_kern(nt_font_t font, uint32_t left_codepoint, uint32_t right_codepoint);

/* ---- Test access (test-only) ---- */

#ifdef NT_FONT_TEST_ACCESS
uint32_t nt_font_test_register_data(const uint8_t *data, uint32_t size);
#endif

#endif /* NT_FONT_H */
