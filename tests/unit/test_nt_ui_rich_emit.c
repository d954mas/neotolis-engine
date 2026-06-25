/* nt_ui_rich_text emit: the widget declares ONE Clay FIXED block and self-emits
 * its solved TEXT atoms as positioned nt_text_renderer_draw_n spans during the walk. No GL:
 * the fixture's stub font (units_per_em=0) makes draw_n a counted no-op, so the walker-command
 * probe + the draw_n call counter prove emit without a real glyph atlas. Modeled on
 * test_ui_radial.c (ui_walker_fixture, no GL capture). */

#include <math.h>
#include <stdalign.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "atlas/nt_atlas.h"
#include "clay.h"
#include "font/nt_font.h"
#include "hash/nt_hash.h"
#include "material/nt_material.h"
#include "memory/nt_mem_scratch.h"
#include "renderers/nt_sprite_renderer.h"
#include "renderers/nt_text_renderer.h"
#include "test_helpers/nt_assert_trap.h"
#include "test_helpers/ui_walker_fixture.h"
#include "ui/nt_ui.h"
#include "ui/nt_ui_internal.h"
#include "ui/nt_ui_rich_fx.h"
#include "ui/nt_ui_rich_tagset.h"
#include "ui/nt_ui_rich_text.h"
#include "unity.h"

alignas(NT_UI_ARENA_ALIGN) static uint8_t s_arena[NT_UI_TEST_ARENA_SIZE];
static ui_walker_fixture_t s_fx;

#define FONT_SIZE_DEFAULT 16.0F /* the widget's NT_UI_RICH_DEFAULT_FONT_SIZE */

/* White region's name_hash in the minimal fixture atlas (ui_atlas.c "WHITEENU"). */
#define FX_WHITE_NAME_HASH 0x57484954454E4555ULL

/* Inline rich images now ride the PLAIN u8 sprite path: the gate only needs a valid base
 * material (nt_ui_image renders via the walker's base sprite material, not a per-image one).
 * The fixture already binds a standard sprite material, so reuse it. */
static nt_material_t make_rich_image_material(void) { return s_fx.sprite_material; }

void setUp(void) {
    ui_walker_fixture_init(&s_fx, s_arena, sizeof s_arena, UI_WALKER_FX_BIND_ALL);
    nt_mem_scratch_reset();
    s_fx.ctx->pending_rich = NULL;
    s_fx.ctx->rich_session_open = false;
    /* Deterministic metrics: advance = size/2/char; ascent 800/1000. */
    nt_font_test_set_metrics(s_fx.stub_font, 1000, 800, -200, 1000);
}

void tearDown(void) { ui_walker_fixture_shutdown(&s_fx); }

static bool approx(float a, float b) { return fabsf(a - b) < 1e-3F; }

/* Mirror nt_ui_rich_fx.c's rich_fx_clamp01 so the glow params asserts predict the clamped factor. */
static float rich_fx_clamp01_ref(float v) {
    if (v < 0.0F) {
        return 0.0F;
    }
    if (v > 1.0F) {
        return 1.0F;
    }
    return v;
}

/* Build a 2-run text-only block, declare the rich-text widget, walk once. */
static void frame_two_run_text(float container_w, nt_rich_align_t align) {
    nt_ui_rich_style_t base = nt_ui_rich_style_defaults();
    base.font_id[0] = s_fx.stub_font;

    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    CLAY({.id = CLAY_ID("rich_root"), .layout = {.sizing = {CLAY_SIZING_FIXED(400), CLAY_SIZING_FIXED(200)}}}) {
        nt_ui_rich_begin(s_fx.ctx, &base);
        nt_ui_rich_push_color(s_fx.ctx, 0xFF00FF00U); /* second run: distinct style -> split */
        nt_ui_rich_text_n(s_fx.ctx, "Hello ", 6);
        nt_ui_rich_pop(s_fx.ctx);
        nt_ui_rich_text_n(s_fx.ctx, "world", 5);
        nt_ui_rich_end(s_fx.ctx);
        nt_ui_rich_text(s_fx.ctx, CLAY_ID("rich").id, NULL, &base, container_w, align, 0.0F, NULL);
    }
    nt_ui_end(s_fx.ctx);

    nt_ui_target_t target = {.viewport = {0, 0, 800, 600}};
    nt_ui_walk(s_fx.ctx, &target);
}

/* (1) emit produces walker text commands (> 0) AND the draw_n span count matches the
 * solved TEXT line-fragments. Two runs on one wide line -> two spans. */
static void test_emit_produces_text_spans(void) {
    nt_text_renderer_test_reset_call_counters();
    frame_two_run_text(400.0F, NT_RICH_ALIGN_LEFT);

    const uint32_t spans = nt_text_renderer_test_draw_n_calls();
    TEST_ASSERT_TRUE_MESSAGE(spans > 0U, "rich-text emits at least one draw_n span");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(2U, spans, "two style runs on one line -> two draw_n spans");

    /* The widget-side span counter agrees with the renderer's draw_n call count. */
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(spans, nt_ui_rich_test_emit_span_count(s_fx.ctx), "widget span count == draw_n calls");

    /* The walker saw a CUSTOM command (the FIXED block) -> the walk text barrier flushed. */
    TEST_ASSERT_TRUE_MESSAGE(nt_ui_rich_test_atom_count(s_fx.ctx) >= 2U, "solver placed >=2 TEXT atoms");
}

/* (1b) a rich-only frame (NO nt_ui_label before the rich block) must bind the text material at the
 * CUSTOM dispatch boundary -- emit_text is the only OTHER binder, so without the dispatch bind the
 * text pipeline stays id==0 and flush discards the glyphs. Force a foreign material before the walk
 * so the assertion proves a REBIND, not a leftover. */
static void test_rich_only_frame_binds_text_material(void) {
    /* Bind a different (sprite) material into the text renderer so a missing rebind is detectable. */
    nt_text_renderer_set_material(s_fx.sprite_material);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(s_fx.sprite_material.id, nt_text_renderer_test_material_id(), "precondition: text renderer holds the foreign material");

    nt_text_renderer_test_reset_call_counters();
    frame_two_run_text(400.0F, NT_RICH_ALIGN_LEFT); /* rich block only, no nt_ui_label */

    TEST_ASSERT_TRUE_MESSAGE(nt_text_renderer_test_draw_n_calls() > 0U, "rich-only frame emits draw_n spans");
    TEST_ASSERT_TRUE_MESSAGE(nt_text_renderer_test_set_material_calls() > 0U, "rich dispatch rebinds the text material");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(s_fx.text_material.id, nt_text_renderer_test_material_id(), "rich-only frame leaves the ctx text material bound (glyphs not discarded)");
}

/* (2) the FIXED block size equals the solved total size. */
static void test_fixed_block_size_matches_solved(void) {
    frame_two_run_text(400.0F, NT_RICH_ALIGN_LEFT);
    /* Explicit container_w drives total_w; total_h is the one-line height (size 16 -> 16px). */
    TEST_ASSERT_TRUE_MESSAGE(approx(nt_ui_rich_test_total_w(s_fx.ctx), 400.0F), "FIXED width == explicit container_w");
    TEST_ASSERT_TRUE_MESSAGE(nt_ui_rich_test_total_h(s_fx.ctx) > 0.0F, "FIXED height == solved line height");
}

/* (3) a run WITHOUT an effect emits one span per line-fragment (batch-friendly):
 * one wide line of one style -> exactly one span. */
static void test_single_style_one_span_per_line(void) {
    nt_ui_rich_style_t base = nt_ui_rich_style_defaults();
    base.font_id[0] = s_fx.stub_font;

    nt_text_renderer_test_reset_call_counters();
    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    CLAY({.id = CLAY_ID("rich_root2"), .layout = {.sizing = {CLAY_SIZING_FIXED(400), CLAY_SIZING_FIXED(200)}}}) {
        nt_ui_rich_begin(s_fx.ctx, &base);
        nt_ui_rich_text_n(s_fx.ctx, "onerun", 6);
        nt_ui_rich_end(s_fx.ctx);
        nt_ui_rich_text(s_fx.ctx, CLAY_ID("rich2").id, NULL, &base, 400.0F, NT_RICH_ALIGN_LEFT, 0.0F, NULL);
    }
    nt_ui_end(s_fx.ctx);
    nt_ui_target_t target = {.viewport = {0, 0, 800, 600}};
    nt_ui_walk(s_fx.ctx, &target);

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1U, nt_text_renderer_test_draw_n_calls(), "one style, one line -> one draw_n span");
}

/* (4) double-walk determinism: re-walking the same frame yields identical span counts and
 * does NOT mutate the run-list (re-walkable, read-only on context). */
static void test_double_walk_is_deterministic(void) {
    nt_ui_rich_style_t base = nt_ui_rich_style_defaults();
    base.font_id[0] = s_fx.stub_font;

    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    CLAY({.id = CLAY_ID("rich_root3"), .layout = {.sizing = {CLAY_SIZING_FIXED(400), CLAY_SIZING_FIXED(200)}}}) {
        nt_ui_rich_begin(s_fx.ctx, &base);
        nt_ui_rich_text_n(s_fx.ctx, "alpha beta", 10);
        nt_ui_rich_end(s_fx.ctx);
        nt_ui_rich_text(s_fx.ctx, CLAY_ID("rich3").id, NULL, &base, 400.0F, NT_RICH_ALIGN_LEFT, 0.0F, NULL);
    }
    nt_ui_end(s_fx.ctx);
    nt_ui_target_t target = {.viewport = {0, 0, 800, 600}};

    nt_text_renderer_test_reset_call_counters();
    nt_ui_walk(s_fx.ctx, &target);
    const uint32_t spans_1 = nt_text_renderer_test_draw_n_calls();
    const uint32_t atoms_1 = nt_ui_rich_test_atom_count(s_fx.ctx);
    const uint32_t runs_1 = nt_ui_rich_test_run_count(s_fx.ctx);

    nt_text_renderer_test_reset_call_counters();
    nt_ui_walk(s_fx.ctx, &target); /* re-walk the SAME frozen frame */
    const uint32_t spans_2 = nt_text_renderer_test_draw_n_calls();
    const uint32_t atoms_2 = nt_ui_rich_test_atom_count(s_fx.ctx);
    const uint32_t runs_2 = nt_ui_rich_test_run_count(s_fx.ctx);

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(spans_1, spans_2, "double-walk emits the same span count");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(atoms_1, atoms_2, "double-walk leaves the solved atoms unchanged");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(runs_1, runs_2, "double-walk leaves the run-list unchanged (read-only)");
    TEST_ASSERT_TRUE_MESSAGE(spans_1 > 0U, "emit actually ran");
}

/* ===== Font-grouped text emit ===== */

/* Make a distinct stub font (own pool slot -> own .id) so a multi-face block has real font transitions.
 * units_per_em stays 0 so draw_n is a counted no-op (no glyph atlas needed). */
static nt_font_t make_stub_font(void) {
    return nt_font_create(&(nt_font_create_desc_t){
        .curve_texture_width = 64,
        .curve_texture_height = 64,
        .band_texture_height = 16,
        .band_count = 4,
        .measure_cache_size = 0,
    });
}

/* Build a block that interleaves the 4 font faces R B R I R BI R as separate runs (each <color>-split so
 * the solver keeps them as distinct atoms). With 4 distinct fonts in the family, set_font must be called
 * once PER DISTINCT FONT (4) -- NOT once per transition (7) -- proving the font-grouped multi-pass. */
static void frame_multi_face(const nt_font_t fam[4]) {
    nt_mem_scratch_reset();
    s_fx.ctx->pending_rich = NULL;
    s_fx.ctx->rich_session_open = false;

    nt_ui_rich_style_t base = nt_ui_rich_style_defaults();
    for (int i = 0; i < 4; i++) {
        base.font_id[i] = fam[i];
        nt_font_test_set_metrics(fam[i], 1000, 800, -200, 1000);
    }

    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    CLAY({.id = CLAY_ID("rich_mf_root"), .layout = {.sizing = {CLAY_SIZING_FIXED(400), CLAY_SIZING_FIXED(200)}}}) {
        nt_ui_rich_begin(s_fx.ctx, &base);
        /* R B R I R BI R: 7 runs, 4 distinct faces, interleaved (forces transitions in source order). */
        nt_ui_rich_text_n(s_fx.ctx, "r ", 2);
        nt_ui_rich_push_bold(s_fx.ctx);
        nt_ui_rich_text_n(s_fx.ctx, "b ", 2);
        nt_ui_rich_pop(s_fx.ctx);
        nt_ui_rich_text_n(s_fx.ctx, "r ", 2);
        nt_ui_rich_push_italic(s_fx.ctx);
        nt_ui_rich_text_n(s_fx.ctx, "i ", 2);
        nt_ui_rich_pop(s_fx.ctx);
        nt_ui_rich_text_n(s_fx.ctx, "r ", 2);
        nt_ui_rich_push_bold(s_fx.ctx);
        nt_ui_rich_push_italic(s_fx.ctx);
        nt_ui_rich_text_n(s_fx.ctx, "bi ", 3);
        nt_ui_rich_pop(s_fx.ctx);
        nt_ui_rich_pop(s_fx.ctx);
        nt_ui_rich_text_n(s_fx.ctx, "r", 1);
        nt_ui_rich_end(s_fx.ctx);
        nt_ui_rich_text(s_fx.ctx, CLAY_ID("rich_mf").id, NULL, &base, 800.0F, NT_RICH_ALIGN_LEFT, 0.0F, NULL);
    }
    nt_ui_end(s_fx.ctx);
    nt_ui_target_t target = {.viewport = {0, 0, 800, 600}};
    nt_ui_walk(s_fx.ctx, &target);
}

/* (1c) FONT-GROUPED emit: a block interleaving 4 distinct faces (R B R I R BI R, 7 transitions in source
 * order) calls set_font exactly 4 times -- once per DISTINCT font, NOT once per transition (7) -- because
 * emit groups atoms by font.id. Pinning 4 proves the per-transition flushes collapse. */
static void test_emit_groups_text_by_font(void) {
    /* font pool cap is 4 and the fixture already holds slot 1 (stub_font): reuse it as the regular face,
     * create the other 3 distinct faces -> 4 distinct font.id within the cap. */
    nt_font_t fam[4];
    fam[0] = s_fx.stub_font;
    for (int i = 1; i < 4; i++) {
        fam[i] = make_stub_font();
    }
    nt_text_renderer_test_reset_call_counters();
    frame_multi_face(fam);

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(4U, nt_text_renderer_test_set_font_calls(), "set_font called once per DISTINCT font (4), not once per transition (7)");
    TEST_ASSERT_TRUE_MESSAGE(nt_text_renderer_test_draw_n_calls() > 0U, "multi-face block still emits draw_n spans");

    for (int i = 1; i < 4; i++) {
        nt_font_destroy(fam[i]);
    }
}

/* (1d) a SINGLE-face block (one font, two color runs) calls set_font exactly once (no per-run regression
 * from the grouping pass -- distinct-font count is 1). */
static void test_emit_single_face_one_set_font(void) {
    nt_text_renderer_test_reset_call_counters();
    frame_two_run_text(400.0F, NT_RICH_ALIGN_LEFT); /* two color runs, ONE font */
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1U, nt_text_renderer_test_set_font_calls(), "single-face block calls set_font once (one distinct font)");
}

/* Build a block whose LAST run is italic on a family with NO italic face -> NT_UI_RICH_RUN_SYNTH_ITALIC.
 * Italic is last so a MISSING reset would leave the renderer at the shear, not 0 -- making the leak guard
 * observable after the walk. */
static void frame_synth_italic(void) {
    nt_mem_scratch_reset();
    s_fx.ctx->pending_rich = NULL;
    s_fx.ctx->rich_session_open = false;

    nt_ui_rich_style_t base = nt_ui_rich_style_defaults();
    base.font_id[0] = s_fx.stub_font; /* only the regular face; font_id[1..3] stay {0} -> no italic member */

    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    CLAY({.id = CLAY_ID("rich_si_root"), .layout = {.sizing = {CLAY_SIZING_FIXED(400), CLAY_SIZING_FIXED(200)}}}) {
        nt_ui_rich_begin(s_fx.ctx, &base);
        nt_ui_rich_text_n(s_fx.ctx, "up ", 3); /* upright run */
        nt_ui_rich_push_italic(s_fx.ctx);
        nt_ui_rich_text_n(s_fx.ctx, "lean", 4); /* italic, no italic face -> synthetic shear */
        nt_ui_rich_pop(s_fx.ctx);
        nt_ui_rich_end(s_fx.ctx);
        nt_ui_rich_text(s_fx.ctx, CLAY_ID("rich_si").id, NULL, &base, 800.0F, NT_RICH_ALIGN_LEFT, 0.0F, NULL);
    }
    nt_ui_end(s_fx.ctx);
    nt_ui_target_t target = {.viewport = {0, 0, 800, 600}};
    nt_ui_walk(s_fx.ctx, &target);
}

/* (1f) WIRING + LEAK-GUARD: an italic run on a family with no italic face raises NT_UI_RICH_RUN_SYNTH_ITALIC,
 * which the emit pass feeds to nt_text_renderer_set_oblique as NT_UI_RICH_SYNTH_ITALIC_SHEAR, then resets to
 * 0 at end of pass. Pins both halves: the shear reaches the renderer, and it does NOT leak past the block. */
static void test_emit_synth_italic_wires_and_resets_oblique(void) {
    nt_text_renderer_test_reset_call_counters();
    frame_synth_italic();
    TEST_ASSERT_TRUE_MESSAGE(approx(nt_text_renderer_test_max_oblique(), NT_UI_RICH_SYNTH_ITALIC_SHEAR), "SYNTH_ITALIC run feeds NT_UI_RICH_SYNTH_ITALIC_SHEAR to the renderer during emit");
    TEST_ASSERT_TRUE_MESSAGE(nt_text_renderer_test_oblique() == 0.0F, "emit resets oblique to 0 after the pass (no lean leak onto the next caller)");
}

/* (1g) NEGATIVE: a family WITH a real italic face uses it -> no synthetic shear ever reaches the renderer. */
static void test_emit_real_italic_face_no_oblique(void) {
    nt_font_t fam[4];
    fam[0] = s_fx.stub_font;
    for (int i = 1; i < 4; i++) {
        fam[i] = make_stub_font();
    }
    nt_text_renderer_test_reset_call_counters();
    frame_multi_face(fam); /* pushes italic / bold-italic against a family that HAS those faces */
    TEST_ASSERT_TRUE_MESSAGE(nt_text_renderer_test_max_oblique() == 0.0F, "real italic face -> no synthetic shear reaches the renderer");
    TEST_ASSERT_TRUE(nt_text_renderer_test_oblique() == 0.0F);
    for (int i = 1; i < 4; i++) {
        nt_font_destroy(fam[i]);
    }
}

/* Build a block with SIX distinct font families on one band (one regular-face text run each), more than
 * any per-block font array could hold. Each push_font swaps the whole family; default variant=regular picks font_id[0]. */
static void frame_six_distinct_fonts(const nt_font_t fam[6]) {
    nt_mem_scratch_reset();
    s_fx.ctx->pending_rich = NULL;
    s_fx.ctx->rich_session_open = false;

    nt_ui_rich_style_t base = nt_ui_rich_style_defaults();
    base.font_id[0] = fam[0];
    for (int i = 0; i < 6; i++) {
        nt_font_test_set_metrics(fam[i], 1000, 800, -200, 1000);
    }

    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    CLAY({.id = CLAY_ID("rich_6f_root"), .layout = {.sizing = {CLAY_SIZING_FIXED(400), CLAY_SIZING_FIXED(200)}}}) {
        nt_ui_rich_begin(s_fx.ctx, &base);
        nt_ui_rich_text_n(s_fx.ctx, "a ", 2); /* family 0 (base) */
        for (int i = 1; i < 6; i++) {
            const nt_font_t family[4] = {fam[i], fam[i], fam[i], fam[i]};
            nt_ui_rich_push_font(s_fx.ctx, family);
            nt_ui_rich_text_n(s_fx.ctx, "x ", 2);
            nt_ui_rich_pop(s_fx.ctx);
        }
        nt_ui_rich_end(s_fx.ctx);
        nt_ui_rich_text(s_fx.ctx, CLAY_ID("rich_6f").id, NULL, &base, 800.0F, NT_RICH_ALIGN_LEFT, 0.0F, NULL);
    }
    nt_ui_end(s_fx.ctx);
    nt_ui_target_t target = {.viewport = {0, 0, 800, 600}};
    nt_ui_walk(s_fx.ctx, &target);
}

/* (1e) REGRESSION: a band with SIX distinct fonts emits ALL six runs' text -- one set_font per distinct
 * font, NO cap, NO drop. Pin set_font==6 AND span_count==TEXT-atom count (every TEXT atom emits a span;
 * none dropped past a font limit). */
static void test_emit_more_than_four_fonts_no_drop(void) {
    nt_font_t fam[6];
    fam[0] = s_fx.stub_font;
    for (int i = 1; i < 6; i++) {
        fam[i] = make_stub_font();
    }
    nt_text_renderer_test_reset_call_counters();
    frame_six_distinct_fonts(fam);

    /* Count the band's TEXT atoms: every one must produce a span (no drop). */
    const uint32_t n = nt_ui_rich_test_atom_count(s_fx.ctx);
    uint32_t text_atoms = 0;
    for (uint32_t i = 0; i < n; i++) {
        if (nt_ui_rich_test_atom(s_fx.ctx, i).kind == NT_RICH_ATOM_TEXT) {
            text_atoms++;
        }
    }
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(6U, nt_text_renderer_test_set_font_calls(), "six distinct fonts -> set_font called six times (no cap)");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(text_atoms, nt_ui_rich_test_emit_span_count(s_fx.ctx), "every TEXT atom emits a span -> no font dropped past 4");

    for (int i = 1; i < 6; i++) {
        nt_font_destroy(fam[i]);
    }
}

/* ===== Inline IMAGE emit ===== */

/* Build [text][image][text] on one wide line, declare the rich-text widget, walk once.
 * The image rides the plain u8 sprite path (composed <color> packed to the sprite tint). */
static void frame_text_image_text(nt_material_t img_mat, nt_rich_valign_t valign, uint32_t tint_abgr) {
    /* Fresh frame: free the per-call rich scratch. pending_rich is released by the terminal
     * nt_ui_rich_text and re-zeroed by nt_ui_begin, so no manual clear is needed here. */
    nt_mem_scratch_reset();

    nt_ui_rich_style_t base = nt_ui_rich_style_defaults();
    base.font_id[0] = s_fx.stub_font;
    base.image_material = img_mat;
    /* By-name resolve target: the fixture's white region, addressed by its name_hash. */
    const nt_atlas_region_ref_t ref = nt_atlas_ref(s_fx.atlas.handle, FX_WHITE_NAME_HASH);

    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    CLAY({.id = CLAY_ID("rich_img_root"), .layout = {.sizing = {CLAY_SIZING_FIXED(400), CLAY_SIZING_FIXED(200)}}}) {
        nt_ui_rich_begin(s_fx.ctx, &base);
        if (tint_abgr != 0xFFFFFFFFU) {
            nt_ui_rich_push_color(s_fx.ctx, tint_abgr);
        }
        nt_ui_rich_text_n(s_fx.ctx, "A ", 2);
        nt_ui_rich_image(s_fx.ctx, ref, valign, 0.0F, 1.0F);
        nt_ui_rich_text_n(s_fx.ctx, " B", 2);
        if (tint_abgr != 0xFFFFFFFFU) {
            nt_ui_rich_pop(s_fx.ctx);
        }
        nt_ui_rich_end(s_fx.ctx);
        nt_ui_rich_text(s_fx.ctx, CLAY_ID("rich_img").id, NULL, &base, 400.0F, NT_RICH_ALIGN_LEFT, 0.0F, NULL);
    }
    nt_ui_end(s_fx.ctx);

    nt_ui_target_t target = {.viewport = {0, 0, 800, 600}};
    nt_ui_walk(s_fx.ctx, &target);
}

/* (5) [text][image][text]: the image emits >=1 sprite (4-vert region quad) AND the text
 * emits draw_n spans on the same line. The image rides the plain nt_ui_image sprite emit,
 * text rides draw_n -- both present. */
static void test_inline_image_emits_sprite_and_text(void) {
    const nt_material_t mat = make_rich_image_material();
    nt_text_renderer_test_reset_call_counters();
    frame_text_image_text(mat, NT_RICH_VALIGN_MIDDLE, 0xFFFFFFFFU);

    /* Text spans for "A " and " B" (image splits the run anyway -> two text runs). */
    TEST_ASSERT_TRUE_MESSAGE(nt_text_renderer_test_draw_n_calls() > 0U, "inline-image line still emits text draw_n spans");
    /* The image emitted a region quad (4 verts) via the standard sprite path. */
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(4U, nt_sprite_renderer_test_last_emit_vertex_count(), "inline image emits a 4-vert region quad via nt_ui_image");
    /* The widget reports exactly one inline image emitted. */
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1U, nt_ui_rich_test_image_emit_count(s_fx.ctx), "exactly one IMAGE atom emitted");
}

/* (5b) MATERIAL DEFAULT: a block that leaves image_material UNSET (id==0) inherits ctx->sprite_material
 * (nt_ui_set_sprite_material) -- the image still emits its region quad via the ctx default, no per-block set. */
static void test_inline_image_defaults_material_from_ctx(void) {
    nt_text_renderer_test_reset_call_counters();
    frame_text_image_text((nt_material_t){0}, NT_RICH_VALIGN_MIDDLE, 0xFFFFFFFFU); /* image_material left unset */
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(4U, nt_sprite_renderer_test_last_emit_vertex_count(), "unset image_material -> image emits via the ctx->sprite_material default");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1U, nt_ui_rich_test_image_emit_count(s_fx.ctx), "one IMAGE atom emitted via the ctx default material");
}

/* (6) the inline image's composed <color> reaches the standard u8 sprite tint: a run with
 * <color> r=255 g=128 b=0 a=255 emits that per-vertex color on the region quad (the walker
 * packs the run tint into backgroundColor -> a_color, not a float4 custom block). */
static void test_inline_image_tint_packed(void) {
    const nt_material_t mat = make_rich_image_material();
    /* 0xAABBGGRR: r=255 g=128 b=0 a=255 -> orange, alpha 1. */
    frame_text_image_text(mat, NT_RICH_VALIGN_MIDDLE, 0xFF0080FFU);

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(4U, nt_sprite_renderer_test_last_emit_vertex_count(), "image quad");
    for (uint32_t v = 0; v < 4U; v++) {
        uint8_t col[4] = {0};
        nt_sprite_renderer_test_last_emit_color(v, col); /* 0xAABBGGRR byte order: r,g,b,a */
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(255U, col[0], "tint.r == 255");
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(128U, col[1], "tint.g == 128");
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(0U, col[2], "tint.b == 0");
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(255U, col[3], "tint.a == 255 (full opacity)");
    }
}

/* Build [text][image][text] under a rich block carrying `opacity`; walk once. The image's
 * sprite tint alpha must fade with the parent opacity (folded at the walker), matching the text path. */
static void frame_image_with_opacity(nt_material_t img_mat, float opacity) {
    nt_mem_scratch_reset();
    s_fx.ctx->pending_rich = NULL;
    s_fx.ctx->rich_session_open = false;

    nt_ui_rich_style_t base = nt_ui_rich_style_defaults();
    base.font_id[0] = s_fx.stub_font;
    base.image_material = img_mat;
    const nt_atlas_region_ref_t ref = nt_atlas_ref(s_fx.atlas.handle, FX_WHITE_NAME_HASH);

    nt_ui_element_data_t block_data = {.user_data = NULL, .layer = 0U, .flags = (uint8_t)NT_UI_ELEM_FLAG_HAS_OPACITY, .transform = nt_ui_transform_defaults(), .opacity = opacity};

    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    CLAY({.id = CLAY_ID("rich_op_root"), .layout = {.sizing = {CLAY_SIZING_FIXED(400), CLAY_SIZING_FIXED(200)}}}) {
        nt_ui_rich_begin(s_fx.ctx, &base);
        nt_ui_rich_text_n(s_fx.ctx, "A ", 2);
        nt_ui_rich_image(s_fx.ctx, ref, NT_RICH_VALIGN_MIDDLE, 0.0F, 1.0F);
        nt_ui_rich_text_n(s_fx.ctx, " B", 2);
        nt_ui_rich_end(s_fx.ctx);
        nt_ui_rich_text(s_fx.ctx, CLAY_ID("rich_op").id, &block_data, &base, 400.0F, NT_RICH_ALIGN_LEFT, 0.0F, NULL);
    }
    nt_ui_end(s_fx.ctx);
    nt_ui_target_t target = {.viewport = {0, 0, 800, 600}};
    nt_ui_walk(s_fx.ctx, &target);
}

/* (6b) the inline image's sprite tint alpha fades with the parent opacity: a fully-opaque white run
 * under a 0.5-opacity rich block emits a_color.a == ~128 (base 255 * parent 0.5), matching the
 * TEXT/sprite opacity fold (the walker folds accum_opacity into backgroundColor.a). At opacity 1.0
 * the tint stays full (no fade). */
static void test_inline_image_fades_with_parent_opacity(void) {
    const nt_material_t mat = make_rich_image_material();

    frame_image_with_opacity(mat, 1.0F);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(4U, nt_sprite_renderer_test_last_emit_vertex_count(), "image quad (opacity 1)");
    for (uint32_t v = 0; v < 4U; v++) {
        uint8_t col[4] = {0};
        nt_sprite_renderer_test_last_emit_color(v, col);
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(255U, col[3], "tint.a == 255 at full opacity");
    }

    frame_image_with_opacity(mat, 0.5F);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(4U, nt_sprite_renderer_test_last_emit_vertex_count(), "image quad (opacity 0.5)");
    for (uint32_t v = 0; v < 4U; v++) {
        uint8_t col[4] = {0};
        nt_sprite_renderer_test_last_emit_color(v, col);
        /* rgb unchanged (white), alpha = base 255 * parent 0.5 ~= 128 -- same fold as TEXT/sprites. */
        TEST_ASSERT_TRUE_MESSAGE(col[0] == 255U && col[1] == 255U && col[2] == 255U, "tint.rgb unchanged by opacity (white)");
        TEST_ASSERT_TRUE_MESSAGE(col[3] >= 126U && col[3] <= 130U, "tint.a halved (~128) under 0.5 opacity (image fades like TEXT)");
    }
}

/* Build [text][img][text][img][text]: two inline images in one block. They emit immediately in the rich
 * self-emit -- one sprite batch, set_material bound once. */
static void frame_two_images(nt_material_t img_mat) {
    nt_mem_scratch_reset();
    s_fx.ctx->pending_rich = NULL;
    s_fx.ctx->rich_session_open = false;

    nt_ui_rich_style_t base = nt_ui_rich_style_defaults();
    base.font_id[0] = s_fx.stub_font;
    base.image_material = img_mat;
    const nt_atlas_region_ref_t ref = nt_atlas_ref(s_fx.atlas.handle, FX_WHITE_NAME_HASH);

    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    CLAY({.id = CLAY_ID("rich_2img_root"), .layout = {.sizing = {CLAY_SIZING_FIXED(400), CLAY_SIZING_FIXED(200)}}}) {
        nt_ui_rich_begin(s_fx.ctx, &base);
        nt_ui_rich_text_n(s_fx.ctx, "A ", 2);
        nt_ui_rich_image(s_fx.ctx, ref, NT_RICH_VALIGN_MIDDLE, 0.0F, 1.0F);
        nt_ui_rich_text_n(s_fx.ctx, " B ", 3);
        nt_ui_rich_image(s_fx.ctx, ref, NT_RICH_VALIGN_MIDDLE, 0.0F, 1.0F);
        nt_ui_rich_text_n(s_fx.ctx, " C", 2);
        nt_ui_rich_end(s_fx.ctx);
        nt_ui_rich_text(s_fx.ctx, CLAY_ID("rich_2img").id, NULL, &base, 800.0F, NT_RICH_ALIGN_LEFT, 0.0F, NULL);
    }
    nt_ui_end(s_fx.ctx);
    nt_ui_target_t target = {.viewport = {0, 0, 800, 600}};
    nt_sprite_renderer_test_reset_nonempty_flush_calls();
    nt_ui_walk(s_fx.ctx, &target);
}

/* (6c) two same-band inline images COALESCE: set_material binds once in rich_emit_images and both quads
 * share one staging batch with no flush between them, so the band drains in ONE non-empty flush, not two. */
static void test_two_inline_images_coalesce(void) {
    const nt_material_t mat = make_rich_image_material();
    frame_two_images(mat);

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(2U, nt_ui_rich_test_image_emit_count(s_fx.ctx), "both inline images emit in the immediate pass");
    /* ONE non-empty sprite flush across BOTH images of the single band -> they coalesced (no per-image flush). */
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1U, nt_sprite_renderer_test_nonempty_flush_calls(), "two band images coalesce into ONE sprite batch (one non-empty flush, not two)");
    /* The LAST image emitted is a 4-vert region quad with the right tint (white, full opacity). */
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(4U, nt_sprite_renderer_test_last_emit_vertex_count(), "second image emits a 4-vert region quad");
    for (uint32_t v = 0; v < 4U; v++) {
        uint8_t col[4] = {0};
        nt_sprite_renderer_test_last_emit_color(v, col);
        TEST_ASSERT_TRUE_MESSAGE(col[0] == 255U && col[1] == 255U && col[2] == 255U && col[3] == 255U, "second image tint == white, full opacity");
    }
}

/* Inline rich images self-emit in the CUSTOM block's sprite batch, NOT as Clay IMAGE commands: a
 * two-image block reports image-COMMAND count 0 while the rich-image probe reports 2. */
static void test_inline_images_not_in_image_command_count(void) {
    const nt_material_t mat = make_rich_image_material();
    frame_two_images(mat); /* [text][img][text][img][text] -- two inline images, no nt_ui_image */

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(2U, nt_ui_rich_test_image_emit_count(s_fx.ctx), "both inline images emit in the rich self-emit (probe == 2)");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0U, nt_ui_get_last_walk_image_command_count(s_fx.ctx), "inline rich images are NOT Clay IMAGE commands -> 0 image-command count");
}

/* (7) by-name resolve: <img by name_hash> resolves to the white region index (0) via
 * nt_atlas_ref + nt_atlas_resolve_ref -- no per-image registry. */
static void test_inline_image_resolves_by_name(void) {
    /* Independent of the widget: prove the resolve helper the widget uses lands on index 0. */
    nt_atlas_region_ref_t ref = nt_atlas_ref(s_fx.atlas.handle, FX_WHITE_NAME_HASH);
    nt_atlas_resolve_ref(&ref);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(s_fx.atlas.white_region_idx, ref.region, "by-name resolve lands on the white region index");

    /* And the widget emits using that resolved region (region index recorded by the solver). */
    const nt_material_t mat = make_rich_image_material();
    frame_text_image_text(mat, NT_RICH_VALIGN_MIDDLE, 0xFFFFFFFFU);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(s_fx.atlas.white_region_idx, nt_ui_rich_test_image_region(s_fx.ctx), "widget emits the by-name-resolved region");
}

/* (8) the image sits at the solver's valign-correct y: a TOP-valign image's solved y equals
 * the line pen_y (box top), distinct from a BASELINE image. The solved atom y is the emit y. */
static void test_inline_image_valign_y(void) {
    const nt_material_t mat = make_rich_image_material();

    frame_text_image_text(mat, NT_RICH_VALIGN_TOP, 0xFFFFFFFFU);
    const float y_top = nt_ui_rich_test_image_y(s_fx.ctx);

    frame_text_image_text(mat, NT_RICH_VALIGN_BASELINE, 0xFFFFFFFFU);
    const float y_base = nt_ui_rich_test_image_y(s_fx.ctx);

    /* TOP places the box at the line top (pen_y); BASELINE drops it to baseline - h.
     * For a 1x1 white region scaled by font metrics, the two differ. */
    TEST_ASSERT_TRUE_MESSAGE(y_base > y_top, "BASELINE image sits lower than a TOP-aligned image");
    TEST_ASSERT_TRUE_MESSAGE(approx(y_top, 0.0F), "TOP-aligned image sits at the line top (pen_y=0 on line 0)");
}

/* ===== Per-atom effects ===== */

/* The stock fns' documented curve constants (mirror nt_ui_rich_fx.c). */
#define FX_WAVE_AMP 3.0F
#define FX_WAVE_SPEED 6.0F
#define FX_WAVE_PHASE 0.5F
#define FX_SHAKE_AMP 2.0F
#define FX_SHAKE_RATE 30.0F
#define FX_RAINBOW_PHASE 0.07F
#define FX_RAINBOW_SPEED 0.30F
#define FX_PULSE_AMP 0.15F
#define FX_PULSE_SPEED 4.0F
#define FX_BOUNCE_AMP 6.0F
#define FX_BOUNCE_SPEED 6.0F
#define FX_BOUNCE_PHASE 0.5F
#define FX_GLOW_AMP 0.6F
#define FX_GLOW_SPEED 3.0F
#define FX_SWAY_AMP 4.0F
#define FX_SWAY_SPEED 3.0F
#define FX_SWAY_PHASE 0.5F
#define FX_FADE_STAGGER 0.05F
#define FX_FADE_DUR 0.30F

/* (9) the stock wave fn returns the deterministic offset.y == A*sin(t*SPEED + idx*PHASE).
 * Tests the fn ABI directly (headless, no walk) -- the contract the emit path folds in. */
static void test_fx_wave_deterministic(void) {
    const float base_color[4] = {1.0F, 1.0F, 1.0F, 1.0F};
    const float xy[2] = {0.0F, 0.0F};
    const float wh[2] = {10.0F, 16.0F};
    const float t = 0.25F;
    const uint32_t idx = 3U;
    const nt_ui_rich_fx_result_t r = nt_ui_rich_fx_wave(idx, NT_RICH_ATOM_TEXT, xy, wh, base_color, t, false, NULL);
    const float expect = FX_WAVE_AMP * sinf((t * FX_WAVE_SPEED) + ((float)idx * FX_WAVE_PHASE));
    TEST_ASSERT_TRUE_MESSAGE(approx(r.offset_y, expect), "wave offset.y == A*sin(t*SPEED + idx*PHASE)");
    TEST_ASSERT_TRUE_MESSAGE(approx(r.offset_x, 0.0F), "wave has no x shift");
    TEST_ASSERT_TRUE_MESSAGE(r.visible, "wave keeps the atom visible");
    /* At time 0 the curve is its t=0 value (headless-deterministic). */
    const nt_ui_rich_fx_result_t z = nt_ui_rich_fx_wave(idx, NT_RICH_ATOM_TEXT, xy, wh, base_color, 0.0F, false, NULL);
    TEST_ASSERT_TRUE_MESSAGE(approx(z.offset_y, FX_WAVE_AMP * sinf((float)idx * FX_WAVE_PHASE)), "wave t=0 is deterministic");
}

/* fade_in returns alpha 0 + visible=false before its window opens; alpha ramps after. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity) -- visibility asserts + params duration override
static void test_fx_fade_in_visibility(void) {
    const float base_color[4] = {1.0F, 1.0F, 1.0F, 1.0F};
    const float xy[2] = {0.0F, 0.0F};
    const float wh[2] = {10.0F, 16.0F};
    /* atom 0 at time 0: window just opening -> alpha 0 -> not visible. */
    const nt_ui_rich_fx_result_t r0 = nt_ui_rich_fx_fade_in(0U, NT_RICH_ATOM_TEXT, xy, wh, base_color, 0.0F, false, NULL);
    TEST_ASSERT_FALSE_MESSAGE(r0.visible, "fade_in at t=0 alpha 0 -> atom skipped");
    TEST_ASSERT_TRUE_MESSAGE(approx(r0.color[3], 0.0F), "fade_in alpha 0 at t=0");
    /* later: fully faded in -> visible, alpha 1. */
    const nt_ui_rich_fx_result_t r1 = nt_ui_rich_fx_fade_in(0U, NT_RICH_ATOM_TEXT, xy, wh, base_color, 1.0F, false, NULL);
    TEST_ASSERT_TRUE_MESSAGE(r1.visible, "fade_in fully open -> visible");
    TEST_ASSERT_TRUE_MESSAGE(approx(r1.color[3], 1.0F), "fade_in alpha 1 when fully open");

    /* PARAMS override: speed = reveal rate (1/sec) -> per-atom duration dur = 1/speed. speed=10 -> a
     * fast 0.1s fade; pick a mid-window time so the ramp is partial (not clamped) and pins 1/speed. */
    nt_ui_rich_fx_params_t p = {.amp = 0.0F, .speed = 10.0F};
    const uint32_t idx = 2U;
    const float tp = 0.15F; /* (tp - idx*STAGGER)/dur = (0.15 - 0.10)/0.10 = 0.5 -> mid-ramp, unclamped */
    const float dur_p = 1.0F / 10.0F;
    const float a_p = (tp - ((float)idx * FX_FADE_STAGGER)) / dur_p;
    const nt_ui_rich_fx_result_t tuned = nt_ui_rich_fx_fade_in(idx, NT_RICH_ATOM_TEXT, xy, wh, base_color, tp, false, &p);
    TEST_ASSERT_TRUE_MESSAGE(approx(tuned.color[3], base_color[3] * a_p), "fade_in tuned alpha == base * (t - idx*STAGGER)/(1/speed)");
    TEST_ASSERT_TRUE_MESSAGE(tuned.visible, "fade_in tuned mid-ramp is visible");
    /* The default duration (FX_FADE_DUR) would give a DIFFERENT alpha at the same time -> override took effect. */
    const float a_def = (tp - ((float)idx * FX_FADE_STAGGER)) / FX_FADE_DUR;
    TEST_ASSERT_TRUE_MESSAGE(fabsf(a_p - a_def) > 0.01F, "fade_in speed override changes the duration (differs from default)");
}

/* Mirror nt_ui_rich_fx.c's rich_fx_hash01 (xorshift-mix, 24-bit -> [0,1)) so the shake
 * assertions can predict the exact deterministic jitter. */
static float fx_hash01(uint32_t a, uint32_t b) {
    uint32_t hh = (a * 0x9E3779B1U) + (b * 0x85EBCA77U);
    hh ^= hh >> 15;
    hh *= 0x2C1B3C6DU;
    hh ^= hh >> 12;
    return (float)(hh & 0xFFFFFFU) / (float)0x1000000U;
}

/* Mirror nt_ui_rich_fx.c's rich_fx_hue_rgb (HSV s=v=1 -> RGB) for the rainbow assertion. */
static void fx_hue_rgb(float hue, float out_rgb[3]) {
    const float hp = (hue - floorf(hue)) * 6.0F;
    const float x = 1.0F - fabsf(fmodf(hp, 2.0F) - 1.0F);
    float r = 0.0F;
    float g = 0.0F;
    float b = 0.0F;
    if (hp < 1.0F) {
        r = 1.0F;
        g = x;
    } else if (hp < 2.0F) {
        r = x;
        g = 1.0F;
    } else if (hp < 3.0F) {
        g = 1.0F;
        b = x;
    } else if (hp < 4.0F) {
        g = x;
        b = 1.0F;
    } else if (hp < 5.0F) {
        r = x;
        b = 1.0F;
    } else {
        r = 1.0F;
        b = x;
    }
    out_rgb[0] = r;
    out_rgb[1] = g;
    out_rgb[2] = b;
}

/* (9b) shake is deterministic + bounded by AMP, and DEFINED for negative time (the
 * float->unsigned step quantize goes through a signed intermediate so countdown clocks don't UB). */
// NOLINTNEXTLINE(readability-function-cognitive-complexity) -- deterministic + bounded asserts + params override
static void test_fx_shake_deterministic(void) {
    const float base_color[4] = {1.0F, 1.0F, 1.0F, 1.0F};
    const float xy[2] = {0.0F, 0.0F};
    const float wh[2] = {10.0F, 16.0F};
    const float t = 0.25F;
    const uint32_t idx = 3U;
    const uint32_t step = (uint32_t)(int32_t)floorf(t * FX_SHAKE_RATE);
    const float ex = FX_SHAKE_AMP * (fx_hash01(idx, step) - 0.5F) * 2.0F;
    const float ey = FX_SHAKE_AMP * (fx_hash01(idx, step + 0x1000U) - 0.5F) * 2.0F;

    const nt_ui_rich_fx_result_t r = nt_ui_rich_fx_shake(idx, NT_RICH_ATOM_TEXT, xy, wh, base_color, t, false, NULL);
    TEST_ASSERT_TRUE_MESSAGE(approx(r.offset_x, ex), "shake offset.x == AMP*(hash-0.5)*2");
    TEST_ASSERT_TRUE_MESSAGE(approx(r.offset_y, ey), "shake offset.y == AMP*(hash(+0x1000)-0.5)*2");
    TEST_ASSERT_TRUE_MESSAGE(fabsf(r.offset_x) <= FX_SHAKE_AMP + 1e-3F, "shake x bounded by AMP");
    TEST_ASSERT_TRUE_MESSAGE(fabsf(r.offset_y) <= FX_SHAKE_AMP + 1e-3F, "shake y bounded by AMP");

    /* Re-evaluation at the same (idx,time) is identical (no global state). */
    const nt_ui_rich_fx_result_t r2 = nt_ui_rich_fx_shake(idx, NT_RICH_ATOM_TEXT, xy, wh, base_color, t, false, NULL);
    TEST_ASSERT_TRUE_MESSAGE(approx(r.offset_x, r2.offset_x), "shake x is deterministic");
    TEST_ASSERT_TRUE_MESSAGE(approx(r.offset_y, r2.offset_y), "shake y is deterministic");

    /* PARAMS override: amp = jitter px, speed = the quantize RATE -> same hash formula, tuned step. */
    nt_ui_rich_fx_params_t p = {.amp = 5.0F, .speed = 60.0F};
    const uint32_t step_p = (uint32_t)(int32_t)floorf(t * 60.0F);
    const float ex_p = 5.0F * (fx_hash01(idx, step_p) - 0.5F) * 2.0F;
    const float ey_p = 5.0F * (fx_hash01(idx, step_p + 0x1000U) - 0.5F) * 2.0F;
    const nt_ui_rich_fx_result_t tuned = nt_ui_rich_fx_shake(idx, NT_RICH_ATOM_TEXT, xy, wh, base_color, t, false, &p);
    TEST_ASSERT_TRUE_MESSAGE(approx(tuned.offset_x, ex_p), "shake tuned x == amp*(hash(idx,floor(t*speed))-0.5)*2");
    TEST_ASSERT_TRUE_MESSAGE(approx(tuned.offset_y, ey_p), "shake tuned y == amp*(hash(+0x1000)-0.5)*2");
}

/* (9b') shake is DEFINED + stable + bounded for NEGATIVE time (countdown/clock-offset clocks
 * are reachable). The .c quantizes through a signed intermediate so there is no out-of-range
 * float->unsigned conversion UB; the result must equal the same signed-quantize formula. */
static void test_fx_shake_negative_time_defined(void) {
    const float base_color[4] = {1.0F, 1.0F, 1.0F, 1.0F};
    const float xy[2] = {0.0F, 0.0F};
    const float wh[2] = {10.0F, 16.0F};
    const float tn = -0.25F;
    const uint32_t idx = 3U;
    const uint32_t stepn = (uint32_t)(int32_t)floorf(tn * FX_SHAKE_RATE);
    const float exn = FX_SHAKE_AMP * (fx_hash01(idx, stepn) - 0.5F) * 2.0F;

    const nt_ui_rich_fx_result_t rn = nt_ui_rich_fx_shake(idx, NT_RICH_ATOM_TEXT, xy, wh, base_color, tn, false, NULL);
    TEST_ASSERT_TRUE_MESSAGE(approx(rn.offset_x, exn), "shake defined for negative time (signed-intermediate quantize)");
    TEST_ASSERT_TRUE_MESSAGE(fabsf(rn.offset_x) <= FX_SHAKE_AMP + 1e-3F, "shake x bounded for negative time");
    TEST_ASSERT_TRUE_MESSAGE(fabsf(rn.offset_y) <= FX_SHAKE_AMP + 1e-3F, "shake y bounded for negative time");
}

/* (9c) rainbow REPLACES rgb with the hue curve (absolute tint) and keeps base alpha. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity) -- per-channel hue asserts + params speed override
static void test_fx_rainbow_deterministic(void) {
    const float base_color[4] = {0.2F, 0.4F, 0.6F, 0.8F};
    const float xy[2] = {0.0F, 0.0F};
    const float wh[2] = {10.0F, 16.0F};
    const float t = 0.5F;
    const uint32_t idx = 2U;
    const float hue = ((float)idx * FX_RAINBOW_PHASE) + (t * FX_RAINBOW_SPEED);
    float rgb[3];
    fx_hue_rgb(hue, rgb);

    const nt_ui_rich_fx_result_t r = nt_ui_rich_fx_rainbow(idx, NT_RICH_ATOM_TEXT, xy, wh, base_color, t, false, NULL);
    TEST_ASSERT_TRUE_MESSAGE(approx(r.color[0], rgb[0]), "rainbow r == hue(idx*PHASE + t*SPEED)");
    TEST_ASSERT_TRUE_MESSAGE(approx(r.color[1], rgb[1]), "rainbow g == hue curve");
    TEST_ASSERT_TRUE_MESSAGE(approx(r.color[2], rgb[2]), "rainbow b == hue curve");
    TEST_ASSERT_TRUE_MESSAGE(approx(r.color[3], 0.8F), "rainbow keeps the base alpha (REPLACES rgb only)");
    TEST_ASSERT_TRUE_MESSAGE(r.visible, "rainbow keeps the atom visible");

    /* PARAMS override: speed = hue turns/sec tunes the cycle (amp has no axis here -> ignored). */
    nt_ui_rich_fx_params_t p = {.amp = 9.0F, .speed = 1.0F};
    const float hue_p = ((float)idx * FX_RAINBOW_PHASE) + (t * 1.0F);
    float rgb_p[3];
    fx_hue_rgb(hue_p, rgb_p);
    const nt_ui_rich_fx_result_t tuned = nt_ui_rich_fx_rainbow(idx, NT_RICH_ATOM_TEXT, xy, wh, base_color, t, false, &p);
    TEST_ASSERT_TRUE_MESSAGE(approx(tuned.color[0], rgb_p[0]) && approx(tuned.color[1], rgb_p[1]) && approx(tuned.color[2], rgb_p[2]), "rainbow tuned == hue(idx*PHASE + t*speed)");
    TEST_ASSERT_TRUE_MESSAGE(approx(tuned.color[3], 0.8F), "rainbow tuned keeps the base alpha");
}

/* (9d) pulse breathes scale = 1 + AMP*sin(t*SPEED), within [1-AMP, 1+AMP], no tint/offset. */
static void test_fx_pulse_deterministic(void) {
    const float base_color[4] = {1.0F, 1.0F, 1.0F, 1.0F};
    const float xy[2] = {0.0F, 0.0F};
    const float wh[2] = {10.0F, 16.0F};
    const float t = 0.3F;
    const float expect = 1.0F + (FX_PULSE_AMP * sinf(t * FX_PULSE_SPEED));

    const nt_ui_rich_fx_result_t r = nt_ui_rich_fx_pulse(7U, NT_RICH_ATOM_TEXT, xy, wh, base_color, t, false, NULL);
    TEST_ASSERT_TRUE_MESSAGE(approx(r.scale, expect), "pulse scale == 1 + AMP*sin(t*SPEED)");
    TEST_ASSERT_TRUE_MESSAGE(r.scale >= 1.0F - FX_PULSE_AMP - 1e-3F && r.scale <= 1.0F + FX_PULSE_AMP + 1e-3F, "pulse scale within [1-AMP, 1+AMP]");
    TEST_ASSERT_TRUE_MESSAGE(approx(r.offset_x, 0.0F) && approx(r.offset_y, 0.0F), "pulse has no offset");
    TEST_ASSERT_TRUE_MESSAGE(r.visible, "pulse keeps the atom visible");

    /* PARAMS override: amp = scale delta, speed = rad/s -> same breathing formula, tuned constants. */
    nt_ui_rich_fx_params_t p = {.amp = 0.5F, .speed = 2.0F};
    const nt_ui_rich_fx_result_t tuned = nt_ui_rich_fx_pulse(7U, NT_RICH_ATOM_TEXT, xy, wh, base_color, t, false, &p);
    TEST_ASSERT_TRUE_MESSAGE(approx(tuned.scale, 1.0F + (0.5F * sinf(t * 2.0F))), "pulse tuned scale == 1 + amp*sin(t*speed)");
}

/* (9e) bounce hops always-upward (offset_y <= 0), bounded by AMP, with the closed-form
 * -AMP*|sin(t*SPEED + idx*PHASE)|; an amp/speed override produces the same formula tuned. */
static void test_fx_bounce_deterministic(void) {
    const float base_color[4] = {1.0F, 1.0F, 1.0F, 1.0F};
    const float xy[2] = {0.0F, 0.0F};
    const float wh[2] = {10.0F, 16.0F};
    const float t = 0.25F;
    const uint32_t idx = 3U;
    const float expect = -FX_BOUNCE_AMP * fabsf(sinf((t * FX_BOUNCE_SPEED) + ((float)idx * FX_BOUNCE_PHASE)));

    const nt_ui_rich_fx_result_t r = nt_ui_rich_fx_bounce(idx, NT_RICH_ATOM_TEXT, xy, wh, base_color, t, false, NULL);
    TEST_ASSERT_TRUE_MESSAGE(approx(r.offset_y, expect), "bounce offset.y == -AMP*|sin(t*SPEED + idx*PHASE)|");
    TEST_ASSERT_TRUE_MESSAGE(r.offset_y <= 1e-3F, "bounce hops always-upward (offset_y <= 0)");
    TEST_ASSERT_TRUE_MESSAGE(fabsf(r.offset_y) <= FX_BOUNCE_AMP + 1e-3F, "bounce bounded by AMP");
    TEST_ASSERT_TRUE_MESSAGE(approx(r.offset_x, 0.0F), "bounce has no x shift");
    TEST_ASSERT_TRUE_MESSAGE(r.visible, "bounce keeps the atom visible");

    /* PARAMS override: amp=12, speed=4 produce the same formula with the tuned constants. */
    nt_ui_rich_fx_params_t p = {.amp = 12.0F, .speed = 4.0F};
    const nt_ui_rich_fx_result_t tuned = nt_ui_rich_fx_bounce(idx, NT_RICH_ATOM_TEXT, xy, wh, base_color, t, false, &p);
    const float tuned_expect = -12.0F * fabsf(sinf((t * 4.0F) + ((float)idx * FX_BOUNCE_PHASE)));
    TEST_ASSERT_TRUE_MESSAGE(approx(tuned.offset_y, tuned_expect), "bounce tuned == -amp*|sin(t*speed + idx*PHASE)|");
}

/* (9f) glow brightens each rgb toward white (>= base), keeps alpha, bounded; an amp/speed override
 * tunes the same lerp. Visual-only (no offset). */
// NOLINTNEXTLINE(readability-function-cognitive-complexity) -- per-channel brighten asserts + params override
static void test_fx_glow_deterministic(void) {
    const float base_color[4] = {0.2F, 0.4F, 0.6F, 0.8F};
    const float xy[2] = {0.0F, 0.0F};
    const float wh[2] = {10.0F, 16.0F};
    const float t = 0.5F;
    const uint32_t idx = 2U;
    const float g = FX_GLOW_AMP * (0.5F + (0.5F * sinf(t * FX_GLOW_SPEED)));
    const float er = base_color[0] + ((1.0F - base_color[0]) * g);

    const nt_ui_rich_fx_result_t r = nt_ui_rich_fx_glow(idx, NT_RICH_ATOM_TEXT, xy, wh, base_color, t, false, NULL);
    TEST_ASSERT_TRUE_MESSAGE(approx(r.color[0], er), "glow r == base + (1-base)*amp*(0.5+0.5*sin)");
    TEST_ASSERT_TRUE_MESSAGE(r.color[0] >= base_color[0] - 1e-3F, "glow brightens r (>= base)");
    TEST_ASSERT_TRUE_MESSAGE(r.color[1] >= base_color[1] - 1e-3F, "glow brightens g (>= base)");
    TEST_ASSERT_TRUE_MESSAGE(r.color[2] >= base_color[2] - 1e-3F, "glow brightens b (>= base)");
    TEST_ASSERT_TRUE_MESSAGE(r.color[0] <= 1.0F + 1e-3F && r.color[1] <= 1.0F + 1e-3F && r.color[2] <= 1.0F + 1e-3F, "glow bounded by white");
    TEST_ASSERT_TRUE_MESSAGE(approx(r.color[3], 0.8F), "glow keeps the base alpha (color-only)");
    TEST_ASSERT_TRUE_MESSAGE(approx(r.offset_x, 0.0F) && approx(r.offset_y, 0.0F), "glow has no offset (visual-only color)");
    TEST_ASSERT_TRUE_MESSAGE(r.visible, "glow keeps the atom visible");

    /* PARAMS override: amp=1, speed=2 tune the same lerp. */
    nt_ui_rich_fx_params_t p = {.amp = 1.0F, .speed = 2.0F};
    const nt_ui_rich_fx_result_t tuned = nt_ui_rich_fx_glow(idx, NT_RICH_ATOM_TEXT, xy, wh, base_color, t, false, &p);
    const float gt = 1.0F * (0.5F + (0.5F * sinf(t * 2.0F)));
    TEST_ASSERT_TRUE_MESSAGE(approx(tuned.color[0], base_color[0] + ((1.0F - base_color[0]) * gt)), "glow tuned r matches amp/speed override");

    /* PARAMS amp > 1: the brighten factor clamps at 1 so rgb stays bounded by white (not over-driven).
     * speed<=0 selects the DEFAULT speed (FX_GLOW_SPEED), so the real factor is amp*(0.5+0.5*sin(t*SPEED)),
     * NOT amp*0.5 -- assert against the real clamped formula. amp=2 drives g well past 1 -> clamp01 -> white. */
    nt_ui_rich_fx_params_t over = {.amp = 2.0F, .speed = 0.0F};
    const nt_ui_rich_fx_result_t big = nt_ui_rich_fx_glow(idx, NT_RICH_ATOM_TEXT, xy, wh, base_color, t, false, &over);
    const float g_over = rich_fx_clamp01_ref(2.0F * (0.5F + (0.5F * sinf(t * FX_GLOW_SPEED))));
    TEST_ASSERT_TRUE_MESSAGE(big.color[0] <= 1.0F + 1e-3F && big.color[1] <= 1.0F + 1e-3F && big.color[2] <= 1.0F + 1e-3F, "glow amp>1 stays bounded by white (clamp01)");
    TEST_ASSERT_TRUE_MESSAGE(approx(big.color[0], base_color[0] + ((1.0F - base_color[0]) * g_over)), "glow amp>1 matches the real clamped formula (speed<=0 -> default speed)");
}

/* (9g) sway shifts horizontally within [-AMP, AMP] via AMP*sin(t*SPEED + idx*PHASE); no y/tint.
 * An amp/speed override tunes the same curve. */
static void test_fx_sway_deterministic(void) {
    const float base_color[4] = {1.0F, 1.0F, 1.0F, 1.0F};
    const float xy[2] = {0.0F, 0.0F};
    const float wh[2] = {10.0F, 16.0F};
    const float t = 0.25F;
    const uint32_t idx = 3U;
    const float expect = FX_SWAY_AMP * sinf((t * FX_SWAY_SPEED) + ((float)idx * FX_SWAY_PHASE));

    const nt_ui_rich_fx_result_t r = nt_ui_rich_fx_sway(idx, NT_RICH_ATOM_TEXT, xy, wh, base_color, t, false, NULL);
    TEST_ASSERT_TRUE_MESSAGE(approx(r.offset_x, expect), "sway offset.x == AMP*sin(t*SPEED + idx*PHASE)");
    TEST_ASSERT_TRUE_MESSAGE(fabsf(r.offset_x) <= FX_SWAY_AMP + 1e-3F, "sway x within [-AMP, AMP]");
    TEST_ASSERT_TRUE_MESSAGE(approx(r.offset_y, 0.0F), "sway has no y shift");
    TEST_ASSERT_TRUE_MESSAGE(r.visible, "sway keeps the atom visible");

    /* PARAMS override: amp=10, speed=2 produce the same formula with the tuned constants. */
    nt_ui_rich_fx_params_t p = {.amp = 10.0F, .speed = 2.0F};
    const nt_ui_rich_fx_result_t tuned = nt_ui_rich_fx_sway(idx, NT_RICH_ATOM_TEXT, xy, wh, base_color, t, false, &p);
    const float tuned_expect = 10.0F * sinf((t * 2.0F) + ((float)idx * FX_SWAY_PHASE));
    TEST_ASSERT_TRUE_MESSAGE(approx(tuned.offset_x, tuned_expect), "sway tuned == amp*sin(t*speed + idx*PHASE)");
}

/* (10) PARAMS: a tuned amp/speed produces a DIFFERENT curve than the default, and NULL params is
 * byte-identical to the default (the plain push_effect path). Tests the stock fn ABI directly. */
static void test_fx_params_override_vs_default(void) {
    const float base_color[4] = {1.0F, 1.0F, 1.0F, 1.0F};
    const float xy[2] = {0.0F, 0.0F};
    const float wh[2] = {10.0F, 16.0F};
    const float t = 0.25F;
    const uint32_t idx = 3U;

    /* NULL params == byte-identical to the compile-time default curve. */
    const nt_ui_rich_fx_result_t def = nt_ui_rich_fx_wave(idx, NT_RICH_ATOM_TEXT, xy, wh, base_color, t, false, NULL);
    const float def_expect = FX_WAVE_AMP * sinf((t * FX_WAVE_SPEED) + ((float)idx * FX_WAVE_PHASE));
    TEST_ASSERT_TRUE_MESSAGE(approx(def.offset_y, def_expect), "wave NULL params == compile-time default");

    /* Tuned amp = 14, speed = 5: the offset matches the SAME formula with the overridden constants,
     * and is measurably different from the default. */
    nt_ui_rich_fx_params_t p = {.amp = 14.0F, .speed = 5.0F};
    const nt_ui_rich_fx_result_t tuned = nt_ui_rich_fx_wave(idx, NT_RICH_ATOM_TEXT, xy, wh, base_color, t, false, &p);
    const float tuned_expect = 14.0F * sinf((t * 5.0F) + ((float)idx * FX_WAVE_PHASE));
    TEST_ASSERT_TRUE_MESSAGE(approx(tuned.offset_y, tuned_expect), "wave tuned offset == amp*sin(t*speed + idx*PHASE)");
    TEST_ASSERT_TRUE_MESSAGE(fabsf(tuned.offset_y - def.offset_y) > 0.01F, "tuned wave differs from the default");

    /* A field <=0 keeps that effect's default: amp=0 -> default amp, speed=5 overrides only speed. */
    nt_ui_rich_fx_params_t half = {.amp = 0.0F, .speed = 5.0F};
    const nt_ui_rich_fx_result_t mixed = nt_ui_rich_fx_wave(idx, NT_RICH_ATOM_TEXT, xy, wh, base_color, t, false, &half);
    const float mixed_expect = FX_WAVE_AMP * sinf((t * 5.0F) + ((float)idx * FX_WAVE_PHASE));
    TEST_ASSERT_TRUE_MESSAGE(approx(mixed.offset_y, mixed_expect), "amp<=0 keeps default amp, speed override applies");
}

/* Build [text][image][text] with a TUNED stock wave (via push_effect_ex) over the whole block;
 * record the image quad's first-vertex y (the wave offset folds into it). params NULL -> stock
 * default curve. Reuses the IMAGE-quad position probe -- the same one test (11) trusts. */
static float frame_tuned_wave_image_y(nt_material_t img_mat, const nt_ui_rich_fx_params_t *params, float time) {
    nt_mem_scratch_reset();
    s_fx.ctx->pending_rich = NULL;
    s_fx.ctx->rich_session_open = false;

    nt_ui_rich_style_t base = nt_ui_rich_style_defaults();
    base.font_id[0] = s_fx.stub_font;
    base.image_material = img_mat;
    const nt_atlas_region_ref_t ref = nt_atlas_ref(s_fx.atlas.handle, FX_WHITE_NAME_HASH);

    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    CLAY({.id = CLAY_ID("tw_root"), .layout = {.sizing = {CLAY_SIZING_FIXED(400), CLAY_SIZING_FIXED(200)}}}) {
        nt_ui_rich_begin(s_fx.ctx, &base);
        nt_ui_rich_push_effect_ex(s_fx.ctx, NT_UI_RICH_FX_ID_WAVE, params);
        nt_ui_rich_text_n(s_fx.ctx, "A ", 2);
        nt_ui_rich_image(s_fx.ctx, ref, NT_RICH_VALIGN_MIDDLE, 0.0F, 1.0F);
        nt_ui_rich_text_n(s_fx.ctx, " B", 2);
        nt_ui_rich_pop(s_fx.ctx);
        nt_ui_rich_end(s_fx.ctx);
        nt_ui_rich_text(s_fx.ctx, CLAY_ID("tw").id, NULL, &base, 400.0F, NT_RICH_ALIGN_LEFT, time, NULL);
    }
    nt_ui_end(s_fx.ctx);
    nt_ui_target_t target = {.viewport = {0, 0, 800, 600}};
    nt_ui_walk(s_fx.ctx, &target);
    float pos[3] = {0};
    nt_sprite_renderer_test_last_emit_position(0U, pos);
    return pos[1];
}

/* (10b) BUILDER push_effect_ex: a big-amplitude tuned wave shifts the image quad y differently than
 * the default wave -- proving the by-value params reach the stock fn at emit through the per-block table. */
static void test_fx_push_effect_ex_tunes_emit(void) {
    const nt_material_t mat = make_rich_image_material();
    const float t = 0.25F;
    const float y_default = frame_tuned_wave_image_y(mat, NULL, t); /* NULL -> stock default */
    nt_ui_rich_fx_params_t big = {.amp = 14.0F, .speed = 5.0F};
    const float y_tuned = frame_tuned_wave_image_y(mat, &big, t);
    TEST_ASSERT_TRUE_MESSAGE(fabsf(y_tuned - y_default) > 0.1F, "push_effect_ex tuned wave shifts the quad differently than the default");
}

/* Parse a wave markup over a one-glyph block and return the first solved atom's effect_id. A plain
 * <fx=wave> carries the STOCK id; a tuned <fx=wave amp=.. speed=..> routes through the per-block
 * custom table -> a CUSTOM id (>= NT_UI_RICH_FX_CUSTOM_BASE). The solve uses the test probe. */
static uint8_t markup_wave_atom_effect_id(const char *markup) {
    nt_ui_rich_tagset_t ts;
    nt_ui_rich_tagset_init(&ts);
    nt_ui_rich_tagset_register_effect(&ts, "wave", NT_UI_RICH_FX_ID_WAVE);

    nt_ui_rich_style_t base = nt_ui_rich_style_defaults();
    base.font_id[0] = s_fx.stub_font;

    nt_mem_scratch_reset();
    s_fx.ctx->pending_rich = NULL;
    s_fx.ctx->rich_session_open = false;
    nt_ui_rich_parse(s_fx.ctx, &ts, &base, markup, strlen(markup));
    nt_ui_rich_test_solve(s_fx.ctx, 400.0F, FONT_SIZE_DEFAULT);
    return nt_ui_rich_test_atom_effect_id(s_fx.ctx, 0U);
}

/* (10c) MARKUP <fx=wave amp=14 speed=5> applies the params via the per-block custom table: the
 * tuned markup carries a CUSTOM effect_id (the params path), the plain <fx=wave> a STOCK id. The
 * builder/markup VALUE parity (same emitted offset) is covered by the direct-ABI test (10) +
 * push_effect_ex emit test (10b); here we prove the markup front reaches the params path at all. */
static void test_fx_markup_params_apply(void) {
    const uint8_t id_default = markup_wave_atom_effect_id("<fx=wave>X</fx>");
    const uint8_t id_tuned = markup_wave_atom_effect_id("<fx=wave amp=14 speed=5>X</fx>");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(NT_UI_RICH_FX_ID_WAVE, id_default, "default <fx=wave> carries the stock id");
    TEST_ASSERT_TRUE_MESSAGE(id_tuned >= NT_UI_RICH_FX_CUSTOM_BASE, "tuned <fx=wave amp=.. speed=..> routes through the per-block custom table");

    /* An only-speed form also routes through the params path (amp omitted -> default amp). */
    const uint8_t id_speed_only = markup_wave_atom_effect_id("<fx=wave speed=5>X</fx>");
    TEST_ASSERT_TRUE_MESSAGE(id_speed_only >= NT_UI_RICH_FX_CUSTOM_BASE, "tuned <fx=wave speed=5> routes through the params path");
}

/* (10d) BUILDER push_effect_ex(WAVE, NULL): the NULL-params path carries the STOCK id (no custom-table
 * slot allocated), mirroring the markup-side STOCK-id assertion in (10c). */
static void test_fx_push_effect_ex_null_stock_id(void) {
    nt_ui_rich_style_t base = nt_ui_rich_style_defaults();
    base.font_id[0] = s_fx.stub_font;

    nt_mem_scratch_reset();
    s_fx.ctx->pending_rich = NULL;
    s_fx.ctx->rich_session_open = false;
    nt_ui_rich_begin(s_fx.ctx, &base);
    nt_ui_rich_push_effect_ex(s_fx.ctx, NT_UI_RICH_FX_ID_WAVE, NULL); /* NULL params -> stock id, no custom slot */
    nt_ui_rich_text_n(s_fx.ctx, "X", 1);
    nt_ui_rich_pop(s_fx.ctx);
    nt_ui_rich_end(s_fx.ctx);
    nt_ui_rich_test_solve(s_fx.ctx, 400.0F, FONT_SIZE_DEFAULT);

    const uint8_t id = nt_ui_rich_test_atom_effect_id(s_fx.ctx, 0U);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(NT_UI_RICH_FX_ID_WAVE, id, "push_effect_ex(WAVE, NULL) carries the stock id");
    TEST_ASSERT_TRUE_MESSAGE(id < NT_UI_RICH_FX_CUSTOM_BASE, "NULL-params path does not allocate a custom-table slot");
}

/* Build [text][image][text] with a per-atom EFFECT applied to the whole block. The image run
 * carries the effect_id; emit folds the wave offset into the image quad + the tint into the sprite color. */
static void frame_effected_image(nt_material_t img_mat, uint8_t effect_id, float time) {
    nt_mem_scratch_reset();
    s_fx.ctx->pending_rich = NULL;
    s_fx.ctx->rich_session_open = false;

    nt_ui_rich_style_t base = nt_ui_rich_style_defaults();
    base.font_id[0] = s_fx.stub_font;
    base.image_material = img_mat;
    const nt_atlas_region_ref_t ref = nt_atlas_ref(s_fx.atlas.handle, FX_WHITE_NAME_HASH);

    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    CLAY({.id = CLAY_ID("rich_fx_root"), .layout = {.sizing = {CLAY_SIZING_FIXED(400), CLAY_SIZING_FIXED(200)}}}) {
        nt_ui_rich_begin(s_fx.ctx, &base);
        nt_ui_rich_push_effect(s_fx.ctx, effect_id); /* effect on TEXT + IMAGE together */
        nt_ui_rich_text_n(s_fx.ctx, "A ", 2);
        nt_ui_rich_image(s_fx.ctx, ref, NT_RICH_VALIGN_MIDDLE, 0.0F, 1.0F);
        nt_ui_rich_text_n(s_fx.ctx, " B", 2);
        nt_ui_rich_pop(s_fx.ctx);
        nt_ui_rich_end(s_fx.ctx);
        nt_ui_rich_text(s_fx.ctx, CLAY_ID("rich_fx").id, NULL, &base, 400.0F, NT_RICH_ALIGN_LEFT, time, NULL);
    }
    nt_ui_end(s_fx.ctx);
    nt_ui_target_t target = {.viewport = {0, 0, 800, 600}};
    nt_ui_walk(s_fx.ctx, &target);
}

/* (11) an effect on an IMAGE run shifts the image quad (vs no effect) AND the line/box layout
 * is identical with vs without the effect (visual-only). */
static void test_fx_image_shifts_quad_visual_only(void) {
    const nt_material_t mat = make_rich_image_material();

    /* No effect: record the image quad's first-vertex y + the solved total height. */
    frame_effected_image(mat, 0U, 0.0F);
    float pos_noeff[3] = {0};
    nt_sprite_renderer_test_last_emit_position(0U, pos_noeff);
    const float total_h_noeff = nt_ui_rich_test_total_h(s_fx.ctx);
    const float img_y_noeff = nt_ui_rich_test_image_y(s_fx.ctx);

    /* Wave effect at a time whose offset.y is clearly non-zero. The image atom's fx_idx is its
     * solved index; the quad shifts by the wave offset, but the SOLVED box y is unchanged. */
    frame_effected_image(mat, NT_UI_RICH_FX_ID_WAVE, 0.4F);
    float pos_eff[3] = {0};
    nt_sprite_renderer_test_last_emit_position(0U, pos_eff);
    const float total_h_eff = nt_ui_rich_test_total_h(s_fx.ctx);
    const float img_y_eff = nt_ui_rich_test_image_y(s_fx.ctx);

    TEST_ASSERT_TRUE_MESSAGE(fabsf(pos_eff[1] - pos_noeff[1]) > 0.1F, "wave effect shifts the image quad y");
    /* Visual-only: the solved layout (total height + the image's SOLVED box y) is identical. */
    TEST_ASSERT_TRUE_MESSAGE(approx(total_h_eff, total_h_noeff), "effect does not change the solved block height (visual-only)");
    TEST_ASSERT_TRUE_MESSAGE(approx(img_y_eff, img_y_noeff), "effect does not change the image's solved box y (visual-only)");
}

/* (12) fade_in with alpha 0 (time 0) skips the image atom emit entirely (zero sprite verts);
 * the layout is unchanged regardless. */
static void test_fx_fade_in_skips_image(void) {
    const nt_material_t mat = make_rich_image_material();

    /* time 0: every atom's fade window is closed -> the image is skipped. */
    frame_effected_image(mat, NT_UI_RICH_FX_ID_FADE_IN, 0.0F);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0U, nt_ui_rich_test_image_emit_count(s_fx.ctx), "fade_in t=0 skips the image atom emit");
    /* The image's solved box is still reserved (layout unchanged) -- the solver placed it. */
    TEST_ASSERT_TRUE_MESSAGE(nt_ui_rich_test_total_h(s_fx.ctx) > 0.0F, "layout still solved with the box reserved");
}

/* ===== Links ===== */

static nt_pointer_t make_ptr(float x, float y, bool down, bool pressed, bool released) {
    nt_pointer_t p = {0};
    p.x = x;
    p.y = y;
    p.active = true;
    p.buttons[NT_BUTTON_LEFT].is_down = down;
    p.buttons[NT_BUTTON_LEFT].is_pressed = pressed;
    p.buttons[NT_BUTTON_LEFT].is_released = released;
    return p;
}

#define LINK_ID 0xABCD1234U
#define LINK_ROOT_X 50.0F
#define LINK_ROOT_Y 40.0F

/* Build a block "go <link>HERE</link> now" inside a root positioned at a known offset so the
 * link rect lands at a predictable absolute position; walk once with pointer `p`. Returns the
 * resolved link result. The two-pass bbox needs a warm-up frame (first frame has no prev bbox). */
static nt_ui_rich_result_t frame_link(const nt_pointer_t *p) {
    nt_mem_scratch_reset();
    s_fx.ctx->pending_rich = NULL;
    s_fx.ctx->rich_session_open = false;

    nt_ui_rich_style_t base = nt_ui_rich_style_defaults();
    base.font_id[0] = s_fx.stub_font;

    nt_ui_rich_result_t res = {0};
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, p, 1);
    CLAY({.id = CLAY_ID("link_root"), .layout = {.sizing = {CLAY_SIZING_FIXED(400), CLAY_SIZING_FIXED(200)}}, .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .offset = {LINK_ROOT_X, LINK_ROOT_Y}}}) {
        nt_ui_rich_begin(s_fx.ctx, &base);
        nt_ui_rich_text_n(s_fx.ctx, "go ", 3);
        nt_ui_rich_link(s_fx.ctx, LINK_ID);
        nt_ui_rich_text_n(s_fx.ctx, "HERE", 4);
        nt_ui_rich_link(s_fx.ctx, 0U);
        nt_ui_rich_text_n(s_fx.ctx, " now", 4);
        nt_ui_rich_end(s_fx.ctx);
        nt_ui_rich_text(s_fx.ctx, CLAY_ID("link_rt").id, NULL, &base, 400.0F, NT_RICH_ALIGN_LEFT, 0.0F, &res);
    }
    nt_ui_end(s_fx.ctx);
    nt_ui_target_t target = {.viewport = {0, 0, 800, 600}};
    nt_ui_walk(s_fx.ctx, &target);
    return res;
}

/* The pointer x to land inside the link rect: the root offset + "go " width + half the link
 * width. "go " is 3 chars; link "HERE" is 4 chars; advance = size/2 per char at size 16 -> 8px. */
static float link_hit_x(void) { return LINK_ROOT_X + (3.0F * 8.0F) + (2.0F * 8.0F); }

/* Pure-translation transform on the block so the link DRAWS at (local + LINK_XFORM_DX/DY) on screen.
 * Big enough that the transformed and untransformed link positions don't overlap (link rect ~32x16). */
#define LINK_XFORM_DX 120.0F
#define LINK_XFORM_DY 90.0F

/* Same block as frame_link, but the FIXED block carries a HAS_TRANSFORM translation (offset_x/y).
 * The block DRAWS its links shifted by (DX,DY); the hit-test must map the pointer through the block's
 * baked transform to resolve them there. Mirrors frame_link's two-pass warm-up contract. */
static nt_ui_rich_result_t frame_link_xform(const nt_pointer_t *p) {
    nt_mem_scratch_reset();
    s_fx.ctx->pending_rich = NULL;
    s_fx.ctx->rich_session_open = false;

    nt_ui_rich_style_t base = nt_ui_rich_style_defaults();
    base.font_id[0] = s_fx.stub_font;

    nt_ui_transform_t t = nt_ui_transform_defaults();
    t.offset_x = LINK_XFORM_DX;
    t.offset_y = LINK_XFORM_DY;
    const nt_ui_element_data_t *xdata = NT_UI_DATA_XFORM(0U, &t, 1.0F);

    nt_ui_rich_result_t res = {0};
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, p, 1);
    CLAY({.id = CLAY_ID("linkx_root"), .layout = {.sizing = {CLAY_SIZING_FIXED(400), CLAY_SIZING_FIXED(200)}}, .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .offset = {LINK_ROOT_X, LINK_ROOT_Y}}}) {
        nt_ui_rich_begin(s_fx.ctx, &base);
        nt_ui_rich_text_n(s_fx.ctx, "go ", 3);
        nt_ui_rich_link(s_fx.ctx, LINK_ID);
        nt_ui_rich_text_n(s_fx.ctx, "HERE", 4);
        nt_ui_rich_link(s_fx.ctx, 0U);
        nt_ui_rich_text_n(s_fx.ctx, " now", 4);
        nt_ui_rich_end(s_fx.ctx);
        nt_ui_rich_text(s_fx.ctx, CLAY_ID("linkx_rt").id, xdata, &base, 400.0F, NT_RICH_ALIGN_LEFT, 0.0F, &res);
    }
    nt_ui_end(s_fx.ctx);
    nt_ui_target_t target = {.viewport = {0, 0, 800, 600}};
    nt_ui_walk(s_fx.ctx, &target);
    return res;
}

/* X far outside every link rect (past the whole block width). */
static float link_miss_x(void) { return LINK_ROOT_X + 380.0F; }

/* Two-link block "go <link A>A1A1</link> or <link B>B2B2</link> now" so a press-on-A /
 * release-on-B sequence is expressible. Both links 4 chars wide; advance = 8px/char. */
#define LINK_ID_A 0x0A0A0A0AU
#define LINK_ID_B 0x0B0B0B0BU

static nt_ui_rich_result_t frame_two_links(const nt_pointer_t *p) {
    nt_mem_scratch_reset();
    s_fx.ctx->pending_rich = NULL;
    s_fx.ctx->rich_session_open = false;

    nt_ui_rich_style_t base = nt_ui_rich_style_defaults();
    base.font_id[0] = s_fx.stub_font;

    nt_ui_rich_result_t res = {0};
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, p, 1);
    CLAY({.id = CLAY_ID("link2_root"), .layout = {.sizing = {CLAY_SIZING_FIXED(400), CLAY_SIZING_FIXED(200)}}, .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .offset = {LINK_ROOT_X, LINK_ROOT_Y}}}) {
        nt_ui_rich_begin(s_fx.ctx, &base);
        nt_ui_rich_text_n(s_fx.ctx, "go ", 3);
        nt_ui_rich_link(s_fx.ctx, LINK_ID_A);
        nt_ui_rich_text_n(s_fx.ctx, "A1A1", 4);
        nt_ui_rich_link(s_fx.ctx, 0U);
        nt_ui_rich_text_n(s_fx.ctx, " or ", 4);
        nt_ui_rich_link(s_fx.ctx, LINK_ID_B);
        nt_ui_rich_text_n(s_fx.ctx, "B2B2", 4);
        nt_ui_rich_link(s_fx.ctx, 0U);
        nt_ui_rich_text_n(s_fx.ctx, " now", 4);
        nt_ui_rich_end(s_fx.ctx);
        nt_ui_rich_text(s_fx.ctx, CLAY_ID("link2_rt").id, NULL, &base, 400.0F, NT_RICH_ALIGN_LEFT, 0.0F, &res);
    }
    nt_ui_end(s_fx.ctx);
    nt_ui_target_t target = {.viewport = {0, 0, 800, 600}};
    nt_ui_walk(s_fx.ctx, &target);
    return res;
}

/* Link A mid-x: "go " (3) + half of "A1A1" (2) = 5 chars * 8px. Link B mid-x: "go A1A1 or "
 * (3+4+4 = 11) + half of "B2B2" (2) = 13 chars * 8px. */
static float link2_a_x(void) { return LINK_ROOT_X + (5.0F * 8.0F); }
static float link2_b_x(void) { return LINK_ROOT_X + (13.0F * 8.0F); }

/* (13) press+release-on-the-SAME-link click semantics (the press must START on the link the
 * release lands on). hover is purely geometric; a release without a matching press is no click. */
static void test_link_hover_and_click(void) {
    nt_mem_scratch_reset();
    s_fx.ctx->pending_rich = NULL;
    s_fx.ctx->rich_session_open = false;
    nt_ui_state_clear_all(s_fx.ctx); /* drop any stale press-latch cell from a prior test */

    const float hx = link_hit_x();
    const float hy = LINK_ROOT_Y + 8.0F; /* inside the one line (height ~16px) */
    const float mx = link_miss_x();

    /* Warm-up frame: the link rects exist this frame, but the block's bbox is resolved from the
     * PREV frame (two-pass), so hovering needs the block solved at least once. */
    nt_pointer_t over = make_ptr(hx, hy, false, false, false);
    (void)frame_link(&over);

    /* (a) hover without any press -> hovered == id, no click. */
    nt_ui_rich_result_t hov = frame_link(&over);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(LINK_ID, hov.hovered_link, "pointer over link rect -> hovered_link == id");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0U, hov.clicked_link, "hover without press -> no click");
    TEST_ASSERT_TRUE_MESSAGE(nt_ui_rich_test_link_rect_count(s_fx.ctx) >= 1U, "solver recorded >=1 link rect");

    /* (b) press inside the link, then release inside the SAME link -> real click. */
    nt_pointer_t press_in = make_ptr(hx, hy, true, true, false);
    nt_ui_rich_result_t pr = frame_link(&press_in);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0U, pr.clicked_link, "press alone is not a click");
    nt_pointer_t rel_in = make_ptr(hx, hy, false, false, true);
    nt_ui_rich_result_t clk = frame_link(&rel_in);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(LINK_ID, clk.hovered_link, "release over link still hovers");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(LINK_ID, clk.clicked_link, "press+release on the SAME link -> clicked == id");

    /* (c) press OUTSIDE all links, then release INSIDE a link -> NO false click. */
    nt_pointer_t press_out = make_ptr(mx, hy, true, true, false);
    (void)frame_link(&press_out);
    nt_pointer_t rel_in2 = make_ptr(hx, hy, false, false, true);
    nt_ui_rich_result_t false_clk = frame_link(&rel_in2);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(LINK_ID, false_clk.hovered_link, "release-over still hovers");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0U, false_clk.clicked_link, "press-outside + release-inside -> NO click");

    /* (d) press INSIDE the link, then release OUTSIDE -> no click. */
    nt_pointer_t press_in2 = make_ptr(hx, hy, true, true, false);
    (void)frame_link(&press_in2);
    nt_pointer_t rel_out = make_ptr(mx, hy, false, false, true);
    nt_ui_rich_result_t drag_off = frame_link(&rel_out);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0U, drag_off.hovered_link, "release outside hovers nothing");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0U, drag_off.clicked_link, "press-inside + release-outside -> NO click");

    /* (e) hovering nothing at all -> both zero. */
    nt_pointer_t idle_out = make_ptr(mx, hy, false, false, false);
    nt_ui_rich_result_t none = frame_link(&idle_out);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0U, none.hovered_link, "pointer outside link rects -> hovered_link == 0");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0U, none.clicked_link, "pointer outside link rects -> clicked_link == 0");
}

/* (13b) press inside link A, release inside link B -> NO click on EITHER link (the press target
 * and the release target must be the same link). */
static void test_link_press_a_release_b_no_click(void) {
    nt_mem_scratch_reset();
    s_fx.ctx->pending_rich = NULL;
    s_fx.ctx->rich_session_open = false;
    nt_ui_state_clear_all(s_fx.ctx);

    const float ax = link2_a_x();
    const float bx = link2_b_x();
    const float hy = LINK_ROOT_Y + 8.0F;

    /* Warm-up so the prev-frame bbox the link hit-test needs is populated. */
    nt_pointer_t idle = make_ptr(ax, hy, false, false, false);
    (void)frame_two_links(&idle);

    /* Sanity: the pointer really lands on A then on B (two distinct links solved). */
    nt_ui_rich_result_t hov_a = frame_two_links(&idle);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(LINK_ID_A, hov_a.hovered_link, "pointer at A-mid hovers link A");
    nt_pointer_t idle_b = make_ptr(bx, hy, false, false, false);
    nt_ui_rich_result_t hov_b = frame_two_links(&idle_b);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(LINK_ID_B, hov_b.hovered_link, "pointer at B-mid hovers link B");

    /* Press on A, release on B -> no click anywhere. */
    nt_pointer_t press_a = make_ptr(ax, hy, true, true, false);
    (void)frame_two_links(&press_a);
    nt_pointer_t rel_b = make_ptr(bx, hy, false, false, true);
    nt_ui_rich_result_t res = frame_two_links(&rel_b);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(LINK_ID_B, res.hovered_link, "release-over-B still hovers B");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0U, res.clicked_link, "press-A + release-B -> NO click");
}

/* Build "AA XX AA" where the SAME link id wraps the first and last word but NOT the middle word,
 * all on one line -> two disjoint same-link rects with a non-link gap between. Root floated at a
 * known offset so the gap x is predictable; walk once with pointer `p`. */
#define GAP_LINK_ID 0x77777777U

static nt_ui_rich_result_t frame_link_gap(const nt_pointer_t *p) {
    nt_mem_scratch_reset();
    s_fx.ctx->pending_rich = NULL;
    s_fx.ctx->rich_session_open = false;
    nt_ui_state_clear_all(s_fx.ctx);

    nt_ui_rich_style_t base = nt_ui_rich_style_defaults();
    base.font_id[0] = s_fx.stub_font;

    nt_ui_rich_result_t res = {0};
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, p, 1);
    CLAY(
        {.id = CLAY_ID("link_gap_root"), .layout = {.sizing = {CLAY_SIZING_FIXED(400), CLAY_SIZING_FIXED(200)}}, .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .offset = {LINK_ROOT_X, LINK_ROOT_Y}}}) {
        nt_ui_rich_begin(s_fx.ctx, &base);
        nt_ui_rich_link(s_fx.ctx, GAP_LINK_ID);
        nt_ui_rich_text_n(s_fx.ctx, "AA ", 3); /* link word 1 */
        nt_ui_rich_link(s_fx.ctx, 0U);
        nt_ui_rich_text_n(s_fx.ctx, "XX ", 3); /* non-link GAP word */
        nt_ui_rich_link(s_fx.ctx, GAP_LINK_ID);
        nt_ui_rich_text_n(s_fx.ctx, "AA", 2); /* link word 2 (same id) */
        nt_ui_rich_link(s_fx.ctx, 0U);
        nt_ui_rich_end(s_fx.ctx);
        nt_ui_rich_text(s_fx.ctx, CLAY_ID("link_gap_rt").id, NULL, &base, 400.0F, NT_RICH_ALIGN_LEFT, 0.0F, &res);
    }
    nt_ui_end(s_fx.ctx);
    nt_ui_target_t target = {.viewport = {0, 0, 800, 600}};
    nt_ui_walk(s_fx.ctx, &target);
    return res;
}

/* (13c) [A][gap][A] on one line: the repeated link id must NOT union its rect across the non-link
 * gap. Two disjoint rects exist for A, and the pointer over the GAP reports hovered/clicked == 0. */
static void test_link_no_union_across_gap(void) {
    const float hy = LINK_ROOT_Y + 8.0F;
    /* The GAP word "XX " spans x in [24, 48) from the root (8px/char): mid ~= root + 36. */
    const float gap_x = LINK_ROOT_X + 36.0F;
    /* Link word 1 "AA " mid ~= root + 12. */
    const float a1_x = LINK_ROOT_X + 12.0F;

    /* Warm-up so the prev-frame bbox the hit-test needs is populated. */
    nt_pointer_t idle = make_ptr(gap_x, hy, false, false, false);
    (void)frame_link_gap(&idle);

    /* Two disjoint rects for the same link (would be 1 if the union spanned the gap). */
    nt_ui_rich_result_t hov_gap = frame_link_gap(&idle);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(2U, nt_ui_rich_test_link_rect_count(s_fx.ctx), "[A][gap][A] -> two disjoint same-link rects (no union across the gap)");
    /* Pointer over the GAP must NOT report the link (no rect union across the non-link gap). */
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0U, hov_gap.hovered_link, "pointer over the non-link gap -> hovered == 0");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0U, hov_gap.clicked_link, "pointer over the gap -> clicked == 0");

    /* Sanity: the pointer over the first link word still hovers the link (the rects are real). */
    nt_pointer_t over_a = make_ptr(a1_x, hy, false, false, false);
    nt_ui_rich_result_t hov_a = frame_link_gap(&over_a);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(GAP_LINK_ID, hov_a.hovered_link, "pointer over link word 1 hovers the link");
}

/* Link hit-test under a block transform: the block carries a HAS_TRANSFORM translation, so it DRAWS
 * its links shifted by (DX,DY). The hit-test must map the pointer through the block's baked transform
 * (the SAME path standard widgets use) -> the pointer at the TRANSFORMED screen position resolves the
 * link, and the pointer at the old untransformed position now misses (draw == hit). */
static void test_link_hover_honors_block_transform(void) {
    nt_mem_scratch_reset();
    s_fx.ctx->pending_rich = NULL;
    s_fx.ctx->rich_session_open = false;
    nt_ui_state_clear_all(s_fx.ctx);

    const float local_hx = link_hit_x();            /* link mid-x in the block's LOCAL layout space */
    const float local_hy = LINK_ROOT_Y + 8.0F;      /* inside the single line */
    const float draw_hx = local_hx + LINK_XFORM_DX; /* where the link is DRAWN on screen */
    const float draw_hy = local_hy + LINK_XFORM_DY;

    /* Warm-up: the two-pass bbox needs the block solved once (first frame has no prev bbox). */
    nt_pointer_t over_draw = make_ptr(draw_hx, draw_hy, false, false, false);
    (void)frame_link_xform(&over_draw);

    /* Positive: pointer at the TRANSFORMED draw position resolves the link. */
    nt_ui_rich_result_t hov = frame_link_xform(&over_draw);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(LINK_ID, hov.hovered_link, "transformed block: pointer at the drawn link position hovers the link");

    /* Negative: pointer at the OLD untransformed layout position now misses (it's no longer where
     * the link is drawn). A flat-rect hit-test would wrongly still hit here. */
    nt_pointer_t over_flat = make_ptr(local_hx, local_hy, false, false, false);
    nt_ui_rich_result_t miss = frame_link_xform(&over_flat);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0U, miss.hovered_link, "transformed block: pointer at the OLD flat-layout position misses (draw != flat-rect)");

    /* Click through the transform: press+release at the drawn position -> real click. */
    nt_pointer_t press = make_ptr(draw_hx, draw_hy, true, true, false);
    (void)frame_link_xform(&press);
    nt_pointer_t release = make_ptr(draw_hx, draw_hy, false, false, true);
    nt_ui_rich_result_t clk = frame_link_xform(&release);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(LINK_ID, clk.clicked_link, "transformed block: press+release at the drawn link position -> click");
}

/* ===== Custom OBJECT ===== */

#define OBJ_W 24.0F
#define OBJ_H 18.0F

static uint32_t s_obj_measure_calls;
static uint32_t s_obj_draw_calls;
static float s_obj_draw_x, s_obj_draw_y, s_obj_draw_w, s_obj_draw_h;
static float s_obj_draw_color[4];
static float s_obj_draw_world[16];

static nt_ui_rich_object_measure_t stub_measure(void *user_data) {
    (void)user_data;
    s_obj_measure_calls++;
    return (nt_ui_rich_object_measure_t){.width = OBJ_W, .height = OBJ_H, .ascent = OBJ_H};
}

static void stub_draw(void *user_data, float x, float y, float w, float h, const float color[4], const float world_mat4[16]) {
    (void)user_data;
    TEST_ASSERT_NOT_NULL(world_mat4); /* engine always passes the frame's layout->world matrix */
    s_obj_draw_calls++;
    s_obj_draw_x = x;
    s_obj_draw_y = y;
    s_obj_draw_w = w;
    s_obj_draw_h = h;
    memcpy(s_obj_draw_color, color, sizeof s_obj_draw_color);
    memcpy(s_obj_draw_world, world_mat4, sizeof s_obj_draw_world);
}

/* Build "A [object] B" with an optional effect; walk once. Records the draw_fn call args. */
static void frame_object(uint8_t effect_id, float time) {
    nt_mem_scratch_reset();
    s_fx.ctx->pending_rich = NULL;
    s_fx.ctx->rich_session_open = false;
    s_obj_measure_calls = 0;
    s_obj_draw_calls = 0;

    nt_ui_rich_style_t base = nt_ui_rich_style_defaults();
    base.font_id[0] = s_fx.stub_font;

    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    CLAY({.id = CLAY_ID("obj_root"), .layout = {.sizing = {CLAY_SIZING_FIXED(400), CLAY_SIZING_FIXED(200)}}}) {
        nt_ui_rich_begin(s_fx.ctx, &base);
        if (effect_id != 0U) {
            nt_ui_rich_push_effect(s_fx.ctx, effect_id);
        }
        nt_ui_rich_text_n(s_fx.ctx, "A ", 2);
        nt_ui_rich_object(s_fx.ctx, stub_measure, stub_draw, NULL);
        nt_ui_rich_text_n(s_fx.ctx, " B", 2);
        if (effect_id != 0U) {
            nt_ui_rich_pop(s_fx.ctx);
        }
        nt_ui_rich_end(s_fx.ctx);
        nt_ui_rich_text(s_fx.ctx, CLAY_ID("obj_rt").id, NULL, &base, 400.0F, NT_RICH_ALIGN_LEFT, time, NULL);
    }
    nt_ui_end(s_fx.ctx);
    nt_ui_target_t target = {.viewport = {0, 0, 800, 600}};
    nt_ui_walk(s_fx.ctx, &target);
}

/* (14) an OBJECT run reserves a box via measure_fn and calls draw_fn(x,y,w,h) exactly once at
 * the solved box position; the surrounding text wraps around the reserved box. */
static void test_object_draws_at_solved_box(void) {
    frame_object(0U, 0.0F);
    TEST_ASSERT_TRUE_MESSAGE(s_obj_measure_calls >= 1U, "measure_fn called to reserve the box");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1U, s_obj_draw_calls, "draw_fn called exactly once");
    TEST_ASSERT_TRUE_MESSAGE(approx(s_obj_draw_w, OBJ_W), "draw_fn w == measured width (box reserved)");
    TEST_ASSERT_TRUE_MESSAGE(approx(s_obj_draw_h, OBJ_H), "draw_fn h == measured height");
    /* The object sits after "A " (2 chars * 8px = 16px) from the block origin; x >= that. */
    TEST_ASSERT_TRUE_MESSAGE(s_obj_draw_x >= 16.0F - 1.0F, "object x is past the leading 'A ' text");
}

/* (15) an effect on the object run shifts the draw_fn box (vs no effect); fade_in t=0 -> the
 * draw_fn is NOT called (visible=false skips the object). */
static void test_object_effect_and_skip(void) {
    frame_object(0U, 0.0F);
    const float x_noeff = s_obj_draw_x;
    const float y_noeff = s_obj_draw_y;

    frame_object(NT_UI_RICH_FX_ID_WAVE, 0.4F);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1U, s_obj_draw_calls, "effected object still draws once");
    const bool shifted = (fabsf(s_obj_draw_x - x_noeff) > 0.1F) || (fabsf(s_obj_draw_y - y_noeff) > 0.1F);
    TEST_ASSERT_TRUE_MESSAGE(shifted, "wave effect shifts the draw_fn box");

    frame_object(NT_UI_RICH_FX_ID_FADE_IN, 0.0F);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0U, s_obj_draw_calls, "fade_in t=0 (visible=false) skips the draw_fn call");
}

/* (15c) the OBJECT draw callback receives the ABSOLUTE resolved RGBA: a <color> run with a
 * parent opacity folds the <color> rgb + (alpha * opacity) into the callback color, exactly the
 * color the TEXT/IMAGE paths render with. Proves a custom object can honour opacity/<color>/fx. */
static void test_object_draw_receives_resolved_color(void) {
    nt_mem_scratch_reset();
    s_fx.ctx->pending_rich = NULL;
    s_fx.ctx->rich_session_open = false;
    s_obj_measure_calls = 0;
    s_obj_draw_calls = 0;
    memset(s_obj_draw_color, 0, sizeof s_obj_draw_color);

    nt_ui_rich_style_t base = nt_ui_rich_style_defaults();
    base.font_id[0] = s_fx.stub_font;

    /* The rich block carries opacity 0.5 -> frame->opacity 0.5 at emit. */
    const float block_opacity = 0.5F;
    nt_ui_element_data_t block_data = {.user_data = NULL, .layer = 0U, .flags = (uint8_t)NT_UI_ELEM_FLAG_HAS_OPACITY, .transform = nt_ui_transform_defaults(), .opacity = block_opacity};

    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    CLAY({.id = CLAY_ID("obj_col_root"), .layout = {.sizing = {CLAY_SIZING_FIXED(400), CLAY_SIZING_FIXED(200)}}}) {
        nt_ui_rich_begin(s_fx.ctx, &base);
        nt_ui_rich_push_color(s_fx.ctx, 0xFF0080FFU); /* 0xAABBGGRR: r=255 g=128 b=0 a=255 */
        nt_ui_rich_object(s_fx.ctx, stub_measure, stub_draw, NULL);
        nt_ui_rich_pop(s_fx.ctx);
        nt_ui_rich_end(s_fx.ctx);
        nt_ui_rich_text(s_fx.ctx, CLAY_ID("obj_col").id, &block_data, &base, 400.0F, NT_RICH_ALIGN_LEFT, 0.0F, NULL);
    }
    nt_ui_end(s_fx.ctx);
    nt_ui_target_t target = {.viewport = {0, 0, 800, 600}};
    nt_ui_walk(s_fx.ctx, &target);

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1U, s_obj_draw_calls, "object drew once");
    TEST_ASSERT_TRUE_MESSAGE(approx(s_obj_draw_color[0], 1.0F), "object color.r == 255/255 (<color> rgb)");
    TEST_ASSERT_TRUE_MESSAGE(approx(s_obj_draw_color[1], 128.0F / 255.0F), "object color.g == 128/255 (<color> rgb)");
    TEST_ASSERT_TRUE_MESSAGE(approx(s_obj_draw_color[2], 0.0F), "object color.b == 0/255 (<color> rgb)");
    /* alpha = base alpha (1.0) * parent opacity (0.5) -- the same fold the TEXT path uses. */
    TEST_ASSERT_TRUE_MESSAGE(approx(s_obj_draw_color[3], block_opacity), "object color.a == base_alpha * parent_opacity (opacity folded like TEXT)");
}

/* (15d) END-TO-END markup path: register an OBJECT tag, then drive the FULL public pipeline via
 * nt_ui_rich_text_markup with a string containing <obj=widget/>. Proves the parser dispatch reaches
 * the registered draw_fn (the same path the demo uses) -- not just a direct builder call. */
static void test_object_markup_reaches_draw_fn(void) {
    nt_mem_scratch_reset();
    s_fx.ctx->pending_rich = NULL;
    s_fx.ctx->rich_session_open = false;
    s_obj_measure_calls = 0;
    s_obj_draw_calls = 0;
    s_obj_draw_w = 0.0F;
    s_obj_draw_h = 0.0F;

    nt_ui_rich_tagset_t ts;
    nt_ui_rich_tagset_init(&ts);
    nt_ui_rich_tagset_register_object_tag(&ts, "widget", stub_measure, stub_draw, NULL);

    nt_ui_rich_style_t base = nt_ui_rich_style_defaults();
    base.font_id[0] = s_fx.stub_font;

    const char *markup = "A <obj=widget/> B";
    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    CLAY({.id = CLAY_ID("obj_markup_root"), .layout = {.sizing = {CLAY_SIZING_FIXED(400), CLAY_SIZING_FIXED(200)}}}) {
        nt_ui_rich_text_markup(s_fx.ctx, CLAY_ID("obj_markup").id, NULL, &ts, &base, markup, strlen(markup), 400.0F, NT_RICH_ALIGN_LEFT, 0.0F, NULL);
    }
    nt_ui_end(s_fx.ctx);
    nt_ui_target_t target = {.viewport = {0, 0, 800, 600}};
    nt_ui_walk(s_fx.ctx, &target);

    TEST_ASSERT_TRUE_MESSAGE(s_obj_measure_calls >= 1U, "<obj=widget/> markup reserved the box via measure_fn");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1U, s_obj_draw_calls, "<obj=widget/> markup reached draw_fn exactly once (end-to-end)");
    TEST_ASSERT_TRUE_MESSAGE(approx(s_obj_draw_w, OBJ_W), "markup-path draw_fn w == measured width");
    TEST_ASSERT_TRUE_MESSAGE(approx(s_obj_draw_h, OBJ_H), "markup-path draw_fn h == measured height");
}

/* A second object draw stub that captures ITS world_mat4 into a separate static, so a single frame
 * with two objects can compare the matrix each receives (proves every emit shares one matrix). */
static float s_obj2_draw_world[16];
static uint32_t s_obj2_draw_calls;
static void stub_draw2(void *user_data, float x, float y, float w, float h, const float color[4], const float world_mat4[16]) {
    (void)user_data;
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    (void)color;
    s_obj2_draw_calls++;
    memcpy(s_obj2_draw_world, world_mat4, sizeof s_obj2_draw_world);
}

#define OBJ_XFORM_DX 120.0F
#define OBJ_XFORM_DY 90.0F

/* (15e) world_mat4 CONTENT, not just non-NULL: under a FIXED block carrying a HAS_TRANSFORM
 * translation (mirrors test_link_hover_honors_block_transform), an OBJECT must receive the SAME baked
 * world matrix every other emit in the block uses -- Y-flip baked in (world[5] < 0) and the block's
 * (DX,DY) translation present. Two objects in one block must get BYTE-EQUAL matrices. */
static void test_object_world_mat4_matches_block_transform(void) {
    nt_mem_scratch_reset();
    s_fx.ctx->pending_rich = NULL;
    s_fx.ctx->rich_session_open = false;
    s_obj_draw_calls = 0;
    s_obj2_draw_calls = 0;
    memset(s_obj_draw_world, 0, sizeof s_obj_draw_world);
    memset(s_obj2_draw_world, 0, sizeof s_obj2_draw_world);

    nt_ui_rich_style_t base = nt_ui_rich_style_defaults();
    base.font_id[0] = s_fx.stub_font;

    nt_ui_transform_t t = nt_ui_transform_defaults();
    t.offset_x = OBJ_XFORM_DX;
    t.offset_y = OBJ_XFORM_DY;
    const nt_ui_element_data_t *xdata = NT_UI_DATA_XFORM(0U, &t, 1.0F);

    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    CLAY({.id = CLAY_ID("obj_xform_root"), .layout = {.sizing = {CLAY_SIZING_FIXED(400), CLAY_SIZING_FIXED(200)}}}) {
        nt_ui_rich_begin(s_fx.ctx, &base);
        nt_ui_rich_object(s_fx.ctx, stub_measure, stub_draw, NULL);
        nt_ui_rich_text_n(s_fx.ctx, " ", 1);
        nt_ui_rich_object(s_fx.ctx, stub_measure, stub_draw2, NULL);
        nt_ui_rich_end(s_fx.ctx);
        nt_ui_rich_text(s_fx.ctx, CLAY_ID("obj_xform_rt").id, xdata, &base, 400.0F, NT_RICH_ALIGN_LEFT, 0.0F, NULL);
    }
    nt_ui_end(s_fx.ctx);
    nt_ui_target_t target = {.viewport = {0, 0, 800, 600}};
    nt_ui_walk(s_fx.ctx, &target);

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1U, s_obj_draw_calls, "first object drew once");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1U, s_obj2_draw_calls, "second object drew once");
    /* Both objects in the SAME block get the IDENTICAL baked world matrix (not just non-NULL). */
    TEST_ASSERT_EQUAL_MEMORY_MESSAGE(s_obj_draw_world, s_obj2_draw_world, sizeof s_obj_draw_world, "both objects in one block receive the SAME world_mat4 (one shared matrix per emit)");
    /* Y-flip baked into the column-major world matrix: the +y axis row (world[5]) is negative. */
    TEST_ASSERT_TRUE_MESSAGE(s_obj_draw_world[5] < 0.0F, "world_mat4 carries the baked Y-flip (world[5] < 0)");
    /* The block's HAS_TRANSFORM translation rides the matrix: translation column (world[12],world[13])
     * reflects the (DX,DY) offset. Y-flip negates the Y translation contribution -> compare magnitudes. */
    const bool tx_present = fabsf(s_obj_draw_world[12]) >= OBJ_XFORM_DX - 1.0F;
    const bool ty_present = fabsf(s_obj_draw_world[13]) >= OBJ_XFORM_DY - 1.0F;
    TEST_ASSERT_TRUE_MESSAGE(tx_present, "world_mat4 translation carries the block's X transform offset");
    TEST_ASSERT_TRUE_MESSAGE(ty_present, "world_mat4 translation carries the block's Y transform offset");
}

/* (15f) degenerate measure_fn return: a stub returning {NaN, -5, NaN} must (a) trap the fail-early
 * assert in FULL, and (b) -- proven separately via the clamp -- keep the block size finite. This
 * death test pins the FULL assert; the OFF hard clamp (non-finite/negative -> 0) is by-construction
 * bounded (clamps width/height/ascent BEFORE rich_break_lines / the Clay FIXED block size). */
static nt_ui_rich_object_measure_t stub_measure_degenerate(void *user_data) {
    (void)user_data;
    return (nt_ui_rich_object_measure_t){.width = NAN, .height = -5.0F, .ascent = NAN};
}
static void frame_object_measure(nt_ui_rich_object_measure_fn measure_fn) {
    nt_mem_scratch_reset();
    s_fx.ctx->pending_rich = NULL;
    s_fx.ctx->rich_session_open = false;
    nt_ui_rich_style_t base = nt_ui_rich_style_defaults();
    base.font_id[0] = s_fx.stub_font;
    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    CLAY({.id = CLAY_ID("obj_deg_root"), .layout = {.sizing = {CLAY_SIZING_FIXED(400), CLAY_SIZING_FIXED(200)}}}) {
        nt_ui_rich_begin(s_fx.ctx, &base);
        nt_ui_rich_object(s_fx.ctx, measure_fn, stub_draw, NULL);
        nt_ui_rich_end(s_fx.ctx);
        nt_ui_rich_text(s_fx.ctx, CLAY_ID("obj_deg_rt").id, NULL, &base, 400.0F, NT_RICH_ALIGN_LEFT, 0.0F, NULL);
    }
    nt_ui_end(s_fx.ctx);
    nt_ui_target_t target = {.viewport = {0, 0, 800, 600}};
    nt_ui_walk(s_fx.ctx, &target);
}
static void test_object_degenerate_measure_asserts(void) { NT_TEST_EXPECT_ASSERT(frame_object_measure(stub_measure_degenerate)); }

/* An object whose measured ascent is LESS than its height -> the box must seat by ascent
 * (box bottom at baseline + (h - ascent)) and grow the line descent, not hang the whole box
 * above the baseline. */
#define OBJ_ASC_W 24.0F
#define OBJ_ASC_H 18.0F
#define OBJ_ASC_ASCENT 12.0F /* < height: 6px must fall below the baseline as descent */

static nt_ui_rich_object_measure_t stub_measure_ascent(void *user_data) {
    (void)user_data;
    s_obj_measure_calls++;
    return (nt_ui_rich_object_measure_t){.width = OBJ_ASC_W, .height = OBJ_ASC_H, .ascent = OBJ_ASC_ASCENT};
}

/* Build "A [object] B" with a measure fn whose ascent != height; solve and probe the object atom. */
static void frame_object_ascent(void) {
    nt_mem_scratch_reset();
    s_fx.ctx->pending_rich = NULL;
    s_fx.ctx->rich_session_open = false;
    s_obj_measure_calls = 0;
    s_obj_draw_calls = 0;

    nt_ui_rich_style_t base = nt_ui_rich_style_defaults();
    base.font_id[0] = s_fx.stub_font;

    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    CLAY({.id = CLAY_ID("obj_asc_root"), .layout = {.sizing = {CLAY_SIZING_FIXED(400), CLAY_SIZING_FIXED(200)}}}) {
        nt_ui_rich_begin(s_fx.ctx, &base);
        nt_ui_rich_text_n(s_fx.ctx, "A ", 2);
        nt_ui_rich_object(s_fx.ctx, stub_measure_ascent, stub_draw, NULL);
        nt_ui_rich_text_n(s_fx.ctx, " B", 2);
        nt_ui_rich_end(s_fx.ctx);
        nt_ui_rich_text(s_fx.ctx, CLAY_ID("obj_asc").id, NULL, &base, 400.0F, NT_RICH_ALIGN_LEFT, 0.0F, NULL);
    }
    nt_ui_end(s_fx.ctx);
    nt_ui_target_t target = {.viewport = {0, 0, 800, 600}};
    nt_ui_walk(s_fx.ctx, &target);
}

/* (15b) an OBJECT with ascent != height is seated by its ascent: the box BOTTOM lands at
 * baseline + (h - ascent), and the line descent grows so the next line cannot overlap. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity) -- expected-metric setup + atom-scan + per-axis asserts
static void test_object_baseline_honours_ascent(void) {
    /* Deterministic text metrics (setUp): ascent 800/1000, descent 200/1000 -> at size 16,
     * text asc = 12.8, text desc = 3.2, text line height = 16. */
    const float text_asc = 800.0F / 1000.0F * FONT_SIZE_DEFAULT;  /* 12.8 */
    const float text_desc = 200.0F / 1000.0F * FONT_SIZE_DEFAULT; /* 3.2 */
    /* Expected line metrics: line asc = max(text_asc, obj_ascent); line desc = max(text_desc, obj_h - obj_ascent). */
    const float exp_asc = (text_asc > OBJ_ASC_ASCENT) ? text_asc : OBJ_ASC_ASCENT; /* 12.8 */
    const float exp_obj_desc = OBJ_ASC_H - OBJ_ASC_ASCENT;                         /* 6.0 */
    const float exp_desc = (text_desc > exp_obj_desc) ? text_desc : exp_obj_desc;  /* 6.0 */
    const float exp_line_h = exp_asc + exp_desc;
    const float exp_baseline = exp_asc; /* line 0: pen_y == 0 */

    frame_object_ascent();

    /* The line grew past the bare text line height (descent grew by the object's below-baseline part). */
    TEST_ASSERT_TRUE_MESSAGE(nt_ui_rich_test_total_h(s_fx.ctx) > (text_asc + text_desc) + 1e-3F, "object below-baseline part grows the line descent");
    TEST_ASSERT_TRUE_MESSAGE(approx(nt_ui_rich_test_total_h(s_fx.ctx), exp_line_h), "one-line height == asc + desc (asc by ascent, desc by h-ascent)");

    /* Find the OBJECT solved atom and assert its box bottom == baseline + (h - ascent). */
    bool found = false;
    const uint32_t n = nt_ui_rich_test_atom_count(s_fx.ctx);
    for (uint32_t i = 0; i < n; i++) {
        const nt_ui_rich_test_atom_t a = nt_ui_rich_test_atom(s_fx.ctx, i);
        if (a.kind == NT_RICH_ATOM_OBJECT) {
            found = true;
            TEST_ASSERT_TRUE_MESSAGE(approx(a.y, exp_baseline - OBJ_ASC_ASCENT), "object box top == baseline - ascent");
            TEST_ASSERT_TRUE_MESSAGE(approx(a.y + a.h, exp_baseline + exp_obj_desc), "object box bottom == baseline + (h - ascent)");
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(found, "solver placed the OBJECT atom");
}

/* Two nt_ui_rich_text widgets in ONE frame must not trip the "rich-text calls do not nest" guard:
 * the terminal call releases pending_rich, so the second begin starts clean. No manual pending_rich nulling. */
static void test_two_rich_text_blocks_one_frame_no_trap(void) {
    nt_ui_rich_style_t base = nt_ui_rich_style_defaults();
    base.font_id[0] = s_fx.stub_font;

    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    CLAY({.id = CLAY_ID("rich_multi_root"), .layout = {.sizing = {CLAY_SIZING_FIXED(400), CLAY_SIZING_FIXED(200)}}}) {
        nt_ui_rich_begin(s_fx.ctx, &base);
        nt_ui_rich_text_n(s_fx.ctx, "first block", 11);
        nt_ui_rich_text(s_fx.ctx, CLAY_ID("rich_a").id, NULL, &base, 400.0F, NT_RICH_ALIGN_LEFT, 0.0F, NULL);

        /* second rich-text widget, SAME frame */
        nt_ui_rich_begin(s_fx.ctx, &base);
        nt_ui_rich_text_n(s_fx.ctx, "second block", 12);
        nt_ui_rich_text(s_fx.ctx, CLAY_ID("rich_b").id, NULL, &base, 400.0F, NT_RICH_ALIGN_LEFT, 0.0F, NULL);
    }
    nt_ui_end(s_fx.ctx);

    nt_ui_target_t target = {.viewport = {0, 0, 800, 600}};
    nt_text_renderer_test_reset_call_counters();
    nt_ui_walk(s_fx.ctx, &target);
    TEST_ASSERT_TRUE_MESSAGE(nt_text_renderer_test_draw_n_calls() > 0U, "both rich-text blocks emit (no nest-guard trap)");
}

/* ===== Public markup entry e2e (nt_ui_rich_text_markup) ===== */

#define MK_ROOT_X 60.0F
#define MK_ROOT_Y 30.0F
#define MK_CONTAINER_W 400.0F

/* Drive the PUBLIC markup entry through parse -> solve -> emit -> link in one call, inside a CLAY
 * block positioned at a known offset, and walk once. Markup: "go <link=here>HERE</link> now". */
static nt_ui_rich_result_t frame_markup(const nt_pointer_t *p) {
    nt_mem_scratch_reset();
    s_fx.ctx->pending_rich = NULL;
    s_fx.ctx->rich_session_open = false;

    nt_ui_rich_style_t base = nt_ui_rich_style_defaults();
    base.font_id[0] = s_fx.stub_font;

    static const char *const markup = "go <link=here>HERE</link> now";
    nt_ui_rich_result_t res = {0};
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, p, 1);
    CLAY({.id = CLAY_ID("mk_root"),
          .layout = {.sizing = {CLAY_SIZING_FIXED(MK_CONTAINER_W), CLAY_SIZING_FIXED(200)}},
          .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .offset = {MK_ROOT_X, MK_ROOT_Y}}}) {
        nt_ui_rich_text_markup(s_fx.ctx, CLAY_ID("mk_rt").id, NULL, NULL, &base, markup, strlen(markup), MK_CONTAINER_W, NT_RICH_ALIGN_LEFT, 0.0F, &res);
    }
    nt_ui_end(s_fx.ctx);
    nt_ui_target_t target = {.viewport = {0, 0, 800, 600}};
    nt_ui_walk(s_fx.ctx, &target);
    return res;
}

/* (16) the public markup entry emits text spans, sizes the FIXED block to container_w, and resolves
 * a link click via a warm prev-frame bbox. */
static void test_markup_e2e_emit_and_link(void) {
    const uint32_t link_id = nt_hash32("here", 4).value;

    /* Warm-up frame so the prev-frame bbox the link hit-test needs is populated. */
    nt_text_renderer_test_reset_call_counters();
    nt_pointer_t idle = make_ptr(0.0F, 0.0F, false, false, false);
    (void)frame_markup(&idle);

    TEST_ASSERT_TRUE_MESSAGE(nt_text_renderer_test_draw_n_calls() > 0U, "markup entry emits draw_n spans");
    TEST_ASSERT_TRUE_MESSAGE(approx(nt_ui_rich_test_total_w(s_fx.ctx), MK_CONTAINER_W), "markup FIXED width == container_w");
    TEST_ASSERT_TRUE_MESSAGE(nt_ui_rich_test_link_rect_count(s_fx.ctx) >= 1U, "markup <link> produced a link rect");

    /* Press then release over the link rect: "go " = 3 chars * 8px = 24px; link "HERE" mid at +16px.
     * Click requires press+release on the SAME link, so press inside first, then release inside. */
    const float hx = MK_ROOT_X + (3.0F * 8.0F) + (2.0F * 8.0F);
    const float hy = MK_ROOT_Y + 8.0F;
    nt_pointer_t press = make_ptr(hx, hy, true, true, false);
    (void)frame_markup(&press);
    nt_pointer_t rel = make_ptr(hx, hy, false, false, true);
    nt_ui_rich_result_t clk = frame_markup(&rel);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(link_id, clk.hovered_link, "markup link hovered (hash of \"here\")");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(link_id, clk.clicked_link, "markup link clicked on press+release over its rect");
}

/* ===== Custom (game-supplied) effects ===== */

/* A DISTINCTIVE user_data-DRIVEN custom effect: the offset is read from the game-registered
 * user_data (NOT a constant), so the test asserts the emitted box reflects that EXACT pointer's
 * fields -- proving user_data is delivered to the callback at emit, not merely stored. A NULL
 * user_data (e.g. a stock-style registration) falls back to a sentinel offset distinct from any
 * param so the two paths are distinguishable. */
typedef struct {
    float off_x;
    float off_y;
} fx_param_t;
#define FX_CUSTOM_NULL_OFF_X 5.0F /* sentinel when user_data == NULL */
#define FX_CUSTOM_NULL_OFF_Y 7.0F
static uint32_t s_custom_fx_calls;
static void *s_custom_fx_seen_user; /* the user_data the fn actually received at emit */

static nt_ui_rich_fx_result_t custom_fx_param(uint32_t atom_idx, nt_rich_atom_kind_t kind, const float base_xy[2], const float base_wh[2], const float base_color[4], float time, bool hovered,
                                              void *user_data) {
    (void)atom_idx;
    (void)kind;
    (void)base_xy;
    (void)base_wh;
    (void)time;
    (void)hovered;
    s_custom_fx_calls++;
    s_custom_fx_seen_user = user_data;
    nt_ui_rich_fx_result_t r = nt_ui_rich_fx_identity(base_color);
    if (user_data != NULL) {
        const fx_param_t *p = (const fx_param_t *)user_data;
        r.offset_x = p->off_x; /* offset DERIVED from the registered user_data */
        r.offset_y = p->off_y;
    } else {
        r.offset_x = FX_CUSTOM_NULL_OFF_X;
        r.offset_y = FX_CUSTOM_NULL_OFF_Y;
    }
    r.color[0] = 1.0F; /* distinctive magenta tint */
    r.color[1] = 0.0F;
    r.color[2] = 1.0F;
    return r;
}

/* Build "A [object] B" pushing a CUSTOM effect fn via the builder; record the object draw box. */
static void frame_object_custom_fn(nt_ui_rich_fx_fn fn, void *user) {
    nt_mem_scratch_reset();
    s_fx.ctx->pending_rich = NULL;
    s_fx.ctx->rich_session_open = false;
    s_obj_measure_calls = 0;
    s_obj_draw_calls = 0;

    nt_ui_rich_style_t base = nt_ui_rich_style_defaults();
    base.font_id[0] = s_fx.stub_font;

    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    CLAY({.id = CLAY_ID("obj_cfx_root"), .layout = {.sizing = {CLAY_SIZING_FIXED(400), CLAY_SIZING_FIXED(200)}}}) {
        nt_ui_rich_begin(s_fx.ctx, &base);
        nt_ui_rich_push_effect_fn(s_fx.ctx, fn, user);
        nt_ui_rich_text_n(s_fx.ctx, "A ", 2);
        nt_ui_rich_object(s_fx.ctx, stub_measure, stub_draw, NULL);
        nt_ui_rich_text_n(s_fx.ctx, " B", 2);
        nt_ui_rich_pop(s_fx.ctx);
        nt_ui_rich_end(s_fx.ctx);
        nt_ui_rich_text(s_fx.ctx, CLAY_ID("obj_cfx").id, NULL, &base, 400.0F, NT_RICH_ALIGN_LEFT, 0.5F, NULL);
    }
    nt_ui_end(s_fx.ctx);
    nt_ui_target_t target = {.viewport = {0, 0, 800, 600}};
    nt_ui_walk(s_fx.ctx, &target);
}

/* (17) BUILDER path: a custom fn pushed via nt_ui_rich_push_effect_fn actually RUNS at emit AND
 * receives its registered user_data -- the object draw box shifts by EXACTLY the offset carried in
 * the user_data struct (vs the no-effect baseline), proving the registered pointer reaches the
 * callback at emit time, not just that some custom fn ran. */
static void test_custom_fx_runs_via_builder(void) {
    /* No-effect baseline box position. */
    frame_object(0U, 0.5F);
    const float x_base = s_obj_draw_x;
    const float y_base = s_obj_draw_y;

    /* Custom fn: the box shifts by EXACTLY the user_data-carried offset (about-center scale==1). */
    s_custom_fx_calls = 0;
    s_custom_fx_seen_user = NULL;
    fx_param_t param = {.off_x = 37.0F, .off_y = -19.0F};
    frame_object_custom_fn(custom_fx_param, &param);
    TEST_ASSERT_TRUE_MESSAGE(s_custom_fx_calls > 0U, "custom effect fn actually ran at emit (builder path)");
    TEST_ASSERT_EQUAL_PTR_MESSAGE(&param, s_custom_fx_seen_user, "custom fn received the EXACT registered user_data pointer at emit");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1U, s_obj_draw_calls, "object still drawn once under a custom effect");
    TEST_ASSERT_TRUE_MESSAGE(approx(s_obj_draw_x - x_base, param.off_x), "custom fn's user_data offset.x folds into the object draw box");
    TEST_ASSERT_TRUE_MESSAGE(approx(s_obj_draw_y - y_base, param.off_y), "custom fn's user_data offset.y folds into the object draw box");
}

/* (18) MARKUP path: <fx=myfx> resolves to a tagset-registered custom fn (custom resolves BEFORE
 * stock) and that fn ACTUALLY RUNS at emit (call counter ticks during the walk) AND receives the
 * user_data registered with it -- proving the markup/tagset front carries fn+user_data through
 * parse -> per-block table -> emit, even though the tagset is not consulted at emit. A stock
 * <fx=wavename> shares the tagset to prove the two coexist. */
static fx_param_t s_markup_param = {.off_x = 11.0F, .off_y = -3.0F};

static void test_custom_fx_runs_via_markup(void) {
    nt_ui_rich_tagset_t ts;
    nt_ui_rich_tagset_init(&ts);
    nt_ui_rich_tagset_register_effect(&ts, "wavename", NT_UI_RICH_FX_ID_WAVE);           /* stock entry coexists */
    nt_ui_rich_tagset_register_effect_fn(&ts, "myfx", custom_fx_param, &s_markup_param); /* custom + user_data */

    nt_ui_rich_style_t base = nt_ui_rich_style_defaults();
    base.font_id[0] = s_fx.stub_font;

    s_custom_fx_calls = 0;
    s_custom_fx_seen_user = NULL;
    nt_mem_scratch_reset();
    s_fx.ctx->pending_rich = NULL;
    s_fx.ctx->rich_session_open = false;
    /* "A " has no effect; "<fx=myfx>BB</fx>" runs the custom fn per glyph; the stock wave too. */
    static const char *const markup_fx = "A <fx=myfx>BB</fx> <fx=wavename>CC</fx>";
    nt_pointer_t mouse = {0};
    nt_text_renderer_test_reset_call_counters();
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    CLAY({.id = CLAY_ID("cfx_mk_root"), .layout = {.sizing = {CLAY_SIZING_FIXED(400), CLAY_SIZING_FIXED(200)}}}) {
        nt_ui_rich_text_markup(s_fx.ctx, CLAY_ID("cfx_mk").id, NULL, &ts, &base, markup_fx, strlen(markup_fx), 400.0F, NT_RICH_ALIGN_LEFT, 0.5F, NULL);
    }
    nt_ui_end(s_fx.ctx);
    nt_ui_target_t target = {.viewport = {0, 0, 800, 600}};
    nt_ui_walk(s_fx.ctx, &target);

    /* The custom fn ran (once per glyph of "BB" = 2). The stock wave ran too but does NOT tick the
     * custom counter -- proving custom resolved to the GAME fn, not the stock id (custom != stock). */
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(2U, s_custom_fx_calls, "markup <fx=myfx> ran the custom fn once per glyph (custom before stock)");
    TEST_ASSERT_EQUAL_PTR_MESSAGE(&s_markup_param, s_custom_fx_seen_user, "markup-registered user_data reaches the custom fn at emit");
    TEST_ASSERT_TRUE_MESSAGE(nt_text_renderer_test_draw_n_calls() > 0U, "markup with custom + stock effects still emits text spans");
}

/* ===== Z-order layers ===== */

/* Build text + inline image + object with NO <layer> -> the solver must assign the per-kind defaults
 * TEXT=0, IMAGE=1, OBJECT=2 (ascending = further back -> further front). Walk so the atoms get solved. */
static void frame_text_image_object(void) {
    nt_mem_scratch_reset();
    s_fx.ctx->pending_rich = NULL;
    s_fx.ctx->rich_session_open = false;

    nt_ui_rich_style_t base = nt_ui_rich_style_defaults();
    base.font_id[0] = s_fx.stub_font;
    base.image_material = make_rich_image_material();
    const nt_atlas_region_ref_t ref = nt_atlas_ref(s_fx.atlas.handle, FX_WHITE_NAME_HASH);

    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    CLAY({.id = CLAY_ID("rich_tio_root"), .layout = {.sizing = {CLAY_SIZING_FIXED(400), CLAY_SIZING_FIXED(200)}}}) {
        nt_ui_rich_begin(s_fx.ctx, &base);
        nt_ui_rich_text_n(s_fx.ctx, "T ", 2);
        nt_ui_rich_image(s_fx.ctx, ref, NT_RICH_VALIGN_MIDDLE, 0.0F, 1.0F);
        nt_ui_rich_text_n(s_fx.ctx, " ", 1);
        nt_ui_rich_object(s_fx.ctx, stub_measure, stub_draw, NULL);
        nt_ui_rich_end(s_fx.ctx);
        nt_ui_rich_text(s_fx.ctx, CLAY_ID("rich_tio").id, NULL, &base, 800.0F, NT_RICH_ALIGN_LEFT, 0.0F, NULL);
    }
    nt_ui_end(s_fx.ctx);
    nt_ui_target_t target = {.viewport = {0, 0, 800, 600}};
    nt_ui_walk(s_fx.ctx, &target);
}

/* (L1) default layers by kind: with no <layer>, every TEXT atom reports layer 0, the IMAGE atom layer 1,
 * the OBJECT atom layer 2 -- proving the AUTO sentinel resolves to the per-kind default at atom build. */
static void test_default_layers_by_kind(void) {
    frame_text_image_object();
    const uint32_t n = nt_ui_rich_test_atom_count(s_fx.ctx);
    TEST_ASSERT_TRUE_MESSAGE(n >= 3U, "block placed text + image + object atoms");
    bool saw_text = false;
    bool saw_image = false;
    bool saw_object = false;
    for (uint32_t i = 0; i < n; i++) {
        const nt_ui_rich_test_atom_t a = nt_ui_rich_test_atom(s_fx.ctx, i);
        const uint8_t layer = nt_ui_rich_test_atom_layer(s_fx.ctx, i);
        if (a.kind == NT_RICH_ATOM_TEXT) {
            saw_text = true;
            TEST_ASSERT_EQUAL_UINT8_MESSAGE(0U, layer, "TEXT default layer == 0");
        } else if (a.kind == NT_RICH_ATOM_IMAGE) {
            saw_image = true;
            TEST_ASSERT_EQUAL_UINT8_MESSAGE(1U, layer, "IMAGE default layer == 1");
        } else {
            saw_object = true;
            TEST_ASSERT_EQUAL_UINT8_MESSAGE(2U, layer, "OBJECT default layer == 2");
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(saw_text && saw_image && saw_object, "all three kinds present");
}

/* Within-band emit-order recorder: at object draw_fn time, capture how many inline images have already
 * emitted (image_emit_count). With the within-band order text->image->object, the object's draw_fn runs
 * AFTER the band's image emitted, so it observes image_emit_count == 1. */
static uint32_t s_order_img_at_object_draw;
static void order_recording_draw(void *user_data, float x, float y, float w, float h, const float color[4], const float world_mat4[16]) {
    (void)user_data;
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    (void)color;
    (void)world_mat4;
    s_order_img_at_object_draw = nt_ui_rich_test_image_emit_count(s_fx.ctx);
}

/* (L2) <layer=5> override: a push_layer(5) around mixed text + image -> EVERY enclosed atom (any kind)
 * reports layer 5, not the per-kind default. Proves an explicit layer overrides the AUTO-by-kind rule. */
static void test_layer_override(void) {
    nt_mem_scratch_reset();
    s_fx.ctx->pending_rich = NULL;
    s_fx.ctx->rich_session_open = false;

    nt_ui_rich_style_t base = nt_ui_rich_style_defaults();
    base.font_id[0] = s_fx.stub_font;
    base.image_material = make_rich_image_material();
    const nt_atlas_region_ref_t ref = nt_atlas_ref(s_fx.atlas.handle, FX_WHITE_NAME_HASH);

    s_order_img_at_object_draw = 0xFFFFFFFFU; /* sentinel: stays unset if the object never draws */
    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    CLAY({.id = CLAY_ID("rich_lo_root"), .layout = {.sizing = {CLAY_SIZING_FIXED(400), CLAY_SIZING_FIXED(200)}}}) {
        nt_ui_rich_begin(s_fx.ctx, &base);
        nt_ui_rich_push_layer(s_fx.ctx, 5U);
        nt_ui_rich_text_n(s_fx.ctx, "X ", 2);
        nt_ui_rich_image(s_fx.ctx, ref, NT_RICH_VALIGN_MIDDLE, 0.0F, 1.0F);
        nt_ui_rich_text_n(s_fx.ctx, " Y", 2);
        nt_ui_rich_object(s_fx.ctx, stub_measure, order_recording_draw, NULL); /* same <layer=5> band as text + image */
        nt_ui_rich_pop(s_fx.ctx);
        nt_ui_rich_end(s_fx.ctx);
        nt_ui_rich_text(s_fx.ctx, CLAY_ID("rich_lo").id, NULL, &base, 800.0F, NT_RICH_ALIGN_LEFT, 0.0F, NULL);
    }
    nt_ui_end(s_fx.ctx);
    nt_ui_target_t target = {.viewport = {0, 0, 800, 600}};
    nt_ui_walk(s_fx.ctx, &target);

    const uint32_t n = nt_ui_rich_test_atom_count(s_fx.ctx);
    TEST_ASSERT_TRUE_MESSAGE(n >= 4U, "override block placed text + image + object atoms");
    for (uint32_t i = 0; i < n; i++) {
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(5U, nt_ui_rich_test_atom_layer(s_fx.ctx, i), "every enclosed atom (any kind) reports layer 5");
    }
    /* WITHIN-BAND ORDER: in the single <layer=5> band the emit order is text -> image -> object, so the
     * object's draw_fn ran AFTER the band's image emitted (image-before-object). image_emit_count is 1 at
     * draw time (1 = the band's lone image already emitted; not the sentinel = the object did draw). */
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1U, s_order_img_at_object_draw, "within band: image emits BEFORE object (object draw_fn sees image_emit_count == 1)");
}

/* Build a multi-face block split across TWO explicit layers: faces R,B on layer 0 and faces I,BI on
 * layer 1. The font-group gather is PER-LAYER, so set_font is called (distinct fonts in layer 0 = 2) +
 * (distinct fonts in layer 1 = 2) = 4 -- proving the gather scopes to the band, not the whole block. */
static void frame_multi_face_two_layers(const nt_font_t fam[4]) {
    nt_mem_scratch_reset();
    s_fx.ctx->pending_rich = NULL;
    s_fx.ctx->rich_session_open = false;

    nt_ui_rich_style_t base = nt_ui_rich_style_defaults();
    for (int i = 0; i < 4; i++) {
        base.font_id[i] = fam[i];
        nt_font_test_set_metrics(fam[i], 1000, 800, -200, 1000);
    }

    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    CLAY({.id = CLAY_ID("rich_ml_root"), .layout = {.sizing = {CLAY_SIZING_FIXED(400), CLAY_SIZING_FIXED(200)}}}) {
        nt_ui_rich_begin(s_fx.ctx, &base);
        /* Layer 0: regular + bold faces. */
        nt_ui_rich_push_layer(s_fx.ctx, 0U);
        nt_ui_rich_text_n(s_fx.ctx, "r ", 2);
        nt_ui_rich_push_bold(s_fx.ctx);
        nt_ui_rich_text_n(s_fx.ctx, "b ", 2);
        nt_ui_rich_pop(s_fx.ctx); /* bold */
        nt_ui_rich_pop(s_fx.ctx); /* layer 0 */
        /* Layer 1: italic + bold-italic faces. */
        nt_ui_rich_push_layer(s_fx.ctx, 1U);
        nt_ui_rich_push_italic(s_fx.ctx);
        nt_ui_rich_text_n(s_fx.ctx, "i ", 2);
        nt_ui_rich_push_bold(s_fx.ctx);
        nt_ui_rich_text_n(s_fx.ctx, "bi", 2);
        nt_ui_rich_pop(s_fx.ctx); /* bold */
        nt_ui_rich_pop(s_fx.ctx); /* italic */
        nt_ui_rich_pop(s_fx.ctx); /* layer 1 */
        nt_ui_rich_end(s_fx.ctx);
        nt_ui_rich_text(s_fx.ctx, CLAY_ID("rich_ml").id, NULL, &base, 800.0F, NT_RICH_ALIGN_LEFT, 0.0F, NULL);
    }
    nt_ui_end(s_fx.ctx);
    nt_ui_target_t target = {.viewport = {0, 0, 800, 600}};
    nt_ui_walk(s_fx.ctx, &target);
}

/* (L3) font-group gather is per-band, not per-block: {R,B} on layer 0 + {I,BI} on layer 1
 * still costs 4 set_font calls (the layer split does not collapse the per-band grouping). */
static void test_font_group_per_layer(void) {
    nt_font_t fam[4];
    fam[0] = s_fx.stub_font;   /* R */
    fam[1] = make_stub_font(); /* B */
    fam[2] = make_stub_font(); /* I */
    fam[3] = make_stub_font(); /* BI */

    nt_text_renderer_test_reset_call_counters();
    frame_multi_face_two_layers(fam);

    /* Per-band gather: layer 0 = {R,B} (2) + layer 1 = {I,BI} (2) = 4 set_font calls. */
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(4U, nt_text_renderer_test_set_font_calls(), "font gather is per-layer: 2 fonts in band 0 + 2 in band 1 = 4 set_font calls");
    TEST_ASSERT_TRUE_MESSAGE(nt_text_renderer_test_draw_n_calls() > 0U, "layered multi-face block still emits draw_n spans");

    for (int i = 1; i < 4; i++) {
        nt_font_destroy(fam[i]);
    }
}

/* (L4) a SINGLE face reused across TWO layers calls set_font ONCE PER BAND (2 total), not once for the
 * whole block (1) -- the clean proof that the gather re-scopes per layer (the same font.id rebinds in
 * the second band because the first band drained between them). */
static void test_font_rebinds_per_layer_for_shared_face(void) {
    nt_mem_scratch_reset();
    s_fx.ctx->pending_rich = NULL;
    s_fx.ctx->rich_session_open = false;

    nt_ui_rich_style_t base = nt_ui_rich_style_defaults();
    base.font_id[0] = s_fx.stub_font;
    nt_font_test_set_metrics(s_fx.stub_font, 1000, 800, -200, 1000);

    nt_text_renderer_test_reset_call_counters();
    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    CLAY({.id = CLAY_ID("rich_sf_root"), .layout = {.sizing = {CLAY_SIZING_FIXED(400), CLAY_SIZING_FIXED(200)}}}) {
        nt_ui_rich_begin(s_fx.ctx, &base);
        nt_ui_rich_push_layer(s_fx.ctx, 0U);
        nt_ui_rich_text_n(s_fx.ctx, "a ", 2);
        nt_ui_rich_pop(s_fx.ctx);
        nt_ui_rich_push_layer(s_fx.ctx, 1U);
        nt_ui_rich_text_n(s_fx.ctx, "b", 1);
        nt_ui_rich_pop(s_fx.ctx);
        nt_ui_rich_end(s_fx.ctx);
        nt_ui_rich_text(s_fx.ctx, CLAY_ID("rich_sf").id, NULL, &base, 800.0F, NT_RICH_ALIGN_LEFT, 0.0F, NULL);
    }
    nt_ui_end(s_fx.ctx);
    nt_ui_target_t target = {.viewport = {0, 0, 800, 600}};
    nt_ui_walk(s_fx.ctx, &target);

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(2U, nt_text_renderer_test_set_font_calls(), "one face across two layers -> set_font once per band (2), not once for the whole block");
}

/* Build two inline images on DISTINCT explicit layers: a red image on layer 0 (lower band) and a green
 * image on layer 1 (higher band). Two populated sprite bands -> two per-band sprite drains; ascending emit
 * means the layer-1 (green) image is the LAST one emitted. */
static void frame_two_layer_images(nt_material_t img_mat) {
    nt_mem_scratch_reset();
    s_fx.ctx->pending_rich = NULL;
    s_fx.ctx->rich_session_open = false;

    nt_ui_rich_style_t base = nt_ui_rich_style_defaults();
    base.font_id[0] = s_fx.stub_font;
    base.image_material = img_mat;
    const nt_atlas_region_ref_t ref = nt_atlas_ref(s_fx.atlas.handle, FX_WHITE_NAME_HASH);

    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    CLAY({.id = CLAY_ID("rich_2li_root"), .layout = {.sizing = {CLAY_SIZING_FIXED(400), CLAY_SIZING_FIXED(200)}}}) {
        nt_ui_rich_begin(s_fx.ctx, &base);
        nt_ui_rich_push_layer(s_fx.ctx, 0U);
        nt_ui_rich_push_color(s_fx.ctx, 0xFF0000FFU); /* red (0xAABBGGRR): r=255 g=0 b=0 a=255 */
        nt_ui_rich_image(s_fx.ctx, ref, NT_RICH_VALIGN_MIDDLE, 0.0F, 1.0F);
        nt_ui_rich_pop(s_fx.ctx);
        nt_ui_rich_pop(s_fx.ctx);
        nt_ui_rich_push_layer(s_fx.ctx, 1U);
        nt_ui_rich_push_color(s_fx.ctx, 0xFF00FF00U); /* green: r=0 g=255 b=0 a=255 */
        nt_ui_rich_image(s_fx.ctx, ref, NT_RICH_VALIGN_MIDDLE, 0.0F, 1.0F);
        nt_ui_rich_pop(s_fx.ctx);
        nt_ui_rich_pop(s_fx.ctx);
        nt_ui_rich_end(s_fx.ctx);
        nt_ui_rich_text(s_fx.ctx, CLAY_ID("rich_2li").id, NULL, &base, 800.0F, NT_RICH_ALIGN_LEFT, 0.0F, NULL);
    }
    nt_ui_end(s_fx.ctx);
    nt_ui_target_t target = {.viewport = {0, 0, 800, 600}};
    nt_sprite_renderer_test_reset_nonempty_flush_calls();
    nt_ui_walk(s_fx.ctx, &target);
}

/* (L5) per-band drain + ascending z (otherwise visual-only): a layer-0 red + layer-1 green image drain as
 * two non-empty sprite flushes (not one coalesced batch), and ascending band order makes layer-1 green emit LAST. */
static void test_layer_drain_orders_ascending(void) {
    const nt_material_t mat = make_rich_image_material();
    frame_two_layer_images(mat);

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(2U, nt_ui_rich_test_image_emit_count(s_fx.ctx), "two images on two layers both emit");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(2U, nt_sprite_renderer_test_nonempty_flush_calls(), "two populated bands -> one non-empty sprite drain per band (2)");
    /* Ascending: the HIGHER band (layer 1, green) emits LAST, so the last-emit probe holds green. */
    uint8_t col[4] = {0};
    nt_sprite_renderer_test_last_emit_color(0U, col); /* 0xAABBGGRR -> r,g,b,a */
    TEST_ASSERT_TRUE_MESSAGE(col[0] == 0U && col[1] == 255U && col[2] == 0U, "higher band (layer 1, green) emits LAST -> ascending band order");
}

/* (L6) default mixed block -> per-kind bands; only the IMAGE band carries sprites (text is stub-font no-op,
 * object self-draws), so exactly ONE non-empty sprite drain occurs. */
static void test_default_mixed_block_band_flush_count(void) {
    nt_sprite_renderer_test_reset_nonempty_flush_calls();
    frame_text_image_object(); /* one IMAGE atom (band 1); text band 0 + object band 2 emit no sprites */

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1U, nt_ui_rich_test_image_emit_count(s_fx.ctx), "one inline image in the mixed block");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1U, nt_sprite_renderer_test_nonempty_flush_calls(), "only the IMAGE band drains sprite content -> exactly one non-empty sprite flush");
}

/* (L7) AUTO + explicit layers MIXED in one block: a <layer=5> text run, then plain (AUTO) text + AUTO
 * image -> rich_effective_layer is per-ATOM. The wrapped run reports layer 5; the trailing AUTO text falls
 * to the per-kind default 0; the AUTO image to 1. Pins that AUTO resolves per atom, not block-wide. */
static void test_mixed_auto_and_explicit_layers(void) {
    nt_mem_scratch_reset();
    s_fx.ctx->pending_rich = NULL;
    s_fx.ctx->rich_session_open = false;

    nt_ui_rich_style_t base = nt_ui_rich_style_defaults();
    base.font_id[0] = s_fx.stub_font;
    base.image_material = make_rich_image_material();
    const nt_atlas_region_ref_t ref = nt_atlas_ref(s_fx.atlas.handle, FX_WHITE_NAME_HASH);

    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    CLAY({.id = CLAY_ID("rich_mix_root"), .layout = {.sizing = {CLAY_SIZING_FIXED(400), CLAY_SIZING_FIXED(200)}}}) {
        nt_ui_rich_begin(s_fx.ctx, &base);
        nt_ui_rich_push_layer(s_fx.ctx, 5U);
        nt_ui_rich_text_n(s_fx.ctx, "W ", 2); /* explicit layer 5 */
        nt_ui_rich_pop(s_fx.ctx);
        nt_ui_rich_text_n(s_fx.ctx, "auto ", 5);                            /* AUTO text -> 0 */
        nt_ui_rich_image(s_fx.ctx, ref, NT_RICH_VALIGN_MIDDLE, 0.0F, 1.0F); /* AUTO image -> 1 */
        nt_ui_rich_end(s_fx.ctx);
        nt_ui_rich_text(s_fx.ctx, CLAY_ID("rich_mix").id, NULL, &base, 800.0F, NT_RICH_ALIGN_LEFT, 0.0F, NULL);
    }
    nt_ui_end(s_fx.ctx);
    nt_ui_target_t target = {.viewport = {0, 0, 800, 600}};
    nt_ui_walk(s_fx.ctx, &target);

    const uint32_t n = nt_ui_rich_test_atom_count(s_fx.ctx);
    bool saw_explicit = false;
    bool saw_auto_text = false;
    bool saw_auto_image = false;
    for (uint32_t i = 0; i < n; i++) {
        const nt_ui_rich_test_atom_t a = nt_ui_rich_test_atom(s_fx.ctx, i);
        const uint8_t layer = nt_ui_rich_test_atom_layer(s_fx.ctx, i);
        if (a.kind == NT_RICH_ATOM_IMAGE) {
            saw_auto_image = true;
            TEST_ASSERT_EQUAL_UINT8_MESSAGE(1U, layer, "AUTO image atom resolves to per-kind default 1");
        } else if (layer == 5U) {
            saw_explicit = true; /* the <layer=5>-wrapped text run */
        } else {
            saw_auto_text = true;
            TEST_ASSERT_EQUAL_UINT8_MESSAGE(0U, layer, "trailing AUTO text atom resolves to per-kind default 0");
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(saw_explicit && saw_auto_text && saw_auto_image, "block carried explicit-5 text + AUTO text + AUTO image");
}

/* (L8) CAP-16 HARD GUARD: a block with >16 DISTINCT push_layer values must not crash and must surface
 * exactly NT_UI_RICH_MAX_LAYERS distinct layers (the OFF-mode over-cap drop path). Run under the assert
 * trap so the DEBUG over-cap assert is caught, proving the hard skip survives. */
static void frame_over_cap_layers(void) {
    nt_mem_scratch_reset();
    s_fx.ctx->pending_rich = NULL;
    s_fx.ctx->rich_session_open = false;

    nt_ui_rich_style_t base = nt_ui_rich_style_defaults();
    base.font_id[0] = s_fx.stub_font;

    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    CLAY({.id = CLAY_ID("rich_cap_root"), .layout = {.sizing = {CLAY_SIZING_FIXED(400), CLAY_SIZING_FIXED(200)}}}) {
        nt_ui_rich_begin(s_fx.ctx, &base);
        for (uint8_t L = 0; L < 20U; L++) { /* 20 distinct layers > NT_UI_RICH_MAX_LAYERS (16) */
            nt_ui_rich_push_layer(s_fx.ctx, L);
            nt_ui_rich_text_n(s_fx.ctx, "z ", 2);
            nt_ui_rich_pop(s_fx.ctx);
        }
        nt_ui_rich_end(s_fx.ctx);
        nt_ui_rich_text(s_fx.ctx, CLAY_ID("rich_cap").id, NULL, &base, 800.0F, NT_RICH_ALIGN_LEFT, 0.0F, NULL);
    }
    nt_ui_end(s_fx.ctx);
    nt_ui_target_t target = {.viewport = {0, 0, 800, 600}};
    nt_ui_walk(s_fx.ctx, &target);
}

static void test_over_cap_layers_hard_guard(void) {
    /* Over-cap distinct-band assert fires in rich_gather_layers (trap catches it); the hard `count >= cap`
     * skip also runs in assert-OFF builds, so a >16-distinct-layer block never writes past out[NT_UI_RICH_MAX_LAYERS]. */
    NT_TEST_EXPECT_ASSERT(frame_over_cap_layers());
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_emit_produces_text_spans);
    RUN_TEST(test_rich_only_frame_binds_text_material);
    RUN_TEST(test_fixed_block_size_matches_solved);
    RUN_TEST(test_single_style_one_span_per_line);
    RUN_TEST(test_double_walk_is_deterministic);
    RUN_TEST(test_emit_groups_text_by_font);
    RUN_TEST(test_emit_single_face_one_set_font);
    RUN_TEST(test_emit_synth_italic_wires_and_resets_oblique);
    RUN_TEST(test_emit_real_italic_face_no_oblique);
    RUN_TEST(test_emit_more_than_four_fonts_no_drop);
    RUN_TEST(test_default_layers_by_kind);
    RUN_TEST(test_layer_override);
    RUN_TEST(test_font_group_per_layer);
    RUN_TEST(test_font_rebinds_per_layer_for_shared_face);
    RUN_TEST(test_layer_drain_orders_ascending);
    RUN_TEST(test_default_mixed_block_band_flush_count);
    RUN_TEST(test_mixed_auto_and_explicit_layers);
    RUN_TEST(test_over_cap_layers_hard_guard);
    RUN_TEST(test_inline_image_emits_sprite_and_text);
    RUN_TEST(test_inline_image_defaults_material_from_ctx);
    RUN_TEST(test_inline_image_fades_with_parent_opacity);
    RUN_TEST(test_two_inline_images_coalesce);
    RUN_TEST(test_inline_images_not_in_image_command_count);
    RUN_TEST(test_inline_image_tint_packed);
    RUN_TEST(test_inline_image_resolves_by_name);
    RUN_TEST(test_inline_image_valign_y);
    RUN_TEST(test_fx_wave_deterministic);
    RUN_TEST(test_fx_shake_deterministic);
    RUN_TEST(test_fx_shake_negative_time_defined);
    RUN_TEST(test_fx_rainbow_deterministic);
    RUN_TEST(test_fx_pulse_deterministic);
    RUN_TEST(test_fx_bounce_deterministic);
    RUN_TEST(test_fx_glow_deterministic);
    RUN_TEST(test_fx_sway_deterministic);
    RUN_TEST(test_fx_params_override_vs_default);
    RUN_TEST(test_fx_push_effect_ex_tunes_emit);
    RUN_TEST(test_fx_push_effect_ex_null_stock_id);
    RUN_TEST(test_fx_markup_params_apply);
    RUN_TEST(test_fx_fade_in_visibility);
    RUN_TEST(test_fx_image_shifts_quad_visual_only);
    RUN_TEST(test_fx_fade_in_skips_image);
    RUN_TEST(test_link_hover_and_click);
    RUN_TEST(test_link_press_a_release_b_no_click);
    RUN_TEST(test_link_no_union_across_gap);
    RUN_TEST(test_link_hover_honors_block_transform);
    RUN_TEST(test_object_draws_at_solved_box);
    RUN_TEST(test_object_effect_and_skip);
    RUN_TEST(test_object_draw_receives_resolved_color);
    RUN_TEST(test_object_markup_reaches_draw_fn);
    RUN_TEST(test_object_world_mat4_matches_block_transform);
    RUN_TEST(test_object_degenerate_measure_asserts);
    RUN_TEST(test_object_baseline_honours_ascent);
    RUN_TEST(test_two_rich_text_blocks_one_frame_no_trap);
    RUN_TEST(test_markup_e2e_emit_and_link);
    RUN_TEST(test_custom_fx_runs_via_builder);
    RUN_TEST(test_custom_fx_runs_via_markup);
    return UNITY_END();
}
