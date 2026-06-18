#include "input/nt_input_internal.h"

#include <string.h>

/* ---- Global input state ---- */

nt_input_t g_nt_input;

/* ---- File-scope statics for edge detection ---- */

static bool s_keys_current[NT_KEY_COUNT];
static bool s_keys_pressed[NT_KEY_COUNT];
static bool s_keys_released[NT_KEY_COUNT];

/* ---- UTF-32 char ring (typed text, fed by platform char sources) ---- */

#define NT_INPUT_CHAR_RING 32 /* power-of-2: one frame's typing < 32 */

static uint32_t s_char_ring[NT_INPUT_CHAR_RING];
static uint32_t s_char_head; /* next write slot */
static uint32_t s_char_tail; /* next read slot */

/* ---- Player gate ---- */

/* Gate at the apply seam: covers native drain + web direct apply. */
static bool s_player_enabled = true;

/* ---- Frame-countdown seam (relative; advances only when frame changes) ---- */

static uint32_t s_last_poll_frame;
static bool s_have_last_frame;

/* ---- Synthetic inject queue (bounded BSS; frame-scheduled) ---- */

typedef struct {
    uint16_t frames_remaining; /* sim-advance countdown; 0 = apply this poll */
    uint8_t kind;              /* nt_inject_kind_t */
    union {
        struct {
            uint8_t key; /* nt_key_t fits in u8 (COUNT=69) */
            bool down;
        } key;
        struct {
            uint32_t id;
            float x, y, pressure;
            uint8_t type;
            uint8_t buttons_mask;
        } pointer; /* down / move */
        struct {
            uint32_t id;
        } pointer_up;
        struct {
            float dx, dy;
        } wheel;
        struct {
            uint32_t cp;
        } chr;
    } u;
} nt_inject_event_t;

static nt_inject_event_t s_inject_queue[NT_INPUT_INJECT_QUEUE_MAX];
static uint32_t s_inject_count;

static void inject_drain(bool advanced);

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
    s_char_head = 0;
    s_char_tail = 0;
    s_last_poll_frame = 0;
    s_have_last_frame = false;
    s_inject_count = 0;
    s_player_enabled = true; /* clean default: real devices drive until a bot gates them */
    nt_input_platform_init();
}

void nt_input_poll(uint32_t frame) {
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
    /* Typed text is frame-local like key edges: drop any chars unconsumed last frame so they can't
     * leak into a field focused later. platform_poll below refills the ring with this frame's chars
     * -- BOTH backends stage chars and drain them in platform_poll (web: _ntCharBuf; native:
     * s_char_buf via nt_input_buffer_char_event), so a same-frame typed char survives this clear. */
    s_char_tail = s_char_head;
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

    /* Relative frame countdown: only a NEW sim-advance frame ticks the inject queue,
       so PAUSE / MANUAL-idle (same frame re-polled) freeze it. Inject drains in the same
       post-clear window as the native char drain. */
    bool advanced = !s_have_last_frame || (frame != s_last_poll_frame);
    inject_drain(advanced);
    s_last_poll_frame = frame;
    s_have_last_frame = true;
}

void nt_input_shutdown(void) {
    nt_input_platform_shutdown();
    /* Symmetric with nt_input_init: leave NO observable state (stale key edges or a disabled
       gate) for a consumer that queries after shutdown or relies on it to drop a bot gate. */
    memset(&g_nt_input, 0, sizeof(g_nt_input));
    memset(s_keys_current, 0, sizeof(s_keys_current));
    memset(s_keys_pressed, 0, sizeof(s_keys_pressed));
    memset(s_keys_released, 0, sizeof(s_keys_released));
    s_char_head = 0;
    s_char_tail = 0;
    s_last_poll_frame = 0;
    s_have_last_frame = false;
    s_inject_count = 0;
    s_player_enabled = true;
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

/* ---- string -> nt_key_t name table ---- */

/* Indexed by nt_key_t; each string = the enum identifier minus the NT_KEY_ prefix. The
   _Static_assert is the drift guard: adding an enum value without a name breaks the build. */
static const char *const k_key_names[NT_KEY_COUNT] = {
    "A",        "B",          "C",          "D",           "E",     "F",     "G",      "H",   "I",         "J",      "K",      "L",     "M",     "N",       "O",         "P",  "Q",  "R",
    "S",        "T",          "U",          "V",           "W",     "X",     "Y",      "Z",   "0",         "1",      "2",      "3",     "4",     "5",       "6",         "7",  "8",  "9",
    "ARROW_UP", "ARROW_DOWN", "ARROW_LEFT", "ARROW_RIGHT", "SPACE", "ENTER", "ESCAPE", "TAB", "BACKSPACE", "LSHIFT", "RSHIFT", "LCTRL", "RCTRL", "LALT",    "RALT",      "F1", "F2", "F3",
    "F4",       "F5",         "F6",         "F7",          "F8",    "F9",    "F10",    "F11", "F12",       "DELETE", "INSERT", "HOME",  "END",   "PAGE_UP", "PAGE_DOWN",
};
_Static_assert(sizeof(k_key_names) / sizeof(k_key_names[0]) == NT_KEY_COUNT, "key-name table must match nt_key_t");

bool nt_input_key_from_name(const char *name, nt_key_t *out) {
    if (name == NULL || out == NULL) {
        return false;
    }
    for (int i = 0; i < NT_KEY_COUNT; i++) {
        if (strcmp(name, k_key_names[i]) == 0) {
            *out = (nt_key_t)i;
            return true;
        }
    }
    return false;
}

bool nt_input_pop_char(uint32_t *out_codepoint) {
    if (s_char_tail == s_char_head) {
        return false; /* empty — leave *out untouched */
    }
    *out_codepoint = s_char_ring[s_char_tail & (NT_INPUT_CHAR_RING - 1U)];
    s_char_tail++;
    return true;
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

static void nt_input_set_key_apply(nt_key_t key, bool down) {
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

void nt_input_set_key(nt_key_t key, bool down) {
    if (!s_player_enabled) {
        return;
    }
    nt_input_set_key_apply(key, down);
}

static void nt_input_buffer_char_apply(uint32_t cp) {
    /* Drop when full so unread chars are never clobbered. */
    if (s_char_head - s_char_tail >= NT_INPUT_CHAR_RING) {
        return;
    }
    s_char_ring[s_char_head & (NT_INPUT_CHAR_RING - 1U)] = cp;
    s_char_head++;
}

void nt_input_buffer_char(uint32_t cp) {
    if (!s_player_enabled) {
        return;
    }
    nt_input_buffer_char_apply(cp);
}

static void nt_input_pointer_down_apply(uint32_t id, float x, float y, float pressure, uint8_t type, uint8_t buttons_mask) {
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

void nt_input_pointer_down(uint32_t id, float x, float y, float pressure, uint8_t type, uint8_t buttons_mask) {
    if (!s_player_enabled) {
        return;
    }
    nt_input_pointer_down_apply(id, x, y, pressure, type, buttons_mask);
}

static void nt_input_pointer_move_apply(uint32_t id, float x, float y, float pressure, uint8_t type, uint8_t buttons_mask) {
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

void nt_input_pointer_move(uint32_t id, float x, float y, float pressure, uint8_t type, uint8_t buttons_mask) {
    if (!s_player_enabled) {
        return;
    }
    nt_input_pointer_move_apply(id, x, y, pressure, type, buttons_mask);
}

static void nt_input_pointer_up_apply(uint32_t id) {
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

void nt_input_pointer_up(uint32_t id) {
    if (!s_player_enabled) {
        return;
    }
    nt_input_pointer_up_apply(id);
}

static void nt_input_wheel_apply(float dx, float dy) {
    nt_pointer_t *mouse = find_mouse_pointer();
    if (mouse == NULL) {
        return; /* No mouse slot yet — wheel before first move is lost */
    }
    mouse->wheel_dx += dx;
    mouse->wheel_dy += dy;
}

void nt_input_wheel(float dx, float dy) {
    if (!s_player_enabled) {
        return;
    }
    nt_input_wheel_apply(dx, dy);
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
    /* Mirror pointer_up_apply: raise the release edge then DEFER deactivation one frame so the
       release is readable via nt_input_mouse_is_released this frame (find_mouse_pointer only scans
       active slots). The next poll resolves deactivate_pending -> slot deactivates, no leak. */
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
        g_nt_input.pointers[i].deactivate_pending = true;
    }
}

void nt_input_set_player_enabled(bool enabled) {
    /* ON->OFF edge: release held real input so no key/button sticks down (focus-lost reuse). */
    if (s_player_enabled && !enabled) {
        nt_input_clear_all_keys();
        nt_input_clear_all_pointers();
    }
    s_player_enabled = enabled;
}

/* ---- Synthetic input injection ---- */

/* Whole-or-nothing reserve: a command needing N entries either gets all N or none -- a partial
   enqueue would leave a stuck key-down or a drag with no release. Returns the first reserved
   slot, or NULL on overflow. */
static nt_inject_event_t *inject_reserve(uint32_t n) {
    if (n > NT_INPUT_INJECT_QUEUE_MAX - s_inject_count) {
        return NULL; /* would overflow -- reject the whole command, write nothing */
    }
    nt_inject_event_t *first = &s_inject_queue[s_inject_count];
    s_inject_count += n;
    return first;
}

bool nt_input_inject_can_reserve(uint32_t n) { return n <= NT_INPUT_INJECT_QUEUE_MAX - s_inject_count; /* mirrors inject_reserve's capacity test */ }

bool nt_input_inject_key(nt_key_t key, bool down, uint16_t at_frame) {
    nt_inject_event_t *e = inject_reserve(1);
    if (e == NULL) {
        return false;
    }
    e->frames_remaining = at_frame;
    e->kind = NT_INJECT_KEY;
    e->u.key.key = (uint8_t)key;
    e->u.key.down = down;
    return true;
}

bool nt_input_inject_key_tap(nt_key_t key, uint16_t hold_frames) {
    nt_inject_event_t *e = inject_reserve(2); /* atomic down@0 + up@hold */
    if (e == NULL) {
        return false;
    }
    e[0].frames_remaining = 0;
    e[0].kind = NT_INJECT_KEY;
    e[0].u.key.key = (uint8_t)key;
    e[0].u.key.down = true;
    e[1].frames_remaining = hold_frames;
    e[1].kind = NT_INJECT_KEY;
    e[1].u.key.key = (uint8_t)key;
    e[1].u.key.down = false;
    return true;
}

bool nt_input_inject_pointer(nt_inject_kind_t kind, uint32_t id, float x, float y, float pressure, uint8_t type, uint8_t buttons_mask, uint16_t at_frame) {
    nt_inject_event_t *e = inject_reserve(1);
    if (e == NULL) {
        return false;
    }
    e->frames_remaining = at_frame;
    e->kind = (uint8_t)kind;
    if (kind == NT_INJECT_POINTER_UP) {
        e->u.pointer_up.id = id;
    } else {
        e->u.pointer.id = id;
        e->u.pointer.x = x;
        e->u.pointer.y = y;
        e->u.pointer.pressure = pressure;
        e->u.pointer.type = type;
        e->u.pointer.buttons_mask = buttons_mask;
    }
    return true;
}

bool nt_input_inject_wheel(float dx, float dy, uint16_t at_frame) {
    nt_inject_event_t *e = inject_reserve(1);
    if (e == NULL) {
        return false;
    }
    e->frames_remaining = at_frame;
    e->kind = NT_INJECT_WHEEL;
    e->u.wheel.dx = dx;
    e->u.wheel.dy = dy;
    return true;
}

bool nt_input_inject_text(const uint32_t *cps, uint32_t n) {
    if (cps == NULL || n == 0) {
        return false;
    }
    /* Every CHAR event is scheduled at frames_remaining=0, so all n drain in a SINGLE poll into
       the 32-slot char ring; nt_input_buffer_char_apply drops once full. Reject n beyond the ring
       so queued never lies about what lands (whole-or-nothing by codepoint count). */
    if (n > NT_INPUT_CHAR_RING) {
        return false;
    }
    nt_inject_event_t *e = inject_reserve(n); /* whole-or-nothing by codepoint count */
    if (e == NULL) {
        return false;
    }
    for (uint32_t i = 0; i < n; i++) {
        e[i].frames_remaining = 0;
        e[i].kind = NT_INJECT_CHAR;
        e[i].u.chr.cp = cps[i];
    }
    return true;
}

static void inject_apply_one(const nt_inject_event_t *e) {
    /* Apply through the *_apply cores so inject always flows past the player gate: injected input
       is indistinguishable from human to the query API. */
    switch ((nt_inject_kind_t)e->kind) {
    case NT_INJECT_KEY:
        nt_input_set_key_apply((nt_key_t)e->u.key.key, e->u.key.down);
        break;
    case NT_INJECT_POINTER_DOWN:
        nt_input_pointer_down_apply(e->u.pointer.id, e->u.pointer.x, e->u.pointer.y, e->u.pointer.pressure, e->u.pointer.type, e->u.pointer.buttons_mask);
        break;
    case NT_INJECT_POINTER_MOVE:
        nt_input_pointer_move_apply(e->u.pointer.id, e->u.pointer.x, e->u.pointer.y, e->u.pointer.pressure, e->u.pointer.type, e->u.pointer.buttons_mask);
        break;
    case NT_INJECT_POINTER_UP:
        nt_input_pointer_up_apply(e->u.pointer_up.id);
        break;
    case NT_INJECT_WHEEL:
        nt_input_wheel_apply(e->u.wheel.dx, e->u.wheel.dy);
        break;
    case NT_INJECT_CHAR:
        nt_input_buffer_char_apply(e->u.chr.cp);
        break;
    }
}

/* Apply every entry whose countdown hit 0 (in enqueue order = cross-command FIFO), compacting
   them out; survivors decrement by 1 only on a real sim-advance (advanced). */
static void inject_drain(bool advanced) {
    uint32_t out = 0;
    for (uint32_t i = 0; i < s_inject_count; i++) {
        if (s_inject_queue[i].frames_remaining == 0) {
            inject_apply_one(&s_inject_queue[i]);
            continue; /* applied -- drop it (compact) */
        }
        if (advanced) {
            s_inject_queue[i].frames_remaining--;
        }
        s_inject_queue[out++] = s_inject_queue[i];
    }
    s_inject_count = out;
}
