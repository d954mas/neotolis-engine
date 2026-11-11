#ifndef NT_FONT_INTERNAL_H
#define NT_FONT_INTERNAL_H

#include "font/nt_font.h"
#include "graphics/nt_gfx.h"
#include "pool/nt_pool.h"

/* Used by both calloc (in create) and memset (in clear) — macro keeps them in sync. */
#define NT_FONT_MEASURE_CACHE_ENTRY_BYTES (sizeof(uint64_t) + sizeof(uint32_t) + sizeof(nt_text_size_t) + sizeof(uint8_t))

/* Sentinel for ascii_glyph_idx[] — codepoint absent in all payloads. */
#define NT_FONT_ASCII_IDX_NONE ((uint16_t)0xFFFFU)

/* SoA: hash + size + valid in separate arrays so misses don't drag value
 * into L1. All four arrays share one calloc'd block (base = key_hashes). */
typedef struct {
    uint64_t *key_hashes;
    uint32_t *size_bits;
    nt_text_size_t *values;
    uint8_t *valid;
} nt_font_measure_cache_t;

typedef struct {
    nt_glyph_cache_entry_t entry;
    uint32_t lru_frame;
    int16_t key_offset; /* embolden weight bucket (font units); part of the cache key with entry.codepoint */
} nt_font_cache_slot_t;

/* Opaque forward-decl in nt_font_hot.h; the typedef is duplicated there.
 * C17 permits identical typedefs across TUs. */
typedef struct nt_font_slot_s nt_font_slot_t;

struct nt_font_slot_s {
    nt_font_metrics_t metrics;

    uint8_t *data; /* one owned payload; each fill merges into its sorted glyph table */
    uint32_t data_size;

    nt_texture_t curve_texture; /* RGBA16F, NT_FONT_GLYPH_TEXELS x max_glyphs: row i = cache slot i */

    nt_font_cache_slot_t *cache; /* [max_glyphs]; slot 0 holds tofu */
    uint16_t max_glyphs;
    uint16_t fill; /* slots [1, fill) were taken since the last clear; later ones are unused */

    uint16_t *hash_table; /* codepoint → cache slot+1 (0 = empty), POT */
    uint16_t hash_table_size;

    nt_font_measure_cache_t measure_cache; /* pointers NULL when size == 0 */
    uint32_t measure_cache_size;
    uint32_t measure_cache_mask;

    /* Fast-path gate: most Latin fonts have no kern table. */
    bool has_any_kern;

    /* For codepoint < 128, ascii_glyph_idx[cp] is the glyph position in data. NONE = absent. */
    uint16_t ascii_glyph_idx[128];

#ifdef NT_TEST_ACCESS
    uint32_t test_measure_cache_hits;
    uint32_t test_measure_cache_misses;
#endif
};

typedef struct {
    nt_pool_t pool;
    nt_font_slot_t *slots; /* [capacity+1], index 0 reserved */
    bool initialized;
} nt_font_state_t;

#endif /* NT_FONT_INTERNAL_H */
