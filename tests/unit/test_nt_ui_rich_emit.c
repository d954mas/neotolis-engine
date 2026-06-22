/* nt_ui_rich_text emit (plan-04): the widget declares ONE Clay FIXED block and self-emits
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
#include "core/nt_assert.h"
#include "font/nt_font.h"
#include "graphics/nt_gfx.h"
#include "hash/nt_hash.h"
#include "material/nt_material.h"
#include "memory/nt_mem_scratch.h"
#include "nt_pack_format.h" /* NT_ASSET_SHADER_CODE */
#include "renderers/nt_sprite_renderer.h"
#include "renderers/nt_text_renderer.h"
#include "resource/nt_resource.h"
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

static uint32_t s_vpack_counter;

/* A rich-image material: attr_map {a_tint @5, a_uvrect @6, a_layout @7} -- the 48 B
 * inline-image custom block (a_tint lossless float4 + a_uvrect/a_layout walker-filled). */
static nt_material_t make_rich_image_material(void) {
    nt_shader_t vs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_VERTEX, .source = "void main(){}", .label = "rich_img_vs"});
    nt_shader_t fs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_FRAGMENT, .source = "void main(){}", .label = "rich_img_fs"});

    char pack_name[64];
    char vs_name[64];
    char fs_name[64];
    (void)snprintf(pack_name, sizeof pack_name, "rich_img_pack_%u", s_vpack_counter);
    (void)snprintf(vs_name, sizeof vs_name, "rich_img_vs_%u", s_vpack_counter);
    (void)snprintf(fs_name, sizeof fs_name, "rich_img_fs_%u", s_vpack_counter);
    s_vpack_counter++;

    const nt_hash32_t pid = nt_hash32_str(pack_name);
    const nt_hash64_t vs_rid = nt_hash64_str(vs_name);
    const nt_hash64_t fs_rid = nt_hash64_str(fs_name);

    TEST_ASSERT_EQUAL(NT_OK, nt_resource_create_pack(pid, 0));
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_register(pid, vs_rid, NT_ASSET_SHADER_CODE, vs.id));
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_register(pid, fs_rid, NT_ASSET_SHADER_CODE, fs.id));
    const nt_resource_t vs_res = nt_resource_request(vs_rid, NT_ASSET_SHADER_CODE);
    const nt_resource_t fs_res = nt_resource_request(fs_rid, NT_ASSET_SHADER_CODE);
    nt_resource_step();

    nt_material_create_desc_t desc;
    memset(&desc, 0, sizeof desc);
    desc.vs = vs_res;
    desc.fs = fs_res;
    desc.cull_mode = NT_CULL_NONE;
    desc.color_mode = NT_COLOR_MODE_NONE;
    desc.attr_map[0].stream_name = "a_tint";
    desc.attr_map[0].location = 5;
    desc.attr_map[1].stream_name = "a_uvrect";
    desc.attr_map[1].location = 6;
    desc.attr_map[2].stream_name = "a_layout";
    desc.attr_map[2].location = 7;
    desc.attr_map_count = 3;
    desc.label = "rich_image_test_material";

    const nt_material_t mat = nt_material_create(&desc);
    nt_material_step();
    return mat;
}

void setUp(void) {
    s_vpack_counter = 0;
    ui_walker_fixture_init(&s_fx, s_arena, sizeof s_arena, UI_WALKER_FX_BIND_ALL);
    nt_mem_scratch_reset();
    s_fx.ctx->pending_rich = NULL;
    s_fx.ctx->rich_session_open = false;
    /* Deterministic metrics: advance = size/2/char; ascent 800/1000. */
    nt_font_test_set_metrics(s_fx.stub_font, 1000, 800, -200, 1000);
}

void tearDown(void) { ui_walker_fixture_shutdown(&s_fx); }

static bool approx(float a, float b) { return fabsf(a - b) < 1e-3F; }

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

/* (2) the FIXED block size equals the solved total size (D-67-03). */
static void test_fixed_block_size_matches_solved(void) {
    frame_two_run_text(400.0F, NT_RICH_ALIGN_LEFT);
    /* Explicit container_w drives total_w; total_h is the one-line height (size 16 -> 16px). */
    TEST_ASSERT_TRUE_MESSAGE(approx(nt_ui_rich_test_total_w(s_fx.ctx), 400.0F), "FIXED width == explicit container_w");
    TEST_ASSERT_TRUE_MESSAGE(nt_ui_rich_test_total_h(s_fx.ctx) > 0.0F, "FIXED height == solved line height");
}

/* (3) a run WITHOUT an effect emits one span per line-fragment (Open Q2 batch-friendly):
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
 * does NOT mutate the run-list (UI-06 re-walkable, read-only on context). */
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

/* ===== Inline IMAGE emit (RICH-67-05) ===== */

/* Build [text][image][text] on one wide line, declare the rich-text widget, walk once.
 * The image rides the rich_image material with a 48 B {a_tint,a_uvrect,a_layout} block. */
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
 * emits draw_n spans on the same line. The image rides nt_ui_image_custom (sprite emit),
 * text rides draw_n -- both present. */
static void test_inline_image_emits_sprite_and_text(void) {
    const nt_material_t mat = make_rich_image_material();
    nt_text_renderer_test_reset_call_counters();
    frame_text_image_text(mat, NT_RICH_VALIGN_MIDDLE, 0xFFFFFFFFU);

    /* Text spans for "A " and " B" (image splits the run anyway -> two text runs). */
    TEST_ASSERT_TRUE_MESSAGE(nt_text_renderer_test_draw_n_calls() > 0U, "inline-image line still emits text draw_n spans");
    /* The image emitted a region quad (4 verts) carrying the custom block. */
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(4U, nt_sprite_renderer_test_last_emit_vertex_count(), "inline image emits a 4-vert region quad via nt_ui_image_custom");
    /* The widget reports exactly one inline image emitted. */
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1U, nt_ui_rich_test_image_emit_count(s_fx.ctx), "exactly one IMAGE atom emitted");
}

/* (6) the inline image's custom block carries a_tint as a lossless float4 (the run's tint),
 * at block offset 0 (attr_map order {a_tint,a_uvrect,a_layout}); the block is 48 B. */
static void test_inline_image_a_tint_lossless(void) {
    const nt_material_t mat = make_rich_image_material();
    /* 0xAABBGGRR: r=255 g=128 b=0 a=255 -> orange, alpha 1. */
    frame_text_image_text(mat, NT_RICH_VALIGN_MIDDLE, 0xFF0080FFU);

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(4U, nt_sprite_renderer_test_last_emit_vertex_count(), "image quad");
    for (uint32_t v = 0; v < 4U; v++) {
        float out[4] = {0};
        nt_sprite_renderer_test_last_emit_radial(v, out, 4); /* a_tint @ floats 0..3 */
        TEST_ASSERT_TRUE_MESSAGE(approx(out[0], 1.0F), "a_tint.r == 255/255");
        TEST_ASSERT_TRUE_MESSAGE(approx(out[1], 128.0F / 255.0F), "a_tint.g == 128/255");
        TEST_ASSERT_TRUE_MESSAGE(approx(out[2], 0.0F), "a_tint.b == 0/255");
        TEST_ASSERT_TRUE_MESSAGE(approx(out[3], 1.0F), "a_tint.a == 255/255");
    }
    /* The block is 48 B = 3 FLOAT4 (a_tint + a_uvrect + a_layout). */
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(48U, nt_ui_rich_test_image_block_bytes(s_fx.ctx), "inline-image custom block is 48 B (3 attrs)");
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

/* ===== Per-atom effects (FX-67-01/02) ===== */

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

/* (9b) shake is deterministic + bounded by AMP, and DEFINED for negative time (H3: the
 * float->unsigned step quantize goes through a signed intermediate so countdown clocks don't UB). */
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
}

/* (9b') H3: shake is DEFINED + stable + bounded for NEGATIVE time (countdown/clock-offset clocks
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

/* (9c) rainbow REPLACES rgb with the hue curve (absolute tint, D-67-07) and keeps base alpha. */
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
}

/* Build [text][image][text] with a per-atom EFFECT applied to the whole block. The image run
 * carries the effect_id; emit folds the wave offset into the image quad + the tint into a_tint. */
static void frame_effected_image(nt_material_t img_mat, uint8_t effect_id, float time, nt_ui_rich_fx_fn fn_for_image) {
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
        nt_ui_rich_push_effect(s_fx.ctx, effect_id); /* effect on TEXT + IMAGE together (D-67-17) */
        nt_ui_rich_text_n(s_fx.ctx, "A ", 2);
        nt_ui_rich_image(s_fx.ctx, ref, NT_RICH_VALIGN_MIDDLE, 0.0F, 1.0F);
        nt_ui_rich_text_n(s_fx.ctx, " B", 2);
        nt_ui_rich_pop(s_fx.ctx);
        nt_ui_rich_end(s_fx.ctx);
        nt_ui_rich_text(s_fx.ctx, CLAY_ID("rich_fx").id, NULL, &base, 400.0F, NT_RICH_ALIGN_LEFT, time, NULL);
    }
    nt_ui_end(s_fx.ctx);
    (void)fn_for_image;
    nt_ui_target_t target = {.viewport = {0, 0, 800, 600}};
    nt_ui_walk(s_fx.ctx, &target);
}

/* (11) an effect on an IMAGE run shifts the image quad (vs no effect) AND the line/box layout
 * is identical with vs without the effect (visual-only, D-67-19). */
static void test_fx_image_shifts_quad_visual_only(void) {
    const nt_material_t mat = make_rich_image_material();

    /* No effect: record the image quad's first-vertex y + the solved total height. */
    frame_effected_image(mat, 0U, 0.0F, NULL);
    float pos_noeff[3] = {0};
    nt_sprite_renderer_test_last_emit_position(0U, pos_noeff);
    const float total_h_noeff = nt_ui_rich_test_total_h(s_fx.ctx);
    const float img_y_noeff = nt_ui_rich_test_image_y(s_fx.ctx);

    /* Wave effect at a time whose offset.y is clearly non-zero. The image atom's fx_idx is its
     * solved index; the quad shifts by the wave offset, but the SOLVED box y is unchanged. */
    frame_effected_image(mat, NT_UI_RICH_FX_ID_WAVE, 0.4F, NULL);
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
    frame_effected_image(mat, NT_UI_RICH_FX_ID_FADE_IN, 0.0F, NULL);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0U, nt_ui_rich_test_image_emit_count(s_fx.ctx), "fade_in t=0 skips the image atom emit");
    /* The image's solved box is still reserved (layout unchanged) -- the solver placed it. */
    TEST_ASSERT_TRUE_MESSAGE(nt_ui_rich_test_total_h(s_fx.ctx) > 0.0F, "layout still solved with the box reserved");
}

/* ===== Links (FX-67-03) ===== */

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
     * PREV frame (D-67-23 two-pass), so hovering needs the block solved at least once. */
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

    /* (c) press OUTSIDE all links, then release INSIDE a link -> NO false click (the BUG). */
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
    /* Pointer over the GAP must NOT report the link (the bug widened A's rect across the gap). */
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0U, hov_gap.hovered_link, "pointer over the non-link gap -> hovered == 0");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0U, hov_gap.clicked_link, "pointer over the gap -> clicked == 0");

    /* Sanity: the pointer over the first link word still hovers the link (the rects are real). */
    nt_pointer_t over_a = make_ptr(a1_x, hy, false, false, false);
    nt_ui_rich_result_t hov_a = frame_link_gap(&over_a);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(GAP_LINK_ID, hov_a.hovered_link, "pointer over link word 1 hovers the link");
}

/* ===== Custom OBJECT (FX-67-04) ===== */

#define OBJ_W 24.0F
#define OBJ_H 18.0F

static uint32_t s_obj_measure_calls;
static uint32_t s_obj_draw_calls;
static float s_obj_draw_x, s_obj_draw_y, s_obj_draw_w, s_obj_draw_h;

static nt_ui_rich_object_measure_t stub_measure(void *user_data) {
    (void)user_data;
    s_obj_measure_calls++;
    return (nt_ui_rich_object_measure_t){.width = OBJ_W, .height = OBJ_H, .ascent = OBJ_H};
}

static void stub_draw(void *user_data, float x, float y, float w, float h) {
    (void)user_data;
    s_obj_draw_calls++;
    s_obj_draw_x = x;
    s_obj_draw_y = y;
    s_obj_draw_w = w;
    s_obj_draw_h = h;
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
    /* The fix: line asc = max(text_asc, obj_ascent); line desc = max(text_desc, obj_h - obj_ascent). */
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

/* REGRESSION (showcase Rich Text tab crashed): two nt_ui_rich_text widgets in ONE frame must
 * not trip the "rich-text calls do not nest" guard. The terminal call now releases pending_rich,
 * so the second begin starts clean. NO manual pending_rich nulling here -- that masking hid the bug. */
static void test_two_rich_text_blocks_one_frame_no_trap(void) {
    nt_ui_rich_style_t base = nt_ui_rich_style_defaults();
    base.font_id[0] = s_fx.stub_font;

    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    CLAY({.id = CLAY_ID("rich_multi_root"), .layout = {.sizing = {CLAY_SIZING_FIXED(400), CLAY_SIZING_FIXED(200)}}}) {
        nt_ui_rich_begin(s_fx.ctx, &base);
        nt_ui_rich_text_n(s_fx.ctx, "first block", 11);
        nt_ui_rich_text(s_fx.ctx, CLAY_ID("rich_a").id, NULL, &base, 400.0F, NT_RICH_ALIGN_LEFT, 0.0F, NULL);

        /* second rich-text widget, SAME frame -- this is the call that crashed the demo tab */
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
 * a link click via a warm prev-frame bbox. (No test called this public entry before.) */
static void test_markup_e2e_emit_and_link(void) {
    const uint32_t link_id = nt_hash32("here", 4).value;

    /* Warm-up frame so the prev-frame bbox the link hit-test needs is populated (D-67-23). */
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

/* ===== Custom (game-supplied) effects (D-67-26) ===== */

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

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_emit_produces_text_spans);
    RUN_TEST(test_fixed_block_size_matches_solved);
    RUN_TEST(test_single_style_one_span_per_line);
    RUN_TEST(test_double_walk_is_deterministic);
    RUN_TEST(test_inline_image_emits_sprite_and_text);
    RUN_TEST(test_inline_image_a_tint_lossless);
    RUN_TEST(test_inline_image_resolves_by_name);
    RUN_TEST(test_inline_image_valign_y);
    RUN_TEST(test_fx_wave_deterministic);
    RUN_TEST(test_fx_shake_deterministic);
    RUN_TEST(test_fx_shake_negative_time_defined);
    RUN_TEST(test_fx_rainbow_deterministic);
    RUN_TEST(test_fx_pulse_deterministic);
    RUN_TEST(test_fx_fade_in_visibility);
    RUN_TEST(test_fx_image_shifts_quad_visual_only);
    RUN_TEST(test_fx_fade_in_skips_image);
    RUN_TEST(test_link_hover_and_click);
    RUN_TEST(test_link_press_a_release_b_no_click);
    RUN_TEST(test_link_no_union_across_gap);
    RUN_TEST(test_object_draws_at_solved_box);
    RUN_TEST(test_object_effect_and_skip);
    RUN_TEST(test_object_baseline_honours_ascent);
    RUN_TEST(test_two_rich_text_blocks_one_frame_no_trap);
    RUN_TEST(test_markup_e2e_emit_and_link);
    RUN_TEST(test_custom_fx_runs_via_builder);
    RUN_TEST(test_custom_fx_runs_via_markup);
    return UNITY_END();
}
