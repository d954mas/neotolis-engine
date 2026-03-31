#include "font/nt_font.h"
#include "font/nt_font_internal.h"

#include <stdlib.h>
#include <string.h>

#include "core/nt_assert.h"
#include "graphics/nt_gfx.h"
#include "log/nt_log.h"
#include "nt_font_format.h"
#include "nt_pack_format.h"
#include "pool/nt_pool.h"
#include "resource/nt_resource.h"

/* ---- Font data storage (side table for pack blobs accessed via activator) ---- */

#define NT_FONT_MAX_DATA_ENTRIES 64

typedef struct {
    const uint8_t *data;
    uint32_t size;
} nt_font_data_entry_t;

static struct {
    nt_font_data_entry_t entries[NT_FONT_MAX_DATA_ENTRIES];
    uint32_t count;
} s_font_data;

/* ---- Module state ---- */

static nt_font_state_t s_font;

/* ---- Font activator callbacks ---- */

static uint32_t activate_font(const uint8_t *data, uint32_t size) {
    /* Store pointer to pack data for later access by font module.
     * The data pointer is valid as long as the pack remains mounted. */
    NT_ASSERT(s_font_data.count < NT_FONT_MAX_DATA_ENTRIES);
    uint32_t idx = s_font_data.count++;
    s_font_data.entries[idx].data = data;
    s_font_data.entries[idx].size = size;
    return idx + 1; /* 1-based handle (0 = failure in resource system) */
}

static void deactivate_font(uint32_t runtime_handle) {
    if (runtime_handle == 0 || runtime_handle > s_font_data.count) {
        return;
    }
    uint32_t idx = runtime_handle - 1;
    s_font_data.entries[idx].data = NULL;
    s_font_data.entries[idx].size = 0;
}

/* Get font data from activation handle */
static const uint8_t *get_font_data(uint32_t runtime_handle, uint32_t *out_size) {
    if (runtime_handle == 0 || runtime_handle > s_font_data.count) {
        if (out_size) {
            *out_size = 0;
        }
        return NULL;
    }
    uint32_t idx = runtime_handle - 1;
    if (out_size) {
        *out_size = s_font_data.entries[idx].size;
    }
    return s_font_data.entries[idx].data;
}

/* ---- Internal helpers ---- */

static nt_font_slot_t *get_slot(nt_font_t font) {
    uint32_t idx = nt_pool_slot_index(font.id);
    return &s_font.slots[idx];
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

    /* Register font activator for NT_ASSET_FONT resources */
    nt_resource_set_activator(NT_ASSET_FONT, activate_font, deactivate_font);

    /* Reset font data storage */
    memset(&s_font_data, 0, sizeof(s_font_data));

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
        free(slot->curve_staging);
        nt_gfx_destroy_texture(slot->curve_texture);
        nt_gfx_destroy_texture(slot->band_texture);
    }
    // #endregion
    free(s_font.slots);
    nt_pool_shutdown(&s_font.pool);
    memset(&s_font, 0, sizeof(s_font));
    memset(&s_font_data, 0, sizeof(s_font_data));
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void nt_font_step(void) {
    NT_ASSERT(s_font.initialized);
    if (!s_font.initialized) {
        return;
    }

    for (uint32_t i = 1; i <= s_font.pool.capacity; i++) {
        if (!nt_pool_slot_alive(&s_font.pool, i)) {
            continue;
        }

        nt_font_slot_t *slot = &s_font.slots[i];
        slot->frame_counter++;

        // #region Resolve resources
        for (uint8_t ri = 0; ri < slot->resource_count; ri++) {
            uint32_t ver = nt_resource_get(slot->resources[ri]);
            if (ver == 0) {
                continue; /* resource not ready yet */
            }
            if (ver == slot->resource_versions[ri]) {
                continue; /* no change */
            }

            /* Resource changed -- parse font header */
            uint32_t blob_size = 0;
            const uint8_t *blob = get_font_data(ver, &blob_size);
            if (!blob || blob_size < sizeof(NtFontAssetHeader)) {
                continue;
            }

            const NtFontAssetHeader *hdr = (const NtFontAssetHeader *)blob;
            NT_ASSERT(hdr->magic == NT_FONT_MAGIC);
            NT_ASSERT(hdr->version == NT_FONT_VERSION);

            // #region Metrics validation (D-19)
            if (!slot->metrics_set) {
                slot->metrics.ascent = hdr->ascent;
                slot->metrics.descent = hdr->descent;
                slot->metrics.line_gap = hdr->line_gap;
                slot->metrics.units_per_em = hdr->units_per_em;
                slot->metrics.line_height = (int16_t)(hdr->ascent - hdr->descent + hdr->line_gap);
                slot->metrics_set = true;
            } else {
                NT_ASSERT(slot->metrics.units_per_em == hdr->units_per_em);
                NT_ASSERT(slot->metrics.ascent == hdr->ascent);
                NT_ASSERT(slot->metrics.descent == hdr->descent);
                NT_ASSERT(slot->metrics.line_gap == hdr->line_gap);
            }
            // #endregion

            /* If version changed (reload), invalidate cache */
            if (slot->resource_versions[ri] != 0) {
                memset(slot->cache, 0, (size_t)slot->max_glyphs * sizeof(nt_font_cache_slot_t));
                slot->glyphs_cached = 0;
                slot->curve_write_head = 0;
                slot->tofu_generated = false;
            }

            slot->resource_versions[ri] = ver;
        }
        // #endregion
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

    uint8_t band_count = desc->band_count > 0 ? desc->band_count : 8; /* default 8 per D-03 */

    uint32_t id = nt_pool_alloc(&s_font.pool);
    if (id == 0) {
        NT_LOG_ERROR("font pool full -- increase max_fonts");
        return NT_FONT_INVALID;
    }

    uint32_t slot_index = nt_pool_slot_index(id);
    nt_font_slot_t *slot = &s_font.slots[slot_index];
    memset(slot, 0, sizeof(*slot));

    // #region Store config
    slot->curve_tex_width = desc->curve_texture_width;
    slot->curve_tex_height = desc->curve_texture_height;
    slot->band_tex_height = desc->band_texture_height;
    slot->band_count = band_count;
    slot->max_glyphs = desc->band_texture_height; /* D-08 */
    // #endregion

    // #region Create GPU textures (D-11: once, never resized)
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
        .width = band_count,
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

    // #region Allocate cache and staging buffer
    slot->cache = (nt_font_cache_slot_t *)calloc(desc->band_texture_height, sizeof(nt_font_cache_slot_t));
    NT_ASSERT(slot->cache);

    /* CPU staging buffer for curve texture (RGBA16F = 4 x uint16 per texel) per D-12 */
    size_t total_texels = (size_t)desc->curve_texture_width * desc->curve_texture_height;
    slot->curve_staging = (uint16_t *)calloc(total_texels * 4, sizeof(uint16_t));
    NT_ASSERT(slot->curve_staging);
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
    free(slot->curve_staging);
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

void nt_font_add(nt_font_t font, nt_resource_t resource) {
    NT_ASSERT(s_font.initialized);
    NT_ASSERT(nt_pool_valid(&s_font.pool, font.id));

    nt_font_slot_t *slot = get_slot(font);
    NT_ASSERT(slot->resource_count < NT_FONT_MAX_RESOURCES);

    slot->resources[slot->resource_count] = resource;
    slot->resource_versions[slot->resource_count] = 0; /* resolved in step */
    slot->resource_count++;
}

/* ---- Query ---- */

nt_font_metrics_t nt_font_get_metrics(nt_font_t font) {
    NT_ASSERT(s_font.initialized);
    NT_ASSERT(nt_pool_valid(&s_font.pool, font.id));
    if (!s_font.initialized || !nt_pool_valid(&s_font.pool, font.id)) {
        return (nt_font_metrics_t){0};
    }
    nt_font_slot_t *slot = get_slot(font);
    NT_ASSERT(slot->metrics_set);
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
        .band_texels_used = (uint32_t)slot->glyphs_cached * slot->band_count,
        .band_texels_total = (uint32_t)slot->band_count * slot->band_tex_height,
    };
}

/* ---- Glyph lookup (stub for Task 1b) ---- */

const nt_glyph_cache_entry_t *nt_font_lookup_glyph(nt_font_t font, uint32_t codepoint) {
    NT_ASSERT(s_font.initialized);
    NT_ASSERT(nt_pool_valid(&s_font.pool, font.id));
    /* Task 1b: cache lookup, cache miss handling, LRU eviction */
    (void)font;
    (void)codepoint;
    return NULL;
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

/* ---- Kern pair lookup (stub for Task 1b) ---- */

int16_t nt_font_get_kern(nt_font_t font, uint32_t left_codepoint, uint32_t right_codepoint) {
    NT_ASSERT(s_font.initialized);
    NT_ASSERT(nt_pool_valid(&s_font.pool, font.id));
    /* Task 1b: bsearch kern pairs in resource data */
    (void)font;
    (void)left_codepoint;
    (void)right_codepoint;
    return 0;
}
