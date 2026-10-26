#include <string.h>

#include "app/nt_app.h" /* g_nt_app.frame — the advance clock the schedule ticks on. */
#include "core/nt_assert.h"
#include "devapi/nt_devapi_input_internal.h"
#include "devapi/nt_devapi_internal.h"
#include "input/nt_input.h"
#include "input/nt_input_internal.h"
#include "window/nt_window.h" /* g_nt_window.fb_height — the height basis for the input.* Y flip. */

/* Veneer over the core inject API: range-check bot input -> bad_params, never assert. */

// #region devapi input schedule
/* Schedule lives in devapi, not nt_input: only the devtool legitimately knows g_nt_app.frame. */

/* NT_DEVAPI_INPUT_SCHED_MAX (bounded BSS schedule cap, -D overridable) is defined in
   nt_devapi_input_internal.h so the ui group + the unit tests derive their sizes from the real cap. */

/* Links the two caps so a full schedule's release can never exceed the immediate buffer. */
_Static_assert(NT_DEVAPI_INPUT_SCHED_MAX <= NT_INPUT_INJECT_QUEUE_MAX, "schedule must never release more than the immediate buffer can hold");

typedef struct {
    uint16_t frames_remaining; /* sim-advance countdown; 0 = release on the next advancing tick */
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
            uint32_t id;
            uint8_t buttons_mask;
        } buttons; /* set mask at the slot's apply-time position */
        struct {
            float dx, dy;
        } wheel;
        struct {
            uint32_t cp;
        } chr;
    } u;
} sched_entry_t;

static sched_entry_t s_sched[NT_DEVAPI_INPUT_SCHED_MAX];
static uint32_t s_sched_count;

/* Whole-or-nothing reserve: a multi-event command gets all N slots or none, so a near-full
   schedule can never accept a DOWN and reject its UP. */
static sched_entry_t *sched_reserve(uint32_t n) {
    if (n > NT_DEVAPI_INPUT_SCHED_MAX - s_sched_count) {
        return NULL;
    }
    sched_entry_t *first = &s_sched[s_sched_count];
    s_sched_count += n;
    return first;
}

static bool sched_can_reserve(uint32_t n) { return n <= NT_DEVAPI_INPUT_SCHED_MAX - s_sched_count; }

static bool sched_key(nt_key_t key, bool down, uint16_t at_frame) {
    sched_entry_t *e = sched_reserve(1);
    if (e == NULL) {
        return false;
    }
    e->frames_remaining = at_frame;
    e->kind = NT_INJECT_KEY;
    e->u.key.key = (uint8_t)key;
    e->u.key.down = down;
    return true;
}

static bool sched_pointer(nt_inject_kind_t kind, uint32_t id, float x, float y, float pressure, uint8_t type, uint8_t buttons_mask, uint16_t at_frame) {
    sched_entry_t *e = sched_reserve(1);
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

/* Mask applies at the slot's apply-time position (schedule order), so a queued move is honored. */
static bool sched_buttons(uint32_t id, uint8_t buttons_mask, uint16_t at_frame) {
    sched_entry_t *e = sched_reserve(1);
    if (e == NULL) {
        return false;
    }
    e->frames_remaining = at_frame;
    e->kind = NT_INJECT_POINTER_BUTTONS;
    e->u.buttons.id = id;
    e->u.buttons.buttons_mask = buttons_mask;
    return true;
}

static bool sched_wheel(float dx, float dy, uint16_t at_frame) {
    sched_entry_t *e = sched_reserve(1);
    if (e == NULL) {
        return false;
    }
    e->frames_remaining = at_frame;
    e->kind = NT_INJECT_WHEEL;
    e->u.wheel.dx = dx;
    e->u.wheel.dy = dy;
    return true;
}

/* Cross-group reuse wrappers (the ui group delegates here so there is ONE scheduler on the immediate
   buffer — see nt_devapi_input_internal.h). Thin pass-throughs to the static scheduler; whole-or-nothing
   semantics are the caller's (preflight can_reserve(N) then issue N sched_* calls, single-threaded). */
bool nt_devapi_input_sched_can_reserve(uint32_t n) { return sched_can_reserve(n); }

bool nt_devapi_input_sched_pointer(nt_inject_kind_t kind, uint32_t id, float x, float y, float pressure, uint8_t type, uint8_t buttons_mask, uint16_t at_frame) {
    return sched_pointer(kind, id, x, y, pressure, type, buttons_mask, at_frame);
}

bool nt_devapi_input_sched_wheel(float dx, float dy, uint16_t at_frame) { return sched_wheel(dx, dy, at_frame); }

/* down@0 + up@hold (2 entries): the tap/click hold primitive. */
static bool sched_tap(nt_key_t key, uint16_t hold_frames) {
    sched_entry_t *e = sched_reserve(2);
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

/* whole-or-nothing by codepoint count: reject n beyond the char ring so queued never overstates. */
static bool sched_text(const uint32_t *cps, uint32_t n) {
    if (cps == NULL || n == 0) {
        return false;
    }
    if (n > NT_INPUT_CHAR_RING) {
        return false;
    }
    sched_entry_t *e = sched_reserve(n);
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

/* Release one due entry into the immediate inject buffer. false == engine invariant violation
   (capacity preflighted + _Static_assert), so the caller asserts. */
static bool sched_release_one(const sched_entry_t *e) {
    switch ((nt_inject_kind_t)e->kind) {
    case NT_INJECT_KEY:
        return nt_input_inject_key((nt_key_t)e->u.key.key, e->u.key.down);
    case NT_INJECT_POINTER_DOWN:
    case NT_INJECT_POINTER_MOVE:
        return nt_input_inject_pointer((nt_inject_kind_t)e->kind, e->u.pointer.id, e->u.pointer.x, e->u.pointer.y, e->u.pointer.pressure, e->u.pointer.type, e->u.pointer.buttons_mask);
    case NT_INJECT_POINTER_UP:
        return nt_input_inject_pointer(NT_INJECT_POINTER_UP, e->u.pointer_up.id, 0.0F, 0.0F, 0.0F, 0, 0);
    case NT_INJECT_POINTER_BUTTONS:
        return nt_input_inject_buttons(e->u.buttons.id, e->u.buttons.buttons_mask);
    case NT_INJECT_WHEEL:
        return nt_input_inject_wheel(e->u.wheel.dx, e->u.wheel.dy);
    case NT_INJECT_CHAR: {
        uint32_t cp = e->u.chr.cp;
        return nt_input_inject_text(&cp, 1U);
    }
    }
    return false; /* unreachable: kind is always one of the above. */
}

/* Release due entries in enqueue order (preserves bot event ordering); survivors decrement + compact. */
static void sched_tick(void) {
    uint32_t out = 0;
    for (uint32_t i = 0; i < s_sched_count; i++) {
        if (s_sched[i].frames_remaining == 0) {
            bool ok = sched_release_one(&s_sched[i]); /* due: release, do NOT carry forward. */
            NT_ASSERT(ok);                            /* buffer >= schedule by _Static_assert; a drop is a build bug. */
            (void)ok;
            continue;
        }
        s_sched[i].frames_remaining--;
        s_sched[out++] = s_sched[i]; /* survivor: decrement + compact down. */
    }
    s_sched_count = out;
}

/* Real sim-advance = g_nt_app.frame changed since last update. Seeded in reset so the
   first post-reset tick doesn't fire spuriously. */
static uint32_t s_last_frame;

void nt_devapi_input_update(void) {
    bool advanced = (g_nt_app.frame != s_last_frame);
    s_last_frame = g_nt_app.frame;
    /* A frozen tick (pause / manual-idle) releases nothing: synthetic input holds until the sim
       advances, so an injected edge can't be missed by a stalled update. */
    if (advanced) {
        sched_tick();
    }
}

void nt_devapi_input_reset(void) {
    /* Clears ONLY devapi-owned transient state (schedule + advance clock). Applied input is
       game-owned (a bot is indistinguishable from a human at the inject layer), so it is NOT released here. */
    s_sched_count = 0;
    s_last_frame = g_nt_app.frame; /* re-seed so the next update compares against the real frame. */
}
// #endregion

/* string -> nt_key_t, indexed by enum value; each name = the enum minus NT_KEY_. */
static const char *const k_key_names[NT_KEY_COUNT] = {
    "A",        "B",          "C",          "D",           "E",     "F",     "G",      "H",   "I",         "J",      "K",      "L",     "M",     "N",       "O",         "P",  "Q",  "R",
    "S",        "T",          "U",          "V",           "W",     "X",     "Y",      "Z",   "0",         "1",      "2",      "3",     "4",     "5",       "6",         "7",  "8",  "9",
    "ARROW_UP", "ARROW_DOWN", "ARROW_LEFT", "ARROW_RIGHT", "SPACE", "ENTER", "ESCAPE", "TAB", "BACKSPACE", "LSHIFT", "RSHIFT", "LCTRL", "RCTRL", "LALT",    "RALT",      "F1", "F2", "F3",
    "F4",       "F5",         "F6",         "F7",          "F8",    "F9",    "F10",    "F11", "F12",       "DELETE", "INSERT", "HOME",  "END",   "PAGE_UP", "PAGE_DOWN",
};
_Static_assert(NT_COUNTOF(k_key_names) == NT_KEY_COUNT, "key-name table must match nt_key_t");

static bool nt_input_key_from_name(const char *name, nt_key_t *out) {
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

/* Map an optional pointer "type" string -> nt_pointer_type_t. Default mouse; unknown -> false. */
static bool pointer_type_from_name(const cJSON *t, uint8_t *out) {
    if (t == NULL) {
        *out = (uint8_t)NT_POINTER_MOUSE;
        return true;
    }
    if (!cJSON_IsString(t) || t->valuestring == NULL) {
        return false;
    }
    if (strcmp(t->valuestring, "mouse") == 0) {
        *out = (uint8_t)NT_POINTER_MOUSE;
    } else if (strcmp(t->valuestring, "touch") == 0) {
        *out = (uint8_t)NT_POINTER_TOUCH;
    } else if (strcmp(t->valuestring, "pen") == 0) {
        *out = (uint8_t)NT_POINTER_PEN;
    } else {
        return false;
    }
    return true;
}

/* Mouse-button mask: bits {1,2,4}, so [0,7]; a wider value would set phantom bits. */
static bool parse_button_mask(const cJSON *bj, const char *message, uint8_t *out, nt_devapi_error *err) {
    uint16_t v = 0;
    if (!nt_devapi_parse_u16_param_exact(bj, 7, err, message, &v)) {
        return false;
    }
    *out = (uint8_t)v;
    return true;
}

/* The ONE Y-up -> device flip for input.*. Basis is g_nt_window.fb_height (device space), distinct
   from ui.*'s ctx LAYOUT height — different basis by design, not a bug. */
static float flip_y_up_to_device(float y_up) { return (float)g_nt_window.fb_height - y_up; }

// #region input.*
static bool cmd_input_key(const cJSON *params, cJSON *result, nt_devapi_error *err, void *ud) {
    const cJSON *k = cJSON_GetObjectItemCaseSensitive(params, "key");
    if (!cJSON_IsString(k) || k->valuestring == NULL) {
        return nt_devapi_fail(err, "input.key: key must be a string");
    }
    nt_key_t key;
    if (!nt_input_key_from_name(k->valuestring, &key)) {
        return nt_devapi_fail(err, "input.key: unknown key name");
    }
    const cJSON *hold = cJSON_GetObjectItemCaseSensitive(params, "hold");
    if (hold != NULL) {
        uint16_t h;
        if (!nt_devapi_parse_u16_param_exact(hold, UINT16_MAX, err, "input.key: hold must be an integer in [0, 65535]", &h)) {
            return false;
        }
        if (!sched_tap(key, h)) {
            return nt_devapi_fail(err, "input.key: inject schedule overflow");
        }
    } else {
        bool down = true;
        const cJSON *d = cJSON_GetObjectItemCaseSensitive(params, "down");
        if (d != NULL) {
            if (!cJSON_IsBool(d)) {
                return nt_devapi_fail(err, "input.key: down must be a bool");
            }
            down = cJSON_IsTrue(d);
        }
        if (!sched_key(key, down, 0)) {
            return nt_devapi_fail(err, "input.key: inject schedule overflow");
        }
    }
    devapi_add_bool(result, "ok", true);
    return true;
}

/* The pointer primitive: action -> nt_inject_kind_t, type string -> nt_pointer_type_t. */
static bool cmd_input_pointer(const cJSON *params, cJSON *result, nt_devapi_error *err, void *ud) {
    const cJSON *a = cJSON_GetObjectItemCaseSensitive(params, "action");
    if (!cJSON_IsString(a) || a->valuestring == NULL) {
        return nt_devapi_fail(err, "input.pointer: action must be a string");
    }
    nt_inject_kind_t kind;
    if (strcmp(a->valuestring, "down") == 0) {
        kind = NT_INJECT_POINTER_DOWN;
    } else if (strcmp(a->valuestring, "move") == 0) {
        kind = NT_INJECT_POINTER_MOVE;
    } else if (strcmp(a->valuestring, "up") == 0) {
        kind = NT_INJECT_POINTER_UP;
    } else {
        return nt_devapi_fail(err, "input.pointer: action must be 'down', 'move' or 'up'");
    }
    const cJSON *idj = cJSON_GetObjectItemCaseSensitive(params, "id");
    uint32_t id;
    if (!nt_devapi_parse_u32_param_exact(idj, UINT32_MAX, err, "input.pointer: id must be an integer in [0, 4294967295]", &id)) {
        return false;
    }
    float x = 0.0F;
    float y = 0.0F;
    if (kind != NT_INJECT_POINTER_UP) {
        const cJSON *xj = cJSON_GetObjectItemCaseSensitive(params, "x");
        const cJSON *yj = cJSON_GetObjectItemCaseSensitive(params, "y");
        if (!nt_devapi_parse_f32_param(xj, err, "input.pointer: x must be a finite number for down/move", &x) ||
            !nt_devapi_parse_f32_param(yj, err, "input.pointer: y must be a finite number for down/move", &y)) {
            return false;
        }
        y = flip_y_up_to_device(y);
    }
    uint8_t type;
    if (!pointer_type_from_name(cJSON_GetObjectItemCaseSensitive(params, "type"), &type)) {
        return nt_devapi_fail(err, "input.pointer: type must be 'mouse', 'touch' or 'pen'");
    }
    uint8_t buttons = 0;
    const cJSON *bj = cJSON_GetObjectItemCaseSensitive(params, "buttons");
    if (bj != NULL) {
        if (!parse_button_mask(bj, "input.pointer: buttons must be an integer mask in [0, 7]", &buttons, err)) {
            return false;
        }
    }
    if (!sched_pointer(kind, id, x, y, 1.0F, type, buttons, 0)) {
        return nt_devapi_fail(err, "input.pointer: inject schedule overflow");
    }
    devapi_add_number(result, "queued", 1.0);
    return true;
}

/* sugar = pointer move on the default mouse slot (reserved id). */
static bool cmd_input_move(const cJSON *params, cJSON *result, nt_devapi_error *err, void *ud) {
    const cJSON *xj = cJSON_GetObjectItemCaseSensitive(params, "x");
    const cJSON *yj = cJSON_GetObjectItemCaseSensitive(params, "y");
    float x;
    float y;
    if (!nt_devapi_parse_f32_param(xj, err, "input.move: x must be a finite number", &x) || !nt_devapi_parse_f32_param(yj, err, "input.move: y must be a finite number", &y)) {
        return false;
    }
    y = flip_y_up_to_device(y);
    uint32_t id = NT_INPUT_INJECT_POINTER_ID_BASE;
    const cJSON *idj = cJSON_GetObjectItemCaseSensitive(params, "id");
    if (idj != NULL) {
        if (!nt_devapi_parse_u32_param_exact(idj, UINT32_MAX, err, "input.move: id must be an integer in [0, 4294967295]", &id)) {
            return false;
        }
    }
    uint8_t type;
    if (!pointer_type_from_name(cJSON_GetObjectItemCaseSensitive(params, "type"), &type)) {
        return nt_devapi_fail(err, "input.move: type must be 'mouse', 'touch' or 'pen'");
    }
    if (!sched_pointer(NT_INJECT_POINTER_MOVE, id, x, y, 1.0F, type, 0, 0)) {
        return nt_devapi_fail(err, "input.move: inject schedule overflow");
    }
    devapi_add_number(result, "queued", 1.0);
    return true;
}

/* down@0 + up@hold on the mouse slot; hold default 1 (1-frame click), 0 = same-frame. */
static bool cmd_input_click(const cJSON *params, cJSON *result, nt_devapi_error *err, void *ud) {
    const cJSON *xj = cJSON_GetObjectItemCaseSensitive(params, "x");
    const cJSON *yj = cJSON_GetObjectItemCaseSensitive(params, "y");
    float x;
    float y;
    if (!nt_devapi_parse_f32_param(xj, err, "input.click: x must be a finite number", &x) || !nt_devapi_parse_f32_param(yj, err, "input.click: y must be a finite number", &y)) {
        return false;
    }
    y = flip_y_up_to_device(y);
    uint32_t id = NT_INPUT_INJECT_POINTER_ID_BASE;
    const cJSON *idj = cJSON_GetObjectItemCaseSensitive(params, "id");
    if (idj != NULL) {
        if (!nt_devapi_parse_u32_param_exact(idj, UINT32_MAX, err, "input.click: id must be an integer in [0, 4294967295]", &id)) {
            return false;
        }
    }
    uint8_t buttons = 1U; /* default left */
    const cJSON *bj = cJSON_GetObjectItemCaseSensitive(params, "button");
    if (bj != NULL) {
        if (!parse_button_mask(bj, "input.click: button must be an integer mask in [0, 7]", &buttons, err)) {
            return false;
        }
    }
    uint16_t hold = 1U; /* default: 1-frame-held click (down@0 + up@1) */
    const cJSON *hj = cJSON_GetObjectItemCaseSensitive(params, "hold");
    if (hj != NULL) {
        if (!nt_devapi_parse_u16_param_exact(hj, UINT16_MAX, err, "input.click: hold must be an integer in [0, 65535]", &hold)) {
            return false;
        }
    }
    /* whole-or-nothing: preflight both entries (see sched_reserve). */
    if (!sched_can_reserve(2U)) {
        return nt_devapi_fail(err, "input.click: inject schedule overflow");
    }
    if (!sched_pointer(NT_INJECT_POINTER_DOWN, id, x, y, 1.0F, (uint8_t)NT_POINTER_MOUSE, buttons, 0)) {
        return nt_devapi_fail(err, "input.click: inject schedule overflow");
    }
    if (!sched_pointer(NT_INJECT_POINTER_UP, id, x, y, 0.0F, (uint8_t)NT_POINTER_MOUSE, 0, hold)) {
        return nt_devapi_fail(err, "input.click: inject schedule overflow");
    }
    devapi_add_number(result, "queued", 2.0);
    return true;
}

/* Without x/y the wheel applies at the slot's apply-time position (no submit-time g_nt_input read). */
static bool cmd_input_wheel(const cJSON *params, cJSON *result, nt_devapi_error *err, void *ud) {
    /* dx/dy are notch deltas (content-scroll sign, see nt_pointer_t.wheel_dy), NOT spatial coords —
       the Y-up flip applies only to the positioned (x,y) below, never to the wheel delta. */
    float dx = 0.0F;
    float dy = 0.0F;
    const cJSON *dxj = cJSON_GetObjectItemCaseSensitive(params, "dx");
    const cJSON *dyj = cJSON_GetObjectItemCaseSensitive(params, "dy");
    if (dxj != NULL && !nt_devapi_parse_f32_param(dxj, err, "input.wheel: dx must be a finite number", &dx)) {
        return false;
    }
    if (dyj != NULL && !nt_devapi_parse_f32_param(dyj, err, "input.wheel: dy must be a finite number", &dy)) {
        return false;
    }
    const cJSON *xj = cJSON_GetObjectItemCaseSensitive(params, "x");
    const cJSON *yj = cJSON_GetObjectItemCaseSensitive(params, "y");
    if (xj != NULL || yj != NULL) {
        /* Positioned scroll: x AND y both required, both finite. */
        float x;
        float y;
        if (!nt_devapi_parse_f32_param(xj, err, "input.wheel: x must be a finite number", &x) || !nt_devapi_parse_f32_param(yj, err, "input.wheel: y must be a finite number", &y)) {
            return false;
        }
        y = flip_y_up_to_device(y); /* positioned scroll: Y-up in -> device Y-down for the MOVE entry. */
        /* whole-or-nothing: reserve MOVE + wheel together (see sched_reserve). */
        if (!sched_can_reserve(2U)) {
            return nt_devapi_fail(err, "input.wheel: inject schedule overflow");
        }
        sched_pointer(NT_INJECT_POINTER_MOVE, NT_INPUT_INJECT_POINTER_ID_BASE, x, y, 1.0F, (uint8_t)NT_POINTER_MOUSE, 0, 0);
        sched_wheel(dx, dy, 0);
    } else if (!sched_wheel(dx, dy, 0)) {
        return nt_devapi_fail(err, "input.wheel: inject schedule overflow");
    }
    devapi_add_bool(result, "ok", true);
    return true;
}

/* sugar: down@0 + a move per point (each on its own frame_offset using frame_stride, default 1) + up.
   NO C interpolation — the bot supplies the path samples. Per-entry overflow -> bad_params. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static bool cmd_input_gesture(const cJSON *params, cJSON *result, nt_devapi_error *err, void *ud) {
    const cJSON *idj = cJSON_GetObjectItemCaseSensitive(params, "id");
    uint32_t id;
    if (!nt_devapi_parse_u32_param_exact(idj, UINT32_MAX, err, "input.gesture: id must be an integer in [0, 4294967295]", &id)) {
        return false;
    }
    uint8_t type;
    if (!pointer_type_from_name(cJSON_GetObjectItemCaseSensitive(params, "type"), &type)) {
        return nt_devapi_fail(err, "input.gesture: type must be 'mouse', 'touch' or 'pen'");
    }
    const cJSON *points = cJSON_GetObjectItemCaseSensitive(params, "points");
    if (!cJSON_IsArray(points)) {
        return nt_devapi_fail(err, "input.gesture: points must be an array of [x,y]");
    }
    int npoints = cJSON_GetArraySize(points);
    if (npoints < 1) {
        return nt_devapi_fail(err, "input.gesture: points must have at least one [x,y]");
    }
    uint16_t stride = 1;
    const cJSON *sj = cJSON_GetObjectItemCaseSensitive(params, "frame_stride");
    if (sj != NULL) {
        if (!nt_devapi_parse_u16_param_exact(sj, UINT16_MAX, err, "input.gesture: frame_stride must be an integer in [0, 65535]", &stride)) {
            return false;
        }
    }
    /* Validate every point up front (incl. finiteness) so a malformed point rejects the whole gesture
       before any enqueue — extraction below then trusts the values are finite. */
    const cJSON *p = NULL;
    cJSON_ArrayForEach(p, points) {
        if (!cJSON_IsArray(p) || cJSON_GetArraySize(p) != 2) {
            return nt_devapi_fail(err, "input.gesture: each point must be [x,y]");
        }
        float tmp;
        if (!nt_devapi_parse_f32_param(cJSON_GetArrayItem(p, 0), err, "input.gesture: point x must be a finite number", &tmp) ||
            !nt_devapi_parse_f32_param(cJSON_GetArrayItem(p, 1), err, "input.gesture: point y must be a finite number", &tmp)) {
            return false;
        }
    }
    /* Frame-offset arithmetic guard: (npoints-1)*stride must fit in the uint16 countdown.
       Compute in a wider type so the multiplication itself can't overflow int (untrusted npoints). */
    int64_t last_offset = (int64_t)(npoints - 1) * (int64_t)stride;
    if (last_offset > UINT16_MAX) {
        return nt_devapi_fail(err, "input.gesture: span (points*frame_stride) exceeds the frame-offset range");
    }
    /* whole-or-nothing: reserve DOWN + (npoints-1) moves + UP up front (see sched_reserve).
       DOWN@0 carries point[0], so moves cover points[1..n-1] only. */
    if (!sched_can_reserve((uint32_t)npoints + 1U)) {
        return nt_devapi_fail(err, "input.gesture: inject schedule overflow");
    }
    /* down@0 carrying first point, a move per SUBSEQUENT point at its frame offset, up after the last. */
    const cJSON *first = cJSON_GetArrayItem(points, 0);
    float fx = (float)cJSON_GetArrayItem(first, 0)->valuedouble;
    float fy = flip_y_up_to_device((float)cJSON_GetArrayItem(first, 1)->valuedouble); /* each point's Y is Y-up. */
    if (!sched_pointer(NT_INJECT_POINTER_DOWN, id, fx, fy, 1.0F, type, 1U, 0)) {
        return nt_devapi_fail(err, "input.gesture: inject schedule overflow");
    }
    int queued = 1;
    int idx = 0;
    cJSON_ArrayForEach(p, points) {
        if (idx == 0) {
            idx++;
            continue; /* DOWN already applied point[0] @0 -- skip the redundant same-frame MOVE. */
        }
        float mx = (float)cJSON_GetArrayItem(p, 0)->valuedouble;
        float my = flip_y_up_to_device((float)cJSON_GetArrayItem(p, 1)->valuedouble); /* Y-up point. */
        /* Compute the offset in int64 so the narrowing source is self-evidently within the
           already-validated [0, UINT16_MAX] span (idx <= npoints-1 => at <= last_offset). */
        uint16_t at = (uint16_t)((int64_t)idx * (int64_t)stride);
        if (!sched_pointer(NT_INJECT_POINTER_MOVE, id, mx, my, 1.0F, type, 1U, at)) {
            return nt_devapi_fail(err, "input.gesture: inject schedule overflow");
        }
        queued++;
        idx++;
    }
    if (!sched_pointer(NT_INJECT_POINTER_UP, id, 0.0F, 0.0F, 0.0F, type, 0, (uint16_t)last_offset)) {
        return nt_devapi_fail(err, "input.gesture: inject schedule overflow");
    }
    queued++;
    devapi_add_number(result, "queued", (double)queued);
    return true;
}

/* mouse-button mask {1,2,4}: inject a pointer event carrying the button mask on the given id. */
static bool cmd_input_button(const cJSON *params, cJSON *result, nt_devapi_error *err, void *ud) {
    const cJSON *bj = cJSON_GetObjectItemCaseSensitive(params, "buttons");
    uint8_t buttons;
    if (!parse_button_mask(bj, "input.button: buttons must be an integer mask in [0, 7]", &buttons, err)) {
        return false;
    }
    uint32_t id = NT_INPUT_INJECT_POINTER_ID_BASE;
    const cJSON *idj = cJSON_GetObjectItemCaseSensitive(params, "id");
    if (idj != NULL) {
        if (!nt_devapi_parse_u32_param_exact(idj, UINT32_MAX, err, "input.button: id must be an integer in [0, 4294967295]", &id)) {
            return false;
        }
    }
    /* Mask applies at the slot's apply-time position (schedule order), so a queued move is honored. */
    if (!sched_buttons(id, buttons, 0)) {
        return nt_devapi_fail(err, "input.button: inject schedule overflow");
    }
    devapi_add_bool(result, "ok", true);
    return true;
}

static bool cmd_input_set_player_enabled(const cJSON *params, cJSON *result, nt_devapi_error *err, void *ud) {
    const cJSON *e = cJSON_GetObjectItemCaseSensitive(params, "enabled");
    if (!cJSON_IsBool(e)) {
        return nt_devapi_fail(err, "input.set_player_enabled: enabled must be a bool");
    }
    bool enabled = cJSON_IsTrue(e);
    nt_input_set_player_enabled(enabled);
    devapi_add_bool(result, "enabled", enabled);
    return true;
}

/* Decode UTF-8 -> codepoints (malformed/invalid scalar -> bad_params, never assert). */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static bool cmd_input_text(const cJSON *params, cJSON *result, nt_devapi_error *err, void *ud) {
    const cJSON *t = cJSON_GetObjectItemCaseSensitive(params, "text");
    if (!cJSON_IsString(t) || t->valuestring == NULL) {
        return nt_devapi_fail(err, "input.text: text must be a string");
    }
    const unsigned char *s = (const unsigned char *)t->valuestring;
    /* Bounded by the schedule cap: more codepoints than that can never enqueue whole. */
    uint32_t cps[NT_DEVAPI_INPUT_SCHED_MAX];
    uint32_t n = 0;
    while (*s != 0) {
        uint32_t cp;
        uint32_t extra;
        unsigned char c = *s;
        if (c < 0x80U) {
            cp = c;
            extra = 0;
        } else if ((c & 0xE0U) == 0xC0U) {
            cp = c & 0x1FU;
            extra = 1;
        } else if ((c & 0xF0U) == 0xE0U) {
            cp = c & 0x0FU;
            extra = 2;
        } else if ((c & 0xF8U) == 0xF0U) {
            cp = c & 0x07U;
            extra = 3;
        } else {
            return nt_devapi_fail(err, "input.text: malformed UTF-8 lead byte");
        }
        s++;
        for (uint32_t i = 0; i < extra; i++) {
            if ((*s & 0xC0U) != 0x80U) {
                return nt_devapi_fail(err, "input.text: malformed UTF-8 continuation byte");
            }
            cp = (cp << 6U) | (*s & 0x3FU);
            s++;
        }
        /* Reject structurally-valid-but-invalid scalars: an overlong encoding (below the minimum
           for its byte-length), a UTF-16 surrogate, or a value above U+10FFFF. A real keyboard
           never produces these; reject untrusted input -> bad_params (never assert). */
        static const uint32_t k_min_cp[4] = {0x0U, 0x80U, 0x800U, 0x10000U};
        if (cp < k_min_cp[extra] || cp > 0x10FFFFU || (cp >= 0xD800U && cp <= 0xDFFFU)) {
            return nt_devapi_fail(err, "input.text: invalid UTF-8");
        }
        if (n >= NT_DEVAPI_INPUT_SCHED_MAX) {
            return nt_devapi_fail(err, "input.text: too many codepoints (exceeds inject schedule)");
        }
        cps[n] = cp;
        n++;
    }
    /* Empty text is valid UTF-8 -> a no-op queued:0 (the advertised {queued:number} contract
       represents 0), not a hard error mid-script. */
    if (n == 0) {
        devapi_add_number(result, "queued", 0.0);
        return true;
    }
    if (!sched_text(cps, n)) {
        return nt_devapi_fail(err, "input.text: exceeds char-ring capacity (32)");
    }
    devapi_add_number(result, "queued", (double)n);
    return true;
}

/* IMMEDIATE read of the last polled state: an enqueued inject is not visible until a sim-advance.
   pop_text:true DRAINS the char ring (consuming side effect). */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static bool cmd_input_state(const cJSON *params, cJSON *result, nt_devapi_error *err, void *ud) {
    const cJSON *k = cJSON_GetObjectItemCaseSensitive(params, "key");
    if (k != NULL) {
        if (!cJSON_IsString(k) || k->valuestring == NULL) {
            return nt_devapi_fail(err, "input.state: key must be a string");
        }
        nt_key_t key;
        if (!nt_input_key_from_name(k->valuestring, &key)) {
            return nt_devapi_fail(err, "input.state: unknown key name");
        }
        devapi_add_bool(result, "down", nt_input_key_is_down(key));
        devapi_add_bool(result, "pressed", nt_input_key_is_pressed(key));
        devapi_add_bool(result, "released", nt_input_key_is_released(key));
    }
    const cJSON *pt = cJSON_GetObjectItemCaseSensitive(params, "pop_text");
    if (pt != NULL) {
        if (!cJSON_IsBool(pt)) {
            return nt_devapi_fail(err, "input.state: pop_text must be a bool");
        }
        if (cJSON_IsTrue(pt)) {
            cJSON *cps = cJSON_AddArrayToObject(result, "codepoints");
            NT_ASSERT(cps != NULL); /* OOM traps fail-early (matches devapi_add_*). */
            /* Cap the drain to the inject queue size so a flood can't spin the read forever. */
            uint32_t cp;
            for (uint32_t i = 0; i < (uint32_t)NT_INPUT_INJECT_QUEUE_MAX && nt_input_pop_char(&cp); i++) {
                cJSON *num = cJSON_CreateNumber((double)cp);
                NT_ASSERT(num != NULL);
                cJSON_bool added = cJSON_AddItemToArray(cps, num);
                NT_ASSERT(added);
                (void)added;
            }
        }
    }
    return true;
}
// #endregion

static const nt_devapi_command k_input_cmds[] = {
    {
        .method = "input.key",
        .summary = "inject a key edge (down default true) OR a tap (hold frames -> down@0 + up@hold)",
        .params_shape = "{key:string, down?:bool, hold?:number}",
        .result_shape = "{ok:bool}",
        .fn = cmd_input_key,
    },
    {
        .method = "input.pointer",
        .summary = "the pointer primitive: action down/move/up on a given id (default mouse type); x/y are Y-up (origin bottom-left), framebuffer px — same convention as ui.*",
        .params_shape = "{action:string, id:number, x?:number, y?:number, type?:string, buttons?:number}, origin:bottom-left, y_axis:up, space:framebuffer-px",
        .result_shape = "{queued:number}",
        .fn = cmd_input_pointer,
    },
    {
        .method = "input.move",
        .summary = "sugar: pointer move on the default mouse slot (reserved id); x/y are Y-up (origin bottom-left), framebuffer px",
        .params_shape = "{x:number, y:number, id?:number, type?:string}, origin:bottom-left, y_axis:up, space:framebuffer-px",
        .result_shape = "{queued:number}",
        .fn = cmd_input_move,
    },
    {
        .method = "input.click",
        .summary = "sugar: pointer down@0 + up@hold (2 entries) on the mouse slot; hold default 1 frame, 0 = same-frame; x/y are Y-up (origin bottom-left), framebuffer px",
        .params_shape = "{x:number, y:number, button?:number, id?:number, hold?:number}, origin:bottom-left, y_axis:up, space:framebuffer-px",
        .result_shape = "{queued:number}",
        .fn = cmd_input_click,
    },
    {
        .method = "input.wheel",
        .summary = "scroll the mouse slot (notches); with x/y scrolls AT (x,y), else at the slot's apply-time position (no slot -> no-op); x/y are Y-up (origin bottom-left), framebuffer px; "
                   "dx/dy are notch deltas (not coords)",
        .params_shape = "{dx?:number, dy?:number, x?:number, y?:number}, origin:bottom-left, y_axis:up, space:framebuffer-px",
        .result_shape = "{ok:bool}",
        .fn = cmd_input_wheel,
    },
    {
        .method = "input.gesture",
        .summary = "sugar: down@0 + a move per point (frame_stride apart) + up; NO interpolation; each point's x/y is Y-up (origin bottom-left), framebuffer px",
        .params_shape = "{id:number, type?:string, points:[[x,y]], frame_stride?:number}, origin:bottom-left, y_axis:up, space:framebuffer-px",
        .result_shape = "{queued:number}",
        .fn = cmd_input_gesture,
    },
    {
        .method = "input.button",
        .summary = "set the mouse-button mask {1,2,4} on the given id (default reserved mouse slot)",
        .params_shape = "{buttons:number, id?:number}",
        .result_shape = "{ok:bool}",
        .fn = cmd_input_button,
    },
    {
        .method = "input.set_player_enabled",
        .summary = "toggle the player-input gate (false suppresses the real device; inject still flows)",
        .params_shape = "{enabled:bool}",
        .result_shape = "{enabled:bool}",
        .fn = cmd_input_set_player_enabled,
    },
    {
        .method = "input.text",
        .summary = "decode a UTF-8 string -> codepoints and enqueue them into the char ring",
        .params_shape = "{text:string}",
        .result_shape = "{queued:number}",
        .fn = cmd_input_text,
    },
    {
        .method = "input.state",
        .summary = "IMMEDIATE read of polled input state (stale until a sim-advance polls the inject queue); pop_text:true drains (consumes) the char ring",
        .params_shape = "{key?:string, pop_text?:bool}",
        .result_shape = "{down?:bool, pressed?:bool, released?:bool, codepoints?:[number]}",
        .fn = cmd_input_state,
    },
};

void nt_devapi_register_input(void) {
    nt_devapi_input_reset(); /* fresh schedule + seeded advance clock each init->register. */
    /* The only core coupling point: core/net call these generic hooks, never the group symbols. */
    nt_devapi_register_tick(nt_devapi_input_update);
    nt_devapi_register_reset(nt_devapi_input_reset);
    nt_devapi_register(k_input_cmds, NT_COUNTOF(k_input_cmds), NULL);
}
