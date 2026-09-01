# nt_http -- HTTP request module with swappable backends

# Header-only interface carries nt_http.h's include root; the exe selects the impl.
nt_declare_interface(nt_http)

if(EMSCRIPTEN)
    nt_add_module(nt_http LOG_DOMAIN "http" nt_http.c web/nt_http_web.c nt_http.h)
    # Web backend self-declares its EM_JS runtime-method deps via EM_JS_DEPS in
    # nt_http_web.c ($UTF8ToString,malloc) -- composes across modules and
    # survives Closure, so no exe-level export list is needed.
elseif(NT_HTTP_CURL)
    nt_add_module(nt_http LOG_DOMAIN "http" nt_http.c native/nt_http_native.c nt_http.h)
    # Static libcurl (deps/curl): PRIVATE keeps curl headers out of consumers,
    # $<LINK_ONLY> still carries the archives into the final exe link.
    target_link_libraries(nt_http PRIVATE CURL::libcurl)
    # Mark curl includes as SYSTEM so clang-tidy and -Werror skip them (idiom:
    # engine/math does the same for cglm).
    get_target_property(_curl_inc CURL::libcurl INTERFACE_INCLUDE_DIRECTORIES)
    if(_curl_inc)
        target_include_directories(nt_http SYSTEM PRIVATE ${_curl_inc})
    endif()
else()
    # NT_HTTP_CURL=OFF: native nt_http is the always-fail stub backend. Loud on both
    # ends — the define makes nt_http_init WARN at runtime so a build without
    # networking is diagnosable at init, not at the first FAILED request.
    message(STATUS "nt_http: native backend is the always-fail stub (NT_HTTP_CURL=OFF)")
    nt_add_module(nt_http LOG_DOMAIN "http" nt_http.c stub/nt_http_stub.c nt_http.h)
    target_compile_definitions(nt_http PRIVATE NT_HTTP_STUB_IS_FALLBACK)
endif()
target_link_libraries(nt_http PUBLIC nt_core nt_http_interface)

nt_add_module(nt_http_stub LOG_DOMAIN "http_stub" nt_http.c stub/nt_http_stub.c nt_http.h)
target_link_libraries(nt_http_stub PUBLIC nt_core nt_http_interface)
