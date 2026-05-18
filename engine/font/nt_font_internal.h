#ifndef NT_FONT_INTERNAL_H
#define NT_FONT_INTERNAL_H

#include "font/nt_font.h"
#include "graphics/nt_gfx.h"
#include "pool/nt_pool.h"

/* ---- Measure cache (Phase 51 / D-51-08) ----
 *
 * Per-font 256-entry direct-mapped table indexed by low 8 bits of
 * xxHash32(content, len). Each entry stores the full 32-bit hash + size_bits
 * (bit-cast of float size) for collision detection. Pure replace-on-collision
 * eviction within the 256 entries — NOT LRU; the cache is direct-mapped, a
 * collision in slot N overwrites the previous occupant unconditionally. The
 * name "LRU" in earlier wording was wrong; renamed throughout Phase 51.1.
 *
 * Drift 3 Option B: key uses XXH32 via nt_hash32() — NOT FNV-1a.
 */
#define NT_FONT_MEASURE_CACHE_SIZE 256U

/* Sentinel value for ascii_glyph_idx[] — codepoint is absent in all resources. */
#define NT_FONT_ASCII_IDX_NONE ((uint16_t)0xFFFFU)

/* Default measure cache size when desc->measure_cache_size == 0 and the
 * caller used nt_font_create_desc_defaults (versus a bare {0} that disables
 * the cache outright). Kept as a #define for migration paths that still
 * reference the v1.7 hardcoded constant. */

/* Measure cache entry — direct-mapped, 20 bytes per entry. */
typedef struct {
    uint32_t key_hash;  /* full 32-bit xxHash for collision detection */
    uint32_t size_bits; /* bit_cast<uint32_t>(float size) — exact comparison, no quantization */
    nt_text_size_t value;
    bool valid;
} nt_font_measure_cache_entry_t;

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
    nt_resource_t resources[NT_FONT_MAX_SOURCES_PER_FONT];
    uint32_t resource_handles[NT_FONT_MAX_SOURCES_PER_FONT];
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

    /* Curve texture packing (D-07) */
    uint32_t curve_write_head; /* next free texel offset (linear allocator) */

    /* Tofu (D-22, D-23) */
    bool tofu_generated;

    /* Max glyphs (= band_tex_height, D-08) */
    uint16_t max_glyphs;

    /* Free slot stack (O(1) alloc/free) */
    uint16_t *free_stack; /* [max_glyphs] */
    uint16_t free_top;    /* next free index to pop */

    /* Codepoint → cache slot hash table (open addressing, POT size) */
    uint16_t *hash_table;     /* values: slot_index + 1 (0 = empty) */
    uint16_t hash_table_size; /* POT, = max_glyphs * 2 */

    /* Cache generation (bumped on full flush, for Phase 45 batch invalidation) */
    uint32_t cache_generation;

    /* Measure cache (Phase 51 / D-51-08 / FONT-02). Direct-mapped table,
     * size = measure_cache_size (power-of-two, configured per font in
     * nt_font_create_desc_t). Indexed by xxHash32(content,len) &
     * (measure_cache_size - 1). Drift 3 Option B: xxHash32, not FNV-1a.
     * Per-font isolation means font_id is redundant in the per-entry key —
     * collision detection uses {key_hash, size_bits} only.
     *
     * Heap-allocated at create (NOT in hot path — AGENTS.md permits heap
     * at lifecycle boundaries). NULL when measure_cache_size == 0 (cache
     * disabled). The measure_cache_warm flag lets invalidate_cache skip
     * slots that were never written. */
    nt_font_measure_cache_entry_t *measure_cache; /* [measure_cache_size] or NULL */
    uint16_t measure_cache_size;                  /* 0 = disabled; else POT */
    uint16_t measure_cache_mask;                  /* measure_cache_size - 1 */
    bool measure_cache_warm;                      /* true after any write; lets invalidate_cache skip cold slots */
    /* True if ANY loaded resource has at least one glyph with kern_count > 0.
     * Cheap fast-path gate in measure_n / draw_n: most Latin fonts ship with
     * no kern table, so we skip the per-codepoint kern lookup entirely. Set
     * during nt_font_step when a resource handle changes; cleared by
     * destroy/flush_cache. */
    bool has_any_kern;

    /* ASCII fast-path index (Phase 51 perf). For codepoints in [0, 128),
     * stores the position of the matching glyph in its resource's glyph
     * table, replacing the bsearch with a single array lookup.
     *   ascii_glyph_idx[cp] == NT_FONT_ASCII_IDX_NONE → not present in any resource
     *   else: glyph at index ascii_glyph_idx[cp] inside resource ascii_glyph_res[cp]
     * Built once per resource change in nt_font_step. Walks the glyph table
     * in order so the first resource that owns a codepoint wins (matches
     * the lookup precedence of find_glyph_in_resources).
     *
     * Size: 128 × 2 + 128 = 384 B per slot; 16 fonts default → 6 KB.
     * Non-ASCII codepoints still use the bsearch path. */
    uint16_t ascii_glyph_idx[128];
    uint8_t ascii_glyph_res[128];

#ifdef NT_FONT_TEST_ACCESS
    uint32_t test_measure_cache_hits;
    uint32_t test_measure_cache_misses;
#endif
} nt_font_slot_t;

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
