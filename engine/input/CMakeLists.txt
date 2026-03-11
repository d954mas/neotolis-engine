# nt_input -- input polling with swappable backends

# Stub implementation for headless builds and testing
nt_add_module(nt_input_stub
    nt_input.c
    stub/nt_input_stub.c
    nt_input.h
)
target_link_libraries(nt_input_stub PUBLIC nt_core nt_window)

# Platform-conditional backends (Plan 02 will populate)
if(EMSCRIPTEN)
    # nt_input with web backend (Plan 02)
else()
    # nt_input with native backend (Plan 02)
endif()
