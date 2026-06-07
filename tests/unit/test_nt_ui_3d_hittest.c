/* 3D raycast hit-test.
 *
 * Validates use_raycast_input=true path: nt_ui_set_view_proj caches inverse,
 * ui_hit_test unprojects pixel → world ray → widget local → bbox.
 *
 * Asymmetric test data: NON-square bbox 200×60 at non-origin (150, 80) within
 * an 800×600 viewport. Ortho view_proj matches the engine's GL Y-up convention. */

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
#include "unity.h"

alignas(NT_UI_ARENA_ALIGN) static uint8_t s_arena[NT_UI_TEST_ARENA_SIZE];
static ui_walker_fixture_t s_fx;

#define SCREEN_W 800.0F
#define SCREEN_H 600.0F
#define BBOX_X 150.0F
#define BBOX_Y 80.0F
#define BBOX_W 200.0F
#define BBOX_H 60.0F

/* Builds a 3D ctx via the fixture's bind-all path but recreates the ctx with use_raycast_input=true. */
static void setup_3d_ctx(void) {
    /* Tear down the fixture's 2D ctx and rebuild with the 3D flag. */
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

void setUp(void) {
    nt_test_assert_install();
    ui_walker_fixture_init(&s_fx, s_arena, sizeof s_arena, UI_WALKER_FX_BIND_ALL);
    setup_3d_ctx();
}

void tearDown(void) { ui_walker_fixture_shutdown(&s_fx); }

/* Y-DOWN ortho — matches Clay layout convention so baked.m (no Y-flip in 3D ctx)
 * maps Clay (Y-down) directly to world (Y-down). Game provides this when it wants
 * 2D-equivalent rendering in 3D mode; for Y-up world (e.g. perspective game camera)
 * the game must include a Y-flip in the baked.m chain or in view_proj math. */
static void make_ortho_clay_y_down(float view_proj[16]) {
    mat4 m;
    glm_ortho(0.0F, SCREEN_W, SCREEN_H, 0.0F, -1.0F, 1.0F, m);
    memcpy(view_proj, m, sizeof(float) * 16);
}

/* Declare bbox at Clay (BBOX_X, BBOX_Y, BBOX_W, BBOX_H), apply Y-flip via transform offset_y
 * so the widget's world position matches the Y-up GL space at (BBOX_X, SCREEN_H - BBOX_Y - BBOX_H). */
static void declare_bbox_3d(void) {
    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, SCREEN_W, SCREEN_H, 0.0F, &mouse, 1);
    CLAY({.id = CLAY_ID("hit3d"),
          .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .offset = {.x = BBOX_X, .y = BBOX_Y}},
          .layout = {.sizing = {CLAY_SIZING_FIXED(BBOX_W), CLAY_SIZING_FIXED(BBOX_H)}},
          .userData = NULL}) {}
    nt_ui_end(s_fx.ctx);
}

/* One frame: begin + view_proj (+ optional occlusion cutoff) + declare hit3d + step. occlusion < 0
 * leaves the default (+inf, no cutoff). */
static nt_ui_interaction_t step_bbox_3d(float px, float py, float occlusion) {
    float vp[16];
    make_ortho_clay_y_down(vp);
    nt_pointer_t mouse = {0};
    mouse.x = px;
    mouse.y = py;
    mouse.active = true;
    nt_ui_begin(s_fx.ctx, SCREEN_W, SCREEN_H, 0.0F, &mouse, 1);
    nt_ui_set_view_proj(s_fx.ctx, vp);
    if (occlusion >= 0.0F) {
        nt_ui_set_pointer_occlusion(s_fx.ctx, 0U, occlusion);
    }
    CLAY({.id = CLAY_ID("hit3d"), .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .offset = {.x = BBOX_X, .y = BBOX_Y}}, .layout = {.sizing = {CLAY_SIZING_FIXED(BBOX_W), CLAY_SIZING_FIXED(BBOX_H)}}}) {}
    nt_ui_interaction_t in = nt_ui_step_interaction(s_fx.ctx, nt_ui_id("hit3d"));
    nt_ui_end(s_fx.ctx);
    return in;
}

/* Widget sits at world distance ~1.0 (ortho near=-1/far=+1, Clay z=0 → t=0.5, dist=0.5*2). A cutoff
 * below that occludes it: the pointer hits nothing valid, and the gate's hot==0 fallback must NOT
 * re-admit it (this is the regression that shipped the occlusion feature broken). */
static void test_occlusion_blocks_widget_beyond_cutoff(void) {
    const float cx = BBOX_X + (BBOX_W * 0.5F);
    const float cy = BBOX_Y + (BBOX_H * 0.5F);
    declare_bbox_3d();                                        /* frame 1: bake bbox */
    step_bbox_3d(cx, cy, -1.0F);                              /* frame 2: register (resolve sees empty registry → fallback) */
    nt_ui_interaction_t blocked = step_bbox_3d(cx, cy, 0.5F); /* frame 3: cutoff 0.5 < dist 1.0 */
    TEST_ASSERT_FALSE(blocked.hovered);
}

/* A cutoff beyond the widget distance leaves it interactive. */
static void test_occlusion_allows_widget_within_cutoff(void) {
    const float cx = BBOX_X + (BBOX_W * 0.5F);
    const float cy = BBOX_Y + (BBOX_H * 0.5F);
    declare_bbox_3d();
    step_bbox_3d(cx, cy, -1.0F);
    nt_ui_interaction_t allowed = step_bbox_3d(cx, cy, 2.0F); /* cutoff 2.0 > dist 1.0 */
    TEST_ASSERT_TRUE(allowed.hovered);
}

/* ---- Test 1: view_proj setter caches inverse + flag, and round-trip is sane. ---- */
static void test_view_proj_set_caches_inverse(void) {
    float vp[16];
    make_ortho_clay_y_down(vp);
    nt_ui_set_view_proj(s_fx.ctx, vp);
    /* No direct getter — round-trip via a hit-test. Declare a widget and probe its center. */
    declare_bbox_3d();

    /* Frame 2: probe. */
    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, SCREEN_W, SCREEN_H, 0.0F, &mouse, 1);
    /* Must reset view_proj each frame (engine asserts view_proj_set per walk; hit-test reads cached inverse). */
    nt_ui_set_view_proj(s_fx.ctx, vp);
    const uint32_t id = nt_ui_id("hit3d");
    /* For ortho Y-up view, Clay (BBOX_X, BBOX_Y) maps to GL (BBOX_X, SCREEN_H - BBOX_Y). The widget's
     * world position lives in Clay-layout space (no extra transform attached → identity baked).
     * Pixel center of widget = (BBOX_X + BBOX_W/2, BBOX_Y + BBOX_H/2) in Clay-pixel coords. */
    const float center_x = BBOX_X + (BBOX_W * 0.5F);
    const float center_y = BBOX_Y + (BBOX_H * 0.5F);
    const bool h = nt_ui_test_hit(s_fx.ctx, id, center_x, center_y);
    nt_ui_end(s_fx.ctx);
    TEST_ASSERT_TRUE_MESSAGE(h, "ortho 3D ctx: center pixel must hit identity widget at its bbox center");
}

/* ---- Test 2: raycast accepts widget center, rejects clearly outside. ---- */
static void test_raycast_hit_accept_and_reject(void) {
    float vp[16];
    make_ortho_clay_y_down(vp);
    nt_ui_set_view_proj(s_fx.ctx, vp);
    declare_bbox_3d();

    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, SCREEN_W, SCREEN_H, 0.0F, &mouse, 1);
    nt_ui_set_view_proj(s_fx.ctx, vp);
    const uint32_t id = nt_ui_id("hit3d");
    const float center_x = BBOX_X + (BBOX_W * 0.5F);
    const float center_y = BBOX_Y + (BBOX_H * 0.5F);
    /* Outside: 50 px past right edge. */
    const float outside_x = BBOX_X + BBOX_W + 50.0F;
    TEST_ASSERT_TRUE(nt_ui_test_hit(s_fx.ctx, id, center_x, center_y));
    TEST_ASSERT_FALSE(nt_ui_test_hit(s_fx.ctx, id, outside_x, center_y));
    nt_ui_end(s_fx.ctx);
}

/* ---- Test 3b: 3D ctx hit-test asserts when nt_ui_set_view_proj was not called. ---- */
static void test_hit_test_asserts_when_view_proj_not_set(void) {
    /* Don't call nt_ui_set_view_proj — view_proj_set stays false. */
    declare_bbox_3d();

    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, SCREEN_W, SCREEN_H, 0.0F, &mouse, 1);
    const uint32_t id = nt_ui_id("hit3d");
    NT_TEST_EXPECT_ASSERT(nt_ui_test_hit(s_fx.ctx, id, 200.0F, 100.0F));
    nt_ui_end(s_fx.ctx);
}

/* ---- Test 4: set_view_proj asserts when ctx is not 3D mode. ---- */
static void test_set_view_proj_asserts_on_2d_ctx(void) {
    /* Recreate 2D ctx. */
    nt_ui_destroy_context(s_fx.ctx);
    nt_ui_create_desc_t desc = nt_ui_create_desc_defaults();
    s_fx.ctx = nt_ui_create_context(s_arena, sizeof s_arena, &desc);
    nt_ui_set_font(s_fx.ctx, 0U, s_fx.stub_font);

    float vp[16];
    make_ortho_clay_y_down(vp);
    NT_TEST_EXPECT_ASSERT(nt_ui_set_view_proj(s_fx.ctx, vp));
}

/* ---- Test 5: perspective view_proj — widget in front of camera should still hit at center pixel.
 *      Validates the ray-plane math path independent of ortho convention. ---- */
static void test_raycast_hit_perspective(void) {
    /* Use the same test bbox but with a perspective camera looking down -Z at a widget on z=0 plane.
     * Camera at z=+5 looking toward origin. Wrap the widget in a transform that places it at world (0, 0, 0). */
    mat4 view;
    mat4 proj;
    glm_lookat((vec3){0.0F, 0.0F, 5.0F}, (vec3){0.0F, 0.0F, 0.0F}, (vec3){0.0F, 1.0F, 0.0F}, view);
    glm_perspective(glm_rad(60.0F), SCREEN_W / SCREEN_H, 0.1F, 100.0F, proj);
    mat4 vp_mat;
    glm_mat4_mul(proj, view, vp_mat);
    float vp[16];
    memcpy(vp, vp_mat, sizeof vp);
    nt_ui_set_view_proj(s_fx.ctx, vp);

    /* Widget at Clay layout-space (0, 0, 100x100) with no transform → world position at (0, 0). */
    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, SCREEN_W, SCREEN_H, 0.0F, &mouse, 1);
    CLAY({.id = CLAY_ID("persp"), .layout = {.sizing = {CLAY_SIZING_FIXED(100.0F), CLAY_SIZING_FIXED(100.0F)}}, .userData = NULL}) {}
    nt_ui_end(s_fx.ctx);

    /* Frame 2 probe. Widget at world (0, 0) → projects to screen center (400, 300).
     * Mandatory per-frame setter after begin — nt_ui_begin resets view_proj_set, so hit-test
     * asserts if the call is omitted. */
    nt_ui_begin(s_fx.ctx, SCREEN_W, SCREEN_H, 0.0F, &mouse, 1);
    nt_ui_set_view_proj(s_fx.ctx, vp);
    const uint32_t id = nt_ui_id("persp");
    const bool h_center = nt_ui_test_hit(s_fx.ctx, id, SCREEN_W * 0.5F, SCREEN_H * 0.5F);
    const bool h_corner = nt_ui_test_hit(s_fx.ctx, id, 10.0F, 10.0F);
    nt_ui_end(s_fx.ctx);
    TEST_ASSERT_TRUE_MESSAGE(h_center, "perspective camera: screen-center pixel must hit widget projected to origin");
    TEST_ASSERT_FALSE_MESSAGE(h_corner, "perspective camera: top-left pixel must miss widget at world origin");
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_view_proj_set_caches_inverse);
    RUN_TEST(test_raycast_hit_accept_and_reject);
    RUN_TEST(test_hit_test_asserts_when_view_proj_not_set);
    RUN_TEST(test_set_view_proj_asserts_on_2d_ctx);
    RUN_TEST(test_raycast_hit_perspective);
    RUN_TEST(test_occlusion_blocks_widget_beyond_cutoff);
    RUN_TEST(test_occlusion_allows_widget_within_cutoff);
    return UNITY_END();
}
