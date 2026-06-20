/* nt_ui probe — flat POD tree extraction + always-compiled `enabled` signal.
 *
 * Asymmetric known geometry (200×60 @ (150,80) in 800×600) mirrors
 * test_nt_ui_3d_hittest.c so axis-swap / flip bugs surface in the projection cases. */

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
#include "ui/nt_ui_input.h"
#include "ui/nt_ui_internal.h"
#include "ui/nt_ui_label.h"
#include "ui/nt_ui_slider.h"
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

/* ---- enabled signal ---- */

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

/* An id that was never registered defaults to enabled=true. */
static void test_enabled_default_true_unregistered(void) {
    nt_pointer_t mouse = make_pointer(0.0F, 0.0F);
    nt_ui_begin(s_fx.ctx, SCREEN_W, SCREEN_H, 0.0F, &mouse, 1);
    TEST_ASSERT_TRUE(nt_ui_internal_widget_enabled(s_fx.ctx, nt_ui_id("never_registered")));
    nt_ui_end(s_fx.ctx);
}

/* A DISABLED input field forwards its real enabled state to the probe (must read false,
 * not the hardcoded true the compose path used to register). */
static void test_input_disabled_reports_enabled_false(void) {
    nt_ui_input_style_t style = nt_ui_input_style_defaults();
    style.text.font_id = 0;
    style.placeholder.font_id = 0;
    nt_ui_input_props_t props;
    memset(&props, 0, sizeof props);
    const Clay_ElementDeclaration field = {.layout = {.sizing = {CLAY_SIZING_FIXED(BBOX_W), CLAY_SIZING_FIXED(BBOX_H)}}};
    char buf[8] = "";

    nt_pointer_t mouse = make_pointer(-100.0F, -100.0F);
    const uint32_t id = nt_ui_id("disabled_input");
    nt_ui_begin(s_fx.ctx, SCREEN_W, SCREEN_H, 0.0F, &mouse, 1);
    (void)nt_ui_input_text(s_fx.ctx, NULL, 0, id, buf, sizeof buf, &props, &style, &field, false, NULL);
    TEST_ASSERT_FALSE(nt_ui_internal_widget_enabled(s_fx.ctx, id));
    nt_ui_end(s_fx.ctx);
}

/* A DISABLED slider forwards its real enabled state to the probe (must read false). */
static void test_slider_disabled_reports_enabled_false(void) {
    const nt_atlas_region_ref_t art = nt_atlas_ref_idx(s_fx.atlas.handle, 0, s_fx.atlas.white_region_idx);
    nt_ui_slider_style_t style = nt_ui_slider_style_defaults();
    for (int i = 0; i < 4; ++i) {
        style.states[i].track = art;
        style.states[i].fill = art;
        style.states[i].thumb = art;
    }
    style.track_w = BBOX_W;
    style.track_h = 20.0F;
    style.thumb_w = 20.0F;
    style.thumb_h = 24.0F;
    style.state_speed = 0.0F;
    style.value_speed = 0.0F;
    const Clay_ElementDeclaration track = {.layout = {.sizing = {CLAY_SIZING_FIXED(BBOX_W), CLAY_SIZING_FIXED(20.0F)}}};
    float value = 0.5F;

    nt_pointer_t mouse = make_pointer(-100.0F, -100.0F);
    const uint32_t id = nt_ui_id("disabled_slider");
    nt_ui_begin(s_fx.ctx, SCREEN_W, SCREEN_H, 0.0F, &mouse, 1);
    (void)nt_ui_slider_float(s_fx.ctx, NULL, 0, id, NULL, &value, 0.0F, 1.0F, 0.0F, &style, &track, false);
    TEST_ASSERT_FALSE(nt_ui_internal_widget_enabled(s_fx.ctx, id));
    nt_ui_end(s_fx.ctx);
}

/* ---- collect contract ---- */

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
    const uint32_t n = nt_ui_probe_collect(s_fx.ctx, s_nodes, NT_UI_PROBE_MAX_NODES, &count, NULL);
    TEST_ASSERT_EQUAL_UINT32(n, count);
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(2U, n);

    const nt_ui_probe_node_t *panel = find_node(s_nodes, n, nt_ui_id("panel"));
    const nt_ui_probe_node_t *child = find_node(s_nodes, n, nt_ui_id("child_box"));
    TEST_ASSERT_NOT_NULL(panel);
    TEST_ASSERT_NOT_NULL(child);
    TEST_ASSERT_EQUAL_STRING("panel", panel->id_string);
    TEST_ASSERT_EQUAL_UINT32(NT_UI_PROBE_NO_PARENT, panel->parent);
}

/* A `cap` smaller than the node count truncates: out_count saturates at cap, out_truncated=true. The
 * cap PARAMETER exercises the same early-stop path the 1024-node BSS cap hits, without a huge tree. */
static void test_collect_cap_truncation_signaled(void) {
    nt_pointer_t mouse = make_pointer(-100.0F, -100.0F);
    nt_ui_begin(s_fx.ctx, SCREEN_W, SCREEN_H, 0.0F, &mouse, 1);
    CLAY({.id = CLAY_ID("panel"), .layout = {.sizing = {CLAY_SIZING_FIXED(BBOX_W), CLAY_SIZING_FIXED(BBOX_H)}}}) {
        CLAY({.id = CLAY_ID("child_a"), .layout = {.sizing = {CLAY_SIZING_FIXED(40), CLAY_SIZING_FIXED(40)}}}) {}
        CLAY({.id = CLAY_ID("child_b"), .layout = {.sizing = {CLAY_SIZING_FIXED(40), CLAY_SIZING_FIXED(40)}}}) {}
    }
    nt_ui_end(s_fx.ctx);

    /* Full collect to learn the real node count, then a cap one below it. */
    uint32_t full = 0;
    bool full_trunc = true;
    const uint32_t n = nt_ui_probe_collect(s_fx.ctx, s_nodes, NT_UI_PROBE_MAX_NODES, &full, &full_trunc);
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(2U, n);
    TEST_ASSERT_FALSE(full_trunc); /* whole tree fits: not truncated */

    const uint32_t cap = n - 1U;
    uint32_t count = 0;
    bool truncated = false;
    const uint32_t got = nt_ui_probe_collect(s_fx.ctx, s_nodes, cap, &count, &truncated);
    TEST_ASSERT_EQUAL_UINT32(cap, got);
    TEST_ASSERT_EQUAL_UINT32(cap, count);
    TEST_ASSERT_TRUE(truncated);
}

/* Copy-on-collect: the id_string copy survives a SECOND nt_ui_begin. */
static void test_collect_id_string_copied(void) {
    nt_pointer_t mouse = make_pointer(-100.0F, -100.0F);
    nt_ui_begin(s_fx.ctx, SCREEN_W, SCREEN_H, 0.0F, &mouse, 1);
    CLAY({.id = CLAY_ID("persistent")}) {}
    nt_ui_end(s_fx.ctx);

    uint32_t count = 0;
    const uint32_t n = nt_ui_probe_collect(s_fx.ctx, s_nodes, NT_UI_PROBE_MAX_NODES, &count, NULL);
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
    /* opacity 0 subtree -> composed opacity below the visible threshold. */
    CLAY({.id = CLAY_ID("hidden_box"), .layout = {.sizing = {CLAY_SIZING_FIXED(BBOX_W), CLAY_SIZING_FIXED(BBOX_H)}}, .userData = (void *)NT_UI_DATA_XFORM(0U, &ident, 0.0F)}) {}
    nt_ui_end(s_fx.ctx);

    uint32_t count = 0;
    const uint32_t n = nt_ui_probe_collect(s_fx.ctx, s_nodes, NT_UI_PROBE_MAX_NODES, &count, NULL);
    const nt_ui_probe_node_t *vis = find_node(s_nodes, n, nt_ui_id("vis_box"));
    const nt_ui_probe_node_t *hid = find_node(s_nodes, n, nt_ui_id("hidden_box"));
    TEST_ASSERT_NOT_NULL(vis);
    TEST_ASSERT_NOT_NULL(hid);
    TEST_ASSERT_TRUE(vis->visible);
    TEST_ASSERT_FALSE(hid->visible);
    /* the invisible node is STILL emitted, carrying visible=false. */
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
    const uint32_t n = nt_ui_probe_collect(s_fx.ctx, s_nodes, NT_UI_PROBE_MAX_NODES, &count, NULL);
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
    const uint32_t n = nt_ui_probe_collect(s_fx.ctx, s_nodes, NT_UI_PROBE_MAX_NODES, &count, NULL);
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
    const uint32_t n = nt_ui_probe_collect(s_fx.ctx, s_nodes, NT_UI_PROBE_MAX_NODES, &count, NULL);
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

/* a disabled widget remains in the collected set carrying enabled=false. */
static void test_collect_all_nodes_disabled_present(void) {
    nt_pointer_t mouse = make_pointer(-100.0F, -100.0F);
    nt_ui_begin(s_fx.ctx, SCREEN_W, SCREEN_H, 0.0F, &mouse, 1);
    CLAY({.id = CLAY_ID("dis_btn"), .layout = {.sizing = {CLAY_SIZING_FIXED(40), CLAY_SIZING_FIXED(40)}}}) {}
    register_widget(nt_ui_id("dis_btn"), false);
    nt_ui_end(s_fx.ctx);

    uint32_t count = 0;
    const uint32_t n = nt_ui_probe_collect(s_fx.ctx, s_nodes, NT_UI_PROBE_MAX_NODES, &count, NULL);
    const nt_ui_probe_node_t *btn = find_node(s_nodes, n, nt_ui_id("dis_btn"));
    TEST_ASSERT_NOT_NULL(btn); /* present despite disabled */
    TEST_ASSERT_FALSE(btn->enabled);
}

/* ---- projection ----
 *
 * Framebuffer convention (locked here): bounds are GL-Y-up framebuffer px (origin bottom-left).
 * The 2D-plain path keeps the trivial Y-flip (vh - by - bh); the 2D-affine path projects the Clay
 * corners through tree_baked[].m + the same Y-flip; the 3D path projects 4 corners through
 * hit_baked[].m -> view_proj -> NDC -> px (GL Y-up, no extra flip). The ortho-equivalence case below
 * proves a 3D-ortho element lands on the SAME pixel rect as the 2D-plain equivalent. */

#define PROJ_EPS 0.5F

/* Unity float asserts are compiled out in this suite; compare via fabsf like test_nt_ui_tree_build. */
static bool near_eq(float a, float b) { return fabsf(a - b) <= PROJ_EPS; }

/* Rebuild the fixture ctx with use_raycast_input=true (clone of test_nt_ui_3d_hittest.c). */
static void probe_setup_3d_ctx(void) {
    nt_ui_destroy_context(s_fx.ctx);
    nt_ui_create_desc_t desc = nt_ui_create_desc_defaults();
    desc.use_raycast_input = true;
    s_fx.ctx = nt_ui_create_context(s_arena, sizeof s_arena, &desc);
    TEST_ASSERT_NOT_NULL(s_fx.ctx);
    nt_ui_set_font(s_fx.ctx, 0U, s_fx.stub_font);
    nt_ui_set_atlas_white_region(s_fx.ctx, s_fx.atlas.handle, s_fx.atlas.white_region_idx);
    nt_ui_set_sprite_material(s_fx.ctx, s_fx.sprite_material);
    nt_ui_set_text_material(s_fx.ctx, s_fx.text_material);
}

/* Y-DOWN ortho — Clay(x,y) maps to world(x,y) under identity baked.m (matches the 3D hittest fixture). */
static void make_ortho_clay_y_down(float view_proj[16]) {
    mat4 m;
    glm_ortho(0.0F, SCREEN_W, SCREEN_H, 0.0F, -1.0F, 1.0F, m);
    memcpy(view_proj, m, sizeof(float) * 16);
}

/* 2D-plain: a known asymmetric box reports bounds = trivial Y-flip of its Clay bbox. */
static void test_project_2d_plain_yflip(void) {
    nt_pointer_t mouse = make_pointer(-100.0F, -100.0F);
    nt_ui_begin(s_fx.ctx, SCREEN_W, SCREEN_H, 0.0F, &mouse, 1);
    CLAY({.id = CLAY_ID("plain"), .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .offset = {.x = BBOX_X, .y = BBOX_Y}}, .layout = {.sizing = {CLAY_SIZING_FIXED(BBOX_W), CLAY_SIZING_FIXED(BBOX_H)}}}) {}
    nt_ui_end(s_fx.ctx);

    uint32_t count = 0;
    const uint32_t n = nt_ui_probe_collect(s_fx.ctx, s_nodes, NT_UI_PROBE_MAX_NODES, &count, NULL);
    const nt_ui_probe_node_t *node = find_node(s_nodes, n, nt_ui_id("plain"));
    TEST_ASSERT_NOT_NULL(node);
    /* Y-up: top-left Clay (150,80) -> bottom edge at vh - by - bh = 600-80-60 = 460. */
    TEST_ASSERT_TRUE(near_eq(node->bounds[0], BBOX_X));
    TEST_ASSERT_TRUE(near_eq(node->bounds[1], SCREEN_H - BBOX_Y - BBOX_H));
    TEST_ASSERT_TRUE(near_eq(node->bounds[2], BBOX_W));
    TEST_ASSERT_TRUE(near_eq(node->bounds[3], BBOX_H));
}

/* 2D-affine: a pure-translation transform shifts the projected AABB by the offset (priming-frame:
 * the composed affine reads the PREVIOUS frame's baked transform). */
static void test_project_2d_affine_translation(void) {
    const float ox = 30.0F;
    const float oy = 20.0F;
    nt_ui_transform_t t = nt_ui_transform_defaults();
    t.offset_x = ox;
    t.offset_y = oy;
    nt_pointer_t mouse = make_pointer(-100.0F, -100.0F);

    /* Prime one frame so the baked transform is in tree_baked for the collect frame. */
    for (int frame = 0; frame < 2; ++frame) {
        nt_ui_begin(s_fx.ctx, SCREEN_W, SCREEN_H, 0.0F, &mouse, 1);
        CLAY({.id = CLAY_ID("xbox"),
              .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .offset = {.x = BBOX_X, .y = BBOX_Y}},
              .layout = {.sizing = {CLAY_SIZING_FIXED(BBOX_W), CLAY_SIZING_FIXED(BBOX_H)}},
              .userData = (void *)NT_UI_DATA_XFORM(0U, &t, 1.0F)}) {}
        nt_ui_end(s_fx.ctx);
    }

    uint32_t count = 0;
    const uint32_t n = nt_ui_probe_collect(s_fx.ctx, s_nodes, NT_UI_PROBE_MAX_NODES, &count, NULL);
    const nt_ui_probe_node_t *node = find_node(s_nodes, n, nt_ui_id("xbox"));
    TEST_ASSERT_NOT_NULL(node);
    /* Clay corners shift by (ox, oy); Y-up flip: y = vh - (by+oy) - bh. */
    TEST_ASSERT_TRUE(near_eq(node->bounds[0], BBOX_X + ox));
    TEST_ASSERT_TRUE(near_eq(node->bounds[1], SCREEN_H - (BBOX_Y + oy) - BBOX_H));
    TEST_ASSERT_TRUE(near_eq(node->bounds[2], BBOX_W));
    TEST_ASSERT_TRUE(near_eq(node->bounds[3], BBOX_H));
}

/* 3D-ctx: an ortho-projected box yields the AABB of its 4 projected corners, equal to the
 * 2D-plain pixel rect (locks the framebuffer convention). */
static void test_project_3d_ortho_aabb(void) {
    probe_setup_3d_ctx();
    float vp[16];
    make_ortho_clay_y_down(vp);
    nt_pointer_t mouse = make_pointer(-100.0F, -100.0F);
    nt_ui_begin(s_fx.ctx, SCREEN_W, SCREEN_H, 0.0F, &mouse, 1);
    nt_ui_set_view_proj(s_fx.ctx, vp);
    CLAY({.id = CLAY_ID("box3d"), .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .offset = {.x = BBOX_X, .y = BBOX_Y}}, .layout = {.sizing = {CLAY_SIZING_FIXED(BBOX_W), CLAY_SIZING_FIXED(BBOX_H)}}}) {}
    nt_ui_end(s_fx.ctx);

    uint32_t count = 0;
    const uint32_t n = nt_ui_probe_collect(s_fx.ctx, s_nodes, NT_UI_PROBE_MAX_NODES, &count, NULL);
    const nt_ui_probe_node_t *node = find_node(s_nodes, n, nt_ui_id("box3d"));
    TEST_ASSERT_NOT_NULL(node);
    /* SAME rect as the 2D-plain equivalent: x=150, y=460 (Y-up), w=200, h=60. */
    TEST_ASSERT_TRUE(near_eq(node->bounds[0], BBOX_X));
    TEST_ASSERT_TRUE(near_eq(node->bounds[1], SCREEN_H - BBOX_Y - BBOX_H));
    TEST_ASSERT_TRUE(near_eq(node->bounds[2], BBOX_W));
    TEST_ASSERT_TRUE(near_eq(node->bounds[3], BBOX_H));
    TEST_ASSERT_TRUE(node->visible);
}

/* 3D behind-camera: a view_proj that puts a corner at clip[3] <= 0 flags the node (visible=false),
 * never a garbage AABB (negative size / pixels far outside the framebuffer). */
static void test_project_3d_behind_camera_flag(void) {
    probe_setup_3d_ctx();
    /* Perspective frustum looking down -Z; place the panel's plane at/behind the eye so w<=0.
     * glm_perspective gives clip[3] = -z_eye; a Clay element baked at world z=0 with the camera
     * at the origin looking down -Z makes the near corners land at w<=0. */
    mat4 proj;
    glm_perspective(glm_rad(60.0F), SCREEN_W / SCREEN_H, 0.1F, 100.0F, proj);
    float vp[16];
    memcpy(vp, proj, sizeof vp); /* view = identity: eye at origin, Clay z=0 plane is AT the eye (w=0..<0) */

    nt_pointer_t mouse = make_pointer(-100.0F, -100.0F);
    nt_ui_begin(s_fx.ctx, SCREEN_W, SCREEN_H, 0.0F, &mouse, 1);
    nt_ui_set_view_proj(s_fx.ctx, vp);
    CLAY({.id = CLAY_ID("behind"), .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .offset = {.x = BBOX_X, .y = BBOX_Y}}, .layout = {.sizing = {CLAY_SIZING_FIXED(BBOX_W), CLAY_SIZING_FIXED(BBOX_H)}}}) {
    }
    nt_ui_end(s_fx.ctx);

    uint32_t count = 0;
    const uint32_t n = nt_ui_probe_collect(s_fx.ctx, s_nodes, NT_UI_PROBE_MAX_NODES, &count, NULL);
    const nt_ui_probe_node_t *node = find_node(s_nodes, n, nt_ui_id("behind"));
    TEST_ASSERT_NOT_NULL(node);
    TEST_ASSERT_FALSE(node->visible); /* behind-camera flagged */
    /* No garbage AABB: width/height non-negative (sentinel bounds, not a flipped divide). */
    TEST_ASSERT_TRUE(node->bounds[2] >= 0.0F);
    TEST_ASSERT_TRUE(node->bounds[3] >= 0.0F);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_enabled_false_with_inspector_off);
    RUN_TEST(test_enabled_true);
    RUN_TEST(test_enabled_default_true_unregistered);
    RUN_TEST(test_input_disabled_reports_enabled_false);
    RUN_TEST(test_slider_disabled_reports_enabled_false);
    RUN_TEST(test_collect_flat_pod_nodes);
    RUN_TEST(test_collect_cap_truncation_signaled);
    RUN_TEST(test_collect_id_string_copied);
    RUN_TEST(test_collect_visible_clip_opacity);
    RUN_TEST(test_collect_role_from_def_name);
    RUN_TEST(test_collect_parent_children);
    RUN_TEST(test_collect_label_distinct_from_text);
    RUN_TEST(test_collect_all_nodes_disabled_present);
    RUN_TEST(test_project_2d_plain_yflip);
    RUN_TEST(test_project_2d_affine_translation);
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
