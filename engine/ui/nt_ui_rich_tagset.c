/* Rich-text parser vocabulary: name->value maps keyed by xxh64 of the registered name.
 * Fixed-cap tables, no heap; re-registering a name overrides it in place (find-or-append). */

#include "ui/nt_ui_rich_tagset.h"

#include <string.h>

#include "core/nt_assert.h"
#include "hash/nt_hash.h"

void nt_ui_rich_tagset_init(nt_ui_rich_tagset_t *ts) {
    NT_ASSERT(ts != NULL && "nt_ui_rich_tagset_init: NULL tagset");
    memset(ts, 0, sizeof *ts);
}

void nt_ui_rich_tagset_reset(nt_ui_rich_tagset_t *ts) {
    NT_ASSERT(ts != NULL && "nt_ui_rich_tagset_reset: NULL tagset");
    ts->font_count = 0;
    ts->atlas_count = 0;
    ts->color_count = 0;
    ts->effect_count = 0;
    ts->object_count = 0;
}

// #region register
void nt_ui_rich_tagset_register_font(nt_ui_rich_tagset_t *ts, const char *name, const nt_font_t family[4]) {
    NT_ASSERT(ts != NULL && name != NULL && name[0] != '\0' && family != NULL);
    const uint64_t h = nt_hash64_str(name).value;
    for (uint32_t i = 0; i < ts->font_count; i++) {
        if (ts->fonts[i].name_hash == h) {
            memcpy(ts->fonts[i].family, family, sizeof ts->fonts[i].family);
            return; /* override in place */
        }
    }
    NT_ASSERT(ts->font_count < NT_UI_RICH_TAGSET_MAX_FONTS && "rich tagset font table overflow");
    nt_ui_rich_tagset_font_t *e = &ts->fonts[ts->font_count++];
    e->name_hash = h;
    memcpy(e->family, family, sizeof e->family);
}

void nt_ui_rich_tagset_register_atlas(nt_ui_rich_tagset_t *ts, const char *name, nt_resource_t atlas) {
    NT_ASSERT(ts != NULL && name != NULL && name[0] != '\0');
    const uint64_t h = nt_hash64_str(name).value;
    for (uint32_t i = 0; i < ts->atlas_count; i++) {
        if (ts->atlases[i].name_hash == h) {
            ts->atlases[i].atlas = atlas;
            return;
        }
    }
    NT_ASSERT(ts->atlas_count < NT_UI_RICH_TAGSET_MAX_ATLASES && "rich tagset atlas table overflow");
    nt_ui_rich_tagset_atlas_t *e = &ts->atlases[ts->atlas_count++];
    e->name_hash = h;
    e->atlas = atlas;
}

void nt_ui_rich_tagset_register_color(nt_ui_rich_tagset_t *ts, const char *name, uint32_t color_abgr) {
    NT_ASSERT(ts != NULL && name != NULL && name[0] != '\0');
    const uint64_t h = nt_hash64_str(name).value;
    for (uint32_t i = 0; i < ts->color_count; i++) {
        if (ts->colors[i].name_hash == h) {
            ts->colors[i].color_abgr = color_abgr;
            return;
        }
    }
    NT_ASSERT(ts->color_count < NT_UI_RICH_TAGSET_MAX_COLORS && "rich tagset color table overflow");
    nt_ui_rich_tagset_color_t *e = &ts->colors[ts->color_count++];
    e->name_hash = h;
    e->color_abgr = color_abgr;
}

void nt_ui_rich_tagset_register_effect(nt_ui_rich_tagset_t *ts, const char *name, uint8_t effect_id) {
    NT_ASSERT(ts != NULL && name != NULL && name[0] != '\0');
    const uint64_t h = nt_hash64_str(name).value;
    for (uint32_t i = 0; i < ts->effect_count; i++) {
        if (ts->effects[i].name_hash == h) {
            ts->effects[i].effect_id = effect_id;
            ts->effects[i].fn = NULL; /* override a prior custom entry back to stock */
            ts->effects[i].user_data = NULL;
            return;
        }
    }
    NT_ASSERT(ts->effect_count < NT_UI_RICH_TAGSET_MAX_EFFECTS && "rich tagset effect table overflow");
    nt_ui_rich_tagset_effect_t *e = &ts->effects[ts->effect_count++];
    e->name_hash = h;
    e->effect_id = effect_id;
    e->fn = NULL; /* stock entry */
    e->user_data = NULL;
}

void nt_ui_rich_tagset_register_effect_fn(nt_ui_rich_tagset_t *ts, const char *name, nt_ui_rich_fx_fn fn, void *user_data) {
    NT_ASSERT(ts != NULL && name != NULL && name[0] != '\0' && fn != NULL && "rich tagset: custom effect needs a non-NULL fn");
    const uint64_t h = nt_hash64_str(name).value;
    for (uint32_t i = 0; i < ts->effect_count; i++) {
        if (ts->effects[i].name_hash == h) {
            ts->effects[i].effect_id = 0; /* custom entry: id unused (resolved via fn) */
            ts->effects[i].fn = fn;
            ts->effects[i].user_data = user_data;
            return; /* override in place */
        }
    }
    NT_ASSERT(ts->effect_count < NT_UI_RICH_TAGSET_MAX_EFFECTS && "rich tagset effect table overflow");
    nt_ui_rich_tagset_effect_t *e = &ts->effects[ts->effect_count++];
    e->name_hash = h;
    e->effect_id = 0;
    e->fn = fn;
    e->user_data = user_data;
}

void nt_ui_rich_tagset_register_object_tag(nt_ui_rich_tagset_t *ts, const char *name, nt_ui_rich_object_measure_fn measure_fn, nt_ui_rich_object_draw_fn draw_fn, void *user_data) {
    NT_ASSERT(ts != NULL && name != NULL && name[0] != '\0' && measure_fn != NULL);
    const uint64_t h = nt_hash64_str(name).value;
    for (uint32_t i = 0; i < ts->object_count; i++) {
        if (ts->objects[i].name_hash == h) {
            ts->objects[i].measure_fn = measure_fn;
            ts->objects[i].draw_fn = draw_fn;
            ts->objects[i].user_data = user_data;
            return;
        }
    }
    NT_ASSERT(ts->object_count < NT_UI_RICH_TAGSET_MAX_OBJECTS && "rich tagset object table overflow");
    nt_ui_rich_tagset_object_t *e = &ts->objects[ts->object_count++];
    e->name_hash = h;
    e->measure_fn = measure_fn;
    e->draw_fn = draw_fn;
    e->user_data = user_data;
}
// #endregion

// #region lookup
bool nt_ui_rich_tagset_lookup_font(const nt_ui_rich_tagset_t *ts, uint64_t name_hash, nt_font_t out_family[4]) {
    NT_ASSERT(ts != NULL && out_family != NULL);
    if (ts == NULL || out_family == NULL) { /* HARD: public lookup stays NULL-safe with NT_ASSERT OFF */
        return false;
    }
    for (uint32_t i = 0; i < ts->font_count; i++) {
        if (ts->fonts[i].name_hash == name_hash) {
            memcpy(out_family, ts->fonts[i].family, sizeof ts->fonts[i].family);
            return true;
        }
    }
    return false;
}

bool nt_ui_rich_tagset_lookup_atlas(const nt_ui_rich_tagset_t *ts, uint64_t name_hash, nt_resource_t *out_atlas) {
    NT_ASSERT(ts != NULL && out_atlas != NULL);
    if (ts == NULL || out_atlas == NULL) { /* HARD: public lookup stays NULL-safe with NT_ASSERT OFF */
        return false;
    }
    for (uint32_t i = 0; i < ts->atlas_count; i++) {
        if (ts->atlases[i].name_hash == name_hash) {
            *out_atlas = ts->atlases[i].atlas;
            return true;
        }
    }
    return false;
}

bool nt_ui_rich_tagset_lookup_color(const nt_ui_rich_tagset_t *ts, uint64_t name_hash, uint32_t *out_color_abgr) {
    NT_ASSERT(ts != NULL && out_color_abgr != NULL);
    if (ts == NULL || out_color_abgr == NULL) { /* HARD: public lookup stays NULL-safe with NT_ASSERT OFF */
        return false;
    }
    for (uint32_t i = 0; i < ts->color_count; i++) {
        if (ts->colors[i].name_hash == name_hash) {
            *out_color_abgr = ts->colors[i].color_abgr;
            return true;
        }
    }
    return false;
}

bool nt_ui_rich_tagset_lookup_effect(const nt_ui_rich_tagset_t *ts, uint64_t name_hash, uint8_t *out_effect_id) {
    NT_ASSERT(ts != NULL && out_effect_id != NULL);
    if (ts == NULL || out_effect_id == NULL) { /* HARD: public lookup stays NULL-safe with NT_ASSERT OFF */
        return false;
    }
    for (uint32_t i = 0; i < ts->effect_count; i++) {
        if (ts->effects[i].name_hash == name_hash && ts->effects[i].fn == NULL) {
            *out_effect_id = ts->effects[i].effect_id; /* stock entries only (a custom entry misses) */
            return true;
        }
    }
    return false;
}

bool nt_ui_rich_tagset_lookup_effect_fn(const nt_ui_rich_tagset_t *ts, uint64_t name_hash, uint8_t *out_effect_id, nt_ui_rich_fx_fn *out_fn, void **out_user) {
    NT_ASSERT(ts != NULL && out_effect_id != NULL && out_fn != NULL && out_user != NULL);
    if (ts == NULL || out_effect_id == NULL || out_fn == NULL || out_user == NULL) { /* HARD: public lookup stays NULL-safe with NT_ASSERT OFF */
        return false;
    }
    for (uint32_t i = 0; i < ts->effect_count; i++) {
        if (ts->effects[i].name_hash == name_hash) {
            *out_effect_id = ts->effects[i].effect_id;
            *out_fn = ts->effects[i].fn; /* NULL -> stock (use out_effect_id); non-NULL -> custom */
            *out_user = ts->effects[i].user_data;
            return true;
        }
    }
    return false;
}

bool nt_ui_rich_tagset_lookup_object(const nt_ui_rich_tagset_t *ts, uint64_t name_hash, nt_ui_rich_tagset_object_t *out_object) {
    NT_ASSERT(ts != NULL && out_object != NULL);
    if (ts == NULL || out_object == NULL) { /* HARD: public lookup stays NULL-safe with NT_ASSERT OFF */
        return false;
    }
    for (uint32_t i = 0; i < ts->object_count; i++) {
        if (ts->objects[i].name_hash == name_hash) {
            *out_object = ts->objects[i];
            return true;
        }
    }
    return false;
}
// #endregion
