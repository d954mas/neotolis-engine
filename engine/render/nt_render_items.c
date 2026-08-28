#include "render/nt_render_items.h"
#include "core/nt_assert.h"

#include "entity/nt_entity.h"
#include "sort/nt_sort.h"
#include "transform_comp/nt_transform_comp.h"

/* ---- Instantiate typed radix sort for render items ---- */

// NOLINTNEXTLINE(readability-function-cognitive-complexity) -- generated radix sort includes assert expansion
NT_SORT_DEFINE(nt_sort_by_key, nt_render_item_t)

static void sort_by_batch_key(nt_render_item_t *items, uint32_t count, nt_render_item_t *scratch) {
    uint32_t histograms[4][256] = {{0}};
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t key = items[i].batch_key;
        ++histograms[0][(uint8_t)key];
        ++histograms[1][(uint8_t)(key >> 8)];
        ++histograms[2][(uint8_t)(key >> 16)];
        ++histograms[3][(uint8_t)(key >> 24)];
    }

    bool active[4];
    uint32_t active_count = 0;
    for (uint32_t pass = 0; pass < 4; ++pass) {
        active[pass] = true;
        for (uint32_t bucket = 0; bucket < 256; ++bucket) {
            if (histograms[pass][bucket] == count) {
                active[pass] = false;
                break;
            }
        }
        if (active[pass]) {
            ++active_count;
        }
    }
    if (active_count == 0) {
        return;
    }

    nt_render_item_t *src = items;
    nt_render_item_t *dst = scratch;
    if ((active_count & 1U) != 0) {
        memcpy(scratch, items, (size_t)count * sizeof(*items));
        src = scratch;
        dst = items;
    }

    for (uint32_t pass = 0; pass < 4; ++pass) {
        if (!active[pass]) {
            continue;
        }

        uint32_t *histogram = histograms[pass];
        uint32_t sum = 0;
        for (uint32_t bucket = 0; bucket < 256; ++bucket) {
            uint32_t bucket_count = histogram[bucket];
            histogram[bucket] = sum;
            sum += bucket_count;
        }

        uint32_t shift = pass * 8;
        for (uint32_t i = 0; i < count; ++i) {
            uint8_t digit = (uint8_t)(src[i].batch_key >> shift);
            dst[histogram[digit]++] = src[i];
        }

        nt_render_item_t *tmp = src;
        src = dst;
        dst = tmp;
    }
}

void nt_sort_by_key_then_batch(nt_render_item_t *items, uint32_t count, nt_render_item_t *scratch) {
    if (count < 2) {
        return;
    }
    NT_ASSERT(items != NULL);
    NT_ASSERT(scratch != NULL);
    NT_ASSERT(items != scratch);

    sort_by_batch_key(items, count, scratch);
    nt_sort_by_key(items, count, scratch);
}

/* ---- View depth calculation ---- */

float nt_calc_view_depth(uint32_t entity_id, const float view_pos[3], const float view_fwd[3]) {
    nt_entity_t entity = {.id = entity_id};
    const float *m = nt_transform_comp_world_matrix(entity);

    /* Extract world position from column 3 of the 4x4 matrix (column-major) */
    float dx = m[12] - view_pos[0];
    float dy = m[13] - view_pos[1];
    float dz = m[14] - view_pos[2];

    return (dx * view_fwd[0]) + (dy * view_fwd[1]) + (dz * view_fwd[2]);
}
