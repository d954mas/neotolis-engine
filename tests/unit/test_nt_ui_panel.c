/* Unit tests for nt_ui_panel/group widgets (Phase 54 Plan 05).
 *
 * Tests verify balanced push/pop of transform+opacity stacks,
 * Clay element tree integrity, and death tests for invalid input.
 * Pattern follows test_nt_ui_label.c and test_nt_ui_image.c. */

#include <stdalign.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "clay.h"
#include "core/nt_assert.h"
#include "test_helpers/nt_assert_trap.h"
#include "test_helpers/ui_test_arena.h"
#include "test_helpers/ui_walker_fixture.h"
#include "ui/nt_ui.h"
#include "ui/nt_ui_image.h"
#include "ui/nt_ui_internal.h"
#include "ui/nt_ui_label.h"
#include "ui/nt_ui_panel.h"
#include "unity.h"

alignas(NT_UI_ARENA_ALIGN) static uint8_t s_arena[NT_UI_TEST_ARENA_SIZE];
static ui_walker_fixture_t s_fx;

/* Shared styles. */
static const nt_ui_image_style_t s_panel_style = {
    .color_packed = 0xFFFFFFFF,
    .flip_bits = 0,
    .slice9_lrtb = {0, 0, 0, 0},
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

/* Helper: find the first IMAGE render command. */
static const Clay_RenderCommand *find_first_image_cmd(const nt_ui_context_t *ctx) {
    for (int32_t i = 0; i < ctx->frozen_cmds.length; ++i) {
        const Clay_RenderCommand *c = &ctx->frozen_cmds.internalArray[i];
        if (c->commandType == CLAY_RENDER_COMMAND_TYPE_IMAGE) {
            return c;
        }
    }
    return NULL;
}

/* Helper: find the first TEXT render command. */
static const Clay_RenderCommand *find_first_text_cmd(const nt_ui_context_t *ctx) {
    for (int32_t i = 0; i < ctx->frozen_cmds.length; ++i) {
        const Clay_RenderCommand *c = &ctx->frozen_cmds.internalArray[i];
        if (c->commandType == CLAY_RENDER_COMMAND_TYPE_TEXT) {
            return c;
        }
    }
    return NULL;
}

/* ---- Test 1: panel_begin/end balanced with child label ---- */
static void test_panel_begin_end_balanced(void) {
    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse);
    CLAY({.id = CLAY_ID("root")}) {
        nt_ui_panel_begin(s_fx.ctx, NULL, s_fx.atlas.handle, s_fx.atlas.white_region_idx, &s_panel_style, NULL, 1.0F);
        {
            nt_ui_label(s_fx.ctx, NULL, "Inside panel", &s_label_style);
        }
        nt_ui_panel_end(s_fx.ctx);
    }
    nt_ui_end(s_fx.ctx);

    /* IMAGE cmd emitted for the panel background. */
    const Clay_RenderCommand *img = find_first_image_cmd(s_fx.ctx);
    TEST_ASSERT_NOT_NULL(img);
    /* TEXT cmd emitted for the child label. */
    const Clay_RenderCommand *txt = find_first_text_cmd(s_fx.ctx);
    TEST_ASSERT_NOT_NULL(txt);
}

/* ---- Test 2: panel with explicit transform ---- */
static void test_panel_with_transform(void) {
    nt_ui_transform_t t = {.offset_x = 10.0F, .offset_y = 5.0F, .rotation = 0, .scale_x = 1.0F, .scale_y = 1.0F};
    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse);
    CLAY({.id = CLAY_ID("root")}) {
        nt_ui_panel_begin(s_fx.ctx, NULL, s_fx.atlas.handle, s_fx.atlas.white_region_idx, &s_panel_style, &t, 1.0F);
        {
            nt_ui_label(s_fx.ctx, NULL, "Offset", &s_label_style);
        }
        nt_ui_panel_end(s_fx.ctx);
    }
    nt_ui_end(s_fx.ctx);

    /* Walk succeeds (stacks balanced). */
    const Clay_RenderCommand *img = find_first_image_cmd(s_fx.ctx);
    TEST_ASSERT_NOT_NULL(img);
}

/* ---- Test 3: panel with NULL transform (identity) ---- */
static void test_panel_null_transform(void) {
    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse);
    CLAY({.id = CLAY_ID("root")}) {
        nt_ui_panel_begin(s_fx.ctx, NULL, s_fx.atlas.handle, s_fx.atlas.white_region_idx, &s_panel_style, NULL, 1.0F);
        nt_ui_panel_end(s_fx.ctx);
    }
    nt_ui_end(s_fx.ctx);

    /* Walk succeeds (identity transform used, no assert). */
    TEST_PASS();
}

/* ---- Test 4: group_begin/end balanced with child label ---- */
static void test_group_begin_end_balanced(void) {
    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse);
    CLAY({.id = CLAY_ID("root")}) {
        nt_ui_group_begin(s_fx.ctx, NULL, NULL, 1.0F);
        {
            nt_ui_label(s_fx.ctx, NULL, "In group", &s_label_style);
        }
        nt_ui_group_end(s_fx.ctx);
    }
    nt_ui_end(s_fx.ctx);

    /* TEXT cmd emitted (no IMAGE for group). */
    const Clay_RenderCommand *txt = find_first_text_cmd(s_fx.ctx);
    TEST_ASSERT_NOT_NULL(txt);
    /* No IMAGE cmd for group. */
    const Clay_RenderCommand *img = find_first_image_cmd(s_fx.ctx);
    TEST_ASSERT_NULL(img);
}

/* ---- Test 5: group with opacity ---- */
static void test_group_with_opacity(void) {
    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse);
    CLAY({.id = CLAY_ID("root")}) {
        nt_ui_group_begin(s_fx.ctx, NULL, NULL, 0.5F);
        {
            nt_ui_label(s_fx.ctx, NULL, "Half opacity", &s_label_style);
        }
        nt_ui_group_end(s_fx.ctx);
    }
    nt_ui_end(s_fx.ctx);

    /* Walk succeeds, stacks balanced. */
    TEST_PASS();
}

/* ---- Test 6: panel payload carries atlas/region ---- */
static void test_panel_payload_carries_atlas(void) {
    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse);
    CLAY({.id = CLAY_ID("root")}) {
        nt_ui_panel_begin(s_fx.ctx, NULL, s_fx.atlas.handle, s_fx.atlas.white_region_idx, &s_panel_style, NULL, 1.0F);
        nt_ui_panel_end(s_fx.ctx);
    }
    nt_ui_end(s_fx.ctx);

    const Clay_RenderCommand *c = find_first_image_cmd(s_fx.ctx);
    TEST_ASSERT_NOT_NULL(c);
    const nt_ui_image_payload_t *p = (const nt_ui_image_payload_t *)c->renderData.image.imageData;
    TEST_ASSERT_EQUAL_UINT32(s_fx.atlas.handle.id, p->atlas.id);
    TEST_ASSERT_EQUAL_UINT32(s_fx.atlas.white_region_idx, p->region_index);
}

/* ---- Test 7: nested panel + group ---- */
static void test_nested_panel_group(void) {
    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse);
    CLAY({.id = CLAY_ID("root")}) {
        nt_ui_panel_begin(s_fx.ctx, NULL, s_fx.atlas.handle, s_fx.atlas.white_region_idx, &s_panel_style, NULL, 1.0F);
        {
            nt_ui_group_begin(s_fx.ctx, NULL, NULL, 0.8F);
            {
                nt_ui_label(s_fx.ctx, NULL, "Nested", &s_label_style);
            }
            nt_ui_group_end(s_fx.ctx);
        }
        nt_ui_panel_end(s_fx.ctx);
    }
    nt_ui_end(s_fx.ctx);

    /* Walk succeeds (both stacks balanced at depth 2). */
    const Clay_RenderCommand *img = find_first_image_cmd(s_fx.ctx);
    TEST_ASSERT_NOT_NULL(img);
    const Clay_RenderCommand *txt = find_first_text_cmd(s_fx.ctx);
    TEST_ASSERT_NOT_NULL(txt);
}

/* ---- Death tests (NT_ASSERT_FULL only) ---- */
#if NT_ASSERT_MODE == NT_ASSERT_FULL

/* ---- Test 8: panel_begin with NULL style asserts ---- */
static void test_panel_null_style_asserts(void) {
    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse);
    CLAY({.id = CLAY_ID("root")}) { NT_TEST_EXPECT_ASSERT(nt_ui_panel_begin(s_fx.ctx, NULL, s_fx.atlas.handle, 0, NULL, NULL, 1.0F)); }
    nt_ui_end(s_fx.ctx);
}

/* ---- Test 9: panel_begin with invalid atlas asserts ---- */
static void test_panel_invalid_atlas_asserts(void) {
    nt_resource_t bad = {.id = 0};
    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse);
    CLAY({.id = CLAY_ID("root")}) { NT_TEST_EXPECT_ASSERT(nt_ui_panel_begin(s_fx.ctx, NULL, bad, 0, &s_panel_style, NULL, 1.0F)); }
    nt_ui_end(s_fx.ctx);
}

#endif /* NT_ASSERT_MODE == NT_ASSERT_FULL */

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_panel_begin_end_balanced);
    RUN_TEST(test_panel_with_transform);
    RUN_TEST(test_panel_null_transform);
    RUN_TEST(test_group_begin_end_balanced);
    RUN_TEST(test_group_with_opacity);
    RUN_TEST(test_panel_payload_carries_atlas);
    RUN_TEST(test_nested_panel_group);
#if NT_ASSERT_MODE == NT_ASSERT_FULL
    RUN_TEST(test_panel_null_style_asserts);
    RUN_TEST(test_panel_invalid_atlas_asserts);
#endif
    return UNITY_END();
}
