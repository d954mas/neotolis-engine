#ifndef NT_FONT_INTERNAL_H
#define NT_FONT_INTERNAL_H

#include "font/nt_font.h"
#include "graphics/nt_gfx.h"
#include "pool/nt_pool.h"

/* Per-font direct-mapped table. Index = xxHash64(content,len) & mask.
 * Replace-on-collision (NOT LRU). Entry holds full 64-bit hash + size_bits
 * (bit-cast float) for collision detection. */
#define NT_FONT_MEASURE_CACHE_SIZE 256U

/* SoA per-entry size — used by both calloc and memset; macro keeps them
 * in sync if the layout changes. */
#define NT_FONT_MEASURE_CACHE_ENTRY_BYTES (sizeof(uint64_t) + sizeof(uint32_t) + sizeof(nt_text_size_t) + sizeof(uint8_t))

/* Sentinel value for ascii_glyph_idx[] — codepoint is absent in all resources. */
#define NT_FONT_ASCII_IDX_NONE ((uint16_t)0xFFFFU)

/* Measure cache (SoA). Hot bands (hash + size + valid = 13 B) packed in
 * separate arrays so a lookup miss doesn't pull the 8 B value into L1.
 * size_bits keeps "same string at different size" entries distinct. */
typedef struct {
    /* All four arrays share one calloc'd block (layout: hashes | size_bits |
     * values | valid). key_hashes is the base — free it to free all. */
    uint64_t *key_hashes;
    uint32_t *size_bits;
    nt_text_size_t *values;
    uint8_t *valid; /* 0/1 occupancy flag */
} nt_font_measure_cache_t;

/* ---- Internal cache entry (extends public entry with LRU data) ---- */

typedef struct {
    nt_glyph_cache_entry_t entry; /* public-facing data (first member) */
    uint32_t lru_frame;           /* last frame accessed (for LRU eviction) */
    uint8_t resource_index;       /* which resource this glyph came from */
} nt_font_cache_slot_t;

/* ---- Font slot (per-handle internal state) ----
 *
 * Layout stays module-private; an opaque forward-decl lives in
 * nt_font_hot.h for cross-module hot paths. The typedef is duplicated
 * here so nt_font.c sees the name without pulling in the hot header
 * (C17 permits identical typedefs across TUs). */

typedef struct nt_font_slot_s nt_font_slot_t;

struct nt_font_slot_s {
    nt_font_metrics_t metrics; /* populated on first nt_font_add */
    bool metrics_set;          /* true after first resource parsed */

    /* Resources */
    nt_resource_t resources[NT_FONT_MAX_SOURCES_PER_FONT];
    uint32_t resource_handles[NT_FONT_MAX_SOURCES_PER_FONT];
    uint8_t resource_count;

    /* GPU textures */
    nt_texture_t curve_texture; /* RGBA16F */
    nt_texture_t band_texture;  /* RG16UI */
    uint16_t curve_tex_width;
    uint16_t curve_tex_height;
    uint16_t band_tex_height; /* = max_glyphs */
    uint8_t band_count;

    /* Glyph cache */
    nt_font_cache_slot_t *cache; /* [max_glyphs] array */
    uint16_t glyphs_cached;

    /* Curve texture packing */
    uint32_t curve_write_head; /* next free texel offset (linear allocator) */

    /* Tofu */
    bool tofu_generated;

    /* Max glyphs (= band_tex_height) */
    uint16_t max_glyphs;

    /* Free slot stack (O(1) alloc/free) */
    uint16_t *free_stack; /* [max_glyphs] */
    uint16_t free_top;    /* next free index to pop */

    /* Codepoint → cache slot hash table (open addressing, POT size) */
    uint16_t *hash_table;     /* values: slot_index + 1 (0 = empty) */
    uint16_t hash_table_size; /* POT, = max_glyphs * 2 */

    /* Bumped on full flush — batch consumers use it to invalidate staging. */
    uint32_t cache_generation;

    /* Measure cache: SoA, pointers NULL when disabled (size = 0). */
    nt_font_measure_cache_t measure_cache;
    uint32_t measure_cache_size; /* 0 = disabled; else POT, ≤ 32768 */
    uint32_t measure_cache_mask; /* measure_cache_size - 1 */
    /* Set when any loaded resource has a glyph with kern_count > 0. Latin
     * fonts usually skip the kern table, so the per-codepoint kern lookup
     * gates on this. */
    bool has_any_kern;

    /* ASCII fast-path index. For codepoint < 128:
     *   ascii_glyph_idx[cp] == NT_FONT_ASCII_IDX_NONE → not in any resource
     *   else: glyph at ascii_glyph_idx[cp] inside ascii_glyph_res[cp].
     * Rebuilt on resource change. First resource owning a codepoint wins.
     * Non-ASCII codepoints still use the bsearch path. */
    uint16_t ascii_glyph_idx[128];
    uint8_t ascii_glyph_res[128];

#ifdef NT_FONT_TEST_ACCESS
    uint32_t test_measure_cache_hits;
    uint32_t test_measure_cache_misses;
#endif
};

/* ---- Module state ---- */

typedef struct {
    nt_pool_t pool;
    nt_font_slot_t *slots;             /* [capacity+1], index 0 reserved */
    void *data_entries;                /* nt_font_data_entry_t[], allocated in init */
    uint32_t data_capacity;            /* max_fonts * NT_FONT_MAX_SOURCES_PER_FONT */
    uint32_t data_count;               /* high-water mark */
    uint32_t frame_counter;            /* module-level LRU tick, incremented each step */
    nt_font_pre_flush_fn pre_flush_fn; /* called before cache clear, see nt_font.h */
    bool initialized;
} nt_font_state_t;

#endif /* NT_FONT_INTERNAL_H */
