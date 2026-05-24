/* Unit tests for nt_ui_label widget (Phase 53 Plan 04).
 *
 * Note on floats: Unity is compiled with UNITY_EXCLUDE_FLOAT (see
 * deps/unity/CMakeLists.txt) -- TEST_ASSERT_EQUAL_FLOAT fails at runtime.
 * Clay_Color components are float, but all values used here are
 * integer-valued (e.g. 255.0F, 128.0F), so we compare as int32_t after
 * truncation. Same pattern as test_nt_ui_measure_cb.c. */

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
#include "ui/nt_ui_internal.h"
#include "ui/nt_ui_label.h"
#include "unity.h"

alignas(NT_UI_ARENA_ALIGN) static uint8_t s_arena[NT_UI_TEST_ARENA_SIZE];
static ui_walker_fixture_t s_fx;

/* Shared style for happy-path tests: font_id=0 (stub_font), size=14, red. */
static const nt_ui_label_style_t s_style_body = {
    .font_id = 0, .font_size = 14, .color = {255.0F, 0.0F, 0.0F, 255.0F},
    /* wrap_mode = 0 = CLAY_TEXT_WRAP_WORDS, align = 0 = CLAY_TEXT_ALIGN_LEFT */
};

void setUp(void) {
    nt_test_assert_install();
    ui_walker_fixture_init(&s_fx, s_arena, sizeof s_arena, UI_WALKER_FX_BIND_ALL);
}

void tearDown(void) { ui_walker_fixture_shutdown(&s_fx); }

/* Helper: walks the frozen_cmds array and returns the first TEXT cmd, or NULL. */
static const Clay_RenderCommand *find_first_text_cmd(const nt_ui_context_t *ctx) {
    for (int32_t i = 0; i < ctx->frozen_cmds.length; ++i) {
        const Clay_RenderCommand *c = &ctx->frozen_cmds.internalArray[i];
        if (c->commandType == CLAY_RENDER_COMMAND_TYPE_TEXT) {
            return c;
        }
    }
    return NULL;
}

/* ---- Test 1: happy path ---- */
static void test_label_emits_text_with_style_color(void) {
    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse);
    CLAY({.id = CLAY_ID("root")}) { nt_ui_label(s_fx.ctx, NULL, "Hello", &s_style_body); }
    nt_ui_end(s_fx.ctx);

    const Clay_RenderCommand *c = find_first_text_cmd(s_fx.ctx);
    TEST_ASSERT_NOT_NULL(c);
    TEST_ASSERT_EQUAL_UINT16(0U, c->renderData.text.fontId);
    TEST_ASSERT_EQUAL_UINT16(14U, c->renderData.text.fontSize);
    /* UNITY_EXCLUDE_FLOAT: compare colors as int32_t after truncation. */
    TEST_ASSERT_EQUAL_INT32(255, (int32_t)c->renderData.text.textColor.r);
    TEST_ASSERT_EQUAL_INT32(0, (int32_t)c->renderData.text.textColor.g);
    TEST_ASSERT_EQUAL_INT32(0, (int32_t)c->renderData.text.textColor.b);
    TEST_ASSERT_EQUAL_INT32(255, (int32_t)c->renderData.text.textColor.a);
}

/* ---- Death tests (NT_ASSERT_FULL only) ----
 * NT_TEST_EXPECT_ASSERT relies on the nt_assert_handler hook (setjmp/longjmp
 * via test_helpers/nt_assert_trap). The handler is only called in
 * NT_ASSERT_FULL mode -- NT_ASSERT_TRAP (release default) traps directly via
 * __builtin_trap() with no handler dispatch (engine/core/nt_assert.h:39-56).
 * The 4 death tests below are gated to FULL mode so this binary also passes
 * in native-release. Same gating is needed across all NT_TEST_EXPECT_ASSERT
 * users (test_nt_ui_measure_cb, test_nt_ui_walker_dispatch, ...) -- a Phase
 * 52 infrastructure cleanup outside the scope of Plan 53-04. */
#if NT_ASSERT_MODE == NT_ASSERT_FULL

/* ---- Test 2: death test -- NULL style asserts ----
 * Assert fires at function entry BEFORE any Clay state mutation, so the
 * longjmp returns to setjmp inside the CLAY for-loop body. The loop
 * continuation (Clay__CloseElement) still runs, leaving Clay balanced;
 * nt_ui_end then closes the frame normally. */
static void test_label_null_style_asserts(void) {
    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse);
    CLAY({.id = CLAY_ID("root")}) { NT_TEST_EXPECT_ASSERT(nt_ui_label(s_fx.ctx, NULL, "X", NULL)); }
    nt_ui_end(s_fx.ctx);
}

/* ---- Test 3: death test -- font_id out of range ---- */
static void test_label_out_of_range_font_asserts(void) {
    static const nt_ui_label_style_t bad = {
        .font_id = NT_UI_MAX_FONTS,
        .font_size = 14,
        .color = {255.0F, 255.0F, 255.0F, 255.0F},
    };
    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse);
    CLAY({.id = CLAY_ID("root")}) { NT_TEST_EXPECT_ASSERT(nt_ui_label(s_fx.ctx, NULL, "X", &bad)); }
    nt_ui_end(s_fx.ctx);
}

/* ---- Test 4: death test -- unbound font slot ----
 * Slot 3 is in range but never bound by the fixture (only slot 0 is). */
static void test_label_unbound_font_asserts(void) {
    static const nt_ui_label_style_t bad = {
        .font_id = 3,
        .font_size = 14,
        .color = {255.0F, 255.0F, 255.0F, 255.0F},
    };
    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse);
    CLAY({.id = CLAY_ID("root")}) { NT_TEST_EXPECT_ASSERT(nt_ui_label(s_fx.ctx, NULL, "X", &bad)); }
    nt_ui_end(s_fx.ctx);
}

/* ---- Test 5: death test -- font_size == 0 ---- */
static void test_label_zero_font_size_asserts(void) {
    static const nt_ui_label_style_t bad = {
        .font_id = 0,
        .font_size = 0,
        .color = {255.0F, 255.0F, 255.0F, 255.0F},
    };
    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse);
    CLAY({.id = CLAY_ID("root")}) { NT_TEST_EXPECT_ASSERT(nt_ui_label(s_fx.ctx, NULL, "X", &bad)); }
    nt_ui_end(s_fx.ctx);
}

#endif /* NT_ASSERT_MODE == NT_ASSERT_FULL */

/* ---- Test 6: zero-init wrap_mode + align ----
 * wrap_mode / align rely on zero-init giving CLAY_TEXT_WRAP_WORDS (0)
 * + CLAY_TEXT_ALIGN_LEFT (0). Clay_TextRenderData does NOT surface
 * wrapMode/textAlignment (they're consumed by layout, not exposed
 * post-layout), so we (a) verify a TEXT cmd was emitted (no enum-cast
 * crash) and (b) document the Clay enum 0-mapping via static asserts. */
static void test_label_zero_init_wraps_words_left(void) {
    static const nt_ui_label_style_t s = {
        .font_id = 0,
        .font_size = 14,
        .color = {255.0F, 255.0F, 255.0F, 255.0F},
    };
    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse);
    CLAY({.id = CLAY_ID("root")}) { nt_ui_label(s_fx.ctx, NULL, "ABC DEF", &s); }
    nt_ui_end(s_fx.ctx);

    const Clay_RenderCommand *c = find_first_text_cmd(s_fx.ctx);
    TEST_ASSERT_NOT_NULL(c);
    /* Unity convention: TEST_ASSERT_EQUAL_INT(expected, actual). The
     * expected literal 0 goes FIRST; the cast Clay enum value goes SECOND
     * (W-2). If Clay ever renumbers, error message reads
     * "expected 0, was N" which is the correct direction. */
    TEST_ASSERT_EQUAL_INT(0, (int)CLAY_TEXT_WRAP_WORDS);
    TEST_ASSERT_EQUAL_INT(0, (int)CLAY_TEXT_ALIGN_LEFT);
}

/* ---- Test 7: full-field passthrough including full RGBA ----
 * line_height + letter_tracking pass through to Clay_TextRenderData;
 * full RGBA verified per channel (W-3). wrap_mode/align are NOT verified
 * here because Clay_TextRenderData does not surface wrapMode/textAlignment
 * (they're consumed during layout). Cast safety for those two fields is
 * a compile-time check satisfied by Plan 03 (wired in nt_ui_label.c with
 * explicit (Clay_TextElementConfigWrapMode) and (Clay_TextAlignment) casts
 * under -Wconversion -Werror). Using CLAY_TEXT_WRAP_NONE here would also
 * cause Clay to skip emitting the TEXT cmd when measure cb returns 0 width
 * (stub_font has no resource data) -- defeating the passthrough check. */
static void test_label_full_field_passthrough(void) {
    static const nt_ui_label_style_t s = {
        .font_id = 0, .font_size = 16, .color = {128.0F, 64.0F, 32.0F, 200.0F}, .line_height = 24, .letter_tracking = 4,
        /* wrap_mode + align left zero-init (WORDS + LEFT) -- see comment above. */
    };
    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse);
    /* Stub font's measure cb returns {0,0}; with non-zero letterSpacing
     * Clay subtracts at clay.h:1677 producing preferredDimensions.width<0,
     * and the resulting bbox is then offscreen-culled (clay.h:2473). Disable
     * culling so the TEXT cmd survives for our passthrough check. */
    Clay_SetCullingEnabled(false);
    CLAY({.id = CLAY_ID("root")}) { nt_ui_label(s_fx.ctx, NULL, "Hello", &s); }
    nt_ui_end(s_fx.ctx);

    const Clay_RenderCommand *c = find_first_text_cmd(s_fx.ctx);
    TEST_ASSERT_NOT_NULL(c);
    TEST_ASSERT_EQUAL_UINT16(0U, c->renderData.text.fontId);
    TEST_ASSERT_EQUAL_UINT16(16U, c->renderData.text.fontSize);
    TEST_ASSERT_EQUAL_UINT16(24U, c->renderData.text.lineHeight);
    TEST_ASSERT_EQUAL_UINT16(4U, c->renderData.text.letterSpacing);
    /* W-3: verify all 4 RGBA channels for {128, 64, 32, 200}. */
    TEST_ASSERT_EQUAL_INT32(128, (int32_t)c->renderData.text.textColor.r);
    TEST_ASSERT_EQUAL_INT32(64, (int32_t)c->renderData.text.textColor.g);
    TEST_ASSERT_EQUAL_INT32(32, (int32_t)c->renderData.text.textColor.b);
    TEST_ASSERT_EQUAL_INT32(200, (int32_t)c->renderData.text.textColor.a);
}

/* ---- Test 8: per-call override (STYLE-OVERRIDE-01) ----
 * Stack-local copy + mutate + pass pointer. Test passing proves Clay
 * COPIED the config from the stack-local before s went out of scope --
 * a use-after-free would crash inside find_first_text_cmd's walk over
 * Clay's frozen render commands. */
static void test_label_per_call_override(void) {
    nt_ui_label_style_t s = s_style_body; /* copy static const */
    s.color = (Clay_Color){32.0F, 200.0F, 64.0F, 255.0F};

    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse);
    CLAY({.id = CLAY_ID("root")}) { nt_ui_label(s_fx.ctx, NULL, "Override", &s); }
    nt_ui_end(s_fx.ctx);

    const Clay_RenderCommand *c = find_first_text_cmd(s_fx.ctx);
    TEST_ASSERT_NOT_NULL(c);
    TEST_ASSERT_EQUAL_INT32(32, (int32_t)c->renderData.text.textColor.r);
    TEST_ASSERT_EQUAL_INT32(200, (int32_t)c->renderData.text.textColor.g);
    TEST_ASSERT_EQUAL_INT32(64, (int32_t)c->renderData.text.textColor.b);
}

/* ---- Test 9: empty text "" is accepted (no assert) ----
 * "" is non-NULL so the text != NULL assert passes; Clay handles
 * length=0 via the measure cb's len==0 early-return path. */
static void test_label_empty_text_accepted(void) {
    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse);
    CLAY({.id = CLAY_ID("root")}) { nt_ui_label(s_fx.ctx, NULL, "", &s_style_body); }
    nt_ui_end(s_fx.ctx);
    TEST_PASS();
}

/* ---- Test 10: element_data passthrough -- layer + user_data reach the TEXT cmd
 * via textConfig.userData, so the walker can read .layer for batch sort and
 * a game pointer for hit detection. */
static void test_label_element_data_passthrough(void) {
    int marker = 42;
    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse);
    CLAY({.id = CLAY_ID("root")}) { nt_ui_label(s_fx.ctx, NT_UI_DATA_FULL(7, &marker), "Hi", &s_style_body); }
    nt_ui_end(s_fx.ctx);

    const Clay_RenderCommand *c = find_first_text_cmd(s_fx.ctx);
    TEST_ASSERT_NOT_NULL(c);
    TEST_ASSERT_NOT_NULL(c->userData);
    const nt_ui_element_data_t *d = (const nt_ui_element_data_t *)c->userData;
    TEST_ASSERT_EQUAL_UINT8(7U, d->layer);
    TEST_ASSERT_EQUAL_PTR(&marker, d->user_data);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_label_emits_text_with_style_color);
#if NT_ASSERT_MODE == NT_ASSERT_FULL
    RUN_TEST(test_label_null_style_asserts);
    RUN_TEST(test_label_out_of_range_font_asserts);
    RUN_TEST(test_label_unbound_font_asserts);
    RUN_TEST(test_label_zero_font_size_asserts);
#endif
    RUN_TEST(test_label_zero_init_wraps_words_left);
    RUN_TEST(test_label_full_field_passthrough);
    RUN_TEST(test_label_per_call_override);
    RUN_TEST(test_label_empty_text_accepted);
    RUN_TEST(test_label_element_data_passthrough);
    return UNITY_END();
}
