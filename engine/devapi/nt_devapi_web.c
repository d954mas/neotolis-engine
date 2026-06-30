/* Web (Emscripten) devapi transport — a push/pull ccall bridge instead of a socket (D-16: no
   listener on web). JS pushes a request line through nt_devapi_web_submit and pulls queued deferred
   responses through nt_devapi_web_poll; both return the SAME const char* the core already owns (D-03:
   the core's growing response buffer, valid until the next submit/poll), so the web side adds NO new
   buffer and NO new validation surface — every line goes through the same nt_devapi_submit parse. */

#include <emscripten.h>

#include "devapi/nt_devapi.h"
#include "devapi/nt_devapi_web.h"

/* clang-format off */
/* EM_JS_DEPS keeps UTF8ToString through Closure (Pitfall 2 / R6): the install shim reads the
   const char* the KEEPALIVE wrappers return. ccall (RD-2) marshals the inbound JS string for
   submit, so no malloc/stringToNewUTF8 dep is needed here (D-03 reuses the core buffer). */
EM_JS_DEPS(nt_devapi_web, "$UTF8ToString")

/* Install window.__devapi = {submit, poll, tick, ready}. submit uses ccall so the JS-string `line`
   auto-marshals to a stack UTF8 copy (a bare _nt_devapi_web_submit(line) would NOT convert a JS
   string — RD-2 inbound-string note). NULL return (deferred / nothing ready) maps to "". */
EM_JS(void, nt_devapi_web_install_shim, (void), {
    window.__devapi = {
        ready: true,
        submit: function(line) {
            var p = Module.ccall('nt_devapi_web_submit', 'number', ['string'], [line]);
            return p ? UTF8ToString(p) : "";
        },
        poll: function() {
            var p = _nt_devapi_web_poll();
            return p ? UTF8ToString(p) : "";
        },
        tick: function(n) {
            return window.__devapi.submit(JSON.stringify(
                {method: "time.step", request_id: 0, params: {count: (n || 1)}}));
        }
    };
})
/* clang-format on */

/* JS calls these with a JS string (auto-marshalled to const char* via ccall). Both return the core's
   const char* verbatim — NULL when deferred / nothing ready — and JS maps NULL to "". */
EMSCRIPTEN_KEEPALIVE const char *nt_devapi_web_submit(const char *line) {
    return nt_devapi_submit(line);
}

EMSCRIPTEN_KEEPALIVE const char *nt_devapi_web_poll(void) { return nt_devapi_poll_response(); }
