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
#include "ui/nt_ui_label.h"
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

static const nt_ui_label_style_t s_label_style = {
    .font_id = 0,
    .font_size = 14,
    .color = {255.0F, 255.0F, 255.0F, 255.0F},
};

/* Linear scan for the collected node carrying `id`; NULL if absent. */
static const nt_ui_probe_node_t *find_node(const nt_ui_probe_node_t *nodes, uint32_t count, uint32_t id) {
    for (uint32_t i = 0; i < count; ++i) {
        if (nodes[i].id == id) {
            return &nodes[i];
        }
    }
    return NULL;
}

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

/* ---- Plan 02: collect contract ---- */

static nt_ui_probe_node_t s_nodes[NT_UI_PROBE_MAX_NODES];

/* A panel with a registered button child reporting role/enabled; collect emits flat POD nodes. */
static void test_collect_flat_pod_nodes(void) {
    nt_pointer_t mouse = make_pointer(-100.0F, -100.0F);
    nt_ui_begin(s_fx.ctx, SCREEN_W, SCREEN_H, 0.0F, &mouse, 1);
    CLAY({.id = CLAY_ID("panel"), .layout = {.sizing = {CLAY_SIZING_FIXED(BBOX_W), CLAY_SIZING_FIXED(BBOX_H)}}}) {
        CLAY({.id = CLAY_ID("child_box"), .layout = {.sizing = {CLAY_SIZING_FIXED(40), CLAY_SIZING_FIXED(40)}}}) {}
    }
    nt_ui_end(s_fx.ctx);

    uint32_t count = 0;
    const uint32_t n = nt_ui_probe_collect(s_fx.ctx, s_nodes, NT_UI_PROBE_MAX_NODES, &count);
    TEST_ASSERT_EQUAL_UINT32(n, count);
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(2U, n);

    const nt_ui_probe_node_t *panel = find_node(s_nodes, n, nt_ui_id("panel"));
    const nt_ui_probe_node_t *child = find_node(s_nodes, n, nt_ui_id("child_box"));
    TEST_ASSERT_NOT_NULL(panel);
    TEST_ASSERT_NOT_NULL(child);
    TEST_ASSERT_EQUAL_STRING("panel", panel->id_string);
    TEST_ASSERT_EQUAL_UINT32(NT_UI_PROBE_NO_PARENT, panel->parent);
}

/* Copy-on-collect: the id_string copy survives a SECOND nt_ui_begin (spike SC3). */
static void test_collect_id_string_copied(void) {
    nt_pointer_t mouse = make_pointer(-100.0F, -100.0F);
    nt_ui_begin(s_fx.ctx, SCREEN_W, SCREEN_H, 0.0F, &mouse, 1);
    CLAY({.id = CLAY_ID("persistent")}) {}
    nt_ui_end(s_fx.ctx);

    uint32_t count = 0;
    const uint32_t n = nt_ui_probe_collect(s_fx.ctx, s_nodes, NT_UI_PROBE_MAX_NODES, &count);
    const nt_ui_probe_node_t *node = find_node(s_nodes, n, nt_ui_id("persistent"));
    TEST_ASSERT_NOT_NULL(node);
    TEST_ASSERT_EQUAL_STRING("persistent", node->id_string);

    /* A second begin (re-using Clay's id-string arena) would dangle a borrowed pointer. */
    nt_ui_begin(s_fx.ctx, SCREEN_W, SCREEN_H, 0.0F, &mouse, 1);
    CLAY({.id = CLAY_ID("other_tree_a")}) {}
    CLAY({.id = CLAY_ID("other_tree_b")}) {}
    nt_ui_end(s_fx.ctx);

    /* The owned copy in the FIRST collect is still intact. */
    TEST_ASSERT_EQUAL_STRING("persistent", node->id_string);
}

/* visible: an opacity-0 subtree reports visible=false; a plain on-screen node reports true. */
static void test_collect_visible_clip_opacity(void) {
    nt_pointer_t mouse = make_pointer(-100.0F, -100.0F);
    nt_ui_transform_t ident = nt_ui_transform_defaults();
    nt_ui_begin(s_fx.ctx, SCREEN_W, SCREEN_H, 0.0F, &mouse, 1);
    CLAY({.id = CLAY_ID("vis_box"), .layout = {.sizing = {CLAY_SIZING_FIXED(BBOX_W), CLAY_SIZING_FIXED(BBOX_H)}}}) {}
    /* opacity 0 subtree -> composed opacity below the visible threshold (D-03). */
    CLAY({.id = CLAY_ID("hidden_box"), .layout = {.sizing = {CLAY_SIZING_FIXED(BBOX_W), CLAY_SIZING_FIXED(BBOX_H)}}, .userData = (void *)NT_UI_DATA_XFORM(0U, &ident, 0.0F)}) {}
    nt_ui_end(s_fx.ctx);

    uint32_t count = 0;
    const uint32_t n = nt_ui_probe_collect(s_fx.ctx, s_nodes, NT_UI_PROBE_MAX_NODES, &count);
    const nt_ui_probe_node_t *vis = find_node(s_nodes, n, nt_ui_id("vis_box"));
    const nt_ui_probe_node_t *hid = find_node(s_nodes, n, nt_ui_id("hidden_box"));
    TEST_ASSERT_NOT_NULL(vis);
    TEST_ASSERT_NOT_NULL(hid);
    TEST_ASSERT_TRUE(vis->visible);
    TEST_ASSERT_FALSE(hid->visible);
    /* D-05: the invisible node is STILL emitted, carrying visible=false. */
}

/* role: a registered widget reports role=WIDGET + def->name; an unregistered box falls back. */
static void test_collect_role_from_def_name(void) {
    nt_pointer_t mouse = make_pointer(-100.0F, -100.0F);
    nt_ui_begin(s_fx.ctx, SCREEN_W, SCREEN_H, 0.0F, &mouse, 1);
    CLAY({.id = CLAY_ID("plain_box"), .layout = {.sizing = {CLAY_SIZING_FIXED(40), CLAY_SIZING_FIXED(40)}}}) {}
    CLAY({.id = CLAY_ID("reg_btn"), .layout = {.sizing = {CLAY_SIZING_FIXED(40), CLAY_SIZING_FIXED(40)}}}) {}
    register_widget(nt_ui_id("reg_btn"), true);
    nt_ui_end(s_fx.ctx);

    uint32_t count = 0;
    const uint32_t n = nt_ui_probe_collect(s_fx.ctx, s_nodes, NT_UI_PROBE_MAX_NODES, &count);
    const nt_ui_probe_node_t *box = find_node(s_nodes, n, nt_ui_id("plain_box"));
    const nt_ui_probe_node_t *btn = find_node(s_nodes, n, nt_ui_id("reg_btn"));
    TEST_ASSERT_NOT_NULL(box);
    TEST_ASSERT_NOT_NULL(btn);
    TEST_ASSERT_EQUAL_INT(NT_UI_PROBE_ROLE_BOX, box->role);
    TEST_ASSERT_EQUAL_INT(NT_UI_PROBE_ROLE_WIDGET, btn->role);
    TEST_ASSERT_EQUAL_STRING(NT_UI_BUTTON_DEF.name, btn->role_name);
}

/* parent/children: a child reports parent == the panel's id; the panel counts its children. */
static void test_collect_parent_children(void) {
    nt_pointer_t mouse = make_pointer(-100.0F, -100.0F);
    nt_ui_begin(s_fx.ctx, SCREEN_W, SCREEN_H, 0.0F, &mouse, 1);
    CLAY({.id = CLAY_ID("outer")}) {
        CLAY({.id = CLAY_ID("inner_a"), .layout = {.sizing = {CLAY_SIZING_FIXED(10), CLAY_SIZING_FIXED(10)}}}) {}
        CLAY({.id = CLAY_ID("inner_b"), .layout = {.sizing = {CLAY_SIZING_FIXED(10), CLAY_SIZING_FIXED(10)}}}) {}
    }
    nt_ui_end(s_fx.ctx);

    uint32_t count = 0;
    const uint32_t n = nt_ui_probe_collect(s_fx.ctx, s_nodes, NT_UI_PROBE_MAX_NODES, &count);
    const nt_ui_probe_node_t *outer = find_node(s_nodes, n, nt_ui_id("outer"));
    const nt_ui_probe_node_t *a = find_node(s_nodes, n, nt_ui_id("inner_a"));
    const nt_ui_probe_node_t *b = find_node(s_nodes, n, nt_ui_id("inner_b"));
    TEST_ASSERT_NOT_NULL(outer);
    TEST_ASSERT_NOT_NULL(a);
    TEST_ASSERT_NOT_NULL(b);
    TEST_ASSERT_EQUAL_UINT32(nt_ui_id("outer"), a->parent);
    TEST_ASSERT_EQUAL_UINT32(nt_ui_id("outer"), b->parent);
    TEST_ASSERT_EQUAL_UINT16(2U, outer->child_count);
}

/* label vs text: a box with a "Save" text child reports label="Save", own text empty;
 * the text leaf itself reports its own text. */
static void test_collect_label_distinct_from_text(void) {
    nt_pointer_t mouse = make_pointer(-100.0F, -100.0F);
    nt_ui_begin(s_fx.ctx, SCREEN_W, SCREEN_H, 0.0F, &mouse, 1);
    CLAY({.id = CLAY_ID("save_box"), .layout = {.sizing = {CLAY_SIZING_FIXED(80), CLAY_SIZING_FIXED(30)}}}) { nt_ui_label(s_fx.ctx, NULL, "Save", &s_label_style); }
    nt_ui_end(s_fx.ctx);

    uint32_t count = 0;
    const uint32_t n = nt_ui_probe_collect(s_fx.ctx, s_nodes, NT_UI_PROBE_MAX_NODES, &count);
    const nt_ui_probe_node_t *box = find_node(s_nodes, n, nt_ui_id("save_box"));
    TEST_ASSERT_NOT_NULL(box);
    TEST_ASSERT_EQUAL_STRING("Save", box->label);
    TEST_ASSERT_EQUAL_STRING("", box->text); /* own text empty; "Save" is the child's */

    /* The text leaf carries its OWN text "Save" (distinct from the parent's empty own-text). */
    bool found_text_leaf = false;
    for (uint32_t i = 0; i < n; ++i) {
        if (s_nodes[i].id != box->id && strcmp(s_nodes[i].text, "Save") == 0) {
            found_text_leaf = true;
        }
    }
    TEST_ASSERT_TRUE(found_text_leaf);
}

/* D-05: a disabled widget remains in the collected set carrying enabled=false. */
static void test_collect_all_nodes_disabled_present(void) {
    nt_pointer_t mouse = make_pointer(-100.0F, -100.0F);
    nt_ui_begin(s_fx.ctx, SCREEN_W, SCREEN_H, 0.0F, &mouse, 1);
    CLAY({.id = CLAY_ID("dis_btn"), .layout = {.sizing = {CLAY_SIZING_FIXED(40), CLAY_SIZING_FIXED(40)}}}) {}
    register_widget(nt_ui_id("dis_btn"), false);
    nt_ui_end(s_fx.ctx);

    uint32_t count = 0;
    const uint32_t n = nt_ui_probe_collect(s_fx.ctx, s_nodes, NT_UI_PROBE_MAX_NODES, &count);
    const nt_ui_probe_node_t *btn = find_node(s_nodes, n, nt_ui_id("dis_btn"));
    TEST_ASSERT_NOT_NULL(btn); /* present despite disabled */
    TEST_ASSERT_FALSE(btn->enabled);
}

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
    RUN_TEST(test_collect_all_nodes_disabled_present);
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
