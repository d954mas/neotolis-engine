#ifndef NT_FONT_INTERNAL_H
#define NT_FONT_INTERNAL_H

#include "font/nt_font.h"
#include "graphics/nt_gfx.h"
#include "pool/nt_pool.h"

/* ---- Measure cache ----
 *
 * Per-font direct-mapped table; size is configured per font via
 * nt_font_create_desc_t.measure_cache_size (POT, 0 = disabled). The index
 * is `xxHash64(content, len) & (measure_cache_size - 1)`. Each entry
 * stores the full 64-bit hash + size_bits (bit-cast of float size) for
 * collision detection. Pure replace-on-collision eviction — a collision
 * in slot N overwrites the previous occupant unconditionally (NOT LRU).
 *
 * History:
 *   - Drift 3 Option B (Phase 51): xxHash via nt_hash32() — not FNV-1a.
 *   - Phase 51.1: upgraded to xxHash64 (eliminates theoretical 32-bit
 *     false-positive cache hits over long sessions; also faster on big
 *     strings due to wider lanes).
 *   - Phase 51.1: layout changed from AoS to SoA (see nt_font_measure_cache_t).
 *   - Phase 51.1: size made runtime-configurable.
 */
#define NT_FONT_MEASURE_CACHE_SIZE 256U /* legacy default; configurable in desc */

/* Single source of truth for the SoA per-entry footprint. The SoA block
 * is allocated as one calloc(cache_size, NT_FONT_MEASURE_CACHE_ENTRY_BYTES)
 * in nt_font_create and zeroed in measure_cache_clear with the same
 * multiplier — keeping both call sites in sync via this macro means a
 * layout change in nt_font_measure_cache_t can't silently desync the
 * alloc/zero sizes. */
#define NT_FONT_MEASURE_CACHE_ENTRY_BYTES (sizeof(uint64_t) + sizeof(uint32_t) + sizeof(nt_text_size_t) + sizeof(uint8_t))

/* Sentinel value for ascii_glyph_idx[] — codepoint is absent in all resources. */
#define NT_FONT_ASCII_IDX_NONE ((uint16_t)0xFFFFU)

/* Measure cache (SoA — Structure-of-Arrays).
 *
 * On lookup we read key_hashes[i], size_bits[i], valid[i] to test for a hit.
 * Only on a confirmed hit do we touch values[i]. With AoS, an entry would
 * be 24 B and EVERY lookup loads the entire entry into L1 — including the
 * 8 B value that the miss path never reads. SoA splits the per-entry data
 * into 4 parallel arrays:
 *
 *   key_hashes  — 8 B/entry (xxHash64) — read on every lookup
 *   size_bits   — 4 B/entry            — read on every lookup
 *   valid       — 1 B/entry            — read on every lookup
 *   values      — 8 B/entry            — read ONLY on a confirmed hit
 *
 * Total: 21 B per entry (no padding), 13 B in the hot bands. At 256 entries
 * a single 64 B cache line covers 8 hash slots' (hash+size+valid) compactly.
 *
 * key_hash is 64-bit xxHash64(content, len). At 32-bit, false-positive cache
 * hits become statistically possible in long sessions (~10⁻⁸ per call ×
 * 10K calls/session = ~0.01% chance of one bogus result); 64-bit drops that
 * to ~10⁻¹⁹ per call — effectively zero. size_bits stays as a separate
 * field so two requests for the same string at different sizes don't alias.
 */
typedef struct {
    /* All four arrays live in one calloc'd block, [measure_cache_size] each,
     * laid out: key_hashes | size_bits | values | valid. The pointers below
     * point into that block. nt_font_destroy frees key_hashes (the base);
     * the others are not freed independently. */
    uint64_t *key_hashes;   /* [measure_cache_size] — xxHash64 of content */
    uint32_t *size_bits;    /* [measure_cache_size] — bit_cast<u32>(size) */
    nt_text_size_t *values; /* [measure_cache_size] — measured w/h */
    uint8_t *valid;         /* [measure_cache_size] — 0/1 occupancy flag */
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

    /* Cache generation (bumped on full flush, for Phase 45 batch invalidation) */
    uint32_t cache_generation;

    /* Measure cache. Direct-mapped table,
     * size = measure_cache_size (power-of-two, configured per
     * font in nt_font_create_desc_t). Indexed by xxHash64(content,len) &
     * (measure_cache_size - 1). Per-font isolation means font_id is
     * redundant in the per-entry key — collision detection uses
     * {key_hash, size_bits} only.
     *
     * Layout is SoA — see nt_font_measure_cache_t. Heap-allocated as one
     * block at create (NOT in hot path — AGENTS.md permits heap at
     * lifecycle boundaries). All four sub-pointers are NULL when
     * measure_cache_size == 0 (cache disabled).
     *
     * Auto-invalidated on any resource handle change inside nt_font_step
     * (so async fallback chains don't return stale tofu metrics). */
    nt_font_measure_cache_t measure_cache; /* SoA; all pointers NULL when cache disabled */
    uint32_t measure_cache_size;           /* 0 = disabled; else POT, ≤ 32768 */
    uint32_t measure_cache_mask;           /* measure_cache_size - 1 */
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
