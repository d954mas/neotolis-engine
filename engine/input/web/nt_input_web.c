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

/* ---- KEEPALIVE wrappers for JS event handlers ---- */

EMSCRIPTEN_KEEPALIVE void nt_input_web_on_key(int key, int down) { nt_input_set_key((nt_key_t)key, down != 0); }

EMSCRIPTEN_KEEPALIVE void nt_input_web_on_char(unsigned int cp) { nt_input_buffer_char((uint32_t)cp); }

EMSCRIPTEN_KEEPALIVE void nt_input_web_on_mouse_move(float cx, float cy, int buttons) {
    float fx;
    float fy;
    map_css_to_fb(cx, cy, &fx, &fy);
    nt_input_mouse_move(fx, fy, (uint8_t)buttons);
}

EMSCRIPTEN_KEEPALIVE void nt_input_web_on_mouse_leave(void) { nt_input_mouse_leave(); }

EMSCRIPTEN_KEEPALIVE void nt_input_web_on_pointer_down(int id, float cx, float cy, float pressure, int ptype, int buttons) {
    float fx;
    float fy;
    map_css_to_fb(cx, cy, &fx, &fy);
    nt_input_pointer_down((uint32_t)id, fx, fy, pressure, (uint8_t)ptype, (uint8_t)buttons);
}

EMSCRIPTEN_KEEPALIVE void nt_input_web_on_pointer_move(int id, float cx, float cy, float pressure, int buttons) {
    float fx;
    float fy;
    map_css_to_fb(cx, cy, &fx, &fy);
    nt_input_pointer_move((uint32_t)id, fx, fy, pressure, (uint8_t)buttons);
}

EMSCRIPTEN_KEEPALIVE void nt_input_web_on_pointer_up(int id) { nt_input_pointer_up((uint32_t)id); }

EMSCRIPTEN_KEEPALIVE void nt_input_web_on_wheel(float dx, float dy) { nt_input_wheel(dx, dy); }

EMSCRIPTEN_KEEPALIVE void nt_input_web_on_blur(void) { nt_input_focus_lost(); }

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

    /* Flat event buffers — no object allocation per event.
       Key:     stride 2  [key, down, ...]
       Pointer: stride 7  [type, id, x, y, pressure, ptype, buttons, ...]
                type 0/1/2 = contact down/move/up, 3 = mouse move, 4 = mouse leave
       Wheel:   stride 2  [dx, dy, ...] */
    Module['_ntKeyBuf'] = [];
    Module['_ntCharBuf'] = [];
    Module['_ntPtrBuf'] = [];
    Module['_ntWheelBuf'] = [];
    Module['_ntBlurred'] = false;

    /* Keyboard events */
    canvas.addEventListener("keydown", function(e) {
        /* First press emits the key event; OS auto-repeat (e.repeat) does NOT — nav/edit-key
           repeat is the input field's own timer (is_down), so re-pushing key edges would double it. */
        if (!e.repeat) {
            var k = keyMap[e.code];
            if (k !== undefined) {
                Module['_ntKeyBuf'].push(k, 1);
            }
        }
        /* Typed character: e.key is a single printable codepoint (not "Enter"/"ArrowUp"/...).
           Array.from counts CODEPOINTS, so a non-BMP char (e.key.length 2 surrogate pair) still
           passes -- the ring is UTF-32. Exclude Ctrl/Cmd shortcuts so Ctrl+A does not type 'a'. No
           glfwSetCharCallback on web; this hand-rolled path is the char source. Emitted on e.repeat
           too so a held key types a run, matching native GLFW char-repeat. */
        if (Array.from(e.key).length === 1 && !e.ctrlKey && !e.metaKey) {
            Module['_ntCharBuf'].push(e.key.codePointAt(0));
        }
        if (preventSet[e.code]) {
            e.preventDefault();
        }
    });

    canvas.addEventListener("keyup", function(e) {
        var k = keyMap[e.code];
        if (k !== undefined) {
            Module['_ntKeyBuf'].push(k, 0);
        }
    });

    /* Blur: clear all keys on focus loss */
    canvas.addEventListener("blur", function() {
        Module['_ntBlurred'] = true;
        mouseOwned = false; /* the release may land outside; a held capture or the next wheel must not press again */
        mouseButtons = 0;
    });

    /* Pointer events — pass CSS-relative coords, C maps with engine DPR. Every mouse event is a move
       with the buttons (type 3; a release keeps the mouse); touch and pen are contacts. The buttons
       count only from a press on the canvas until they are all up, like a native window: a drag from
       outside never presses, and a cancel, a lost capture or a blur never leaves a button stuck. */
    var mouseButtons = 0;
    var mouseOwned = false;
    function mouseMove(e, buttons) {
        mouseButtons = buttons & 7; /* back/forward buttons are not engine buttons */
        mouseOwned = mouseOwned && mouseButtons !== 0;
        Module['_ntPtrBuf'].push(3, 0, e.clientX - rect.left, e.clientY - rect.top, 0, 0, mouseButtons);
    }

    function ownedButtons(e) {
        return mouseOwned ? e.buttons : 0;
    }

    function isContact(e) {
        return e.pointerType === "touch" || e.pointerType === "pen";
    }

    function contact(type, e) {
        Module['_ntPtrBuf'].push(type, e.pointerId,
            e.clientX - rect.left, e.clientY - rect.top,
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
            var x = e.clientX - rect.left;
            var y = e.clientY - rect.top;
            if (mouseButtons === 0 && (x < 0 || y < 0 || x >= rect.width || y >= rect.height)) {
                Module['_ntPtrBuf'].push(4, 0, 0, 0, 0, 0, 0); /* released outside: not every browser sends a pointerleave after a captured drag */
            }
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
            Module['_ntPtrBuf'].push(4, 0, 0, 0, 0, 0, 0);
        }
    });

    canvas.addEventListener("lostpointercapture", function(e) {
        if (!isContact(e) && mouseButtons !== 0) {
            mouseMove(e, 0); /* capture taken away mid-drag: the release will not reach the canvas */
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
        Module['_ntWheelBuf'].push(dx, dy);
        e.preventDefault();
    }, {passive: false});

    /* Disable browser touch gestures on canvas */
    canvas.style.touchAction = "none";
})

EM_JS(void, nt_input_web_flush_events, (void), {
    /* Blur handling — discard stale pre-blur events */
    if (Module['_ntBlurred']) {
        Module['_nt_input_web_on_blur']();
        Module['_ntBlurred'] = false;
        Module['_ntKeyBuf'].length = 0;
        Module['_ntCharBuf'].length = 0;
        Module['_ntPtrBuf'].length = 0;
        Module['_ntWheelBuf'].length = 0;
        return;
    }

    /* Drain key buffer (stride 2: key, down) */
    var kb = Module['_ntKeyBuf'];
    for (var i = 0; i < kb.length; i += 2) {
        Module['_nt_input_web_on_key'](kb[i], kb[i + 1]);
    }
    kb.length = 0;

    /* Drain char buffer (stride 1: UTF-32 codepoint) */
    var cb = Module['_ntCharBuf'];
    for (var i = 0; i < cb.length; i++) {
        Module['_nt_input_web_on_char'](cb[i]);
    }
    cb.length = 0;

    /* Drain pointer buffer (stride 7: type, id, x, y, pressure, ptype, buttons) */
    var pb = Module['_ntPtrBuf'];
    for (var i = 0; i < pb.length; i += 7) {
        if (pb[i] === 0) {
            Module['_nt_input_web_on_pointer_down'](
                pb[i + 1], pb[i + 2], pb[i + 3], pb[i + 4], pb[i + 5], pb[i + 6]);
        } else if (pb[i] === 1) {
            Module['_nt_input_web_on_pointer_move'](
                pb[i + 1], pb[i + 2], pb[i + 3], pb[i + 4], pb[i + 6]);
        } else if (pb[i] === 2) {
            Module['_nt_input_web_on_pointer_up'](pb[i + 1]);
        } else if (pb[i] === 3) {
            Module['_nt_input_web_on_mouse_move'](pb[i + 2], pb[i + 3], pb[i + 6]);
        } else {
            Module['_nt_input_web_on_mouse_leave']();
        }
    }
    pb.length = 0;

    /* Drain wheel buffer (stride 2: dx, dy) */
    var wb = Module['_ntWheelBuf'];
    for (var i = 0; i < wb.length; i += 2) {
        Module['_nt_input_web_on_wheel'](wb[i], wb[i + 1]);
    }
    wb.length = 0;
})
/* clang-format on */

/* ---- Platform lifecycle ---- */

void nt_input_platform_init(void) { nt_input_web_register_listeners(); }

void nt_input_platform_poll(void) { nt_input_web_flush_events(); }

void nt_input_platform_shutdown(void) { /* Listeners GC'd with module unload */ }
