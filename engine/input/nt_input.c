#include "input/nt_input_feed.h"

#include <string.h>

#include "core/nt_assert.h"

/* ---- Input state: the frame the game reads, and the next one events apply to ---- */

nt_input_t g_nt_input; /* published by nt_input_poll */
static uint32_t s_char_read;
static nt_input_t s_next; /* events apply here in arrival order */

static bool s_player_enabled = true;

/* A pointer button shows one direction change after a release per frame: a press that follows a
   release before the poll waits for the next frame, so a frame never reads a release together with
   a newer press. Bit b = button b. The frame of the release shows the pointer where the release
   happened; where it went since waits with the press. */
static uint8_t s_waiting_press[NT_INPUT_MAX_POINTERS];
static uint8_t s_waiting_down[NT_INPUT_MAX_POINTERS]; /* that press is still held */
static float s_waiting_pos[NT_INPUT_MAX_POINTERS][2];
static float s_release_pos[NT_INPUT_MAX_POINTERS][2]; /* the pointer at its last release edge */

/* ---- Pointer helpers ---- */

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

static void apply_buttons_mask(nt_pointer_t *ptr, uint8_t buttons_mask) {
    static const uint8_t masks[NT_BUTTON_MAX] = {1U, 2U, 4U};
    NT_ASSERT(buttons_mask <= 7U);
    const ptrdiff_t i = ptr - s_next.pointers;
    for (int b = 0; b < NT_BUTTON_MAX; b++) {
        bool now = (buttons_mask & masks[b]) != 0;
        if ((s_waiting_press[i] & masks[b]) != 0U || (now && ptr->buttons[b].is_released)) {
            if (s_waiting_press[i] == 0U) {
                /* Back to where the release happened, taking the hover since then out of the deltas. */
                s_waiting_pos[i][0] = ptr->x;
                s_waiting_pos[i][1] = ptr->y;
                ptr->dx -= ptr->x - s_release_pos[i][0];
                ptr->dy -= ptr->y - s_release_pos[i][1];
                ptr->x = s_release_pos[i][0];
                ptr->y = s_release_pos[i][1];
            }
            s_waiting_press[i] |= masks[b];
            s_waiting_down[i] = (uint8_t)(now ? (s_waiting_down[i] | masks[b]) : (s_waiting_down[i] & ~masks[b]));
            continue;
        }
        if (now && !ptr->buttons[b].is_down) {
            ptr->buttons[b].is_pressed = true;
        }
        if (!now && ptr->buttons[b].is_down) {
            ptr->buttons[b].is_released = true;
            s_release_pos[i][0] = ptr->x;
            s_release_pos[i][1] = ptr->y;
        }
        ptr->buttons[b].is_down = now;
    }
}

/* Moves a pointer; while a press waits, the new position waits with it. */
static void move_pointer(nt_pointer_t *ptr, float x, float y, bool delta) {
    const ptrdiff_t i = ptr - s_next.pointers;
    if (s_waiting_press[i] != 0U) {
        s_waiting_pos[i][0] = x;
        s_waiting_pos[i][1] = y;
        return;
    }
    if (delta) {
        ptr->dx += x - ptr->x;
        ptr->dy += y - ptr->y;
    }
    ptr->x = x;
    ptr->y = y;
}

/* Its release is published with it; it is gone the frame after. */
static void release_pointer(nt_pointer_t *ptr) {
    apply_buttons_mask(ptr, 0);
    ptr->pressure = 0.0F;
    ptr->deactivate_pending = true;
}

/* ---- Lifecycle ---- */

void nt_input_init(void) {
    memset(&g_nt_input, 0, sizeof(g_nt_input));
    memset(&s_next, 0, sizeof(s_next));
    memset(s_waiting_press, 0, sizeof(s_waiting_press));
    memset(s_waiting_down, 0, sizeof(s_waiting_down));
    s_char_read = 0;
    s_player_enabled = true;
}

void nt_input_shutdown(void) { nt_input_init(); }

static bool pointer_in_use(const nt_pointer_t *p) {
    for (int b = 0; b < NT_BUTTON_MAX; b++) {
        if (p->buttons[b].is_down || p->buttons[b].is_released) {
            return p->active;
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
    if (!in_use && s_next.pointers[0].active && !s_next.pointers[0].deactivate_pending) {
        s_next.primary = 0;
    }
}

void nt_input_poll(void) {
    primary_update();
    g_nt_input = s_next;
    s_char_read = 0;

    /* Edges, deltas and text are per frame; a released pointer was published once and is gone now. */
    memset(s_next.key_pressed, 0, sizeof(s_next.key_pressed));
    memset(s_next.key_released, 0, sizeof(s_next.key_released));
    s_next.char_count = 0;
    for (int i = 0; i < NT_INPUT_MAX_POINTERS; i++) {
        nt_pointer_t *p = &s_next.pointers[i];
        if (p->deactivate_pending && s_waiting_press[i] == 0U) { /* a waiting tap is published first */
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
            if (p->active && (s_waiting_press[i] & (1U << b)) != 0U) {
                p->buttons[b].is_pressed = true;
                p->buttons[b].is_down = (s_waiting_down[i] & (1U << b)) != 0U;
                p->buttons[b].is_released = !p->buttons[b].is_down; /* lifted again meanwhile: a whole tap */
                p->dx += s_waiting_pos[i][0] - p->x;                /* the move the wait held back */
                p->dy += s_waiting_pos[i][1] - p->y;
                p->x = s_waiting_pos[i][0];
                p->y = s_waiting_pos[i][1];
            }
        }
        s_waiting_press[i] = 0U;
        s_waiting_down[i] = 0U;
    }
}

/* ---- Queries (the published frame) ---- */

bool nt_input_key_is_down(nt_key_t key) { return key < NT_KEY_COUNT && g_nt_input.key_down[key]; }

bool nt_input_key_is_pressed(nt_key_t key) { return key < NT_KEY_COUNT && g_nt_input.key_pressed[key]; }

bool nt_input_key_is_released(nt_key_t key) { return key < NT_KEY_COUNT && g_nt_input.key_released[key]; }

bool nt_input_any_key_pressed(void) {
    for (int i = 0; i < NT_KEY_COUNT; i++) {
        if (g_nt_input.key_pressed[i]) {
            return true;
        }
    }
    return false;
}

bool nt_input_pop_char(uint32_t *out_codepoint) {
    if (s_char_read == g_nt_input.char_count) {
        return false; /* empty — leave *out untouched */
    }
    *out_codepoint = g_nt_input.chars[s_char_read++];
    return true;
}

bool nt_input_mouse_is_down(nt_button_t button) { return button < NT_BUTTON_MAX && g_nt_input.pointers[0].active && g_nt_input.pointers[0].buttons[button].is_down; }

bool nt_input_mouse_is_pressed(nt_button_t button) { return button < NT_BUTTON_MAX && g_nt_input.pointers[0].active && g_nt_input.pointers[0].buttons[button].is_pressed; }

bool nt_input_mouse_is_released(nt_button_t button) { return button < NT_BUTTON_MAX && g_nt_input.pointers[0].active && g_nt_input.pointers[0].buttons[button].is_released; }

/* ---- Applying an event to the next frame ---- */

static void apply_key(nt_key_t key, bool down) {
    NT_ASSERT(key < NT_KEY_COUNT);
    s_next.key_pressed[key] = s_next.key_pressed[key] || (down && !s_next.key_down[key]);
    s_next.key_released[key] = s_next.key_released[key] || (!down && s_next.key_down[key]);
    s_next.key_down[key] = down;
}

static void apply_mouse_move(float x, float y, uint8_t buttons) {
    nt_pointer_t *m = &s_next.pointers[0];
    const bool tracked = m->active && !m->deactivate_pending;
    if (!m->active) {
        *m = (nt_pointer_t){.active = true};
    }
    m->deactivate_pending = false; /* back before the leave took effect: keep this frame's edges */
    m->pressure = buttons != 0U ? 1.0F : 0.0F;
    move_pointer(m, x, y, tracked);
    apply_buttons_mask(m, buttons);
}

static void apply_contact_down(uint32_t id, float x, float y, float pressure, uint8_t type, uint8_t buttons) {
    NT_ASSERT((type == NT_POINTER_TOUCH || type == NT_POINTER_PEN) && "a contact is touch or pen; the mouse moves");
    nt_pointer_t *ptr = find_contact(id);
    if (ptr == NULL) {
        ptr = find_free_contact_slot();
        if (ptr == NULL) {
            return; /* All slots full */
        }
        *ptr = (nt_pointer_t){.id = id, .type = type, .active = true};
    }
    ptr->pressure = pressure;
    move_pointer(ptr, x, y, false);
    apply_buttons_mask(ptr, buttons);
}

static void apply_contact_move(uint32_t id, float x, float y, float pressure, uint8_t buttons) {
    nt_pointer_t *ptr = find_contact(id);
    if (ptr == NULL) {
        return; /* a contact exists only from its down to its up (a hovering pen has no slot) */
    }
    ptr->pressure = pressure;
    move_pointer(ptr, x, y, true);
    apply_buttons_mask(ptr, buttons);
}

static void apply_mouse_buttons(uint8_t buttons) {
    const nt_pointer_t *m = &s_next.pointers[0];
    NT_ASSERT(buttons <= 7U);
    if (m->active && !m->deactivate_pending) {         /* no press at (0,0) for a mouse that is not over the window */
        const bool waiting = s_waiting_press[0] != 0U; /* then the mouse is where the waiting press put it */
        apply_mouse_move(waiting ? s_waiting_pos[0][0] : m->x, waiting ? s_waiting_pos[0][1] : m->y, buttons);
    }
}

static void apply_mouse_leave(void) {
    nt_pointer_t *m = &s_next.pointers[0];
    for (int b = 0; b < NT_BUTTON_MAX; b++) {
        if (m->buttons[b].is_down || s_waiting_down[0] != 0U) {
            return; /* a drag outside keeps the mouse; the window pump leaves after the release */
        }
    }
    m->deactivate_pending = m->active;
}

static void apply_focus_lost(void) {
    for (int i = 0; i < NT_KEY_COUNT; i++) {
        apply_key((nt_key_t)i, false);
    }
    for (int i = 0; i < NT_INPUT_MAX_POINTERS; i++) {
        if (s_next.pointers[i].active) {
            release_pointer(&s_next.pointers[i]);
        }
    }
}

static void apply(const nt_input_event_t *e) {
    nt_pointer_t *m = &s_next.pointers[0];
    switch ((nt_input_event_kind_t)e->kind) {
    case NT_INPUT_EV_KEY:
        apply_key((nt_key_t)e->key.key, e->key.down);
        break;
    case NT_INPUT_EV_CHAR:
        if (s_next.char_count < NT_INPUT_CHAR_MAX) { /* drop-newest: earlier typing is never clobbered */
            s_next.chars[s_next.char_count++] = e->cp;
        }
        break;
    case NT_INPUT_EV_MOUSE_MOVE:
        apply_mouse_move(e->mouse.x, e->mouse.y, e->mouse.buttons);
        break;
    case NT_INPUT_EV_MOUSE_BUTTONS:
        apply_mouse_buttons(e->mouse.buttons);
        break;
    case NT_INPUT_EV_MOUSE_LEAVE:
        apply_mouse_leave();
        break;
    case NT_INPUT_EV_CONTACT_DOWN:
        apply_contact_down(e->contact.id, e->contact.x, e->contact.y, e->contact.pressure, e->contact.type, e->contact.buttons);
        break;
    case NT_INPUT_EV_CONTACT_MOVE:
        apply_contact_move(e->contact.id, e->contact.x, e->contact.y, e->contact.pressure, e->contact.buttons);
        break;
    case NT_INPUT_EV_CONTACT_UP: {
        nt_pointer_t *ptr = find_contact(e->contact.id);
        if (ptr != NULL) {
            release_pointer(ptr);
        }
        break;
    }
    case NT_INPUT_EV_WHEEL:
        if (m->active) { /* no mouse over the window: the window pump and the bot move it first */
            m->wheel_dx += e->wheel.dx;
            m->wheel_dy += e->wheel.dy;
        }
        break;
    case NT_INPUT_EV_FOCUS_LOST:
        apply_focus_lost();
        break;
    default:
        NT_ASSERT(false && "unknown input event kind");
        break;
    }
}

void nt_input_feed(nt_input_event_t e) {
    if (s_player_enabled) { /* closed: the held input is the bot's, and a click into another window must not cut it */
        apply(&e);
    }
}

void nt_input_inject(nt_input_event_t e) { apply(&e); }

void nt_input_set_player_enabled(bool enabled) { s_player_enabled = enabled; }
