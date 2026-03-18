# nt_http -- HTTP request module with swappable backends

if(EMSCRIPTEN)
    nt_add_module(nt_http nt_http.c web/nt_http_web.c nt_http.h)
else()
    nt_add_module(nt_http nt_http.c native/nt_http_native.c nt_http.h)
endif()
target_link_libraries(nt_http PUBLIC nt_core)

nt_add_module(nt_http_stub nt_http.c stub/nt_http_stub.c nt_http.h)
target_link_libraries(nt_http_stub PUBLIC nt_core)
