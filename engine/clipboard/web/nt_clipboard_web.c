#include "clipboard/nt_clipboard.h"

#include <emscripten.h>
#include <string.h>

/* Browser clipboard reads are async (Promise). We never block the frame on one:
 * a DOM `paste` listener caches the pasted text into this bounded buffer, and
 * get_text returns the cache synchronously. ALL Promise/async stays in EM_JS. */

#ifndef NT_CLIPBOARD_WEB_CACHE_SIZE
#define NT_CLIPBOARD_WEB_CACHE_SIZE 4096
#endif

static char s_cache[NT_CLIPBOARD_WEB_CACHE_SIZE] = {0};
static int s_registered = 0;

/* Copy a NUL-terminated UTF-8 string into the cache, clamped so an arbitrarily
 * large external paste can never overflow. Clamp on a byte boundary;
 * a split trailing multi-byte sequence is harmless for display. */
static void cache_store(const char *utf8) {
    if (utf8 == NULL) {
        s_cache[0] = '\0';
        return;
    }
    size_t n = strlen(utf8);
    if (n >= sizeof(s_cache)) {
        n = sizeof(s_cache) - 1;
    }
    memcpy(s_cache, utf8, n);
    s_cache[n] = '\0';
}

/* KEEPALIVE bridge: the DOM paste listener (EM_JS) calls this with the pasted text. */
EMSCRIPTEN_KEEPALIVE void nt_clipboard_web_on_paste(const char *utf8) { cache_store(utf8); }

/* clang-format off */
EM_JS(void, nt_clipboard_web_register, (void), {
    document.addEventListener("paste", function(e) {
        var text = (e.clipboardData || window.clipboardData).getData("text");
        /* Module[...] access: bare globals get renamed by Closure (release). */
        var ptr = Module["stringToNewUTF8"](text);
        Module["_nt_clipboard_web_on_paste"](ptr);
        Module["_free"](ptr);
    });
})

EM_JS(void, nt_clipboard_web_write, (const char *utf8), {
    var text = Module["UTF8ToString"](utf8);
    if (navigator.clipboard && navigator.clipboard.writeText) {
        /* Fire-and-forget: the Promise never crosses back into C. */
        navigator.clipboard.writeText(text);
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
