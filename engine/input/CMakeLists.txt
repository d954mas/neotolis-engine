# nt_input -- input polling with swappable backends

# Synthetic-input inject pipeline (immediate buffer + nt_input_inject_* + per-poll drain) is gated
# behind NT_INPUT_INJECT_ENABLED so a pure release build (devapi OFF) carries none of it. Its sole
# driver is the devapi layer; the test targets also drive the L1 inject API directly, so it is ON
# when devapi is enabled OR when this is a top-level build (NT_BUILDING_TESTS, set by the root
# CMakeLists, which always builds the tests). A pure release-without-devapi build used as a
# subdirectory (NT_DEVAPI_ENABLED OFF, NT_BUILDING_TESTS unset) leaves it OFF.
if(NT_DEVAPI_ENABLED OR NT_BUILDING_TESTS)
    set(NT_INPUT_INJECT_DEFS NT_INPUT_INJECT_ENABLED=1)
else()
    set(NT_INPUT_INJECT_DEFS NT_INPUT_INJECT_ENABLED=0)
endif()

# Header-only interface carries nt_input.h's include root; the exe selects the impl.
nt_declare_interface(nt_input)

if(EMSCRIPTEN)
    nt_add_module(nt_input LOG_DOMAIN "input"
        nt_input.c
        web/nt_input_web.c
        nt_input.h
        nt_input_internal.h
    )
else()
    nt_add_module(nt_input LOG_DOMAIN "input"
        nt_input.c
        native/nt_input_native.c
        nt_input.h
        nt_input_internal.h
    )
endif()

target_link_libraries(nt_input PUBLIC nt_core nt_input_interface)
# PUBLIC: nt_input.h's #if NT_INPUT_INJECT_ENABLED is evaluated in every consumer TU (the devapi
# layer + the input header itself), so the value must propagate to anything that links nt_input.
target_compile_definitions(nt_input PUBLIC ${NT_INPUT_INJECT_DEFS})
if(EMSCRIPTEN)
    # Web: nt_input_web.c reads g_nt_window.dpr for coordinate mapping.
    # g_nt_window resolves at executable link time (exe picks a window impl).
    target_link_libraries(nt_input PUBLIC nt_window_interface)
endif()
# Native: nt_input_native.c reads g_nt_window.dpr at flush time. The header is
# found via the shared engine/ include root; the symbol g_nt_window resolves at
# executable link time (app links a window impl).

# Stub implementation for headless builds (CI, builder, tests)
nt_add_module(nt_input_stub LOG_DOMAIN "input_stub"
    nt_input.c
    stub/nt_input_stub.c
    nt_input.h
)

target_link_libraries(nt_input_stub PUBLIC nt_core nt_input_interface)
target_compile_definitions(nt_input_stub PUBLIC ${NT_INPUT_INJECT_DEFS})
