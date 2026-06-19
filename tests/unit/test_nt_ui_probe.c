/* nt_ui probe (L1) — flat POD tree extraction + always-compiled `enabled` signal.
 *
 * Phase 67 Wave 1 scaffold: the `enabled`-in-slot cases land here now (spike SC4:
 * a disabled widget must report enabled=false with the inspector OFF). The
 * collect-contract (Plan 02) and 3D-projection (Plan 03) cases are scaffolded
 * with TEST_IGNORE so later plans fill them in.
 *
 * Asymmetric known geometry (200×60 @ (150,80) in 800×600) mirrors
 * test_nt_ui_3d_hittest.c so axis-swap / flip bugs surface in the projection
 * cases that arrive in Plan 03. */

#include <math.h>
#include <stdalign.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "clay.h"
#include "input/nt_input.h"
#include "math/nt_math.h"
#include "test_helpers/nt_assert_trap.h"
#include "test_helpers/ui_test_arena.h"
#include "test_helpers/ui_walker_fixture.h"
#include "ui/nt_ui.h"
#include "ui/nt_ui_button.h"
#include "ui/nt_ui_internal.h"
#include "unity.h"

#if NT_UI_DEBUG_TOOLS

alignas(NT_UI_ARENA_ALIGN) static uint8_t s_arena[NT_UI_TEST_ARENA_SIZE];
static ui_walker_fixture_t s_fx;

#define SCREEN_W 800.0F
#define SCREEN_H 600.0F
#define BBOX_X 150.0F
#define BBOX_Y 80.0F
#define BBOX_W 200.0F
#define BBOX_H 60.0F

void setUp(void) {
    nt_test_assert_install();
    ui_walker_fixture_init(&s_fx, s_arena, sizeof s_arena, UI_WALKER_FX_BIND_ALL);
    /* inspector_active stays OFF (fixture default) — proves the always-compiled signal. */
}

void tearDown(void) { ui_walker_fixture_shutdown(&s_fx); }

static nt_pointer_t make_pointer(float x, float y) {
    nt_pointer_t p = {0};
    p.x = x;
    p.y = y;
    p.active = true;
    return p;
}

/* Register a widget id with the given enabled state — exactly the slot write
 * nt_ui_button_begin performs internally, isolated from button rendering. */
static void register_widget(uint32_t id, bool enabled) { nt_ui_widget_register(s_fx.ctx, id, &NT_UI_BUTTON_DEF, NULL, enabled); }

/* ---- enabled signal (Wave 1, the spike SC4 closer) ---- */

/* A disabled widget reports enabled=false with the inspector OFF. */
static void test_enabled_false_with_inspector_off(void) {
    nt_pointer_t mouse = make_pointer(0.0F, 0.0F);
    nt_ui_begin(s_fx.ctx, SCREEN_W, SCREEN_H, 0.0F, &mouse, 1);
    const uint32_t id = nt_ui_id("disabled_btn");
    register_widget(id, false);
    TEST_ASSERT_FALSE(nt_ui_internal_widget_enabled(s_fx.ctx, id));
    nt_ui_end(s_fx.ctx);
}

/* An enabled widget reports enabled=true. */
static void test_enabled_true(void) {
    nt_pointer_t mouse = make_pointer(0.0F, 0.0F);
    nt_ui_begin(s_fx.ctx, SCREEN_W, SCREEN_H, 0.0F, &mouse, 1);
    const uint32_t id = nt_ui_id("enabled_btn");
    register_widget(id, true);
    TEST_ASSERT_TRUE(nt_ui_internal_widget_enabled(s_fx.ctx, id));
    nt_ui_end(s_fx.ctx);
}

/* An id that was never registered defaults to enabled=true (D-02). */
static void test_enabled_default_true_unregistered(void) {
    nt_pointer_t mouse = make_pointer(0.0F, 0.0F);
    nt_ui_begin(s_fx.ctx, SCREEN_W, SCREEN_H, 0.0F, &mouse, 1);
    TEST_ASSERT_TRUE(nt_ui_internal_widget_enabled(s_fx.ctx, nt_ui_id("never_registered")));
    nt_ui_end(s_fx.ctx);
}

/* ---- Plan 02: collect contract (scaffold) ---- */

static void test_collect_flat_pod_nodes(void) { TEST_IGNORE_MESSAGE("Plan 02: nt_ui_probe_collect flat POD nodes"); }
static void test_collect_id_string_copied(void) { TEST_IGNORE_MESSAGE("Plan 02: copy-on-collect id_string survives next nt_ui_begin (SC3)"); }
static void test_collect_visible_clip_opacity(void) { TEST_IGNORE_MESSAGE("Plan 02: visible = clip + opacity"); }
static void test_collect_role_from_def_name(void) { TEST_IGNORE_MESSAGE("Plan 02: role = registered def->name, fallback config_mask"); }
static void test_collect_parent_children(void) { TEST_IGNORE_MESSAGE("Plan 02: parent/children reconstructed from DFS stack"); }
static void test_collect_label_distinct_from_text(void) { TEST_IGNORE_MESSAGE("Plan 02: label = first text child, distinct from text"); }

/* ---- Plan 03: projection (scaffold) ---- */

static void test_project_2d_plain_yflip(void) { TEST_IGNORE_MESSAGE("Plan 03: 2D plain Y-flip bounds match known geometry"); }
static void test_project_3d_ortho_aabb(void) { TEST_IGNORE_MESSAGE("Plan 03: 3D-ctx AABB matches ortho-projected geometry"); }
static void test_project_3d_behind_camera_flag(void) { TEST_IGNORE_MESSAGE("Plan 03: behind-camera corner flagged, not garbage pixels"); }

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_enabled_false_with_inspector_off);
    RUN_TEST(test_enabled_true);
    RUN_TEST(test_enabled_default_true_unregistered);
    RUN_TEST(test_collect_flat_pod_nodes);
    RUN_TEST(test_collect_id_string_copied);
    RUN_TEST(test_collect_visible_clip_opacity);
    RUN_TEST(test_collect_role_from_def_name);
    RUN_TEST(test_collect_parent_children);
    RUN_TEST(test_collect_label_distinct_from_text);
    RUN_TEST(test_project_2d_plain_yflip);
    RUN_TEST(test_project_3d_ortho_aabb);
    RUN_TEST(test_project_3d_behind_camera_flag);
    return UNITY_END();
}

#else /* NT_UI_DEBUG_TOOLS off: probe is DEBUG_TOOLS-gated; compile to a passing stub. */

int main(void) {
    UNITY_BEGIN();
    return UNITY_END();
}

#endif
