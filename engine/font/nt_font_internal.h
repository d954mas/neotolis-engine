#ifndef NT_FONT_INTERNAL_H
#define NT_FONT_INTERNAL_H

#include "font/nt_font.h"
#include "graphics/nt_gfx.h"
#include "pool/nt_pool.h"

/* ---- Internal cache entry (extends public entry with LRU data) ---- */

typedef struct {
    nt_glyph_cache_entry_t entry; /* public-facing data (first member) */
    uint32_t lru_frame;           /* last frame accessed (for LRU eviction) */
    uint8_t resource_index;       /* which resource this glyph came from */
} nt_font_cache_slot_t;

/* ---- Font slot (per-handle internal state) ---- */

typedef struct {
    nt_font_metrics_t metrics; /* populated on first nt_font_add */
    bool metrics_set;          /* true after first resource parsed */

    /* Resources (D-17, D-18) */
    nt_resource_t resources[NT_FONT_MAX_RESOURCES];
    uint32_t resource_handles[NT_FONT_MAX_RESOURCES];
    uint8_t resource_count;

    /* GPU textures (D-06, D-11) */
    nt_texture_t curve_texture; /* RGBA16F */
    nt_texture_t band_texture;  /* RG16UI */
    uint16_t curve_tex_width;
    uint16_t curve_tex_height;
    uint16_t band_tex_height; /* = max_glyphs (D-08) */
    uint8_t band_count;

    /* Glyph cache (D-13, D-14, D-15) */
    nt_font_cache_slot_t *cache; /* [max_glyphs] array */
    uint16_t glyphs_cached;
    uint32_t frame_counter; /* incremented each step for LRU */

    /* Curve texture packing (D-07) */
    uint32_t curve_write_head; /* next free texel offset (linear allocator) */

    /* Tofu (D-22, D-23) */
    bool tofu_generated;

    /* Max glyphs (= band_tex_height, D-08) */
    uint16_t max_glyphs;

    /* Codepoint → cache slot hash table (open addressing, POT size) */
    uint16_t *hash_table;      /* values: slot_index + 1 (0 = empty) */
    uint16_t hash_table_size;  /* POT, = max_glyphs * 2 */

    /* Cache generation (bumped on full flush, for Phase 45 batch invalidation) */
    uint32_t cache_generation;
} nt_font_slot_t;

/* ---- Module state ---- */

typedef struct {
    nt_pool_t pool;
    nt_font_slot_t *slots;   /* [capacity+1], index 0 reserved */
    void *data_entries;       /* nt_font_data_entry_t[], allocated in init */
    uint32_t data_capacity;   /* max_fonts * NT_FONT_MAX_RESOURCES */
    uint32_t data_count;      /* high-water mark */
    bool initialized;
} nt_font_state_t;

#endif /* NT_FONT_INTERNAL_H */
