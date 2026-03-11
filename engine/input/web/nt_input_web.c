#include "input/nt_input.h"
#include <emscripten.h>

/* ---- KEEPALIVE wrappers for JS event handlers ---- */

EMSCRIPTEN_KEEPALIVE void nt_input_web_on_key(int key, int down) { nt_input_set_key((nt_key_t)key, down != 0); }

EMSCRIPTEN_KEEPALIVE void nt_input_web_on_pointer_down(int id, float cx, float cy, float pressure, int ptype, int buttons) {
    nt_input_pointer_down((uint32_t)id, cx, cy, pressure, (uint8_t)ptype, (uint8_t)buttons);
}

EMSCRIPTEN_KEEPALIVE void nt_input_web_on_pointer_move(int id, float cx, float cy, float pressure, int buttons) { nt_input_pointer_move((uint32_t)id, cx, cy, pressure, (uint8_t)buttons); }

EMSCRIPTEN_KEEPALIVE void nt_input_web_on_pointer_up(int id) { nt_input_pointer_up((uint32_t)id); }

EMSCRIPTEN_KEEPALIVE void nt_input_web_on_wheel(float dx, float dy) { nt_input_wheel(dx, dy); }

EMSCRIPTEN_KEEPALIVE void nt_input_web_on_blur(void) { nt_input_clear_all_keys(); }

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

    /* Event accumulation buffers */
    Module._ntInputKeyEvents = [];
    Module._ntInputPointerEvents = [];
    Module._ntInputWheelEvents = [];
    Module._ntInputBlurred = false;

    /* Keyboard events */
    canvas.addEventListener("keydown", function(e) {
        if (e.repeat) return;
        var k = keyMap[e.code];
        if (k !== undefined) {
            Module._ntInputKeyEvents.push({key: k, down: 1});
        }
        if (preventSet[e.code]) {
            e.preventDefault();
        }
    });

    canvas.addEventListener("keyup", function(e) {
        var k = keyMap[e.code];
        if (k !== undefined) {
            Module._ntInputKeyEvents.push({key: k, down: 0});
        }
    });

    /* Blur: clear all keys on focus loss */
    canvas.addEventListener("blur", function() {
        Module._ntInputBlurred = true;
    });

    /* CSS -> framebuffer coordinate mapping */
    function toFB(cssX, cssY) {
        var dpr = window.devicePixelRatio || 1;
        return [Math.round((cssX - rect.left) * dpr),
                Math.round((cssY - rect.top) * dpr)];
    }

    /* Pointer events */
    canvas.addEventListener("pointerdown", function(e) {
        var fb = toFB(e.clientX, e.clientY);
        var ptype = e.pointerType === "touch" ? 1
                  : (e.pointerType === "pen" ? 2 : 0);
        Module._ntInputPointerEvents.push({
            type: 0, id: e.pointerId,
            x: fb[0], y: fb[1],
            pressure: e.pressure, ptype: ptype,
            buttons: e.buttons
        });
        e.preventDefault();
    });

    canvas.addEventListener("pointermove", function(e) {
        var fb = toFB(e.clientX, e.clientY);
        var ptype = e.pointerType === "touch" ? 1
                  : (e.pointerType === "pen" ? 2 : 0);
        Module._ntInputPointerEvents.push({
            type: 1, id: e.pointerId,
            x: fb[0], y: fb[1],
            pressure: e.pressure, ptype: ptype,
            buttons: e.buttons
        });
    });

    canvas.addEventListener("pointerup", function(e) {
        Module._ntInputPointerEvents.push({type: 2, id: e.pointerId});
        e.preventDefault();
    });

    canvas.addEventListener("pointercancel", function(e) {
        Module._ntInputPointerEvents.push({type: 2, id: e.pointerId});
        e.preventDefault();
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
        Module._ntInputWheelEvents.push({dx: dx, dy: dy});
        e.preventDefault();
    }, {passive: false});

    /* Disable browser touch gestures on canvas */
    canvas.style.touchAction = "none";
})

EM_JS(void, nt_input_web_flush_events, (void), {
    /* Blur handling: clear all keys */
    if (Module._ntInputBlurred) {
        Module._nt_input_web_on_blur();
        Module._ntInputBlurred = false;
    }

    /* Drain key events */
    var keys = Module._ntInputKeyEvents;
    for (var i = 0; i < keys.length; i++) {
        Module._nt_input_web_on_key(keys[i].key, keys[i].down);
    }
    Module._ntInputKeyEvents = [];

    /* Drain pointer events */
    var ptrs = Module._ntInputPointerEvents;
    for (var i = 0; i < ptrs.length; i++) {
        var ev = ptrs[i];
        if (ev.type === 0) {
            Module._nt_input_web_on_pointer_down(
                ev.id, ev.x, ev.y, ev.pressure, ev.ptype, ev.buttons);
        } else if (ev.type === 1) {
            Module._nt_input_web_on_pointer_move(
                ev.id, ev.x, ev.y, ev.pressure, ev.buttons);
        } else {
            Module._nt_input_web_on_pointer_up(ev.id);
        }
    }
    Module._ntInputPointerEvents = [];

    /* Drain wheel events */
    var wheels = Module._ntInputWheelEvents;
    for (var i = 0; i < wheels.length; i++) {
        Module._nt_input_web_on_wheel(wheels[i].dx, wheels[i].dy);
    }
    Module._ntInputWheelEvents = [];
})
/* clang-format on */

/* ---- Platform lifecycle ---- */

void nt_input_platform_init(void) { nt_input_web_register_listeners(); }

void nt_input_platform_poll(void) { nt_input_web_flush_events(); }

void nt_input_platform_shutdown(void) { /* Listeners GC'd with module unload */ }
