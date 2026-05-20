#ifndef NT_TEST_HELPER_UI_WALKER_FIXTURE_H
#define NT_TEST_HELPER_UI_WALKER_FIXTURE_H

/* Shared test fixture for nt_ui walker tests.
 *
 * Replaces ~110 lines of duplicated setUp / tearDown + make_test_material
 * across the five test_nt_ui_walker_*.c binaries and test_nt_ui_stats.c.
 *
 * Typical usage:
 *
 *     static uint64_t s_arena[NT_UI_DEFAULT_ARENA_SIZE / 8u];
 *     static ui_walker_fixture_t s_fx;
 *
 *     void setUp(void) {
 *         ui_walker_fixture_init(&s_fx, s_arena, sizeof s_arena, UI_WALKER_FX_BIND_ALL);
 *     }
 *     void tearDown(void) {
 *         ui_walker_fixture_shutdown(&s_fx);
 *     }
 *
 *     // ... tests reference s_fx.ctx, s_fx.atlas, s_fx.sprite_material, ...
 *
 * Death-tests for "setter not called before walk" pass a partial bind mask:
 *
 *     ui_walker_fixture_init(&s_fx, ..., UI_WALKER_FX_BIND_ALL & ~UI_WALKER_FX_BIND_ATLAS);
 *     NT_TEST_EXPECT_ASSERT(nt_ui_walk(s_fx.ctx, &target));
 */

#include <stdint.h>

#include "material/nt_material.h"
#include "test_helpers/ui_atlas.h"
#include "ui/nt_ui.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Per-field bind mask. Controls which walker setters fixture_init calls
 * on the freshly-created context. A field whose bit is zero stays at the
 * zero-initialised default -- walker asserts will fire on its absence.
 *
 * Underlying type is uint32_t (not an enum) because callers combine bits
 * arbitrarily -- e.g. ALL & ~ATLAS produces a valid mask value (6) that
 * isn't an enum member; an enum type would trip clang-analyzer's
 * EnumCastOutOfRange check on every such expression. */
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
} ui_walker_fixture_t;

/* Brings up nt_hash, nt_gfx (stub), nt_resource, nt_atlas, nt_font,
 * nt_material, nt_sprite_renderer, nt_text_renderer, nt_stats; opens a
 * gfx frame/pass; creates the ui_atlas helper and two test materials;
 * creates the nt_ui context in the caller's arena; then calls the
 * walker setters selected by `bind`. The custom handler is always
 * cleared to (NULL, NULL).
 *
 * Aborts via Unity TEST_ASSERT if any subsystem fails to initialise. */
void ui_walker_fixture_init(ui_walker_fixture_t *fx, void *arena, size_t arena_size, ui_walker_fx_bind_t bind);

/* Tears down everything ui_walker_fixture_init brought up, in reverse
 * order. Safe to call after a partial init failure (best-effort). */
void ui_walker_fixture_shutdown(ui_walker_fixture_t *fx);

/* Allocates an additional test material backed by a fresh virtual pack
 * (unique vs / fs shader resource ids per call). Tests that need more
 * than the two materials fixture_init builds (e.g. flush-boundary tests)
 * use this. Asserts the renderer is initialised. */
nt_material_t ui_walker_fixture_make_material(void);

#ifdef __cplusplus
}
#endif

#endif /* NT_TEST_HELPER_UI_WALKER_FIXTURE_H */
