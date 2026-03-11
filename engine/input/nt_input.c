#include "input/nt_input_internal.h"

#include <string.h>

/* ---- Global input state ---- */

nt_input_t g_nt_input;

/* ---- File-scope statics for edge detection ---- */

static bool s_keys_current[NT_KEY_COUNT];
static bool s_keys_pressed[NT_KEY_COUNT];
static bool s_keys_released[NT_KEY_COUNT];

/* ---- Internal pointer helpers ---- */

static nt_pointer_t *find_pointer_by_id(uint32_t id) {
    for (int i = 0; i < NT_INPUT_MAX_POINTERS; i++) {
        if (g_nt_input.pointers[i].active && g_nt_input.pointers[i].id == id) {
            return &g_nt_input.pointers[i];
        }
    }
    return NULL;
}

static nt_pointer_t *find_free_pointer_slot(void) {
    for (int i = 0; i < NT_INPUT_MAX_POINTERS; i++) {
        if (!g_nt_input.pointers[i].active) {
            return &g_nt_input.pointers[i];
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

void nt_input_init(void) {
    memset(&g_nt_input, 0, sizeof(g_nt_input));
    memset(s_keys_current, 0, sizeof(s_keys_current));
    memset(s_keys_pressed, 0, sizeof(s_keys_pressed));
    memset(s_keys_released, 0, sizeof(s_keys_released));
    nt_input_platform_init();
}

void nt_input_poll(void) {
    /* Deactivate pointers that had pointer_up last frame */
    for (int i = 0; i < NT_INPUT_MAX_POINTERS; i++) {
        if (g_nt_input.pointers[i].deactivate_pending) {
            g_nt_input.pointers[i].active = false;
            g_nt_input.pointers[i].deactivate_pending = false;
        }
    }

    /* Clear edge flags accumulated since last poll */
    memset(s_keys_pressed, 0, sizeof(s_keys_pressed));
    memset(s_keys_released, 0, sizeof(s_keys_released));
    for (int i = 0; i < NT_INPUT_MAX_POINTERS; i++) {
        g_nt_input.pointers[i].dx = 0.0F;
        g_nt_input.pointers[i].dy = 0.0F;
        g_nt_input.pointers[i].wheel_dx = 0.0F;
        g_nt_input.pointers[i].wheel_dy = 0.0F;
        for (int b = 0; b < NT_BUTTON_MAX; b++) {
            g_nt_input.pointers[i].buttons[b].is_pressed = false;
            g_nt_input.pointers[i].buttons[b].is_released = false;
        }
    }

    /* Platform backend delivers events (calls set_key, pointer_down/move/up,
       which set edge flags immediately) */
    nt_input_platform_poll();
}

void nt_input_shutdown(void) {
    nt_input_platform_shutdown();
    memset(&g_nt_input, 0, sizeof(g_nt_input));
}

/* ---- Key query functions ---- */

bool nt_input_key_is_down(nt_key_t key) {
    if (key >= NT_KEY_COUNT) {
        return false;
    }
    return s_keys_current[key];
}

bool nt_input_key_is_pressed(nt_key_t key) {
    if (key >= NT_KEY_COUNT) {
        return false;
    }
    return s_keys_pressed[key];
}

bool nt_input_key_is_released(nt_key_t key) {
    if (key >= NT_KEY_COUNT) {
        return false;
    }
    return s_keys_released[key];
}

bool nt_input_any_key_pressed(void) {
    for (int i = 0; i < NT_KEY_COUNT; i++) {
        if (s_keys_pressed[i]) {
            return true;
        }
    }
    return false;
}

/* ---- Mouse convenience helpers ---- */

static nt_pointer_t *find_mouse_pointer(void) {
    for (int i = 0; i < NT_INPUT_MAX_POINTERS; i++) {
        if (g_nt_input.pointers[i].active && g_nt_input.pointers[i].type == NT_POINTER_MOUSE) {
            return &g_nt_input.pointers[i];
        }
    }
    return NULL;
}

bool nt_input_mouse_is_down(nt_button_t button) {
    if (button >= NT_BUTTON_MAX) {
        return false;
    }
    nt_pointer_t *mouse = find_mouse_pointer();
    if (mouse == NULL) {
        return false;
    }
    return mouse->buttons[button].is_down;
}

bool nt_input_mouse_is_pressed(nt_button_t button) {
    if (button >= NT_BUTTON_MAX) {
        return false;
    }
    nt_pointer_t *mouse = find_mouse_pointer();
    if (mouse == NULL) {
        return false;
    }
    return mouse->buttons[button].is_pressed;
}

bool nt_input_mouse_is_released(nt_button_t button) {
    if (button >= NT_BUTTON_MAX) {
        return false;
    }
    nt_pointer_t *mouse = find_mouse_pointer();
    if (mouse == NULL) {
        return false;
    }
    return mouse->buttons[button].is_released;
}

/* ---- Internal helpers (called by platform backends) ---- */

void nt_input_set_key(nt_key_t key, bool down) {
    if (key >= NT_KEY_COUNT) {
        return;
    }
    if (down && !s_keys_current[key]) {
        s_keys_pressed[key] = true;
    }
    if (!down && s_keys_current[key]) {
        s_keys_released[key] = true;
    }
    s_keys_current[key] = down;
}

void nt_input_pointer_down(uint32_t id, float x, float y, float pressure, uint8_t type, uint8_t buttons_mask) {
    nt_pointer_t *ptr = find_pointer_by_id(id);
    bool fresh = (ptr == NULL);
    if (fresh) {
        ptr = find_free_pointer_slot();
        if (ptr == NULL) {
            return; /* All slots full */
        }
        ptr->active = true;
        ptr->id = id;
        ptr->type = type;
    }
    if (fresh || ptr->deactivate_pending) {
        reset_pointer_transients(ptr);
    }
    ptr->pressure = pressure;
    ptr->x = x;
    ptr->y = y;
    apply_buttons_mask(ptr, buttons_mask);
}

void nt_input_pointer_move(uint32_t id, float x, float y, float pressure, uint8_t type, uint8_t buttons_mask) {
    nt_pointer_t *ptr = find_pointer_by_id(id);
    bool fresh = (ptr == NULL) || ptr->deactivate_pending;
    if (ptr == NULL) {
        /* Auto-create on first hover (mouse enters canvas before click) */
        ptr = find_free_pointer_slot();
        if (ptr == NULL) {
            return;
        }
        ptr->active = true;
        ptr->id = id;
        ptr->type = type;
    }
    if (fresh) {
        reset_pointer_transients(ptr);
    } else {
        ptr->dx += x - ptr->x;
        ptr->dy += y - ptr->y;
    }
    ptr->x = x;
    ptr->y = y;
    ptr->pressure = pressure;
    apply_buttons_mask(ptr, buttons_mask);
}

void nt_input_pointer_up(uint32_t id) {
    nt_pointer_t *ptr = find_pointer_by_id(id);
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

void nt_input_wheel(float dx, float dy) {
    nt_pointer_t *mouse = find_mouse_pointer();
    if (mouse == NULL) {
        return; /* No mouse slot yet — wheel before first move is lost */
    }
    mouse->wheel_dx += dx;
    mouse->wheel_dy += dy;
}

void nt_input_clear_all_keys(void) {
    for (int i = 0; i < NT_KEY_COUNT; i++) {
        if (s_keys_current[i]) {
            s_keys_released[i] = true;
        }
    }
    memset(s_keys_current, 0, sizeof(s_keys_current));
}

void nt_input_clear_all_pointers(void) {
    for (int i = 0; i < NT_INPUT_MAX_POINTERS; i++) {
        if (!g_nt_input.pointers[i].active) {
            continue;
        }
        for (int b = 0; b < NT_BUTTON_MAX; b++) {
            if (g_nt_input.pointers[i].buttons[b].is_down) {
                g_nt_input.pointers[i].buttons[b].is_released = true;
            }
            g_nt_input.pointers[i].buttons[b].is_down = false;
        }
        g_nt_input.pointers[i].active = false;
        g_nt_input.pointers[i].deactivate_pending = false;
    }
}
