# nt_http -- HTTP request module with swappable backends

if(EMSCRIPTEN)
    nt_add_module(nt_http LOG_DOMAIN "http" nt_http.c web/nt_http_web.c nt_http.h)
    # Web backend EM_JS uses Module._malloc/_free and Module.HEAPU8
    # INTERFACE propagates to any executable linking nt_http
    target_link_options(nt_http INTERFACE
        "SHELL:-sEXPORTED_FUNCTIONS=['_main','_malloc','_free']"
        "SHELL:-sEXPORTED_RUNTIME_METHODS=['HEAPU8']"
    )
else()
    nt_add_module(nt_http LOG_DOMAIN "http" nt_http.c native/nt_http_native.c nt_http.h)
endif()
target_link_libraries(nt_http PUBLIC nt_core)

nt_add_module(nt_http_stub LOG_DOMAIN "http_stub" nt_http.c stub/nt_http_stub.c nt_http.h)
target_link_libraries(nt_http_stub PUBLIC nt_core)
