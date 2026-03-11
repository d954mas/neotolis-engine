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

EMSCRIPTEN_KEEPALIVE void nt_input_web_on_pointer_down(int id, float cx, float cy, float pressure, int ptype, int buttons) {
    float fx;
    float fy;
    map_css_to_fb(cx, cy, &fx, &fy);
    nt_input_pointer_down((uint32_t)id, fx, fy, pressure, (uint8_t)ptype, (uint8_t)buttons);
}

EMSCRIPTEN_KEEPALIVE void nt_input_web_on_pointer_move(int id, float cx, float cy, float pressure, int ptype, int buttons) {
    float fx;
    float fy;
    map_css_to_fb(cx, cy, &fx, &fy);
    nt_input_pointer_move((uint32_t)id, fx, fy, pressure, (uint8_t)ptype, (uint8_t)buttons);
}

EMSCRIPTEN_KEEPALIVE void nt_input_web_on_pointer_up(int id) { nt_input_pointer_up((uint32_t)id); }

EMSCRIPTEN_KEEPALIVE void nt_input_web_on_wheel(float dx, float dy) { nt_input_wheel(dx, dy); }

EMSCRIPTEN_KEEPALIVE void nt_input_web_on_blur(void) {
    nt_input_clear_all_keys();
    nt_input_clear_all_pointers();
}

/* ---- EM_JS event registration ---- */

/* clang-format off */
EM_JS(void, nt_input_web_register_listeners, (void), {
    var canvas = Module.canvas;

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
       Wheel:   stride 2  [dx, dy, ...] */
    Module._ntKeyBuf = [];
    Module._ntPtrBuf = [];
    Module._ntWheelBuf = [];
    Module._ntBlurred = false;

    /* Keyboard events */
    canvas.addEventListener("keydown", function(e) {
        if (e.repeat) return;
        var k = keyMap[e.code];
        if (k !== undefined) {
            Module._ntKeyBuf.push(k, 1);
        }
        if (preventSet[e.code]) {
            e.preventDefault();
        }
    });

    canvas.addEventListener("keyup", function(e) {
        var k = keyMap[e.code];
        if (k !== undefined) {
            Module._ntKeyBuf.push(k, 0);
        }
    });

    /* Blur: clear all keys on focus loss */
    canvas.addEventListener("blur", function() {
        Module._ntBlurred = true;
    });

    /* Pointer events — pass CSS-relative coords, C maps with engine DPR */
    canvas.addEventListener("pointerdown", function(e) {
        canvas.focus();
        var ptype = e.pointerType === "touch" ? 1
                  : (e.pointerType === "pen" ? 2 : 0);
        Module._ntPtrBuf.push(0, e.pointerId,
            e.clientX - rect.left, e.clientY - rect.top,
            e.pressure, ptype, e.buttons);
        e.preventDefault();
    });

    canvas.addEventListener("pointermove", function(e) {
        var ptype = e.pointerType === "touch" ? 1
                  : (e.pointerType === "pen" ? 2 : 0);
        Module._ntPtrBuf.push(1, e.pointerId,
            e.clientX - rect.left, e.clientY - rect.top,
            e.pressure, ptype, e.buttons);
    });

    canvas.addEventListener("pointerup", function(e) {
        if (e.pointerType === "mouse") {
            /* Mouse stays active after button release — send move with buttons=0 */
            Module._ntPtrBuf.push(1, e.pointerId,
                e.clientX - rect.left, e.clientY - rect.top,
                e.pressure, 0, e.buttons);
        } else {
            Module._ntPtrBuf.push(2, e.pointerId, 0, 0, 0, 0, 0);
        }
        e.preventDefault();
    });

    canvas.addEventListener("pointercancel", function(e) {
        Module._ntPtrBuf.push(2, e.pointerId, 0, 0, 0, 0, 0);
        e.preventDefault();
    });

    /* Mouse leaves canvas — deactivate slot */
    canvas.addEventListener("pointerleave", function(e) {
        if (e.pointerType === "mouse") {
            Module._ntPtrBuf.push(2, e.pointerId, 0, 0, 0, 0, 0);
        }
    });

    /* Wheel event (passive: false for preventDefault) */
    canvas.addEventListener("wheel", function(e) {
        var dx = e.deltaX;
        var dy = e.deltaY;
        if (e.deltaMode === 1) {
            dx *= 16.0;
            dy *= 16.0;
        } else if (e.deltaMode === 2) {
            dx *= window.innerWidth;
            dy *= window.innerHeight;
        }
        Module._ntWheelBuf.push(dx, dy);
        e.preventDefault();
    }, {passive: false});

    /* Disable browser touch gestures on canvas */
    canvas.style.touchAction = "none";
})

EM_JS(void, nt_input_web_flush_events, (void), {
    /* Blur handling */
    if (Module._ntBlurred) {
        Module._nt_input_web_on_blur();
        Module._ntBlurred = false;
    }

    /* Drain key buffer (stride 2: key, down) */
    var kb = Module._ntKeyBuf;
    for (var i = 0; i < kb.length; i += 2) {
        Module._nt_input_web_on_key(kb[i], kb[i + 1]);
    }
    kb.length = 0;

    /* Drain pointer buffer (stride 7: type, id, x, y, pressure, ptype, buttons) */
    var pb = Module._ntPtrBuf;
    for (var i = 0; i < pb.length; i += 7) {
        if (pb[i] === 0) {
            Module._nt_input_web_on_pointer_down(
                pb[i + 1], pb[i + 2], pb[i + 3], pb[i + 4], pb[i + 5], pb[i + 6]);
        } else if (pb[i] === 1) {
            Module._nt_input_web_on_pointer_move(
                pb[i + 1], pb[i + 2], pb[i + 3], pb[i + 4], pb[i + 5], pb[i + 6]);
        } else {
            Module._nt_input_web_on_pointer_up(pb[i + 1]);
        }
    }
    pb.length = 0;

    /* Drain wheel buffer (stride 2: dx, dy) */
    var wb = Module._ntWheelBuf;
    for (var i = 0; i < wb.length; i += 2) {
        Module._nt_input_web_on_wheel(wb[i], wb[i + 1]);
    }
    wb.length = 0;
})
/* clang-format on */

/* ---- Platform lifecycle ---- */

void nt_input_platform_init(void) { nt_input_web_register_listeners(); }

void nt_input_platform_poll(void) { nt_input_web_flush_events(); }

void nt_input_platform_shutdown(void) { /* Listeners GC'd with module unload */ }
