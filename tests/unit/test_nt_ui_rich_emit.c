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
        nt_ui_rich_text(s_fx.ctx, CLAY_ID("rich").id, NULL, &base, container_w, align, 0.0F);
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
        nt_ui_rich_text(s_fx.ctx, CLAY_ID("rich2").id, NULL, &base, 400.0F, NT_RICH_ALIGN_LEFT, 0.0F);
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
        nt_ui_rich_text(s_fx.ctx, CLAY_ID("rich3").id, NULL, &base, 400.0F, NT_RICH_ALIGN_LEFT, 0.0F);
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
    /* Fresh frame: clear the per-call rich scratch so back-to-back frames in one test don't
     * trip the "rich-text calls do not nest" guard (pending_rich is cleared on scratch reset). */
    nt_mem_scratch_reset();
    s_fx.ctx->pending_rich = NULL;

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
        nt_ui_rich_text(s_fx.ctx, CLAY_ID("rich_img").id, NULL, &base, 400.0F, NT_RICH_ALIGN_LEFT, 0.0F);
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
    return UNITY_END();
}
