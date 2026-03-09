# Native unit tests (Unity framework via CTest)
# WASM tests use a separate Node.js-based approach

if(NOT EMSCRIPTEN)
    add_executable(test_core unit/test_core.c)
    target_link_libraries(test_core PRIVATE nt_core unity)

    # Strict warning and sanitizer policy (same as engine code)
    nt_set_warning_flags(test_core)
    nt_set_sanitizer_flags(test_core)

    # Per-project output: build/tests/[preset]/
    set_target_properties(test_core PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/tests/${NT_PRESET_NAME}"
    )

    # Register with CTest
    add_test(NAME test_core COMMAND test_core)

    # --- Sanitizer proof test ---
    # Intentionally triggers UB to prove ASan+UBSan are working.
    # WILL_FAIL=TRUE: CTest expects this test to fail (sanitizer aborts it).
    add_executable(test_sanitizer_proof unit/test_sanitizer_proof.c)
    target_link_libraries(test_sanitizer_proof PRIVATE nt_core unity)

    nt_set_warning_flags(test_sanitizer_proof)
    nt_set_sanitizer_flags(test_sanitizer_proof)

    set_target_properties(test_sanitizer_proof PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/tests/${NT_PRESET_NAME}"
    )

    add_test(NAME sanitizer_proof COMMAND test_sanitizer_proof)
    set_tests_properties(sanitizer_proof PROPERTIES WILL_FAIL TRUE)
endif()

# --- WASM smoke test (Node.js loading via -sMODULARIZE) ---
if(EMSCRIPTEN)
    add_executable(wasm_smoke wasm/smoke_entry.c)
    target_link_libraries(wasm_smoke PRIVATE nt_core)

    # Strict warning and sanitizer policy (same as engine code)
    nt_set_warning_flags(wasm_smoke)
    nt_set_sanitizer_flags(wasm_smoke)

    # Per-project output: build/tests/[preset]/
    set_target_properties(wasm_smoke PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/tests/${NT_PRESET_NAME}"
    )

    # Node.js-specific WASM link options for smoke testing
    target_link_options(wasm_smoke PRIVATE
        "SHELL:-sMODULARIZE"
        "SHELL:-sEXPORT_NAME=createModule"
        "SHELL:-sENVIRONMENT=node"
        "SHELL:-sEXPORTED_FUNCTIONS=['_main']"
        "SHELL:-sEXPORTED_RUNTIME_METHODS=['ccall']"
    )
endif()
