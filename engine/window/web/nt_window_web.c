#include "core/nt_platform.h"

#ifdef NT_PLATFORM_WEB

#include "core/nt_assert.h"
#include "input/nt_input_feed.h"
#include "window/nt_window.h"
#include <emscripten.h>
#include <emscripten/html5.h>

/* ---- EM_JS canvas bridge ---- */

/* clang-format off */
EM_JS(double, nt_window_js_get_dpr, (void), {
    return window.devicePixelRatio || 1.0;
})

EM_JS(double, nt_window_js_get_canvas_width, (void), {
    return Module['canvas'].clientWidth;
})

EM_JS(double, nt_window_js_get_canvas_height, (void), {
    return Module['canvas'].clientHeight;
})

EM_JS(void, nt_window_js_set_backing_size, (int w, int h), {
    Module['canvas'].width = w;
    Module['canvas'].height = h;
})
/* clang-format on */

/* ---- CSS → framebuffer coordinate mapping ---- */

/* ---- Input events (the DOM listeners in nt_window_web_register_input call the dispatcher) ---- */

/* The JS listeners below write these enum values as numbers. */
_Static_assert(NT_INPUT_EV_KEY == 0 && NT_INPUT_EV_CHAR == 1 && NT_INPUT_EV_MOUSE_MOVE == 2 && NT_INPUT_EV_MOUSE_LEAVE == 4 && NT_INPUT_EV_CONTACT_DOWN == 5 && NT_INPUT_EV_CONTACT_MOVE == 6 &&
                   NT_INPUT_EV_CONTACT_UP == 7 && NT_INPUT_EV_WHEEL == 8 && NT_INPUT_EV_FOCUS_LOST == 9,
               "EV_* in nt_window_web_register_input");
_Static_assert(NT_KEY_A == 0 && NT_KEY_0 == 26 && NT_KEY_ARROW_UP == 36 && NT_KEY_SPACE == 40 && NT_KEY_LSHIFT == 45 && NT_KEY_F1 == 51 && NT_KEY_DELETE == 63 && NT_KEY_COUNT == 69 &&
                   NT_POINTER_TOUCH == 1 && NT_POINTER_PEN == 2,
               "codes (nt_key_t order) and the contact type in nt_window_web_register_input");
_Static_assert(NT_TEXT_INPUT_URL == 3 && NT_TEXT_INPUT_PASSWORD == 4, "modes in nt_window_js_set_text_input_mode");

/* One DOM event: kind = nt_input_event_kind_t, a..f its fields, dpr the devicePixelRatio when it fired.
   Coordinates map with that DPR, like native at arrival; fractional, so a sub-pixel move is kept. */
EMSCRIPTEN_KEEPALIVE void nt_window_web_on_input(int kind, double a, double b, double c, double d, double e, double f, double dpr) {
    const float scale = nt_window_effective_dpr((float)dpr);
    nt_input_event_t ev = {.kind = (uint8_t)kind};
    switch ((nt_input_event_kind_t)kind) {
    case NT_INPUT_EV_KEY:
        ev.key.key = (uint8_t)a;
        ev.key.down = b != 0.0;
        break;
    case NT_INPUT_EV_CHAR:
        ev.cp = (uint32_t)a;
        break;
    case NT_INPUT_EV_MOUSE_MOVE:
        ev.mouse.x = (float)a * scale;
        ev.mouse.y = (float)b * scale;
        ev.mouse.buttons = (uint8_t)c;
        break;
    case NT_INPUT_EV_CONTACT_DOWN:
    case NT_INPUT_EV_CONTACT_MOVE:
    case NT_INPUT_EV_CONTACT_UP:
        ev.contact.id = (uint32_t)(int32_t)a; /* pointerId is a signed long */
        ev.contact.x = (float)b * scale;
        ev.contact.y = (float)c * scale;
        ev.contact.pressure = (float)d;
        ev.contact.type = (uint8_t)e;
        ev.contact.buttons = (uint8_t)f;
        break;
    case NT_INPUT_EV_WHEEL:
        ev.wheel.dx = (float)a;
        ev.wheel.dy = (float)b;
        break;
    default:
        break; /* MOUSE_LEAVE and FOCUS_LOST carry nothing; the core asserts an unknown kind */
    }
    nt_input_feed(ev);
}

/* ---- Input event registration ---- */

/* clang-format off */
EM_JS(void, nt_window_web_register_input, (void), {
    var canvas = Module['canvas'];

    /* KeyboardEvent.code in nt_key_t order: keyMap[code] is the enum value */
    var codes = [];
    for (var i = 0; i < 26; i++) {
        codes.push("Key" + String.fromCharCode(65 + i));
    }
    for (var d = 0; d < 10; d++) {
        codes.push("Digit" + d);
    }
    codes.push("ArrowUp", "ArrowDown", "ArrowLeft", "ArrowRight", "Space", "Enter", "Escape", "Tab", "Backspace",
               "ShiftLeft", "ShiftRight", "ControlLeft", "ControlRight", "AltLeft", "AltRight");
    for (var f = 1; f <= 12; f++) {
        codes.push("F" + f);
    }
    codes.push("Delete", "Insert", "Home", "End", "PageUp", "PageDown");
    var keyMap = {};
    codes.forEach(function(c, k) { keyMap[c] = k; });

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

    /* Each event applies to nt_input's next frame as it fires; the published frame changes only at
       nt_input_poll. */
    var onInput = Module['_nt_window_web_on_input'];
    /* nt_input_event_kind_t (input/nt_input_feed.h) */
    var EV_KEY = 0, EV_CHAR = 1, EV_MOUSE_MOVE = 2, EV_MOUSE_LEAVE = 4, EV_CONTACT_DOWN = 5, EV_CONTACT_MOVE = 6,
        EV_CONTACT_UP = 7, EV_WHEEL = 8, EV_FOCUS_LOST = 9;
    function ev(t, a, b, c, d, e, f) {
        onInput(t, a || 0, b || 0, c || 0, d || 0, e || 0, f || 0, window.devicePixelRatio || 1.0);
    }

    /* Keyboard events */
    canvas.addEventListener("keydown", function(e) {
        /* OS auto-repeat (e.repeat) pushes no key event: the field's own timer repeats nav/edit
           keys from is_down. */
        if (!e.repeat) {
            var k = keyMap[e.code];
            if (k !== undefined) {
                ev(EV_KEY, k, 1);
            }
        }
        /* Typed character: e.key is a single printable codepoint (not "Enter"/"ArrowUp"/...).
           Array.from counts CODEPOINTS, so a non-BMP char (e.key.length 2 surrogate pair) still
           passes -- typed text is UTF-32. Exclude Ctrl/Cmd shortcuts so Ctrl+A does not type 'a'. No
           glfwSetCharCallback on web; this hand-rolled path is the char source. Emitted on e.repeat
           too so a held key types a run, matching native GLFW char-repeat. */
        if (Array.from(e.key).length === 1 && !e.ctrlKey && !e.metaKey) {
            ev(EV_CHAR, e.key.codePointAt(0));
        }
        if (preventSet[e.code]) {
            e.preventDefault();
        }
    });

    canvas.addEventListener("keyup", function(e) {
        var k = keyMap[e.code];
        if (k !== undefined) {
            ev(EV_KEY, k, 0);
        }
    });

    /* Blur: held input releases in order with the events around it */
    canvas.addEventListener("blur", function() {
        ev(EV_FOCUS_LOST);
        mouseOwned = false; /* the release may land outside; a held capture or the next wheel must not press again */
        mouseButtons = 0;
    });

    /* Pointer events — pass CSS-relative coords, C maps with engine DPR. Every mouse press, release and
       move is a move with the buttons (a release keeps the mouse), a leave is its own event; touch and
       pen are contacts. The buttons
       count only from a press on the canvas until they are all up, like a native window: a drag from
       outside never presses, and a cancel or a blur never leaves a button stuck. */
    var mouseButtons = 0;
    var mouseOwned = false;
    function mouseMove(e, buttons) {
        mouseButtons = buttons & 7; /* back/forward buttons are not engine buttons */
        mouseOwned = mouseOwned && buttons !== 0; /* a held back button keeps the press on the canvas */
        ev(EV_MOUSE_MOVE, e.clientX - rect.left, e.clientY - rect.top, mouseButtons);
    }

    function ownedButtons(e) {
        return mouseOwned ? e.buttons : 0;
    }

    function isContact(e) {
        return e.pointerType === "touch" || e.pointerType === "pen";
    }

    function contact(type, e) {
        ev(type, e.pointerId, e.clientX - rect.left, e.clientY - rect.top,
           e.pressure, e.pointerType === "pen" ? 2 : 1, e.buttons & 7);
    }

    canvas.addEventListener("pointerdown", function(e) {
        canvas.focus();
        canvas.setPointerCapture(e.pointerId);
        if (isContact(e)) {
            contact(EV_CONTACT_DOWN, e);
        } else {
            mouseOwned = true;
            mouseMove(e, e.buttons);
        }
        e.preventDefault();
    });

    canvas.addEventListener("pointermove", function(e) {
        if (isContact(e)) {
            contact(EV_CONTACT_MOVE, e);
        } else {
            mouseMove(e, ownedButtons(e));
        }
    });

    canvas.addEventListener("pointerup", function(e) {
        if (isContact(e)) {
            contact(EV_CONTACT_UP, e);
        } else {
            mouseMove(e, ownedButtons(e));
        }
        e.preventDefault();
    });

    canvas.addEventListener("pointercancel", function(e) {
        if (isContact(e)) {
            contact(EV_CONTACT_UP, e);
        } else {
            mouseMove(e, 0);
        }
        e.preventDefault();
    });

    canvas.addEventListener("pointerleave", function(e) {
        if (!isContact(e)) {
            ev(EV_MOUSE_LEAVE);
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
        ev(EV_WHEEL, dx, dy);
        e.preventDefault();
    }, {passive: false});

    /* Disable browser touch gestures on canvas */
    canvas.style.touchAction = "none";
})

/* clang-format on */

/* ---- Helpers ---- */

static void sync_sizes(void) {
    float canvas_w = (float)nt_window_js_get_canvas_width();
    float canvas_h = (float)nt_window_js_get_canvas_height();
    float device_dpr = (float)nt_window_js_get_dpr();

    float effective_dpr = nt_window_effective_dpr(device_dpr);

    uint32_t w = (uint32_t)canvas_w;
    uint32_t h = (uint32_t)canvas_h;

    if (w == g_nt_window.width && h == g_nt_window.height && effective_dpr == g_nt_window.dpr) {
        return;
    }

    nt_window_apply_sizes(canvas_w, canvas_h, device_dpr);
    nt_window_js_set_backing_size((int)g_nt_window.fb_width, (int)g_nt_window.fb_height);
}

/* ---- Lifecycle ---- */

void nt_window_init(void) {
    static bool s_registered;
    NT_ASSERT(!s_registered && "the DOM listeners are never removed: init once per page");
    s_registered = true;
    sync_sizes();
    nt_window_web_register_input();
}

void nt_window_poll(void) { sync_sizes(); }

void nt_window_shutdown(void) { /* No-op on web */ }

void nt_window_set_fullscreen(bool fullscreen) {
    /* Browser requires a user gesture (click/key) on the call stack for this
       to succeed. Calling outside a gesture handler is silently ignored. */
    if (fullscreen) {
        emscripten_request_fullscreen("#canvas", true);
    } else {
        emscripten_exit_fullscreen();
    }
}

/* ---- Soft-keyboard hint ---- */

/* clang-format off */
EM_JS(void, nt_window_js_set_text_input_mode, (int mode), {
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
void nt_window_set_text_input_mode(nt_text_input_mode_t mode) { nt_window_js_set_text_input_mode((int)mode); }

/* ---- Presentation ---- */

void nt_window_swap_buffers(void) { /* No-op: browser swaps after rAF return */ }

void nt_window_set_vsync(nt_vsync_t mode) { (void)mode; /* No-op: browser controls vsync */ }

/* ---- Close management ---- */

bool nt_window_should_close(void) { return false; /* Web apps don't close via window API */ }

void nt_window_request_close(void) { /* No-op on web */ }

#else
typedef int nt_window_web_unused;
#endif /* NT_PLATFORM_WEB */
