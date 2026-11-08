# nt_input -- input state: platform events apply to the next frame, nt_input_poll publishes it

# nt_input automation surface (player gate + inject pipeline) is gated behind
# NT_INPUT_AUTOMATION_ENABLED so a build without the devapi input group is the lean apply layer (no
# gate, no inject symbols). DERIVED from the devapi input group, never hand-set: it is an
# nt_input-layer flag (this module must not carry a devapi name), distinct from NT_DEVAPI_GROUP_INPUT.
if(NT_DEVAPI_ENABLED AND NT_DEVAPI_GROUP_INPUT)
    set(NT_INPUT_AUTOMATION_DEFS NT_INPUT_AUTOMATION_ENABLED=1)
else()
    set(NT_INPUT_AUTOMATION_DEFS NT_INPUT_AUTOMATION_ENABLED=0)
endif()

# Header-only interface carries nt_input.h's include root; the exe selects the impl. The input core is
# platform-free: the window module's platform pump feeds it.
nt_declare_interface(nt_input)

nt_add_module(nt_input LOG_DOMAIN "input"
    nt_input.c
    nt_input.h
    nt_input_internal.h
)

target_link_libraries(nt_input PUBLIC nt_core nt_input_interface)
# PUBLIC: nt_input.h's #if NT_INPUT_AUTOMATION_ENABLED is evaluated in every consumer TU (the devapi
# layer + the input header itself), so the value must propagate to anything that links nt_input.
target_compile_definitions(nt_input PUBLIC ${NT_INPUT_AUTOMATION_DEFS})

# The same core with the automation surface always compiled, for tests that drive the inject API and
# the player gate regardless of NT_DEVAPI_ENABLED.
nt_add_module(nt_input_automation LOG_DOMAIN "input"
    nt_input.c
    nt_input.h
)

target_link_libraries(nt_input_automation PUBLIC nt_core nt_input_interface)
target_compile_definitions(nt_input_automation PUBLIC NT_INPUT_AUTOMATION_ENABLED=1)
