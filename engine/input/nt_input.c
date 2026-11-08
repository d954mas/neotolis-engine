#include "input/nt_input_internal.h"

#include <string.h>

#include "core/nt_assert.h"

/* ---- Input state: the frame the game reads, and the next one events apply to ---- */

typedef struct {
    bool down[NT_KEY_COUNT];
    bool pressed[NT_KEY_COUNT];
    bool released[NT_KEY_COUNT];
} key_state_t;

typedef struct {
    uint32_t cps[NT_INPUT_CHAR_MAX];
    uint32_t count;
} char_list_t;

/* Published by nt_input_poll; read by the game. */
nt_input_t g_nt_input;
static key_state_t s_keys;
static char_list_t s_chars;
static uint32_t s_char_read;

/* Events apply here as they reach the core, in arrival order; nt_input_poll publishes it. */
static nt_input_t s_next;
static key_state_t s_next_keys;
static char_list_t s_next_chars;

/* Player gate: judges a real event when it reaches the core (devapi automation). */
static bool s_player_enabled = true;

/* ---- Internal pointer helpers ---- */

#define MOUSE (&s_next.pointers[0])

static nt_pointer_t *find_contact(uint32_t id) {
    for (int i = 1; i < NT_INPUT_MAX_POINTERS; i++) {
        /* a lifted contact is gone: a new down with its id takes another slot, so its release edge stays */
        if (s_next.pointers[i].active && !s_next.pointers[i].deactivate_pending && s_next.pointers[i].id == id) {
            return &s_next.pointers[i];
        }
    }
    return NULL;
}

static nt_pointer_t *find_free_contact_slot(void) {
    for (int i = 1; i < NT_INPUT_MAX_POINTERS; i++) {
        if (!s_next.pointers[i].active) {
            return &s_next.pointers[i];
        }
    }
    return NULL;
}

static void reset_pointer_transients(nt_pointer_t *ptr) {
    ptr->dx = 0.0F;
    ptr->dy = 0.0F;
    ptr->wheel_dx = 0.0F;
    ptr->wheel_dy = 0.0F;
    ptr->deactivate_pending = false;
    memset(ptr->buttons, 0, sizeof(ptr->buttons));
}

static void apply_buttons_mask(nt_pointer_t *ptr, uint8_t buttons_mask) {
    static const uint8_t masks[NT_BUTTON_MAX] = {1U, 2U, 4U};
    for (int b = 0; b < NT_BUTTON_MAX; b++) {
        bool now = (buttons_mask & masks[b]) != 0;
        if (now && !ptr->buttons[b].is_down) {
            ptr->buttons[b].is_pressed = true;
        }
        if (!now && ptr->buttons[b].is_down) {
            ptr->buttons[b].is_released = true;
        }
        ptr->buttons[b].is_down = now;
    }
}

/* ---- Lifecycle ---- */

static void reset_state(void) {
    memset(&g_nt_input, 0, sizeof(g_nt_input));
    memset(&s_next, 0, sizeof(s_next));
    memset(&s_keys, 0, sizeof(s_keys));
    memset(&s_next_keys, 0, sizeof(s_next_keys));
    s_chars.count = 0;
    s_next_chars.count = 0;
    s_char_read = 0;
    s_player_enabled = true; /* clean default: real devices drive until a bot gates them */
}

void nt_input_init(void) { reset_state(); }

static bool pointer_in_use(const nt_pointer_t *p) {
    for (int b = 0; b < NT_BUTTON_MAX; b++) {
        if (p->active && (p->buttons[b].is_down || p->buttons[b].is_released)) {
            return true;
        }
    }
    return false;
}

/* A single-pointer consumer never sees a press or a release it did not get, and a held capture never
   moves to another device. Contacts are always held, so only the mouse can take it by hovering. */
static void primary_update(void) {
    if (pointer_in_use(&s_next.pointers[s_next.primary])) {
        return;
    }
    bool in_use = false;
    for (int i = 0; i < NT_INPUT_MAX_POINTERS; i++) {
        for (int b = 0; b < NT_BUTTON_MAX; b++) {
            if (s_next.pointers[i].active && s_next.pointers[i].buttons[b].is_pressed) {
                s_next.primary = (uint8_t)i;
                return;
            }
        }
        in_use = in_use || pointer_in_use(&s_next.pointers[i]);
    }
    if (!in_use && MOUSE->active && !MOUSE->deactivate_pending) {
        s_next.primary = 0;
    }
}

void nt_input_poll(void) {
    primary_update();
    g_nt_input = s_next;
    s_keys = s_next_keys;
    s_chars = s_next_chars;
    s_char_read = 0;

    /* Start the next frame: edges, deltas and typed text are per frame; a released pointer was
       published once with its release and is gone now. */
    memset(s_next_keys.pressed, 0, sizeof(s_next_keys.pressed));
    memset(s_next_keys.released, 0, sizeof(s_next_keys.released));
    s_next_chars.count = 0;
    for (int i = 0; i < NT_INPUT_MAX_POINTERS; i++) {
        nt_pointer_t *p = &s_next.pointers[i];
        if (p->deactivate_pending) {
            p->active = false;
            p->deactivate_pending = false;
        }
        p->dx = 0.0F;
        p->dy = 0.0F;
        p->wheel_dx = 0.0F;
        p->wheel_dy = 0.0F;
        for (int b = 0; b < NT_BUTTON_MAX; b++) {
            p->buttons[b].is_pressed = false;
            p->buttons[b].is_released = false;
        }
    }
}

void nt_input_shutdown(void) {
    /* Symmetric with nt_input_init: leave NO observable state (stale key edges or a disabled
       gate) for a consumer that queries after shutdown or relies on it to drop a bot gate. */
    reset_state();
}

/* ---- Key query functions ---- */

bool nt_input_key_is_down(nt_key_t key) {
    if (key >= NT_KEY_COUNT) {
        return false;
    }
    return s_keys.down[key];
}

bool nt_input_key_is_pressed(nt_key_t key) {
    if (key >= NT_KEY_COUNT) {
        return false;
    }
    return s_keys.pressed[key];
}

bool nt_input_key_is_released(nt_key_t key) {
    if (key >= NT_KEY_COUNT) {
        return false;
    }
    return s_keys.released[key];
}

bool nt_input_any_key_pressed(void) {
    for (int i = 0; i < NT_KEY_COUNT; i++) {
        if (s_keys.pressed[i]) {
            return true;
        }
    }
    return false;
}

bool nt_input_pop_char(uint32_t *out_codepoint) {
    if (s_char_read == s_chars.count) {
        return false; /* empty — leave *out untouched */
    }
    *out_codepoint = s_chars.cps[s_char_read++];
    return true;
}

/* ---- Mouse convenience helpers ---- */

static const nt_pointer_t *published_mouse(nt_button_t button) {
    const nt_pointer_t *m = &g_nt_input.pointers[0];
    return button < NT_BUTTON_MAX && m->active ? m : NULL;
}

bool nt_input_mouse_is_down(nt_button_t button) {
    const nt_pointer_t *m = published_mouse(button);
    return m != NULL && m->buttons[button].is_down;
}

bool nt_input_mouse_is_pressed(nt_button_t button) {
    const nt_pointer_t *m = published_mouse(button);
    return m != NULL && m->buttons[button].is_pressed;
}

bool nt_input_mouse_is_released(nt_button_t button) {
    const nt_pointer_t *m = published_mouse(button);
    return m != NULL && m->buttons[button].is_released;
}

/* ---- Apply cores and the gated real-input wrappers (called by platform backends) ---- */

static void nt_input_set_key_apply(nt_key_t key, bool down) {
    NT_ASSERT(key < NT_KEY_COUNT);
    if (down && !s_next_keys.down[key]) {
        s_next_keys.pressed[key] = true;
    }
    if (!down && s_next_keys.down[key]) {
        s_next_keys.released[key] = true;
    }
    s_next_keys.down[key] = down;
}

void nt_input_set_key(nt_key_t key, bool down) {
    if (!s_player_enabled) {
        return; /* gated: real device suppressed while a bot drives (inject bypasses via *_apply). */
    }
    nt_input_set_key_apply(key, down);
}

static void nt_input_char_apply(uint32_t cp) {
    if (s_next_chars.count < NT_INPUT_CHAR_MAX) { /* drop-newest: earlier typing is never clobbered */
        s_next_chars.cps[s_next_chars.count++] = cp;
    }
}

void nt_input_char(uint32_t cp) {
    if (!s_player_enabled) {
        return;
    }
    nt_input_char_apply(cp);
}

static void nt_input_mouse_move_apply(float x, float y, uint8_t buttons_mask) {
    if (!MOUSE->active) {
        reset_pointer_transients(MOUSE);
        MOUSE->active = true;
    } else if (!MOUSE->deactivate_pending) {
        MOUSE->dx += x - MOUSE->x;
        MOUSE->dy += y - MOUSE->y;
    }
    MOUSE->deactivate_pending = false; /* back before the leave took effect: keep this frame's edges */
    MOUSE->x = x;
    MOUSE->y = y;
    MOUSE->pressure = buttons_mask != 0U ? 1.0F : 0.0F;
    apply_buttons_mask(MOUSE, buttons_mask);
}

void nt_input_mouse_move(float x, float y, uint8_t buttons_mask) {
    if (!s_player_enabled) {
        return;
    }
    nt_input_mouse_move_apply(x, y, buttons_mask);
}

void nt_input_mouse_leave(void) {
    if (!s_player_enabled) {
        return;
    }
    for (int b = 0; b < NT_BUTTON_MAX; b++) {
        if (MOUSE->buttons[b].is_down) {
            return; /* a drag outside the window keeps the mouse; the backend leaves after the release */
        }
    }
    MOUSE->deactivate_pending = MOUSE->active;
}

static void nt_input_pointer_down_apply(uint32_t id, float x, float y, float pressure, uint8_t type, uint8_t buttons_mask) {
    NT_ASSERT((type == NT_POINTER_TOUCH || type == NT_POINTER_PEN) && "a contact is touch or pen; the mouse uses nt_input_mouse_move");
    nt_pointer_t *ptr = find_contact(id);
    bool fresh = (ptr == NULL);
    if (fresh) {
        ptr = find_free_contact_slot();
        if (ptr == NULL) {
            return; /* All slots full */
        }
        ptr->active = true;
        ptr->id = id;
        ptr->type = type;
        reset_pointer_transients(ptr);
    }
    ptr->pressure = pressure;
    ptr->x = x;
    ptr->y = y;
    apply_buttons_mask(ptr, buttons_mask);
}

void nt_input_pointer_down(uint32_t id, float x, float y, float pressure, uint8_t type, uint8_t buttons_mask) {
    if (!s_player_enabled) {
        return;
    }
    nt_input_pointer_down_apply(id, x, y, pressure, type, buttons_mask);
}

static void nt_input_pointer_move_apply(uint32_t id, float x, float y, float pressure, uint8_t buttons_mask) {
    nt_pointer_t *ptr = find_contact(id);
    if (ptr == NULL) {
        return; /* a contact exists only from its down to its up (a hovering pen has no slot) */
    }
    ptr->dx += x - ptr->x;
    ptr->dy += y - ptr->y;
    ptr->x = x;
    ptr->y = y;
    ptr->pressure = pressure;
    apply_buttons_mask(ptr, buttons_mask);
}

void nt_input_pointer_move(uint32_t id, float x, float y, float pressure, uint8_t buttons_mask) {
    if (!s_player_enabled) {
        return;
    }
    nt_input_pointer_move_apply(id, x, y, pressure, buttons_mask);
}

static void nt_input_pointer_up_apply(uint32_t id) {
    nt_pointer_t *ptr = find_contact(id);
    if (ptr == NULL) {
        return; /* Unknown pointer */
    }
    for (int b = 0; b < NT_BUTTON_MAX; b++) {
        if (ptr->buttons[b].is_down) {
            ptr->buttons[b].is_released = true;
        }
        ptr->buttons[b].is_down = false;
    }
    ptr->deactivate_pending = true;
}

void nt_input_pointer_up(uint32_t id) {
    if (!s_player_enabled) {
        return;
    }
    nt_input_pointer_up_apply(id);
}

static void nt_input_wheel_apply(float dx, float dy) {
    if (!MOUSE->active) {
        return; /* no mouse over the window: the backends and the bot move it first */
    }
    MOUSE->wheel_dx += dx;
    MOUSE->wheel_dy += dy;
}

void nt_input_wheel(float dx, float dy) {
    if (!s_player_enabled) {
        return;
    }
    nt_input_wheel_apply(dx, dy);
}

void nt_input_focus_lost(void) {
    if (!s_player_enabled) {
        return; /* gated: the held input is the bot's, and a click into another window must not cut it */
    }
    for (int i = 0; i < NT_KEY_COUNT; i++) {
        s_next_keys.released[i] = s_next_keys.released[i] || s_next_keys.down[i];
    }
    memset(s_next_keys.down, 0, sizeof(s_next_keys.down));
    /* Like a pointer up: the release is published with the pointer, which is gone the frame after. */
    for (int i = 0; i < NT_INPUT_MAX_POINTERS; i++) {
        nt_pointer_t *p = &s_next.pointers[i];
        if (!p->active) {
            continue;
        }
        for (int b = 0; b < NT_BUTTON_MAX; b++) {
            p->buttons[b].is_released = p->buttons[b].is_released || p->buttons[b].is_down;
            p->buttons[b].is_down = false;
        }
        p->pressure = 0.0F;
        p->deactivate_pending = true;
    }
}

/* ---- Automation: player gate + synthetic input injection (devapi) ---- */

void nt_input_set_player_enabled(bool enabled) { s_player_enabled = enabled; }

/* Inject applies through the *_apply cores, past the player gate: injected input is
   indistinguishable from human to the query API. The devapi layer validated the untrusted command. */

void nt_input_inject_key(nt_key_t key, bool down) { nt_input_set_key_apply(key, down); }

void nt_input_inject_mouse(float x, float y, uint8_t buttons_mask) {
    NT_ASSERT(buttons_mask <= 7U);
    nt_input_mouse_move_apply(x, y, buttons_mask);
}

void nt_input_inject_mouse_buttons(uint8_t buttons_mask) {
    NT_ASSERT(buttons_mask <= 7U);
    if (MOUSE->active && !MOUSE->deactivate_pending) { /* at its current position; no mouse over the window -> no press at (0,0) */
        nt_input_mouse_move_apply(MOUSE->x, MOUSE->y, buttons_mask);
    }
}

void nt_input_inject_pointer_down(uint32_t id, float x, float y, float pressure, uint8_t type, uint8_t buttons_mask) {
    NT_ASSERT(buttons_mask <= 7U);
    nt_input_pointer_down_apply(id, x, y, pressure, type, buttons_mask);
}

void nt_input_inject_pointer_move(uint32_t id, float x, float y, float pressure, uint8_t buttons_mask) {
    NT_ASSERT(buttons_mask <= 7U);
    nt_input_pointer_move_apply(id, x, y, pressure, buttons_mask);
}

void nt_input_inject_pointer_up(uint32_t id) { nt_input_pointer_up_apply(id); }

void nt_input_inject_wheel(float dx, float dy) { nt_input_wheel_apply(dx, dy); }

void nt_input_inject_char(uint32_t cp) { nt_input_char_apply(cp); }
