#include "input/nt_input_internal.h"

#include <string.h>

/* ---- Global input state ---- */

nt_input_t g_nt_input;

/* ---- File-scope statics for edge detection ---- */

static bool s_keys_current[NT_KEY_COUNT];
static bool s_keys_pressed[NT_KEY_COUNT];
static bool s_keys_released[NT_KEY_COUNT];

/* ---- UTF-32 char ring (typed text, fed by platform char sources) ---- */

/* NT_INPUT_CHAR_RING (power-of-2: one frame's typing < 32) is defined in nt_input.h so the devapi
   layer can reject an over-capacity text batch. */

static uint32_t s_char_ring[NT_INPUT_CHAR_RING];
static uint32_t s_char_head; /* next write slot */
static uint32_t s_char_tail; /* next read slot */

/* ---- nt_input automation surface (player gate + immediate inject buffer) ---- */

/* The whole automation surface (player gate + inject pipeline) exists ONLY to serve the devapi
   layer. NT_INPUT_AUTOMATION_ENABLED gates it so a build without the devapi input group carries
   none of it ("tiny size — every byte counts"); OFF leaves the plain lean apply layer. */
#if NT_INPUT_AUTOMATION_ENABLED

/* Gate at the apply seam: covers native drain + web direct apply. */
static bool s_player_enabled = true;

typedef struct {
    uint8_t kind; /* nt_inject_kind_t */
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
            uint32_t id;
            uint8_t buttons_mask;
        } buttons; /* set mask at the slot's current position */
        struct {
            float dx, dy;
        } wheel;
        struct {
            uint32_t cp;
        } chr;
    } u;
} nt_inject_event_t;

static nt_inject_event_t s_inject_buffer[NT_INPUT_INJECT_QUEUE_MAX];
static uint32_t s_inject_count;

static void inject_drain(void);

#endif /* NT_INPUT_AUTOMATION_ENABLED */

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
#if NT_INPUT_AUTOMATION_ENABLED
    s_inject_count = 0;
    s_player_enabled = true; /* clean default: real devices drive until a bot gates them */
#endif
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

#if NT_INPUT_AUTOMATION_ENABLED
    /* Apply the whole immediate inject buffer in the same post-clear window as the native char
       drain, so an injected rising edge survives to this frame's update. The devapi layer staged
       only the due events (it owns the schedule + sim-advance gating). */
    inject_drain();
#endif
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
#if NT_INPUT_AUTOMATION_ENABLED
    s_inject_count = 0;
    s_player_enabled = true;
#endif
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
#if NT_INPUT_AUTOMATION_ENABLED
    if (!s_player_enabled) {
        return; /* gated: real device suppressed while a bot drives (inject bypasses via *_apply). */
    }
#endif
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
#if NT_INPUT_AUTOMATION_ENABLED
    if (!s_player_enabled) {
        return;
    }
#endif
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
#if NT_INPUT_AUTOMATION_ENABLED
    if (!s_player_enabled) {
        return;
    }
#endif
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
#if NT_INPUT_AUTOMATION_ENABLED
    if (!s_player_enabled) {
        return;
    }
#endif
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
#if NT_INPUT_AUTOMATION_ENABLED
    if (!s_player_enabled) {
        return;
    }
#endif
    nt_input_pointer_up_apply(id);
}

static void nt_input_wheel_apply(float dx, float dy) {
    nt_pointer_t *mouse = find_mouse_pointer();
    if (mouse == NULL) {
        return; /* No mouse slot at apply time -> documented no-op (caller passes x/y or moves first). */
    }
    mouse->wheel_dx += dx;
    mouse->wheel_dy += dy;
}

void nt_input_wheel(float dx, float dy) {
#if NT_INPUT_AUTOMATION_ENABLED
    if (!s_player_enabled) {
        return;
    }
#endif
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

/* ---- nt_input automation surface (player gate API + synthetic input injection) ---- */

#if NT_INPUT_AUTOMATION_ENABLED

void nt_input_set_player_enabled(bool enabled) {
    /* ON->OFF edge: release held real input so no key/button sticks down (focus-lost reuse). */
    if (s_player_enabled && !enabled) {
        nt_input_clear_all_keys();
        nt_input_clear_all_pointers();
    }
    s_player_enabled = enabled;
}

/* Apply a button mask on slot `id` at its CURRENT position — no move, no delta, no baked x/y. If the
   slot does not exist (no prior pointer), create it at (0,0) as a mouse so the button still lands. */
static void nt_input_pointer_buttons_apply(uint32_t id, uint8_t buttons_mask) {
    nt_pointer_t *ptr = find_pointer_by_id(id);
    if (ptr == NULL || ptr->deactivate_pending) {
        nt_input_pointer_down_apply(id, 0.0F, 0.0F, 1.0F, (uint8_t)NT_POINTER_MOUSE, buttons_mask);
        return;
    }
    apply_buttons_mask(ptr, buttons_mask);
}

/* Whole-or-nothing reserve: a command needing N entries either gets all N or none -- a partial
   stage would leave a stuck key-down or a drag with no release. Returns the first reserved
   slot, or NULL on overflow. */
static nt_inject_event_t *inject_reserve(uint32_t n) {
    if (n > NT_INPUT_INJECT_QUEUE_MAX - s_inject_count) {
        return NULL; /* would overflow -- reject the whole command, write nothing */
    }
    nt_inject_event_t *first = &s_inject_buffer[s_inject_count];
    s_inject_count += n;
    return first;
}

bool nt_input_inject_key(nt_key_t key, bool down) {
    if (key >= NT_KEY_COUNT) {
        return false; /* L1 defense-in-depth: out-of-range key (L2 validates first; never asserts). */
    }
    nt_inject_event_t *e = inject_reserve(1);
    if (e == NULL) {
        return false;
    }
    e->kind = NT_INJECT_KEY;
    e->u.key.key = (uint8_t)key;
    e->u.key.down = down;
    return true;
}

bool nt_input_inject_pointer(nt_inject_kind_t kind, uint32_t id, float x, float y, float pressure, uint8_t type, uint8_t buttons_mask) {
    /* L1 defense-in-depth (L2 validates first; never asserts): reject a kind that is not a pointer
       event, an unknown pointer type, or a mask with bits outside {1,2,4}. */
    if (kind != NT_INJECT_POINTER_DOWN && kind != NT_INJECT_POINTER_MOVE && kind != NT_INJECT_POINTER_UP) {
        return false;
    }
    if (kind != NT_INJECT_POINTER_UP && (type > (uint8_t)NT_POINTER_PEN || buttons_mask > 7U)) {
        return false;
    }
    nt_inject_event_t *e = inject_reserve(1);
    if (e == NULL) {
        return false;
    }
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

bool nt_input_inject_buttons(uint32_t id, uint8_t buttons_mask) {
    nt_inject_event_t *e = inject_reserve(1);
    if (e == NULL) {
        return false;
    }
    e->kind = NT_INJECT_POINTER_BUTTONS;
    e->u.buttons.id = id;
    e->u.buttons.buttons_mask = buttons_mask;
    return true;
}

bool nt_input_inject_wheel(float dx, float dy) {
    nt_inject_event_t *e = inject_reserve(1);
    if (e == NULL) {
        return false;
    }
    e->kind = NT_INJECT_WHEEL;
    e->u.wheel.dx = dx;
    e->u.wheel.dy = dy;
    return true;
}

bool nt_input_inject_text(const uint32_t *cps, uint32_t n) {
    if (cps == NULL || n == 0) {
        return false;
    }
    /* All n CHARs drain in this SINGLE poll into the 32-slot char ring; nt_input_buffer_char_apply
       drops once full. Reject n beyond the ring so the caller never lies about what lands
       (whole-or-nothing by codepoint count). */
    if (n > NT_INPUT_CHAR_RING) {
        return false;
    }
    nt_inject_event_t *e = inject_reserve(n); /* whole-or-nothing by codepoint count */
    if (e == NULL) {
        return false;
    }
    for (uint32_t i = 0; i < n; i++) {
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
    case NT_INJECT_POINTER_BUTTONS:
        nt_input_pointer_buttons_apply(e->u.buttons.id, e->u.buttons.buttons_mask);
        break;
    case NT_INJECT_WHEEL:
        nt_input_wheel_apply(e->u.wheel.dx, e->u.wheel.dy);
        break;
    case NT_INJECT_CHAR:
        nt_input_buffer_char_apply(e->u.chr.cp);
        break;
    }
}

/* Apply the whole immediate buffer in stage order (FIFO), then empty it. The devapi layer staged
   only the events due this poll, so there is nothing to defer here. */
static void inject_drain(void) {
    for (uint32_t i = 0; i < s_inject_count; i++) {
        inject_apply_one(&s_inject_buffer[i]);
    }
    s_inject_count = 0;
}

#endif /* NT_INPUT_AUTOMATION_ENABLED */
