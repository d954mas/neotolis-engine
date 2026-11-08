# nt_input -- input state: platform events apply to the next frame, nt_input_poll publishes it. One
# platform-free implementation: the window module's platform pump feeds it.

nt_add_module(nt_input LOG_DOMAIN "input"
    nt_input.c
    nt_input.h
    nt_input_internal.h
)

target_link_libraries(nt_input PUBLIC nt_core)
