/* Unit tests for nt_ui_image widget (Phase 54 Plan 05).
 *
 * Tests follow the nt_ui_label test pattern: walker fixture with stub
 * backend, death tests gated to NT_ASSERT_FULL mode. */

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
#include "unity.h"

alignas(NT_UI_ARENA_ALIGN) static uint8_t s_arena[NT_UI_TEST_ARENA_SIZE];
static ui_walker_fixture_t s_fx;

/* Default untinted style. */
static const nt_ui_image_style_t s_style_default = {
    .color_packed = 0xFFFFFFFF,
    .flip_bits = 0,
    .slice9_lrtb = {0, 0, 0, 0},
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

/* ---- Test 1: basic image emits IMAGE command ---- */
static void test_image_basic(void) {
    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    CLAY({.id = CLAY_ID("root")}) { nt_ui_image(s_fx.ctx, NULL, s_fx.atlas.handle, s_fx.atlas.white_region_idx, &s_style_default); }
    nt_ui_end(s_fx.ctx);

    const Clay_RenderCommand *c = find_first_image_cmd(s_fx.ctx);
    TEST_ASSERT_NOT_NULL(c);
    TEST_ASSERT_NOT_NULL(c->renderData.image.imageData);

    /* Payload carries the atlas handle and region index. */
    const nt_ui_image_payload_t *p = (const nt_ui_image_payload_t *)c->renderData.image.imageData;
    TEST_ASSERT_EQUAL_UINT32(s_fx.atlas.handle.id, p->atlas.id);
    TEST_ASSERT_EQUAL_UINT32(s_fx.atlas.white_region_idx, p->region_index);
}

/* ---- Test 2: slice9 override copies to payload ---- */
static void test_image_slice9_override(void) {
    static const nt_ui_image_style_t s = {
        .color_packed = 0xFFFFFFFF,
        .flip_bits = 0,
        .slice9_lrtb = {4, 4, 4, 4},
    };
    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    CLAY({.id = CLAY_ID("root")}) { nt_ui_image(s_fx.ctx, NULL, s_fx.atlas.handle, s_fx.atlas.white_region_idx, &s); }
    nt_ui_end(s_fx.ctx);

    const Clay_RenderCommand *c = find_first_image_cmd(s_fx.ctx);
    TEST_ASSERT_NOT_NULL(c);
    const nt_ui_image_payload_t *p = (const nt_ui_image_payload_t *)c->renderData.image.imageData;
    TEST_ASSERT_EQUAL_UINT16(4, p->slice9_override[0]);
    TEST_ASSERT_EQUAL_UINT16(4, p->slice9_override[1]);
    TEST_ASSERT_EQUAL_UINT16(4, p->slice9_override[2]);
    TEST_ASSERT_EQUAL_UINT16(4, p->slice9_override[3]);
}

/* ---- Test 3: tint color unpacked to Clay_Color ---- */
static void test_image_tint_color(void) {
    static const nt_ui_image_style_t s = {
        .color_packed = 0x80FF8040, /* A=0x80, B=0xFF, G=0x80, R=0x40 */
        .flip_bits = 0,
        .slice9_lrtb = {0, 0, 0, 0},
    };
    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    CLAY({.id = CLAY_ID("root")}) { nt_ui_image(s_fx.ctx, NULL, s_fx.atlas.handle, s_fx.atlas.white_region_idx, &s); }
    nt_ui_end(s_fx.ctx);

    const Clay_RenderCommand *c = find_first_image_cmd(s_fx.ctx);
    TEST_ASSERT_NOT_NULL(c);
    /* backgroundColor carries the tint. */
    TEST_ASSERT_EQUAL_INT32(64, (int32_t)c->renderData.image.backgroundColor.r);
    TEST_ASSERT_EQUAL_INT32(128, (int32_t)c->renderData.image.backgroundColor.g);
    TEST_ASSERT_EQUAL_INT32(255, (int32_t)c->renderData.image.backgroundColor.b);
    TEST_ASSERT_EQUAL_INT32(128, (int32_t)c->renderData.image.backgroundColor.a);
}

/* ---- Test 4: element_data passthrough ---- */
static void test_image_element_data_passthrough(void) {
    int marker = 77;
    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    CLAY({.id = CLAY_ID("root")}) { nt_ui_image(s_fx.ctx, NT_UI_DATA_FULL(5, &marker), s_fx.atlas.handle, s_fx.atlas.white_region_idx, &s_style_default); }
    nt_ui_end(s_fx.ctx);

    const Clay_RenderCommand *c = find_first_image_cmd(s_fx.ctx);
    TEST_ASSERT_NOT_NULL(c);
    TEST_ASSERT_NOT_NULL(c->userData);
    const nt_ui_element_data_t *d = (const nt_ui_element_data_t *)c->userData;
    TEST_ASSERT_EQUAL_UINT8(5U, d->layer);
    TEST_ASSERT_EQUAL_PTR(&marker, d->user_data);
}

/* ---- Test 5: flip_bits copied to payload ---- */
static void test_image_flip_bits(void) {
    static const nt_ui_image_style_t s = {
        .color_packed = 0xFFFFFFFF,
        .flip_bits = 3, /* FLIP_X | FLIP_Y */
        .slice9_lrtb = {0, 0, 0, 0},
    };
    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    CLAY({.id = CLAY_ID("root")}) { nt_ui_image(s_fx.ctx, NULL, s_fx.atlas.handle, s_fx.atlas.white_region_idx, &s); }
    nt_ui_end(s_fx.ctx);

    const Clay_RenderCommand *c = find_first_image_cmd(s_fx.ctx);
    TEST_ASSERT_NOT_NULL(c);
    const nt_ui_image_payload_t *p = (const nt_ui_image_payload_t *)c->renderData.image.imageData;
    TEST_ASSERT_EQUAL_UINT8(3, p->flip_bits);
}

/* ---- Test 6: flags + origin copied to payload ---- */
static void test_image_flags_origin(void) {
    static const nt_ui_image_style_t s = {
        .color_packed = 0xFFFFFFFF,
        .origin_x = 0.25F,
        .origin_y = 0.75F,
        .flip_bits = 0,
        .flags = NT_UI_IMAGE_ORIGIN_OVERRIDE | NT_UI_IMAGE_SLICE9_OVERRIDE,
    };
    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    CLAY({.id = CLAY_ID("root")}) { nt_ui_image(s_fx.ctx, NULL, s_fx.atlas.handle, s_fx.atlas.white_region_idx, &s); }
    nt_ui_end(s_fx.ctx);

    const Clay_RenderCommand *c = find_first_image_cmd(s_fx.ctx);
    TEST_ASSERT_NOT_NULL(c);
    const nt_ui_image_payload_t *p = (const nt_ui_image_payload_t *)c->renderData.image.imageData;
    TEST_ASSERT_EQUAL_UINT8(NT_UI_IMAGE_ORIGIN_OVERRIDE | NT_UI_IMAGE_SLICE9_OVERRIDE, p->flags);
    TEST_ASSERT_TRUE(p->origin_x == 0.25F); /* NOLINT(cert-flp30-c) exact literal */
    TEST_ASSERT_TRUE(p->origin_y == 0.75F); /* NOLINT(cert-flp30-c) exact literal */
}

/* ---- Test 7: defaults() sets origin to 0.5 and flags to 0 ---- */
static void test_image_style_defaults(void) {
    nt_ui_image_style_t d = nt_ui_image_style_defaults();
    TEST_ASSERT_EQUAL_UINT32(0xFFFFFFFF, d.color_packed);
    TEST_ASSERT_TRUE(d.origin_x == 0.5F); /* NOLINT(cert-flp30-c) exact literal */
    TEST_ASSERT_TRUE(d.origin_y == 0.5F); /* NOLINT(cert-flp30-c) exact literal */
    TEST_ASSERT_EQUAL_UINT8(0, d.flags);
    TEST_ASSERT_EQUAL_UINT8(0, d.flip_bits);
}

/* ---- Death tests (NT_ASSERT_FULL only) ---- */
#if NT_ASSERT_MODE == NT_ASSERT_FULL

/* ---- Test 8: NULL style asserts ---- */
static void test_image_null_style_asserts(void) {
    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    CLAY({.id = CLAY_ID("root")}) { NT_TEST_EXPECT_ASSERT(nt_ui_image(s_fx.ctx, NULL, s_fx.atlas.handle, 0, NULL)); }
    nt_ui_end(s_fx.ctx);
}

/* ---- Test 9: invalid atlas (id=0) asserts ---- */
static void test_image_invalid_atlas_asserts(void) {
    nt_resource_t bad = {.id = 0};
    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    CLAY({.id = CLAY_ID("root")}) { NT_TEST_EXPECT_ASSERT(nt_ui_image(s_fx.ctx, NULL, bad, 0, &s_style_default)); }
    nt_ui_end(s_fx.ctx);
}

#endif /* NT_ASSERT_MODE == NT_ASSERT_FULL */

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_image_basic);
    RUN_TEST(test_image_slice9_override);
    RUN_TEST(test_image_tint_color);
    RUN_TEST(test_image_element_data_passthrough);
    RUN_TEST(test_image_flip_bits);
    RUN_TEST(test_image_flags_origin);
    RUN_TEST(test_image_style_defaults);
#if NT_ASSERT_MODE == NT_ASSERT_FULL
    RUN_TEST(test_image_null_style_asserts);
    RUN_TEST(test_image_invalid_atlas_asserts);
#endif
    return UNITY_END();
}
