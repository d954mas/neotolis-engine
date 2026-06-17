/* Dropdown / combobox tests (WGT-01). Driven through the walker fixture + NT_TEST_ACCESS probes (no GL
 * surface). Covers: a row click writes the game-owned int* selected and closes the list; a long list
 * scrolls via the nt_ui_scroll wrapper without leaking a scroll-container state-pool slot across N
 * open/close cycles (Pitfall 7 / T-65-14); the list edge-flips ABOVE near the bottom border. */

#include <stdalign.h>
#include <stdbool.h>
#include <stdint.h>

#include "clay.h"
#include "input/nt_input_internal.h"
#include "test_helpers/nt_assert_trap.h"
#include "test_helpers/ui_test_arena.h"
#include "test_helpers/ui_walker_fixture.h"
#include "ui/nt_ui.h"
#include "ui/nt_ui_dropdown.h"
#include "ui/nt_ui_internal.h"
#include "ui/nt_ui_popup.h"
#include "ui/nt_ui_state.h"
#include "unity.h"

alignas(NT_UI_ARENA_ALIGN) static uint8_t s_arena[NT_UI_TEST_ARENA_SIZE];
static ui_walker_fixture_t s_fx;

#define VIEW_W 800.0F
#define VIEW_H 600.0F

#define DD_A 0x44D001U

static const char *const s_short[] = {"Alpha", "Beta", "Gamma"};
/* >max_visible_rows so the list scrolls. */
static const char *const s_long[] = {"r0", "r1", "r2", "r3", "r4", "r5", "r6", "r7", "r8", "r9", "r10", "r11"};

void setUp(void) {
    nt_test_assert_install();
    nt_input_clear_all_keys();
    nt_input_poll(); /* clear sticky edges from a prior test */
    ui_walker_fixture_init(&s_fx, s_arena, sizeof s_arena, UI_WALKER_FX_BIND_ALL);
}

void tearDown(void) {
    nt_input_clear_all_keys();
    ui_walker_fixture_shutdown(&s_fx);
}

static nt_pointer_t pointer_at(float x, float y, bool is_down, bool is_pressed, bool is_released) {
    nt_pointer_t p = {0};
    p.x = x;
    p.y = y;
    p.active = true;
    p.buttons[NT_BUTTON_LEFT].is_down = is_down;
    p.buttons[NT_BUTTON_LEFT].is_pressed = is_pressed;
    p.buttons[NT_BUTTON_LEFT].is_released = is_released;
    return p;
}

/* ---- ABI sanity ---- */
static void test_dropdown_abi_size(void) { TEST_ASSERT_EQUAL_UINT(40U, (unsigned)sizeof(nt_ui_dropdown_style_t)); }

static void test_dropdown_defaults_valid(void) {
    nt_ui_dropdown_style_t st = nt_ui_dropdown_style_defaults();
    TEST_ASSERT_TRUE(st.font_size > 0.0F);
    TEST_ASSERT_TRUE(st.row_height > 0U);
    TEST_ASSERT_TRUE(st.min_width > 0U);
    TEST_ASSERT_TRUE(st.max_visible_rows > 0U);
}

/* One full dropdown frame: a trigger at a fixed position, then the open list. Returns whether a
 * selection was made this frame. The trigger uses a floating element so its bbox is at (tx,ty). */
static bool dropdown_frame(const nt_pointer_t *p, float tx, float ty, const char *const *labels, int count, int *selected, bool *open, const nt_ui_dropdown_style_t *st, bool *out_made) {
    bool toggled = false;
    bool made = false;
    nt_ui_begin(s_fx.ctx, VIEW_W, VIEW_H, 1.0F / 60.0F, p, 1);
    CLAY({.id = (Clay_ElementId){.id = 0xDD0007U}, .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .offset = {.x = tx, .y = ty}}}) {
        toggled = nt_ui_dropdown_trigger(s_fx.ctx, NT_UI_DATA_LAYER(1), 2U, DD_A, labels, count, *selected, "Select...", st,
                                         &(Clay_ElementDeclaration){.layout = {.sizing = {CLAY_SIZING_FIXED(160), CLAY_SIZING_FIXED(32)}}}, open);
    }
    made = nt_ui_dropdown_list(s_fx.ctx, NT_UI_DATA_LAYER(1), 2U, DD_A, labels, count, selected, st, open);
    nt_ui_end(s_fx.ctx);
    if (out_made != NULL) {
        *out_made = made;
    }
    return toggled;
}

/* ---- Trigger toggles the game-owned open bool. ---- */
static void test_dropdown_trigger_toggles_open(void) {
    nt_ui_dropdown_style_t st = nt_ui_dropdown_style_defaults();
    int selected = -1;
    bool open = false;

    /* Warm frame so the trigger bbox is baked. */
    nt_pointer_t f1 = pointer_at(40.0F, 40.0F, false, false, false);
    dropdown_frame(&f1, 30.0F, 30.0F, s_short, 3, &selected, &open, &st, NULL);

    /* Press inside the trigger, then release inside -> click toggles open. */
    nt_pointer_t f2 = pointer_at(40.0F, 40.0F, true, true, false);
    dropdown_frame(&f2, 30.0F, 30.0F, s_short, 3, &selected, &open, &st, NULL);
    nt_pointer_t f3 = pointer_at(40.0F, 40.0F, false, false, true);
    dropdown_frame(&f3, 30.0F, 30.0F, s_short, 3, &selected, &open, &st, NULL);
    TEST_ASSERT_TRUE(open);
}

/* ---- A row click writes *selected and closes the list (Model D). ---- */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_dropdown_row_select_sets_int_and_closes(void) {
    nt_ui_dropdown_style_t st = nt_ui_dropdown_style_defaults();
    int selected = -1;
    bool open = true; /* list already open */

    /* The list opens below the trigger at (30,30)+(0,32). Row i center ~ y = 62 + pad + i*row_height. */
    const float row_x = 60.0F;
    const float row1_y = 30.0F + 32.0F + (float)st.pad + (1.5F * (float)st.row_height);

    /* Two warm frames: the list anchors to the trigger's PREVIOUS-frame bbox (1-frame IM lag), so the
     * rows are only at their final screen position from the 2nd frame on. */
    nt_pointer_t w = pointer_at(row_x, row1_y, false, false, false);
    dropdown_frame(&w, 30.0F, 30.0F, s_short, 3, &selected, &open, &st, NULL);
    dropdown_frame(&w, 30.0F, 30.0F, s_short, 3, &selected, &open, &st, NULL);

    /* Frame 2: press on row 1. */
    nt_pointer_t pr = pointer_at(row_x, row1_y, true, true, false);
    dropdown_frame(&pr, 30.0F, 30.0F, s_short, 3, &selected, &open, &st, NULL);

    /* Frame 3: release over row 1 -> clicked -> selected = 1, open cleared. */
    bool made = false;
    nt_pointer_t rl = pointer_at(row_x, row1_y, false, false, true);
    dropdown_frame(&rl, 30.0F, 30.0F, s_short, 3, &selected, &open, &st, &made);
    TEST_ASSERT_EQUAL_INT(1, selected);
    TEST_ASSERT_FALSE(open);
    TEST_ASSERT_TRUE(made);
}

/* ---- Non-leak: opening + closing a LONG (scrolling) list >= 8 times must not grow the state-pool
 *      slot count monotonically (the scroll container's cell is GC-safe; a raw .clip would leak). ---- */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_dropdown_long_list_scroll_no_leak(void) {
    nt_ui_dropdown_style_t st = nt_ui_dropdown_style_defaults();
    st.max_visible_rows = 4; /* 12 rows > 4 -> the list scrolls */
    int selected = 0;

    /* Baseline: one closed-then-open-then-closed warm cycle, settle, sample the pool. */
    bool open = false;
    nt_pointer_t idle = pointer_at(400.0F, 300.0F, false, false, false);
    dropdown_frame(&idle, 30.0F, 30.0F, s_long, 12, &selected, &open, &st, NULL);

    uint32_t baseline = 0U;
    for (int cycle = 0; cycle < 10; ++cycle) {
        /* Open: declare the scrolling list for several frames (lets the scroll cell allocate). */
        open = true;
        for (int f = 0; f < 4; ++f) {
            dropdown_frame(&idle, 30.0F, 30.0F, s_long, 12, &selected, &open, &st, NULL);
        }
        /* Close + let the close tween settle so the popup stops declaring the list. */
        open = false;
        for (int f = 0; f < 60; ++f) {
            dropdown_frame(&idle, 30.0F, 30.0F, s_long, 12, &selected, &open, &st, NULL);
        }
        const uint32_t used = nt_ui_state_used_slots(s_fx.ctx);
        if (cycle == 1) {
            baseline = used; /* after cycle 0/1 the working set is established */
        } else if (cycle > 1) {
            /* No monotonic growth: the per-cycle scroll cell is reused, not leaked. */
            TEST_ASSERT_TRUE_MESSAGE(used <= baseline, "scroll-container pool slot leaked across open/close cycles");
        }
    }
}

/* ---- Edge-flip: a trigger near the bottom border opens its list ABOVE (popup-core side probe). ---- */
static void test_dropdown_edge_flip_up_near_bottom(void) {
    nt_ui_dropdown_style_t st = nt_ui_dropdown_style_defaults();
    int selected = 0;
    bool open = true;

    /* Trigger near the bottom: the list would overflow downward -> flips ABOVE. Two frames so the list's
     * prev-frame bbox feeds the edge-flip projection (frame 1 measures, frame 2 flips). */
    nt_pointer_t idle = pointer_at(400.0F, 300.0F, false, false, false);
    dropdown_frame(&idle, 30.0F, 540.0F, s_short, 3, &selected, &open, &st, NULL);
    dropdown_frame(&idle, 30.0F, 540.0F, s_short, 3, &selected, &open, &st, NULL);
    TEST_ASSERT_EQUAL_UINT8(NT_UI_POPUP_ABOVE, nt_ui_dropdown_test_last_side());
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_dropdown_abi_size);
    RUN_TEST(test_dropdown_defaults_valid);
    RUN_TEST(test_dropdown_trigger_toggles_open);
    RUN_TEST(test_dropdown_row_select_sets_int_and_closes);
    RUN_TEST(test_dropdown_long_list_scroll_no_leak);
    RUN_TEST(test_dropdown_edge_flip_up_near_bottom);
    return UNITY_END();
}
