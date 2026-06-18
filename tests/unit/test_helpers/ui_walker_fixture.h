#ifndef NT_TEST_HELPER_UI_WALKER_FIXTURE_H
#define NT_TEST_HELPER_UI_WALKER_FIXTURE_H

/* Shared setUp/tearDown for nt_ui walker tests. */

#include <stdint.h>

#include "font/nt_font.h"
#include "material/nt_material.h"
#include "test_helpers/ui_atlas.h"
#include "test_helpers/ui_test_arena.h"
#include "ui/nt_ui.h"
#include "ui/nt_ui_dropdown.h" /* nt_ui_dropdown_style_t for the combo RED decls */
#include "ui/nt_ui_menu.h"     /* nt_ui_menu_state_t/_style_t for the immediate-menu RED decls */

#ifdef __cplusplus
extern "C" {
#endif

/* ============================ #236 immediate-mode RED forward decls ============================ */
/* Wave-0 scaffold: these begin/end + combo symbols + their probes are DEFINED by Plans 02-04. Declaring
 * them here lets test_ui_menu.c / test_ui_dropdown.c COMPILE (drive the new API surface) and link-FAIL
 * on the missing definitions — the RED signal. Once the engine headers declare these, the duplicate
 * compatible prototypes are harmless; this block is deleted when Plan 02-04 land the real decls. */

/* Item options (NEW, D-236-03): full OS-menu field set. enabled defaults true at the call site via a
 * designated-init {0} meaning "use call defaults" is NOT assumed — callers set .enabled explicitly. */
typedef struct {
    nt_atlas_region_ref_t icon; /* optional leading-gutter sprite (atlas.id==0 = aligned empty gutter) */
    const char *shortcut;       /* optional right-aligned shortcut text (e.g. "Ctrl+S") */
    bool enabled;               /* greyed + non-selectable when false */
    bool selected;              /* checkmark (toggle/radio menu items) */
    bool activatable;           /* false = an interactive child owns the click (§7 opt-out) */
    uint8_t _pad[1];
} nt_ui_menu_item_opts_t;

/* Menu immediate begin/end (DESIGN §3). */
void nt_ui_menu_begin(nt_ui_context_t *ctx, uint32_t menu_id, nt_ui_menu_state_t *st, nt_ui_menu_style_t *style);
bool nt_ui_menu_item(nt_ui_context_t *ctx, uint32_t key, const char *label);
bool nt_ui_menu_item_ex(nt_ui_context_t *ctx, uint32_t key, const char *label, nt_ui_menu_item_opts_t opts);
bool nt_ui_menu_item_begin(nt_ui_context_t *ctx, uint32_t key, nt_ui_menu_item_opts_t opts);
void nt_ui_menu_item_end(nt_ui_context_t *ctx);
bool nt_ui_menu_submenu_begin(nt_ui_context_t *ctx, uint32_t key, const char *label);
void nt_ui_menu_submenu_end(nt_ui_context_t *ctx);
void nt_ui_menu_separator(nt_ui_context_t *ctx);
void nt_ui_menu_separator_text(nt_ui_context_t *ctx, const char *label);
void nt_ui_menu_end(nt_ui_context_t *ctx);

/* Combo (dropdown) immediate begin/selectable/end (DESIGN §3). */
bool nt_ui_combo_begin(nt_ui_context_t *ctx, uint32_t id, const char *preview, nt_ui_dropdown_style_t *style, bool *open);
bool nt_ui_combo_preview_begin(nt_ui_context_t *ctx, uint32_t id, nt_ui_dropdown_style_t *style, bool *open);
void nt_ui_combo_preview_end(nt_ui_context_t *ctx);
bool nt_ui_combo_selectable(nt_ui_context_t *ctx, uint32_t key, const char *label, bool selected);
bool nt_ui_combo_selectable_begin(nt_ui_context_t *ctx, uint32_t key, bool selected);
void nt_ui_combo_selectable_end(nt_ui_context_t *ctx);
void nt_ui_combo_end(nt_ui_context_t *ctx);

/* New probes (NT_TEST_ACCESS-style; declared unconditionally for the RED tests): scope-stack id
 * derivation + the prev-frame frame-record focus introspection. */
uint32_t nt_ui_menu_test_item_id(uint32_t scope_id, uint32_t key, uint32_t idx);
uint32_t nt_ui_menu_test_focus_item_id(uint32_t menu_id, uint8_t depth);

/* Bit-mask of walker setters fixture_init calls; uint32_t so `ALL & ~MASK`
 * is well-defined (an enum type would trip EnumCastOutOfRange). */
typedef uint32_t ui_walker_fx_bind_t;

#define UI_WALKER_FX_BIND_NONE ((ui_walker_fx_bind_t)0U)
#define UI_WALKER_FX_BIND_ATLAS ((ui_walker_fx_bind_t)(1U << 0))
#define UI_WALKER_FX_BIND_SPRITE_MATERIAL ((ui_walker_fx_bind_t)(1U << 1))
#define UI_WALKER_FX_BIND_TEXT_MATERIAL ((ui_walker_fx_bind_t)(1U << 2))
#define UI_WALKER_FX_BIND_ALL (UI_WALKER_FX_BIND_ATLAS | UI_WALKER_FX_BIND_SPRITE_MATERIAL | UI_WALKER_FX_BIND_TEXT_MATERIAL)

typedef struct {
    nt_ui_context_t *ctx;
    minimal_ui_atlas_t atlas;
    nt_material_t sprite_material;
    nt_material_t text_material;
    /* Empty font handle bound to ctx->fonts[0]. Passes nt_font_valid check
     * (pool slot occupied) but has no resource data, so nt_text_renderer
     * silently skips at the units_per_em==0 guard. Lets walker tests
     * traverse TEXT commands without setting up real font blob/atlas. */
    nt_font_t stub_font;
} ui_walker_fixture_t;

void ui_walker_fixture_init(ui_walker_fixture_t *fx, void *arena, size_t arena_size, ui_walker_fx_bind_t bind);
void ui_walker_fixture_shutdown(ui_walker_fixture_t *fx);
/* Extra material backed by a fresh virtual pack -- unique vs/fs per call. */
nt_material_t ui_walker_fixture_make_material(void);

#ifdef __cplusplus
}
#endif

#endif /* NT_TEST_HELPER_UI_WALKER_FIXTURE_H */
