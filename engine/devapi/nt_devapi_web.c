/* Web (Emscripten) devapi transport: a push/pull ccall bridge, NO socket/listener. submit/poll return
   the core's OWN response buffer (valid until the next submit/poll), so the web side adds no new buffer
   and no new validation surface — every line still goes through nt_devapi_submit. */

#include <emscripten.h>

#include "devapi/nt_devapi.h"
#include "devapi/nt_devapi_internal.h"
#include "devapi/nt_devapi_web.h"

/* clang-format off */
/* EM_JS_DEPS keeps UTF8ToString and ccall through Closure: the install shim reads the const char*
   the KEEPALIVE wrappers return, and ccall marshals the inbound JS string for submit, so no
   malloc/stringToNewUTF8 dep is needed here (reuses the core buffer). Module.ccall exists only
   when exported; release renames it. */
EM_JS_DEPS(nt_devapi_web, "$UTF8ToString,$ccall")

/* Install window.__devapi = {submit, poll, reset, tick, ready}. Every page-visible name is quoted so
   Closure (release --closure 1) keeps it. submit uses ccall so the JS-string `line` auto-marshals to a
   stack UTF8 copy (a bare _nt_devapi_web_submit(line) would NOT convert a JS string). NULL return
   (deferred / nothing ready) maps to "". */
EM_JS(void, nt_devapi_web_install_shim, (void), {
    var step_rid = 0;
    var submit = function(line) {
        var p = ccall('nt_devapi_web_submit', 'number', ['string'], [line]);
        return p ? UTF8ToString(p) : "";
    };
    window['__devapi'] = {
        'ready': true,
        'submit': submit,
        'poll': function() {
            var p = _nt_devapi_web_poll();
            return p ? UTF8ToString(p) : "";
        },
        'reset': function() {
            _nt_devapi_web_reset();
        },
        /* Internal MANUAL-step pump. time.step DEFERS (its reply arrives later via poll), so use a
           distinct DECREASING request_id that never collides with a caller's positive id; a caller that
           does not await it ignores it by id (the Python pump drops these negative-id replies). */
        'tick': function(n) {
            step_rid -= 1;
            return submit(JSON.stringify(
                {'method': 'time.step', 'request_id': step_rid, 'params': {'count': (n || 1)}}));
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

EMSCRIPTEN_KEEPALIVE void nt_devapi_web_reset(void) {
    nt_devapi_deferred_reset();
    nt_devapi_run_reset_hooks();
}
