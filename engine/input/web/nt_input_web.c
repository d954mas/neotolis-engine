#include "input/nt_input_internal.h"
#include "window/nt_window.h"

#include <emscripten.h>
#include <math.h>

/* ---- CSS → framebuffer coordinate mapping ---- */

static void map_css_to_fb(float css_x, float css_y, float *fb_x, float *fb_y) {
    float scale = g_nt_window.dpr; /* effective DPR = min(device_dpr, max_dpr) */
    *fb_x = roundf(css_x * scale);
    *fb_y = roundf(css_y * scale);
}

/* ---- Drained events (one ordered JS queue, see nt_input_web_register_listeners) ---- */

/* Stride-7 records [type, a, b, c, d, e, f]; the JS listeners push these numeric types. */
typedef enum {
    WEB_CONTACT_DOWN = 0, /* id, x, y, pressure, type, buttons */
    WEB_CONTACT_MOVE = 1, /* id, x, y, pressure, -, buttons */
    WEB_CONTACT_UP = 2,   /* id */
    WEB_MOUSE_MOVE = 3,   /* x, y, buttons */
    WEB_MOUSE_LEAVE = 4,
    WEB_KEY = 5,   /* key, down */
    WEB_CHAR = 6,  /* codepoint */
    WEB_WHEEL = 7, /* dx, dy in notches */
    WEB_BLUR = 8,
} web_event_t;

EMSCRIPTEN_KEEPALIVE void nt_input_web_on_event(int type, double a, double b, double c, double d, double e, double f) {
    float fx = 0.0F;
    float fy = 0.0F;
    switch ((web_event_t)type) {
    case WEB_CONTACT_DOWN:
        map_css_to_fb((float)b, (float)c, &fx, &fy);
        nt_input_pointer_down((uint32_t)a, fx, fy, (float)d, (uint8_t)e, (uint8_t)f);
        break;
    case WEB_CONTACT_MOVE:
        map_css_to_fb((float)b, (float)c, &fx, &fy);
        nt_input_pointer_move((uint32_t)a, fx, fy, (float)d, (uint8_t)f);
        break;
    case WEB_CONTACT_UP:
        nt_input_pointer_up((uint32_t)a);
        break;
    case WEB_MOUSE_MOVE:
        map_css_to_fb((float)a, (float)b, &fx, &fy);
        nt_input_mouse_move(fx, fy, (uint8_t)c);
        break;
    case WEB_MOUSE_LEAVE:
        nt_input_mouse_leave();
        break;
    case WEB_KEY:
        nt_input_set_key((nt_key_t)a, b != 0.0);
        break;
    case WEB_CHAR:
        nt_input_buffer_char((uint32_t)a);
        break;
    case WEB_WHEEL:
        nt_input_wheel((float)a, (float)b);
        break;
    case WEB_BLUR:
        nt_input_focus_lost();
        break;
    }
}

/* ---- Soft-keyboard hint ---- */

/* clang-format off */
EM_JS(void, nt_input_web_set_text_input_mode, (int mode), {
    var modes = ['text', 'numeric', 'email', 'url', 'text'];
    var el = Module['canvas'] || document.activeElement;
    if (!el) { return; }
    el.setAttribute('inputmode', modes[mode] || 'text');
    if (mode === 4 /* NT_TEXT_INPUT_PASSWORD */) {
        el.setAttribute('type', 'password');
    } else {
        el.removeAttribute('type');
    }
})
/* clang-format on */

/* Map the field's text-input mode to the web on-screen keyboard (canvas inputmode/type). */
void nt_input_set_text_input_mode(nt_text_input_mode_t mode) { nt_input_web_set_text_input_mode((int)mode); }

/* ---- EM_JS event registration ---- */

/* clang-format off */
EM_JS(void, nt_input_web_register_listeners, (void), {
    var canvas = Module['canvas'];

    /* Key mapping: KeyboardEvent.code -> nt_key_t enum value */
    var keyMap = {};
    var letters = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    for (var i = 0; i < 26; i++) {
        keyMap["Key" + letters[i]] = i;
    }
    for (var d = 0; d < 10; d++) {
        keyMap["Digit" + d] = 26 + d;
    }
    keyMap["ArrowUp"]    = 36;
    keyMap["ArrowDown"]  = 37;
    keyMap["ArrowLeft"]  = 38;
    keyMap["ArrowRight"] = 39;
    keyMap["Space"]      = 40;
    keyMap["Enter"]      = 41;
    keyMap["Escape"]     = 42;
    keyMap["Tab"]        = 43;
    keyMap["Backspace"]  = 44;
    keyMap["ShiftLeft"]  = 45;
    keyMap["ShiftRight"] = 46;
    keyMap["ControlLeft"]  = 47;
    keyMap["ControlRight"] = 48;
    keyMap["AltLeft"]  = 49;
    keyMap["AltRight"] = 50;
    for (var f = 1; f <= 12; f++) {
        keyMap["F" + f] = 50 + f;
    }
    keyMap["Delete"]   = 63;
    keyMap["Insert"]   = 64;
    keyMap["Home"]     = 65;
    keyMap["End"]      = 66;
    keyMap["PageUp"]   = 67;
    keyMap["PageDown"] = 68;

    /* Keys that need preventDefault to avoid browser defaults */
    var preventSet = {
        "Space": 1, "Tab": 1,
        "ArrowUp": 1, "ArrowDown": 1, "ArrowLeft": 1, "ArrowRight": 1,
        "F1": 1, "F2": 1, "F3": 1, "F4": 1,
        "F6": 1, "F7": 1, "F8": 1, "F9": 1, "F10": 1
    };

    /* Cached canvas rect — updated on resize */
    var rect = canvas.getBoundingClientRect();
    new ResizeObserver(function() {
        rect = canvas.getBoundingClientRect();
    }).observe(canvas);

    /* One ordered queue of stride-7 records (web_event_t in C), drained in nt_input_poll. Listeners
       only push, so C never runs inside a DOM callback. */
    var q = [];
    Module['_ntInputEvents'] = q;
    function ev(t, a, b, c, d, e, f) {
        q.push(t, a || 0, b || 0, c || 0, d || 0, e || 0, f || 0);
    }

    /* Keyboard events */
    canvas.addEventListener("keydown", function(e) {
        /* First press emits the key event; OS auto-repeat (e.repeat) does NOT — nav/edit-key
           repeat is the input field's own timer (is_down), so re-pushing key edges would double it. */
        if (!e.repeat) {
            var k = keyMap[e.code];
            if (k !== undefined) {
                ev(5, k, 1);
            }
        }
        /* Typed character: e.key is a single printable codepoint (not "Enter"/"ArrowUp"/...).
           Array.from counts CODEPOINTS, so a non-BMP char (e.key.length 2 surrogate pair) still
           passes -- the ring is UTF-32. Exclude Ctrl/Cmd shortcuts so Ctrl+A does not type 'a'. No
           glfwSetCharCallback on web; this hand-rolled path is the char source. Emitted on e.repeat
           too so a held key types a run, matching native GLFW char-repeat. */
        if (Array.from(e.key).length === 1 && !e.ctrlKey && !e.metaKey) {
            ev(6, e.key.codePointAt(0));
        }
        if (preventSet[e.code]) {
            e.preventDefault();
        }
    });

    canvas.addEventListener("keyup", function(e) {
        var k = keyMap[e.code];
        if (k !== undefined) {
            ev(5, k, 0);
        }
    });

    /* Blur: held input releases in order with the events around it */
    canvas.addEventListener("blur", function() {
        ev(8);
        mouseOwned = false; /* the release may land outside; a held capture or the next wheel must not press again */
        mouseButtons = 0;
    });

    /* Pointer events — pass CSS-relative coords, C maps with engine DPR. Every mouse event is a move
       with the buttons (a release keeps the mouse); touch and pen are contacts. The buttons
       count only from a press on the canvas until they are all up, like a native window: a drag from
       outside never presses, and a cancel or a blur never leaves a button stuck. */
    var mouseButtons = 0;
    var mouseOwned = false;
    function mouseMove(e, buttons) {
        mouseButtons = buttons & 7; /* back/forward buttons are not engine buttons */
        mouseOwned = mouseOwned && mouseButtons !== 0;
        ev(3, e.clientX - rect.left, e.clientY - rect.top, mouseButtons);
    }

    function ownedButtons(e) {
        return mouseOwned ? e.buttons : 0;
    }

    function isContact(e) {
        return e.pointerType === "touch" || e.pointerType === "pen";
    }

    function contact(type, e) {
        ev(type, e.pointerId, e.clientX - rect.left, e.clientY - rect.top,
           e.pressure, e.pointerType === "pen" ? 2 : 1, e.buttons);
    }

    canvas.addEventListener("pointerdown", function(e) {
        canvas.focus();
        canvas.setPointerCapture(e.pointerId);
        if (isContact(e)) {
            contact(0, e);
        } else {
            mouseOwned = true;
            mouseMove(e, e.buttons);
        }
        e.preventDefault();
    });

    canvas.addEventListener("pointermove", function(e) {
        if (isContact(e)) {
            contact(1, e);
        } else {
            mouseMove(e, ownedButtons(e));
        }
    });

    canvas.addEventListener("pointerup", function(e) {
        if (isContact(e)) {
            contact(2, e);
        } else {
            mouseMove(e, ownedButtons(e));
        }
        e.preventDefault();
    });

    canvas.addEventListener("pointercancel", function(e) {
        if (isContact(e)) {
            contact(2, e);
        } else {
            mouseMove(e, 0);
        }
        e.preventDefault();
    });

    canvas.addEventListener("pointerleave", function(e) {
        if (!isContact(e)) {
            ev(4);
        }
    });

    /* Wheel event (passive: false for preventDefault). Normalize to the nt_input NOTCH unit
     * (1.0 == one detent) so wheel strength is platform-independent. deltaMode: 0 = pixels
     * (~120/notch), 1 = lines (~3/notch), 2 = pages (1 page ~ 1 notch). Trackpads send small
     * pixel deltas -> fractional notches. */
    var WHEEL_PX_PER_NOTCH = 120.0;
    var WHEEL_LINES_PER_NOTCH = 3.0;
    canvas.addEventListener("wheel", function(e) {
        var dx = e.deltaX;
        var dy = e.deltaY;
        if (e.deltaMode === 1) {
            dx /= WHEEL_LINES_PER_NOTCH;
            dy /= WHEEL_LINES_PER_NOTCH;
        } else if (e.deltaMode === 2) {
            dx = Math.sign(dx);
            dy = Math.sign(dy);
        } else {
            dx /= WHEEL_PX_PER_NOTCH;
            dy /= WHEEL_PX_PER_NOTCH;
        }
        mouseMove(e, mouseButtons); /* the wheel lands on the mouse at the cursor: a bot may have left it elsewhere */
        ev(7, dx, dy);
        e.preventDefault();
    }, {passive: false});

    /* Disable browser touch gestures on canvas */
    canvas.style.touchAction = "none";
})

EM_JS(void, nt_input_web_flush_events, (void), {
    var q = Module['_ntInputEvents'];
    var on_event = Module['_nt_input_web_on_event'];
    for (var i = 0; i < q.length; i += 7) {
        on_event(q[i], q[i + 1], q[i + 2], q[i + 3], q[i + 4], q[i + 5], q[i + 6]);
    }
    q.length = 0;
})
/* clang-format on */

/* ---- Platform lifecycle ---- */

void nt_input_platform_init(void) { nt_input_web_register_listeners(); }

void nt_input_platform_poll(void) { nt_input_web_flush_events(); }
