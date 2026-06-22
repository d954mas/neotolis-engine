# nt_http -- HTTP request module with swappable backends

# Header-only interface carries nt_http.h's include root; the exe selects the impl.
nt_declare_interface(nt_http)

if(EMSCRIPTEN)
    nt_add_module(nt_http LOG_DOMAIN "http" nt_http.c web/nt_http_web.c nt_http.h)
    # Web backend EM_JS uses wasmExports['malloc'] (Closure-safe) + _free + HEAPU8.
    # The exe MUST call nt_web_exports(<target>) -- emcc collapses EXPORTED_* to one
    # effective `=` list, so a per-module INTERFACE += silently lands in
    # unexportedSymbols (verified). See cmake/nt_web_exports.cmake.
else()
    nt_add_module(nt_http LOG_DOMAIN "http" nt_http.c native/nt_http_native.c nt_http.h)
endif()
target_link_libraries(nt_http PUBLIC nt_core nt_http_interface)

nt_add_module(nt_http_stub LOG_DOMAIN "http_stub" nt_http.c stub/nt_http_stub.c nt_http.h)
target_link_libraries(nt_http_stub PUBLIC nt_core nt_http_interface)
