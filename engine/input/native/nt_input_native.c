#include "input/nt_input_internal.h"
#include "window/nt_window.h" /* g_nt_window.dpr for coordinate mapping */

/* ---- Event buffers (filled by nt_window callbacks, drained by platform_poll) ---- */

#define NT_MAX_KEY_EVENTS 64
#define NT_MAX_PTR_EVENTS 64
#define NT_MAX_WHEEL_EVENTS 16

typedef struct {
    nt_key_t key;
    bool down;
} nt_key_event_t;

typedef struct {
    double x, y;
    uint8_t buttons;
    bool is_down;
} nt_ptr_event_t;

typedef struct {
    float dx, dy;
} nt_wheel_event_t;

static nt_key_event_t s_key_buf[NT_MAX_KEY_EVENTS];
static uint32_t s_key_count;

static nt_ptr_event_t s_ptr_buf[NT_MAX_PTR_EVENTS];
static uint32_t s_ptr_count;

static nt_wheel_event_t s_wheel_buf[NT_MAX_WHEEL_EVENTS];
static uint32_t s_wheel_count;

static bool s_focus_lost;

/* ---- Buffer write functions (called from nt_window_native.c callbacks) ---- */

void nt_input_buffer_key(nt_key_t key, bool down) {
    if (s_key_count < NT_MAX_KEY_EVENTS) {
        s_key_buf[s_key_count++] = (nt_key_event_t){.key = key, .down = down};
    } else {
        s_key_buf[NT_MAX_KEY_EVENTS - 1] = (nt_key_event_t){.key = key, .down = down};
    }
}

void nt_input_buffer_pointer(bool is_down, double raw_x, double raw_y, uint8_t buttons) {
    /* Coalesce consecutive moves: if the tail is also a plain move with the
       same button mask, overwrite its coordinates instead of appending.
       Button transitions (is_down=true or changed mask) always get their own
       slot so press/release events are never lost. */
    if (!is_down && s_ptr_count > 0) {
        nt_ptr_event_t *tail = &s_ptr_buf[s_ptr_count - 1];
        if (!tail->is_down && tail->buttons == buttons) {
            tail->x = raw_x;
            tail->y = raw_y;
            return;
        }
    }
    if (s_ptr_count < NT_MAX_PTR_EVENTS) {
        s_ptr_buf[s_ptr_count++] = (nt_ptr_event_t){.x = raw_x, .y = raw_y, .buttons = buttons, .is_down = is_down};
    } else {
        /* Buffer full — overwrite tail so the newest state (especially button
           release) is never lost. Losing a mid-frame move is acceptable;
           losing the final press/release causes stuck buttons. */
        s_ptr_buf[NT_MAX_PTR_EVENTS - 1] = (nt_ptr_event_t){.x = raw_x, .y = raw_y, .buttons = buttons, .is_down = is_down};
    }
}

void nt_input_buffer_wheel(float dx, float dy) {
    if (s_wheel_count < NT_MAX_WHEEL_EVENTS) {
        s_wheel_buf[s_wheel_count++] = (nt_wheel_event_t){.dx = dx, .dy = dy};
    } else {
        /* Accumulate into tail — wheel deltas are additive */
        s_wheel_buf[NT_MAX_WHEEL_EVENTS - 1].dx += dx;
        s_wheel_buf[NT_MAX_WHEEL_EVENTS - 1].dy += dy;
    }
}

void nt_input_buffer_focus_lost(void) { s_focus_lost = true; }

/* ---- Platform lifecycle ---- */

void nt_input_platform_init(void) {}

void nt_input_platform_poll(void) {
    /* Focus lost: clear all state, discard buffered events */
    if (s_focus_lost) {
        nt_input_clear_all_keys();
        nt_input_clear_all_pointers();
        s_focus_lost = false;
        s_key_count = 0;
        s_ptr_count = 0;
        s_wheel_count = 0;
        return;
    }

    /* Apply DPR at drain time — always uses the most current value */
    float dpr = g_nt_window.dpr;

    for (uint32_t i = 0; i < s_key_count; i++) {
        nt_input_set_key(s_key_buf[i].key, s_key_buf[i].down);
    }
    s_key_count = 0;

    for (uint32_t i = 0; i < s_ptr_count; i++) {
        float fx = (float)s_ptr_buf[i].x * dpr;
        float fy = (float)s_ptr_buf[i].y * dpr;
        if (s_ptr_buf[i].is_down) {
            nt_input_pointer_down(0, fx, fy, 1.0F, NT_POINTER_MOUSE, s_ptr_buf[i].buttons);
        } else {
            nt_input_pointer_move(0, fx, fy, 1.0F, NT_POINTER_MOUSE, s_ptr_buf[i].buttons);
        }
    }
    s_ptr_count = 0;

    for (uint32_t i = 0; i < s_wheel_count; i++) {
        nt_input_wheel(s_wheel_buf[i].dx, s_wheel_buf[i].dy);
    }
    s_wheel_count = 0;
}

void nt_input_platform_shutdown(void) {}
