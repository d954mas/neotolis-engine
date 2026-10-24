/* Web (Emscripten) devapi transport: a push/pull bridge, NO socket/listener. submit/poll return the
   core's OWN response buffer (valid until the next submit/poll), so the web side adds no response
   buffer and no new validation surface — every line still goes through nt_devapi_submit. */

#include <emscripten.h>
#include <stdlib.h>

#include "devapi/nt_devapi.h"
#include "devapi/nt_devapi_internal.h"
#include "devapi/nt_devapi_web.h"

/* JS-only entries (reached through window.__devapi, never named by the host). submit takes ownership
   of a stringToNewUTF8 line and frees it; both return the core's own buffer, NULL when deferred /
   nothing ready. */
EMSCRIPTEN_KEEPALIVE const char *nt_devapi_web_submit(char *line);
EMSCRIPTEN_KEEPALIVE const char *nt_devapi_web_poll(void);
EMSCRIPTEN_KEEPALIVE void nt_devapi_web_reset(void);

/* clang-format off */
/* The line goes through the heap, not ccall's wasm-stack copy: a request may exceed the 64 KiB stack.
   Names are quoted so release --closure 1 keeps them. */
EM_JS_DEPS(nt_devapi_web, "$stringToNewUTF8,$UTF8ToString")

EM_JS(void, nt_devapi_web_install_js, (void), {
    window['__devapi'] = {
        'submit': function(line) { return UTF8ToString(_nt_devapi_web_submit(stringToNewUTF8(line))); },
        'poll': function() { return UTF8ToString(_nt_devapi_web_poll()); },
        'reset': function() { _nt_devapi_web_reset(); }
    };
})
/* clang-format on */

/* A defined C entry: the EM_JS symbol alone is an import, so a host call to it would not pull this
   archive member (or its KEEPALIVE exports) into the link. */
void nt_devapi_web_install_shim(void) {
    nt_devapi_web_install_js();
}

const char *nt_devapi_web_submit(char *line) {
    const char *resp = nt_devapi_submit(line);
    free(line);
    return resp;
}

const char *nt_devapi_web_poll(void) { return nt_devapi_poll_response(); }

void nt_devapi_web_reset(void) {
    nt_devapi_deferred_reset();
    nt_devapi_run_reset_hooks();
}
