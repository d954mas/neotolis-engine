#include "drawable_comp/nt_drawable_comp.h"

#include <stdlib.h>
#include <string.h>

#include "comp_storage/nt_comp_storage.h"
#include "core/nt_assert.h"
#include "core/nt_clamp.h"
#include "hash/nt_hash.h"

static nt_comp_storage_t s_storage;

/* ---- Data arrays ---- */

static nt_hash32_t *s_tags;
static bool *s_visible;
static float (*s_colors)[4];
static uint32_t *s_colors_packed; /* RGBA8 mirror; setters keep in sync with s_colors */

/* ---- Callbacks ---- */

/* Normalized 0..1 input; scale to 0..255 then use the shared clamp. */
static inline uint32_t pack_color_rgba(float r, float g, float b, float a) {
    return (uint32_t)nt_clamp_f_to_u8(r * 255.0F) | ((uint32_t)nt_clamp_f_to_u8(g * 255.0F) << 8) | ((uint32_t)nt_clamp_f_to_u8(b * 255.0F) << 16) | ((uint32_t)nt_clamp_f_to_u8(a * 255.0F) << 24);
}

static void drawable_default(uint16_t idx) {
    s_tags[idx] = (nt_hash32_t){.value = 0};
    s_visible[idx] = true;
    s_colors[idx][0] = 1.0F;
    s_colors[idx][1] = 1.0F;
    s_colors[idx][2] = 1.0F;
    s_colors[idx][3] = 1.0F;
    s_colors_packed[idx] = 0xFFFFFFFFU; /* white opaque */
}

static void drawable_swap(uint16_t dst, uint16_t src) {
    s_tags[dst] = s_tags[src];
    s_visible[dst] = s_visible[src];
    memcpy(s_colors[dst], s_colors[src], sizeof(float) * 4);
    s_colors_packed[dst] = s_colors_packed[src];
}

static void drawable_on_destroy(nt_entity_t entity) {
    if (nt_comp_storage_has(&s_storage, entity)) {
        nt_comp_storage_remove(&s_storage, entity);
    }
}

/* ---- Lifecycle ---- */

nt_result_t nt_drawable_comp_init(const nt_drawable_comp_desc_t *desc) {
    NT_ASSERT(desc != NULL);
    NT_ASSERT(desc->capacity > 0);

    nt_result_t res = nt_comp_storage_init(&s_storage, desc->capacity, drawable_default, drawable_swap);
    if (res != NT_OK) {
        return res;
    }

    uint16_t cap = desc->capacity;
    s_tags = (nt_hash32_t *)calloc(cap, sizeof(nt_hash32_t));
    s_visible = (bool *)calloc(cap, sizeof(bool));
    s_colors = calloc(cap, sizeof(*s_colors));
    s_colors_packed = (uint32_t *)calloc(cap, sizeof(uint32_t));

    if (!s_tags || !s_visible || !s_colors || !s_colors_packed) {
        nt_drawable_comp_shutdown();
        return NT_ERR_INIT_FAILED;
    }

    nt_entity_register_storage(&(nt_comp_storage_reg_t){
        .name = "drawable",
        .has = nt_drawable_comp_has,
        .on_destroy = drawable_on_destroy,
    });

    return NT_OK;
}

void nt_drawable_comp_shutdown(void) {
    free(s_tags);
    free(s_visible);
    free(s_colors);
    free(s_colors_packed);
    s_tags = NULL;
    s_visible = NULL;
    s_colors = NULL;
    s_colors_packed = NULL;
    nt_comp_storage_shutdown(&s_storage);
}

/* ---- Operations ---- */

bool nt_drawable_comp_add(nt_entity_t entity) { return nt_comp_storage_add(&s_storage, entity) != NT_INVALID_COMP_INDEX; }

bool nt_drawable_comp_has(nt_entity_t entity) { return nt_comp_storage_has(&s_storage, entity); }

void nt_drawable_comp_remove(nt_entity_t entity) { nt_comp_storage_remove(&s_storage, entity); }

nt_hash32_t *nt_drawable_comp_tag(nt_entity_t entity) {
    uint16_t idx = nt_comp_storage_index(&s_storage, entity);
    NT_ASSERT(idx != NT_INVALID_COMP_INDEX);
    return &s_tags[idx];
}

bool *nt_drawable_comp_visible(nt_entity_t entity) {
    uint16_t idx = nt_comp_storage_index(&s_storage, entity);
    NT_ASSERT(idx != NT_INVALID_COMP_INDEX);
    return &s_visible[idx];
}

const float *nt_drawable_comp_color(nt_entity_t entity) {
    uint16_t idx = nt_comp_storage_index(&s_storage, entity);
    NT_ASSERT(idx != NT_INVALID_COMP_INDEX);
    return s_colors[idx];
}

void nt_drawable_comp_set_color(nt_entity_t entity, float r, float g, float b, float a) {
    uint16_t idx = nt_comp_storage_index(&s_storage, entity);
    NT_ASSERT(idx != NT_INVALID_COMP_INDEX);
    s_colors[idx][0] = r;
    s_colors[idx][1] = g;
    s_colors[idx][2] = b;
    s_colors[idx][3] = a;
    s_colors_packed[idx] = pack_color_rgba(r, g, b, a);
}

void nt_drawable_comp_set_alpha(nt_entity_t entity, float a) {
    uint16_t idx = nt_comp_storage_index(&s_storage, entity);
    NT_ASSERT(idx != NT_INVALID_COMP_INDEX);
    s_colors[idx][3] = a;
    float *c = s_colors[idx];
    s_colors_packed[idx] = pack_color_rgba(c[0], c[1], c[2], a);
}

/* ---- Bulk SoA view ---- */

nt_drawable_comp_view_t nt_drawable_comp_view(void) {
    return (nt_drawable_comp_view_t){
        .count = nt_comp_storage_count(&s_storage),
        .sparse_indices = nt_comp_storage_sparse(&s_storage),
        .colors_packed = s_colors_packed,
        .visible = s_visible,
    };
}
