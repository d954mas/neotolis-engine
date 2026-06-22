# nt_http -- HTTP request module with swappable backends

# Header-only interface carries nt_http.h's include root; the exe selects the impl.
nt_declare_interface(nt_http)

if(EMSCRIPTEN)
    nt_add_module(nt_http LOG_DOMAIN "http" nt_http.c web/nt_http_web.c nt_http.h)
    # Web backend self-declares its EM_JS runtime-method deps via EM_JS_DEPS in
    # nt_http_web.c ($UTF8ToString,$HEAPU8,_malloc) -- composes across modules and
    # survives Closure, so no exe-level export list is needed.
else()
    nt_add_module(nt_http LOG_DOMAIN "http" nt_http.c native/nt_http_native.c nt_http.h)
endif()
target_link_libraries(nt_http PUBLIC nt_core nt_http_interface)

nt_add_module(nt_http_stub LOG_DOMAIN "http_stub" nt_http.c stub/nt_http_stub.c nt_http.h)
target_link_libraries(nt_http_stub PUBLIC nt_core nt_http_interface)
