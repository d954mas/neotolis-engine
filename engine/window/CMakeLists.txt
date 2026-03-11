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

if(UNIX)
    target_link_libraries(nt_window PRIVATE m)
endif()
