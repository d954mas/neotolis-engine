/* Unit tests for nt_ui_label.
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
#include "font/nt_font.h"
#include "renderers/nt_text_renderer.h"
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
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
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

/* Death tests (NT_ASSERT_FULL only): NT_TEST_EXPECT_ASSERT needs the
 * setjmp/longjmp handler installed by test_helpers/nt_assert_trap, which is
 * only wired in FULL mode (TRAP traps directly via __builtin_trap). */
#if NT_ASSERT_MODE == NT_ASSERT_FULL

/* Assert fires before any Clay state mutation; the CLAY for-loop's
 * continuation still runs Clay__CloseElement so the frame closes balanced. */
static void test_label_null_style_asserts(void) {
    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
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
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    CLAY({.id = CLAY_ID("root")}) { NT_TEST_EXPECT_ASSERT(nt_ui_label(s_fx.ctx, NULL, "X", &bad)); }
    nt_ui_end(s_fx.ctx);
}

/* Slot 3 is in range but never bound by the fixture (only slot 0 is). */
static void test_label_unbound_font_asserts(void) {
    static const nt_ui_label_style_t bad = {
        .font_id = 3,
        .font_size = 14,
        .color = {255.0F, 255.0F, 255.0F, 255.0F},
    };
    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
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
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    CLAY({.id = CLAY_ID("root")}) { NT_TEST_EXPECT_ASSERT(nt_ui_label(s_fx.ctx, NULL, "X", &bad)); }
    nt_ui_end(s_fx.ctx);
}

#endif /* NT_ASSERT_MODE == NT_ASSERT_FULL */

/* Zero-init wrap_mode/align rely on CLAY_TEXT_WRAP_WORDS == 0 and
 * CLAY_TEXT_ALIGN_LEFT == 0. Clay_TextRenderData does not surface these
 * fields, so we pin the enum-zero invariant + TEXT cmd emission. */
static void test_label_zero_init_wraps_words_left(void) {
    static const nt_ui_label_style_t s = {
        .font_id = 0,
        .font_size = 14,
        .color = {255.0F, 255.0F, 255.0F, 255.0F},
    };
    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    CLAY({.id = CLAY_ID("root")}) { nt_ui_label(s_fx.ctx, NULL, "ABC DEF", &s); }
    nt_ui_end(s_fx.ctx);

    const Clay_RenderCommand *c = find_first_text_cmd(s_fx.ctx);
    TEST_ASSERT_NOT_NULL(c);
    /* expected (literal 0) first so a Clay renumber prints "expected 0, was N". */
    TEST_ASSERT_EQUAL_INT(0, (int)CLAY_TEXT_WRAP_WORDS);
    TEST_ASSERT_EQUAL_INT(0, (int)CLAY_TEXT_ALIGN_LEFT);
}

/* line_height + letter_tracking + RGBA reach Clay_TextRenderData. wrap_mode/
 * align are layout-only fields not surfaced post-layout. */
static void test_label_full_field_passthrough(void) {
    static const nt_ui_label_style_t s = {
        .font_id = 0, .font_size = 16, .color = {128.0F, 64.0F, 32.0F, 200.0F}, .line_height = 24, .letter_tracking = 4,
        /* wrap_mode + align left zero-init (WORDS + LEFT). */
    };
    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    /* Stub font's measure cb returns {0,0}; with non-zero letterSpacing the
     * resulting bbox is offscreen-culled. Disable culling to keep the TEXT cmd. */
    Clay_SetCullingEnabled(false);
    CLAY({.id = CLAY_ID("root")}) { nt_ui_label(s_fx.ctx, NULL, "Hello", &s); }
    nt_ui_end(s_fx.ctx);

    const Clay_RenderCommand *c = find_first_text_cmd(s_fx.ctx);
    TEST_ASSERT_NOT_NULL(c);
    TEST_ASSERT_EQUAL_UINT16(0U, c->renderData.text.fontId);
    TEST_ASSERT_EQUAL_UINT16(16U, c->renderData.text.fontSize);
    TEST_ASSERT_EQUAL_UINT16(24U, c->renderData.text.lineHeight);
    TEST_ASSERT_EQUAL_UINT16(4U, c->renderData.text.letterSpacing);
    /* Verify all 4 RGBA channels for {128, 64, 32, 200}. */
    TEST_ASSERT_EQUAL_INT32(128, (int32_t)c->renderData.text.textColor.r);
    TEST_ASSERT_EQUAL_INT32(64, (int32_t)c->renderData.text.textColor.g);
    TEST_ASSERT_EQUAL_INT32(32, (int32_t)c->renderData.text.textColor.b);
    TEST_ASSERT_EQUAL_INT32(200, (int32_t)c->renderData.text.textColor.a);
}

/* Stack-local style copy + pass pointer: Clay must COPY the config before s
 * goes out of scope (else find_first_text_cmd would walk freed memory). */
static void test_label_per_call_override(void) {
    nt_ui_label_style_t s = s_style_body; /* copy static const */
    s.color = (Clay_Color){32.0F, 200.0F, 64.0F, 255.0F};

    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    CLAY({.id = CLAY_ID("root")}) { nt_ui_label(s_fx.ctx, NULL, "Override", &s); }
    nt_ui_end(s_fx.ctx);

    const Clay_RenderCommand *c = find_first_text_cmd(s_fx.ctx);
    TEST_ASSERT_NOT_NULL(c);
    TEST_ASSERT_EQUAL_INT32(32, (int32_t)c->renderData.text.textColor.r);
    TEST_ASSERT_EQUAL_INT32(200, (int32_t)c->renderData.text.textColor.g);
    TEST_ASSERT_EQUAL_INT32(64, (int32_t)c->renderData.text.textColor.b);
}

/* Empty "" is non-NULL so the text-pointer assert passes; Clay handles
 * length=0 via the measure cb's early-return path. */
static void test_label_empty_text_accepted(void) {
    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
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
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    CLAY({.id = CLAY_ID("root")}) { nt_ui_label(s_fx.ctx, NT_UI_DATA_FULL(7, &marker), "Hi", &s_style_body); }
    nt_ui_end(s_fx.ctx);

    const Clay_RenderCommand *c = find_first_text_cmd(s_fx.ctx);
    TEST_ASSERT_NOT_NULL(c);
    TEST_ASSERT_NOT_NULL(c->userData);
    const nt_ui_element_data_t *d = (const nt_ui_element_data_t *)c->userData;
    TEST_ASSERT_EQUAL_UINT8(7U, d->layer);
    TEST_ASSERT_EQUAL_PTR(&marker, d->user_data);
}

/* Label must scratch-copy its text: a stack buffer scribbled AFTER the call (and
 * reused for a second label) must not corrupt the first label's emitted content. */
static void test_label_scratch_copies_text(void) {
    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    char buf[32];
    (void)strcpy(buf, "ALPHA");
    CLAY({.id = CLAY_ID("root")}) {
        nt_ui_label(s_fx.ctx, NULL, buf, &s_style_body);
        /* Scribble + reuse the same buffer like the demo's shared snprintf buffer. */
        (void)strcpy(buf, "ZZZZZZZ");
        nt_ui_label(s_fx.ctx, NULL, buf, &s_style_body);
        (void)strcpy(buf, "OVERWRITTEN-GARBAGE");
    }
    nt_ui_end(s_fx.ctx);

    /* Walk all TEXT cmds; the first must still read "ALPHA", the second "ZZZZZZZ". */
    const char *first = NULL;
    int32_t first_len = 0;
    const char *second = NULL;
    int32_t second_len = 0;
    for (int32_t i = 0; i < s_fx.ctx->frozen_cmds.length; ++i) {
        const Clay_RenderCommand *c = &s_fx.ctx->frozen_cmds.internalArray[i];
        if (c->commandType != CLAY_RENDER_COMMAND_TYPE_TEXT) {
            continue;
        }
        if (first == NULL) {
            first = c->renderData.text.stringContents.chars;
            first_len = c->renderData.text.stringContents.length;
        } else if (second == NULL) {
            second = c->renderData.text.stringContents.chars;
            second_len = c->renderData.text.stringContents.length;
        }
    }
    TEST_ASSERT_NOT_NULL(first);
    TEST_ASSERT_NOT_NULL(second);
    TEST_ASSERT_EQUAL_INT32(5, first_len);
    TEST_ASSERT_EQUAL_MEMORY("ALPHA", first, 5);
    TEST_ASSERT_EQUAL_INT32(7, second_len);
    TEST_ASSERT_EQUAL_MEMORY("ZZZZZZZ", second, 7);
}

/* T7: nt_ui_label_sized overrides font_size from style. */
static void test_label_sized_overrides_font_size(void) {
    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    CLAY({.id = CLAY_ID("root")}) { nt_ui_label_sized(s_fx.ctx, NULL, "Sized", &s_style_body, 28U); }
    nt_ui_end(s_fx.ctx);

    const Clay_RenderCommand *c = find_first_text_cmd(s_fx.ctx);
    TEST_ASSERT_NOT_NULL(c);
    TEST_ASSERT_EQUAL_UINT16(28U, c->renderData.text.fontSize);
    TEST_ASSERT_EQUAL_INT32(255, (int32_t)c->renderData.text.textColor.r);
}

/* a label whose style carries decoration sets the sticky renderer decoration state per draw
 * (bold->synth weight, outline width, underline) through the walker, then resets after (no leak). Pinned
 * via the renderer observe hooks (the stub font emits no glyphs, but draw_n observes the state at entry). */
static void test_label_decoration_wires_and_resets_setters(void) {
    nt_font_test_set_metrics(s_fx.stub_font, 1000, 800, -200, 1000);
    nt_text_renderer_test_reset_call_counters();

    static const nt_ui_label_style_t s = {
        .font_id = 0,
        .font_size = 16,
        .color = {255.0F, 255.0F, 255.0F, 255.0F},
        .variant = NT_UI_LABEL_VARIANT_BOLD | NT_UI_LABEL_VARIANT_UNDERLINE,
        .outline_w = 2.0F,
        .outline_color = 0xFF0000FFU,
        .shadow_dx = 1.0F,
        .shadow_dy = 1.0F,
        .shadow_color = 0xFF000000U,
    };
    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    Clay_SetCullingEnabled(false); /* stub measure returns {0,0}; keep the TEXT cmd so it reaches emit */
    CLAY({.id = CLAY_ID("root")}) { nt_ui_label(s_fx.ctx, NULL, "Deco", &s); }
    nt_ui_end(s_fx.ctx);
    nt_ui_target_t target = {.viewport = {0, 0, 800, 600}};
    nt_ui_walk(s_fx.ctx, &target);

    TEST_ASSERT_TRUE_MESSAGE(nt_text_renderer_test_max_weight() > 0.0F, "bold label feeds a synthetic weight to the renderer during emit");
    TEST_ASSERT_TRUE_MESSAGE(nt_text_renderer_test_max_outline_width() > 0.0F, "label outline width reaches the renderer");
    TEST_ASSERT_TRUE_MESSAGE(nt_text_renderer_test_saw_underline(), "label underline reaches the renderer");
    TEST_ASSERT_TRUE_MESSAGE(nt_text_renderer_test_weight() == 0.0F, "decoration reset after the label draw (no leak onto later text)");
}

/* Parent opacity must fold into outline/shadow alpha (not just the fill): the walker pre-multiplies
 * only textColor.a, so nt_ui_label_deco_apply folds accum_opacity into the decoration colors — else a
 * faded panel keeps opaque outline/shadow. */
static void test_label_deco_folds_parent_opacity(void) {
    nt_ui_label_deco_t d = {0};
    d.outline_w = 0.06F;
    d.outline_color = 0xFFFFFFFFU; /* opaque white (AABBGGRR): alpha 1.0 */
    d.shadow_dx = 0.1F;
    d.shadow_dy = 0.1F;
    d.shadow_color = 0xFFFFFFFFU; /* alpha > 0 -> shadow active */

    /* Float asserts are excluded in this suite; compare alpha*100 as int (0.5 -> 50, 1.0 -> 100). */
    nt_ui_label_deco_apply(&d, 0.5F); /* half-faded parent */
    TEST_ASSERT_EQUAL_INT(50, (int)((nt_text_renderer_test_outline_color_a() * 100.0F) + 0.5F));
    TEST_ASSERT_EQUAL_INT(50, (int)((nt_text_renderer_test_shadow_color_a() * 100.0F) + 0.5F));

    nt_ui_label_deco_apply(&d, 1.0F); /* opaque parent leaves alpha untouched */
    TEST_ASSERT_EQUAL_INT(100, (int)((nt_text_renderer_test_outline_color_a() * 100.0F) + 0.5F));
    TEST_ASSERT_EQUAL_INT(100, (int)((nt_text_renderer_test_shadow_color_a() * 100.0F) + 0.5F));
    nt_text_renderer_reset_decoration();
}

/* A decorated label that breaks into multiple lines (embedded '\n') must decorate EVERY emitted line.
 * Decoration rides the label's private element_data, which Clay carries identically on every wrapped-line
 * TEXT command (userData is uniform per element), so all lines match -> deco applied == text-command count. */
static void test_label_decoration_applies_to_wrapped_lines(void) {
    nt_font_test_set_metrics(s_fx.stub_font, 1000, 800, -200, 1000);
    nt_ui_test_reset_deco_applied_count();

    static const nt_ui_label_style_t s = {
        .font_id = 0,
        .font_size = 16,
        .color = {255.0F, 255.0F, 255.0F, 255.0F},
        .variant = NT_UI_LABEL_VARIANT_BOLD | NT_UI_LABEL_VARIANT_UNDERLINE,
    };
    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    Clay_SetCullingEnabled(false);
    CLAY({.id = CLAY_ID("root")}) { nt_ui_label(s_fx.ctx, NULL, "AAA\nBBB", &s); }
    nt_ui_end(s_fx.ctx);

    /* Confirm the newline actually produced >=2 TEXT commands, else the repro is void. */
    int32_t text_cmds = 0;
    for (int32_t i = 0; i < s_fx.ctx->frozen_cmds.length; ++i) {
        if (s_fx.ctx->frozen_cmds.internalArray[i].commandType == CLAY_RENDER_COMMAND_TYPE_TEXT) {
            text_cmds++;
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(text_cmds >= 2, "embedded newline must emit >=2 TEXT commands (multi-line repro)");

    nt_ui_target_t target = {.viewport = {0, 0, 800, 600}};
    nt_ui_walk(s_fx.ctx, &target);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE((uint32_t)text_cmds, nt_ui_test_deco_applied_count(), "decoration must apply to EVERY line (uniform element_data), not just the first");
    nt_text_renderer_reset_decoration();
}

/* Headline property of the special-data design: a decorated label built WITH game data keeps the caller's
 * user_data + layer (its private element_data is a COPY of the caller's) AND carries the decoration in
 * .special — decoration and the game pointer coexist. Guards the base-copy in label_attach_decoration. */
static void test_label_decoration_preserves_element_data(void) {
    int marker = 0;
    static const nt_ui_label_style_t s = {
        .font_id = 0,
        .font_size = 14,
        .color = {255.0F, 255.0F, 255.0F, 255.0F},
        .variant = NT_UI_LABEL_VARIANT_BOLD,
    };
    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    Clay_SetCullingEnabled(false);
    CLAY({.id = CLAY_ID("root")}) { nt_ui_label(s_fx.ctx, NT_UI_DATA_FULL(5, &marker), "Deco+data", &s); }
    nt_ui_end(s_fx.ctx);

    const Clay_RenderCommand *c = find_first_text_cmd(s_fx.ctx);
    TEST_ASSERT_NOT_NULL(c);
    const nt_ui_element_data_t *ed = (const nt_ui_element_data_t *)c->userData;
    TEST_ASSERT_NOT_NULL(ed);
    TEST_ASSERT_EQUAL_INT_MESSAGE((int)NT_UI_SPECIAL_TEXT_DECO, (int)ed->special_kind, "decorated label carries text-deco special data");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(5U, ed->layer, "private element_data keeps the caller's layer");
    TEST_ASSERT_EQUAL_PTR_MESSAGE(&marker, ed->user_data, "decorated label keeps the caller's game user_data (coexists with decoration)");
}

/* NEGATIVE: a plain (undecorated) label passes the caller's element_data through unchanged (special_kind
 * NONE), so the walker applies no decoration and the sticky renderer state stays clean. */
static void test_label_plain_no_decoration(void) {
    nt_font_test_set_metrics(s_fx.stub_font, 1000, 800, -200, 1000);
    nt_text_renderer_test_reset_call_counters();
    nt_ui_test_reset_deco_applied_count();
    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    Clay_SetCullingEnabled(false);
    CLAY({.id = CLAY_ID("root")}) { nt_ui_label(s_fx.ctx, NULL, "Plain", &s_style_body); }
    nt_ui_end(s_fx.ctx);
    nt_ui_target_t target = {.viewport = {0, 0, 800, 600}};
    nt_ui_walk(s_fx.ctx, &target);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0U, nt_ui_test_deco_applied_count(), "plain label triggers no decoration apply in the walker");
    TEST_ASSERT_TRUE_MESSAGE(nt_text_renderer_test_max_weight() == 0.0F, "plain label feeds no synthetic weight");
    TEST_ASSERT_FALSE_MESSAGE(nt_text_renderer_test_saw_underline(), "plain label feeds no underline");
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
    RUN_TEST(test_label_scratch_copies_text);
    RUN_TEST(test_label_sized_overrides_font_size);
    RUN_TEST(test_label_decoration_wires_and_resets_setters);
    RUN_TEST(test_label_decoration_applies_to_wrapped_lines);
    RUN_TEST(test_label_decoration_preserves_element_data);
    RUN_TEST(test_label_deco_folds_parent_opacity);
    RUN_TEST(test_label_plain_no_decoration);
    return UNITY_END();
}
