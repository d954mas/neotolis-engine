# nt_app -- frame loop, one implementation per platform

if(EMSCRIPTEN)
    nt_add_module(nt_app
        nt_app.c
        web/nt_app_web.c
        nt_app.h
    )
    # RAF callback fires after main() returns -- runtime must stay alive
    target_link_options(nt_app INTERFACE "SHELL:-sNO_EXIT_RUNTIME=1")
else()
    nt_add_module(nt_app
        nt_app.c
        native/nt_app_native.c
        nt_app.h
    )
endif()

target_link_libraries(nt_app PUBLIC nt_time)

if(NOT EMSCRIPTEN)
    target_link_libraries(nt_app PRIVATE nt_window)
endif()

# Stub implementation for headless builds (CI, tests)
nt_add_module(nt_app_stub
    nt_app.c
    stub/nt_app_stub.c
    nt_app.h
)

target_link_libraries(nt_app_stub PUBLIC nt_time)
