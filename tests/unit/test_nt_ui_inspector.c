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
    TEST_ASSERT_EQUAL_INT(NT_UI_WIDGET_NONE, (int)nt_ui_widget_lookup(s_fx.ctx, nt_ui_id("foo")));
    /* Register one. */
    nt_ui_widget_register(s_fx.ctx, nt_ui_id("foo"), NT_UI_WIDGET_BUTTON);
    TEST_ASSERT_EQUAL_INT(NT_UI_WIDGET_BUTTON, (int)nt_ui_widget_lookup(s_fx.ctx, nt_ui_id("foo")));
    /* Different id, same bucket NOT guaranteed -- but distinct lookups return NONE. */
    TEST_ASSERT_EQUAL_INT(NT_UI_WIDGET_NONE, (int)nt_ui_widget_lookup(s_fx.ctx, nt_ui_id("bar")));
    nt_ui_end(s_fx.ctx);
}

/* ---- Test 2: registry resets each nt_ui_begin (no stale tags) ---- */
static void test_registry_resets_each_begin(void) {
    nt_pointer_t mouse = make_pointer(0.0F, 0.0F);
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    nt_ui_widget_register(s_fx.ctx, nt_ui_id("foo"), NT_UI_WIDGET_BUTTON);
    TEST_ASSERT_EQUAL_INT(NT_UI_WIDGET_BUTTON, (int)nt_ui_widget_lookup(s_fx.ctx, nt_ui_id("foo")));
    nt_ui_end(s_fx.ctx);

    /* Next begin -- registry must be empty again. */
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    TEST_ASSERT_EQUAL_INT(NT_UI_WIDGET_NONE, (int)nt_ui_widget_lookup(s_fx.ctx, nt_ui_id("foo")));
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
    nt_ui_widget_register(s_fx.ctx, base_id, NT_UI_WIDGET_BUTTON);
    TEST_ASSERT_EQUAL_INT(NT_UI_WIDGET_BUTTON, (int)nt_ui_widget_lookup(s_fx.ctx, base_id));
    /* Collide -- new write must overwrite the slot. */
    nt_ui_widget_register(s_fx.ctx, collide_id, NT_UI_WIDGET_IMAGE);
    TEST_ASSERT_EQUAL_INT(NT_UI_WIDGET_IMAGE, (int)nt_ui_widget_lookup(s_fx.ctx, collide_id));
    /* The original id now misses (replace-on-collision policy). */
    TEST_ASSERT_EQUAL_INT(NT_UI_WIDGET_NONE, (int)nt_ui_widget_lookup(s_fx.ctx, base_id));
    nt_ui_end(s_fx.ctx);
}

/* ---- Test 4: id=0 (sentinel) is silently dropped on register + lookup ---- */
static void test_registry_id_zero_dropped(void) {
    nt_pointer_t mouse = make_pointer(0.0F, 0.0F);
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    nt_ui_widget_register(s_fx.ctx, 0U, NT_UI_WIDGET_BUTTON); /* must not assert */
    TEST_ASSERT_EQUAL_INT(NT_UI_WIDGET_NONE, (int)nt_ui_widget_lookup(s_fx.ctx, 0U));
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
    /* While in-frame, the button id must be tagged BUTTON. */
    TEST_ASSERT_EQUAL_INT(NT_UI_WIDGET_BUTTON, (int)nt_ui_widget_lookup(s_fx.ctx, nt_ui_id("btn")));
    nt_ui_end(s_fx.ctx);
}

/* ---- Test 6: nt_ui_panel declaration auto-tags PANEL (id == Clay-assigned) ---- */
static void test_panel_widget_tagged(void) {
    nt_pointer_t mouse = make_pointer(0.0F, 0.0F);
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    /* Panel/image use Clay's auto-assigned id (no explicit id arg in API),
     * so we can't easily look it up by name. Verify by counting tags: at least
     * one PANEL entry must exist in the registry after declaration. */
    CLAY({.id = CLAY_ID("root")}) {
        nt_ui_panel_begin(s_fx.ctx, NULL, s_fx.atlas.handle, s_fx.atlas.white_region_idx, &s_img_style);
        nt_ui_panel_end(s_fx.ctx);
    }
    /* Scan the registry for any PANEL-tagged slot. */
    uint32_t panel_count = 0U;
    for (uint32_t i = 0; i < (uint32_t)NT_UI_WIDGET_REGISTRY_CAP; ++i) {
        if (s_fx.ctx->widget_registry[i].id != 0U && s_fx.ctx->widget_registry[i].type == (uint8_t)NT_UI_WIDGET_PANEL) {
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
        if (s_fx.ctx->widget_registry[i].id != 0U && s_fx.ctx->widget_registry[i].type == (uint8_t)NT_UI_WIDGET_IMAGE) {
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

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_registry_register_lookup);
    RUN_TEST(test_registry_resets_each_begin);
    RUN_TEST(test_registry_replace_on_collision);
    RUN_TEST(test_registry_id_zero_dropped);
    RUN_TEST(test_button_widget_auto_tagged);
    RUN_TEST(test_panel_widget_tagged);
    RUN_TEST(test_image_widget_tagged);
    RUN_TEST(test_inspector_toggle_persists);
    RUN_TEST(test_inspector_inactive_emit_noop);
    RUN_TEST(test_inspector_active_grows_element_count);
    RUN_TEST(test_inspector_many_widgets_safe);
    RUN_TEST(test_overlay_noop_without_highlight);
    return UNITY_END();
}
