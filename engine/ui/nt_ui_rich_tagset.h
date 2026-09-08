#ifndef NT_UI_RICH_TAGSET_H
#define NT_UI_RICH_TAGSET_H

/* Rich-text PARSER vocabulary: name->value maps so markup can resolve a name (`<font=heading>`,
 * `<color=gold>`, `<img=alias:region/>`). The CODE-FIRST builder NEVER touches it (it gets real
 * values directly); registration exists ONLY for the parser. Keys are xxh64 of the name; no heap
 * (fixed caps). In-memory only, NEVER serialized -- so no _Static_assert pins its size. */

#include <stdbool.h>
#include <stdint.h>

#include "font/nt_font.h"         /* nt_font_t */
#include "resource/nt_resource.h" /* nt_resource_t (atlas alias handle) */
#include "ui/nt_ui_rich_text.h"   /* nt_ui_rich_fx_fn + object measure/draw fns */

/* ---- Vocabulary caps (no heap; NT_ASSERT on overflow) ---- */
#ifndef NT_UI_RICH_TAGSET_MAX_FONTS
#define NT_UI_RICH_TAGSET_MAX_FONTS 16
#endif
#ifndef NT_UI_RICH_TAGSET_MAX_ATLASES
#define NT_UI_RICH_TAGSET_MAX_ATLASES 16
#endif
#ifndef NT_UI_RICH_TAGSET_MAX_COLORS
#define NT_UI_RICH_TAGSET_MAX_COLORS 32
#endif
#ifndef NT_UI_RICH_TAGSET_MAX_EFFECTS
#define NT_UI_RICH_TAGSET_MAX_EFFECTS 16
#endif
#ifndef NT_UI_RICH_TAGSET_MAX_OBJECTS
#define NT_UI_RICH_TAGSET_MAX_OBJECTS 16
#endif

/* ---- Per-kind entries (key = xxh64 of the registered name) ---- */
typedef struct {
    uint64_t name_hash;
    nt_font_t family[4]; /* R/B/I/BI; <b>/<i> select within the family */
} nt_ui_rich_tagset_font_t;

typedef struct {
    uint64_t name_hash;
    nt_resource_t atlas; /* <img=alias:region/> -> this handle, then by-name region resolve */
} nt_ui_rich_tagset_atlas_t;

typedef struct {
    uint64_t name_hash;
    uint32_t color_abgr; /* semantic/named color -> packed AABBGGRR */
} nt_ui_rich_tagset_color_t;

/* Effect functions are captured into the block during parsing. */
typedef struct {
    uint64_t name_hash;
    bool tunable;        /* accepts nt_ui_rich_fx_params_t */
    nt_ui_rich_fx_fn fn; /* non-NULL effect callback */
    void *user_data;     /* borrowed until the consuming walk; NULL for tunable */
} nt_ui_rich_tagset_effect_t;

typedef struct {
    uint64_t name_hash;
    nt_ui_rich_object_measure_fn measure_fn;
    nt_ui_rich_object_draw_fn draw_fn;
    void *user_data;
} nt_ui_rich_tagset_object_t;

/* ---- The registry object (plain owned struct; NOT on nt_ui_context) ---- */
typedef struct nt_ui_rich_tagset {
    nt_ui_rich_tagset_font_t fonts[NT_UI_RICH_TAGSET_MAX_FONTS];
    uint32_t font_count;
    nt_ui_rich_tagset_atlas_t atlases[NT_UI_RICH_TAGSET_MAX_ATLASES];
    uint32_t atlas_count;
    nt_ui_rich_tagset_color_t colors[NT_UI_RICH_TAGSET_MAX_COLORS];
    uint32_t color_count;
    nt_ui_rich_tagset_effect_t effects[NT_UI_RICH_TAGSET_MAX_EFFECTS];
    uint32_t effect_count;
    nt_ui_rich_tagset_object_t objects[NT_UI_RICH_TAGSET_MAX_OBJECTS];
    uint32_t object_count;
} nt_ui_rich_tagset_t;

/* ---- Lifecycle (caller owns the storage; init clears, reset empties) ---- */
void nt_ui_rich_tagset_init(nt_ui_rich_tagset_t *ts);
void nt_ui_rich_tagset_reset(nt_ui_rich_tagset_t *ts);

/* ---- Registration (parser vocabulary only). Re-registering a name overrides it.
 * Each name is hashed via xxh64 (nt_hash64_str), the same key the atlas uses. ---- */
void nt_ui_rich_tagset_register_font(nt_ui_rich_tagset_t *ts, const char *name, const nt_font_t family[4]);
void nt_ui_rich_tagset_register_atlas(nt_ui_rich_tagset_t *ts, const char *name, nt_resource_t atlas);
void nt_ui_rich_tagset_register_color(nt_ui_rich_tagset_t *ts, const char *name, uint32_t color_abgr);
/* Tunable functions accept nt_ui_rich_fx_params_t through user_data. */
void nt_ui_rich_tagset_register_effect(nt_ui_rich_tagset_t *ts, const char *name, nt_ui_rich_fx_fn fn);
/* Borrow user_data until the consuming walk; markup amp/speed is ignored. */
void nt_ui_rich_tagset_register_effect_fn(nt_ui_rich_tagset_t *ts, const char *name, nt_ui_rich_fx_fn fn, void *user_data);
void nt_ui_rich_tagset_register_object_tag(nt_ui_rich_tagset_t *ts, const char *name, nt_ui_rich_object_measure_fn measure_fn, nt_ui_rich_object_draw_fn draw_fn, void *user_data);

/* ---- Lookup by name_hash (xxh64). Returns true + writes the out param on hit. ---- */
bool nt_ui_rich_tagset_lookup_font(const nt_ui_rich_tagset_t *ts, uint64_t name_hash, nt_font_t out_family[4]);
bool nt_ui_rich_tagset_lookup_atlas(const nt_ui_rich_tagset_t *ts, uint64_t name_hash, nt_resource_t *out_atlas);
bool nt_ui_rich_tagset_lookup_color(const nt_ui_rich_tagset_t *ts, uint64_t name_hash, uint32_t *out_color_abgr);
/* Lookup a callback and whether it accepts markup amp/speed parameters. */
bool nt_ui_rich_tagset_lookup_effect_fn(const nt_ui_rich_tagset_t *ts, uint64_t name_hash, bool *out_tunable, nt_ui_rich_fx_fn *out_fn, void **out_user);
bool nt_ui_rich_tagset_lookup_object(const nt_ui_rich_tagset_t *ts, uint64_t name_hash, nt_ui_rich_tagset_object_t *out_object);

#endif /* NT_UI_RICH_TAGSET_H */
