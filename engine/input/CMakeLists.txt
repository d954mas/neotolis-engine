# nt_input -- input polling with swappable backends

# Synthetic-input inject pipeline (immediate buffer + nt_input_inject_* + per-poll drain) is gated
# behind NT_INPUT_INJECT_ENABLED so a build without the devapi input group carries none of it. Its
# sole production driver is the devapi input group, so the SHARED nt_input lib keys the pipeline on
# NT_DEVAPI_ENABLED AND the input group being ON — a devapi-OFF build, OR a devapi build with the
# input group switched off (NT_DEVAPI_GROUP_INPUT=OFF), gets a lean nt_input with no inject buffer or
# symbols. The test targets drive the L1 inject API directly; they link the headless nt_input_stub,
# which forces the pipeline ON unconditionally (below), so L1-inject tests build regardless.
# NT_INPUT_INJECT_ENABLED is DERIVED here from the devapi input group and is never set by hand: it is
# an nt_input-layer flag (this module must not carry a devapi name), so it stays distinct from the
# NT_DEVAPI_GROUP_INPUT switch that drives it.
if(NT_DEVAPI_ENABLED AND NT_DEVAPI_GROUP_INPUT)
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
# The unit tests (test_input_inject / test_devapi_input) link the stub and drive the L1 inject API
# directly, so the stub ALWAYS carries the inject pipeline regardless of NT_DEVAPI_ENABLED.
target_compile_definitions(nt_input_stub PUBLIC NT_INPUT_INJECT_ENABLED=1)
