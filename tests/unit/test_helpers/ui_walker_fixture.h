#ifndef NT_TEST_HELPER_UI_WALKER_FIXTURE_H
#define NT_TEST_HELPER_UI_WALKER_FIXTURE_H

/* Shared setUp/tearDown for nt_ui walker tests. */

#include <stdint.h>

#include "font/nt_font.h"
#include "material/nt_material.h"
#include "test_helpers/ui_atlas.h"
#include "ui/nt_ui.h"

#ifdef __cplusplus
extern "C" {
#endif

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
