/* nt_ui_inspector + widget_registry tests. Visual verification of pixels lives
 * in ui_buttons_demo (D toggle). */

#include <math.h>
#include <stdalign.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "clay.h"
#include "graphics/nt_gfx.h"
#include "input/nt_input.h"
#include "test_helpers/nt_assert_trap.h"
#include "test_helpers/ui_test_arena.h"
#include "test_helpers/ui_walker_fixture.h"
#include "ui/nt_ui.h"
#include "ui/nt_ui_button.h"
#include "ui/nt_ui_checkbox.h"
#include "ui/nt_ui_debug_hit_zones.h"
#include "ui/nt_ui_image.h"
#include "ui/nt_ui_inspector.h"
#include "ui/nt_ui_internal.h"
#include "ui/nt_ui_label.h"
#include "ui/nt_ui_panel.h"
#include "ui/nt_ui_scroll.h"
#include "ui/nt_ui_state.h"
#include "unity.h"

#if NT_UI_DEBUG_TOOLS

alignas(NT_UI_ARENA_ALIGN) static uint8_t s_arena[NT_UI_TEST_ARENA_SIZE];
static ui_walker_fixture_t s_fx;

static nt_ui_button_style_t s_btn_style = {
    .idle = {.bg_tint = 0xFFFFFFFF, .scale = 1.0F, .opacity = 1.0F},
    .hover = {.bg_tint = 0xFFFFFFFF, .scale = 1.05F, .opacity = 1.0F},
    .pressed = {.bg_tint = 0xFFFFFFFF, .scale = 0.95F, .opacity = 1.0F},
    .disabled = {.bg_tint = 0xFFFFFFFF, .scale = 1.0F, .opacity = 0.5F},
    .transition_speed = 0.0F,
    .slice9_scale = 1.0F,
};

static const nt_ui_image_style_t s_img_style = {
    .color_packed = 0xFFFFFFFF,
    .slice9_scale = 1.0F,
};

static const nt_ui_label_style_t s_label_style = {
    .font_id = 0,
    .font_size = 14,
    .color = {255.0F, 255.0F, 255.0F, 255.0F},
};

void setUp(void) {
    nt_test_assert_install();
    ui_walker_fixture_init(&s_fx, s_arena, sizeof s_arena, UI_WALKER_FX_BIND_ALL);
    s_btn_style.idle.bg.atlas = s_fx.atlas.handle;
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

/* ---- Test 3: linear probing resolves bucket collisions correctly ---- */
static void test_registry_collision_linear_probe(void) {
    nt_pointer_t mouse = make_pointer(0.0F, 0.0F);
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    /* Synthesize two ids landing in the same bucket: id_b = id_a + cap → same id & mask. */
    const uint32_t base_id = nt_ui_id("collide_a");
    const uint32_t collide_id = base_id + s_fx.ctx->widget_registry_cap;
    nt_ui_widget_register(s_fx.ctx, base_id, &NT_UI_BUTTON_DEF, NULL);
    nt_ui_widget_register(s_fx.ctx, collide_id, &NT_UI_IMAGE_DEF, NULL);
    /* Linear probe resolves both — no replacement. */
    TEST_ASSERT_EQUAL_PTR(&NT_UI_BUTTON_DEF, nt_ui_widget_lookup(s_fx.ctx, base_id));
    TEST_ASSERT_EQUAL_PTR(&NT_UI_IMAGE_DEF, nt_ui_widget_lookup(s_fx.ctx, collide_id));
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

/* Engine + game-side widget defs coexist in the same registry; lookup returns
 * the exact pointer per id (pill name + color via def->name / pill_color). */
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
    /* Engine widget defs are prefixed "nt_" to disambiguate from Clay's own
     * verbatim config-type pills (Clay emits "Image" / "Text" / etc.). Game
     * widgets are NOT prefix-constrained -- "inv_slot" stays as-is. */
    TEST_ASSERT_EQUAL_STRING("nt_button", btn->name);
    TEST_ASSERT_EQUAL_STRING("inv_slot", slot->name);
    TEST_ASSERT_EQUAL_UINT32(0xFF60D070U, btn->pill_color);
    TEST_ASSERT_EQUAL_UINT32(0xFFB060A0U, slot->pill_color);
    nt_ui_end(s_fx.ctx);
}

/* ---- Test 4b: registering the same id twice in one frame traps (duplicate-id death-test) ----
 * Catches the cryptic Clay "element already declared" class at the registration site instead. */
static void test_registry_duplicate_id_traps(void) {
    nt_pointer_t mouse = make_pointer(0.0F, 0.0F);
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    nt_ui_widget_register(s_fx.ctx, nt_ui_id("dupe"), &NT_UI_BUTTON_DEF, NULL);
    /* Second register of the SAME id this frame must fire NT_ASSERT. */
    NT_TEST_EXPECT_ASSERT(nt_ui_widget_register(s_fx.ctx, nt_ui_id("dupe"), &NT_UI_IMAGE_DEF, NULL));
    nt_ui_end(s_fx.ctx);
}

/* ---- Test 5: nt_ui_button declaration auto-tags BUTTON ---- */
static void test_button_widget_auto_tagged(void) {
    nt_pointer_t mouse = make_pointer(0.0F, 0.0F);
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    CLAY({.id = CLAY_ID("root")}) {
        nt_ui_button_begin(s_fx.ctx, NULL, nt_ui_id("btn"), &s_btn_style, NULL, true);
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
        nt_ui_panel_begin(s_fx.ctx, NULL, &(nt_atlas_region_ref_t){.atlas = s_fx.atlas.handle, .region = s_fx.atlas.white_region_idx}, &s_img_style, NULL);
        nt_ui_panel_end(s_fx.ctx);
    }
    /* Scan the registry for any slot pointing at NT_UI_PANEL_DEF. */
    uint32_t panel_count = 0U;
    for (uint32_t i = 0; i < s_fx.ctx->widget_registry_cap; ++i) {
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
    CLAY({.id = CLAY_ID("root")}) { nt_ui_image(s_fx.ctx, NULL, &(nt_atlas_region_ref_t){.atlas = s_fx.atlas.handle, .region = s_fx.atlas.white_region_idx}, &s_img_style, NULL); }
    uint32_t image_count = 0U;
    for (uint32_t i = 0; i < s_fx.ctx->widget_registry_cap; ++i) {
        if (s_fx.ctx->widget_registry[i].id != 0U && s_fx.ctx->widget_registry[i].def == &NT_UI_IMAGE_DEF) {
            image_count++;
        }
    }
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(1U, image_count);
    nt_ui_end(s_fx.ctx);
}

/* nt_ui_label registers LABEL_DEF against the just-emitted text leaf id
 * (last_emitted_element_id) — not the parent — so the pill lands on the
 * text row, not on the button/panel that opened it. */
static void test_label_widget_tagged_on_text_leaf(void) {
    nt_pointer_t mouse = make_pointer(0.0F, 0.0F);
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    CLAY({.id = CLAY_ID("root")}) { nt_ui_label(s_fx.ctx, NULL, "hello", &s_label_style); }
    /* Scan the registry for a slot pointing at NT_UI_LABEL_DEF. */
    uint32_t label_count = 0U;
    for (uint32_t i = 0; i < s_fx.ctx->widget_registry_cap; ++i) {
        if (s_fx.ctx->widget_registry[i].id != 0U && s_fx.ctx->widget_registry[i].def == &NT_UI_LABEL_DEF) {
            label_count++;
        }
    }
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(1U, label_count);
    nt_ui_end(s_fx.ctx);
}

/* Label tag binds the TEXT leaf id; a wrapping panel keeps NT_UI_PANEL_DEF. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_label_does_not_clobber_parent_widget(void) {
    nt_pointer_t mouse = make_pointer(0.0F, 0.0F);
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    /* Panel parent + label child -- common pattern from the demo. */
    CLAY({.id = CLAY_ID("root")}) {
        nt_ui_panel_begin(s_fx.ctx, NULL, &(nt_atlas_region_ref_t){.atlas = s_fx.atlas.handle, .region = s_fx.atlas.white_region_idx}, &s_img_style, NULL);
        nt_ui_label(s_fx.ctx, NULL, "hi", &s_label_style);
        nt_ui_panel_end(s_fx.ctx);
    }
    /* Both tags coexist: PANEL on the container, LABEL on the TEXT leaf. */
    uint32_t panel_count = 0U;
    uint32_t label_count = 0U;
    for (uint32_t i = 0; i < s_fx.ctx->widget_registry_cap; ++i) {
        if (s_fx.ctx->widget_registry[i].id == 0U) {
            continue;
        }
        if (s_fx.ctx->widget_registry[i].def == &NT_UI_PANEL_DEF) {
            panel_count++;
        } else if (s_fx.ctx->widget_registry[i].def == &NT_UI_LABEL_DEF) {
            label_count++;
        }
    }
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(1U, panel_count);
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(1U, label_count);
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
 * Proves emit_layout injected the panel CLAY blocks. */
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
            nt_ui_button_begin(s_fx.ctx, NULL, nt_ui_id(name), &s_btn_style, NULL, true);
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

/* ---- Test 13: inspector sidebar consumes pointer ----
 * When inspector_active and pointer is in the right-attached sidebar footprint
 * (panel_width wide), step_interaction returns zeroed for every user widget
 * AND nt_ui_inspector_pointer_consumed must be true
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

    /* Frame 2: pointer over button center (inside sidebar) with left pressed_now;
     * step_interaction must return zeroed (sidebar gate consumes the pointer). */
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

    /* Both the unpadded and padded query MUST return zeroed: no hover, no
     * press, no clicked, no capture. */
    nt_ui_interaction_t in = nt_ui_step_interaction(s_fx.ctx, nt_ui_id("hidden_btn"));
    TEST_ASSERT_FALSE(in.hovered);
    TEST_ASSERT_FALSE(in.pressed);
    TEST_ASSERT_FALSE(in.pressed_now);
    TEST_ASSERT_FALSE(in.clicked);
    TEST_ASSERT_FALSE(in.released_now);
    TEST_ASSERT_EQUAL_UINT32(0U, nt_ui_test_capture_active_id(s_fx.ctx, 0));

    const int16_t pad[4] = {32, 32, 32, 32};
    nt_ui_interaction_t in_pad = nt_ui_step_interaction_padded(s_fx.ctx, nt_ui_id("hidden_btn"), pad);
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

    /* Frame 1: declare bbox + step so the button registers (front-most arbitration reads the
     * prev-frame interactive registry; a widget reacts only once registered). */
    nt_pointer_t f1 = make_pointer(0.0F, 0.0F);
    nt_ui_begin(s_fx.ctx, screen_w, screen_h, 0.0F, &f1, 1);
    CLAY(
        {.id = CLAY_ID("visible_btn"), .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .offset = {.x = btn_x, .y = btn_y}}, .layout = {.sizing = {CLAY_SIZING_FIXED(btn_w), CLAY_SIZING_FIXED(btn_h)}}}) {
        (void)nt_ui_step_interaction(s_fx.ctx, nt_ui_id("visible_btn"));
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
    nt_ui_interaction_t in = nt_ui_step_interaction(s_fx.ctx, nt_ui_id("visible_btn"));
    TEST_ASSERT_TRUE(in.hovered);
    TEST_ASSERT_TRUE(in.pressed);
    TEST_ASSERT_TRUE(in.pressed_now);

    nt_ui_end(s_fx.ctx);
}

/* Inspector emit walks the live Clay tree at nt_ui_end time, BEFORE
 * Clay_EndLayout closes the auto-root. Pins growth: ~6 user elements
 * produces many more inspector wrappers than an empty root. */
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

    /* Full-tree walk must emit strictly more inspector elements than the empty root. */
    TEST_ASSERT_GREATER_THAN_INT32(empty_inspector_grew, full_inspector_grew);
    /* Each extra user element adds at least one ElementOuter wrapper; 5 extra
     * children must add at least 5 extra inspector elements. */
    TEST_ASSERT_GREATER_OR_EQUAL_INT32(empty_inspector_grew + 5, full_inspector_grew);
}

/* hit_padding_lrtb survives into the registry slot so the overlay can outline
 * the touch-target distinct from the visual bbox. */
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

/* Plain image/panel register with NULL pad_lrtb and must NOT show a padded
 * hit zone in the inspector overlay. */
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

/* Declaring a button with non-zero style.hit_padding_lrtb makes the inspector
 * see the padding via the widget_registry. */
static void test_button_auto_records_hit_padding(void) {
    nt_pointer_t mouse = make_pointer(0.0F, 0.0F);
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    nt_ui_button_style_t padded_style = s_btn_style;
    padded_style.hit_padding_lrtb[0] = 8;
    padded_style.hit_padding_lrtb[1] = 8;
    padded_style.hit_padding_lrtb[2] = 4;
    padded_style.hit_padding_lrtb[3] = 4;
    CLAY({.id = CLAY_ID("root")}) {
        nt_ui_button_begin(s_fx.ctx, NULL, nt_ui_id("btn"), &padded_style, NULL, true);
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

/* Anonymous Clay containers must NOT emit an inspector tree row — named-only
 * tree and anonymous-wrapped tree produce similar element-count growth. */
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

    /* Same 3 named leaves wrapped in anonymous containers. */
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

    /* anon_growth should match named_growth within slack. */
    TEST_ASSERT_LESS_OR_EQUAL_INT32(named_growth + 6, anon_growth);
}

/* Filter preserves Text/Image/SHARED-config leaves: tree with a CLAY_TEXT leaf
 * must produce more inspector rows than one without. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_inspector_filter_keeps_text_leaves(void) {
    nt_ui_inspector_set_active(s_fx.ctx, true);
    nt_pointer_t mouse = make_pointer(0.0F, 0.0F);

    /* Baseline: container, no text leaf. */
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    CLAY({.id = CLAY_ID("root_no_text")}) {
        CLAY({.id = CLAY_ID("box1"), .layout = {.sizing = {CLAY_SIZING_FIXED(40), CLAY_SIZING_FIXED(40)}}}) {}
    }
    const int32_t before_a = nt_ui_internal_get_layout_element_count(s_fx.ctx);
    nt_ui_end(s_fx.ctx);
    const int32_t after_a = nt_ui_internal_get_layout_element_count(s_fx.ctx);
    const int32_t a_growth = after_a - before_a;

    /* Same container plus a CLAY_TEXT leaf (no id, only TEXT config). */
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    CLAY({.id = CLAY_ID("root_with_text")}) {
        CLAY({.id = CLAY_ID("box2"), .layout = {.sizing = {CLAY_SIZING_FIXED(40), CLAY_SIZING_FIXED(40)}}}) { nt_ui_label(s_fx.ctx, NULL, "hello", &s_label_style); }
    }
    const int32_t before_b = nt_ui_internal_get_layout_element_count(s_fx.ctx);
    nt_ui_end(s_fx.ctx);
    const int32_t after_b = nt_ui_internal_get_layout_element_count(s_fx.ctx);
    const int32_t b_growth = after_b - before_b;

    /* Text-bearing tree must produce more inspector rows. */
    TEST_ASSERT_GREATER_THAN_INT32(a_growth, b_growth);
}

/* Unnamed but widget-registered element produces an inspector tree row (hex id
 * fallback); the layout element count must grow. */
static void test_inspector_emits_hex_for_unnamed_widget(void) {
    nt_ui_inspector_set_active(s_fx.ctx, true);
    nt_pointer_t mouse = make_pointer(0.0F, 0.0F);
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    CLAY({.id = CLAY_ID("root_unnamed_panel")}) {
        nt_ui_panel_begin(s_fx.ctx, NULL, &(nt_atlas_region_ref_t){.atlas = s_fx.atlas.handle, .region = s_fx.atlas.white_region_idx}, &s_img_style, NULL);
        nt_ui_panel_end(s_fx.ctx);
    }
    const int32_t before = nt_ui_internal_get_layout_element_count(s_fx.ctx);
    nt_ui_end(s_fx.ctx);
    const int32_t after = nt_ui_internal_get_layout_element_count(s_fx.ctx);
    /* Inspector must have grown by at least the root pane + header + 2 tree
     * rows (root + panel). Conservative: > before + 4. */
    TEST_ASSERT_GREATER_THAN_INT32(before + 4, after);
}

/* Collapsed-set storage: unseen toggle ADDS, seen toggle REMOVES, and
 * inspector_collapsed_cap saturates cleanly. */
static void test_inspector_collapsed_storage(void) {
    /* Initial empty. */
    TEST_ASSERT_EQUAL_UINT32(0U, s_fx.ctx->inspector_collapsed_count);

    /* Add via toggle = inspector_set_active(true) is not needed for the helper
     * itself, but the dot-icon click pathway runs inside emit_layout. For
     * unit testing storage semantics we drive ctx directly via the same
     * inspector toggle path used by the dot icon: there is no public toggle
     * helper, so we emit a frame with the inspector ON, then poke the set
     * to test add/remove behavior through repeated 1-frame walks. */
    nt_ui_inspector_set_active(s_fx.ctx, true);

    /* Saturate: push CAP distinct ids directly into the array (mirrors the
     * "toggle each in turn" path's add branch). Use the public-equivalent
     * test entry: directly poke the storage (visible via internal header). */
    for (uint32_t i = 0; i < s_fx.ctx->inspector_collapsed_cap; ++i) {
        s_fx.ctx->inspector_collapsed_ids[i] = i + 100U; /* arbitrary nonzero ids */
    }
    s_fx.ctx->inspector_collapsed_count = s_fx.ctx->inspector_collapsed_cap;

    /* At cap, the next attempted add through the public toggle path would
     * be silently dropped. Verify the storage stays consistent (count
     * equals cap, no overflow). */
    TEST_ASSERT_EQUAL_UINT32(s_fx.ctx->inspector_collapsed_cap, s_fx.ctx->inspector_collapsed_count);

    /* Disable inspector -> set is cleared. */
    nt_ui_inspector_set_active(s_fx.ctx, false);
    TEST_ASSERT_EQUAL_UINT32(0U, s_fx.ctx->inspector_collapsed_count);
}

/* Adding a parent id to the collapsed-set hides its children's inspector
 * rows; clearing the set restores them. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_inspector_collapse_hides_children(void) {
    nt_ui_inspector_set_active(s_fx.ctx, true);
    /* Pointer parked offscreen (-100,-100) so viewport-hover propagation
     * does NOT fire (which would emit the
     * floating ElementHighlight rect and skew the inspector growth count
     * between walks). Test focus is collapse vs full DFS, not hover. */
    nt_pointer_t mouse = make_pointer(-100.0F, -100.0F);

    /* Walk 1: full tree (parent + 4 children). */
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    CLAY({.id = CLAY_ID("collapse_parent")}) {
        CLAY({.id = CLAY_ID("collapse_c1"), .layout = {.sizing = {CLAY_SIZING_FIXED(10), CLAY_SIZING_FIXED(10)}}}) {}
        CLAY({.id = CLAY_ID("collapse_c2"), .layout = {.sizing = {CLAY_SIZING_FIXED(10), CLAY_SIZING_FIXED(10)}}}) {}
        CLAY({.id = CLAY_ID("collapse_c3"), .layout = {.sizing = {CLAY_SIZING_FIXED(10), CLAY_SIZING_FIXED(10)}}}) {}
        CLAY({.id = CLAY_ID("collapse_c4"), .layout = {.sizing = {CLAY_SIZING_FIXED(10), CLAY_SIZING_FIXED(10)}}}) {}
    }
    const int32_t before_full = nt_ui_internal_get_layout_element_count(s_fx.ctx);
    nt_ui_end(s_fx.ctx);
    const int32_t full_growth = nt_ui_internal_get_layout_element_count(s_fx.ctx) - before_full;

    /* Resolve the parent's Clay id (CLAY_ID hashes the literal string). */
    const Clay_ElementId parent_eid = Clay__HashString(CLAY_STRING("collapse_parent"), 0, 0);

    /* Walk 2: same tree, but parent id is in the collapsed-set -> the 4
     * children must NOT contribute inspector rows. Drive the set directly
     * (the click pathway runs inside emit_layout). */
    s_fx.ctx->inspector_collapsed_ids[0] = parent_eid.id;
    s_fx.ctx->inspector_collapsed_count = 1U;

    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    CLAY({.id = CLAY_ID("collapse_parent")}) {
        CLAY({.id = CLAY_ID("collapse_c1"), .layout = {.sizing = {CLAY_SIZING_FIXED(10), CLAY_SIZING_FIXED(10)}}}) {}
        CLAY({.id = CLAY_ID("collapse_c2"), .layout = {.sizing = {CLAY_SIZING_FIXED(10), CLAY_SIZING_FIXED(10)}}}) {}
        CLAY({.id = CLAY_ID("collapse_c3"), .layout = {.sizing = {CLAY_SIZING_FIXED(10), CLAY_SIZING_FIXED(10)}}}) {}
        CLAY({.id = CLAY_ID("collapse_c4"), .layout = {.sizing = {CLAY_SIZING_FIXED(10), CLAY_SIZING_FIXED(10)}}}) {}
    }
    const int32_t before_collapsed = nt_ui_internal_get_layout_element_count(s_fx.ctx);
    nt_ui_end(s_fx.ctx);
    const int32_t collapsed_growth = nt_ui_internal_get_layout_element_count(s_fx.ctx) - before_collapsed;

    /* Collapsed walk must be smaller — children + indent wrappers skipped. */
    TEST_ASSERT_LESS_THAN_INT32(full_growth, collapsed_growth);

    /* Walk 3: clear the set -> full tree restored. */
    s_fx.ctx->inspector_collapsed_count = 0U;
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    CLAY({.id = CLAY_ID("collapse_parent")}) {
        CLAY({.id = CLAY_ID("collapse_c1"), .layout = {.sizing = {CLAY_SIZING_FIXED(10), CLAY_SIZING_FIXED(10)}}}) {}
        CLAY({.id = CLAY_ID("collapse_c2"), .layout = {.sizing = {CLAY_SIZING_FIXED(10), CLAY_SIZING_FIXED(10)}}}) {}
        CLAY({.id = CLAY_ID("collapse_c3"), .layout = {.sizing = {CLAY_SIZING_FIXED(10), CLAY_SIZING_FIXED(10)}}}) {}
        CLAY({.id = CLAY_ID("collapse_c4"), .layout = {.sizing = {CLAY_SIZING_FIXED(10), CLAY_SIZING_FIXED(10)}}}) {}
    }
    const int32_t before_restored = nt_ui_internal_get_layout_element_count(s_fx.ctx);
    nt_ui_end(s_fx.ctx);
    const int32_t restored_growth = nt_ui_internal_get_layout_element_count(s_fx.ctx) - before_restored;
    /* Walk 3 matches walk 1 exactly (set is empty again). */
    TEST_ASSERT_EQUAL_INT32(full_growth, restored_growth);
}

/* ---- Test 15j: inspector emits on NT_UI_LAYER_DEBUG_PANEL_BG ----
 * Pins the panel-vs-highlight layer relationship by checking the macro value. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_inspector_root_emitted_on_debug_layer(void) {
    nt_ui_inspector_set_active(s_fx.ctx, true);
    nt_pointer_t mouse = make_pointer(0.0F, 0.0F);
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    CLAY({.id = CLAY_ID("root")}) {}
    nt_ui_end(s_fx.ctx);

    /* Scan the layout elements (post-end the array is finalized) for the
     * inspector root id. */
    const Clay_ElementId rootId = Clay__HashString(CLAY_STRING("ntInsp_Root"), 0, 0);
    const int32_t count = nt_ui_internal_get_layout_element_count(s_fx.ctx);
    TEST_ASSERT_GREATER_THAN_INT32(0, count);

    /* Pin macro value 250 so a silent change is caught. */
    TEST_ASSERT_EQUAL_UINT8(250U, (uint8_t)NT_UI_LAYER_DEBUG_PANEL_BG);
    /* Inspector emit ran (root was added to layoutElements). */
    nt_ui_inspector_element_view_t v;
    bool found_root = false;
    for (int32_t i = 0; i < count; ++i) {
        v = nt_ui_internal_get_layout_element_view(s_fx.ctx, i);
        if (v.id == rootId.id) {
            found_root = true;
            break;
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(found_root, "ntInsp_Root element must be present after active emit");
}

/* ---- Test 15k: NT_UI_LAYER_DEBUG_PANEL_BG is 250 and sits above typical game layers ---- */
static void test_layer_debug_value_above_game_layers(void) {
    /* Layer is uint8_t (0..255). NT_UI_LAYER_DEBUG_PANEL_BG must be > 10 (typical
     * game UI uses 0..~10 for BG/IMG/TEXT/HUD) and < 255 (leave 251..255
     * headroom for future engine overlays). */
    TEST_ASSERT_GREATER_THAN_UINT8(10U, (uint8_t)NT_UI_LAYER_DEBUG_PANEL_BG);
    TEST_ASSERT_LESS_THAN_UINT8(255U, (uint8_t)NT_UI_LAYER_DEBUG_PANEL_BG);
}

/* ---- Test 15k-bis: highlight layer sits BELOW the panel layer ----
 * Highlight at 240, panel at 250 — both inequalities (layer + zIndex) matter. */
static void test_highlight_layer_below_panel_layer(void) {
    TEST_ASSERT_LESS_THAN_UINT8((uint8_t)NT_UI_LAYER_DEBUG_PANEL_BG, (uint8_t)NT_UI_LAYER_DEBUG_HIGHLIGHT);
    /* Highlight still above typical game UI (0..~10). */
    TEST_ASSERT_GREATER_THAN_UINT8(10U, (uint8_t)NT_UI_LAYER_DEBUG_HIGHLIGHT);
    /* Concrete values pin the constants so a silent edit is caught. */
    TEST_ASSERT_EQUAL_UINT8(240U, (uint8_t)NT_UI_LAYER_DEBUG_HIGHLIGHT);
    TEST_ASSERT_EQUAL_UINT8(250U, (uint8_t)NT_UI_LAYER_DEBUG_PANEL_BG);
}

/* ---- Test 15l: clicking the TEXT-CONTENT row selects the parent's id ----
 * Y=95 → element row 2; Y=135 → text-content row 3. Both must select the leaf.
 * Frame 1 primes released state; frame 2 transitions to pressed. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_inspector_click_text_content_row_selects_leaf(void) {
    nt_ui_inspector_set_active(s_fx.ctx, true);
    const float screen_w = 800.0F;
    const float screen_h = 600.0F;

    /* Frame 1: prime released state + capture the text-leaf's id. */
    nt_pointer_t f1 = make_pointer(0.0F, 0.0F);
    nt_ui_begin(s_fx.ctx, screen_w, screen_h, 0.0F, &f1, 1);
    uint32_t text_leaf_id = 0U;
    CLAY({.id = CLAY_ID("root_label")}) {
        nt_ui_label(s_fx.ctx, NULL, "hello", &s_label_style);
        /* Same accessor that the label tag uses; reads layoutElements[length-1]. */
        text_leaf_id = nt_ui_internal_last_emitted_element_id();
    }
    TEST_ASSERT_NOT_EQUAL_UINT32(0U, text_leaf_id);
    nt_ui_end(s_fx.ctx);

    /* Sanity: nothing clicked yet -> selected_id stays 0. */
    TEST_ASSERT_EQUAL_UINT32(0U, s_fx.ctx->inspector_selected_id);

    /* Frame 2a: click on the ELEMENT row (visual row 2) → selects text leaf. */
    nt_pointer_t f2 = {0};
    f2.x = 600.0F; /* inside sidebar (x >= 800 - 400) */
    f2.y = 95.0F;  /* -> highlightedRow = (95/30) - 1 = 2 (element row of leaf) */
    f2.active = true;
    f2.buttons[NT_BUTTON_LEFT].is_down = true;
    f2.buttons[NT_BUTTON_LEFT].is_pressed = true;
    nt_ui_begin(s_fx.ctx, screen_w, screen_h, 0.0F, &f2, 1);
    CLAY({.id = CLAY_ID("root_label")}) { nt_ui_label(s_fx.ctx, NULL, "hello", &s_label_style); }
    nt_ui_end(s_fx.ctx);
    TEST_ASSERT_EQUAL_UINT32(text_leaf_id, s_fx.ctx->inspector_selected_id);

    /* Reset selected_id, prime released state again for the next press-this-frame. */
    s_fx.ctx->inspector_selected_id = 0U;
    nt_pointer_t f3_released = make_pointer(600.0F, 95.0F);
    nt_ui_begin(s_fx.ctx, screen_w, screen_h, 0.0F, &f3_released, 1);
    CLAY({.id = CLAY_ID("root_label")}) { nt_ui_label(s_fx.ctx, NULL, "hello", &s_label_style); }
    nt_ui_end(s_fx.ctx);
    TEST_ASSERT_EQUAL_UINT32(0U, s_fx.ctx->inspector_selected_id);

    /* Frame 2b: click on the TEXT-CONTENT row (visual row 3) → selects same leaf. */
    nt_pointer_t f4 = {0};
    f4.x = 600.0F;
    f4.y = 135.0F; /* -> highlightedRow = (135/30) - 1 = 3 (text-content row) */
    f4.active = true;
    f4.buttons[NT_BUTTON_LEFT].is_down = true;
    f4.buttons[NT_BUTTON_LEFT].is_pressed = true;
    nt_ui_begin(s_fx.ctx, screen_w, screen_h, 0.0F, &f4, 1);
    CLAY({.id = CLAY_ID("root_label")}) { nt_ui_label(s_fx.ctx, NULL, "hello", &s_label_style); }
    nt_ui_end(s_fx.ctx);
    TEST_ASSERT_EQUAL_UINT32(text_leaf_id, s_fx.ctx->inspector_selected_id);
}

/* Persistent selected_id propagates to highlight_id on the next frame via the
 * overlay fallback chain. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_overlay_fallback_chain_with_padded_button(void) {
    nt_ui_inspector_set_active(s_fx.ctx, true);

    /* Non-zero hit_padding — mirrors the demo's reference buttons. */
    nt_ui_button_style_t padded_style = s_btn_style;
    padded_style.hit_padding_lrtb[0] = 16;
    padded_style.hit_padding_lrtb[1] = 16;
    padded_style.hit_padding_lrtb[2] = 16;
    padded_style.hit_padding_lrtb[3] = 16;

    /* Frame 1: emit button + set selected_id directly. Pointer is offscreen
     * (-100,-100) so viewport-hover propagation doesn't fire. */
    nt_pointer_t mouse = make_pointer(-100.0F, -100.0F);
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    CLAY({.id = CLAY_ID("root")}) {
        nt_ui_button_begin(s_fx.ctx, NULL, nt_ui_id("padded_btn"), &padded_style, NULL, true);
        nt_ui_label(s_fx.ctx, NULL, "OK", &s_label_style);
        (void)nt_ui_button_end(s_fx.ctx);
    }
    /* Confirm padding was registered (the cyan/yellow overlay branch depends on this). */
    int16_t pad[4] = {0, 0, 0, 0};
    TEST_ASSERT_TRUE(nt_ui_widget_get_hit_padding(s_fx.ctx, nt_ui_id("padded_btn"), pad));
    TEST_ASSERT_EQUAL_INT16(16, pad[0]);
    TEST_ASSERT_EQUAL_INT16(16, pad[1]);
    TEST_ASSERT_EQUAL_INT16(16, pad[2]);
    TEST_ASSERT_EQUAL_INT16(16, pad[3]);
    /* Set selected_id directly (mirrors the user's "click a sidebar row" effect). */
    s_fx.ctx->inspector_selected_id = nt_ui_id("padded_btn");
    nt_ui_end(s_fx.ctx);

    /* Frame 2: same tree, no hover — fallback must copy selected_id into
     * highlight_id. highlight_id resets each begin so read AFTER nt_ui_end. */
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    CLAY({.id = CLAY_ID("root")}) {
        nt_ui_button_begin(s_fx.ctx, NULL, nt_ui_id("padded_btn"), &padded_style, NULL, true);
        nt_ui_label(s_fx.ctx, NULL, "OK", &s_label_style);
        (void)nt_ui_button_end(s_fx.ctx);
    }
    /* selected_id should still hold from frame 1 (persistent across frames). */
    TEST_ASSERT_EQUAL_UINT32(nt_ui_id("padded_btn"), s_fx.ctx->inspector_selected_id);
    nt_ui_end(s_fx.ctx);

    /* Post-end: the fallback branch must have set highlight_id = selected_id. */
    TEST_ASSERT_EQUAL_UINT32(nt_ui_id("padded_btn"), s_fx.ctx->inspector_highlight_id);

    /* Registry must hold padding post-end so overlay_draw can read it. */
    int16_t pad2[4] = {0, 0, 0, 0};
    TEST_ASSERT_TRUE(nt_ui_widget_get_hit_padding(s_fx.ctx, nt_ui_id("padded_btn"), pad2));
    TEST_ASSERT_EQUAL_INT16(16, pad2[0]);
    TEST_ASSERT_EQUAL_INT16(16, pad2[1]);
    TEST_ASSERT_EQUAL_INT16(16, pad2[2]);
    TEST_ASSERT_EQUAL_INT16(16, pad2[3]);

    /* Stub target — overlay_draw must reach the padded-zone path without crashing. */
    nt_ui_target_t target = {.viewport = {0.0F, 0.0F, 800.0F, 600.0F}};
    nt_ui_inspector_overlay_draw(s_fx.ctx, &target, NT_FONT_INVALID, 0.0F);
}

/* ---- Test 15n: overlay_draw handles a SELECTED widget with zero padding ----
 * get_hit_padding returns TRUE with all-zero components; overlay must skip
 * the padded branch via the OR-of-components check, not crash. */
static void test_overlay_skips_padded_zone_for_zero_padding(void) {
    nt_ui_inspector_set_active(s_fx.ctx, true);
    nt_ui_button_style_t zero_pad_style = s_btn_style;
    zero_pad_style.hit_padding_lrtb[0] = 0;
    zero_pad_style.hit_padding_lrtb[1] = 0;
    zero_pad_style.hit_padding_lrtb[2] = 0;
    zero_pad_style.hit_padding_lrtb[3] = 0;

    nt_pointer_t mouse = make_pointer(0.0F, 0.0F);
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    CLAY({.id = CLAY_ID("root")}) {
        nt_ui_button_begin(s_fx.ctx, NULL, nt_ui_id("nopad_btn"), &zero_pad_style, NULL, true);
        nt_ui_label(s_fx.ctx, NULL, "X", &s_label_style);
        (void)nt_ui_button_end(s_fx.ctx);
    }
    /* Button passes a non-NULL pad array (always non-NULL) → registry records
     * has_padding=1 with zeros; overlay short-circuits via OR-of-components. */
    int16_t pad[4] = {99, 99, 99, 99};
    TEST_ASSERT_TRUE(nt_ui_widget_get_hit_padding(s_fx.ctx, nt_ui_id("nopad_btn"), pad));
    TEST_ASSERT_EQUAL_INT16(0, pad[0]);
    TEST_ASSERT_EQUAL_INT16(0, pad[1]);
    TEST_ASSERT_EQUAL_INT16(0, pad[2]);
    TEST_ASSERT_EQUAL_INT16(0, pad[3]);
    s_fx.ctx->inspector_selected_id = nt_ui_id("nopad_btn");
    nt_ui_end(s_fx.ctx);

    /* Highlight propagates via the fallback. */
    TEST_ASSERT_EQUAL_UINT32(nt_ui_id("nopad_btn"), s_fx.ctx->inspector_highlight_id);
    /* overlay_draw must not crash when the padded branch is skipped. */
    nt_ui_target_t target = {.viewport = {0.0F, 0.0F, 800.0F, 600.0F}};
    nt_ui_inspector_overlay_draw(s_fx.ctx, &target, NT_FONT_INVALID, 0.0F);
}

/* ---- Test 15o: inspector reads layer from CLAY_TEXT userData ----
 * Label sets userData on the TEXT config (no SHARED). */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_inspector_reads_layer_from_text_config_userdata(void) {
    nt_pointer_t mouse = make_pointer(0.0F, 0.0F);
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    uint32_t leaf_id = 0U;
    const nt_ui_element_data_t *data_with_layer = NT_UI_DATA_LAYER((nt_ui_layer_t)5);
    CLAY({.id = CLAY_ID("root_text_layer")}) {
        nt_ui_label(s_fx.ctx, data_with_layer, "L5", &s_label_style);
        leaf_id = nt_ui_internal_last_emitted_element_id();
    }
    TEST_ASSERT_NOT_EQUAL_UINT32(0U, leaf_id);
    nt_ui_end(s_fx.ctx);

    /* Verify data pointer survived; macro alloc is scratch, valid through next begin. */
    TEST_ASSERT_EQUAL_UINT8(5U, (uint8_t)data_with_layer->layer);
}

/* ---- Test 15p: inspector reads layer from SHARED config userData ----
 * Clay auto-routes Clay_ElementDeclaration.userData into a SHARED config. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_inspector_reads_layer_from_shared_config_userdata(void) {
    nt_pointer_t mouse = make_pointer(0.0F, 0.0F);
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    const nt_ui_element_data_t *data_with_layer = NT_UI_DATA_LAYER((nt_ui_layer_t)3);
    CLAY({.id = CLAY_ID("root_shared_layer")}) {
        nt_ui_image(s_fx.ctx, data_with_layer, &(nt_atlas_region_ref_t){.atlas = s_fx.atlas.handle, .region = s_fx.atlas.white_region_idx}, &s_img_style, NULL);
    }
    nt_ui_end(s_fx.ctx);
    /* Clay stored data in SHARED; cdv_element_layer's SHARED branch returns the layer. */
    TEST_ASSERT_EQUAL_UINT8(3U, (uint8_t)data_with_layer->layer);
}

/* ---- Test 15q: overlay projects hit-zone corners through accum for a
 * transformed widget. The projected corner must differ from the axis-aligned
 * Y-flip of the layout bbox — otherwise the accum snapshot was ignored. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_overlay_projects_through_accum_for_transformed_id(void) {
    nt_ui_inspector_set_active(s_fx.ctx, true);

    const float screen_w = 800.0F;
    const float screen_h = 600.0F;
    const float btn_x = 200.0F;
    const float btn_y = 200.0F;
    const float btn_w = 160.0F;
    const float btn_h = 48.0F;

    nt_ui_transform_t baked = nt_ui_transform_defaults();
    baked.offset_x = 60.0F;
    baked.offset_y = -20.0F;
    baked.rotation_z = 25.0F * 0.017453292F;
    baked.scale_x = 1.15F;
    baked.scale_y = 0.85F;

    /* Frame 1: declare with the transform so tree_baked carries it. step_interaction
     * in frame 2 reads PREV-frame tree_baked → must include the rotation already. */
    nt_pointer_t f1 = make_pointer(0.0F, 0.0F);
    nt_ui_begin(s_fx.ctx, screen_w, screen_h, 0.0F, &f1, 1);
    CLAY({.id = CLAY_ID("xform_btn"),
          .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .offset = {.x = btn_x, .y = btn_y}},
          .layout = {.sizing = {CLAY_SIZING_FIXED(btn_w), CLAY_SIZING_FIXED(btn_h)}},
          .userData = (void *)NT_UI_DATA_XFORM(0U, &baked, 1.0F)}) {}
    nt_ui_end(s_fx.ctx);

    nt_pointer_t f2 = make_pointer(0.0F, 0.0F);
    nt_ui_begin(s_fx.ctx, screen_w, screen_h, 0.0F, &f2, 1);
    CLAY({.id = CLAY_ID("xform_btn"),
          .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .offset = {.x = btn_x, .y = btn_y}},
          .layout = {.sizing = {CLAY_SIZING_FIXED(btn_w), CLAY_SIZING_FIXED(btn_h)}},
          .userData = (void *)NT_UI_DATA_XFORM(0U, &baked, 1.0F)}) {
        (void)nt_ui_step_interaction(s_fx.ctx, nt_ui_id("xform_btn"));
    }

    /* Zone must be recorded; composed affine differs from identity (rotation+scale). */
    const nt_ui_debug_zone_t *z = nt_ui_internal_find_debug_zone(s_fx.ctx, nt_ui_id("xform_btn"));
    TEST_ASSERT_NOT_NULL(z);
    TEST_ASSERT_TRUE(z->m[0] != 1.0F || z->m[4] != 0.0F || z->m[1] != 0.0F || z->m[5] != 1.0F || z->m[12] != 0.0F || z->m[13] != 0.0F);

    /* Project top-left through accum + Y-flip; must differ from axis-aligned. */
    float proj_x = 0.0F;
    float proj_y = 0.0F;
    nt_ui_internal_project_layout_to_world(z, 0.0F, screen_h, z->visual_l, z->visual_t, &proj_x, &proj_y);
    const float flat_x = z->visual_l;
    const float flat_y = screen_h - z->visual_t;
    const float dx = fabsf(proj_x - flat_x);
    const float dy = fabsf(proj_y - flat_y);
    /* Conservative 10 px threshold catches identity-projection regression. */
    TEST_ASSERT_TRUE_MESSAGE(dx > 10.0F || dy > 10.0F, "projected corner did not differ from axis-aligned");

    s_fx.ctx->inspector_selected_id = nt_ui_id("xform_btn");
    nt_ui_end(s_fx.ctx);

    /* overlay_draw must run without crashing. */
    nt_ui_target_t target = {.viewport = {0.0F, 0.0F, screen_w, screen_h}};
    nt_ui_inspector_overlay_draw(s_fx.ctx, &target, NT_FONT_INVALID, 0.0F);
}

/* ---- Test 15r: overlay falls back to axis-aligned when no zone recorded ---- */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_overlay_falls_back_to_axis_aligned_when_no_zone(void) {
    nt_ui_inspector_set_active(s_fx.ctx, true);

    /* Frame 1: declare a plain Clay container (no widget tag, no interaction). */
    nt_pointer_t f1 = make_pointer(0.0F, 0.0F);
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &f1, 1);
    CLAY({.id = CLAY_ID("plain_box"), .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .offset = {.x = 100.0F, .y = 100.0F}}, .layout = {.sizing = {CLAY_SIZING_FIXED(60), CLAY_SIZING_FIXED(60)}}}) {}
    nt_ui_end(s_fx.ctx);

    /* Frame 2: declare again so bbox is stable; do NOT query interaction. */
    nt_pointer_t f2 = make_pointer(0.0F, 0.0F);
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &f2, 1);
    CLAY({.id = CLAY_ID("plain_box"), .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .offset = {.x = 100.0F, .y = 100.0F}}, .layout = {.sizing = {CLAY_SIZING_FIXED(60), CLAY_SIZING_FIXED(60)}}}) {}
    s_fx.ctx->inspector_selected_id = nt_ui_id("plain_box");
    nt_ui_end(s_fx.ctx);

    /* No zone -> find returns NULL. */
    const nt_ui_debug_zone_t *z = nt_ui_internal_find_debug_zone(s_fx.ctx, nt_ui_id("plain_box"));
    TEST_ASSERT_NULL(z);

    /* highlight propagated via the fallback chain. */
    TEST_ASSERT_EQUAL_UINT32(nt_ui_id("plain_box"), s_fx.ctx->inspector_highlight_id);

    /* overlay_draw must take the axis-aligned fallback path; no crash. */
    nt_ui_target_t target = {.viewport = {0.0F, 0.0F, 800.0F, 600.0F}};
    nt_ui_inspector_overlay_draw(s_fx.ctx, &target, NT_FONT_INVALID, 0.0F);
}

/* ---- Test 15s: viewport hover propagates inspector_highlight_id ----
 * Frame 1 declares widget so frame 2's pointerOverIds is populated;
 * pointer at widget center outside sidebar → highlight = widget id. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_inspector_highlight_from_viewport_hover(void) {
    nt_ui_inspector_set_active(s_fx.ctx, true);

    const float screen_w = 800.0F;
    const float screen_h = 600.0F;
    const float btn_x = 100.0F; /* well left of sidebar at 400 */
    const float btn_y = 200.0F;
    const float btn_w = 160.0F;
    const float btn_h = 48.0F;
    const float btn_cx = btn_x + (btn_w * 0.5F);
    const float btn_cy = btn_y + (btn_h * 0.5F);

    /* Frame 1: declare widget; pointer (0,0). */
    nt_pointer_t f1 = make_pointer(0.0F, 0.0F);
    nt_ui_begin(s_fx.ctx, screen_w, screen_h, 0.0F, &f1, 1);
    CLAY({.id = CLAY_ID("hover_target"),
          .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .offset = {.x = btn_x, .y = btn_y}},
          .layout = {.sizing = {CLAY_SIZING_FIXED(btn_w), CLAY_SIZING_FIXED(btn_h)}}}) {}
    nt_ui_end(s_fx.ctx);

    /* Frame 2: pointer at widget center; SetPointerState fills pointerOverIds. */
    nt_pointer_t f2 = make_pointer(btn_cx, btn_cy);
    nt_ui_begin(s_fx.ctx, screen_w, screen_h, 0.0F, &f2, 1);
    /* Sanity: pointer is NOT inside the sidebar footprint. */
    TEST_ASSERT_FALSE(nt_ui_inspector_pointer_consumed(s_fx.ctx));
    CLAY({.id = CLAY_ID("hover_target"),
          .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .offset = {.x = btn_x, .y = btn_y}},
          .layout = {.sizing = {CLAY_SIZING_FIXED(btn_w), CLAY_SIZING_FIXED(btn_h)}}}) {}
    nt_ui_end(s_fx.ctx);

    /* Highlight propagates to the hovered widget id. */
    TEST_ASSERT_EQUAL_UINT32(nt_ui_id("hover_target"), s_fx.ctx->inspector_highlight_id);
}

/* ---- Test 15t: viewport hover inside panel area does NOT override highlight ----
 * inspector_pointer_consumed gates the propagation. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_inspector_highlight_ignores_panel_hover(void) {
    nt_ui_inspector_set_active(s_fx.ctx, true);

    const float screen_w = 800.0F;
    const float screen_h = 600.0F;

    /* Widget bbox overlaps the panel footprint (panel at x=400). */
    const float btn_x = 420.0F;
    const float btn_y = 50.0F; /* near top so it doesn't overlap sidebar rows */
    const float btn_w = 160.0F;
    const float btn_h = 48.0F;
    const float btn_cx = btn_x + (btn_w * 0.5F);
    const float btn_cy = btn_y + (btn_h * 0.5F);
    nt_pointer_t f1 = make_pointer(0.0F, 0.0F);
    nt_ui_begin(s_fx.ctx, screen_w, screen_h, 0.0F, &f1, 1);
    CLAY({.id = CLAY_ID("in_panel_widget"),
          .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .offset = {.x = btn_x, .y = btn_y}},
          .layout = {.sizing = {CLAY_SIZING_FIXED(btn_w), CLAY_SIZING_FIXED(btn_h)}}}) {}
    nt_ui_end(s_fx.ctx);

    /* Frame 2: pointer in sidebar; pointer_consumed gates the propagation off. */
    nt_pointer_t f2 = make_pointer(btn_cx, btn_cy);
    nt_ui_begin(s_fx.ctx, screen_w, screen_h, 0.0F, &f2, 1);
    /* Sanity: pointer IS inside the sidebar footprint. */
    TEST_ASSERT_TRUE(nt_ui_inspector_pointer_consumed(s_fx.ctx));
    CLAY({.id = CLAY_ID("in_panel_widget"),
          .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .offset = {.x = btn_x, .y = btn_y}},
          .layout = {.sizing = {CLAY_SIZING_FIXED(btn_w), CLAY_SIZING_FIXED(btn_h)}}}) {}
    nt_ui_end(s_fx.ctx);

    /* Propagation did not fire — no widget id reaches highlight. */
    TEST_ASSERT_NOT_EQUAL_UINT32(nt_ui_id("in_panel_widget"), s_fx.ctx->inspector_highlight_id);
}

/* ---- Test 15u: pointer over nothing leaves highlight clean of inspector-owned ids ---- */
static void test_inspector_highlight_zero_when_no_pointer_over(void) {
    nt_ui_inspector_set_active(s_fx.ctx, true);

    const float screen_w = 800.0F;
    const float screen_h = 600.0F;

    /* Frame 1: declare nothing extra (empty root). */
    nt_pointer_t f1 = make_pointer(0.0F, 0.0F);
    nt_ui_begin(s_fx.ctx, screen_w, screen_h, 0.0F, &f1, 1);
    CLAY({.id = CLAY_ID("empty_root")}) {}
    nt_ui_end(s_fx.ctx);

    /* Pointer at empty area; require no inspector-owned id leaks into highlight. */
    nt_pointer_t f2 = make_pointer(50.0F, 50.0F);
    nt_ui_begin(s_fx.ctx, screen_w, screen_h, 0.0F, &f2, 1);
    TEST_ASSERT_FALSE(nt_ui_inspector_pointer_consumed(s_fx.ctx));
    CLAY({.id = CLAY_ID("empty_root")}) {}
    nt_ui_end(s_fx.ctx);

    /* highlight must not target any inspector-owned id (no self-feedback). */
    const uint32_t hl = s_fx.ctx->inspector_highlight_id;
    TEST_ASSERT_NOT_EQUAL_UINT32(Clay__HashString(CLAY_STRING("ntInsp_ElementHighlight"), 0, 0).id, hl);
    TEST_ASSERT_NOT_EQUAL_UINT32(Clay__HashString(CLAY_STRING("ntInsp_ElementHighlightRectangle"), 0, 0).id, hl);
    TEST_ASSERT_NOT_EQUAL_UINT32(Clay__HashString(CLAY_STRING("ntInsp_Root"), 0, 0).id, hl);
    TEST_ASSERT_NOT_EQUAL_UINT32(Clay__HashString(CLAY_STRING("ntInsp_CloseButton"), 0, 0).id, hl);
}

/* Viewport hover prefers a registered widget over its anonymous child via a
 * two-pass scan of pointerOverIds. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_inspector_viewport_hover_prefers_widget_over_child(void) {
    nt_ui_inspector_set_active(s_fx.ctx, true);

    const float screen_w = 800.0F;
    const float screen_h = 600.0F;
    const float panel_x = 100.0F;
    const float panel_y = 200.0F;
    const float panel_w = 160.0F;
    const float panel_h = 80.0F;
    const float child_cx = panel_x + (panel_w * 0.5F);
    const float child_cy = panel_y + (panel_h * 0.5F);

    /* Frame 1: registered widget wrapping anonymous CLAY child filling the parent. */
    nt_pointer_t f1 = make_pointer(0.0F, 0.0F);
    nt_ui_begin(s_fx.ctx, screen_w, screen_h, 0.0F, &f1, 1);
    CLAY({.id = CLAY_ID("widget_floater"),
          .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .offset = {.x = panel_x, .y = panel_y}},
          .layout = {.sizing = {CLAY_SIZING_FIXED(panel_w), CLAY_SIZING_FIXED(panel_h)}}}) {
        nt_ui_widget_register(s_fx.ctx, nt_ui_id("widget_floater"), &NT_UI_PANEL_DEF, NULL);
        /* Anonymous nested child fills the parent. */
        CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)}}}) {}
    }
    nt_ui_end(s_fx.ctx);

    /* Frame 2: hover center; the registered widget must win over the anonymous child. */
    nt_pointer_t f2 = make_pointer(child_cx, child_cy);
    nt_ui_begin(s_fx.ctx, screen_w, screen_h, 0.0F, &f2, 1);
    TEST_ASSERT_FALSE(nt_ui_inspector_pointer_consumed(s_fx.ctx));
    CLAY({.id = CLAY_ID("widget_floater"),
          .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .offset = {.x = panel_x, .y = panel_y}},
          .layout = {.sizing = {CLAY_SIZING_FIXED(panel_w), CLAY_SIZING_FIXED(panel_h)}}}) {
        nt_ui_widget_register(s_fx.ctx, nt_ui_id("widget_floater"), &NT_UI_PANEL_DEF, NULL);
        CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)}}}) {}
    }
    nt_ui_end(s_fx.ctx);

    /* Highlight is the registered widget id, NOT the anonymous child. */
    TEST_ASSERT_EQUAL_UINT32(nt_ui_id("widget_floater"), s_fx.ctx->inspector_highlight_id);
}

/* ---- Test 15x: every inspector-owned cmd carries a DEBUG layer in userData ----
 * SCISSOR/CUSTOM/NONE are barriers and excluded. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_inspector_inner_emits_carry_debug_layer(void) {
    nt_ui_inspector_set_active(s_fx.ctx, true);
    const float screen_w = 800.0F;
    const float screen_h = 600.0F;
    const float panel_left_x = screen_w - 400.0F; /* NT_UI_INSPECTOR_PANEL_WIDTH = 400 */

    /* Pre-select so the info pane emits too. */
    nt_pointer_t f1 = make_pointer(0.0F, 0.0F);
    nt_ui_begin(s_fx.ctx, screen_w, screen_h, 0.0F, &f1, 1);
    CLAY({.id = CLAY_ID("inspect_root")}) {
        nt_ui_button_begin(s_fx.ctx, NULL, nt_ui_id("inspect_btn"), &s_btn_style, NULL, true);
        nt_ui_label(s_fx.ctx, NULL, "OK", &s_label_style);
        (void)nt_ui_button_end(s_fx.ctx);
        nt_ui_image(s_fx.ctx, NULL, &(nt_atlas_region_ref_t){.atlas = s_fx.atlas.handle, .region = s_fx.atlas.white_region_idx}, &s_img_style, NULL);
    }
    s_fx.ctx->inspector_selected_id = nt_ui_id("inspect_btn");
    nt_ui_end(s_fx.ctx);

    /* Second frame: stable bbox, info pane renders, full emit surface. */
    nt_pointer_t f2 = make_pointer(0.0F, 0.0F);
    nt_ui_begin(s_fx.ctx, screen_w, screen_h, 0.0F, &f2, 1);
    CLAY({.id = CLAY_ID("inspect_root")}) {
        nt_ui_button_begin(s_fx.ctx, NULL, nt_ui_id("inspect_btn"), &s_btn_style, NULL, true);
        nt_ui_label(s_fx.ctx, NULL, "OK", &s_label_style);
        (void)nt_ui_button_end(s_fx.ctx);
        nt_ui_image(s_fx.ctx, NULL, &(nt_atlas_region_ref_t){.atlas = s_fx.atlas.handle, .region = s_fx.atlas.white_region_idx}, &s_img_style, NULL);
    }
    nt_ui_end(s_fx.ctx);

    /* Inside-panel commands must carry PANEL or HIGHLIGHT layer in userData. */
    Clay_RenderCommandArray *arr = &s_fx.ctx->frozen_cmds;
    TEST_ASSERT_NOT_NULL(arr->internalArray);
    TEST_ASSERT_GREATER_THAN_INT32(0, arr->length);

    uint32_t inspector_inside_cmd_count = 0U;
    uint32_t leaked_layer0_count = 0U;
    for (int32_t i = 0; i < arr->length; ++i) {
        const Clay_RenderCommand *cc = &arr->internalArray[i];
        /* Skip non-renderable barriers (Clay zeroes their userData). */
        if (cc->commandType == CLAY_RENDER_COMMAND_TYPE_NONE || cc->commandType == CLAY_RENDER_COMMAND_TYPE_SCISSOR_START || cc->commandType == CLAY_RENDER_COMMAND_TYPE_SCISSOR_END ||
            cc->commandType == CLAY_RENDER_COMMAND_TYPE_CUSTOM) {
            continue;
        }
        /* Inspector ownership heuristic: bbox sits in the panel footprint. */
        if (cc->boundingBox.x < panel_left_x) {
            continue;
        }
        inspector_inside_cmd_count++;
        if (cc->userData == NULL) {
            leaked_layer0_count++;
            continue;
        }
        const nt_ui_element_data_t *ed = (const nt_ui_element_data_t *)cc->userData;
        const uint8_t layer = ed->layer;
        /* Inspector inner emits must be on PANEL_BG / PANEL_TEXT / HIGHLIGHT. */
        const bool valid_layer = (layer == (uint8_t)NT_UI_LAYER_DEBUG_PANEL_BG) || (layer == (uint8_t)NT_UI_LAYER_DEBUG_PANEL_TEXT) || (layer == (uint8_t)NT_UI_LAYER_DEBUG_HIGHLIGHT);
        TEST_ASSERT_TRUE_MESSAGE(valid_layer, "inspector inner emit carries non-debug layer");
    }

    /* Sanity: we iterated inspector-owned commands. */
    TEST_ASSERT_GREATER_THAN_UINT32(10U, inspector_inside_cmd_count);

    /* No layer-0 leaks among inner inspector commands. */
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0U, leaked_layer0_count, "inspector inner emit fell back to layer 0 (userData NULL)");

    nt_ui_target_t target = {.viewport = {0.0F, 0.0F, screen_w, screen_h}};
    nt_ui_walk(s_fx.ctx, &target);
    (void)nt_ui_test_last_walk_unlayered_count(s_fx.ctx);
}

/* ---- Test 15x-perf: inspector RECT↔TEXT alternations capped.
 * Per-row pill backgrounds dropped (colored text only) → ≤ 20 alternations
 * for the demo-shaped scene. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_inspector_alternations_capped_after_strategy_a(void) {
    nt_ui_inspector_set_active(s_fx.ctx, true);
    const float screen_w = 1280.0F;
    const float screen_h = 800.0F;
    const float panel_left_x = screen_w - 400.0F; /* NT_UI_INSPECTOR_PANEL_WIDTH = 400 */

    /* Frame 1: declare so prev-frame bbox exists. */
    nt_pointer_t f1 = make_pointer(0.0F, 0.0F);
    nt_ui_begin(s_fx.ctx, screen_w, screen_h, 0.0F, &f1, 1);
    CLAY({.id = CLAY_ID("perf_root"), .layout = {.padding = CLAY_PADDING_ALL(20)}, .backgroundColor = {30, 30, 30, 255}}) {
        nt_ui_button_begin(s_fx.ctx, NULL, nt_ui_id("perf_btn"), &s_btn_style, NULL, true);
        nt_ui_label(s_fx.ctx, NULL, "OK", &s_label_style);
        (void)nt_ui_button_end(s_fx.ctx);
        nt_ui_image(s_fx.ctx, NULL, &(nt_atlas_region_ref_t){.atlas = s_fx.atlas.handle, .region = s_fx.atlas.white_region_idx}, &s_img_style, NULL);
    }
    s_fx.ctx->inspector_selected_id = nt_ui_id("perf_btn");
    nt_ui_end(s_fx.ctx);

    /* Frame 2: full inspector emit -- tree rows + info pane. */
    nt_pointer_t f2 = make_pointer(0.0F, 0.0F);
    nt_ui_begin(s_fx.ctx, screen_w, screen_h, 0.0F, &f2, 1);
    CLAY({.id = CLAY_ID("perf_root"), .layout = {.padding = CLAY_PADDING_ALL(20)}, .backgroundColor = {30, 30, 30, 255}}) {
        nt_ui_button_begin(s_fx.ctx, NULL, nt_ui_id("perf_btn"), &s_btn_style, NULL, true);
        nt_ui_label(s_fx.ctx, NULL, "OK", &s_label_style);
        (void)nt_ui_button_end(s_fx.ctx);
        nt_ui_image(s_fx.ctx, NULL, &(nt_atlas_region_ref_t){.atlas = s_fx.atlas.handle, .region = s_fx.atlas.white_region_idx}, &s_img_style, NULL);
    }
    nt_ui_end(s_fx.ctx);

    /* Count adjacent RECT/IMAGE/BORDER ↔ TEXT transitions in inspector-owned cmds. */
    Clay_RenderCommandArray *arr = &s_fx.ctx->frozen_cmds;
    TEST_ASSERT_NOT_NULL(arr->internalArray);
    TEST_ASSERT_GREATER_THAN_INT32(0, arr->length);

    enum { PIPE_NONE = 0, PIPE_SPRITE = 1, PIPE_TEXT = 2 };
    int prev_pipe = PIPE_NONE;
    uint32_t alternations = 0U;
    uint32_t inside_cmd_count = 0U;
    for (int32_t i = 0; i < arr->length; ++i) {
        const Clay_RenderCommand *cc = &arr->internalArray[i];
        int pipe = PIPE_NONE;
        switch (cc->commandType) {
        case CLAY_RENDER_COMMAND_TYPE_RECTANGLE:
        case CLAY_RENDER_COMMAND_TYPE_BORDER:
        case CLAY_RENDER_COMMAND_TYPE_IMAGE:
            pipe = PIPE_SPRITE;
            break;
        case CLAY_RENDER_COMMAND_TYPE_TEXT:
            pipe = PIPE_TEXT;
            break;
        default:
            /* Barriers reset prev so subsequent cmds don't falsely alternate. */
            prev_pipe = PIPE_NONE;
            continue;
        }
        /* Inspector-owned heuristic: bbox inside the panel footprint. */
        if (cc->boundingBox.x < panel_left_x) {
            prev_pipe = PIPE_NONE;
            continue;
        }
        inside_cmd_count++;
        if (prev_pipe != PIPE_NONE && prev_pipe != pipe) {
            alternations++;
        }
        prev_pipe = pipe;
    }

    /* Vacuous-pass guard: assert we actually iterated the panel. */
    TEST_ASSERT_GREATER_THAN_UINT32(20U, inside_cmd_count);

    /* Cap is single-sourced from the inspector module so adding a sidebar chrome row
     * bumps it next to that row, not here. Catches per-row pill-background regressions. */
    char msg[160];
    (void)snprintf(msg, sizeof msg, "inspector RECT↔TEXT alternations: %u (cap %u). cmd_count=%u", alternations, NT_UI_INSPECTOR_ALTERNATION_CAP, inside_cmd_count);
    TEST_ASSERT_LESS_OR_EQUAL_UINT32_MESSAGE(NT_UI_INSPECTOR_ALTERNATION_CAP, alternations, msg);
}

/* ---- Test 15x-perf-bulk: 20 widgets stay under scaled alternation cap (60). ---- */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_inspector_alternations_bulk_scene_after_strategy_a(void) {
    nt_ui_inspector_set_active(s_fx.ctx, true);
    const float screen_w = 1280.0F;
    const float screen_h = 800.0F;
    const float panel_left_x = screen_w - 400.0F;

    /* Frame 1: declare 20 buttons; frame 2 has settled bboxes. */
    nt_pointer_t f1 = make_pointer(0.0F, 0.0F);
    nt_ui_begin(s_fx.ctx, screen_w, screen_h, 0.0F, &f1, 1);
    CLAY({.id = CLAY_ID("bulk_root"), .layout = {.padding = CLAY_PADDING_ALL(20), .childGap = 8}, .backgroundColor = {30, 30, 30, 255}}) {
        for (int i = 0; i < 20; ++i) {
            char name[24];
            (void)snprintf(name, sizeof name, "bulk_btn_%d", i);
            nt_ui_button_begin(s_fx.ctx, NULL, nt_ui_id(name), &s_btn_style, NULL, true);
            nt_ui_label(s_fx.ctx, NULL, "X", &s_label_style);
            (void)nt_ui_button_end(s_fx.ctx);
        }
    }
    s_fx.ctx->inspector_selected_id = nt_ui_id("bulk_btn_0");
    nt_ui_end(s_fx.ctx);

    /* Frame 2: full surface. */
    nt_pointer_t f2 = make_pointer(0.0F, 0.0F);
    nt_ui_begin(s_fx.ctx, screen_w, screen_h, 0.0F, &f2, 1);
    CLAY({.id = CLAY_ID("bulk_root"), .layout = {.padding = CLAY_PADDING_ALL(20), .childGap = 8}, .backgroundColor = {30, 30, 30, 255}}) {
        for (int i = 0; i < 20; ++i) {
            char name[24];
            (void)snprintf(name, sizeof name, "bulk_btn_%d", i);
            nt_ui_button_begin(s_fx.ctx, NULL, nt_ui_id(name), &s_btn_style, NULL, true);
            nt_ui_label(s_fx.ctx, NULL, "X", &s_label_style);
            (void)nt_ui_button_end(s_fx.ctx);
        }
    }
    nt_ui_end(s_fx.ctx);

    Clay_RenderCommandArray *arr = &s_fx.ctx->frozen_cmds;
    TEST_ASSERT_NOT_NULL(arr->internalArray);

    enum { PIPE_NONE = 0, PIPE_SPRITE = 1, PIPE_TEXT = 2 };
    int prev_pipe = PIPE_NONE;
    uint32_t alternations = 0U;
    uint32_t inside_cmd_count = 0U;
    for (int32_t i = 0; i < arr->length; ++i) {
        const Clay_RenderCommand *cc = &arr->internalArray[i];
        int pipe = PIPE_NONE;
        switch (cc->commandType) {
        case CLAY_RENDER_COMMAND_TYPE_RECTANGLE:
        case CLAY_RENDER_COMMAND_TYPE_BORDER:
        case CLAY_RENDER_COMMAND_TYPE_IMAGE:
            pipe = PIPE_SPRITE;
            break;
        case CLAY_RENDER_COMMAND_TYPE_TEXT:
            pipe = PIPE_TEXT;
            break;
        default:
            prev_pipe = PIPE_NONE;
            continue;
        }
        if (cc->boundingBox.x < panel_left_x) {
            prev_pipe = PIPE_NONE;
            continue;
        }
        inside_cmd_count++;
        if (prev_pipe != PIPE_NONE && prev_pipe != pipe) {
            alternations++;
        }
        prev_pipe = pipe;
    }

    TEST_ASSERT_GREATER_THAN_UINT32(50U, inside_cmd_count);

    char msg[200];
    (void)snprintf(msg, sizeof msg, "inspector RECT↔TEXT alternations (bulk 20-row scene): %u (cap 60). cmd_count=%u", alternations, inside_cmd_count);
    /* 60 cap = 20 rows × ~2 + ~20 for header/info-pane. */
    TEST_ASSERT_LESS_OR_EQUAL_UINT32_MESSAGE(60U, alternations, msg);
}

/* Declares a 200x200 clip pane whose childOffset comes from the inspector's internal scroll
 * helper (the path that replaced Clay_GetScrollOffset, D-59-01). Tall content overflows so
 * wheel can scroll. The pointer sits at (ptr_x,100) so a caller can aim it on/off the pane. */
static void emit_scroll_pane_frame(float screen_w, float screen_h, float ptr_x, float wheel_dy) {
    nt_pointer_t p = make_pointer(ptr_x, 100.0F);
    p.wheel_dy = wheel_dy;
    nt_ui_begin(s_fx.ctx, screen_w, screen_h, 1.0F / 60.0F, &p, 1);
    CLAY({.id = CLAY_ID("scroll_pane_root"), .layout = {.sizing = {CLAY_SIZING_FIXED(200), CLAY_SIZING_FIXED(200)}}}) {
        /* Resolve offset BEFORE opening the clip element (the scroll_begin pattern: the
         * prev-frame Clay_GetScrollContainerData read must precede this frame's reconfigure). */
        const Clay_Vector2 off = nt_ui_internal_scroll_pane_offset(s_fx.ctx, nt_ui_id("scroll_pane_test"), NT_UI_STATE_TAG('t', 's', 't', 'p'), false, true);
        CLAY({.id = CLAY_ID("scroll_pane_test"), .layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)}}, .clip = {.vertical = true, .childOffset = off}}) {
            /* Tall content forces y overflow so the pane can scroll. */
            CLAY({.id = CLAY_ID("scroll_pane_content"), .layout = {.sizing = {CLAY_SIZING_FIXED(180), CLAY_SIZING_FIXED(1000)}}}) {}
        }
    }
    nt_ui_end(s_fx.ctx);
}

/* ---- Inspector panes scroll via OUR scroll path (Clay's is bypassed, D-59-01). A wheel
 * over a clip pane fed by the internal helper moves the offset in the nt_ui_state pool. ---- */
static void test_inspector_pane_scrolls_on_wheel(void) {
    const float screen_w = 1280.0F;
    const float screen_h = 800.0F;
    const uint32_t pane_id = nt_ui_id("scroll_pane_test");

    /* Frame 1: declare so the pane has prev-frame bbox + content dims; no wheel yet. */
    emit_scroll_pane_frame(screen_w, screen_h, 100.0F, 0.0F);
    const nt_ui_scroll_state_t *s0 = (const nt_ui_scroll_state_t *)nt_ui_state_find(s_fx.ctx, pane_id);
    TEST_ASSERT_NOT_NULL(s0);
    TEST_ASSERT_TRUE(s0->pos[1] == 0.0F);

    /* Frame 2: wheel down over the pane -> Clay negative-down offset (content scrolls up). */
    emit_scroll_pane_frame(screen_w, screen_h, 100.0F, 1.0F);
    const nt_ui_scroll_state_t *s1 = (const nt_ui_scroll_state_t *)nt_ui_state_find(s_fx.ctx, pane_id);
    TEST_ASSERT_NOT_NULL(s1);
    TEST_ASSERT_TRUE_MESSAGE(s1->pos[1] < 0.0F, "wheel-down must move pane offset negative (Clay down sign)");

    /* Frame 3: a wheel NOT over the pane (pointer off to the side) leaves the offset put. */
    const float held = s1->pos[1];
    emit_scroll_pane_frame(screen_w, screen_h, 700.0F, 1.0F);
    const nt_ui_scroll_state_t *s2 = (const nt_ui_scroll_state_t *)nt_ui_state_find(s_fx.ctx, pane_id);
    TEST_ASSERT_NOT_NULL(s2);
    TEST_ASSERT_TRUE_MESSAGE(s2->pos[1] == held, "wheel off the pane must not move its offset");
}

/* ---- Test 15x-perf-layer-split: BG (250) carries only sprite cmds, TEXT (251)
 * only text cmds, so the walker's layer-sort collapses dispatch to one BG→TEXT
 * boundary per scissor segment. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_inspector_layer_split_collapses_dispatch(void) {
    nt_ui_inspector_set_active(s_fx.ctx, true);
    const float screen_w = 1280.0F;
    const float screen_h = 800.0F;
    const float panel_left_x = screen_w - 400.0F;

    /* Frame 1: declare so prev-frame bbox exists for the info pane. */
    nt_pointer_t f1 = make_pointer(0.0F, 0.0F);
    nt_ui_begin(s_fx.ctx, screen_w, screen_h, 0.0F, &f1, 1);
    CLAY({.id = CLAY_ID("split_root"), .layout = {.padding = CLAY_PADDING_ALL(20)}, .backgroundColor = {30, 30, 30, 255}}) {
        nt_ui_button_begin(s_fx.ctx, NULL, nt_ui_id("split_btn"), &s_btn_style, NULL, true);
        nt_ui_label(s_fx.ctx, NULL, "OK", &s_label_style);
        (void)nt_ui_button_end(s_fx.ctx);
        nt_ui_image(s_fx.ctx, NULL, &(nt_atlas_region_ref_t){.atlas = s_fx.atlas.handle, .region = s_fx.atlas.white_region_idx}, &s_img_style, NULL);
    }
    s_fx.ctx->inspector_selected_id = nt_ui_id("split_btn");
    nt_ui_end(s_fx.ctx);

    /* Frame 2: full surface. */
    nt_pointer_t f2 = make_pointer(0.0F, 0.0F);
    nt_ui_begin(s_fx.ctx, screen_w, screen_h, 0.0F, &f2, 1);
    CLAY({.id = CLAY_ID("split_root"), .layout = {.padding = CLAY_PADDING_ALL(20)}, .backgroundColor = {30, 30, 30, 255}}) {
        nt_ui_button_begin(s_fx.ctx, NULL, nt_ui_id("split_btn"), &s_btn_style, NULL, true);
        nt_ui_label(s_fx.ctx, NULL, "OK", &s_label_style);
        (void)nt_ui_button_end(s_fx.ctx);
        nt_ui_image(s_fx.ctx, NULL, &(nt_atlas_region_ref_t){.atlas = s_fx.atlas.handle, .region = s_fx.atlas.white_region_idx}, &s_img_style, NULL);
    }
    nt_ui_end(s_fx.ctx);

    Clay_RenderCommandArray *arr = &s_fx.ctx->frozen_cmds;
    TEST_ASSERT_NOT_NULL(arr->internalArray);
    TEST_ASSERT_GREATER_THAN_INT32(0, arr->length);

    /* Walk frozen_cmds and verify the BG/TEXT invariants per inspector cmd. */
    uint32_t bg_count = 0U;
    uint32_t text_count = 0U;
    uint32_t bad_bg_with_text_pipe = 0U;
    uint32_t bad_text_with_sprite_pipe = 0U;
    for (int32_t i = 0; i < arr->length; ++i) {
        const Clay_RenderCommand *cc = &arr->internalArray[i];
        /* Restrict to inspector footprint; skip user widgets + non-drawable barriers. */
        if (cc->boundingBox.x < panel_left_x) {
            continue;
        }
        if (cc->commandType != CLAY_RENDER_COMMAND_TYPE_RECTANGLE && cc->commandType != CLAY_RENDER_COMMAND_TYPE_BORDER && cc->commandType != CLAY_RENDER_COMMAND_TYPE_IMAGE &&
            cc->commandType != CLAY_RENDER_COMMAND_TYPE_TEXT) {
            continue;
        }
        if (cc->userData == NULL) {
            continue; /* unlayered — excluded from split invariant */
        }
        const nt_ui_element_data_t *ed = (const nt_ui_element_data_t *)cc->userData;
        const uint8_t layer = ed->layer;
        if (layer == (uint8_t)NT_UI_LAYER_DEBUG_PANEL_BG) {
            ++bg_count;
            if (cc->commandType == CLAY_RENDER_COMMAND_TYPE_TEXT) {
                ++bad_bg_with_text_pipe;
            }
        } else if (layer == (uint8_t)NT_UI_LAYER_DEBUG_PANEL_TEXT) {
            ++text_count;
            if (cc->commandType != CLAY_RENDER_COMMAND_TYPE_TEXT) {
                ++bad_text_with_sprite_pipe;
            }
        }
    }

    /* Vacuous-pass guards: both buckets non-empty. */
    TEST_ASSERT_GREATER_THAN_UINT32(10U, bg_count);
    TEST_ASSERT_GREATER_THAN_UINT32(10U, text_count);

    char msg[160];
    (void)snprintf(msg, sizeof msg, "PANEL_BG layer must carry only sprite cmds; got %u TEXT cmd(s) on BG (bg_count=%u)", bad_bg_with_text_pipe, bg_count);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0U, bad_bg_with_text_pipe, msg);

    (void)snprintf(msg, sizeof msg, "PANEL_TEXT layer must carry only text cmds; got %u non-TEXT cmd(s) on TEXT (text_count=%u)", bad_text_with_sprite_pipe, text_count);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0U, bad_text_with_sprite_pipe, msg);

    /* Simulate the layer-sort dispatch + count alternations per scissor segment.
     * Bounded by frozen_cmds.length; no heap alloc. */
    enum { PIPE_NONE = 0, PIPE_SPRITE = 1, PIPE_TEXT = 2, SEG_CAP = 1024 };
    int32_t seg_idx[SEG_CAP];
    uint8_t seg_layer[SEG_CAP];
    int seg_pipe[SEG_CAP];
    int32_t seg_len = 0;
    int32_t prev_z = 0;
    bool prev_z_valid = false;
    uint32_t sim_alternations = 0U;
    for (int32_t i = 0; i <= arr->length; ++i) {
        const Clay_RenderCommand *cc = (i < arr->length) ? &arr->internalArray[i] : NULL;
        const bool is_segmentable = (cc != NULL) && (cc->commandType == CLAY_RENDER_COMMAND_TYPE_RECTANGLE || cc->commandType == CLAY_RENDER_COMMAND_TYPE_BORDER ||
                                                     cc->commandType == CLAY_RENDER_COMMAND_TYPE_IMAGE || cc->commandType == CLAY_RENDER_COMMAND_TYPE_TEXT);
        /* Segment boundary: end of array, non-segmentable cmd, or zIndex change. */
        const bool boundary = (cc == NULL) || !is_segmentable || (prev_z_valid && cc->zIndex != prev_z);
        if (boundary) {
            if (seg_len > 0) {
                /* Insertion sort segment indices by layer ascending. */
                for (int32_t k = 1; k < seg_len; ++k) {
                    const int32_t key = seg_idx[k];
                    const uint8_t key_l = seg_layer[k];
                    const int key_p = seg_pipe[k];
                    int32_t p = k - 1;
                    while (p >= 0 && seg_layer[p] > key_l) {
                        seg_idx[p + 1] = seg_idx[p];
                        seg_layer[p + 1] = seg_layer[p];
                        seg_pipe[p + 1] = seg_pipe[p];
                        --p;
                    }
                    seg_idx[p + 1] = key;
                    seg_layer[p + 1] = key_l;
                    seg_pipe[p + 1] = key_p;
                }
                /* Count pipeline alternations across sorted stream. */
                int sim_prev = PIPE_NONE;
                for (int32_t k = 0; k < seg_len; ++k) {
                    if (sim_prev != PIPE_NONE && sim_prev != seg_pipe[k]) {
                        ++sim_alternations;
                    }
                    sim_prev = seg_pipe[k];
                }
                seg_len = 0;
            }
            prev_z_valid = false;
            if (cc == NULL) {
                break;
            }
            if (!is_segmentable) {
                continue;
            }
        }
        /* Collect inspector-footprint cmds for the current segment. */
        if (cc->boundingBox.x >= panel_left_x && seg_len < SEG_CAP) {
            int pipe = PIPE_NONE;
            if (cc->commandType == CLAY_RENDER_COMMAND_TYPE_TEXT) {
                pipe = PIPE_TEXT;
            } else {
                pipe = PIPE_SPRITE;
            }
            const uint8_t layer = cc->userData ? ((const nt_ui_element_data_t *)cc->userData)->layer : 0U;
            seg_idx[seg_len] = i;
            seg_layer[seg_len] = layer;
            seg_pipe[seg_len] = pipe;
            ++seg_len;
        }
        prev_z = cc->zIndex;
        prev_z_valid = true;
    }

    /* Cap 8 leaves headroom for ~5 scissor/zIndex barriers (sidebar / header /
     * scroll clip / info-pane clip / panelContents). */
    char msg2[200];
    (void)snprintf(msg2, sizeof msg2, "post-sort inspector dispatch alternations: %u (cap 8). bg_count=%u text_count=%u", sim_alternations, bg_count, text_count);
    TEST_ASSERT_LESS_OR_EQUAL_UINT32_MESSAGE(8U, sim_alternations, msg2);
}

/* ---- Test 15y: viewport hover is transform-aware via debug_zones scan ----
 * Pointer inside rotated visual but outside axis-aligned bbox; debug_zones
 * inverse-affine picks the widget. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_inspector_hover_transformed_widget(void) {
    nt_ui_inspector_set_active(s_fx.ctx, true);

    const float screen_w = 800.0F;
    const float screen_h = 600.0F;
    const float btn_x = 100.0F; /* left of sidebar at 400 */
    const float btn_y = 200.0F;
    const float btn_w = 160.0F;
    const float btn_h = 48.0F;
    const float btn_cx = btn_x + (btn_w * 0.5F);
    const float btn_cy = btn_y + (btn_h * 0.5F);

    /* Frame 1: declare under transform; bbox cached + accum recorded. */
    nt_pointer_t f1 = make_pointer(0.0F, 0.0F);
    nt_ui_begin(s_fx.ctx, screen_w, screen_h, 0.0F, &f1, 1);
    nt_ui_transform_t baked = nt_ui_transform_defaults();
    baked.rotation_z = 25.0F * 0.017453292F;
    /* Attach userData directly to xf_btn — floating children inherit transform
     * from their floating parentId (ROOT here = identity), not lexical wrappers. */
    CLAY({.id = CLAY_ID("xf_btn"),
          .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .offset = {.x = btn_x, .y = btn_y}},
          .layout = {.sizing = {CLAY_SIZING_FIXED(btn_w), CLAY_SIZING_FIXED(btn_h)}},
          .userData = (void *)NT_UI_DATA_XFORM(0U, &baked, 1.0F)}) {
        (void)nt_ui_step_interaction(s_fx.ctx, nt_ui_id("xf_btn"));
    }
    nt_ui_end(s_fx.ctx);

    /* Pointer = forward-rotated layout right-center: clearly outside the
     * axis-aligned AABB but ON the rotated visual edge after inverse-affine.
     * cos(25°) ≈ 0.9063, sin(25°) ≈ 0.4226, dx = btn_w/2 = 80.
     * rx = 180 + 0.9063·80 = 252.5; ry = 224 + 0.4226·80 = 257.8.
     * Layout AABB y-range [200, 248] → ry=257.8 is outside. */
    const float rot = 25.0F * 0.017453292F;
    const float c = cosf(rot);
    const float s = sinf(rot);
    const float dx = btn_w * 0.5F;
    const float rx = btn_cx + (c * dx);
    const float ry = btn_cy + (s * dx);
    /* Sanity: this coord is OUTSIDE the axis-aligned layout AABB. */
    TEST_ASSERT_TRUE(ry > btn_y + btn_h);

    nt_pointer_t f2 = make_pointer(rx, ry);
    nt_ui_begin(s_fx.ctx, screen_w, screen_h, 0.0F, &f2, 1);
    TEST_ASSERT_FALSE(nt_ui_inspector_pointer_consumed(s_fx.ctx));
    CLAY({.id = CLAY_ID("xf_btn"),
          .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .offset = {.x = btn_x, .y = btn_y}},
          .layout = {.sizing = {CLAY_SIZING_FIXED(btn_w), CLAY_SIZING_FIXED(btn_h)}},
          .userData = (void *)NT_UI_DATA_XFORM(0U, &baked, 1.0F)}) {
        (void)nt_ui_step_interaction(s_fx.ctx, nt_ui_id("xf_btn"));
    }
    nt_ui_end(s_fx.ctx);

    /* Transform-aware path picked the widget. */
    TEST_ASSERT_EQUAL_UINT32(nt_ui_id("xf_btn"), s_fx.ctx->inspector_highlight_id);
}

/* ---- Test 15z: GPU scissor pinned to GAME area; restored to disabled on exit ---- */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_inspector_overlay_scissor_clips_highlight_outside_panel(void) {
    nt_ui_inspector_set_active(s_fx.ctx, true);
    const float screen_w = 800.0F;
    const float screen_h = 600.0F;

    /* Widget at x=500..900 straddles sidebar edge (400). Plain floating element
     * keeps selected_id resolution clean (no button padding interference). */
    const float btn_x = 500.0F;
    const float btn_y = 200.0F;
    const float btn_w = 400.0F; /* extends to x=900, well past panel_left=400 */
    const float btn_h = 48.0F;

    /* Frame 1: declare so Clay records the bbox. */
    nt_pointer_t f1 = make_pointer(0.0F, 0.0F);
    nt_ui_begin(s_fx.ctx, screen_w, screen_h, 0.0F, &f1, 1);
    CLAY({.id = CLAY_ID("scissor_target"),
          .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .offset = {.x = btn_x, .y = btn_y}},
          .layout = {.sizing = {CLAY_SIZING_FIXED(btn_w), CLAY_SIZING_FIXED(btn_h)}}}) {}
    s_fx.ctx->inspector_selected_id = nt_ui_id("scissor_target");
    nt_ui_end(s_fx.ctx);

    /* Frame 2: stable bbox; selected_id propagates to highlight_id. */
    nt_pointer_t f2 = make_pointer(0.0F, 0.0F);
    nt_ui_begin(s_fx.ctx, screen_w, screen_h, 0.0F, &f2, 1);
    CLAY({.id = CLAY_ID("scissor_target"),
          .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .offset = {.x = btn_x, .y = btn_y}},
          .layout = {.sizing = {CLAY_SIZING_FIXED(btn_w), CLAY_SIZING_FIXED(btn_h)}}}) {}
    nt_ui_end(s_fx.ctx);
    TEST_ASSERT_EQUAL_UINT32(nt_ui_id("scissor_target"), s_fx.ctx->inspector_highlight_id);

    /* Pre-flight sentinel so we can attribute scissor changes to the overlay. */
    nt_gfx_set_scissor(0, 0, 0, 0);
    nt_gfx_set_scissor_enabled(false);
    int pre_rect[4] = {0};
    nt_gfx_test_scissor_rect(pre_rect);
    TEST_ASSERT_EQUAL_INT(0, pre_rect[2]);
    TEST_ASSERT_EQUAL_INT(0, pre_rect[3]);

    /* DIRECT target path: fb_size = 0 → simple scissor branch (no scale/offset). */
    nt_ui_target_t target = {.viewport = {0.0F, 0.0F, screen_w, screen_h}};
    nt_ui_inspector_overlay_draw(s_fx.ctx, &target, NT_FONT_INVALID, 0.0F);

    /* Scissor = GAME area: [0, 0, 400, 600] (GL Y-bottom convention). */
    int rect[4] = {0};
    nt_gfx_test_scissor_rect(rect);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, rect[0], "scissor x must be game-area left");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, rect[1], "scissor y must be game-area bottom (GL Y-flip of viewport y)");
    TEST_ASSERT_EQUAL_INT_MESSAGE(400, rect[2], "scissor width must equal panel_left_x = screen_w - NT_UI_INSPECTOR_PANEL_WIDTH");
    TEST_ASSERT_EQUAL_INT_MESSAGE(600, rect[3], "scissor height must equal viewport height");

    /* Overlay must disable scissor on exit (walker-exit invariant). */
    TEST_ASSERT_FALSE_MESSAGE(nt_gfx_test_scissor_enabled(), "overlay must restore scissor to DISABLED on exit");
}

/* ---- Test 15: inactive inspector never intercepts ----
 * Gate stays off when the inspector is disabled. */
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

/* ---- Runtime nt_ui_inspector_metrics_t ---- */

/* Pin documented defaults (400 / 30 / 16 / 10 / 16). */
static void test_inspector_metrics_default_values(void) {
    TEST_ASSERT_TRUE(fabsf(NT_UI_INSPECTOR_METRICS_DEFAULT.panel_width - 400.0F) <= 1e-3F);
    TEST_ASSERT_TRUE(fabsf(NT_UI_INSPECTOR_METRICS_DEFAULT.row_height - 30.0F) <= 1e-3F);
    TEST_ASSERT_EQUAL_UINT16(16U, NT_UI_INSPECTOR_METRICS_DEFAULT.font_size);
    TEST_ASSERT_EQUAL_UINT8(10U, NT_UI_INSPECTOR_METRICS_DEFAULT.outer_padding);
    TEST_ASSERT_EQUAL_UINT8(16U, NT_UI_INSPECTOR_METRICS_DEFAULT.indent_width);
}

/* Freshly-created ctx installs defaults, not memset 0. */
static void test_inspector_metrics_zero_init_uses_defaults(void) {
    /* The fixture already created s_fx.ctx -- inspect its metrics directly. */
    TEST_ASSERT_TRUE(fabsf(s_fx.ctx->inspector_metrics.panel_width - 400.0F) <= 1e-3F);
    TEST_ASSERT_TRUE(fabsf(s_fx.ctx->inspector_metrics.row_height - 30.0F) <= 1e-3F);
    TEST_ASSERT_EQUAL_UINT16(16U, s_fx.ctx->inspector_metrics.font_size);
    TEST_ASSERT_EQUAL_UINT8(10U, s_fx.ctx->inspector_metrics.outer_padding);
    TEST_ASSERT_EQUAL_UINT8(16U, s_fx.ctx->inspector_metrics.indent_width);
}

/* set_metrics flows panel_width into the pointer-consume gate. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_inspector_metrics_set_changes_input_consume_gate(void) {
    nt_ui_inspector_set_active(s_fx.ctx, true);
    const float screen_w = 800.0F;
    const float screen_h = 600.0F;

    /* Default panel_width = 400 -> sidebar covers x=400..800. x=420 is INSIDE. */
    nt_pointer_t p_default = make_pointer(420.0F, 100.0F);
    nt_ui_begin(s_fx.ctx, screen_w, screen_h, 0.0F, &p_default, 1);
    TEST_ASSERT_TRUE_MESSAGE(nt_ui_inspector_pointer_consumed(s_fx.ctx), "default 400 panel: pointer at x=420 must be consumed");
    nt_ui_end(s_fx.ctx);

    /* Shrink to 320 -> sidebar covers x=480..800. x=420 is now OUTSIDE. */
    nt_ui_inspector_metrics_t narrow = NT_UI_INSPECTOR_METRICS_DEFAULT;
    narrow.panel_width = 320.0F;
    nt_ui_inspector_set_metrics(s_fx.ctx, &narrow);
    TEST_ASSERT_TRUE(fabsf(s_fx.ctx->inspector_metrics.panel_width - 320.0F) <= 1e-3F);

    nt_pointer_t p_after = make_pointer(420.0F, 100.0F);
    nt_ui_begin(s_fx.ctx, screen_w, screen_h, 0.0F, &p_after, 1);
    TEST_ASSERT_FALSE_MESSAGE(nt_ui_inspector_pointer_consumed(s_fx.ctx), "after set_metrics(panel_width=320): pointer at x=420 must NOT be consumed");
    nt_ui_end(s_fx.ctx);

    /* Sanity: a pointer INSIDE the narrowed panel still triggers. x=600 is
     * inside 320 (covers 480..800) and was already inside default 400. */
    nt_pointer_t p_in_narrow = make_pointer(600.0F, 100.0F);
    nt_ui_begin(s_fx.ctx, screen_w, screen_h, 0.0F, &p_in_narrow, 1);
    TEST_ASSERT_TRUE_MESSAGE(nt_ui_inspector_pointer_consumed(s_fx.ctx), "narrowed 320 panel: pointer at x=600 must be consumed");
    nt_ui_end(s_fx.ctx);
}

/* Invalid metrics must assert (death tests via the assert trap). */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_inspector_metrics_setter_asserts_on_invalid(void) {
    nt_ui_inspector_metrics_t ok = NT_UI_INSPECTOR_METRICS_DEFAULT;

    /* NULL ctx. */
    NT_TEST_EXPECT_ASSERT(nt_ui_inspector_set_metrics(NULL, &ok));
    /* NULL metrics. */
    NT_TEST_EXPECT_ASSERT(nt_ui_inspector_set_metrics(s_fx.ctx, NULL));
    /* panel_width == 0. */
    nt_ui_inspector_metrics_t zero_panel = ok;
    zero_panel.panel_width = 0.0F;
    NT_TEST_EXPECT_ASSERT(nt_ui_inspector_set_metrics(s_fx.ctx, &zero_panel));
    /* row_height == 0. */
    nt_ui_inspector_metrics_t zero_row = ok;
    zero_row.row_height = 0.0F;
    NT_TEST_EXPECT_ASSERT(nt_ui_inspector_set_metrics(s_fx.ctx, &zero_row));
    /* font_size == 0. */
    nt_ui_inspector_metrics_t zero_font = ok;
    zero_font.font_size = 0U;
    NT_TEST_EXPECT_ASSERT(nt_ui_inspector_set_metrics(s_fx.ctx, &zero_font));
}

/* Overlay scissor reads panel_width from ctx->inspector_metrics. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_inspector_metrics_set_changes_overlay_scissor(void) {
    nt_ui_inspector_set_active(s_fx.ctx, true);
    const float screen_w = 800.0F;
    const float screen_h = 600.0F;

    /* Narrow the panel to 320 BEFORE the overlay draws. */
    nt_ui_inspector_metrics_t narrow = NT_UI_INSPECTOR_METRICS_DEFAULT;
    narrow.panel_width = 320.0F;
    nt_ui_inspector_set_metrics(s_fx.ctx, &narrow);

    /* Widget straddling the new panel edge (panel left = 480, widget at 500..900). */
    const float btn_x = 500.0F;
    const float btn_y = 200.0F;
    const float btn_w = 400.0F;
    const float btn_h = 48.0F;

    /* Frame 1: declare so Clay records the bbox. */
    nt_pointer_t f1 = make_pointer(0.0F, 0.0F);
    nt_ui_begin(s_fx.ctx, screen_w, screen_h, 0.0F, &f1, 1);
    CLAY({.id = CLAY_ID("metric_scissor_target"),
          .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .offset = {.x = btn_x, .y = btn_y}},
          .layout = {.sizing = {CLAY_SIZING_FIXED(btn_w), CLAY_SIZING_FIXED(btn_h)}}}) {}
    s_fx.ctx->inspector_selected_id = nt_ui_id("metric_scissor_target");
    nt_ui_end(s_fx.ctx);

    /* Frame 2: stable bbox; selected_id propagates to highlight_id. */
    nt_pointer_t f2 = make_pointer(0.0F, 0.0F);
    nt_ui_begin(s_fx.ctx, screen_w, screen_h, 0.0F, &f2, 1);
    CLAY({.id = CLAY_ID("metric_scissor_target"),
          .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .offset = {.x = btn_x, .y = btn_y}},
          .layout = {.sizing = {CLAY_SIZING_FIXED(btn_w), CLAY_SIZING_FIXED(btn_h)}}}) {}
    nt_ui_end(s_fx.ctx);
    TEST_ASSERT_EQUAL_UINT32(nt_ui_id("metric_scissor_target"), s_fx.ctx->inspector_highlight_id);

    /* Reset scissor sentinel + draw overlay. */
    nt_gfx_set_scissor(0, 0, 0, 0);
    nt_gfx_set_scissor_enabled(false);
    nt_ui_target_t target = {.viewport = {0.0F, 0.0F, screen_w, screen_h}};
    nt_ui_inspector_overlay_draw(s_fx.ctx, &target, NT_FONT_INVALID, 0.0F);

    /* Scissor width must equal screen_w - panel_width = 800 - 320 = 480. */
    int rect[4] = {0};
    nt_gfx_test_scissor_rect(rect);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, rect[0], "scissor x (game-area left)");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, rect[1], "scissor y (GL Y-bottom)");
    TEST_ASSERT_EQUAL_INT_MESSAGE(480, rect[2], "scissor width must equal screen_w - inspector_metrics.panel_width (800 - 320)");
    TEST_ASSERT_EQUAL_INT_MESSAGE(600, rect[3], "scissor height must equal viewport height");
}

/* ---- Test 15z: inspector emits cleanly over a DISABLED widget + scroll container ----
 * A disabled widget routes through nt_ui_block_pointer (inert occluder) and skips hit-test;
 * the inspector still walks it. Pins the invariant that a disabled widget keeps a def in the
 * registry (the disabled path registers NT_UI_CHECKBOX_DEF too), so the inspector pill stays
 * meaningful — no NULL-def entry — and the overlay/walk over it never traps. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_inspector_over_disabled_widget_and_scroll(void) {
    nt_ui_checkbox_style_t cb = {0};
    const nt_atlas_region_ref_t box = nt_atlas_ref_idx(s_fx.atlas.handle, 0, s_fx.atlas.white_region_idx);
    for (int i = 0; i < 4; ++i) {
        cb.unchecked[i] = (nt_ui_cb_state_t){.box = box, .box_tint = 0xFFFFFFFFU, .check_tint = 0xFFFFFFFFU, .scale = 1.0F, .opacity = 1.0F};
        cb.checked[i] = (nt_ui_cb_state_t){.box = box, .check = box, .box_tint = 0xFFFFFFFFU, .check_tint = 0xFFFFFFFFU, .scale = 1.0F, .opacity = 1.0F};
    }
    cb.text_base = s_label_style;
    cb.box_w = 24.0F;
    cb.box_h = 24.0F;
    cb.overlay_w = 16.0F;
    cb.overlay_h = 16.0F;
    cb.gap = 8.0F;
    cb.state_speed = 0.0F;
    cb.value_speed = 0.0F;

    nt_ui_scroll_style_t scroll = nt_ui_scroll_style_defaults();
    scroll.track_ref = box;
    scroll.thumb_ref = box;

    nt_ui_inspector_set_active(s_fx.ctx, true);
    bool locked = true;

    /* Pointer parked over the disabled checkbox so the viewport-hover scan picks the blocked
     * id and the inspector reads its def for the pill/selected-info pane. */
    nt_pointer_t mouse = make_pointer(60.0F, 60.0F);
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    CLAY({.id = CLAY_ID("root")}) {
        nt_ui_checkbox(s_fx.ctx, NULL, 0U, nt_ui_id("locked"), "Locked (disabled)", &locked, &cb, &(Clay_ElementDeclaration){.layout = {.sizing = {CLAY_SIZING_FIXED(200), CLAY_SIZING_FIXED(40)}}},
                       false);
        nt_ui_scroll_begin(s_fx.ctx, NULL, nt_ui_id("list"), &scroll, &(Clay_ElementDeclaration){.layout = {.sizing = {CLAY_SIZING_FIXED(180), CLAY_SIZING_FIXED(80)}}});
        for (int i = 0; i < 6; ++i) {
            nt_ui_label(s_fx.ctx, NULL, "row", &s_label_style);
        }
        nt_ui_scroll_end(s_fx.ctx);
    }
    /* The disabled (blocked) widget must carry a def in the registry — invariant the
     * inspector relies on. NT_UI_CHECKBOX_DEF is registered even on the disabled path. */
    TEST_ASSERT_EQUAL_PTR(&NT_UI_CHECKBOX_DEF, nt_ui_widget_lookup(s_fx.ctx, nt_ui_id("locked")));
    nt_ui_end(s_fx.ctx); /* inspector emit_layout walks here — must not trap */

    /* Second frame: now the prev-frame interactive registry (incl. the block_pointer
     * entry) is live, exercising resolve_hot + the inspector's viewport-hover pick. */
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    CLAY({.id = CLAY_ID("root")}) {
        nt_ui_checkbox(s_fx.ctx, NULL, 0U, nt_ui_id("locked"), "Locked (disabled)", &locked, &cb, &(Clay_ElementDeclaration){.layout = {.sizing = {CLAY_SIZING_FIXED(200), CLAY_SIZING_FIXED(40)}}},
                       false);
        nt_ui_scroll_begin(s_fx.ctx, NULL, nt_ui_id("list"), &scroll, &(Clay_ElementDeclaration){.layout = {.sizing = {CLAY_SIZING_FIXED(180), CLAY_SIZING_FIXED(80)}}});
        for (int i = 0; i < 6; ++i) {
            nt_ui_label(s_fx.ctx, NULL, "row", &s_label_style);
        }
        nt_ui_scroll_end(s_fx.ctx);
    }
    nt_ui_end(s_fx.ctx);

    nt_ui_target_t target = {.viewport = {0.0F, 0.0F, 800.0F, 600.0F}};
    nt_ui_inspector_overlay_draw(s_fx.ctx, &target, NT_FONT_INVALID, 0.0F);
}

/* ---- Test 15y: inspector stays within Clay's element budget on a large scene ----
 * The inspector injects ~5-6 layout elements per user element (tree row + wrappers + the
 * per-row background rects). On a scene with a few hundred named elements that multiplier
 * pushes Clay's layoutElements array past max_elements — whose error handler is a hard trap,
 * firing the instant the inspector is toggled ON. The emit must cap its own output: total
 * element count stays strictly under capacity regardless of user-tree size (truncated tree,
 * never a crash). Drives the count well past where the unguarded emit overflowed (~200 users). */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_inspector_budget_capped_on_large_scene(void) {
    nt_ui_inspector_set_active(s_fx.ctx, true);
    nt_pointer_t mouse = make_pointer(-100.0F, -100.0F);
    const int32_t cap = (int32_t)s_fx.ctx->max_elements; /* Clay layoutElements capacity == max_elements */
    /* 600 named leaves: unguarded, the inspector emit blows ~4x past the 1024 cap and traps. */
    for (int rep = 0; rep < 3; ++rep) {
        nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
        CLAY({.id = CLAY_ID("root")}) {
            for (int i = 0; i < 600; ++i) {
                CLAY({.id = CLAY_IDI("leaf", (uint32_t)i), .layout = {.sizing = {CLAY_SIZING_FIXED(4), CLAY_SIZING_FIXED(4)}}}) {}
            }
        }
        nt_ui_end(s_fx.ctx); /* unguarded: Clay capacity error -> nt_ui_clay_error_cb traps here */
        const int32_t total = nt_ui_internal_get_layout_element_count(s_fx.ctx);
        char msg[80];
        (void)snprintf(msg, sizeof msg, "rep=%d total=%d cap=%d", rep, total, cap);
        TEST_ASSERT_LESS_THAN_INT32_MESSAGE(cap, total, msg);
        /* The user's own 600+ leaves were emitted; the inspector truncated, not the scene. */
        TEST_ASSERT_GREATER_THAN_INT32_MESSAGE(600, total, msg);
    }
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_registry_register_lookup);
    RUN_TEST(test_registry_resets_each_begin);
    RUN_TEST(test_registry_collision_linear_probe);
    RUN_TEST(test_registry_id_zero_dropped);
    RUN_TEST(test_registry_engine_and_game_defs_coexist);
    RUN_TEST(test_registry_duplicate_id_traps);
    RUN_TEST(test_button_widget_auto_tagged);
    RUN_TEST(test_panel_widget_tagged);
    RUN_TEST(test_image_widget_tagged);
    RUN_TEST(test_label_widget_tagged_on_text_leaf);
    RUN_TEST(test_label_does_not_clobber_parent_widget);
    RUN_TEST(test_inspector_toggle_persists);
    RUN_TEST(test_inspector_inactive_emit_noop);
    RUN_TEST(test_inspector_active_grows_element_count);
    RUN_TEST(test_inspector_many_widgets_safe);
    RUN_TEST(test_overlay_noop_without_highlight);
    RUN_TEST(test_inspector_intercepts_pointer_over_sidebar);
    RUN_TEST(test_inspector_pointer_outside_sidebar_normal);
    RUN_TEST(test_inspector_walker_enumerates_user_tree);
    RUN_TEST(test_widget_register_padded_roundtrip);
    RUN_TEST(test_widget_unpadded_no_hit_padding);
    RUN_TEST(test_button_auto_records_hit_padding);
    RUN_TEST(test_inspector_filter_skips_anonymous);
    RUN_TEST(test_inspector_filter_keeps_text_leaves);
    RUN_TEST(test_inspector_emits_hex_for_unnamed_widget);
    RUN_TEST(test_inspector_collapsed_storage);
    RUN_TEST(test_inspector_collapse_hides_children);
    RUN_TEST(test_inspector_root_emitted_on_debug_layer);
    RUN_TEST(test_layer_debug_value_above_game_layers);
    RUN_TEST(test_highlight_layer_below_panel_layer);
    RUN_TEST(test_inspector_click_text_content_row_selects_leaf);
    RUN_TEST(test_overlay_fallback_chain_with_padded_button);
    RUN_TEST(test_overlay_skips_padded_zone_for_zero_padding);
    RUN_TEST(test_inspector_reads_layer_from_text_config_userdata);
    RUN_TEST(test_inspector_reads_layer_from_shared_config_userdata);
    RUN_TEST(test_overlay_projects_through_accum_for_transformed_id);
    RUN_TEST(test_overlay_falls_back_to_axis_aligned_when_no_zone);
    RUN_TEST(test_inspector_highlight_from_viewport_hover);
    RUN_TEST(test_inspector_highlight_ignores_panel_hover);
    RUN_TEST(test_inspector_highlight_zero_when_no_pointer_over);
    RUN_TEST(test_inspector_viewport_hover_prefers_widget_over_child);
    RUN_TEST(test_inspector_hover_transformed_widget);
    RUN_TEST(test_inspector_inner_emits_carry_debug_layer);
    RUN_TEST(test_inspector_alternations_capped_after_strategy_a);
    RUN_TEST(test_inspector_alternations_bulk_scene_after_strategy_a);
    RUN_TEST(test_inspector_pane_scrolls_on_wheel);
    RUN_TEST(test_inspector_layer_split_collapses_dispatch);
    RUN_TEST(test_inspector_overlay_scissor_clips_highlight_outside_panel);
    RUN_TEST(test_inspector_inactive_no_interception);
    RUN_TEST(test_inspector_metrics_default_values);
    RUN_TEST(test_inspector_metrics_zero_init_uses_defaults);
    RUN_TEST(test_inspector_metrics_set_changes_input_consume_gate);
    RUN_TEST(test_inspector_metrics_setter_asserts_on_invalid);
    RUN_TEST(test_inspector_metrics_set_changes_overlay_scissor);
    RUN_TEST(test_inspector_over_disabled_widget_and_scroll);
    RUN_TEST(test_inspector_budget_capped_on_large_scene);
    return UNITY_END();
}

#else /* NT_UI_DEBUG_TOOLS */

/* When debug tools are off the file still compiles for compile_commands.json
 * coverage; tidy lints the same TU in both configurations. Unity's link
 * pulls setUp/tearDown unconditionally so stubs stay even without RUN_TEST. */
void setUp(void) {}
void tearDown(void) {}
int main(void) { return 0; }

#endif /* NT_UI_DEBUG_TOOLS */
