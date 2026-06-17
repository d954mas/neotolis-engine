/* Text-field widget tests (nt_ui_input_text core).
 *
 * Covers the codepoint-aware contract: insert/backspace/arrow over Cyrillic "Аб"
 * (multi-byte UTF-8) moves by CODEPOINT not byte; buffer_size clamp never writes
 * OOB nor splits a multi-byte sequence; the allow-predicate drops filtered chars;
 * click hit-test lands on a codepoint boundary; Tab/Esc arbitrate focus across two
 * fields; the generic dblclick/longpress primitive reports edges.
 *
 * Determinism: a stub font with nt_font_test_set_metrics returns tofu advance =
 * size/2 per codepoint, so caret math is exact. Typed chars are fed straight into
 * the global input char ring (nt_input_buffer_char); physical keys via the
 * set_key/poll edge path. */

#include <stdalign.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "clay.h"
#include "clipboard/nt_clipboard.h"
#include "core/nt_assert.h"
#include "input/nt_input.h"
#include "input/nt_input_internal.h" /* nt_input_buffer_char + nt_input_set_key */
#include "test_helpers/nt_assert_trap.h"
#include "test_helpers/ui_test_arena.h"
#include "test_helpers/ui_walker_fixture.h"
#include "ui/nt_ui.h"
#include "ui/nt_ui_input.h"
#include "ui/nt_ui_internal.h"
#include "unity.h"
#include "utf8/nt_utf8.h"

alignas(NT_UI_ARENA_ALIGN) static uint8_t s_arena[NT_UI_TEST_ARENA_SIZE];
static ui_walker_fixture_t s_fx;
static nt_ui_input_style_t s_style;
static nt_ui_input_props_t s_props; /* per-field behaviour; tests set allow/password/keyboard here */

/* The fake clipboard exposes a test-only availability toggle (no fake header; declared here). */
extern void nt_clipboard_fake_set_available(bool available);

/* Field pinned at a fixed absolute position; non-origin so an axis swap is visible. */
#define IN_X 100.0F
#define IN_Y 80.0F
#define IN_W 240.0F
#define IN_H 28.0F
#define FONT_SIZE 20.0F /* tofu advance = size/2 = 10px per codepoint */
#define GLYPH_W (FONT_SIZE * 0.5F)
#define PAD_X 6.0F

/* Cyrillic "Аб": U+0410 (D0 90) + U+0431 (D0 B1) = 4 bytes. */
#define CP_A 0x0410U
#define CP_B 0x0431U

static const Clay_ElementDeclaration s_field_decl = {
    .layout = {.sizing = {CLAY_SIZING_FIXED(IN_W), CLAY_SIZING_FIXED(IN_H)}},
};

static void init_style(void) {
    s_style = nt_ui_input_style_defaults();
    s_style.text.font_id = 0;
    s_style.text.font_size = FONT_SIZE;
    s_style.text.letter_tracking = 0;
    s_style.placeholder.font_id = 0;
    s_style.placeholder.font_size = FONT_SIZE;
    s_style.pad_x = PAD_X;
    s_style.pad_y = 4.0F;
    s_style.caret_blink_rate = 0.0F;     /* always-on caret -> deterministic render */
    memset(&s_props, 0, sizeof s_props); /* zero-init = plain text field (no filter/mask, default keyboard) */
}

void setUp(void) {
    nt_test_assert_install();
    nt_input_init(); /* clean key + char-ring state per test */
    ui_walker_fixture_init(&s_fx, s_arena, sizeof s_arena, UI_WALKER_FX_BIND_ALL);
    nt_font_test_set_metrics(s_fx.stub_font, 1000, 800, -200, 1000); /* tofu advance = size/2 */
    init_style();
}

void tearDown(void) {
    nt_clipboard_fake_set_available(true); /* a test may flip this false; restore unconditionally so a mid-test assert-fail can't cascade */
    ui_walker_fixture_shutdown(&s_fx);
    nt_input_shutdown();
}

static nt_pointer_t make_pointer(float x, float y, bool down, bool pressed, bool released) {
    nt_pointer_t p = {0};
    p.x = x;
    p.y = y;
    p.active = true;
    p.buttons[NT_BUTTON_LEFT].is_down = down;
    p.buttons[NT_BUTTON_LEFT].is_pressed = pressed;
    p.buttons[NT_BUTTON_LEFT].is_released = released;
    return p;
}

static const nt_pointer_t IDLE_PTR = {.x = 0.0F, .y = 0.0F, .active = true};

/* First IMAGE render command in the frozen list (the field's bg sprite, when bg_art is set). */
static const Clay_RenderCommand *find_first_image_cmd(const nt_ui_context_t *ctx) {
    for (int32_t i = 0; i < ctx->frozen_cmds.length; ++i) {
        const Clay_RenderCommand *c = &ctx->frozen_cmds.internalArray[i];
        if (c->commandType == CLAY_RENDER_COMMAND_TYPE_IMAGE) {
            return c;
        }
    }
    return NULL;
}

/* One frame with a single field. Feeds the given pointer; returns the change flag. */
static bool field_frame(const nt_pointer_t *p, uint32_t id, char *buf, size_t cap, bool enabled, bool *submitted) {
    bool changed = false;
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.016F, p, 1);
    CLAY({.id = CLAY_ID("root"), .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .offset = {.x = IN_X, .y = IN_Y}}}) {
        changed = nt_ui_input_text(s_fx.ctx, NULL, 0, id, buf, cap, &s_props, &s_style, &s_field_decl, enabled, submitted);
    }
    nt_ui_end(s_fx.ctx);
    return changed;
}

/* Warm up so Clay caches the field bbox for next frame's hit-test, and the field is focused. */
static void warmup_focus(uint32_t id, char *buf, size_t cap) {
    (void)field_frame(&IDLE_PTR, id, buf, cap, true, NULL);
    /* Press inside the field to focus it (caret lands via hit-test on the warmed bbox). */
    nt_pointer_t press = make_pointer(IN_X + PAD_X + 1.0F, IN_Y + (IN_H * 0.5F), true, true, false);
    (void)field_frame(&press, id, buf, cap, true, NULL);
    nt_pointer_t rel = make_pointer(IN_X + PAD_X + 1.0F, IN_Y + (IN_H * 0.5F), false, false, true);
    (void)field_frame(&rel, id, buf, cap, true, NULL);
}

/* Press a physical key exactly one frame, with the field focused + idle pointer. */
static bool key_frame(uint32_t id, char *buf, size_t cap, nt_key_t key) {
    nt_input_poll();             /* clear last frame's edge flags */
    nt_input_set_key(key, true); /* fresh press edge this frame */
    const bool changed = field_frame(&IDLE_PTR, id, buf, cap, true, NULL);
    nt_input_set_key(key, false); /* release so the next set_key is a fresh edge */
    return changed;
}

/* One frame with two stacked fields (A on top, B below). Both buffers are char[32]. Feeds the pointer. */
static void two_field_frame(const nt_pointer_t *p, uint32_t id_a, char *a, uint32_t id_b, char *b) {
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.016F, p, 1);
    CLAY({.id = CLAY_ID("root2"), .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .offset = {.x = IN_X, .y = IN_Y}}}) {
        CLAY({.id = CLAY_ID("col2"), .layout = {.layoutDirection = CLAY_TOP_TO_BOTTOM}}) {
            (void)nt_ui_input_text(s_fx.ctx, NULL, 0, id_a, a, 32U, &s_props, &s_style, &s_field_decl, true, NULL);
            (void)nt_ui_input_text(s_fx.ctx, NULL, 0, id_b, b, 32U, &s_props, &s_style, &s_field_decl, true, NULL);
        }
    }
    nt_ui_end(s_fx.ctx);
}

/* One frame with three stacked fields (A, B, C). B's enabled state is a parameter so Tab-through-disabled
 * can be exercised. All buffers are char[32]. */
static void three_field_frame(const nt_pointer_t *p, uint32_t ia, char *a, uint32_t ib, char *b, bool b_enabled, uint32_t ic, char *c) {
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.016F, p, 1);
    CLAY({.id = CLAY_ID("root3"), .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .offset = {.x = IN_X, .y = IN_Y}}}) {
        CLAY({.id = CLAY_ID("col3"), .layout = {.layoutDirection = CLAY_TOP_TO_BOTTOM}}) {
            (void)nt_ui_input_text(s_fx.ctx, NULL, 0, ia, a, 32U, &s_props, &s_style, &s_field_decl, true, NULL);
            (void)nt_ui_input_text(s_fx.ctx, NULL, 0, ib, b, 32U, &s_props, &s_style, &s_field_decl, b_enabled, NULL);
            (void)nt_ui_input_text(s_fx.ctx, NULL, 0, ic, c, 32U, &s_props, &s_style, &s_field_decl, true, NULL);
        }
    }
    nt_ui_end(s_fx.ctx);
}

/* One frame with a single GROW-width field inside a fixed-width parent: the field has a real rendered
 * width but its decl carries NO width, exercising the responsive-width (bbox-fallback) scroll path. */
static void grow_field_frame(const nt_pointer_t *p, uint32_t id, char *buf, size_t cap) {
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.016F, p, 1);
    CLAY({.id = CLAY_ID("rootG"), .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .offset = {.x = IN_X, .y = IN_Y}}, .layout = {.sizing = {CLAY_SIZING_FIXED(IN_W), CLAY_SIZING_FIXED(IN_H)}}}) {
        const Clay_ElementDeclaration grow_decl = {.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(IN_H)}}};
        (void)nt_ui_input_text(s_fx.ctx, NULL, 0, id, buf, cap, &s_props, &s_style, &grow_decl, true, NULL);
    }
    nt_ui_end(s_fx.ctx);
}

/* ---- Test 1: insert two Cyrillic codepoints char-by-char; 4 bytes + NUL, caret at 4. ---- */
static void test_insert_cyrillic_codepoints(void) {
    char buf[32] = {0};
    const uint32_t id = nt_ui_id("f");
    warmup_focus(id, buf, sizeof buf);

    nt_input_buffer_char(CP_A);
    (void)field_frame(&IDLE_PTR, id, buf, sizeof buf, true, NULL);
    TEST_ASSERT_EQUAL_UINT(2U, (unsigned)strlen(buf)); /* 'А' = 2 bytes */

    nt_input_buffer_char(CP_B);
    (void)field_frame(&IDLE_PTR, id, buf, sizeof buf, true, NULL);
    TEST_ASSERT_EQUAL_UINT(4U, (unsigned)strlen(buf)); /* + 'б' = 4 bytes */

    /* Exact bytes: D0 90 D0 B1. */
    TEST_ASSERT_EQUAL_UINT8(0xD0U, (uint8_t)buf[0]);
    TEST_ASSERT_EQUAL_UINT8(0x90U, (uint8_t)buf[1]);
    TEST_ASSERT_EQUAL_UINT8(0xD0U, (uint8_t)buf[2]);
    TEST_ASSERT_EQUAL_UINT8(0xB1U, (uint8_t)buf[3]);
    TEST_ASSERT_EQUAL_UINT8(0x00U, (uint8_t)buf[4]);
}

/* ---- Test 2: Backspace deletes ONE codepoint (2 bytes), not one byte; buffer stays valid. ---- */
static void test_backspace_one_codepoint(void) {
    char buf[32];
    strcpy(buf, "\xD0\x90\xD0\xB1"); /* "Аб" */
    const uint32_t id = nt_ui_id("f");
    warmup_focus(id, buf, sizeof buf);
    /* Caret is at the click position (start); jump to end first. */
    (void)key_frame(id, buf, sizeof buf, NT_KEY_END);

    (void)key_frame(id, buf, sizeof buf, NT_KEY_BACKSPACE);
    TEST_ASSERT_EQUAL_UINT(2U, (unsigned)strlen(buf)); /* removed 'б' (2 bytes) */
    TEST_ASSERT_EQUAL_UINT8(0xD0U, (uint8_t)buf[0]);
    TEST_ASSERT_EQUAL_UINT8(0x90U, (uint8_t)buf[1]);
    TEST_ASSERT_EQUAL_UINT8(0x00U, (uint8_t)buf[2]);

    (void)key_frame(id, buf, sizeof buf, NT_KEY_BACKSPACE);
    TEST_ASSERT_EQUAL_UINT(0U, (unsigned)strlen(buf)); /* removed 'А' */
}

/* ---- Test 3: ArrowLeft moves back one codepoint (2 bytes for Cyrillic); insert lands mid-buffer. ---- */
static void test_arrow_left_codepoint(void) {
    char buf[32];
    strcpy(buf, "\xD0\x90\xD0\xB1"); /* "Аб" */
    const uint32_t id = nt_ui_id("f");
    warmup_focus(id, buf, sizeof buf);
    (void)key_frame(id, buf, sizeof buf, NT_KEY_END);        /* caret = 4 */
    (void)key_frame(id, buf, sizeof buf, NT_KEY_ARROW_LEFT); /* caret = 2 (one codepoint) */

    /* Insert 'X' at caret 2 -> between 'А' and 'б'. */
    nt_input_buffer_char((uint32_t)'X');
    (void)field_frame(&IDLE_PTR, id, buf, sizeof buf, true, NULL);
    TEST_ASSERT_EQUAL_UINT(5U, (unsigned)strlen(buf));
    TEST_ASSERT_EQUAL_UINT8(0xD0U, (uint8_t)buf[0]); /* А */
    TEST_ASSERT_EQUAL_UINT8(0x90U, (uint8_t)buf[1]);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)'X', (uint8_t)buf[2]);
    TEST_ASSERT_EQUAL_UINT8(0xD0U, (uint8_t)buf[3]); /* б */
    TEST_ASSERT_EQUAL_UINT8(0xB1U, (uint8_t)buf[4]);
}

/* ---- Test 4: a multi-byte codepoint that does not fit is dropped WHOLE; NUL preserved, no OOB. ---- */
static void test_clamp_no_split_no_oob(void) {
    /* cap 5 -> room for 4 bytes + NUL. "Аб" fills it exactly; a 3rd Cyrillic must be dropped. */
    char buf[5] = {0};
    char guard = (char)0x7E; /* canary byte just past the buffer */
    (void)guard;
    const uint32_t id = nt_ui_id("f");
    warmup_focus(id, buf, sizeof buf);

    nt_input_buffer_char(CP_A);
    nt_input_buffer_char(CP_B);
    (void)field_frame(&IDLE_PTR, id, buf, sizeof buf, true, NULL);
    TEST_ASSERT_EQUAL_UINT(4U, (unsigned)strlen(buf));
    TEST_ASSERT_EQUAL_UINT8(0x00U, (uint8_t)buf[4]); /* NUL intact at the last slot */

    /* A third 2-byte codepoint would need bytes 4..5 -> overflow. Must be dropped whole. */
    nt_input_buffer_char(CP_A);
    (void)field_frame(&IDLE_PTR, id, buf, sizeof buf, true, NULL);
    TEST_ASSERT_EQUAL_UINT(4U, (unsigned)strlen(buf)); /* unchanged */
    TEST_ASSERT_EQUAL_UINT8(0x00U, (uint8_t)buf[4]);   /* still NUL, no split byte written */
}

/* ---- Test 5: numeric predicate drops 'A', keeps '5'. ---- */
static void test_numeric_predicate(void) {
    char buf[32] = {0};
    const uint32_t id = nt_ui_id("f");
    s_props.allow = nt_ui_filter_numeric;
    warmup_focus(id, buf, sizeof buf);

    nt_input_buffer_char((uint32_t)'A'); /* rejected */
    nt_input_buffer_char((uint32_t)'5'); /* accepted */
    (void)field_frame(&IDLE_PTR, id, buf, sizeof buf, true, NULL);
    TEST_ASSERT_EQUAL_STRING("5", buf);
}

/* ---- Test 6: click hit-test maps x to the nearest codepoint boundary. ---- */
static void test_click_hit_test_boundary(void) {
    char buf[32];
    strcpy(buf, "abcd"); /* 4 single-byte glyphs, 10px advance each */
    const uint32_t id = nt_ui_id("f");
    /* Two warm frames so the bbox is cached. */
    (void)field_frame(&IDLE_PTR, id, buf, sizeof buf, true, NULL);
    (void)field_frame(&IDLE_PTR, id, buf, sizeof buf, true, NULL);

    /* Click at ~2.4 glyphs from the text origin -> nearest boundary is after glyph 2 (caret=2). */
    const float click_x = IN_X + PAD_X + (GLYPH_W * 2.4F);
    nt_pointer_t press = make_pointer(click_x, IN_Y + (IN_H * 0.5F), true, true, false);
    (void)field_frame(&press, id, buf, sizeof buf, true, NULL);
    nt_pointer_t rel = make_pointer(click_x, IN_Y + (IN_H * 0.5F), false, false, true);
    (void)field_frame(&rel, id, buf, sizeof buf, true, NULL);

    /* Insert 'Z' at the caret; if hit-test landed at boundary 2 it splices "abZcd". */
    nt_input_buffer_char((uint32_t)'Z');
    (void)field_frame(&IDLE_PTR, id, buf, sizeof buf, true, NULL);
    TEST_ASSERT_EQUAL_STRING("abZcd", buf);
}

/* ---- Test 7: Tab advances focus to the next field; Esc unfocuses; a non-focused field ignores chars. ---- */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_focus_tab_esc(void) {
    char a[32] = {0};
    char b[32] = {0};
    const uint32_t id_a = nt_ui_id("fa");
    const uint32_t id_b = nt_ui_id("fb");

    /* One frame to register both fields + warm their bboxes. */
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.016F, &IDLE_PTR, 1);
    CLAY({.id = CLAY_ID("root"), .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .offset = {.x = IN_X, .y = IN_Y}}}) {
        CLAY({.id = CLAY_ID("col"), .layout = {.layoutDirection = CLAY_TOP_TO_BOTTOM}}) {
            (void)nt_ui_input_text(s_fx.ctx, NULL, 0, id_a, a, sizeof a, &s_props, &s_style, &s_field_decl, true, NULL);
            (void)nt_ui_input_text(s_fx.ctx, NULL, 0, id_b, b, sizeof b, &s_props, &s_style, &s_field_decl, true, NULL);
        }
    }
    nt_ui_end(s_fx.ctx);

    /* Press inside field A to focus it. */
    nt_pointer_t press = make_pointer(IN_X + PAD_X + 1.0F, IN_Y + (IN_H * 0.5F), true, true, false);
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.016F, &press, 1);
    CLAY({.id = CLAY_ID("root"), .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .offset = {.x = IN_X, .y = IN_Y}}}) {
        CLAY({.id = CLAY_ID("col"), .layout = {.layoutDirection = CLAY_TOP_TO_BOTTOM}}) {
            (void)nt_ui_input_text(s_fx.ctx, NULL, 0, id_a, a, sizeof a, &s_props, &s_style, &s_field_decl, true, NULL);
            (void)nt_ui_input_text(s_fx.ctx, NULL, 0, id_b, b, sizeof b, &s_props, &s_style, &s_field_decl, true, NULL);
        }
    }
    nt_ui_end(s_fx.ctx);
    TEST_ASSERT_TRUE(nt_ui_input_focused(s_fx.ctx, id_a));
    TEST_ASSERT_FALSE(nt_ui_input_focused(s_fx.ctx, id_b));

    /* Tab: focus moves to field B. */
    nt_input_poll();
    nt_input_set_key(NT_KEY_TAB, true);
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.016F, &IDLE_PTR, 1);
    CLAY({.id = CLAY_ID("root"), .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .offset = {.x = IN_X, .y = IN_Y}}}) {
        CLAY({.id = CLAY_ID("col"), .layout = {.layoutDirection = CLAY_TOP_TO_BOTTOM}}) {
            (void)nt_ui_input_text(s_fx.ctx, NULL, 0, id_a, a, sizeof a, &s_props, &s_style, &s_field_decl, true, NULL);
            (void)nt_ui_input_text(s_fx.ctx, NULL, 0, id_b, b, sizeof b, &s_props, &s_style, &s_field_decl, true, NULL);
        }
    }
    nt_ui_end(s_fx.ctx);
    nt_input_set_key(NT_KEY_TAB, false);
    TEST_ASSERT_FALSE(nt_ui_input_focused(s_fx.ctx, id_a));
    TEST_ASSERT_TRUE(nt_ui_input_focused(s_fx.ctx, id_b));

    /* A typed char now only edits B (the focused field); A stays empty. */
    nt_input_buffer_char((uint32_t)'q');
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.016F, &IDLE_PTR, 1);
    CLAY({.id = CLAY_ID("root"), .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .offset = {.x = IN_X, .y = IN_Y}}}) {
        CLAY({.id = CLAY_ID("col"), .layout = {.layoutDirection = CLAY_TOP_TO_BOTTOM}}) {
            (void)nt_ui_input_text(s_fx.ctx, NULL, 0, id_a, a, sizeof a, &s_props, &s_style, &s_field_decl, true, NULL);
            (void)nt_ui_input_text(s_fx.ctx, NULL, 0, id_b, b, sizeof b, &s_props, &s_style, &s_field_decl, true, NULL);
        }
    }
    nt_ui_end(s_fx.ctx);
    TEST_ASSERT_EQUAL_STRING("", a);
    TEST_ASSERT_EQUAL_STRING("q", b);

    /* Esc: unfocus B. */
    nt_input_poll();
    nt_input_set_key(NT_KEY_ESCAPE, true);
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.016F, &IDLE_PTR, 1);
    CLAY({.id = CLAY_ID("root"), .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .offset = {.x = IN_X, .y = IN_Y}}}) {
        CLAY({.id = CLAY_ID("col"), .layout = {.layoutDirection = CLAY_TOP_TO_BOTTOM}}) {
            (void)nt_ui_input_text(s_fx.ctx, NULL, 0, id_a, a, sizeof a, &s_props, &s_style, &s_field_decl, true, NULL);
            (void)nt_ui_input_text(s_fx.ctx, NULL, 0, id_b, b, sizeof b, &s_props, &s_style, &s_field_decl, true, NULL);
        }
    }
    nt_ui_end(s_fx.ctx);
    nt_input_set_key(NT_KEY_ESCAPE, false);
    TEST_ASSERT_FALSE(nt_ui_input_focused(s_fx.ctx, id_a));
    TEST_ASSERT_FALSE(nt_ui_input_focused(s_fx.ctx, id_b));
}

/* ---- Test 7b: focus is dropped when the focused field is not re-declared next frame. ---- */
static void test_focus_orphan_cleared(void) {
    char buf[32] = {0};
    const uint32_t id = nt_ui_id("f");
    warmup_focus(id, buf, sizeof buf);
    TEST_ASSERT_TRUE(nt_ui_input_any_focused(s_fx.ctx)); /* field holds focus */

    /* Frame 1 without the field: begin still sees last frame's seen=1, so focus survives this begin
     * but seen stays 0 (the field never runs). */
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.016F, &IDLE_PTR, 1);
    CLAY({.id = CLAY_ID("root")}) {}
    nt_ui_end(s_fx.ctx);

    /* Frame 2's begin runs the orphan sweep on seen==0 -> focus is cleared so a global Esc=quit is
     * no longer gated forever. */
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.016F, &IDLE_PTR, 1);
    CLAY({.id = CLAY_ID("root")}) {}
    nt_ui_end(s_fx.ctx);
    TEST_ASSERT_FALSE(nt_ui_input_any_focused(s_fx.ctx));
}

/* ---- Test 7c: focus is dropped the frame a focused field is declared disabled. ---- */
static void test_focus_dropped_when_disabled(void) {
    char buf[32] = {0};
    const uint32_t id = nt_ui_id("f");
    warmup_focus(id, buf, sizeof buf);
    TEST_ASSERT_TRUE(nt_ui_input_any_focused(s_fx.ctx)); /* field holds focus */

    /* Re-declare the same field disabled: it can't be clicked to refocus and gates out Esc, so
     * holding focus would soft-lock any_focused forever. The disabled declaration must drop it. */
    (void)field_frame(&IDLE_PTR, id, buf, sizeof buf, false, NULL);
    TEST_ASSERT_FALSE(nt_ui_input_any_focused(s_fx.ctx));
}

/* ---- Test 8: Enter raises submit; on_change is true the frame the buffer mutates. ---- */
static void test_submit_and_change(void) {
    char buf[32] = {0};
    const uint32_t id = nt_ui_id("f");
    warmup_focus(id, buf, sizeof buf);

    nt_input_buffer_char((uint32_t)'x');
    const bool changed = field_frame(&IDLE_PTR, id, buf, sizeof buf, true, NULL);
    TEST_ASSERT_TRUE(changed); /* on_change fired the frame 'x' spliced in */

    /* A frame with no edit returns false. */
    const bool no_change = field_frame(&IDLE_PTR, id, buf, sizeof buf, true, NULL);
    TEST_ASSERT_FALSE(no_change);

    /* Enter raises submit. */
    nt_input_poll();
    nt_input_set_key(NT_KEY_ENTER, true);
    bool submitted = false;
    (void)field_frame(&IDLE_PTR, id, buf, sizeof buf, true, &submitted);
    nt_input_set_key(NT_KEY_ENTER, false);
    TEST_ASSERT_TRUE(submitted);
}

/* ---- Test 9: a non-focused field never consumes typed chars. ---- */
static void test_unfocused_ignores_chars(void) {
    char buf[32] = {0};
    const uint32_t id = nt_ui_id("f");
    /* Never focus it (no press). */
    (void)field_frame(&IDLE_PTR, id, buf, sizeof buf, true, NULL);
    nt_input_buffer_char((uint32_t)'z');
    (void)field_frame(&IDLE_PTR, id, buf, sizeof buf, true, NULL);
    TEST_ASSERT_EQUAL_STRING("", buf);
}

/* ---- Test 10: the generic dblclick/longpress primitive reports edges. ---- */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_dblclick_longpress_primitive(void) {
    const uint32_t gid = nt_ui_id("gesture");
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &IDLE_PTR, 1);

    /* First press: no double-click yet. */
    nt_ui_click_gesture_t g = nt_ui_dblclick_longpress(s_fx.ctx, gid, true, false, true, 10.0F, 10.0F, 0.30F, 0.50F, 6.0F);
    TEST_ASSERT_FALSE(g.double_clicked);
    /* Release (result discarded; just clears the live-press state). */
    (void)nt_ui_dblclick_longpress(s_fx.ctx, gid, false, true, false, 10.0F, 10.0F, 0.30F, 0.50F, 6.0F);
    /* Second press within the window + radius -> double-click. */
    g = nt_ui_dblclick_longpress(s_fx.ctx, gid, true, false, true, 11.0F, 11.0F, 0.30F, 0.50F, 6.0F);
    TEST_ASSERT_TRUE(g.double_clicked);
    nt_ui_end(s_fx.ctx);

    /* Long-press: a held press whose accumulated dt crosses the threshold fires once. */
    const uint32_t lid = nt_ui_id("longpress");
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &IDLE_PTR, 1);
    g = nt_ui_dblclick_longpress(s_fx.ctx, lid, true, false, true, 50.0F, 50.0F, 0.30F, 0.50F, 6.0F);
    TEST_ASSERT_FALSE(g.long_pressed);
    nt_ui_end(s_fx.ctx);

    bool fired = false;
    for (int i = 0; i < 60; ++i) { /* advance the clock via 0.1s dt steps */
        nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.1F, &IDLE_PTR, 1);
        g = nt_ui_dblclick_longpress(s_fx.ctx, lid, false, false, true, 50.0F, 50.0F, 0.30F, 0.50F, 6.0F);
        nt_ui_end(s_fx.ctx);
        if (g.long_pressed) {
            fired = true;
            break;
        }
    }
    TEST_ASSERT_TRUE(fired);
}

/* Hold/release a modifier across a key-frame: many selection ops need Shift or Ctrl DOWN while
 * the action key fires its press edge. Sets both keys, runs one field frame, then clears. */
static void chord_frame(uint32_t id, char *buf, size_t cap, nt_key_t mod, nt_key_t key) {
    nt_input_poll();
    nt_input_set_key(mod, true);
    nt_input_set_key(key, true);
    (void)field_frame(&IDLE_PTR, id, buf, cap, true, NULL);
    nt_input_set_key(key, false);
    nt_input_set_key(mod, false);
}

/* ---- Test 12: Shift+arrows extend a selection; a plain arrow collapses it. ---- */
static void test_shift_select_and_collapse(void) {
    char buf[32];
    strcpy(buf, "abcd");
    const uint32_t id = nt_ui_id("f");
    warmup_focus(id, buf, sizeof buf);
    (void)key_frame(id, buf, sizeof buf, NT_KEY_HOME); /* caret = 0, selection empty */

    /* Shift+Right twice selects "ab"; typing replaces the selection. */
    chord_frame(id, buf, sizeof buf, NT_KEY_LSHIFT, NT_KEY_ARROW_RIGHT);
    chord_frame(id, buf, sizeof buf, NT_KEY_LSHIFT, NT_KEY_ARROW_RIGHT);
    nt_input_buffer_char((uint32_t)'Z');
    (void)field_frame(&IDLE_PTR, id, buf, sizeof buf, true, NULL);
    TEST_ASSERT_EQUAL_STRING("Zcd", buf); /* "ab" replaced by 'Z' */
}

/* ---- Test 13: Shift+Right over Cyrillic selects whole codepoints; replace stays UTF-8-valid. ---- */
static void test_shift_select_cyrillic(void) {
    char buf[32];
    strcpy(buf, "\xD0\x90\xD0\xB1"); /* "Аб" */
    const uint32_t id = nt_ui_id("f");
    warmup_focus(id, buf, sizeof buf);
    (void)key_frame(id, buf, sizeof buf, NT_KEY_HOME);

    /* Shift+Right selects the first codepoint 'А' (2 bytes); Backspace deletes the selection. */
    chord_frame(id, buf, sizeof buf, NT_KEY_LSHIFT, NT_KEY_ARROW_RIGHT);
    (void)key_frame(id, buf, sizeof buf, NT_KEY_BACKSPACE);
    TEST_ASSERT_EQUAL_UINT(2U, (unsigned)strlen(buf)); /* 'б' remains, 2 bytes */
    TEST_ASSERT_EQUAL_UINT8(0xD0U, (uint8_t)buf[0]);
    TEST_ASSERT_EQUAL_UINT8(0xB1U, (uint8_t)buf[1]);
    TEST_ASSERT_EQUAL_UINT8(0x00U, (uint8_t)buf[2]);
}

/* ---- Test 14: Ctrl+A selects the whole buffer; typing replaces everything. ---- */
static void test_ctrl_a_select_all(void) {
    char buf[32];
    strcpy(buf, "hello");
    const uint32_t id = nt_ui_id("f");
    warmup_focus(id, buf, sizeof buf);

    chord_frame(id, buf, sizeof buf, NT_KEY_LCTRL, NT_KEY_A);
    nt_input_buffer_char((uint32_t)'!');
    (void)field_frame(&IDLE_PTR, id, buf, sizeof buf, true, NULL);
    TEST_ASSERT_EQUAL_STRING("!", buf);
}

/* ---- Test 15: double-click selects the word under the caret (codepoint-class scan). ---- */
static void test_double_click_word_select(void) {
    char buf[32];
    strcpy(buf, "foo bar"); /* two words */
    const uint32_t id = nt_ui_id("f");
    (void)field_frame(&IDLE_PTR, id, buf, sizeof buf, true, NULL);
    (void)field_frame(&IDLE_PTR, id, buf, sizeof buf, true, NULL); /* warm the bbox */

    /* Two presses inside "foo" within the dbl window => double-click selects "foo". */
    const float click_x = IN_X + PAD_X + (GLYPH_W * 1.0F); /* over the 'o' of foo */
    nt_pointer_t p1 = make_pointer(click_x, IN_Y + (IN_H * 0.5F), true, true, false);
    (void)field_frame(&p1, id, buf, sizeof buf, true, NULL);
    nt_pointer_t r1 = make_pointer(click_x, IN_Y + (IN_H * 0.5F), false, false, true);
    (void)field_frame(&r1, id, buf, sizeof buf, true, NULL);
    nt_pointer_t p2 = make_pointer(click_x, IN_Y + (IN_H * 0.5F), true, true, false);
    (void)field_frame(&p2, id, buf, sizeof buf, true, NULL);

    /* Backspace now deletes the selected word "foo" (the leading space stays). */
    (void)key_frame(id, buf, sizeof buf, NT_KEY_BACKSPACE);
    TEST_ASSERT_EQUAL_STRING(" bar", buf);
}

/* ---- Test 16: Ctrl+C copies the selection; Ctrl+V pastes it (round-trip via the fake clipboard). ---- */
static void test_clipboard_copy_paste_roundtrip(void) {
    char buf[32];
    strcpy(buf, "abcd");
    const uint32_t id = nt_ui_id("f");
    warmup_focus(id, buf, sizeof buf);

    /* Select "ab" (Home, Shift+Right x2), copy it. */
    (void)key_frame(id, buf, sizeof buf, NT_KEY_HOME);
    chord_frame(id, buf, sizeof buf, NT_KEY_LSHIFT, NT_KEY_ARROW_RIGHT);
    chord_frame(id, buf, sizeof buf, NT_KEY_LSHIFT, NT_KEY_ARROW_RIGHT);
    chord_frame(id, buf, sizeof buf, NT_KEY_LCTRL, NT_KEY_C);
    TEST_ASSERT_EQUAL_STRING("ab", nt_clipboard_get_text());

    /* Caret to end, paste -> "abcdab". */
    (void)key_frame(id, buf, sizeof buf, NT_KEY_END);
    chord_frame(id, buf, sizeof buf, NT_KEY_LCTRL, NT_KEY_V);
    TEST_ASSERT_EQUAL_STRING("abcdab", buf);
}

/* ---- Test 17: Ctrl+X copies then deletes the selection. ---- */
static void test_clipboard_cut(void) {
    char buf[32];
    strcpy(buf, "hello");
    const uint32_t id = nt_ui_id("f");
    warmup_focus(id, buf, sizeof buf);

    /* Ctrl+A select-all then Ctrl+X: clipboard holds "hello", buffer empties. */
    chord_frame(id, buf, sizeof buf, NT_KEY_LCTRL, NT_KEY_A);
    chord_frame(id, buf, sizeof buf, NT_KEY_LCTRL, NT_KEY_X);
    TEST_ASSERT_EQUAL_STRING("hello", nt_clipboard_get_text());
    TEST_ASSERT_EQUAL_STRING("", buf);
}

/* ---- Test 17b: Ctrl+X is a full no-op when the clipboard is unavailable (no data loss). ---- */
static void test_clipboard_cut_unavailable_noop(void) {
    char buf[32];
    strcpy(buf, "hello");
    char snapshot[32];
    memcpy(snapshot, buf, sizeof buf);
    const uint32_t id = nt_ui_id("f");
    warmup_focus(id, buf, sizeof buf);

    nt_clipboard_fake_set_available(false);
    /* Select-all then Ctrl+X: with no clipboard, nothing is deleted -- the buffer is byte-identical. */
    chord_frame(id, buf, sizeof buf, NT_KEY_LCTRL, NT_KEY_A);
    chord_frame(id, buf, sizeof buf, NT_KEY_LCTRL, NT_KEY_X);
    TEST_ASSERT_EQUAL_MEMORY(snapshot, buf, sizeof buf); /* buffer untouched */

    /* The selection also survived: typing a char now replaces the whole still-active selection. */
    nt_input_buffer_char((uint32_t)'!');
    (void)field_frame(&IDLE_PTR, id, buf, sizeof buf, true, NULL);
    TEST_ASSERT_EQUAL_STRING("!", buf);

    /* tearDown restores availability unconditionally (covers a mid-test assert-fail too). */
}

/* ---- Test 18: paste longer than remaining capacity is clamped (no OOB, NUL intact, no split). ---- */
static void test_clipboard_paste_clamp(void) {
    /* cap 5 -> room for 4 bytes + NUL. Paste "ABCDEFGH"; only "ABCD" fits. */
    char buf[5] = {0};
    const uint32_t id = nt_ui_id("f");
    warmup_focus(id, buf, sizeof buf);
    nt_clipboard_set_text("ABCDEFGH");
    chord_frame(id, buf, sizeof buf, NT_KEY_LCTRL, NT_KEY_V);
    TEST_ASSERT_EQUAL_UINT(4U, (unsigned)strlen(buf));
    TEST_ASSERT_EQUAL_STRING("ABCD", buf);
    TEST_ASSERT_EQUAL_UINT8(0x00U, (uint8_t)buf[4]); /* NUL intact at the last slot */
}

/* ---- Test 19: paste clamp never splits a multi-byte codepoint. ---- */
static void test_clipboard_paste_clamp_no_split(void) {
    /* cap 5 -> 4 bytes. Paste "Абв" (3 Cyrillic = 6 bytes); "Аб" (4 bytes) fits, 'в' dropped whole. */
    char buf[5] = {0};
    const uint32_t id = nt_ui_id("f");
    warmup_focus(id, buf, sizeof buf);
    nt_clipboard_set_text("\xD0\x90\xD0\xB1\xD0\xB2"); /* Абв */
    chord_frame(id, buf, sizeof buf, NT_KEY_LCTRL, NT_KEY_V);
    TEST_ASSERT_EQUAL_UINT(4U, (unsigned)strlen(buf)); /* "Аб" only */
    TEST_ASSERT_EQUAL_UINT8(0xD0U, (uint8_t)buf[0]);
    TEST_ASSERT_EQUAL_UINT8(0x90U, (uint8_t)buf[1]);
    TEST_ASSERT_EQUAL_UINT8(0xD0U, (uint8_t)buf[2]);
    TEST_ASSERT_EQUAL_UINT8(0xB1U, (uint8_t)buf[3]);
    TEST_ASSERT_EQUAL_UINT8(0x00U, (uint8_t)buf[4]); /* no split byte written */
}

/* ---- Test 20: a numeric-filtered field drops non-numeric pasted chars. ---- */
static void test_clipboard_paste_filtered(void) {
    char buf[32] = {0};
    const uint32_t id = nt_ui_id("f");
    s_props.allow = nt_ui_filter_numeric;
    warmup_focus(id, buf, sizeof buf);
    nt_clipboard_set_text("a1b2c3"); /* letters dropped, digits kept */
    chord_frame(id, buf, sizeof buf, NT_KEY_LCTRL, NT_KEY_V);
    TEST_ASSERT_EQUAL_STRING("123", buf);
}

/* ---- Test 21: paste replaces a non-empty selection. ---- */
static void test_clipboard_paste_replaces_selection(void) {
    char buf[32];
    strcpy(buf, "abcd");
    const uint32_t id = nt_ui_id("f");
    warmup_focus(id, buf, sizeof buf);
    nt_clipboard_set_text("XY");
    /* Select "ab" then paste -> "XYcd". */
    (void)key_frame(id, buf, sizeof buf, NT_KEY_HOME);
    chord_frame(id, buf, sizeof buf, NT_KEY_LSHIFT, NT_KEY_ARROW_RIGHT);
    chord_frame(id, buf, sizeof buf, NT_KEY_LSHIFT, NT_KEY_ARROW_RIGHT);
    chord_frame(id, buf, sizeof buf, NT_KEY_LCTRL, NT_KEY_V);
    TEST_ASSERT_EQUAL_STRING("XYcd", buf);
}

/* ---- Test 22: password mask renders one mask glyph per codepoint (non-visual render-state probe). ---- */
static void test_password_mask_render(void) {
    /* "Аб" = 2 codepoints / 4 bytes -> the mask string is exactly 2 glyphs, not 4. */
    const char *src = "\xD0\x90\xD0\xB1";
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &IDLE_PTR, 1); /* scratch frame for the mask alloc */
    const char *masked = nt_ui_input_build_display_text(src, (uint32_t)strlen(src));
    TEST_ASSERT_EQUAL_UINT(2U, (unsigned)strlen(masked)); /* 2 codepoints -> 2 mask chars */
    TEST_ASSERT_EQUAL_UINT8((uint8_t)'*', (uint8_t)masked[0]);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)'*', (uint8_t)masked[1]);
    TEST_ASSERT_EQUAL_UINT8(0x00U, (uint8_t)masked[2]);
    nt_ui_end(s_fx.ctx);
}

/* ---- Test 23: a password field edits the real buffer (mask is render-only) + keyboard enum plumbs. ---- */
static void test_password_buffer_and_keyboard(void) {
    char buf[32] = {0};
    const uint32_t id = nt_ui_id("f");
    s_props.password = true;
    s_props.keyboard = NT_UI_KB_PASSWORD;
    warmup_focus(id, buf, sizeof buf);

    /* Typing fills the REAL buffer (the mask only affects rendering). */
    nt_input_buffer_char((uint32_t)'s');
    nt_input_buffer_char((uint32_t)'e');
    nt_input_buffer_char((uint32_t)'c');
    (void)field_frame(&IDLE_PTR, id, buf, sizeof buf, true, NULL);
    TEST_ASSERT_EQUAL_STRING("sec", buf); /* native: keyboard enum stored, edit behavior unchanged */
}

/* ---- Test 24: Delete key removes the codepoint to the RIGHT of the caret; caret stays put. ---- */
static void test_delete_key_right(void) {
    char buf[32];
    strcpy(buf, "\xD0\x90\xD0\xB1"); /* "Аб" */
    const uint32_t id = nt_ui_id("f");
    warmup_focus(id, buf, sizeof buf);
    (void)key_frame(id, buf, sizeof buf, NT_KEY_HOME); /* caret = 0 */

    (void)key_frame(id, buf, sizeof buf, NT_KEY_DELETE);
    TEST_ASSERT_EQUAL_UINT(2U, (unsigned)strlen(buf)); /* removed 'А' (2 bytes), caret still 0 */
    TEST_ASSERT_EQUAL_UINT8(0xD0U, (uint8_t)buf[0]);   /* 'б' remains */
    TEST_ASSERT_EQUAL_UINT8(0xB1U, (uint8_t)buf[1]);
    TEST_ASSERT_EQUAL_UINT8(0x00U, (uint8_t)buf[2]);

    /* A second Delete clears the last codepoint. */
    (void)key_frame(id, buf, sizeof buf, NT_KEY_DELETE);
    TEST_ASSERT_EQUAL_UINT(0U, (unsigned)strlen(buf));
}

/* ---- Test 25: a malformed/truncated UTF-8 paste yields a valid buffer; only the valid 'A' lands. ---- */
static void test_clipboard_paste_malformed_utf8(void) {
    char buf[32] = {0};
    const uint32_t id = nt_ui_id("f");
    warmup_focus(id, buf, sizeof buf);
    /* lone 0xFF (reject), 0xC0 (overlong lead, no continuation), 0xE0 (3-byte lead, truncated),
     * then a valid 'A'. The decoder must resync and splice only 'A' -- never read OOB. */
    nt_clipboard_set_text("\xFF\xC0\xE0\x41");
    chord_frame(id, buf, sizeof buf, NT_KEY_LCTRL, NT_KEY_V);
    TEST_ASSERT_EQUAL_STRING("A", buf); /* only the valid codepoint survived */

    /* The buffer is valid UTF-8 end-to-end (a full decode accepts with no reject). */
    uint32_t state = NT_UTF8_ACCEPT;
    uint32_t cp = 0U;
    for (const char *p = buf; *p != '\0'; ++p) {
        const uint32_t s = nt_utf8_decode(&state, &cp, (uint8_t)*p);
        TEST_ASSERT_NOT_EQUAL_UINT(NT_UTF8_REJECT, s);
    }
    TEST_ASSERT_EQUAL_UINT(NT_UTF8_ACCEPT, state); /* ends on a codepoint boundary */
}

/* ---- Test 26: press -> move -> release selects the spanned range (drag-select). ---- */
static void test_drag_select_range(void) {
    char buf[32];
    strcpy(buf, "abcd"); /* 4 single-byte glyphs, 10px advance each */
    const uint32_t id = nt_ui_id("f");
    (void)field_frame(&IDLE_PTR, id, buf, sizeof buf, true, NULL);
    (void)field_frame(&IDLE_PTR, id, buf, sizeof buf, true, NULL); /* warm the bbox */

    /* Press at the start (caret/anchor = 0), drag to ~3 glyphs, release: selects "abc". */
    const float x0 = IN_X + PAD_X + (GLYPH_W * 0.1F);
    const float x3 = IN_X + PAD_X + (GLYPH_W * 3.0F);
    nt_pointer_t press = make_pointer(x0, IN_Y + (IN_H * 0.5F), true, true, false);
    (void)field_frame(&press, id, buf, sizeof buf, true, NULL);
    nt_pointer_t move = make_pointer(x3, IN_Y + (IN_H * 0.5F), true, false, false);
    (void)field_frame(&move, id, buf, sizeof buf, true, NULL);
    nt_pointer_t rel = make_pointer(x3, IN_Y + (IN_H * 0.5F), false, false, true);
    (void)field_frame(&rel, id, buf, sizeof buf, true, NULL);

    /* Backspace deletes the dragged selection "abc", leaving "d". */
    (void)key_frame(id, buf, sizeof buf, NT_KEY_BACKSPACE);
    TEST_ASSERT_EQUAL_STRING("d", buf);
}

/* ---- Test 27: a password field never mutates the game buffer when its masked display renders. ---- */
static void test_password_buffer_unchanged_after_render(void) {
    char buf[32];
    strcpy(buf, "\xD0\x90\xD0\xB1!"); /* "Аб!" -- mixed multi-byte + ASCII */
    char snapshot[32];
    memcpy(snapshot, buf, sizeof buf);
    const uint32_t id = nt_ui_id("f");
    s_props.password = true;
    warmup_focus(id, buf, sizeof buf);

    /* A plain render frame (no edit) must leave every byte of the game buffer intact. */
    (void)field_frame(&IDLE_PTR, id, buf, sizeof buf, true, NULL);
    TEST_ASSERT_EQUAL_MEMORY(snapshot, buf, sizeof buf);
}

/* The auto-repeat core is exposed under NT_TEST_ACCESS with explicit storage (no internal cell type). */
extern bool nt_ui_input_key_repeat_step(uint16_t *repeat_key, float *repeat_t, nt_key_t k, bool pressed, bool down, float dt);

/* ---- Test 29: key-repeat fires once on press, then after the initial delay repeats at the fast rate;
 * a release stops it. Driven directly with fake pressed/down edges so timing is deterministic. ---- */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_key_repeat_timing(void) {
    uint16_t rk = 0U;
    float rt = 0.0F;
    const float dt = 0.016F; /* ~60fps */

    /* Press edge: fires immediately (initial action) and arms the delay (~0.275s, ImGui default). */
    TEST_ASSERT_TRUE(nt_ui_input_key_repeat_step(&rk, &rt, NT_KEY_ARROW_LEFT, true, true, dt));

    /* Held (no new press) until the initial delay elapses: no repeat yet. */
    int frames_to_first_repeat = 0;
    bool fired = false;
    for (int i = 0; i < 200; ++i) {
        fired = nt_ui_input_key_repeat_step(&rk, &rt, NT_KEY_ARROW_LEFT, false, true, dt);
        ++frames_to_first_repeat;
        if (fired) {
            break;
        }
    }
    TEST_ASSERT_TRUE(fired);
    /* First repeat lands after the DELAY (~0.275s / 0.016 ~ 17 frames), not on frame 1. */
    TEST_ASSERT_TRUE(frames_to_first_repeat >= 14);

    /* Subsequent repeats land at the faster RATE (~0.05s / 0.016 ~ 3 frames) — far quicker than delay. */
    int frames_to_next_repeat = 0;
    fired = false;
    for (int i = 0; i < 200; ++i) {
        fired = nt_ui_input_key_repeat_step(&rk, &rt, NT_KEY_ARROW_LEFT, false, true, dt);
        ++frames_to_next_repeat;
        if (fired) {
            break;
        }
    }
    TEST_ASSERT_TRUE(fired);
    TEST_ASSERT_TRUE(frames_to_next_repeat < frames_to_first_repeat); /* rate < delay */

    /* Release (not down): no fire, and the armed key disarms. */
    TEST_ASSERT_FALSE(nt_ui_input_key_repeat_step(&rk, &rt, NT_KEY_ARROW_LEFT, false, false, dt));
    TEST_ASSERT_EQUAL_UINT16(0U, rk);

    /* A different repeatable key pressed while the first is held: last-pressed wins (re-arms to it). */
    (void)nt_ui_input_key_repeat_step(&rk, &rt, NT_KEY_ARROW_LEFT, true, true, dt);              /* arm LEFT */
    TEST_ASSERT_TRUE(nt_ui_input_key_repeat_step(&rk, &rt, NT_KEY_ARROW_RIGHT, true, true, dt)); /* RIGHT wins */
    TEST_ASSERT_EQUAL_UINT16((uint16_t)NT_KEY_ARROW_RIGHT, rk);
    /* LEFT still down but no longer armed: it does not repeat. */
    TEST_ASSERT_FALSE(nt_ui_input_key_repeat_step(&rk, &rt, NT_KEY_ARROW_LEFT, false, true, dt));
}

/* ---- Test 11: style defaults are a valid baseline (caret_width > 0, sensible blink); zero-init
 * props is a plain text field (default keyboard). ---- */
static void test_style_defaults_valid(void) {
    nt_ui_input_style_t s = nt_ui_input_style_defaults();
    TEST_ASSERT_TRUE(s.caret_width > 0.0F);
    TEST_ASSERT_TRUE(s.text.font_size > 0.0F);
    nt_ui_input_props_t p;
    memset(&p, 0, sizeof p);
    TEST_ASSERT_EQUAL_INT(NT_UI_KB_TEXT, p.keyboard);
}

/* ---- Test 28: placeholder shows only when empty AND unfocused; hidden when focused or non-empty. ---- */
static void test_placeholder_empty_unfocused_only(void) {
    /* Empty + unfocused -> the hint renders. */
    TEST_ASSERT_EQUAL_STRING("edit me", nt_ui_input_placeholder_for("", "edit me", false));
    /* Empty but focused -> hidden (the user clicked in). */
    TEST_ASSERT_NULL(nt_ui_input_placeholder_for("", "edit me", true));
    /* Non-empty -> hidden regardless of focus (the real text shows). */
    TEST_ASSERT_NULL(nt_ui_input_placeholder_for("x", "edit me", false));
    TEST_ASSERT_NULL(nt_ui_input_placeholder_for("x", "edit me", true));

    /* No placeholder string (NULL or "") -> nothing, even when empty + unfocused. */
    TEST_ASSERT_NULL(nt_ui_input_placeholder_for("", NULL, false));
    TEST_ASSERT_NULL(nt_ui_input_placeholder_for("", "", false));
}

/* ---- Test 29: bg_art set -> field draws the sprite (IMAGE cmd) with the payload pinned. ---- */
static void test_bg_art_emits_image(void) {
    char buf[8] = {0};
    s_style.bg_art = (nt_atlas_region_ref_t){.atlas = s_fx.atlas.handle, .region = s_fx.atlas.white_region_idx};
    (void)field_frame(&IDLE_PTR, nt_ui_id("bg"), buf, sizeof buf, true, NULL);

    const Clay_RenderCommand *img = find_first_image_cmd(s_fx.ctx);
    TEST_ASSERT_NOT_NULL_MESSAGE(img, "bg_art set must emit a background IMAGE command");
    const nt_ui_image_payload_t *p = (const nt_ui_image_payload_t *)img->renderData.image.imageData;
    TEST_ASSERT_EQUAL_UINT32(s_fx.atlas.handle.id, p->atlas.id);
    TEST_ASSERT_EQUAL_UINT32(s_fx.atlas.white_region_idx, p->region_index);
}

/* ---- Test 30: no bg_art -> flat bg color only, no IMAGE command. ---- */
static void test_no_bg_art_no_image(void) {
    char buf[8] = {0};
    /* init_style() leaves bg_art zero-init (atlas.id == 0 = none). */
    (void)field_frame(&IDLE_PTR, nt_ui_id("nobg"), buf, sizeof buf, true, NULL);
    TEST_ASSERT_NULL_MESSAGE(find_first_image_cmd(s_fx.ctx), "unset bg_art must not emit an IMAGE command");
}

/* ---- Test 31: focus swaps bg_art -> focused_bg_art (distinct region in the payload). ---- */
static void test_bg_art_focus_swap(void) {
    char buf[8] = {0};
    const uint32_t id = nt_ui_id("swap");
    s_style.bg_art = (nt_atlas_region_ref_t){.atlas = s_fx.atlas.handle, .region = s_fx.atlas.white_region_idx};
    s_style.focused_bg_art = (nt_atlas_region_ref_t){.atlas = s_fx.atlas.handle, .region = s_fx.atlas.polygon_region_idx};
    warmup_focus(id, buf, sizeof buf); /* leaves the field focused */
    TEST_ASSERT_TRUE(nt_ui_input_focused(s_fx.ctx, id));

    (void)field_frame(&IDLE_PTR, id, buf, sizeof buf, true, NULL);
    const Clay_RenderCommand *img = find_first_image_cmd(s_fx.ctx);
    TEST_ASSERT_NOT_NULL(img);
    const nt_ui_image_payload_t *p = (const nt_ui_image_payload_t *)img->renderData.image.imageData;
    TEST_ASSERT_EQUAL_UINT32(s_fx.atlas.polygon_region_idx, p->region_index); /* focused art, not idle */
}

/* ---- Test 32: a drag is abandoned when focus leaves the field mid-drag (no stale caret move). ---- */
static void test_drag_abandoned_on_focus_loss(void) {
    char buf[32];
    strcpy(buf, "hello world");
    const uint32_t id = nt_ui_id("f");
    /* Warm the bbox with idle frames (not warmup_focus -- a second press at the same spot would land
     * inside the double-click window and trip word-select, which clears the drag we want to arm). */
    (void)field_frame(&IDLE_PTR, id, buf, sizeof buf, true, NULL);
    (void)field_frame(&IDLE_PTR, id, buf, sizeof buf, true, NULL);

    /* A single press at the left edge focuses the field and arms a drag; caret/anchor collapse there. */
    nt_pointer_t press = make_pointer(IN_X + PAD_X + 1.0F, IN_Y + (IN_H * 0.5F), true, true, false);
    (void)field_frame(&press, id, buf, sizeof buf, true, NULL);
    TEST_ASSERT_TRUE(nt_ui_input_focused(s_fx.ctx, id));
    uint32_t caret0 = 99U;
    uint8_t drag0 = 0U;
    TEST_ASSERT_TRUE(nt_ui_input_test_state(s_fx.ctx, id, &caret0, &drag0, NULL));
    TEST_ASSERT_EQUAL_UINT8(1U, drag0); /* drag armed by the press */

    /* Esc while still holding -> the field unfocuses (Esc handler runs after the drag block; the pointer
     * did not move this frame, so the caret is unchanged here). */
    nt_input_poll();
    nt_input_set_key(NT_KEY_ESCAPE, true);
    nt_pointer_t hold = make_pointer(IN_X + PAD_X + 1.0F, IN_Y + (IN_H * 0.5F), true, false, false);
    (void)field_frame(&hold, id, buf, sizeof buf, true, NULL);
    nt_input_set_key(NT_KEY_ESCAPE, false);
    TEST_ASSERT_FALSE(nt_ui_input_focused(s_fx.ctx, id));

    /* Still held, drag the pointer far right while UNFOCUSED. The abandoned drag must NOT move the caret
     * (pre-fix it would: the held-drag branch was not gated on focus). */
    nt_pointer_t drag_far = make_pointer(IN_X + IN_W - PAD_X - 1.0F, IN_Y + (IN_H * 0.5F), true, false, false);
    (void)field_frame(&drag_far, id, buf, sizeof buf, true, NULL);
    uint32_t caret1 = 99U;
    uint8_t drag1 = 9U;
    TEST_ASSERT_TRUE(nt_ui_input_test_state(s_fx.ctx, id, &caret1, &drag1, NULL));
    TEST_ASSERT_EQUAL_UINT8(0U, drag1);       /* drag abandoned on focus loss */
    TEST_ASSERT_EQUAL_UINT32(caret0, caret1); /* caret did not follow the far-right drag */
}

/* ---- Test 33: props->max_length caps content in BYTES, independently of buffer_size. ---- */
static void test_max_length_byte_cap(void) {
    char buf[32] = {0}; /* buffer_size-1 = 31 would otherwise allow far more */
    const uint32_t id = nt_ui_id("f");
    s_props.max_length = 4U; /* 4 = NUL-inclusive cap -> 3 content bytes */
    warmup_focus(id, buf, sizeof buf);

    /* Type 5 ASCII chars in one frame; only 3 fit under max_length (the 4th/5th drop, no partial). */
    nt_input_buffer_char((uint32_t)'a');
    nt_input_buffer_char((uint32_t)'b');
    nt_input_buffer_char((uint32_t)'c');
    nt_input_buffer_char((uint32_t)'d');
    nt_input_buffer_char((uint32_t)'e');
    (void)field_frame(&IDLE_PTR, id, buf, sizeof buf, true, NULL);
    TEST_ASSERT_EQUAL_STRING("abc", buf); /* capped by max_length, not by the 32-byte buffer */
}

/* ---- Test 34: clicking a different field while one is focused moves focus there (no Tab). ---- */
static void test_click_refocus_other_field(void) {
    char a[32] = {0};
    char b[32] = {0};
    const uint32_t id_a = nt_ui_id("ra");
    const uint32_t id_b = nt_ui_id("rb");
    two_field_frame(&IDLE_PTR, id_a, a, id_b, b); /* warm both bboxes */

    /* Click field A (top row) -> A focused. */
    const float ay = IN_Y + (IN_H * 0.5F);
    nt_pointer_t press_a = make_pointer(IN_X + PAD_X + 1.0F, ay, true, true, false);
    two_field_frame(&press_a, id_a, a, id_b, b);
    nt_pointer_t rel_a = make_pointer(IN_X + PAD_X + 1.0F, ay, false, false, true);
    two_field_frame(&rel_a, id_a, a, id_b, b);
    TEST_ASSERT_TRUE(nt_ui_input_focused(s_fx.ctx, id_a));
    TEST_ASSERT_FALSE(nt_ui_input_focused(s_fx.ctx, id_b));

    /* Click field B (second row) WITHOUT Tab -> focus moves to B, A unfocuses. */
    const float by = IN_Y + IN_H + (IN_H * 0.5F);
    nt_pointer_t press_b = make_pointer(IN_X + PAD_X + 1.0F, by, true, true, false);
    two_field_frame(&press_b, id_a, a, id_b, b);
    TEST_ASSERT_FALSE(nt_ui_input_focused(s_fx.ctx, id_a));
    TEST_ASSERT_TRUE(nt_ui_input_focused(s_fx.ctx, id_b));
}

/* ---- Test 35: Tab skips a disabled field; focus lands on the next ENABLED field, not lost. ---- */
static void test_tab_skips_disabled_field(void) {
    char a[32] = {0};
    char b[32] = {0};
    char c[32] = {0};
    const uint32_t ia = nt_ui_id("ta");
    const uint32_t ib = nt_ui_id("tb");
    const uint32_t ic = nt_ui_id("tc");
    three_field_frame(&IDLE_PTR, ia, a, ib, b, false, ic, c); /* warm; middle field B disabled */

    /* Click field A (top row) to focus it. */
    const float ay = IN_Y + (IN_H * 0.5F);
    nt_pointer_t press = make_pointer(IN_X + PAD_X + 1.0F, ay, true, true, false);
    three_field_frame(&press, ia, a, ib, b, false, ic, c);
    nt_pointer_t rel = make_pointer(IN_X + PAD_X + 1.0F, ay, false, false, true);
    three_field_frame(&rel, ia, a, ib, b, false, ic, c);
    TEST_ASSERT_TRUE(nt_ui_input_focused(s_fx.ctx, ia));

    /* Tab: B is disabled, so the seek must skip it and land on C (pre-fix the disabled B ate the seek
     * and focus vanished). */
    nt_input_poll();
    nt_input_set_key(NT_KEY_TAB, true);
    three_field_frame(&IDLE_PTR, ia, a, ib, b, false, ic, c);
    nt_input_set_key(NT_KEY_TAB, false);
    TEST_ASSERT_FALSE(nt_ui_input_focused(s_fx.ctx, ia));
    TEST_ASSERT_FALSE(nt_ui_input_focused(s_fx.ctx, ib)); /* a disabled field never holds focus */
    TEST_ASSERT_TRUE(nt_ui_input_focused(s_fx.ctx, ic));  /* seek skipped B and reached C */
}

/* ---- Test 36: a responsive (GROW) width field still scrolls to keep the caret visible. ---- */
static void test_scroll_responsive_width(void) {
    char buf[64];
    strcpy(buf, "the quick brown fox jumps over the lazy dog"); /* ~430px @10px/glyph >> ~228px inner width */
    const uint32_t id = nt_ui_id("gw");
    grow_field_frame(&IDLE_PTR, id, buf, sizeof buf);
    grow_field_frame(&IDLE_PTR, id, buf, sizeof buf); /* warm: the GROW field gets a measured bbox width */

    /* Click to focus. */
    nt_pointer_t press = make_pointer(IN_X + PAD_X + 1.0F, IN_Y + (IN_H * 0.5F), true, true, false);
    grow_field_frame(&press, id, buf, sizeof buf);
    nt_pointer_t rel = make_pointer(IN_X + PAD_X + 1.0F, IN_Y + (IN_H * 0.5F), false, false, true);
    grow_field_frame(&rel, id, buf, sizeof buf);
    TEST_ASSERT_TRUE(nt_ui_input_focused(s_fx.ctx, id));

    /* Jump the caret to END: the long line must scroll right to keep the caret in view. With FIXED-only
     * scroll (pre-fix) inner_w was 0 here and scroll_x stayed pinned at 0. */
    nt_input_poll();
    nt_input_set_key(NT_KEY_END, true);
    grow_field_frame(&IDLE_PTR, id, buf, sizeof buf);
    nt_input_set_key(NT_KEY_END, false);

    float scroll_x = -1.0F;
    TEST_ASSERT_TRUE(nt_ui_input_test_state(s_fx.ctx, id, NULL, NULL, &scroll_x));
    TEST_ASSERT_TRUE_MESSAGE(scroll_x > 0.0F, "responsive-width field must scroll to keep the caret visible");
}

/* ---- Death tests (NT_ASSERT_FULL only) ---- */
#if NT_ASSERT_MODE == NT_ASSERT_FULL

static void test_assert_null_buffer(void) {
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &IDLE_PTR, 1);
    CLAY({.id = CLAY_ID("root")}) { NT_TEST_EXPECT_ASSERT((void)nt_ui_input_text(s_fx.ctx, NULL, 0, nt_ui_id("f"), NULL, 32U, &s_props, &s_style, &s_field_decl, true, NULL)); }
    nt_ui_end(s_fx.ctx);
}

static void test_assert_zero_cap(void) {
    char buf[4] = {0};
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &IDLE_PTR, 1);
    CLAY({.id = CLAY_ID("root")}) { NT_TEST_EXPECT_ASSERT((void)nt_ui_input_text(s_fx.ctx, NULL, 0, nt_ui_id("f"), buf, 0U, &s_props, &s_style, &s_field_decl, true, NULL)); }
    nt_ui_end(s_fx.ctx);
}

static void test_assert_null_props(void) {
    char buf[4] = {0};
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &IDLE_PTR, 1);
    CLAY({.id = CLAY_ID("root")}) { NT_TEST_EXPECT_ASSERT((void)nt_ui_input_text(s_fx.ctx, NULL, 0, nt_ui_id("f"), buf, sizeof buf, NULL, &s_style, &s_field_decl, true, NULL)); }
    nt_ui_end(s_fx.ctx);
}

static void test_assert_null_style(void) {
    char buf[4] = {0};
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &IDLE_PTR, 1);
    CLAY({.id = CLAY_ID("root")}) { NT_TEST_EXPECT_ASSERT((void)nt_ui_input_text(s_fx.ctx, NULL, 0, nt_ui_id("f"), buf, sizeof buf, &s_props, NULL, &s_field_decl, true, NULL)); }
    nt_ui_end(s_fx.ctx);
}

#endif /* NT_ASSERT_MODE == NT_ASSERT_FULL */

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_insert_cyrillic_codepoints);
    RUN_TEST(test_backspace_one_codepoint);
    RUN_TEST(test_arrow_left_codepoint);
    RUN_TEST(test_clamp_no_split_no_oob);
    RUN_TEST(test_numeric_predicate);
    RUN_TEST(test_click_hit_test_boundary);
    RUN_TEST(test_focus_tab_esc);
    RUN_TEST(test_focus_orphan_cleared);
    RUN_TEST(test_focus_dropped_when_disabled);
    RUN_TEST(test_submit_and_change);
    RUN_TEST(test_unfocused_ignores_chars);
    RUN_TEST(test_dblclick_longpress_primitive);
    RUN_TEST(test_shift_select_and_collapse);
    RUN_TEST(test_shift_select_cyrillic);
    RUN_TEST(test_ctrl_a_select_all);
    RUN_TEST(test_double_click_word_select);
    RUN_TEST(test_clipboard_copy_paste_roundtrip);
    RUN_TEST(test_clipboard_cut);
    RUN_TEST(test_clipboard_cut_unavailable_noop);
    RUN_TEST(test_clipboard_paste_clamp);
    RUN_TEST(test_clipboard_paste_clamp_no_split);
    RUN_TEST(test_clipboard_paste_filtered);
    RUN_TEST(test_clipboard_paste_replaces_selection);
    RUN_TEST(test_password_mask_render);
    RUN_TEST(test_password_buffer_and_keyboard);
    RUN_TEST(test_delete_key_right);
    RUN_TEST(test_clipboard_paste_malformed_utf8);
    RUN_TEST(test_drag_select_range);
    RUN_TEST(test_password_buffer_unchanged_after_render);
    RUN_TEST(test_style_defaults_valid);
    RUN_TEST(test_placeholder_empty_unfocused_only);
    RUN_TEST(test_key_repeat_timing);
    RUN_TEST(test_bg_art_emits_image);
    RUN_TEST(test_no_bg_art_no_image);
    RUN_TEST(test_bg_art_focus_swap);
    RUN_TEST(test_drag_abandoned_on_focus_loss);
    RUN_TEST(test_max_length_byte_cap);
    RUN_TEST(test_click_refocus_other_field);
    RUN_TEST(test_tab_skips_disabled_field);
    RUN_TEST(test_scroll_responsive_width);
#if NT_ASSERT_MODE == NT_ASSERT_FULL
    RUN_TEST(test_assert_null_buffer);
    RUN_TEST(test_assert_zero_cap);
    RUN_TEST(test_assert_null_props);
    RUN_TEST(test_assert_null_style);
#endif
    return UNITY_END();
}
