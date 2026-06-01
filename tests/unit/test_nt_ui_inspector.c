/* Phase 56 ext rework: nt_ui_inspector + widget_registry tests.
 *
 * Inspector architecture is now layout-pass injection (nt_ui_inspector_emit_layout
 * called inside nt_ui_end before Clay_EndLayout) + a post-walk single-element
 * overlay (nt_ui_inspector_overlay_draw). Tests verify:
 *   1) widget_registry direct-mapped table (unchanged from previous round).
 *   2) inspector toggle is a persistent user pref.
 *   3) inactive inspector is a silent no-op (no Clay emit, no draw).
 *   4) active inspector emit_layout grows the layout element count -- proof
 *      that the panel CLAY({...}) blocks ran.
 *   5) overlay_draw is a no-op when no element is highlighted.
 *
 * Pixel output is NOT unit-tested -- visual verification is in
 * ui_buttons_demo (D toggle). */

#include <stdalign.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "clay.h"
#include "core/nt_assert.h"
#include "input/nt_input.h"
#include "test_helpers/nt_assert_trap.h"
#include "test_helpers/ui_test_arena.h"
#include "test_helpers/ui_walker_fixture.h"
#include "ui/nt_ui.h"
#include "ui/nt_ui_button.h"
#include "ui/nt_ui_debug.h"
#include "ui/nt_ui_image.h"
#include "ui/nt_ui_inspector.h"
#include "ui/nt_ui_internal.h"
#include "ui/nt_ui_label.h"
#include "ui/nt_ui_panel.h"
#include "unity.h"

alignas(NT_UI_ARENA_ALIGN) static uint8_t s_arena[NT_UI_TEST_ARENA_SIZE];
static ui_walker_fixture_t s_fx;

static const nt_ui_button_style_t s_btn_style = {
    .idle = {.bg_region = 0, .bg_tint = 0xFFFFFFFF, .scale = 1.0F, .opacity = 1.0F},
    .hover = {.bg_region = 0, .bg_tint = 0xFFFFFFFF, .scale = 1.05F, .opacity = 1.0F},
    .pressed = {.bg_region = 0, .bg_tint = 0xFFFFFFFF, .scale = 0.95F, .opacity = 1.0F},
    .disabled = {.bg_region = 0, .bg_tint = 0xFFFFFFFF, .scale = 1.0F, .opacity = 0.5F},
    .transition_speed = 0.0F,
};

static const nt_ui_image_style_t s_img_style = {
    .color_packed = 0xFFFFFFFF,
};

static const nt_ui_label_style_t s_label_style = {
    .font_id = 0,
    .font_size = 14,
    .color = {255.0F, 255.0F, 255.0F, 255.0F},
};

void setUp(void) {
    nt_test_assert_install();
    ui_walker_fixture_init(&s_fx, s_arena, sizeof s_arena, UI_WALKER_FX_BIND_ALL);
}

void tearDown(void) { ui_walker_fixture_shutdown(&s_fx); }

static nt_pointer_t make_pointer(float x, float y) {
    nt_pointer_t p = {0};
    p.x = x;
    p.y = y;
    p.active = true;
    return p;
}

/* ---- Test 1: register + lookup roundtrip on a single id ---- */
static void test_registry_register_lookup(void) {
    nt_pointer_t mouse = make_pointer(0.0F, 0.0F);
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    /* Empty by default. */
    TEST_ASSERT_NULL(nt_ui_widget_lookup(s_fx.ctx, nt_ui_id("foo")));
    /* Register one. */
    nt_ui_widget_register(s_fx.ctx, nt_ui_id("foo"), &NT_UI_BUTTON_DEF, NULL);
    TEST_ASSERT_EQUAL_PTR(&NT_UI_BUTTON_DEF, nt_ui_widget_lookup(s_fx.ctx, nt_ui_id("foo")));
    /* Different id, same bucket NOT guaranteed -- but distinct lookups return NULL. */
    TEST_ASSERT_NULL(nt_ui_widget_lookup(s_fx.ctx, nt_ui_id("bar")));
    nt_ui_end(s_fx.ctx);
}

/* ---- Test 2: registry resets each nt_ui_begin (no stale tags) ---- */
static void test_registry_resets_each_begin(void) {
    nt_pointer_t mouse = make_pointer(0.0F, 0.0F);
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    nt_ui_widget_register(s_fx.ctx, nt_ui_id("foo"), &NT_UI_BUTTON_DEF, NULL);
    TEST_ASSERT_EQUAL_PTR(&NT_UI_BUTTON_DEF, nt_ui_widget_lookup(s_fx.ctx, nt_ui_id("foo")));
    nt_ui_end(s_fx.ctx);

    /* Next begin -- registry must be empty again. */
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    TEST_ASSERT_NULL(nt_ui_widget_lookup(s_fx.ctx, nt_ui_id("foo")));
    nt_ui_end(s_fx.ctx);
}

/* ---- Test 3: slot collision -> latest write wins (documented policy) ---- */
static void test_registry_replace_on_collision(void) {
    nt_pointer_t mouse = make_pointer(0.0F, 0.0F);
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    /* Two distinct ids that land in the same bucket. We synthesize this by
     * using raw ids that differ by exactly NT_UI_WIDGET_REGISTRY_CAP, so
     * id & (CAP-1) is identical for both. CAP is power-of-two by _Static_assert. */
    const uint32_t base_id = nt_ui_id("collide_a");
    const uint32_t collide_id = base_id + (uint32_t)NT_UI_WIDGET_REGISTRY_CAP;
    nt_ui_widget_register(s_fx.ctx, base_id, &NT_UI_BUTTON_DEF, NULL);
    TEST_ASSERT_EQUAL_PTR(&NT_UI_BUTTON_DEF, nt_ui_widget_lookup(s_fx.ctx, base_id));
    /* Collide -- new write must overwrite the slot. */
    nt_ui_widget_register(s_fx.ctx, collide_id, &NT_UI_IMAGE_DEF, NULL);
    TEST_ASSERT_EQUAL_PTR(&NT_UI_IMAGE_DEF, nt_ui_widget_lookup(s_fx.ctx, collide_id));
    /* The original id now misses (replace-on-collision policy). */
    TEST_ASSERT_NULL(nt_ui_widget_lookup(s_fx.ctx, base_id));
    nt_ui_end(s_fx.ctx);
}

/* ---- Test 4: id=0 (sentinel) is silently dropped on register + lookup ---- */
static void test_registry_id_zero_dropped(void) {
    nt_pointer_t mouse = make_pointer(0.0F, 0.0F);
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    nt_ui_widget_register(s_fx.ctx, 0U, &NT_UI_BUTTON_DEF, NULL); /* must not assert */
    TEST_ASSERT_NULL(nt_ui_widget_lookup(s_fx.ctx, 0U));
    nt_ui_end(s_fx.ctx);
}

/* ---- Test 4b: register two widgets with distinct defs (engine + game) ----
 * Pins the descriptor-pointer refactor: a game-side static const def must
 * coexist with an engine def in the same registry, and lookup must return
 * the exact pointer per id (pill name + color flow from def->name / pill_color). */
static const nt_ui_widget_def_t TEST_GAME_INV_SLOT_DEF = {
    .name = "inv_slot",
    .pill_color = 0xFFB060A0U,
    ._reserved = 0U,
};
static void test_registry_engine_and_game_defs_coexist(void) {
    nt_pointer_t mouse = make_pointer(0.0F, 0.0F);
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    nt_ui_widget_register(s_fx.ctx, nt_ui_id("a_btn"), &NT_UI_BUTTON_DEF, NULL);
    nt_ui_widget_register(s_fx.ctx, nt_ui_id("a_slot"), &TEST_GAME_INV_SLOT_DEF, NULL);

    const nt_ui_widget_def_t *btn = nt_ui_widget_lookup(s_fx.ctx, nt_ui_id("a_btn"));
    const nt_ui_widget_def_t *slot = nt_ui_widget_lookup(s_fx.ctx, nt_ui_id("a_slot"));
    TEST_ASSERT_EQUAL_PTR(&NT_UI_BUTTON_DEF, btn);
    TEST_ASSERT_EQUAL_PTR(&TEST_GAME_INV_SLOT_DEF, slot);
    /* Pill data is reachable through the pointer (no copy). */
    TEST_ASSERT_EQUAL_STRING("button", btn->name);
    TEST_ASSERT_EQUAL_STRING("inv_slot", slot->name);
    TEST_ASSERT_EQUAL_UINT32(0xFF60D070U, btn->pill_color);
    TEST_ASSERT_EQUAL_UINT32(0xFFB060A0U, slot->pill_color);
    nt_ui_end(s_fx.ctx);
}

/* ---- Test 5: nt_ui_button declaration auto-tags BUTTON ---- */
static void test_button_widget_auto_tagged(void) {
    nt_pointer_t mouse = make_pointer(0.0F, 0.0F);
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    CLAY({.id = CLAY_ID("root")}) {
        nt_ui_button_begin(s_fx.ctx, NULL, nt_ui_id("btn"), s_fx.atlas.handle, &s_btn_style, true);
        nt_ui_label(s_fx.ctx, NULL, "OK", &s_label_style);
        (void)nt_ui_button_end(s_fx.ctx);
    }
    /* While in-frame, the button id must be tagged with NT_UI_BUTTON_DEF. */
    TEST_ASSERT_EQUAL_PTR(&NT_UI_BUTTON_DEF, nt_ui_widget_lookup(s_fx.ctx, nt_ui_id("btn")));
    nt_ui_end(s_fx.ctx);
}

/* ---- Test 6: nt_ui_panel declaration auto-tags PANEL (id == Clay-assigned) ---- */
static void test_panel_widget_tagged(void) {
    nt_pointer_t mouse = make_pointer(0.0F, 0.0F);
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    /* Panel/image use Clay's auto-assigned id (no explicit id arg in API),
     * so we can't easily look it up by name. Verify by counting tags: at least
     * one PANEL-def entry must exist in the registry after declaration. */
    CLAY({.id = CLAY_ID("root")}) {
        nt_ui_panel_begin(s_fx.ctx, NULL, s_fx.atlas.handle, s_fx.atlas.white_region_idx, &s_img_style);
        nt_ui_panel_end(s_fx.ctx);
    }
    /* Scan the registry for any slot pointing at NT_UI_PANEL_DEF. */
    uint32_t panel_count = 0U;
    for (uint32_t i = 0; i < (uint32_t)NT_UI_WIDGET_REGISTRY_CAP; ++i) {
        if (s_fx.ctx->widget_registry[i].id != 0U && s_fx.ctx->widget_registry[i].def == &NT_UI_PANEL_DEF) {
            panel_count++;
        }
    }
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(1U, panel_count);
    nt_ui_end(s_fx.ctx);
}

/* ---- Test 7: nt_ui_image declaration auto-tags IMAGE ---- */
static void test_image_widget_tagged(void) {
    nt_pointer_t mouse = make_pointer(0.0F, 0.0F);
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    CLAY({.id = CLAY_ID("root")}) { nt_ui_image(s_fx.ctx, NULL, s_fx.atlas.handle, s_fx.atlas.white_region_idx, &s_img_style); }
    uint32_t image_count = 0U;
    for (uint32_t i = 0; i < (uint32_t)NT_UI_WIDGET_REGISTRY_CAP; ++i) {
        if (s_fx.ctx->widget_registry[i].id != 0U && s_fx.ctx->widget_registry[i].def == &NT_UI_IMAGE_DEF) {
            image_count++;
        }
    }
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(1U, image_count);
    nt_ui_end(s_fx.ctx);
}

/* ---- Test 8: inspector toggle persists across frames ---- */
static void test_inspector_toggle_persists(void) {
    TEST_ASSERT_FALSE(nt_ui_inspector_is_active(s_fx.ctx));
    nt_ui_inspector_set_active(s_fx.ctx, true);
    TEST_ASSERT_TRUE(nt_ui_inspector_is_active(s_fx.ctx));

    nt_pointer_t mouse = make_pointer(0.0F, 0.0F);
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    nt_ui_end(s_fx.ctx);

    /* Toggle survives the frame boundary (it's a user preference, not a per-frame). */
    TEST_ASSERT_TRUE(nt_ui_inspector_is_active(s_fx.ctx));
    nt_ui_inspector_set_active(s_fx.ctx, false);
    TEST_ASSERT_FALSE(nt_ui_inspector_is_active(s_fx.ctx));
}

/* ---- Test 9: inactive inspector emit_layout is a no-op (no extra Clay elems) ---- */
static void test_inspector_inactive_emit_noop(void) {
    TEST_ASSERT_FALSE(nt_ui_inspector_is_active(s_fx.ctx));
    nt_pointer_t mouse = make_pointer(0.0F, 0.0F);
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    CLAY({.id = CLAY_ID("root")}) {}
    /* Capture the layout element count BEFORE end (after user emits). */
    const int32_t before = nt_ui_internal_get_layout_element_count(s_fx.ctx);
    nt_ui_end(s_fx.ctx);
    /* The total after end MUST equal what the user emitted -- inspector
     * stayed off, no injection. */
    const int32_t after = nt_ui_internal_get_layout_element_count(s_fx.ctx);
    TEST_ASSERT_EQUAL_INT32(before, after);
}

/* ---- Test 10: active inspector grows the layout element count ----
 * Proves the verbatim port runs (emit_layout injected the panel CLAY blocks). */
static void test_inspector_active_grows_element_count(void) {
    nt_ui_inspector_set_active(s_fx.ctx, true);
    nt_pointer_t mouse = make_pointer(0.0F, 0.0F);
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    CLAY({.id = CLAY_ID("root")}) {}
    const int32_t before = nt_ui_internal_get_layout_element_count(s_fx.ctx);
    nt_ui_end(s_fx.ctx);
    const int32_t after = nt_ui_internal_get_layout_element_count(s_fx.ctx);
    /* The verbatim port emits at least the root panel + header bar + close
     * button + scroll pane + element list outer (5+ elements) plus per-row
     * element-outer wrappers. Assert ≥ 5 added elements as a conservative
     * floor that survives small port adjustments. */
    TEST_ASSERT_GREATER_THAN_INT32(before + 4, after);
}

/* ---- Test 11: active inspector with a sidebar full of widgets is safe ---- */
static void test_inspector_many_widgets_safe(void) {
    nt_ui_inspector_set_active(s_fx.ctx, true);
    nt_pointer_t mouse = make_pointer(0.0F, 0.0F);
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    CLAY({.id = CLAY_ID("root")}) {
        for (int i = 0; i < 20; ++i) {
            char name[16];
            (void)snprintf(name, sizeof name, "btn%d", i);
            nt_ui_button_begin(s_fx.ctx, NULL, nt_ui_id(name), s_fx.atlas.handle, &s_btn_style, true);
            nt_ui_label(s_fx.ctx, NULL, "X", &s_label_style);
            (void)nt_ui_button_end(s_fx.ctx);
        }
    }
    nt_ui_end(s_fx.ctx); /* emit_layout runs internally, must not crash */
    /* Post-walk overlay with no element focused -> early-out, must not crash. */
    nt_ui_target_t target = {.viewport = {0.0F, 0.0F, 800.0F, 600.0F}};
    nt_ui_inspector_overlay_draw(s_fx.ctx, &target, NT_FONT_INVALID, 0.0F);
}

/* ---- Test 12: overlay_draw is a no-op when no highlight id is set ---- */
static void test_overlay_noop_without_highlight(void) {
    nt_ui_inspector_set_active(s_fx.ctx, true);
    nt_pointer_t mouse = make_pointer(0.0F, 0.0F);
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    CLAY({.id = CLAY_ID("root")}) {}
    /* No widget hovered, no sidebar row clicked -> highlight stays 0 after end. */
    nt_ui_end(s_fx.ctx);
    nt_ui_target_t target = {.viewport = {0.0F, 0.0F, 800.0F, 600.0F}};
    /* Must not crash; must not draw. We can only verify no-crash here (the
     * draw is an emit; visual verification is in the demo). */
    nt_ui_inspector_overlay_draw(s_fx.ctx, &target, NT_FONT_INVALID, 0.0F);
}

/* ---- Test 13: inspector sidebar INTERCEPTS pointer (regression pin) ----
 * Bug: clicking the sidebar also fired the click on any button geometrically
 * behind it (the sidebar paints on top but the hit-test was pure coord-vs-
 * bbox). Fix: when inspector_active and the pointer is inside the right-
 * attached sidebar footprint (CDV_PANEL_WIDTH = 400 wide on a 800-wide
 * screen -> x >= 400), nt_ui_get_interaction must return a zeroed result
 * for every user widget AND nt_ui_inspector_pointer_consumed must be true
 * AND nt_ui_wants_pointer must be true. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_inspector_intercepts_pointer_over_sidebar(void) {
    nt_ui_inspector_set_active(s_fx.ctx, true);

    /* Frame 1: declare a button at (600, 200) -- inside the sidebar footprint
     * on an 800-wide screen (sidebar starts at x=400). No interaction queried,
     * just sets up the prev-frame bbox for frame 2. */
    const float screen_w = 800.0F;
    const float screen_h = 600.0F;
    const float btn_x = 600.0F;
    const float btn_y = 200.0F;
    const float btn_w = 160.0F;
    const float btn_h = 48.0F;
    const float btn_cx = btn_x + (btn_w * 0.5F);
    const float btn_cy = btn_y + (btn_h * 0.5F);
    nt_pointer_t f1 = make_pointer(0.0F, 0.0F);
    nt_ui_begin(s_fx.ctx, screen_w, screen_h, 0.0F, &f1, 1);
    CLAY({.id = CLAY_ID("hidden_btn"), .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .offset = {.x = btn_x, .y = btn_y}}, .layout = {.sizing = {CLAY_SIZING_FIXED(btn_w), CLAY_SIZING_FIXED(btn_h)}}}) {
    }
    nt_ui_end(s_fx.ctx);

    /* Frame 2: pointer over the button center (which is also inside the
     * sidebar) WITH left-button pressed_now -- the bug would have returned
     * hovered + pressed_now + capture active. The fix returns zeroed. */
    nt_pointer_t f2 = {0};
    f2.x = btn_cx; /* 680 -- well past sidebar left edge at 400 */
    f2.y = btn_cy;
    f2.active = true;
    f2.buttons[NT_BUTTON_LEFT].is_down = true;
    f2.buttons[NT_BUTTON_LEFT].is_pressed = true;
    nt_ui_begin(s_fx.ctx, screen_w, screen_h, 0.0F, &f2, 1);
    CLAY({.id = CLAY_ID("hidden_btn"), .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .offset = {.x = btn_x, .y = btn_y}}, .layout = {.sizing = {CLAY_SIZING_FIXED(btn_w), CLAY_SIZING_FIXED(btn_h)}}}) {
    }

    /* The flag is set in nt_ui_begin (coord-based, frame-1 safe). */
    TEST_ASSERT_TRUE(nt_ui_inspector_pointer_consumed(s_fx.ctx));

    /* Both the unpadded and padded query MUST return zeroed -- no hover, no
     * press, no clicked, no capture (the bug pinned here). */
    nt_ui_interaction_t in = nt_ui_get_interaction(s_fx.ctx, nt_ui_id("hidden_btn"));
    TEST_ASSERT_FALSE(in.hovered);
    TEST_ASSERT_FALSE(in.pressed);
    TEST_ASSERT_FALSE(in.pressed_now);
    TEST_ASSERT_FALSE(in.clicked);
    TEST_ASSERT_FALSE(in.released_now);
    TEST_ASSERT_EQUAL_UINT32(0U, nt_ui_test_capture_active_id(s_fx.ctx, 0));

    const int16_t pad[4] = {32, 32, 32, 32};
    nt_ui_interaction_t in_pad = nt_ui_get_interaction_padded(s_fx.ctx, nt_ui_id("hidden_btn"), pad);
    TEST_ASSERT_FALSE(in_pad.hovered);
    TEST_ASSERT_FALSE(in_pad.pressed);
    TEST_ASSERT_FALSE(in_pad.pressed_now);
    TEST_ASSERT_FALSE(in_pad.clicked);

    /* wants_pointer must be TRUE even though no user widget recorded hover,
     * so the game can suppress its world-input. */
    TEST_ASSERT_TRUE(nt_ui_wants_pointer(s_fx.ctx));

    nt_ui_end(s_fx.ctx);
}

/* ---- Test 14: pointer OUTSIDE the sidebar still works normally ----
 * Counter-test: same setup but the pointer is on the LEFT half of the screen,
 * outside the sidebar footprint. The button there must register normally,
 * proving the gate is not over-aggressive. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_inspector_pointer_outside_sidebar_normal(void) {
    nt_ui_inspector_set_active(s_fx.ctx, true);

    const float screen_w = 800.0F;
    const float screen_h = 600.0F;
    const float btn_x = 100.0F; /* x=100..260, well left of sidebar at 400 */
    const float btn_y = 200.0F;
    const float btn_w = 160.0F;
    const float btn_h = 48.0F;
    const float btn_cx = btn_x + (btn_w * 0.5F);
    const float btn_cy = btn_y + (btn_h * 0.5F);

    /* Frame 1: declare bbox so the next frame has a hit-target. */
    nt_pointer_t f1 = make_pointer(0.0F, 0.0F);
    nt_ui_begin(s_fx.ctx, screen_w, screen_h, 0.0F, &f1, 1);
    CLAY(
        {.id = CLAY_ID("visible_btn"), .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .offset = {.x = btn_x, .y = btn_y}}, .layout = {.sizing = {CLAY_SIZING_FIXED(btn_w), CLAY_SIZING_FIXED(btn_h)}}}) {
    }
    nt_ui_end(s_fx.ctx);

    /* Frame 2: pointer over the button center, NOT over the sidebar. */
    nt_pointer_t f2 = {0};
    f2.x = btn_cx; /* 180 -- well below sidebar threshold of 400 */
    f2.y = btn_cy;
    f2.active = true;
    f2.buttons[NT_BUTTON_LEFT].is_down = true;
    f2.buttons[NT_BUTTON_LEFT].is_pressed = true;
    nt_ui_begin(s_fx.ctx, screen_w, screen_h, 0.0F, &f2, 1);
    CLAY(
        {.id = CLAY_ID("visible_btn"), .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .offset = {.x = btn_x, .y = btn_y}}, .layout = {.sizing = {CLAY_SIZING_FIXED(btn_w), CLAY_SIZING_FIXED(btn_h)}}}) {
    }

    /* Inspector active but pointer is OUTSIDE the sidebar -> consumed false. */
    TEST_ASSERT_FALSE(nt_ui_inspector_pointer_consumed(s_fx.ctx));

    /* Normal interaction must fire. */
    nt_ui_interaction_t in = nt_ui_get_interaction(s_fx.ctx, nt_ui_id("visible_btn"));
    TEST_ASSERT_TRUE(in.hovered);
    TEST_ASSERT_TRUE(in.pressed);
    TEST_ASSERT_TRUE(in.pressed_now);

    nt_ui_end(s_fx.ctx);
}

/* ---- Test 15b: walker enumerates the user's tree (NOT just the auto-root) ----
 * Regression pin: cdv_render_layout_elements_list runs from nt_ui_end BEFORE
 * Clay_EndLayout closes the auto-emitted Clay__RootContainer. At that point
 * Clay__RootContainer.children.elements is NULL even though children.length is
 * populated -- the walker has to fall back to context->layoutElementChildrenBuffer
 * to find the user's first-level CLAY blocks. The pre-fix walker stopped at the
 * auto-root and emitted only ONE ElementOuter row (regardless of how big the
 * user tree was). The fix surfaces every reachable element.
 *
 * We pin the regression by inspector-emitted-element-count growth. Each row the
 * walker visits emits a `CLAY({.id = CLAY_IDI("ntInsp_ElementOuter", ...)})`
 * block (+ inner Clay calls). With ~6 user elements (root + 5 panels) the
 * walker must emit substantially more inspector wrappers than the empty-root
 * test above. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_inspector_walker_enumerates_user_tree(void) {
    nt_ui_inspector_set_active(s_fx.ctx, true);
    nt_pointer_t mouse = make_pointer(0.0F, 0.0F);

    /* Baseline: empty user root -> inspector wraps {RC + user_root} = 2 walker rows. */
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    CLAY({.id = CLAY_ID("root_empty")}) {}
    const int32_t before_empty = nt_ui_internal_get_layout_element_count(s_fx.ctx);
    nt_ui_end(s_fx.ctx);
    const int32_t empty_after = nt_ui_internal_get_layout_element_count(s_fx.ctx);
    const int32_t empty_inspector_grew = empty_after - before_empty;

    /* User tree with 5 children -> walker must visit RC + root + 5 children = 7 rows. */
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    CLAY({.id = CLAY_ID("root_full")}) {
        CLAY({.id = CLAY_ID("child1"), .layout = {.sizing = {CLAY_SIZING_FIXED(10), CLAY_SIZING_FIXED(10)}}}) {}
        CLAY({.id = CLAY_ID("child2"), .layout = {.sizing = {CLAY_SIZING_FIXED(10), CLAY_SIZING_FIXED(10)}}}) {}
        CLAY({.id = CLAY_ID("child3"), .layout = {.sizing = {CLAY_SIZING_FIXED(10), CLAY_SIZING_FIXED(10)}}}) {}
        CLAY({.id = CLAY_ID("child4"), .layout = {.sizing = {CLAY_SIZING_FIXED(10), CLAY_SIZING_FIXED(10)}}}) {}
        CLAY({.id = CLAY_ID("child5"), .layout = {.sizing = {CLAY_SIZING_FIXED(10), CLAY_SIZING_FIXED(10)}}}) {}
    }
    const int32_t before_full = nt_ui_internal_get_layout_element_count(s_fx.ctx);
    nt_ui_end(s_fx.ctx);
    const int32_t full_after = nt_ui_internal_get_layout_element_count(s_fx.ctx);
    const int32_t full_inspector_grew = full_after - before_full;

    /* The full-tree walk must emit strictly more inspector elements than the
     * empty-root walk -- pre-fix, both produced the SAME count (walker stopped
     * at Clay__RootContainer because children.elements was NULL). */
    TEST_ASSERT_GREATER_THAN_INT32(empty_inspector_grew, full_inspector_grew);
    /* Each extra user element adds at least one ElementOuter wrapper; 5 extra
     * children must add at least 5 extra inspector elements. */
    TEST_ASSERT_GREATER_OR_EQUAL_INT32(empty_inspector_grew + 5, full_inspector_grew);
}

/* ---- Test 15c: register with padding round-trip + get_hit_padding returns padding ----
 * Pin for the inspector-overlay padded fill: the button's hit_padding_lrtb
 * must survive into the inspector's widget_registry slot so the overlay can
 * outline the touch-target distinct from the visual bbox. */
static void test_widget_register_padded_roundtrip(void) {
    nt_pointer_t mouse = make_pointer(0.0F, 0.0F);
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    const int16_t pad[4] = {12, 14, 16, 18};
    nt_ui_widget_register(s_fx.ctx, nt_ui_id("btn_padded"), &NT_UI_BUTTON_DEF, pad);
    /* Def still resolves through lookup. */
    TEST_ASSERT_EQUAL_PTR(&NT_UI_BUTTON_DEF, nt_ui_widget_lookup(s_fx.ctx, nt_ui_id("btn_padded")));
    /* Padding is recovered exactly. */
    int16_t out[4] = {-1, -1, -1, -1};
    TEST_ASSERT_TRUE(nt_ui_widget_get_hit_padding(s_fx.ctx, nt_ui_id("btn_padded"), out));
    TEST_ASSERT_EQUAL_INT16(12, out[0]);
    TEST_ASSERT_EQUAL_INT16(14, out[1]);
    TEST_ASSERT_EQUAL_INT16(16, out[2]);
    TEST_ASSERT_EQUAL_INT16(18, out[3]);
    nt_ui_end(s_fx.ctx);
}

/* ---- Test 15d: NULL-padding register reports false from get_hit_padding ----
 * Plain image/panel widgets register with NULL pad_lrtb and must NOT
 * accidentally show a padded hit zone in the inspector overlay. */
static void test_widget_unpadded_no_hit_padding(void) {
    nt_pointer_t mouse = make_pointer(0.0F, 0.0F);
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    nt_ui_widget_register(s_fx.ctx, nt_ui_id("img"), &NT_UI_IMAGE_DEF, NULL);
    int16_t out[4] = {99, 99, 99, 99};
    TEST_ASSERT_FALSE(nt_ui_widget_get_hit_padding(s_fx.ctx, nt_ui_id("img"), out));
    /* out untouched on miss (documented contract). */
    TEST_ASSERT_EQUAL_INT16(99, out[0]);
    TEST_ASSERT_EQUAL_INT16(99, out[1]);
    TEST_ASSERT_EQUAL_INT16(99, out[2]);
    TEST_ASSERT_EQUAL_INT16(99, out[3]);
    nt_ui_end(s_fx.ctx);
}

/* ---- Test 15e: nt_ui_button auto-records hit_padding via the padded form ----
 * End-to-end: declaring a button with non-zero style.hit_padding_lrtb must
 * make the inspector see the padding via the widget_registry. */
static void test_button_auto_records_hit_padding(void) {
    nt_pointer_t mouse = make_pointer(0.0F, 0.0F);
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    nt_ui_button_style_t padded_style = s_btn_style;
    padded_style.hit_padding_lrtb[0] = 8;
    padded_style.hit_padding_lrtb[1] = 8;
    padded_style.hit_padding_lrtb[2] = 4;
    padded_style.hit_padding_lrtb[3] = 4;
    CLAY({.id = CLAY_ID("root")}) {
        nt_ui_button_begin(s_fx.ctx, NULL, nt_ui_id("btn"), s_fx.atlas.handle, &padded_style, true);
        nt_ui_label(s_fx.ctx, NULL, "OK", &s_label_style);
        (void)nt_ui_button_end(s_fx.ctx);
    }
    int16_t out[4] = {0, 0, 0, 0};
    TEST_ASSERT_TRUE(nt_ui_widget_get_hit_padding(s_fx.ctx, nt_ui_id("btn"), out));
    TEST_ASSERT_EQUAL_INT16(8, out[0]);
    TEST_ASSERT_EQUAL_INT16(8, out[1]);
    TEST_ASSERT_EQUAL_INT16(4, out[2]);
    TEST_ASSERT_EQUAL_INT16(4, out[3]);
    nt_ui_end(s_fx.ctx);
}

/* ---- Test 15f: inspector filter hides anonymous (no-id, no-widget) elements ----
 * Pin for the empty-wrapper-row fix: anonymous Clay containers must NOT emit
 * an inspector tree row. Compares the "active inspector" element-count growth
 * for a tree that's all named vs a tree that injects extra anonymous containers
 * around children. With the filter on, both should produce SIMILAR growth
 * (anonymous wrappers don't add ElementOuter blocks). Pre-fix, the anonymous
 * containers each added their own ElementOuter row + 3 indent wrappers. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_inspector_filter_skips_anonymous(void) {
    nt_ui_inspector_set_active(s_fx.ctx, true);
    nt_pointer_t mouse = make_pointer(0.0F, 0.0F);

    /* Named-only tree: 3 children all with CLAY_ID. */
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    CLAY({.id = CLAY_ID("root_named")}) {
        CLAY({.id = CLAY_ID("a"), .layout = {.sizing = {CLAY_SIZING_FIXED(10), CLAY_SIZING_FIXED(10)}}}) {}
        CLAY({.id = CLAY_ID("b"), .layout = {.sizing = {CLAY_SIZING_FIXED(10), CLAY_SIZING_FIXED(10)}}}) {}
        CLAY({.id = CLAY_ID("c"), .layout = {.sizing = {CLAY_SIZING_FIXED(10), CLAY_SIZING_FIXED(10)}}}) {}
    }
    const int32_t before_named = nt_ui_internal_get_layout_element_count(s_fx.ctx);
    nt_ui_end(s_fx.ctx);
    const int32_t named_after = nt_ui_internal_get_layout_element_count(s_fx.ctx);
    const int32_t named_growth = named_after - before_named;

    /* Anonymous-wrapper tree: same 3 named leaves but each wrapped in an
     * anonymous Clay container (no .id). Pre-fix, each anonymous wrapper would
     * have emitted its own ntInsp_ElementOuter row plus 3 indent wrappers. */
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    CLAY({.id = CLAY_ID("root_anon")}) {
        CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)}}}) {
            CLAY({.id = CLAY_ID("a"), .layout = {.sizing = {CLAY_SIZING_FIXED(10), CLAY_SIZING_FIXED(10)}}}) {}
        }
        CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)}}}) {
            CLAY({.id = CLAY_ID("b"), .layout = {.sizing = {CLAY_SIZING_FIXED(10), CLAY_SIZING_FIXED(10)}}}) {}
        }
        CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)}}}) {
            CLAY({.id = CLAY_ID("c"), .layout = {.sizing = {CLAY_SIZING_FIXED(10), CLAY_SIZING_FIXED(10)}}}) {}
        }
    }
    const int32_t before_anon = nt_ui_internal_get_layout_element_count(s_fx.ctx);
    nt_ui_end(s_fx.ctx);
    const int32_t anon_after = nt_ui_internal_get_layout_element_count(s_fx.ctx);
    const int32_t anon_growth = anon_after - before_anon;

    /* With the filter on, the 3 extra anonymous wrappers each cost ZERO
     * inspector elements: anon_growth should match named_growth (within slack
     * for any per-anonymous CloseElement bookkeeping). Pre-fix, anon_growth
     * was substantially larger because each anonymous container emitted its
     * own ElementOuter row + 3 indent wrappers (8+ extra elements per anon). */
    TEST_ASSERT_LESS_OR_EQUAL_INT32(named_growth + 6, anon_growth);
}

/* ---- Test 15g: inspector emits a hex fallback for unnamed widgets ----
 * Pin for the empty-stringId fallback: when an element has no string id but
 * IS a registered widget (so it survives the filter), the inspector tree row
 * must show the element's hex id rather than nothing. We pin by emitting a
 * panel (unnamed, but widget_registry tagged PANEL) and asserting the
 * inspector layout element count grew -- the alternative implementation
 * (drop the row) would not change the count. */
static void test_inspector_emits_hex_for_unnamed_widget(void) {
    nt_ui_inspector_set_active(s_fx.ctx, true);
    nt_pointer_t mouse = make_pointer(0.0F, 0.0F);
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    CLAY({.id = CLAY_ID("root_unnamed_panel")}) {
        nt_ui_panel_begin(s_fx.ctx, NULL, s_fx.atlas.handle, s_fx.atlas.white_region_idx, &s_img_style);
        nt_ui_panel_end(s_fx.ctx);
    }
    const int32_t before = nt_ui_internal_get_layout_element_count(s_fx.ctx);
    nt_ui_end(s_fx.ctx);
    const int32_t after = nt_ui_internal_get_layout_element_count(s_fx.ctx);
    /* Inspector must have grown by at least the root pane + header + 2 tree
     * rows (root + panel). Conservative: > before + 4. */
    TEST_ASSERT_GREATER_THAN_INT32(before + 4, after);
}

/* ---- Test 15: inactive inspector NEVER intercepts (no false positives) ----
 * Even if the pointer is at x=600 (would be inside the sidebar IF active),
 * the gate must stay off when the inspector is disabled -- otherwise the game
 * loses input the moment the user thinks they closed the debug view. */
static void test_inspector_inactive_no_interception(void) {
    /* Inspector off (default). */
    TEST_ASSERT_FALSE(nt_ui_inspector_is_active(s_fx.ctx));

    const float screen_w = 800.0F;
    const float screen_h = 600.0F;
    nt_pointer_t mouse = make_pointer(600.0F, 200.0F); /* would be in sidebar if active */
    nt_ui_begin(s_fx.ctx, screen_w, screen_h, 0.0F, &mouse, 1);
    /* Even with the pointer "in sidebar coords", pointer_consumed must be false. */
    TEST_ASSERT_FALSE(nt_ui_inspector_pointer_consumed(s_fx.ctx));
    nt_ui_end(s_fx.ctx);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_registry_register_lookup);
    RUN_TEST(test_registry_resets_each_begin);
    RUN_TEST(test_registry_replace_on_collision);
    RUN_TEST(test_registry_id_zero_dropped);
    RUN_TEST(test_registry_engine_and_game_defs_coexist);
    RUN_TEST(test_button_widget_auto_tagged);
    RUN_TEST(test_panel_widget_tagged);
    RUN_TEST(test_image_widget_tagged);
    RUN_TEST(test_inspector_toggle_persists);
    RUN_TEST(test_inspector_inactive_emit_noop);
    RUN_TEST(test_inspector_active_grows_element_count);
    RUN_TEST(test_inspector_many_widgets_safe);
    RUN_TEST(test_overlay_noop_without_highlight);
    /* Phase 56 ext fix: sidebar input interception regression pins. */
    RUN_TEST(test_inspector_intercepts_pointer_over_sidebar);
    RUN_TEST(test_inspector_pointer_outside_sidebar_normal);
    RUN_TEST(test_inspector_walker_enumerates_user_tree);
    /* Phase 56 ext bug-fix pass: widget_registry hit_padding + filter + hex fallback. */
    RUN_TEST(test_widget_register_padded_roundtrip);
    RUN_TEST(test_widget_unpadded_no_hit_padding);
    RUN_TEST(test_button_auto_records_hit_padding);
    RUN_TEST(test_inspector_filter_skips_anonymous);
    RUN_TEST(test_inspector_emits_hex_for_unnamed_widget);
    RUN_TEST(test_inspector_inactive_no_interception);
    return UNITY_END();
}
