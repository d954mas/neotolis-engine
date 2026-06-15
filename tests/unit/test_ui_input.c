/* Text-field widget tests (nt_ui_input_text core, T0-T1).
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

alignas(NT_UI_ARENA_ALIGN) static uint8_t s_arena[NT_UI_TEST_ARENA_SIZE];
static ui_walker_fixture_t s_fx;
static nt_ui_input_style_t s_style;

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
    s_style.caret_blink_rate = 0.0F; /* always-on caret -> deterministic render */
    s_style.allow = NULL;
}

void setUp(void) {
    nt_test_assert_install();
    nt_input_init(); /* clean key + char-ring state per test */
    ui_walker_fixture_init(&s_fx, s_arena, sizeof s_arena, UI_WALKER_FX_BIND_ALL);
    nt_font_test_set_metrics(s_fx.stub_font, 1000, 800, -200, 1000); /* tofu advance = size/2 */
    init_style();
}

void tearDown(void) {
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

/* One frame with a single field. Feeds the given pointer; returns the change flag. */
static bool field_frame(const nt_pointer_t *p, uint32_t id, char *buf, size_t cap, bool enabled, bool *submitted) {
    bool changed = false;
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.016F, p, 1);
    CLAY({.id = CLAY_ID("root"), .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .offset = {.x = IN_X, .y = IN_Y}}}) {
        changed = nt_ui_input_text(s_fx.ctx, NULL, 0, id, buf, cap, &s_style, &s_field_decl, enabled, submitted);
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
    s_style.allow = nt_ui_filter_numeric;
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
            (void)nt_ui_input_text(s_fx.ctx, NULL, 0, id_a, a, sizeof a, &s_style, &s_field_decl, true, NULL);
            (void)nt_ui_input_text(s_fx.ctx, NULL, 0, id_b, b, sizeof b, &s_style, &s_field_decl, true, NULL);
        }
    }
    nt_ui_end(s_fx.ctx);

    /* Press inside field A to focus it. */
    nt_pointer_t press = make_pointer(IN_X + PAD_X + 1.0F, IN_Y + (IN_H * 0.5F), true, true, false);
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.016F, &press, 1);
    CLAY({.id = CLAY_ID("root"), .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .offset = {.x = IN_X, .y = IN_Y}}}) {
        CLAY({.id = CLAY_ID("col"), .layout = {.layoutDirection = CLAY_TOP_TO_BOTTOM}}) {
            (void)nt_ui_input_text(s_fx.ctx, NULL, 0, id_a, a, sizeof a, &s_style, &s_field_decl, true, NULL);
            (void)nt_ui_input_text(s_fx.ctx, NULL, 0, id_b, b, sizeof b, &s_style, &s_field_decl, true, NULL);
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
            (void)nt_ui_input_text(s_fx.ctx, NULL, 0, id_a, a, sizeof a, &s_style, &s_field_decl, true, NULL);
            (void)nt_ui_input_text(s_fx.ctx, NULL, 0, id_b, b, sizeof b, &s_style, &s_field_decl, true, NULL);
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
            (void)nt_ui_input_text(s_fx.ctx, NULL, 0, id_a, a, sizeof a, &s_style, &s_field_decl, true, NULL);
            (void)nt_ui_input_text(s_fx.ctx, NULL, 0, id_b, b, sizeof b, &s_style, &s_field_decl, true, NULL);
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
            (void)nt_ui_input_text(s_fx.ctx, NULL, 0, id_a, a, sizeof a, &s_style, &s_field_decl, true, NULL);
            (void)nt_ui_input_text(s_fx.ctx, NULL, 0, id_b, b, sizeof b, &s_style, &s_field_decl, true, NULL);
        }
    }
    nt_ui_end(s_fx.ctx);
    nt_input_set_key(NT_KEY_ESCAPE, false);
    TEST_ASSERT_FALSE(nt_ui_input_focused(s_fx.ctx, id_a));
    TEST_ASSERT_FALSE(nt_ui_input_focused(s_fx.ctx, id_b));
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
    s_style.allow = nt_ui_filter_numeric;
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
    s_style.password = true;
    s_style.keyboard = NT_UI_KB_PASSWORD;
    warmup_focus(id, buf, sizeof buf);

    /* Typing fills the REAL buffer (the mask only affects rendering). */
    nt_input_buffer_char((uint32_t)'s');
    nt_input_buffer_char((uint32_t)'e');
    nt_input_buffer_char((uint32_t)'c');
    (void)field_frame(&IDLE_PTR, id, buf, sizeof buf, true, NULL);
    TEST_ASSERT_EQUAL_STRING("sec", buf); /* native: keyboard enum stored, edit behavior unchanged */
}

/* ---- Test 11: style defaults are a valid baseline (caret_width > 0, sensible blink). ---- */
static void test_style_defaults_valid(void) {
    nt_ui_input_style_t s = nt_ui_input_style_defaults();
    TEST_ASSERT_TRUE(s.caret_width > 0.0F);
    TEST_ASSERT_TRUE(s.text.font_size > 0.0F);
    TEST_ASSERT_EQUAL_INT(NT_UI_KB_TEXT, s.keyboard);
}

/* ---- Death tests (NT_ASSERT_FULL only) ---- */
#if NT_ASSERT_MODE == NT_ASSERT_FULL

static void test_assert_null_buffer(void) {
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &IDLE_PTR, 1);
    CLAY({.id = CLAY_ID("root")}) { NT_TEST_EXPECT_ASSERT((void)nt_ui_input_text(s_fx.ctx, NULL, 0, nt_ui_id("f"), NULL, 32U, &s_style, &s_field_decl, true, NULL)); }
    nt_ui_end(s_fx.ctx);
}

static void test_assert_zero_cap(void) {
    char buf[4] = {0};
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &IDLE_PTR, 1);
    CLAY({.id = CLAY_ID("root")}) { NT_TEST_EXPECT_ASSERT((void)nt_ui_input_text(s_fx.ctx, NULL, 0, nt_ui_id("f"), buf, 0U, &s_style, &s_field_decl, true, NULL)); }
    nt_ui_end(s_fx.ctx);
}

static void test_assert_null_style(void) {
    char buf[4] = {0};
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &IDLE_PTR, 1);
    CLAY({.id = CLAY_ID("root")}) { NT_TEST_EXPECT_ASSERT((void)nt_ui_input_text(s_fx.ctx, NULL, 0, nt_ui_id("f"), buf, sizeof buf, NULL, &s_field_decl, true, NULL)); }
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
    RUN_TEST(test_submit_and_change);
    RUN_TEST(test_unfocused_ignores_chars);
    RUN_TEST(test_dblclick_longpress_primitive);
    RUN_TEST(test_shift_select_and_collapse);
    RUN_TEST(test_shift_select_cyrillic);
    RUN_TEST(test_ctrl_a_select_all);
    RUN_TEST(test_double_click_word_select);
    RUN_TEST(test_clipboard_copy_paste_roundtrip);
    RUN_TEST(test_clipboard_cut);
    RUN_TEST(test_clipboard_paste_clamp);
    RUN_TEST(test_clipboard_paste_clamp_no_split);
    RUN_TEST(test_clipboard_paste_filtered);
    RUN_TEST(test_clipboard_paste_replaces_selection);
    RUN_TEST(test_password_mask_render);
    RUN_TEST(test_password_buffer_and_keyboard);
    RUN_TEST(test_style_defaults_valid);
#if NT_ASSERT_MODE == NT_ASSERT_FULL
    RUN_TEST(test_assert_null_buffer);
    RUN_TEST(test_assert_zero_cap);
    RUN_TEST(test_assert_null_style);
#endif
    return UNITY_END();
}
