/* Web (Emscripten) devapi transport — placeholder for the D-18 CMake target split. Plan 02 fleshes
   out the EMSCRIPTEN_KEEPALIVE submit/poll exports + the JS bridge; this file exists now only so the
   if(EMSCRIPTEN) branch in CMakeLists.txt references a real, compilable TU. Web has no socket (D-16):
   the core owns nt_devapi_update + the frame-keyed deferred drain, and web registers no tick hook
   (results stay queued for the JS pull). */

#include <emscripten.h>

#include "devapi/nt_devapi_web.h"

/* EM_JS_DEPS keeps the JS runtime methods Plan 02's bridge needs through Closure; declared now so the
   placeholder mirrors the established web-module pattern (engine/http/web/nt_http_web.c). */
EM_JS_DEPS(nt_devapi_web, "$UTF8ToString,$stringToUTF8,malloc");
