/* devapi ui.* group over a REAL headless nt_ui ctx: a real probe-able tree registered via
   nt_devapi_ui_register_context, driven through nt_devapi_submit + JSON asserts. The whole body is
   NT_UI_DEBUG_TOOLS-gated (ui.tree/ui.element need the real nt_ui_probe_collect, else a 0-node stub);
   OFF builds compile to a passing stub main so compile_commands.json / tidy stay complete. */

/* System headers before Unity to avoid noreturn / __declspec conflict on MSVC */
#include <math.h>
#include <stdint.h>
#include <string.h>

/* clang-format off */
#include "clay.h"
#include "app/nt_app.h"
#include "devapi/nt_devapi.h"
#include "devapi/nt_devapi_internal.h"
#include "devapi/nt_devapi_net.h"      /* nt_devapi_update — drives the shared inject schedule on a sim-advance */
#include "input/nt_input.h"            /* g_nt_input + the reserved inject pointer id, for the Y-up flip read-back */
#include "memory/nt_mem_scratch.h"     /* CLAY per-element data backing; reset between frames */
#include "test_helpers/ui_test_arena.h"
#include "test_helpers/ui_walker_fixture.h"
#include "ui/nt_ui.h"
#include "ui/nt_ui_button.h"           /* NT_UI_BUTTON_DEF for the registered-widget role */
#include "ui/nt_ui_internal.h"
#include "window/nt_window.h"          /* g_nt_window fb_* + dpr pinned for deterministic metadata */
#include "unity.h"
/* clang-format on */

#if NT_UI_DEBUG_TOOLS

/* A known asymmetric widget rect so axis-swap / Y-flip bugs surface. Clay (top-left, Y-down):
   "widget" sits at (150,80) sized 200x60 inside an 800x600 layout. Floating-attached to the root so
   the layout solve places it at exactly (BBOX_X,BBOX_Y) with no padding/gap interference. */
#define LAYOUT_W 800.0F
#define LAYOUT_H 600.0F
#define BBOX_X 150.0F
#define BBOX_Y 80.0F
#define BBOX_W 200.0F
#define BBOX_H 60.0F

/* Y-up (origin bottom-left) bottom edge of the rect: layout_h - clay_y - clay_h. This is what the
   probe bounds report AND what a bot feeds straight back into ui.click({x,y}). */
#define YUP_Y (LAYOUT_H - BBOX_Y - BBOX_H)

/* The string-id / device path lands the pointer at the bbox CENTER in Clay/device space (Y-down).
   The {x,y} Y-up path must resolve to this SAME device Y after its one documented flip. */
#define DEVICE_CENTER_Y (BBOX_Y + (BBOX_H * 0.5F))
#define DEVICE_CENTER_X (BBOX_X + (BBOX_W * 0.5F))

/* ui.drag's per-command DoS cap is private to nt_devapi_ui.c (NT_DEVAPI_INPUT_SCHED_MAX - 2, leaving
   room for DOWN+UP); re-derive it here from the public scheduler cap so the over-cap / at-cap cases
   track the real bound. */
#define UI_DRAG_FRAMES_MAX (NT_DEVAPI_INPUT_SCHED_MAX - 2U)

alignas(NT_UI_ARENA_ALIGN) static uint8_t s_arena[NT_UI_TEST_ARENA_SIZE];
static ui_walker_fixture_t s_fx;

/* A second, SCALED ctx (letterbox-like: offset + 2x scale) registered as "hud_scaled". Proves
   ui.click on a non-identity ctx viewport lands: resolve_target maps the LAYOUT coord through the ctx
   viewport (nt_ui_layout_to_screen) so the injected pointer is a real DEVICE coord. layout 800x600 ->
   device content rect {SCALED_VP_X, SCALED_VP_Y, 800*SCALE, 600*SCALE}. */
#define SCALED_VP_X 100.0F
#define SCALED_VP_Y 50.0F
#define SCALED_SCALE 2.0F
/* Expected layout->device map: device = vp.offset + layout * scale (vp.w/lw == vp.h/lh == SCALE). */
#define SCALED_DEVICE_CENTER_X (SCALED_VP_X + (DEVICE_CENTER_X * SCALED_SCALE))
#define SCALED_DEVICE_CENTER_Y (SCALED_VP_Y + (DEVICE_CENTER_Y * SCALED_SCALE))

alignas(NT_UI_ARENA_ALIGN) static uint8_t s_scaled_arena[NT_UI_TEST_ARENA_SIZE];
static nt_ui_context_t *s_scaled_ctx;

/* A third ctx that is NEVER nt_ui_begin'd: begin_w stays 0, so the converters would trap. resolve_ctx
   must reject it with bad_params BEFORE any coord conversion (the wire-input-crash regression). */
alignas(NT_UI_ARENA_ALIGN) static uint8_t s_neverbegun_arena[NT_UI_TEST_ARENA_SIZE];
static nt_ui_context_t *s_neverbegun_ctx;

/* Declare the same known widget in the scaled ctx, overriding the per-frame identity viewport with a
   non-identity (offset + scale) one BEFORE the first hit-test (the lazy resolve window). */
static void declare_scaled_tree(void) {
    nt_mem_scratch_reset();
    nt_pointer_t mouse = {0};
    mouse.x = -100.0F;
    mouse.y = -100.0F;
    nt_ui_begin(s_scaled_ctx, LAYOUT_W, LAYOUT_H, 0.0F, &mouse, 1);
    nt_ui_set_viewport(s_scaled_ctx, (nt_ui_viewport_t){.x = SCALED_VP_X, .y = SCALED_VP_Y, .w = LAYOUT_W * SCALED_SCALE, .h = LAYOUT_H * SCALED_SCALE});
    CLAY({.id = CLAY_ID("widget"), .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .offset = {.x = BBOX_X, .y = BBOX_Y}}, .layout = {.sizing = {CLAY_SIZING_FIXED(BBOX_W), CLAY_SIZING_FIXED(BBOX_H)}}}) {
    }
    nt_ui_widget_register(s_scaled_ctx, nt_ui_id("widget"), &NT_UI_BUTTON_DEF, NULL, true);
    nt_ui_end(s_scaled_ctx);
}

/* Declare the known tree for one frame so the probe + get_bbox see it the NEXT read. ui.tree/element
   read the LAST completed frame, and nt_ui_get_bbox reads the immediately-preceding frame's bbox, so
   every test re-declares before asserting. */
static void declare_tree(void) {
    nt_mem_scratch_reset();
    nt_pointer_t mouse = {0};
    mouse.x = -100.0F;
    mouse.y = -100.0F;
    nt_ui_begin(s_fx.ctx, LAYOUT_W, LAYOUT_H, 0.0F, &mouse, 1);
    CLAY({.id = CLAY_ID("widget"), .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .offset = {.x = BBOX_X, .y = BBOX_Y}}, .layout = {.sizing = {CLAY_SIZING_FIXED(BBOX_W), CLAY_SIZING_FIXED(BBOX_H)}}}) {
    }
    nt_ui_widget_register(s_fx.ctx, nt_ui_id("widget"), &NT_UI_BUTTON_DEF, NULL, true);
    nt_ui_end(s_fx.ctx);
}

/* A pure-translation 2D transform for the "read==id-write under transform" case. The probe projects
   the Clay corners through tree_baked.m, so the reported bounds (and ui.click(id)) shift by (ox,oy) —
   whereas the untransformed Clay bbox does NOT. Offset chosen asymmetric so an axis swap would surface. */
#define XFORM_OX 40.0F
#define XFORM_OY (-25.0F)

/* Declare a SECOND widget "xwidget" in the hud ctx carrying a translation transform, primed over two
   frames so tree_baked holds the composed affine for the collect frame (the probe reads the PREVIOUS
   frame's baked transform — mirrors test_nt_ui_probe's 2D-affine fixture). */
static void declare_xform_tree(void) {
    nt_ui_transform_t t = nt_ui_transform_defaults();
    t.offset_x = XFORM_OX;
    t.offset_y = XFORM_OY;
    for (int frame = 0; frame < 2; ++frame) {
        nt_mem_scratch_reset();
        nt_pointer_t mouse = {0};
        mouse.x = -100.0F;
        mouse.y = -100.0F;
        nt_ui_begin(s_fx.ctx, LAYOUT_W, LAYOUT_H, 0.0F, &mouse, 1);
        CLAY({.id = CLAY_ID("xwidget"),
              .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .offset = {.x = BBOX_X, .y = BBOX_Y}},
              .layout = {.sizing = {CLAY_SIZING_FIXED(BBOX_W), CLAY_SIZING_FIXED(BBOX_H)}},
              .userData = (void *)NT_UI_DATA_XFORM(0U, &t, 1.0F)}) {}
        nt_ui_end(s_fx.ctx);
    }
}

void setUp(void) {
    /* Real headless ctx + renderer chain (no window/GLFW); also nt_mem_scratch_init for CLAY data. */
    ui_walker_fixture_init(&s_fx, s_arena, sizeof s_arena, UI_WALKER_FX_BIND_ALL);

    /* nt_devapi_init re-registers the ui group, which CLEARS the host ctx table — so register the
       ctx AFTER init, every test, or ui.* would see an empty table. */
    TEST_ASSERT_EQUAL(NT_OK, nt_devapi_init());
    nt_input_init();
    nt_devapi_ui_register_context("hud", s_fx.ctx);

    /* Bare second ctx (no atlas/material — probe + bbox + viewport need no walk) registered scaled. */
    const nt_ui_create_desc_t scaled_desc = nt_ui_create_desc_defaults();
    s_scaled_ctx = nt_ui_create_context(s_scaled_arena, sizeof s_scaled_arena, &scaled_desc);
    TEST_ASSERT_NOT_NULL(s_scaled_ctx);
    nt_devapi_ui_register_context("hud_scaled", s_scaled_ctx);

    /* Third ctx registered but DELIBERATELY never nt_ui_begin'd (begin_w == 0): the wire-input-crash
       regression target. resolve_ctx must reject it before any converter runs. */
    s_neverbegun_ctx = nt_ui_create_context(s_neverbegun_arena, sizeof s_neverbegun_arena, &scaled_desc);
    TEST_ASSERT_NOT_NULL(s_neverbegun_ctx);
    nt_devapi_ui_register_context("hud_neverbegun", s_neverbegun_ctx);

    /* Pin the window so the dpr metadata + any fb-derived basis is deterministic (the headless ctx
       layout size is LAYOUT_W/H from nt_ui_begin, independent of fb_*; dpr still reads g_nt_window). */
    g_nt_window.fb_width = (uint32_t)LAYOUT_W;
    g_nt_window.fb_height = (uint32_t)LAYOUT_H;
    g_nt_window.dpr = 1.0F;

    declare_tree();
    declare_scaled_tree();
}

void tearDown(void) {
    nt_devapi_shutdown();
    nt_ui_destroy_context(s_neverbegun_ctx);
    s_neverbegun_ctx = NULL;
    nt_ui_destroy_context(s_scaled_ctx);
    s_scaled_ctx = NULL;
    ui_walker_fixture_shutdown(&s_fx);
}

/* ---- dispatch harness (clone of test_devapi_input.c) ---- */

static void assert_bad_params(const char *resp) {
    TEST_ASSERT_NOT_NULL(resp);
    cJSON *root = cJSON_Parse(resp);
    TEST_ASSERT_NOT_NULL(root);
    TEST_ASSERT_TRUE(cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(root, "ok")));
    cJSON *err = cJSON_GetObjectItemCaseSensitive(root, "error");
    TEST_ASSERT_EQUAL_STRING("bad_params", cJSON_GetObjectItemCaseSensitive(err, "code")->valuestring);
    cJSON_Delete(root);
}

static cJSON *parse_ok(const char *resp) {
    TEST_ASSERT_NOT_NULL(resp);
    cJSON *root = cJSON_Parse(resp);
    TEST_ASSERT_NOT_NULL(root);
    TEST_ASSERT_TRUE(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(root, "ok")));
    return root;
}

/* The result object of an ok envelope (borrowed; caller deletes root). */
static cJSON *ok_result(cJSON *root) { return cJSON_GetObjectItemCaseSensitive(root, "result"); }

/* One real sim-advance: bump the frame, run devapi update (releases due schedule entries into the
   inject buffer), poll to apply. Lets a ui.click's scheduled DOWN land in g_nt_input so the resolved
   device coord is observable. */
static void advance(void) {
    g_nt_app.frame++;
    nt_devapi_update();
    nt_input_poll();
}

/* The reserved synthetic-mouse slot after a release, or NULL if it never landed. */
static nt_pointer_t *inject_slot(void) {
    for (int i = 0; i < NT_INPUT_MAX_POINTERS; i++) {
        if (g_nt_input.pointers[i].active && g_nt_input.pointers[i].id == NT_INPUT_INJECT_POINTER_ID_BASE) {
            return &g_nt_input.pointers[i];
        }
    }
    return NULL;
}

static bool near_eq(float a, float b) { return fabsf(a - b) <= 0.5F; }

/* ---- ui.contexts ---- */

/* The registered "hud" + "hud_scaled" + "hud_neverbegun" context names surface in registration order. */
static void test_contexts_lists_registered(void) {
    cJSON *root = parse_ok(nt_devapi_submit("{\"method\":\"ui.contexts\",\"params\":{}}"));
    cJSON *arr = cJSON_GetObjectItemCaseSensitive(ok_result(root), "contexts");
    TEST_ASSERT_TRUE(cJSON_IsArray(arr));
    TEST_ASSERT_EQUAL_INT(3, cJSON_GetArraySize(arr));
    TEST_ASSERT_EQUAL_STRING("hud", cJSON_GetArrayItem(arr, 0)->valuestring);
    TEST_ASSERT_EQUAL_STRING("hud_scaled", cJSON_GetArrayItem(arr, 1)->valuestring);
    TEST_ASSERT_EQUAL_STRING("hud_neverbegun", cJSON_GetArrayItem(arr, 2)->valuestring);
    cJSON_Delete(root);
}

/* ---- ui.tree: metadata + Y-up bounds ---- */

/* Find the {x,y,w,h} bounds object of node `id_string` in a ui.tree nodes array; NULL if absent. */
static cJSON *find_node_bounds(cJSON *nodes, const char *id_string) {
    uint32_t want = nt_ui_id(id_string);
    cJSON *n = NULL;
    cJSON_ArrayForEach(n, nodes) {
        const cJSON *idj = cJSON_GetObjectItemCaseSensitive(n, "id");
        if (cJSON_IsNumber(idj) && (uint32_t)idj->valuedouble == want) {
            return cJSON_GetObjectItemCaseSensitive(n, "bounds");
        }
    }
    return NULL;
}

/* ui.tree emits the DECLARED Y-up contract metadata + the known node at its Y-up bounds. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_tree_meta_and_yup_bounds(void) {
    cJSON *root = parse_ok(nt_devapi_submit("{\"method\":\"ui.tree\",\"params\":{}}"));
    cJSON *result = ok_result(root);

    /* The contract block: Y-up, origin bottom-left, layout dims, dpr, 2D projection. */
    TEST_ASSERT_EQUAL_STRING("ui", cJSON_GetObjectItemCaseSensitive(result, "space")->valuestring);
    TEST_ASSERT_EQUAL_STRING("bottom-left", cJSON_GetObjectItemCaseSensitive(result, "origin")->valuestring);
    TEST_ASSERT_EQUAL_STRING("up", cJSON_GetObjectItemCaseSensitive(result, "y_axis")->valuestring);
    TEST_ASSERT_TRUE(near_eq((float)cJSON_GetObjectItemCaseSensitive(result, "width")->valuedouble, LAYOUT_W));
    TEST_ASSERT_TRUE(near_eq((float)cJSON_GetObjectItemCaseSensitive(result, "height")->valuedouble, LAYOUT_H));
    TEST_ASSERT_TRUE(near_eq((float)cJSON_GetObjectItemCaseSensitive(result, "dpr")->valuedouble, 1.0F));
    TEST_ASSERT_EQUAL_STRING("2d", cJSON_GetObjectItemCaseSensitive(result, "projection")->valuestring);

    cJSON *nodes = cJSON_GetObjectItemCaseSensitive(result, "nodes");
    TEST_ASSERT_TRUE(cJSON_IsArray(nodes));
    cJSON *b = find_node_bounds(nodes, "widget");
    TEST_ASSERT_NOT_NULL(b);
    /* Y-up: x passes through, y = layout_h - clay_y - h, w/h unchanged. */
    TEST_ASSERT_TRUE(near_eq((float)cJSON_GetObjectItemCaseSensitive(b, "x")->valuedouble, BBOX_X));
    TEST_ASSERT_TRUE(near_eq((float)cJSON_GetObjectItemCaseSensitive(b, "y")->valuedouble, YUP_Y));
    TEST_ASSERT_TRUE(near_eq((float)cJSON_GetObjectItemCaseSensitive(b, "w")->valuedouble, BBOX_W));
    TEST_ASSERT_TRUE(near_eq((float)cJSON_GetObjectItemCaseSensitive(b, "h")->valuedouble, BBOX_H));
    cJSON_Delete(root);
}

/* ---- ui.element ---- */

static void test_element_by_id_ok(void) {
    cJSON *root = parse_ok(nt_devapi_submit("{\"method\":\"ui.element\",\"params\":{\"id\":\"widget\"}}"));
    cJSON *node = cJSON_GetObjectItemCaseSensitive(ok_result(root), "node");
    TEST_ASSERT_NOT_NULL(node);
    TEST_ASSERT_EQUAL_STRING("widget", cJSON_GetObjectItemCaseSensitive(node, "id_string")->valuestring);
    cJSON_Delete(root);
}

static void test_element_unknown_id_bad_params(void) { assert_bad_params(nt_devapi_submit("{\"method\":\"ui.element\",\"params\":{\"id\":\"nope\"}}")); }

static void test_element_non_string_id_bad_params(void) { assert_bad_params(nt_devapi_submit("{\"method\":\"ui.element\",\"params\":{\"id\":42}}")); }

/* ---- ui.click string-id ---- */

static void test_click_string_id_queues_two(void) {
    cJSON *root = parse_ok(nt_devapi_submit("{\"method\":\"ui.click\",\"params\":{\"id\":\"widget\"}}"));
    TEST_ASSERT_EQUAL_INT(2, cJSON_GetObjectItemCaseSensitive(ok_result(root), "queued")->valueint);
    cJSON_Delete(root);
}

static void test_click_unknown_id_bad_params(void) { assert_bad_params(nt_devapi_submit("{\"method\":\"ui.click\",\"params\":{\"id\":\"nope\"}}")); }

static void test_click_empty_id_bad_params(void) { assert_bad_params(nt_devapi_submit("{\"method\":\"ui.click\",\"params\":{\"id\":\"\"}}")); }

/* A collapsed (zero-size) node has bounds {.,.,0,0}; ui.click(id) must reject it (bad_params) rather
   than resolve its center to a screen corner — the behind-camera / collapsed-element guard. */
static void declare_collapsed_tree(void) {
    nt_mem_scratch_reset();
    nt_pointer_t mouse = {0};
    mouse.x = -100.0F;
    mouse.y = -100.0F;
    nt_ui_begin(s_fx.ctx, LAYOUT_W, LAYOUT_H, 0.0F, &mouse, 1);
    CLAY({.id = CLAY_ID("collapsed"), .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .offset = {.x = BBOX_X, .y = BBOX_Y}}, .layout = {.sizing = {CLAY_SIZING_FIXED(0), CLAY_SIZING_FIXED(0)}}}) {}
    nt_ui_widget_register(s_fx.ctx, nt_ui_id("collapsed"), &NT_UI_BUTTON_DEF, NULL, true);
    nt_ui_end(s_fx.ctx);
}

static void test_click_collapsed_id_bad_params(void) {
    declare_collapsed_tree();
    assert_bad_params(nt_devapi_submit("{\"method\":\"ui.click\",\"params\":{\"id\":\"collapsed\"}}"));
}

/* String-id click resolves to the bbox CENTER in device (Y-down) space — the reference the {x,y}
   Y-up flip must match. Read it back from the injected pointer slot after an advance. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_click_string_id_lands_at_device_center(void) {
    cJSON_Delete(parse_ok(nt_devapi_submit("{\"method\":\"ui.click\",\"params\":{\"id\":\"widget\"}}")));
    advance();
    nt_pointer_t *slot = inject_slot();
    TEST_ASSERT_NOT_NULL(slot);
    TEST_ASSERT_TRUE(near_eq(slot->x, DEVICE_CENTER_X));
    TEST_ASSERT_TRUE(near_eq(slot->y, DEVICE_CENTER_Y));
    TEST_ASSERT_TRUE(nt_input_mouse_is_down(NT_BUTTON_LEFT));
}

/* ---- ui.click {x,y} Y-up flip — THE original-bug regression ---- */

/* Feed the node's Y-up bounds CENTER straight into ui.click({x,y}); after the one documented
   Y-up->device flip it must land at the SAME device point as the string-id path (read==write). */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_click_xy_yup_flip_matches_string_id(void) {
    /* Y-up center of the known rect: x = bbox center, y = (layout_h - clay_y - h) + h/2. */
    const float yup_cx = DEVICE_CENTER_X;
    const float yup_cy = YUP_Y + (BBOX_H * 0.5F);
    char req[160];
    (void)snprintf(req, sizeof req, "{\"method\":\"ui.click\",\"params\":{\"id\":{\"x\":%.1f,\"y\":%.1f}}}", (double)yup_cx, (double)yup_cy);
    cJSON_Delete(parse_ok(nt_devapi_submit(req)));
    advance();
    nt_pointer_t *slot = inject_slot();
    TEST_ASSERT_NOT_NULL(slot);
    /* device_y == layout_h - y_up == the string-id bbox-center device Y. */
    TEST_ASSERT_TRUE(near_eq(slot->x, DEVICE_CENTER_X));
    TEST_ASSERT_TRUE(near_eq(slot->y, DEVICE_CENTER_Y));
    TEST_ASSERT_TRUE(near_eq(slot->y, LAYOUT_H - yup_cy));
    TEST_ASSERT_TRUE(nt_input_mouse_is_down(NT_BUTTON_LEFT));
}

/* ---- TRANSFORMED widget: ui.click(id) targets the PROJECTED bounds center (read==id-write) ---- */

/* A widget with a NON-IDENTITY 2D transform: ui.click(id) must resolve to the SAME projected position
   ui.tree reports for that id — NOT the untransformed Clay layout center (read==id-write). Asserts both
   the resolved device point and a begin+step round-trip that the transform-aware hit-test really hits it. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_transformed_click_id_targets_projected_center(void) {
    declare_xform_tree();

    /* (1) read side: the ui.tree bounds center for xwidget (Y-up layout px), then the documented
       Y-up->device map (identity hud viewport => device == flipped layout). */
    cJSON *troot = parse_ok(nt_devapi_submit("{\"method\":\"ui.tree\",\"params\":{}}"));
    cJSON *b = find_node_bounds(cJSON_GetObjectItemCaseSensitive(ok_result(troot), "nodes"), "xwidget");
    TEST_ASSERT_NOT_NULL(b);
    const float yup_cx = (float)cJSON_GetObjectItemCaseSensitive(b, "x")->valuedouble + ((float)cJSON_GetObjectItemCaseSensitive(b, "w")->valuedouble * 0.5F);
    const float yup_cy = (float)cJSON_GetObjectItemCaseSensitive(b, "y")->valuedouble + ((float)cJSON_GetObjectItemCaseSensitive(b, "h")->valuedouble * 0.5F);
    cJSON_Delete(troot);
    const float expect_dev_x = yup_cx;            /* identity viewport: device x == layout x */
    const float expect_dev_y = LAYOUT_H - yup_cy; /* Y-up -> Y-down flip, identity viewport */
    /* The projected center MUST differ from the untransformed Clay center (else the test is vacuous). */
    TEST_ASSERT_TRUE(near_eq(expect_dev_x, DEVICE_CENTER_X + XFORM_OX));
    TEST_ASSERT_TRUE(near_eq(expect_dev_y, DEVICE_CENTER_Y + XFORM_OY));

    /* (2) id-write side: ui.click(xwidget) injects exactly that projected device center. */
    cJSON_Delete(parse_ok(nt_devapi_submit("{\"method\":\"ui.click\",\"params\":{\"id\":\"xwidget\"}}")));
    advance();
    nt_pointer_t *slot = inject_slot();
    TEST_ASSERT_NOT_NULL(slot);
    TEST_ASSERT_TRUE(near_eq(slot->x, expect_dev_x));
    TEST_ASSERT_TRUE(near_eq(slot->y, expect_dev_y));

    /* Round-trip: the injected device point, device->layout, hits the widget through its transform-aware
       hit-test (re-declare the transform this frame so tree_baked carries it for the hit). */
    nt_ui_transform_t t = nt_ui_transform_defaults();
    t.offset_x = XFORM_OX;
    t.offset_y = XFORM_OY;
    nt_mem_scratch_reset();
    nt_pointer_t mouse = {0};
    mouse.x = -100.0F;
    mouse.y = -100.0F;
    nt_ui_begin(s_fx.ctx, LAYOUT_W, LAYOUT_H, 0.0F, &mouse, 1);
    CLAY({.id = CLAY_ID("xwidget"),
          .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .offset = {.x = BBOX_X, .y = BBOX_Y}},
          .layout = {.sizing = {CLAY_SIZING_FIXED(BBOX_W), CLAY_SIZING_FIXED(BBOX_H)}},
          .userData = (void *)NT_UI_DATA_XFORM(0U, &t, 1.0F)}) {}
    const float device_pt[2] = {slot->x, slot->y};
    float layout_pt[2];
    nt_ui_screen_to_layout(s_fx.ctx, device_pt, layout_pt);
    bool hit = nt_ui_test_hit(s_fx.ctx, nt_ui_id("xwidget"), layout_pt[0], layout_pt[1]);
    nt_ui_end(s_fx.ctx);
    TEST_ASSERT_TRUE(hit);
}

/* ---- SCALED ctx: ui.click lands through the ctx viewport (the scaled regression) ---- */

/* ui.tree on the scaled ctx exposes the device viewport rect: offset + logical*scale. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_scaled_tree_exposes_viewport(void) {
    cJSON *root = parse_ok(nt_devapi_submit("{\"method\":\"ui.tree\",\"params\":{\"ctx\":\"hud_scaled\"}}"));
    cJSON *vp = cJSON_GetObjectItemCaseSensitive(ok_result(root), "viewport");
    TEST_ASSERT_NOT_NULL(vp);
    TEST_ASSERT_TRUE(near_eq((float)cJSON_GetObjectItemCaseSensitive(vp, "x")->valuedouble, SCALED_VP_X));
    TEST_ASSERT_TRUE(near_eq((float)cJSON_GetObjectItemCaseSensitive(vp, "y")->valuedouble, SCALED_VP_Y));
    TEST_ASSERT_TRUE(near_eq((float)cJSON_GetObjectItemCaseSensitive(vp, "w")->valuedouble, LAYOUT_W * SCALED_SCALE));
    TEST_ASSERT_TRUE(near_eq((float)cJSON_GetObjectItemCaseSensitive(vp, "h")->valuedouble, LAYOUT_H * SCALED_SCALE));
    cJSON_Delete(root);
}

/* The unscaled hud's viewport is the identity {0,0,layout_w,layout_h} (device==layout). */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_unscaled_viewport_is_identity(void) {
    cJSON *root = parse_ok(nt_devapi_submit("{\"method\":\"ui.tree\",\"params\":{}}"));
    cJSON *vp = cJSON_GetObjectItemCaseSensitive(ok_result(root), "viewport");
    TEST_ASSERT_NOT_NULL(vp);
    TEST_ASSERT_TRUE(near_eq((float)cJSON_GetObjectItemCaseSensitive(vp, "x")->valuedouble, 0.0F));
    TEST_ASSERT_TRUE(near_eq((float)cJSON_GetObjectItemCaseSensitive(vp, "y")->valuedouble, 0.0F));
    TEST_ASSERT_TRUE(near_eq((float)cJSON_GetObjectItemCaseSensitive(vp, "w")->valuedouble, LAYOUT_W));
    TEST_ASSERT_TRUE(near_eq((float)cJSON_GetObjectItemCaseSensitive(vp, "h")->valuedouble, LAYOUT_H));
    cJSON_Delete(root);
}

/* String-id ui.click on the scaled ctx injects the bbox center mapped LAYOUT->DEVICE through the
   viewport — NOT the raw layout center (that is the scaled bug this proves fixed). */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_scaled_click_string_id_lands_at_device_center(void) {
    cJSON_Delete(parse_ok(nt_devapi_submit("{\"method\":\"ui.click\",\"params\":{\"id\":\"widget\",\"ctx\":\"hud_scaled\"}}")));
    advance();
    nt_pointer_t *slot = inject_slot();
    TEST_ASSERT_NOT_NULL(slot);
    TEST_ASSERT_TRUE(near_eq(slot->x, SCALED_DEVICE_CENTER_X));
    TEST_ASSERT_TRUE(near_eq(slot->y, SCALED_DEVICE_CENTER_Y));
    /* Round-trip proof: the injected DEVICE coord, converted back to layout via the ctx viewport,
       hit-tests onto the widget. begin reset the viewport to identity, so re-apply the scaled viewport
       before converting the test point device->layout, exactly mirroring the live hit-test path. */
    nt_mem_scratch_reset();
    nt_pointer_t mouse = {0};
    mouse.x = -100.0F;
    mouse.y = -100.0F;
    nt_ui_begin(s_scaled_ctx, LAYOUT_W, LAYOUT_H, 0.0F, &mouse, 1);
    nt_ui_set_viewport(s_scaled_ctx, (nt_ui_viewport_t){.x = SCALED_VP_X, .y = SCALED_VP_Y, .w = LAYOUT_W * SCALED_SCALE, .h = LAYOUT_H * SCALED_SCALE});
    const float device_pt[2] = {slot->x, slot->y};
    float layout_pt[2];
    nt_ui_screen_to_layout(s_scaled_ctx, device_pt, layout_pt);
    bool hit = nt_ui_test_hit(s_scaled_ctx, nt_ui_id("widget"), layout_pt[0], layout_pt[1]);
    nt_ui_end(s_scaled_ctx);
    TEST_ASSERT_TRUE(hit);
}

/* {x,y} (Y-up) ui.click on the scaled ctx: the documented Y-up flip THEN the viewport map -> the SAME
   scaled device center as the string-id path (read-bounds==write, scaled). */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_scaled_click_xy_lands_at_device_center(void) {
    const float yup_cx = DEVICE_CENTER_X;
    const float yup_cy = YUP_Y + (BBOX_H * 0.5F);
    char req[192];
    (void)snprintf(req, sizeof req, "{\"method\":\"ui.click\",\"params\":{\"ctx\":\"hud_scaled\",\"id\":{\"x\":%.1f,\"y\":%.1f}}}", (double)yup_cx, (double)yup_cy);
    cJSON_Delete(parse_ok(nt_devapi_submit(req)));
    advance();
    nt_pointer_t *slot = inject_slot();
    TEST_ASSERT_NOT_NULL(slot);
    TEST_ASSERT_TRUE(near_eq(slot->x, SCALED_DEVICE_CENTER_X));
    TEST_ASSERT_TRUE(near_eq(slot->y, SCALED_DEVICE_CENTER_Y));
    TEST_ASSERT_TRUE(nt_input_mouse_is_down(NT_BUTTON_LEFT));
    /* Round-trip proof (mirrors the string-id scaled case): the injected DEVICE coord, converted back
       to layout via the ctx viewport, actually hit-tests onto the widget — not just a number returned. */
    nt_mem_scratch_reset();
    nt_pointer_t mouse = {0};
    mouse.x = -100.0F;
    mouse.y = -100.0F;
    nt_ui_begin(s_scaled_ctx, LAYOUT_W, LAYOUT_H, 0.0F, &mouse, 1);
    nt_ui_set_viewport(s_scaled_ctx, (nt_ui_viewport_t){.x = SCALED_VP_X, .y = SCALED_VP_Y, .w = LAYOUT_W * SCALED_SCALE, .h = LAYOUT_H * SCALED_SCALE});
    const float device_pt[2] = {slot->x, slot->y};
    float layout_pt[2];
    nt_ui_screen_to_layout(s_scaled_ctx, device_pt, layout_pt);
    bool hit = nt_ui_test_hit(s_scaled_ctx, nt_ui_id("widget"), layout_pt[0], layout_pt[1]);
    nt_ui_end(s_scaled_ctx);
    TEST_ASSERT_TRUE(hit);
}

/* ---- never-begun ctx: wire input must bad_params, NOT trap (the wire-input-crash regression) ---- */

/* ui.tree / ui.element / ui.click({x,y}) against a registered-but-never-begun ctx all reach the coord
   converters (add_meta / resolve_target) which trap on begin_w==0. resolve_ctx must reject FIRST with
   bad_params — proving no bot input on a pre-frame ctx can crash the host. */
static void test_neverbegun_ctx_tree_bad_params(void) { assert_bad_params(nt_devapi_submit("{\"method\":\"ui.tree\",\"params\":{\"ctx\":\"hud_neverbegun\"}}")); }

static void test_neverbegun_ctx_element_bad_params(void) { assert_bad_params(nt_devapi_submit("{\"method\":\"ui.element\",\"params\":{\"id\":\"widget\",\"ctx\":\"hud_neverbegun\"}}")); }

static void test_neverbegun_ctx_click_xy_bad_params(void) { assert_bad_params(nt_devapi_submit("{\"method\":\"ui.click\",\"params\":{\"ctx\":\"hud_neverbegun\",\"id\":{\"x\":10,\"y\":10}}}")); }

/* ---- bad_params paths (never assert on wire input) ---- */

static void test_unknown_ctx_bad_params(void) { assert_bad_params(nt_devapi_submit("{\"method\":\"ui.tree\",\"params\":{\"ctx\":\"missing\"}}")); }

static void test_ctx_non_string_bad_params(void) { assert_bad_params(nt_devapi_submit("{\"method\":\"ui.tree\",\"params\":{\"ctx\":7}}")); }

/* 1e39 overflows FLT_MAX -> the float-narrow guard rejects a non-finite coord, never stores inf. */
static void test_click_xy_non_finite_bad_params(void) { assert_bad_params(nt_devapi_submit("{\"method\":\"ui.click\",\"params\":{\"id\":{\"x\":1e39,\"y\":0}}}")); }

static void test_click_xy_inf_y_bad_params(void) { assert_bad_params(nt_devapi_submit("{\"method\":\"ui.click\",\"params\":{\"id\":{\"x\":0,\"y\":1e309}}}")); }

static void test_click_hold_fractional_bad_params(void) { assert_bad_params(nt_devapi_submit("{\"method\":\"ui.click\",\"params\":{\"id\":\"widget\",\"hold\":2.5}}")); }

static void test_click_hold_negative_bad_params(void) { assert_bad_params(nt_devapi_submit("{\"method\":\"ui.click\",\"params\":{\"id\":\"widget\",\"hold\":-1}}")); }

static void test_click_hold_bool_bad_params(void) { assert_bad_params(nt_devapi_submit("{\"method\":\"ui.click\",\"params\":{\"id\":\"widget\",\"hold\":true}}")); }

static void test_drag_frames_fractional_bad_params(void) { assert_bad_params(nt_devapi_submit("{\"method\":\"ui.drag\",\"params\":{\"from\":\"widget\",\"to\":\"widget\",\"frames\":1.5}}")); }

static void test_drag_frames_negative_bad_params(void) { assert_bad_params(nt_devapi_submit("{\"method\":\"ui.drag\",\"params\":{\"from\":\"widget\",\"to\":\"widget\",\"frames\":-3}}")); }

/* frames over the per-command DoS cap -> bad_params, no partial inject. */
static void test_drag_frames_over_cap_bad_params(void) {
    char req[192];
    (void)snprintf(req, sizeof req, "{\"method\":\"ui.drag\",\"params\":{\"from\":\"widget\",\"to\":\"widget\",\"frames\":%u}}", UI_DRAG_FRAMES_MAX + 1U);
    assert_bad_params(nt_devapi_submit(req));
}

/* ---- ui.drag whole-or-nothing on the shared scheduler ---- */

/* A ui.drag at the per-command cap reserves DOWN + frames MOVEs + UP all-or-nothing on the shared
   input scheduler: with exactly that many free slots it succeeds and queues the full count. */
static void test_drag_at_cap_reserves_whole(void) {
    uint16_t frames = (uint16_t)UI_DRAG_FRAMES_MAX;
    char req[192];
    (void)snprintf(req, sizeof req, "{\"method\":\"ui.drag\",\"params\":{\"from\":\"widget\",\"to\":\"widget\",\"frames\":%u}}", (unsigned)frames);
    cJSON *root = parse_ok(nt_devapi_submit(req));
    TEST_ASSERT_EQUAL_INT((int)frames + 2, cJSON_GetObjectItemCaseSensitive(ok_result(root), "queued")->valueint);
    cJSON_Delete(root);
}

/* Over-reserve is whole-or-nothing with NO partial inject: pre-fill the shared schedule leaving too
   few slots for a 2-entry ui.click, assert the click rejects WHOLE, then prove exactly the expected
   free slots survived (the click wrote nothing). Mirrors the input group's near-full atomic proof. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_drag_over_reserve_no_partial_inject(void) {
    /* Each input.key (no hold) stages exactly 1 schedule entry; we never advance, so none drain. */
    for (uint32_t i = 0; i < NT_DEVAPI_INPUT_SCHED_MAX - 1U; i++) {
        cJSON_Delete(parse_ok(nt_devapi_submit("{\"method\":\"input.key\",\"params\":{\"key\":\"A\"}}")));
    }
    /* 1 free slot left. A ui.click needs 2 -> reject whole, no orphan DOWN. */
    assert_bad_params(nt_devapi_submit("{\"method\":\"ui.click\",\"params\":{\"id\":\"widget\"}}"));
    /* The 1 free slot survived intact: one 1-entry key succeeds, the next overflows. */
    cJSON_Delete(parse_ok(nt_devapi_submit("{\"method\":\"input.key\",\"params\":{\"key\":\"B\"}}")));
    assert_bad_params(nt_devapi_submit("{\"method\":\"input.key\",\"params\":{\"key\":\"C\"}}"));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_contexts_lists_registered);
    RUN_TEST(test_tree_meta_and_yup_bounds);
    RUN_TEST(test_element_by_id_ok);
    RUN_TEST(test_element_unknown_id_bad_params);
    RUN_TEST(test_element_non_string_id_bad_params);
    RUN_TEST(test_click_string_id_queues_two);
    RUN_TEST(test_click_unknown_id_bad_params);
    RUN_TEST(test_click_empty_id_bad_params);
    RUN_TEST(test_click_collapsed_id_bad_params);
    RUN_TEST(test_click_string_id_lands_at_device_center);
    RUN_TEST(test_click_xy_yup_flip_matches_string_id);
    RUN_TEST(test_transformed_click_id_targets_projected_center);
    RUN_TEST(test_scaled_tree_exposes_viewport);
    RUN_TEST(test_unscaled_viewport_is_identity);
    RUN_TEST(test_scaled_click_string_id_lands_at_device_center);
    RUN_TEST(test_scaled_click_xy_lands_at_device_center);
    RUN_TEST(test_neverbegun_ctx_tree_bad_params);
    RUN_TEST(test_neverbegun_ctx_element_bad_params);
    RUN_TEST(test_neverbegun_ctx_click_xy_bad_params);
    RUN_TEST(test_unknown_ctx_bad_params);
    RUN_TEST(test_ctx_non_string_bad_params);
    RUN_TEST(test_click_xy_non_finite_bad_params);
    RUN_TEST(test_click_xy_inf_y_bad_params);
    RUN_TEST(test_click_hold_fractional_bad_params);
    RUN_TEST(test_click_hold_negative_bad_params);
    RUN_TEST(test_click_hold_bool_bad_params);
    RUN_TEST(test_drag_frames_fractional_bad_params);
    RUN_TEST(test_drag_frames_negative_bad_params);
    RUN_TEST(test_drag_frames_over_cap_bad_params);
    RUN_TEST(test_drag_at_cap_reserves_whole);
    RUN_TEST(test_drag_over_reserve_no_partial_inject);
    return UNITY_END();
}

#else /* NT_UI_DEBUG_TOOLS off: ui.tree/element read through the probe, which is DEBUG_TOOLS-gated. */

int main(void) {
    UNITY_BEGIN();
    return UNITY_END();
}

#endif
