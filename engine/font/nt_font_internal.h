#ifndef NT_FONT_INTERNAL_H
#define NT_FONT_INTERNAL_H

#include "font/nt_font.h"
#include "graphics/nt_gfx.h"
#include "pool/nt_pool.h"

#define NT_FONT_MEASURE_CACHE_SIZE 256U

/* Used by both calloc (in create) and memset (in clear) — macro keeps them in sync. */
#define NT_FONT_MEASURE_CACHE_ENTRY_BYTES (sizeof(uint64_t) + sizeof(uint32_t) + sizeof(nt_text_size_t) + sizeof(uint8_t))

/* Sentinel for ascii_glyph_idx[] — codepoint absent in all resources. */
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
    uint8_t resource_index;
} nt_font_cache_slot_t;

/* Opaque forward-decl in nt_font_hot.h. Typedef duplicated here so nt_font.c
 * sees the name without pulling in the hot header (C17 allows). */
typedef struct nt_font_slot_s nt_font_slot_t;

struct nt_font_slot_s {
    nt_font_metrics_t metrics;
    bool metrics_set;

    nt_resource_t resources[NT_FONT_MAX_SOURCES_PER_FONT];
    uint32_t resource_handles[NT_FONT_MAX_SOURCES_PER_FONT];
    uint8_t resource_count;

    nt_texture_t curve_texture; /* RGBA16F */
    nt_texture_t band_texture;  /* RG16UI */
    uint16_t curve_tex_width;
    uint16_t curve_tex_height;
    uint16_t band_tex_height; /* = max_glyphs */
    uint8_t band_count;

    nt_font_cache_slot_t *cache; /* [max_glyphs] */
    uint16_t glyphs_cached;
    uint32_t curve_write_head; /* linear allocator into curve texture */
    bool tofu_generated;
    uint16_t max_glyphs;

    uint16_t *free_stack; /* [max_glyphs], O(1) alloc/free */
    uint16_t free_top;

    uint16_t *hash_table; /* codepoint → cache slot+1 (0 = empty), POT */
    uint16_t hash_table_size;

    uint32_t cache_generation; /* bumped on flush; consumers invalidate staging */

    nt_font_measure_cache_t measure_cache; /* pointers NULL when size == 0 */
    uint32_t measure_cache_size;
    uint32_t measure_cache_mask;

    /* Fast-path gate: most Latin fonts have no kern table. */
    bool has_any_kern;

    /* For codepoint < 128, ascii_glyph_idx[cp] is the glyph position inside
     * resource ascii_glyph_res[cp]. NONE sentinel = not in any resource. */
    uint16_t ascii_glyph_idx[128];
    uint8_t ascii_glyph_res[128];

#ifdef NT_FONT_TEST_ACCESS
    uint32_t test_measure_cache_hits;
    uint32_t test_measure_cache_misses;
#endif
};

typedef struct {
    nt_pool_t pool;
    nt_font_slot_t *slots; /* [capacity+1], index 0 reserved */
    void *data_entries;    /* nt_font_data_entry_t[], allocated in init */
    uint32_t data_capacity;
    uint32_t data_count;
    uint32_t frame_counter; /* LRU tick */
    nt_font_pre_flush_fn pre_flush_fn;
    bool initialized;
} nt_font_state_t;

#endif /* NT_FONT_INTERNAL_H */
