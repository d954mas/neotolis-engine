#ifndef NT_TEST_HELPER_UI_SCENARIO_H
#define NT_TEST_HELPER_UI_SCENARIO_H

/* Scripted user actions over a game frame, for scenario tests that assert only what a game observes. Each
 * action runs whole frames: the pointer carries its movement since the last frame and the press or release
 * edge on its own frame only; keys and chars go through nt_input like the window pump. */

#include <stdbool.h>
#include <stdint.h>

#include "input/nt_input.h"
#include "memory/nt_mem_scratch.h"
#include "test_helpers/nt_input_test_frame.h"
#include "test_helpers/ui_fx_frame.h"
#include "ui/nt_ui.h"
#include "unity.h"

/* Declares the game's UI for one frame, between the driver's nt_ui_begin and nt_ui_end. */
typedef void (*sc_frame_fn)(void *game);

typedef struct {
    nt_ui_context_t *ctx;
    sc_frame_fn frame;
    void *game;
    float x, y;
    bool down[NT_BUTTON_MAX];
    /* Read after the last frame's nt_ui_end, where a game runs its own input: the key owner is fixed at begin,
     * the pointer answer needs every record of the frame. */
    bool wants_keyboard;
    bool wants_pointer;
} sc_t;

/* One frame of `dt` seconds (0 pauses every tween) with the pointer at (x, y) and a wheel delta; `press` /
 * `release` is the button whose edge this frame carries, or -1. */
static inline void sc_step_dt(sc_t *s, float dt, float x, float y, int press, int release, float wheel_dy) {
    nt_pointer_t p = {.x = x, .y = y, .dx = x - s->x, .dy = y - s->y, .active = true, .wheel_dy = wheel_dy};
    for (int b = 0; b < NT_BUTTON_MAX; ++b) {
        s->down[b] = (b == press) || (s->down[b] && b != release);
        p.buttons[b] = (nt_button_state_t){.is_down = s->down[b], .is_pressed = b == press, .is_released = b == release};
    }
    s->x = x;
    s->y = y;
    nt_arena_reset(&g_nt_mem_scratch);
    ui_fx_frame_begin_ex(s->ctx, dt, &p, 1U, 0U);
    s->frame(s->game);
    nt_ui_end(s->ctx);
    s->wants_keyboard = nt_ui_wants_keyboard(s->ctx);
    s->wants_pointer = nt_ui_wants_pointer(s->ctx);
}

/* One 60 Hz frame with the pointer at (x, y) and a wheel delta; `press` / `release` as in sc_step_dt. */
static inline void sc_step_wheel(sc_t *s, float x, float y, int press, int release, float wheel_dy) { sc_step_dt(s, UI_FX_DT, x, y, press, release, wheel_dy); }

/* One frame with the pointer at (x, y); `press` / `release` is the button whose edge this frame carries, or -1. */
static inline void sc_step(sc_t *s, float x, float y, int press, int release) { sc_step_wheel(s, x, y, press, release, 0.0F); }

static inline void sc_frames(sc_t *s, int n) {
    for (int i = 0; i < n; ++i) {
        sc_step(s, s->x, s->y, -1, -1);
    }
}

static inline void sc_move(sc_t *s, float x, float y) { sc_step(s, x, y, -1, -1); }
static inline void sc_press(sc_t *s, nt_button_t b) { sc_step(s, s->x, s->y, (int)b, -1); }
static inline void sc_release(sc_t *s, nt_button_t b) { sc_step(s, s->x, s->y, -1, (int)b); }

static inline void sc_click(sc_t *s, float x, float y) {
    sc_move(s, x, y);
    sc_press(s, NT_BUTTON_LEFT);
    sc_release(s, NT_BUTTON_LEFT);
}

/* One frame with `key` pressed; its release reaches the next frame. */
static inline void sc_key(sc_t *s, nt_key_t key) {
    nt_input_feed(nt_test_key(key, true));
    sc_frames(s, 1);
    nt_input_feed(nt_test_key(key, false));
}

/* One frame that types `cp`. */
static inline void sc_char(sc_t *s, uint32_t cp) {
    nt_input_feed(nt_test_char(cp));
    sc_frames(s, 1);
}

/* Last frame's box of `id`; fails the test when the frame did not lay it out. */
static inline nt_ui_bbox_t sc_box(const sc_t *s, uint32_t id) {
    const nt_ui_bbox_t bb = nt_ui_get_bbox(s->ctx, id);
    TEST_ASSERT_TRUE_MESSAGE(bb.found, "the element was not laid out last frame");
    return bb;
}

static inline void sc_move_to(sc_t *s, uint32_t id) {
    const nt_ui_bbox_t bb = sc_box(s, id);
    sc_move(s, bb.x + (bb.width * 0.5F), bb.y + (bb.height * 0.5F));
}

static inline void sc_click_id(sc_t *s, uint32_t id) {
    const nt_ui_bbox_t bb = sc_box(s, id);
    sc_click(s, bb.x + (bb.width * 0.5F), bb.y + (bb.height * 0.5F));
}

/* One frame that moves onto the center of `id`'s box and presses the left button there: no frame passes between
 * the arrival and the press, for a test whose frame count is the rule. */
static inline void sc_press_at_id(sc_t *s, uint32_t id) {
    const nt_ui_bbox_t bb = sc_box(s, id);
    sc_step(s, bb.x + (bb.width * 0.5F), bb.y + (bb.height * 0.5F), NT_BUTTON_LEFT, -1);
}

/* The release of sc_press_at_id, at the center of `id`'s box in the next frame. */
static inline void sc_release_at_id(sc_t *s, uint32_t id) {
    const nt_ui_bbox_t bb = sc_box(s, id);
    sc_step(s, bb.x + (bb.width * 0.5F), bb.y + (bb.height * 0.5F), -1, NT_BUTTON_LEFT);
}

#endif /* NT_TEST_HELPER_UI_SCENARIO_H */
