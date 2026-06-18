#include <string.h>

#include "core/nt_assert.h"
#include "devapi/nt_devapi_internal.h"
#include "input/nt_input.h"
#include "input/nt_input_internal.h"

/* input.* command group: a thin L2 veneer over the L1 inject API (nt_input_inject_*). Bot input is
   range/type-checked -> bad_params; never assert on untrusted input (invariants assert, untrusted
   input returns a structured error). Fire-and-forget: validate -> enqueue -> immediate ok (or an
   immediate overflow/bad_params); NO defer (defer lives only in frame.wait/time.step, D-12).
   Compiles out entirely when NT_DEVAPI_REGISTER_input is absent. */

#ifdef NT_DEVAPI_REGISTER_input

static void set_bad_params(nt_devapi_error *err, const char *message) {
    err->code = NT_DEVAPI_ERR_BAD_PARAMS;
    err->message = message;
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

// #region input.*
static bool cmd_input_key(const cJSON *params, cJSON *result, nt_devapi_error *err, void *ud) {
    (void)ud;
    const cJSON *k = cJSON_GetObjectItemCaseSensitive(params, "key");
    if (!cJSON_IsString(k) || k->valuestring == NULL) {
        set_bad_params(err, "input.key: key must be a string");
        return false;
    }
    nt_key_t key;
    if (!nt_input_key_from_name(k->valuestring, &key)) {
        set_bad_params(err, "input.key: unknown key name");
        return false;
    }
    const cJSON *hold = cJSON_GetObjectItemCaseSensitive(params, "hold");
    if (hold != NULL) {
        if (!cJSON_IsNumber(hold)) {
            set_bad_params(err, "input.key: hold must be a number");
            return false;
        }
        int h = hold->valueint;
        if (h < 0 || h > UINT16_MAX) {
            set_bad_params(err, "input.key: hold out of range [0, 65535]");
            return false;
        }
        if (!nt_input_inject_key_tap(key, (uint16_t)h)) {
            set_bad_params(err, "input.key: inject queue overflow");
            return false;
        }
    } else {
        bool down = true;
        const cJSON *d = cJSON_GetObjectItemCaseSensitive(params, "down");
        if (d != NULL) {
            if (!cJSON_IsBool(d)) {
                set_bad_params(err, "input.key: down must be a bool");
                return false;
            }
            down = cJSON_IsTrue(d);
        }
        if (!nt_input_inject_key(key, down, 0)) {
            set_bad_params(err, "input.key: inject queue overflow");
            return false;
        }
    }
    devapi_add_bool(result, "ok", true);
    return true;
}

/* The pointer primitive (D-10): action -> nt_inject_kind_t, type string -> nt_pointer_type_t. */
static bool cmd_input_pointer(const cJSON *params, cJSON *result, nt_devapi_error *err, void *ud) {
    (void)ud;
    const cJSON *a = cJSON_GetObjectItemCaseSensitive(params, "action");
    if (!cJSON_IsString(a) || a->valuestring == NULL) {
        set_bad_params(err, "input.pointer: action must be a string");
        return false;
    }
    nt_inject_kind_t kind;
    if (strcmp(a->valuestring, "down") == 0) {
        kind = NT_INJECT_POINTER_DOWN;
    } else if (strcmp(a->valuestring, "move") == 0) {
        kind = NT_INJECT_POINTER_MOVE;
    } else if (strcmp(a->valuestring, "up") == 0) {
        kind = NT_INJECT_POINTER_UP;
    } else {
        set_bad_params(err, "input.pointer: action must be 'down', 'move' or 'up'");
        return false;
    }
    const cJSON *idj = cJSON_GetObjectItemCaseSensitive(params, "id");
    if (!cJSON_IsNumber(idj)) {
        set_bad_params(err, "input.pointer: id must be a number");
        return false;
    }
    uint32_t id = (uint32_t)idj->valuedouble;
    float x = 0.0F;
    float y = 0.0F;
    if (kind != NT_INJECT_POINTER_UP) {
        const cJSON *xj = cJSON_GetObjectItemCaseSensitive(params, "x");
        const cJSON *yj = cJSON_GetObjectItemCaseSensitive(params, "y");
        if (!cJSON_IsNumber(xj) || !cJSON_IsNumber(yj)) {
            set_bad_params(err, "input.pointer: x and y must be numbers for down/move");
            return false;
        }
        x = (float)xj->valuedouble;
        y = (float)yj->valuedouble;
    }
    uint8_t type;
    if (!pointer_type_from_name(cJSON_GetObjectItemCaseSensitive(params, "type"), &type)) {
        set_bad_params(err, "input.pointer: type must be 'mouse', 'touch' or 'pen'");
        return false;
    }
    uint8_t buttons = 0;
    const cJSON *bj = cJSON_GetObjectItemCaseSensitive(params, "buttons");
    if (bj != NULL) {
        if (!cJSON_IsNumber(bj)) {
            set_bad_params(err, "input.pointer: buttons must be a number");
            return false;
        }
        buttons = (uint8_t)bj->valueint;
    }
    if (!nt_input_inject_pointer(kind, id, x, y, 1.0F, type, buttons, 0)) {
        set_bad_params(err, "input.pointer: inject queue overflow");
        return false;
    }
    devapi_add_number(result, "queued", 1.0);
    return true;
}

/* sugar = pointer move on the default mouse slot (reserved id). */
static bool cmd_input_move(const cJSON *params, cJSON *result, nt_devapi_error *err, void *ud) {
    (void)ud;
    const cJSON *xj = cJSON_GetObjectItemCaseSensitive(params, "x");
    const cJSON *yj = cJSON_GetObjectItemCaseSensitive(params, "y");
    if (!cJSON_IsNumber(xj) || !cJSON_IsNumber(yj)) {
        set_bad_params(err, "input.move: x and y must be numbers");
        return false;
    }
    uint32_t id = NT_INPUT_INJECT_POINTER_ID_BASE;
    const cJSON *idj = cJSON_GetObjectItemCaseSensitive(params, "id");
    if (idj != NULL) {
        if (!cJSON_IsNumber(idj)) {
            set_bad_params(err, "input.move: id must be a number");
            return false;
        }
        id = (uint32_t)idj->valuedouble;
    }
    uint8_t type;
    if (!pointer_type_from_name(cJSON_GetObjectItemCaseSensitive(params, "type"), &type)) {
        set_bad_params(err, "input.move: type must be 'mouse', 'touch' or 'pen'");
        return false;
    }
    if (!nt_input_inject_pointer(NT_INJECT_POINTER_MOVE, id, (float)xj->valuedouble, (float)yj->valuedouble, 1.0F, type, 0, 0)) {
        set_bad_params(err, "input.move: inject queue overflow");
        return false;
    }
    devapi_add_number(result, "queued", 1.0);
    return true;
}

/* sugar = pointer down + up (2 entries) on the default mouse slot with the given button mask. */
static bool cmd_input_click(const cJSON *params, cJSON *result, nt_devapi_error *err, void *ud) {
    (void)ud;
    const cJSON *xj = cJSON_GetObjectItemCaseSensitive(params, "x");
    const cJSON *yj = cJSON_GetObjectItemCaseSensitive(params, "y");
    if (!cJSON_IsNumber(xj) || !cJSON_IsNumber(yj)) {
        set_bad_params(err, "input.click: x and y must be numbers");
        return false;
    }
    uint32_t id = NT_INPUT_INJECT_POINTER_ID_BASE;
    const cJSON *idj = cJSON_GetObjectItemCaseSensitive(params, "id");
    if (idj != NULL) {
        if (!cJSON_IsNumber(idj)) {
            set_bad_params(err, "input.click: id must be a number");
            return false;
        }
        id = (uint32_t)idj->valuedouble;
    }
    uint8_t buttons = 1U; /* default left */
    const cJSON *bj = cJSON_GetObjectItemCaseSensitive(params, "button");
    if (bj != NULL) {
        if (!cJSON_IsNumber(bj)) {
            set_bad_params(err, "input.click: button must be a number mask");
            return false;
        }
        buttons = (uint8_t)bj->valueint;
    }
    float x = (float)xj->valuedouble;
    float y = (float)yj->valuedouble;
    /* down carries the button mask, up releases. Enqueue down then up so the click is ordered. */
    if (!nt_input_inject_pointer(NT_INJECT_POINTER_DOWN, id, x, y, 1.0F, (uint8_t)NT_POINTER_MOUSE, buttons, 0)) {
        set_bad_params(err, "input.click: inject queue overflow");
        return false;
    }
    if (!nt_input_inject_pointer(NT_INJECT_POINTER_UP, id, x, y, 0.0F, (uint8_t)NT_POINTER_MOUSE, 0, 0)) {
        set_bad_params(err, "input.click: inject queue overflow");
        return false;
    }
    devapi_add_number(result, "queued", 2.0);
    return true;
}

static bool cmd_input_wheel(const cJSON *params, cJSON *result, nt_devapi_error *err, void *ud) {
    (void)ud;
    float dx = 0.0F;
    float dy = 0.0F;
    const cJSON *dxj = cJSON_GetObjectItemCaseSensitive(params, "dx");
    const cJSON *dyj = cJSON_GetObjectItemCaseSensitive(params, "dy");
    if (dxj != NULL) {
        if (!cJSON_IsNumber(dxj)) {
            set_bad_params(err, "input.wheel: dx must be a number");
            return false;
        }
        dx = (float)dxj->valuedouble;
    }
    if (dyj != NULL) {
        if (!cJSON_IsNumber(dyj)) {
            set_bad_params(err, "input.wheel: dy must be a number");
            return false;
        }
        dy = (float)dyj->valuedouble;
    }
    if (!nt_input_inject_wheel(dx, dy, 0)) {
        set_bad_params(err, "input.wheel: inject queue overflow");
        return false;
    }
    devapi_add_bool(result, "ok", true);
    return true;
}

/* sugar: down@0 + a move per point (each on its own frame_offset using frame_stride, default 1) + up.
   NO C interpolation (D-10) — the bot supplies the path samples. Per-entry overflow -> bad_params. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static bool cmd_input_gesture(const cJSON *params, cJSON *result, nt_devapi_error *err, void *ud) {
    (void)ud;
    const cJSON *idj = cJSON_GetObjectItemCaseSensitive(params, "id");
    if (!cJSON_IsNumber(idj)) {
        set_bad_params(err, "input.gesture: id must be a number");
        return false;
    }
    uint32_t id = (uint32_t)idj->valuedouble;
    uint8_t type;
    if (!pointer_type_from_name(cJSON_GetObjectItemCaseSensitive(params, "type"), &type)) {
        set_bad_params(err, "input.gesture: type must be 'mouse', 'touch' or 'pen'");
        return false;
    }
    const cJSON *points = cJSON_GetObjectItemCaseSensitive(params, "points");
    if (!cJSON_IsArray(points)) {
        set_bad_params(err, "input.gesture: points must be an array of [x,y]");
        return false;
    }
    int npoints = cJSON_GetArraySize(points);
    if (npoints < 1) {
        set_bad_params(err, "input.gesture: points must have at least one [x,y]");
        return false;
    }
    uint16_t stride = 1;
    const cJSON *sj = cJSON_GetObjectItemCaseSensitive(params, "frame_stride");
    if (sj != NULL) {
        if (!cJSON_IsNumber(sj)) {
            set_bad_params(err, "input.gesture: frame_stride must be a number");
            return false;
        }
        int s = sj->valueint;
        if (s < 0 || s > UINT16_MAX) {
            set_bad_params(err, "input.gesture: frame_stride out of range [0, 65535]");
            return false;
        }
        stride = (uint16_t)s;
    }
    /* Validate every point up front so a malformed point rejects the whole gesture before any enqueue. */
    const cJSON *p = NULL;
    cJSON_ArrayForEach(p, points) {
        if (!cJSON_IsArray(p) || cJSON_GetArraySize(p) != 2) {
            set_bad_params(err, "input.gesture: each point must be [x,y]");
            return false;
        }
        const cJSON *px = cJSON_GetArrayItem(p, 0);
        const cJSON *py = cJSON_GetArrayItem(p, 1);
        if (!cJSON_IsNumber(px) || !cJSON_IsNumber(py)) {
            set_bad_params(err, "input.gesture: point coords must be numbers");
            return false;
        }
    }
    /* Frame-offset arithmetic guard: (npoints-1)*stride must fit in the uint16 countdown (T-66-04). */
    int last_offset = (npoints - 1) * (int)stride;
    if (last_offset > UINT16_MAX) {
        set_bad_params(err, "input.gesture: span (points*frame_stride) exceeds the frame-offset range");
        return false;
    }
    /* down@0 carrying first point, move per point at its frame offset, up after the last move. */
    const cJSON *first = cJSON_GetArrayItem(points, 0);
    float fx = (float)cJSON_GetArrayItem(first, 0)->valuedouble;
    float fy = (float)cJSON_GetArrayItem(first, 1)->valuedouble;
    if (!nt_input_inject_pointer(NT_INJECT_POINTER_DOWN, id, fx, fy, 1.0F, type, 1U, 0)) {
        set_bad_params(err, "input.gesture: inject queue overflow");
        return false;
    }
    int queued = 1;
    int idx = 0;
    cJSON_ArrayForEach(p, points) {
        float mx = (float)cJSON_GetArrayItem(p, 0)->valuedouble;
        float my = (float)cJSON_GetArrayItem(p, 1)->valuedouble;
        uint16_t at = (uint16_t)(idx * (int)stride);
        if (!nt_input_inject_pointer(NT_INJECT_POINTER_MOVE, id, mx, my, 1.0F, type, 1U, at)) {
            set_bad_params(err, "input.gesture: inject queue overflow");
            return false;
        }
        queued++;
        idx++;
    }
    if (!nt_input_inject_pointer(NT_INJECT_POINTER_UP, id, 0.0F, 0.0F, 0.0F, type, 0, (uint16_t)last_offset)) {
        set_bad_params(err, "input.gesture: inject queue overflow");
        return false;
    }
    queued++;
    devapi_add_number(result, "queued", (double)queued);
    return true;
}

/* mouse-button mask {1,2,4}: inject a pointer event carrying the button mask on the given id. */
static bool cmd_input_button(const cJSON *params, cJSON *result, nt_devapi_error *err, void *ud) {
    (void)ud;
    const cJSON *bj = cJSON_GetObjectItemCaseSensitive(params, "buttons");
    if (!cJSON_IsNumber(bj)) {
        set_bad_params(err, "input.button: buttons must be a number mask");
        return false;
    }
    uint8_t buttons = (uint8_t)bj->valueint;
    uint32_t id = NT_INPUT_INJECT_POINTER_ID_BASE;
    const cJSON *idj = cJSON_GetObjectItemCaseSensitive(params, "id");
    if (idj != NULL) {
        if (!cJSON_IsNumber(idj)) {
            set_bad_params(err, "input.button: id must be a number");
            return false;
        }
        id = (uint32_t)idj->valuedouble;
    }
    /* A move at the current slot position carrying the new button mask (apply_buttons_mask edges). */
    nt_pointer_t *ptr = NULL;
    for (int i = 0; i < NT_INPUT_MAX_POINTERS; i++) {
        if (g_nt_input.pointers[i].active && g_nt_input.pointers[i].id == id) {
            ptr = &g_nt_input.pointers[i];
            break;
        }
    }
    float x = ptr != NULL ? ptr->x : 0.0F;
    float y = ptr != NULL ? ptr->y : 0.0F;
    nt_inject_kind_t kind = ptr != NULL ? NT_INJECT_POINTER_MOVE : NT_INJECT_POINTER_DOWN;
    if (!nt_input_inject_pointer(kind, id, x, y, 1.0F, (uint8_t)NT_POINTER_MOUSE, buttons, 0)) {
        set_bad_params(err, "input.button: inject queue overflow");
        return false;
    }
    devapi_add_bool(result, "ok", true);
    return true;
}

static bool cmd_input_set_player_enabled(const cJSON *params, cJSON *result, nt_devapi_error *err, void *ud) {
    (void)ud;
    const cJSON *e = cJSON_GetObjectItemCaseSensitive(params, "enabled");
    if (!cJSON_IsBool(e)) {
        set_bad_params(err, "input.set_player_enabled: enabled must be a bool");
        return false;
    }
    bool enabled = cJSON_IsTrue(e);
    nt_input_set_player_enabled(enabled);
    devapi_add_bool(result, "enabled", enabled);
    return true;
}

/* input.text: walk the UTF-8 string into a local codepoint buffer (decode 1-4 byte sequences;
   a malformed byte -> bad_params, never assert), then nt_input_inject_text(cps, n) (overflow ->
   bad_params). Preflight is by codepoint count (the char ring is fed across frames, D-13/INPUT-06). */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static bool cmd_input_text(const cJSON *params, cJSON *result, nt_devapi_error *err, void *ud) {
    (void)ud;
    const cJSON *t = cJSON_GetObjectItemCaseSensitive(params, "text");
    if (!cJSON_IsString(t) || t->valuestring == NULL) {
        set_bad_params(err, "input.text: text must be a string");
        return false;
    }
    const unsigned char *s = (const unsigned char *)t->valuestring;
    /* Bounded by the inject queue cap: more codepoints than that can never enqueue whole. */
    uint32_t cps[NT_INPUT_INJECT_QUEUE_MAX];
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
            set_bad_params(err, "input.text: malformed UTF-8 lead byte");
            return false;
        }
        s++;
        for (uint32_t i = 0; i < extra; i++) {
            if ((*s & 0xC0U) != 0x80U) {
                set_bad_params(err, "input.text: malformed UTF-8 continuation byte");
                return false;
            }
            cp = (cp << 6U) | (*s & 0x3FU);
            s++;
        }
        if (n >= NT_INPUT_INJECT_QUEUE_MAX) {
            set_bad_params(err, "input.text: too many codepoints (exceeds inject queue)");
            return false;
        }
        cps[n] = cp;
        n++;
    }
    if (n == 0) {
        set_bad_params(err, "input.text: text must be non-empty");
        return false;
    }
    if (!nt_input_inject_text(cps, n)) {
        set_bad_params(err, "input.text: inject queue overflow");
        return false;
    }
    devapi_add_number(result, "queued", (double)n);
    return true;
}
// #endregion

static const nt_devapi_command_desc k_input_cmds[] = {
    {
        .method = "input.key",
        .group = "input",
        .summary = "inject a key edge (down default true) OR a tap (hold frames -> down@0 + up@hold)",
        .params_shape = "{key:string, down?:bool, hold?:number}",
        .result_shape = "{ok:bool}",
        .frame_behavior = "any",
        .side_effects = "enqueues a synthetic key event",
    },
    {
        .method = "input.pointer",
        .group = "input",
        .summary = "the pointer primitive: action down/move/up on a given id (default mouse type)",
        .params_shape = "{action:string, id:number, x?:number, y?:number, type?:string, buttons?:number}",
        .result_shape = "{queued:number}",
        .frame_behavior = "any",
        .side_effects = "enqueues a synthetic pointer event",
    },
    {
        .method = "input.move",
        .group = "input",
        .summary = "sugar: pointer move on the default mouse slot (reserved id)",
        .params_shape = "{x:number, y:number, id?:number, type?:string}",
        .result_shape = "{queued:number}",
        .frame_behavior = "any",
        .side_effects = "enqueues a synthetic pointer move",
    },
    {
        .method = "input.click",
        .group = "input",
        .summary = "sugar: pointer down + up (2 entries) on the mouse slot with the given button mask",
        .params_shape = "{x:number, y:number, button?:number, id?:number}",
        .result_shape = "{queued:number}",
        .frame_behavior = "any",
        .side_effects = "enqueues a synthetic pointer down+up",
    },
    {
        .method = "input.wheel",
        .group = "input",
        .summary = "inject a mouse-slot wheel delta (notches)",
        .params_shape = "{dx?:number, dy?:number}",
        .result_shape = "{ok:bool}",
        .frame_behavior = "any",
        .side_effects = "enqueues a synthetic wheel event",
    },
    {
        .method = "input.gesture",
        .group = "input",
        .summary = "sugar: down@0 + a move per point (frame_stride apart) + up; NO interpolation",
        .params_shape = "{id:number, type?:string, points:[[x,y]], frame_stride?:number}",
        .result_shape = "{queued:number}",
        .frame_behavior = "any",
        .side_effects = "enqueues an ordered multi-frame pointer gesture",
    },
    {
        .method = "input.button",
        .group = "input",
        .summary = "set the mouse-button mask {1,2,4} on the given id (default reserved mouse slot)",
        .params_shape = "{buttons:number, id?:number}",
        .result_shape = "{ok:bool}",
        .frame_behavior = "any",
        .side_effects = "enqueues a synthetic button-mask pointer event",
    },
    {
        .method = "input.set_player_enabled",
        .group = "input",
        .summary = "toggle the player-input gate (false suppresses the real device; inject still flows)",
        .params_shape = "{enabled:bool}",
        .result_shape = "{enabled:bool}",
        .frame_behavior = "any",
        .side_effects = "toggles the player input gate",
    },
    {
        .method = "input.text",
        .group = "input",
        .summary = "decode a UTF-8 string -> codepoints and enqueue them into the char ring",
        .params_shape = "{text:string}",
        .result_shape = "{queued:number}",
        .frame_behavior = "any",
        .side_effects = "enqueues synthetic typed codepoints",
    },
};

static const nt_devapi_handler_fn k_input_handlers[] = {
    cmd_input_key, cmd_input_pointer, cmd_input_move, cmd_input_click, cmd_input_wheel, cmd_input_gesture, cmd_input_button, cmd_input_set_player_enabled, cmd_input_text,
};
_Static_assert(sizeof(k_input_cmds) / sizeof(k_input_cmds[0]) == sizeof(k_input_handlers) / sizeof(k_input_handlers[0]), "input: descriptor/handler arrays must have equal length");

void nt_devapi_register_input(void) {
    /* Engine-internal dup is a build-time bug → assert NT_OK. Capture first: NT_ASSERT
       compiles out under NT_ASSERT_MODE=0, so the call must not live inside the macro. */
    int n = (int)(sizeof(k_input_cmds) / sizeof(k_input_cmds[0]));
    for (int i = 0; i < n; i++) {
        nt_result_t rr = nt_devapi_register(&k_input_cmds[i], k_input_handlers[i], NULL);
        NT_ASSERT(rr == NT_OK);
        (void)rr;
    }
}

#endif /* NT_DEVAPI_REGISTER_input */
