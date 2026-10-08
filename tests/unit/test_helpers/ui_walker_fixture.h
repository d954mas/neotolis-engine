#ifndef NT_TEST_HELPER_UI_WALKER_FIXTURE_H
#define NT_TEST_HELPER_UI_WALKER_FIXTURE_H

/* Shared setUp/tearDown for nt_ui walker tests. */

#include <stdbool.h>
#include <stdint.h>

#include "font/nt_font.h"
#include "material/nt_material.h"
#include "test_helpers/nt_gfx_fake.h"
#include "test_helpers/ui_atlas.h"
#include "test_helpers/ui_test_arena.h"
#include "ui/nt_ui.h"
#include "ui/nt_ui_dropdown.h" /* nt_ui_dropdown_style_t for the combo fixture decls */
#include "ui/nt_ui_menu.h"     /* nt_ui_menu_state_t/_style_t for the immediate-menu fixture decls */

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
    nt_material_t text_material_b; /* a second text material, never bound to the ctx */
    /* Empty font handle bound to ctx->fonts[0]. Passes nt_font_valid check
     * (pool slot occupied) but has no resource data, so nt_text_renderer
     * silently skips at the units_per_em==0 guard. Lets walker tests
     * traverse TEXT commands without setting up real font blob/atlas. */
    nt_font_t stub_font;
    uint8_t *real_font_blob; /* shared by every ui_walker_fixture_make_real_font font; freed at shutdown */
    uint32_t real_font_blob_size;
    bool frame_ended; /* ui_walker_fixture_end_frame closed the fixture pass and frame */
} ui_walker_fixture_t;

/* Text vertex layout in frame storage (nt_text_renderer's vertex). */
#define UI_WALKER_FX_TEXT_VERTEX_BYTES 52U
#define UI_WALKER_FX_TEXT_GLYPH_BOUNDS_X1 36U /* glyph_bounds[2] */
#define UI_WALKER_FX_TEXT_DEPTH_BIAS 48U

void ui_walker_fixture_init(ui_walker_fixture_t *fx, void *arena, size_t arena_size, ui_walker_fx_bind_t bind);
void ui_walker_fixture_shutdown(ui_walker_fixture_t *fx);
/* Ends the fixture pass and frame: the fake executes the recorded draws only here. */
void ui_walker_fixture_end_frame(ui_walker_fixture_t *fx);
/* Ends the frame if still open, then opens a new frame and the fixture pass. */
void ui_walker_fixture_next_frame(ui_walker_fixture_t *fx);
/* Installs a hand-built command stream as the frozen frame. Runs an empty frame first so the stream's
 * nt_layout_index 0 (zeroed) resolves to the baked root: identity transform, band 0. */
void ui_walker_fixture_inject_cmds(ui_walker_fixture_t *fx, Clay_RenderCommand *cmds, int32_t count, int32_t capacity);

/* A new font that really draws: nt_test_font_blob over ASCII 32..126. */
nt_font_t ui_walker_fixture_make_real_font(ui_walker_fixture_t *fx);

/* Fake-backend draw trace probes (arm it with nt_gfx_fake_draw_trace_reset). */
uint32_t ui_walker_fx_draw_count(nt_program_t program);
nt_gfx_fake_draw_t ui_walker_fx_draw_at(nt_program_t program, uint32_t n);
/* Bytes of corner `corner` (0 BL, 1 BR, 2 TR, 3 TL) of quad `quad` of an indexed text draw. */
const uint8_t *ui_walker_fx_text_vertex(nt_gfx_fake_draw_t draw, uint32_t quad, uint32_t corner);
float ui_walker_fx_vertex_float(const uint8_t *vertex, uint32_t byte_offset);
static inline uint32_t ui_walker_fx_quads(nt_gfx_fake_draw_t draw) { return draw.num_indices / 6U; }

#ifdef __cplusplus
}
#endif

#endif /* NT_TEST_HELPER_UI_WALKER_FIXTURE_H */
