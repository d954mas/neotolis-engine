# nt_window -- canvas/window abstraction

# Header-only interface carries nt_window.h's include root; the exe selects the impl.
nt_declare_interface(nt_window)

if(NOT EMSCRIPTEN)
    # glfw is the fixed native windowing dep (not a swappable nt_ module). Modules
    # that include <GLFW/glfw3.h> (e.g. nt_gfx native ctx) get the include path from
    # the interface; the glfw library itself resolves at exe link via the real impl.
    target_include_directories(nt_window_interface INTERFACE
        $<TARGET_PROPERTY:glfw,INTERFACE_INCLUDE_DIRECTORIES>)
endif()

if(EMSCRIPTEN)
    nt_add_module(nt_window LOG_DOMAIN "window"
        nt_window.c
        web/nt_window_web.c
        nt_window.h
    )
else()
    nt_add_module(nt_window LOG_DOMAIN "window"
        nt_window.c
        native/nt_window_native.c
        nt_window.h
    )
endif()

target_link_libraries(nt_window PUBLIC nt_core nt_window_interface)

if(NOT EMSCRIPTEN)
    # PUBLIC: glfw propagates to all modules/examples that link nt_window,
    # so they never need to mention glfw themselves.
    target_link_libraries(nt_window PUBLIC glfw)
    target_link_libraries(nt_window PRIVATE nt_log_interface)
    # NOTE: nt_window_native.c calls nt_input_buffer_* (declared in
    # input/nt_input_internal.h). The header is found via the shared engine/
    # include root. The actual symbols are resolved at executable link time --
    # the app chooses which input backend to link (nt_input or nt_input_stub).
endif()

if(UNIX)
    target_link_libraries(nt_window PRIVATE m)
endif()

# Stub implementation for headless builds (CI, tests)
nt_add_module(nt_window_stub LOG_DOMAIN "window_stub"
    nt_window.c
    stub/nt_window_stub.c
    nt_window.h
)

target_link_libraries(nt_window_stub PUBLIC nt_core nt_window_interface)

if(UNIX)
    target_link_libraries(nt_window_stub PRIVATE m)
endif()
