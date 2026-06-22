#ifndef NT_UI_RICH_TAGSET_H
#define NT_UI_RICH_TAGSET_H

/* Rich-text PARSER vocabulary: a standalone owned object holding the
 * name->value maps the markup parser needs to turn a name-string into a real value
 * (`<font=heading>`, `<color=gold>`, `<img=alias:region/>`, `<wave>`, `<obj=portrait/>`).
 *
 * It is NOT on nt_ui_context and the CODE-FIRST builder NEVER touches it -- the builder
 * gets real values directly (push_color(abgr), push_font(family), image(ref)). Registration
 * exists ONLY so the parser can resolve a name. Keys are xxh64 of the name (the same
 * convention atlas region names use); no heap -- fixed `#define` caps, NT_ASSERT on overflow.
 *
 * In-memory-only: this struct is NEVER serialized (no pack/ABI contract), so there is
 * deliberately no _Static_assert pinning its size -- native and wasm layouts may differ. */

#include <stdbool.h>
#include <stdint.h>

#include "font/nt_font.h"         /* nt_font_t */
#include "resource/nt_resource.h" /* nt_resource_t (atlas alias handle) */
#include "ui/nt_ui_rich_fx.h"     /* nt_ui_rich_fx_result_t / nt_ui_rich_fx_identity */
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

/* An <fx=name> resolves to EITHER a stock catalog id OR a game-supplied custom fn. fn==NULL ->
 * stock (effect_id carries the catalog index); fn!=NULL -> custom (effect_id is ignored, the
 * builder captures fn+user_data into the per-block table at build time). */
typedef struct {
    uint64_t name_hash;
    uint8_t effect_id;   /* stock-catalog index when fn==NULL */
    nt_ui_rich_fx_fn fn; /* custom effect callback, or NULL for a stock effect */
    void *user_data;     /* passed back to the custom fn (NULL for stock) */
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
void nt_ui_rich_tagset_register_effect(nt_ui_rich_tagset_t *ts, const char *name, uint8_t effect_id);
/* Register a game-supplied custom effect fn under `name` so `<fx=name>` resolves to it (resolved
 * BEFORE stock). The (fn,user_data) is captured into the run-list at parse/build time. */
void nt_ui_rich_tagset_register_effect_fn(nt_ui_rich_tagset_t *ts, const char *name, nt_ui_rich_fx_fn fn, void *user_data);
void nt_ui_rich_tagset_register_object_tag(nt_ui_rich_tagset_t *ts, const char *name, nt_ui_rich_object_measure_fn measure_fn, nt_ui_rich_object_draw_fn draw_fn, void *user_data);

/* ---- Lookup by name_hash (xxh64). Returns true + writes the out param on hit. ---- */
bool nt_ui_rich_tagset_lookup_font(const nt_ui_rich_tagset_t *ts, uint64_t name_hash, nt_font_t out_family[4]);
bool nt_ui_rich_tagset_lookup_atlas(const nt_ui_rich_tagset_t *ts, uint64_t name_hash, nt_resource_t *out_atlas);
bool nt_ui_rich_tagset_lookup_color(const nt_ui_rich_tagset_t *ts, uint64_t name_hash, uint32_t *out_color_abgr);
/* Stock-only effect lookup: hits ONLY a stock entry (fn==NULL), writes its catalog id. A custom
 * (fn) entry MISSES here -- use nt_ui_rich_tagset_lookup_effect_fn to resolve custom-or-stock. */
bool nt_ui_rich_tagset_lookup_effect(const nt_ui_rich_tagset_t *ts, uint64_t name_hash, uint8_t *out_effect_id);
/* Full effect lookup: hits a stock OR a custom entry. On a custom hit *out_fn!=NULL (+ *out_user);
 * on a stock hit *out_fn==NULL and *out_effect_id is the catalog id. Returns false on a miss. */
bool nt_ui_rich_tagset_lookup_effect_fn(const nt_ui_rich_tagset_t *ts, uint64_t name_hash, uint8_t *out_effect_id, nt_ui_rich_fx_fn *out_fn, void **out_user);
bool nt_ui_rich_tagset_lookup_object(const nt_ui_rich_tagset_t *ts, uint64_t name_hash, nt_ui_rich_tagset_object_t *out_object);

#endif /* NT_UI_RICH_TAGSET_H */
