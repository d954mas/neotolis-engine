#include "clipboard/nt_clipboard.h"

#include <emscripten.h>
#include <stdlib.h>
#include <string.h>

/* Browser clipboard reads are async (Promise). We never block the frame on one:
 * a DOM `paste` listener caches the pasted text into this bounded buffer, and
 * get_text returns the cache synchronously. ALL Promise/async stays in EM_JS. */

#ifndef NT_CLIPBOARD_WEB_CACHE_SIZE
#define NT_CLIPBOARD_WEB_CACHE_SIZE 4096
#endif

static char s_cache[NT_CLIPBOARD_WEB_CACHE_SIZE] = {0};
static int s_registered = 0;

/* Copy a NUL-terminated UTF-8 string into the cache, clamped so an arbitrarily large external
 * paste can never overflow. The clamp backs off a split multi-byte sequence so get_text() always
 * returns valid UTF-8 (the public contract), not a truncated trailing codepoint. */
static void cache_store(const char *utf8) {
    if (utf8 == NULL) {
        s_cache[0] = '\0';
        return;
    }
    size_t n = strlen(utf8);
    if (n >= sizeof(s_cache)) {
        n = sizeof(s_cache) - 1;
        /* Drop a partial trailing codepoint: UTF-8 continuation bytes are 10xxxxxx. */
        while (n > 0U && ((unsigned char)utf8[n] & 0xC0U) == 0x80U) {
            n--;
        }
    }
    memcpy(s_cache, utf8, n);
    s_cache[n] = '\0';
}

/* EM_JS paste listener transfers ownership of a stringToNewUTF8 heap pointer; C frees it here
 * (so JS needs no _free export). */
EMSCRIPTEN_KEEPALIVE void nt_clipboard_web_on_paste(char *utf8) {
    cache_store(utf8);
    free(utf8);
}

/* clang-format off */
/* Force-include the runtime methods this TU's EM_JS bodies reference. EM_JS_DEPS composes across
 * modules (each backend declares its own) and survives Closure: the symbols land in the JS support
 * library, so the bare names below are minified consistently with the rest of the glue -- no
 * EXPORTED_RUNTIME_METHODS list needed. $stringToNewUTF8 transitively pulls malloc. */
EM_JS_DEPS(nt_clipboard_web, "$stringToNewUTF8,$UTF8ToString")

EM_JS(void, nt_clipboard_web_register, (void), {
    document.addEventListener("paste", function(e) {
        var text = (e.clipboardData || window.clipboardData).getData("text");
        /* stringToNewUTF8 allocs on the C heap; nt_clipboard_web_on_paste takes ownership + frees. */
        _nt_clipboard_web_on_paste(stringToNewUTF8(text));
    });
})

EM_JS(void, nt_clipboard_web_write, (const char *utf8), {
    var text = UTF8ToString(utf8);
    if (navigator.clipboard && navigator.clipboard.writeText) {
        /* Best-effort OS write: writeText needs recent user-activation, which may be gone by the
         * frame loop, so the Promise can reject. We swallow it via .catch (no unhandled rejection);
         * in-app copy->paste stays reliable regardless because the paste cache holds the text. */
        navigator.clipboard.writeText(text).catch(function() {});
    }
})
/* clang-format on */

static void ensure_registered(void) {
    if (s_registered) {
        return;
    }
    s_registered = 1;
    nt_clipboard_web_register();
}

/* Register the DOM paste listener EAGERLY at module init so it exists before the user's first
 * Ctrl+V — a lazy registration on the first get_text would miss the paste event that already fired.
 * Runs after the wasm runtime is ready (KEEPALIVE/runtime methods are live) and the DOM exists. */
__attribute__((constructor)) static void nt_clipboard_web_autoregister(void) { ensure_registered(); }

const char *nt_clipboard_get_text(void) {
    ensure_registered();
    return s_cache;
}

void nt_clipboard_set_text(const char *utf8) {
    ensure_registered();
    cache_store(utf8);
    nt_clipboard_web_write(utf8 != NULL ? utf8 : "");
}

/* Could later narrow to navigator.clipboard presence + secure-context, but the paste cache works
 * regardless (DOM paste event), so the façade is always available on web. */
bool nt_clipboard_available(void) { return true; }
