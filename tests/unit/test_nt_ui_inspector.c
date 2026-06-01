/* Phase 56 ext (CHUNK E): nt_ui_inspector + widget_registry tests.
 *
 * Two pieces of new surface to verify:
 *   1) widget_registry direct-mapped table -- register/lookup roundtrip,
 *      slot collision (replace-on-collision policy), reset each begin.
 *   2) nt_ui_inspector toggle + draw -- silent when inactive, no crash
 *      on zero / max registered widgets, doesn't disturb walker state.
 *
 * Pixel output of the inspector is NOT unit-tested -- visual verification
 * happens in the ui_buttons_demo F3 toggle. */

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

/* ---- Test 9: inactive inspector_draw is a silent no-op ---- */
static void test_inspector_inactive_no_crash(void) {
    TEST_ASSERT_FALSE(nt_ui_inspector_is_active(s_fx.ctx));
    nt_pointer_t mouse = make_pointer(0.0F, 0.0F);
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    nt_ui_end(s_fx.ctx);
    nt_ui_target_t target = {.viewport = {0.0F, 0.0F, 800.0F, 600.0F}};
    nt_ui_inspector_draw(s_fx.ctx, &target, NT_FONT_INVALID, 0.0F); /* must not crash */
}

/* ---- Test 10: active inspector_draw on zero declared widgets is safe ---- */
static void test_inspector_zero_widgets_safe(void) {
    nt_ui_inspector_set_active(s_fx.ctx, true);
    nt_pointer_t mouse = make_pointer(0.0F, 0.0F);
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    nt_ui_end(s_fx.ctx);
    nt_ui_target_t target = {.viewport = {0.0F, 0.0F, 800.0F, 600.0F}};
    nt_ui_inspector_draw(s_fx.ctx, &target, NT_FONT_INVALID, 0.0F); /* must not crash */
}

/* ---- Test 11: active inspector_draw with a full sidebar of widgets is safe ---- */
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
    nt_ui_end(s_fx.ctx);
    nt_ui_target_t target = {.viewport = {0.0F, 0.0F, 800.0F, 600.0F}};
    nt_ui_inspector_draw(s_fx.ctx, &target, NT_FONT_INVALID, 0.0F); /* must not crash */
}

/* ---- Test 12 (REWORKED): inspector_draw is a READER of the recorded zone
 *      buffer -- it must not MUTATE it (no consume, no rewrite). It IS now
 *      allowed (and expected) to forward the buffer to the hit-zone overlay
 *      drawing helper when ctx->debug_recording is on (re-coupling fix:
 *      F3 owns both the inspector panel AND the hit-zone visualization).
 *      The buffer contents stay byte-identical after the inspector returns. */
static void test_inspector_does_not_mutate_zone_buffer(void) {
    /* Arrange: record one hit-zone via a button under the mouse. */
    nt_ui_debug_set_recording(s_fx.ctx, true);
    nt_ui_inspector_set_active(s_fx.ctx, true);

    /* Frame 1: declare so Clay stores the bbox in its persistent hashmap.
     * Without a prior frame the bbox is unknown and the zone won't record. */
    nt_pointer_t mouse = make_pointer(100.0F, 100.0F);
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    CLAY({.id = CLAY_ID("root")}) {
        nt_ui_button_begin(s_fx.ctx, NULL, nt_ui_id("zbtn"), s_fx.atlas.handle, &s_btn_style, true);
        nt_ui_label(s_fx.ctx, NULL, "Z", &s_label_style);
        (void)nt_ui_button_end(s_fx.ctx);
    }
    nt_ui_end(s_fx.ctx);

    /* Frame 2: the button bbox is known; the recording path inside
     * nt_ui_get_interaction_padded pushes one zone. */
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    CLAY({.id = CLAY_ID("root")}) {
        nt_ui_button_begin(s_fx.ctx, NULL, nt_ui_id("zbtn"), s_fx.atlas.handle, &s_btn_style, true);
        nt_ui_label(s_fx.ctx, NULL, "Z", &s_label_style);
        (void)nt_ui_button_end(s_fx.ctx);
    }
    nt_ui_end(s_fx.ctx);

    const uint32_t zone_count_before = nt_ui_debug_get_zone_count(s_fx.ctx);
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(1U, zone_count_before);
    /* Snapshot zone[0] -- inspector must not MUTATE the zone buffer. */
    const nt_ui_debug_zone_t z_before = s_fx.ctx->debug_zones[0];

    /* Act: draw the inspector. Inspector NOW also calls the overlay
     * internally (debug_recording is on) -- but it only READS the zones. */
    nt_ui_target_t target = {.viewport = {0.0F, 0.0F, 800.0F, 600.0F}};
    nt_ui_inspector_draw(s_fx.ctx, &target, NT_FONT_INVALID, 0.0F);

    /* Assert: zone count is unchanged, zone[0] is byte-identical. */
    const uint32_t zone_count_after = nt_ui_debug_get_zone_count(s_fx.ctx);
    TEST_ASSERT_EQUAL_UINT32(zone_count_before, zone_count_after);
    TEST_ASSERT_EQUAL_MEMORY(&z_before, &s_fx.ctx->debug_zones[0], sizeof z_before);
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
    RUN_TEST(test_inspector_inactive_no_crash);
    RUN_TEST(test_inspector_zero_widgets_safe);
    RUN_TEST(test_inspector_many_widgets_safe);
    RUN_TEST(test_inspector_does_not_mutate_zone_buffer);
    return UNITY_END();
}
