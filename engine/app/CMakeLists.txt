# Swappable app module
# API header: nt_app.h (shared by both implementations)
# Web impl:    web/nt_app_web.c (RAF loop for WASM -- Emscripten only)
# Native impl: native/nt_app_native.c (while loop -- always available)

if(EMSCRIPTEN)
    nt_add_module(nt_app_web
        web/nt_app_web.c
        nt_app.h
    )
    target_link_libraries(nt_app_web PUBLIC nt_time)
    # RAF callback fires after main() returns -- runtime must stay alive
    target_link_options(nt_app_web INTERFACE "SHELL:-sNO_EXIT_RUNTIME=1")
endif()

nt_add_module(nt_app_native
    native/nt_app_native.c
    nt_app.h
)
target_link_libraries(nt_app_native PUBLIC nt_time)
