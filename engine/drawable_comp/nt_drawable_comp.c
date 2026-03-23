#include "drawable_comp/nt_drawable_comp.h"

#include <stdlib.h>
#include <string.h>

#include "comp_storage/nt_comp_storage.h"
#include "core/nt_assert.h"
#include "hash/nt_hash.h"

static nt_comp_storage_t s_storage;

/* ---- Data arrays ---- */

static nt_hash32_t *s_tags;
static bool *s_visible;
static float (*s_colors)[4]; /* float[N][4] */

/* ---- Callbacks ---- */

static void drawable_default(uint16_t idx) {
    s_tags[idx] = (nt_hash32_t){.value = 0};
    s_visible[idx] = true;
    s_colors[idx][0] = 1.0F;
    s_colors[idx][1] = 1.0F;
    s_colors[idx][2] = 1.0F;
    s_colors[idx][3] = 1.0F;
}

static void drawable_swap(uint16_t dst, uint16_t src) {
    s_tags[dst] = s_tags[src];
    s_visible[dst] = s_visible[src];
    memcpy(s_colors[dst], s_colors[src], sizeof(float) * 4);
}

static void drawable_on_destroy(nt_entity_t entity) {
    if (nt_comp_storage_has(&s_storage, entity)) {
        nt_comp_storage_remove(&s_storage, entity);
    }
}

/* ---- Lifecycle ---- */

nt_result_t nt_drawable_comp_init(const nt_drawable_comp_desc_t *desc) {
    if (!desc || desc->capacity == 0) {
        return NT_ERR_INVALID_ARG;
    }

    nt_result_t res = nt_comp_storage_init(&s_storage, desc->capacity, drawable_default, drawable_swap);
    if (res != NT_OK) {
        return res;
    }

    uint16_t cap = desc->capacity;
    s_tags = (nt_hash32_t *)calloc(cap, sizeof(nt_hash32_t));
    s_visible = (bool *)calloc(cap, sizeof(bool));
    s_colors = calloc(cap, sizeof(*s_colors));

    if (!s_tags || !s_visible || !s_colors) {
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
    s_tags = NULL;
    s_visible = NULL;
    s_colors = NULL;
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

float *nt_drawable_comp_color(nt_entity_t entity) {
    uint16_t idx = nt_comp_storage_index(&s_storage, entity);
    NT_ASSERT(idx != NT_INVALID_COMP_INDEX);
    return s_colors[idx];
}
