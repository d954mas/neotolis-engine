#include "core/nt_core.h"
#include "crash/nt_crash_internal.h"
#include "version/nt_version.h"

#include <emscripten.h>

/* The capture runs in JS after the instance trapped, so it never calls back into wasm: it reads
 * the log ring straight from linear memory and stores the record in localStorage. */

/* clang-format off */
EM_JS_DEPS(nt_crash_web, "$UTF8ToString,$stringToUTF8")

EM_JS(void, nt_crash_web_install, (const char *ring, uint32_t ring_size, const uint32_t *pos, const uint32_t *full,
                                   const char *name, const char *game, const char *engine, const char *preset), {
    var key = UTF8ToString(name);
    var head = "game: " + UTF8ToString(game) + "\nengine: " + UTF8ToString(engine) + " " + UTF8ToString(preset) + "\n";
    var captured = false;
    function cut(text, max_bytes) {
        var bytes = new TextEncoder().encode(text);
        if (bytes.length <= max_bytes) return text;
        var n = max_bytes;
        while (n > 0 && (bytes[n] & 0xC0) === 0x80) n--; /* back off a split UTF-8 sequence */
        return new TextDecoder().decode(bytes.subarray(0, n));
    }
    function log_tail() {
        /* HEAPU8/HEAPU32 are read here, at crash time: memory growth replaces the views. */
        var at = HEAPU32[pos >> 2];
        var wrapped = HEAPU32[full >> 2] !== 0;
        var bytes = new Uint8Array(wrapped ? ring_size : at);
        if (wrapped) {
            bytes.set(HEAPU8.subarray(ring + at, ring + ring_size));
            bytes.set(HEAPU8.subarray(ring, ring + at), ring_size - at);
        } else {
            bytes.set(HEAPU8.subarray(ring, ring + at));
        }
        var first = wrapped ? bytes.indexOf(10) + 1 : 0;
        var last = bytes.lastIndexOf(10) + 1;
        return last > first ? new TextDecoder().decode(bytes.subarray(first, last)) : "";
    }
    function capture(reason) {
        if (captured || !reason || typeof reason.stack !== "string") return;
        /* Best effort: a trap, abort() or a JS error thrown through wasm frames. */
        var stack = reason.stack;
        if (!(reason instanceof WebAssembly.RuntimeError) && stack.indexOf("wasm-function[") < 0 && stack.indexOf("wasm://") < 0 &&
            stack.indexOf("[wasm code]") < 0) return;
        captured = true;
        var text = head + "time: " + Date.now() + "\nreason: " + cut(String(reason), 256) + "\nstack:\n" + cut(stack, 4096) +
                   "\nlog:\n" + log_tail();
        try { window.localStorage.setItem(key, text); } catch (e) {}
        window.dispatchEvent(new CustomEvent("nt-crash", {"detail": {"text": text}}));
    }
    window.addEventListener("error", function(e) { capture(e.error); });
    window.addEventListener("unhandledrejection", function(e) { capture(e.reason); });
})

EM_JS(uint32_t, nt_crash_web_load, (const char *name, char *out, uint32_t cap), {
    var text = null;
    try { text = window.localStorage.getItem(UTF8ToString(name)); } catch (e) {}
    if (text === null) {
        HEAPU8[out] = 0;
        return 0;
    }
    return stringToUTF8(text, out, cap);
})

EM_JS(void, nt_crash_web_clear, (const char *name), {
    try { window.localStorage.removeItem(UTF8ToString(name)); } catch (e) {}
})
/* clang-format on */

void nt_crash_platform_init(void) {
    nt_crash_web_install(g_nt_crash.ring, NT_CRASH_LOG_BYTES, &g_nt_crash.pos, &g_nt_crash.full, g_nt_crash.name, g_nt_crash.game_version, nt_version_engine(), nt_engine_preset_string());
}

uint32_t nt_crash_platform_load(char *out, uint32_t cap) { return nt_crash_web_load(g_nt_crash.name, out, cap); }

void nt_crash_platform_clear(void) { nt_crash_web_clear(g_nt_crash.name); }
