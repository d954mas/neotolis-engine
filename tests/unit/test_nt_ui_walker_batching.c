#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "atlas/nt_atlas.h"
#include "clay.h"
#include "renderers/nt_sprite_renderer.h"
#include "test_helpers/ui_walker_fixture.h"
#include "ui/nt_ui.h"
#include "ui/nt_ui_image.h"
#include "ui/nt_ui_internal.h"
#include "unity.h"

alignas(NT_UI_ARENA_ALIGN) static uint8_t s_arena[NT_UI_TEST_ARENA_SIZE];
static ui_walker_fixture_t s_fx;

#define MAX_TEST_CMDS 16
static Clay_RenderCommand s_test_cmds[MAX_TEST_CMDS];
static const char s_text[] = "X";

void setUp(void) {
    memset(s_test_cmds, 0, sizeof s_test_cmds);
    ui_walker_fixture_init(&s_fx, s_arena, sizeof s_arena, UI_WALKER_FX_BIND_ALL);
}

void tearDown(void) { ui_walker_fixture_shutdown(&s_fx); }

static void inject_frozen_cmds(int32_t count) {
    for (int32_t k = 0; k < count; ++k) {
        s_test_cmds[k].nt_layout_index = -1; /* synthetic: no layout element, band 0 */
    }
    s_fx.ctx->frozen_cmds.internalArray = s_test_cmds;
    s_fx.ctx->frozen_cmds.length = count;
    s_fx.ctx->frozen_cmds.capacity = MAX_TEST_CMDS;
}

/* File-scope so layer data outlives walk (compound literal would scope out). */
static const nt_ui_element_data_t k_layer_sprite = {.layer = 0U};
static const nt_ui_element_data_t k_layer_text = {.layer = 1U};

static void make_rect(int idx, float x) {
    Clay_RenderCommand *c = &s_test_cmds[idx];
    c->commandType = CLAY_RENDER_COMMAND_TYPE_RECTANGLE;
    c->boundingBox = (Clay_BoundingBox){.x = x, .y = 0, .width = 10, .height = 10};
    c->renderData.rectangle.backgroundColor = (Clay_Color){.r = 255, .g = 0, .b = 0, .a = 255};
    c->userData = (void *)&k_layer_sprite;
}

static void make_text(int idx, float x) {
    Clay_RenderCommand *c = &s_test_cmds[idx];
    c->commandType = CLAY_RENDER_COMMAND_TYPE_TEXT;
    c->boundingBox = (Clay_BoundingBox){.x = x, .y = 0, .width = 10, .height = 10};
    c->renderData.text.stringContents = (Clay_StringSlice){.length = 1, .chars = s_text, .baseChars = s_text};
    c->renderData.text.textColor = (Clay_Color){.r = 255, .g = 255, .b = 255, .a = 255};
    c->renderData.text.fontId = 0;
    c->renderData.text.fontSize = 14;
    c->userData = (void *)&k_layer_text;
}

static int s_custom_calls;
static void custom_cb(const nt_ui_custom_frame_t *frame, void *user) {
    (void)frame;
    (void)user;
    /* Callback may bind its own pipeline -- sprite staging must be empty. */
    TEST_ASSERT_EQUAL_UINT32(0U, nt_sprite_renderer_test_vertex_count());
    ++s_custom_calls;
}

/* RECTs on layer 0 + TEXTs on layer 1: layer sort collapses interleaved
 * RTRTRT to 1 sprite dc (sprites batch, single sprite flush on first TEXT). */
static void test_same_z_rect_text_batches(void) {
    make_rect(0, 0);
    make_text(1, 20);
    make_rect(2, 40);
    make_text(3, 60);
    make_rect(4, 80);
    make_text(5, 100);
    inject_frozen_cmds(6);

    nt_ui_target_t target = {.viewport = {0, 0, 800, 600}};
    nt_ui_walk(s_fx.ctx, &target);

    TEST_ASSERT_EQUAL_UINT32(1U, nt_ui_get_last_walk_draw_calls(s_fx.ctx));
}

/* Each scissor transition force-flushes to preserve clip scope. */
static void test_scissor_is_hard_barrier(void) {
    make_rect(0, 0);
    s_test_cmds[1].commandType = CLAY_RENDER_COMMAND_TYPE_SCISSOR_START;
    s_test_cmds[1].boundingBox = (Clay_BoundingBox){.x = 0, .y = 0, .width = 800, .height = 600};
    s_test_cmds[1].renderData.clip.horizontal = true;
    s_test_cmds[1].renderData.clip.vertical = true;
    make_rect(2, 20);
    s_test_cmds[3].commandType = CLAY_RENDER_COMMAND_TYPE_SCISSOR_END;
    make_rect(4, 40);
    inject_frozen_cmds(5);

    const uint32_t calls_before = nt_sprite_renderer_test_draw_call_count();
    nt_ui_target_t target = {.viewport = {0, 0, 800, 600}};
    nt_ui_walk(s_fx.ctx, &target);

    TEST_ASSERT_EQUAL_UINT32(calls_before + 3U, nt_sprite_renderer_test_draw_call_count());
}

/* CUSTOM callback sees clean renderer state (assert inside cb). */
static void test_custom_is_hard_barrier(void) {
    s_custom_calls = 0;
    nt_ui_set_custom_handler(s_fx.ctx, custom_cb, NULL);

    static nt_ui_custom_data_t cd = {.type = NT_UI_CUSTOM_TYPE_GAME, .data = NULL};
    make_rect(0, 0);
    s_test_cmds[1].commandType = CLAY_RENDER_COMMAND_TYPE_CUSTOM;
    s_test_cmds[1].renderData.custom.customData = &cd;
    make_rect(2, 20);
    inject_frozen_cmds(3);

    const uint32_t calls_before = nt_sprite_renderer_test_draw_call_count();
    nt_ui_target_t target = {.viewport = {0, 0, 800, 600}};
    nt_ui_walk(s_fx.ctx, &target);

    TEST_ASSERT_EQUAL_INT(1, s_custom_calls);
    TEST_ASSERT_EQUAL_UINT32(calls_before + 2U, nt_sprite_renderer_test_draw_call_count());
}

/* Real Clay trees: the band comes from build_tree, not from the command's own zIndex. Vertex 0 of an
 * IMAGE lands at the bbox centre with the fixture's origin-(0,0) white region, a RECT's at its corner,
 * so the boxes sit in disjoint x ranges and the last sprite emit is attributed by range. */
static const nt_ui_image_style_t k_image_style = {.color_packed = 0xFFFFFFFF, .slice9_scale = 1.0F};

static int last_emit_x(void) {
    nt_ui_target_t target = {.viewport = {0, 0, 800, 600}};
    nt_ui_walk(s_fx.ctx, &target);
    float pos[3];
    nt_sprite_renderer_test_last_emit_position(0U, pos);
    return (int)pos[0];
}

/* Inside one lifted band the layer orders IMAGE(1) after RECT(0) even though Clay stamps
 * zIndex only on the RECT. */
static void test_band_layer_orders_image_after_rect_inside_overlay(void) {
    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    nt_atlas_region_ref_t ref = nt_atlas_ref_idx(s_fx.atlas.handle, 0, s_fx.atlas.white_region_idx);
    CLAY({.id = CLAY_ID("root"), .layout = {.sizing = {CLAY_SIZING_FIXED(800.0F), CLAY_SIZING_FIXED(600.0F)}}}) {
        CLAY({.id = CLAY_ID("overlay"),
              .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .zIndex = 1000, .offset = {.x = 300.0F, .y = 0.0F}},
              .layout = {.layoutDirection = CLAY_LEFT_TO_RIGHT, .sizing = {CLAY_SIZING_FIXED(200.0F), CLAY_SIZING_FIXED(100.0F)}}}) {
            nt_ui_image(s_fx.ctx, NT_UI_DATA_LAYER(1), &ref, &k_image_style, &(Clay_ElementDeclaration){.layout = {.sizing = {CLAY_SIZING_FIXED(50.0F), CLAY_SIZING_FIXED(50.0F)}}});
            CLAY({.id = CLAY_ID("ov_rect"), .layout = {.sizing = {CLAY_SIZING_FIXED(50.0F), CLAY_SIZING_FIXED(50.0F)}}, .backgroundColor = {255.0F, 0, 0, 255.0F}, .userData = NT_UI_CLAY_DATA(0)}) {}
        }
    }
    nt_ui_end(s_fx.ctx);

    /* Image spans [300,350), rect [350,400): the image (layer 1) must paint last. */
    const int x = last_emit_x();
    TEST_ASSERT_EQUAL_UINT32(1U, nt_ui_get_last_walk_rect_command_count(s_fx.ctx));
    TEST_ASSERT_EQUAL_UINT32(1U, nt_ui_get_last_walk_image_command_count(s_fx.ctx));
    TEST_ASSERT_TRUE_MESSAGE(x >= 300 && x < 350, "overlay IMAGE on layer 1 must paint after the overlay RECT on layer 0");
}

/* A base-band IMAGE adjacent to an overlay IMAGE (both zIndex 0 on the command) must not share
 * a segment: the base sprite would paint over the overlay. */
static void test_band_base_sprite_does_not_paint_over_overlay_image(void) {
    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    nt_atlas_region_ref_t ref = nt_atlas_ref_idx(s_fx.atlas.handle, 0, s_fx.atlas.white_region_idx);
    CLAY({.id = CLAY_ID("root"), .layout = {.sizing = {CLAY_SIZING_FIXED(800.0F), CLAY_SIZING_FIXED(600.0F)}}}) {
        nt_ui_image(s_fx.ctx, NT_UI_DATA_LAYER(7), &ref, &k_image_style, &(Clay_ElementDeclaration){.layout = {.sizing = {CLAY_SIZING_FIXED(40.0F), CLAY_SIZING_FIXED(40.0F)}}});
        CLAY({.id = CLAY_ID("overlay"),
              .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .zIndex = 1000, .offset = {.x = 300.0F, .y = 0.0F}},
              .layout = {.sizing = {CLAY_SIZING_FIXED(200.0F), CLAY_SIZING_FIXED(100.0F)}}}) {
            nt_ui_image(s_fx.ctx, NT_UI_DATA_LAYER(1), &ref, &k_image_style, &(Clay_ElementDeclaration){.layout = {.sizing = {CLAY_SIZING_FIXED(50.0F), CLAY_SIZING_FIXED(50.0F)}}});
        }
    }
    nt_ui_end(s_fx.ctx);

    /* Base image spans [0,40), overlay image [300,350): the overlay must paint last. */
    const int x = last_emit_x();
    TEST_ASSERT_EQUAL_UINT32(2U, nt_ui_get_last_walk_image_command_count(s_fx.ctx));
    TEST_ASSERT_TRUE_MESSAGE(x >= 300, "overlay IMAGE must paint after the base-band IMAGE regardless of layer");
}

/* Band beats layer across tree roots: base RECT(layer 1) still paints under the overlay RECT(layer 0). */
static void test_band_beats_layer_across_roots(void) {
    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    CLAY({.id = CLAY_ID("root"), .layout = {.sizing = {CLAY_SIZING_FIXED(800.0F), CLAY_SIZING_FIXED(600.0F)}}}) {
        CLAY({.id = CLAY_ID("base_rect"), .layout = {.sizing = {CLAY_SIZING_FIXED(40.0F), CLAY_SIZING_FIXED(40.0F)}}, .backgroundColor = {0, 255.0F, 0, 255.0F}, .userData = NT_UI_CLAY_DATA(1)}) {}
        CLAY({.id = CLAY_ID("overlay"),
              .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .zIndex = 1000, .offset = {.x = 300.0F, .y = 0.0F}},
              .layout = {.sizing = {CLAY_SIZING_FIXED(50.0F), CLAY_SIZING_FIXED(50.0F)}},
              .backgroundColor = {255.0F, 0, 0, 255.0F},
              .userData = NT_UI_CLAY_DATA(0)}) {}
    }
    nt_ui_end(s_fx.ctx);

    const int x = last_emit_x();
    TEST_ASSERT_EQUAL_UINT32(2U, nt_ui_get_last_walk_rect_command_count(s_fx.ctx));
    TEST_ASSERT_TRUE_MESSAGE(x >= 300, "overlay RECT on layer 0 must paint after the base RECT on layer 1");
}

/* Same band, same layer: declaration order is the last key. */
static void test_same_band_same_layer_keeps_declaration_order(void) {
    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    CLAY({.id = CLAY_ID("root"), .layout = {.layoutDirection = CLAY_LEFT_TO_RIGHT, .sizing = {CLAY_SIZING_FIXED(800.0F), CLAY_SIZING_FIXED(600.0F)}}}) {
        CLAY({.id = CLAY_ID("first"), .layout = {.sizing = {CLAY_SIZING_FIXED(40.0F), CLAY_SIZING_FIXED(40.0F)}}, .backgroundColor = {0, 255.0F, 0, 255.0F}, .userData = NT_UI_CLAY_DATA(0)}) {}
        CLAY({.id = CLAY_ID("second"), .layout = {.sizing = {CLAY_SIZING_FIXED(40.0F), CLAY_SIZING_FIXED(40.0F)}}, .backgroundColor = {255.0F, 0, 0, 255.0F}, .userData = NT_UI_CLAY_DATA(0)}) {}
    }
    nt_ui_end(s_fx.ctx);

    /* First rect spans [0,40), second [40,80): the second must paint last. */
    const int x = last_emit_x();
    TEST_ASSERT_EQUAL_UINT32(2U, nt_ui_get_last_walk_rect_command_count(s_fx.ctx));
    TEST_ASSERT_TRUE_MESSAGE(x >= 40, "later-declared RECT of the same band and layer must paint last");
}

/* NULL userData defaults to layer 0 -- unlayered count exposes how many
 * segmentable commands fell through to the default layer. */
static void test_unlayered_count_tracks_null_userdata(void) {
    /* Bare setup so userData stays NULL on the first two. */
    Clay_RenderCommand *c0 = &s_test_cmds[0];
    c0->commandType = CLAY_RENDER_COMMAND_TYPE_RECTANGLE;
    c0->boundingBox = (Clay_BoundingBox){.x = 0, .y = 0, .width = 10, .height = 10};
    c0->renderData.rectangle.backgroundColor = (Clay_Color){.r = 255, .g = 0, .b = 0, .a = 255};
    c0->userData = NULL;
    Clay_RenderCommand *c1 = &s_test_cmds[1];
    c1->commandType = CLAY_RENDER_COMMAND_TYPE_RECTANGLE;
    c1->boundingBox = (Clay_BoundingBox){.x = 20, .y = 0, .width = 10, .height = 10};
    c1->renderData.rectangle.backgroundColor = (Clay_Color){.r = 0, .g = 255, .b = 0, .a = 255};
    c1->userData = NULL;
    Clay_RenderCommand *c2 = &s_test_cmds[2];
    c2->commandType = CLAY_RENDER_COMMAND_TYPE_RECTANGLE;
    c2->boundingBox = (Clay_BoundingBox){.x = 40, .y = 0, .width = 10, .height = 10};
    c2->renderData.rectangle.backgroundColor = (Clay_Color){.r = 0, .g = 0, .b = 255, .a = 255};
    c2->userData = (void *)&k_layer_sprite; /* explicit layer 0 */
    inject_frozen_cmds(3);

    nt_ui_target_t target = {.viewport = {0, 0, 800, 600}};
    nt_ui_walk(s_fx.ctx, &target);

    TEST_ASSERT_EQUAL_UINT32(2U, nt_ui_test_last_walk_unlayered_count(s_fx.ctx));
}

/* Sparse layers (0 and 200): bitmask scan skips empty buckets. */
static void test_layer_sort_sparse_layers(void) {
    static const nt_ui_element_data_t k_layer_low = {.layer = 0U};
    static const nt_ui_element_data_t k_layer_high = {.layer = 200U};
    /* Declared in reverse order; layer 0 must still render first. */
    Clay_RenderCommand *c0 = &s_test_cmds[0];
    c0->commandType = CLAY_RENDER_COMMAND_TYPE_RECTANGLE;
    c0->boundingBox = (Clay_BoundingBox){.x = 0, .y = 0, .width = 10, .height = 10};
    c0->renderData.rectangle.backgroundColor = (Clay_Color){.r = 255, .g = 0, .b = 0, .a = 255};
    c0->userData = (void *)&k_layer_high;

    Clay_RenderCommand *c1 = &s_test_cmds[1];
    c1->commandType = CLAY_RENDER_COMMAND_TYPE_RECTANGLE;
    c1->boundingBox = (Clay_BoundingBox){.x = 20, .y = 0, .width = 10, .height = 10};
    c1->renderData.rectangle.backgroundColor = (Clay_Color){.r = 0, .g = 255, .b = 0, .a = 255};
    c1->userData = (void *)&k_layer_low;
    inject_frozen_cmds(2);

    nt_ui_target_t target = {.viewport = {0, 0, 800, 600}};
    nt_ui_walk(s_fx.ctx, &target);

    /* Last emit must be layer 200's rect at x=0. */
    float pos[3];
    nt_sprite_renderer_test_last_emit_position(0U, pos);
    TEST_ASSERT_EQUAL_INT32(0, (int32_t)pos[0]);
}

/* Layer wins over declaration order: layer 0 declared second renders first. */
static void test_layer_sort_overrides_declaration_order(void) {
    Clay_RenderCommand *c0 = &s_test_cmds[0];
    c0->commandType = CLAY_RENDER_COMMAND_TYPE_RECTANGLE;
    c0->boundingBox = (Clay_BoundingBox){.x = 0, .y = 0, .width = 10, .height = 10};
    c0->renderData.rectangle.backgroundColor = (Clay_Color){.r = 255, .g = 0, .b = 0, .a = 255};
    c0->userData = (void *)&k_layer_text;

    Clay_RenderCommand *c1 = &s_test_cmds[1];
    c1->commandType = CLAY_RENDER_COMMAND_TYPE_RECTANGLE;
    c1->boundingBox = (Clay_BoundingBox){.x = 20, .y = 0, .width = 10, .height = 10};
    c1->renderData.rectangle.backgroundColor = (Clay_Color){.r = 0, .g = 255, .b = 0, .a = 255};
    c1->userData = (void *)&k_layer_sprite;
    inject_frozen_cmds(2);

    nt_ui_target_t target = {.viewport = {0, 0, 800, 600}};
    nt_ui_walk(s_fx.ctx, &target);

    /* Layer 1 (declared first) renders LAST: last emit is its rect at x=0. */
    float pos[3];
    nt_sprite_renderer_test_last_emit_position(0U, pos);
    TEST_ASSERT_EQUAL_INT32(0, (int32_t)pos[0]);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_same_z_rect_text_batches);
    RUN_TEST(test_scissor_is_hard_barrier);
    RUN_TEST(test_custom_is_hard_barrier);
    RUN_TEST(test_band_layer_orders_image_after_rect_inside_overlay);
    RUN_TEST(test_band_base_sprite_does_not_paint_over_overlay_image);
    RUN_TEST(test_band_beats_layer_across_roots);
    RUN_TEST(test_same_band_same_layer_keeps_declaration_order);
    RUN_TEST(test_unlayered_count_tracks_null_userdata);
    RUN_TEST(test_layer_sort_overrides_declaration_order);
    RUN_TEST(test_layer_sort_sparse_layers);
    return UNITY_END();
}
