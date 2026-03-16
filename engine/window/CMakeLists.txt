# nt_window -- canvas/window abstraction

if(EMSCRIPTEN)
    nt_add_module(nt_window
        nt_window.c
        web/nt_window_web.c
        nt_window.h
    )
else()
    nt_add_module(nt_window
        nt_window.c
        native/nt_window_native.c
        nt_window.h
    )
endif()

target_link_libraries(nt_window PUBLIC nt_core)

if(NOT EMSCRIPTEN)
    # PUBLIC: glfw propagates to all modules/examples that link nt_window,
    # so they never need to mention glfw themselves.
    target_link_libraries(nt_window PUBLIC glfw)
    target_link_libraries(nt_window PRIVATE nt_log)
    # NOTE: nt_window_native.c calls nt_input_buffer_* (declared in
    # input/nt_input_internal.h). The header is found via the shared engine/
    # include root. The actual symbols are resolved at executable link time --
    # the app chooses which input backend to link (nt_input or nt_input_stub).
endif()

if(UNIX)
    target_link_libraries(nt_window PRIVATE m)
endif()

# Stub implementation for headless builds (CI, tests)
nt_add_module(nt_window_stub
    nt_window.c
    stub/nt_window_stub.c
    nt_window.h
)

target_link_libraries(nt_window_stub PUBLIC nt_core)

if(UNIX)
    target_link_libraries(nt_window_stub PRIVATE m)
endif()
