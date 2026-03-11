# nt_input -- input polling with swappable backends

if(EMSCRIPTEN)
    nt_add_module(nt_input
        nt_input.c
        web/nt_input_web.c
        nt_input.h
    )
else()
    nt_add_module(nt_input
        nt_input.c
        native/nt_input_native.c
        nt_input.h
    )
endif()

target_link_libraries(nt_input PUBLIC nt_core nt_window)

# Stub implementation for headless builds (CI, builder, tests)
nt_add_module(nt_input_stub
    nt_input.c
    stub/nt_input_stub.c
    nt_input.h
)

target_link_libraries(nt_input_stub PUBLIC nt_core nt_window)
