# Native unit tests (Unity framework via CTest)
# WASM tests run through CTest via Node.js when available

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

    # --- Swap demo tests ---
    # Same source, different implementation linked.
    # Proves: swapping = one target_link_libraries change.

    # Swap test: real implementation (printf-based)
    add_executable(test_log_real unit/test_log_swap.c)
    target_link_libraries(test_log_real PRIVATE nt_log unity)
    nt_set_warning_flags(test_log_real)
    nt_set_sanitizer_flags(test_log_real)
    set_target_properties(test_log_real PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/tests/${NT_PRESET_NAME}"
    )
    add_test(NAME test_log_real COMMAND test_log_real)

    # Swap test: stub implementation (no-op, same source)
    add_executable(test_log_stub unit/test_log_swap.c)
    target_link_libraries(test_log_stub PRIVATE nt_log_stub unity)
    nt_set_warning_flags(test_log_stub)
    nt_set_sanitizer_flags(test_log_stub)
    set_target_properties(test_log_stub PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/tests/${NT_PRESET_NAME}"
    )
    add_test(NAME test_log_stub COMMAND test_log_stub)

    # --- Time module tests ---
    add_executable(test_time unit/test_time.c)
    target_link_libraries(test_time PRIVATE nt_time unity)
    nt_set_warning_flags(test_time)
    nt_set_sanitizer_flags(test_time)
    set_target_properties(test_time PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/tests/${NT_PRESET_NAME}"
    )
    add_test(NAME test_time COMMAND test_time)

    # --- App module tests ---
    add_executable(test_app unit/test_app.c)
    target_link_libraries(test_app PRIVATE nt_app_stub unity)
    nt_set_warning_flags(test_app)
    nt_set_sanitizer_flags(test_app)
    set_target_properties(test_app PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/tests/${NT_PRESET_NAME}"
    )
    add_test(NAME test_app COMMAND test_app)

    # --- Window module tests ---
    add_executable(test_window unit/test_window.c)
    target_link_libraries(test_window PRIVATE nt_window_stub unity)
    nt_set_warning_flags(test_window)
    nt_set_sanitizer_flags(test_window)
    set_target_properties(test_window PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/tests/${NT_PRESET_NAME}"
    )
    add_test(NAME test_window COMMAND test_window)

    # --- Native window smoke test (real GLFW, hidden window) ---
    add_executable(test_window_native unit/test_window_native.c)
    target_link_libraries(test_window_native PRIVATE nt_window nt_input_stub nt_log unity)
    nt_set_warning_flags(test_window_native)
    nt_set_sanitizer_flags(test_window_native)
    set_target_properties(test_window_native PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/tests/${NT_PRESET_NAME}"
    )
    add_test(NAME test_window_native COMMAND test_window_native)
    set_tests_properties(test_window_native PROPERTIES LABELS "native")

    # --- Input module tests ---
    add_executable(test_input unit/test_input.c)
    target_link_libraries(test_input PRIVATE nt_input_stub unity)
    nt_set_warning_flags(test_input)
    nt_set_sanitizer_flags(test_input)
    set_target_properties(test_input PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/tests/${NT_PRESET_NAME}"
    )
    add_test(NAME test_input COMMAND test_input)

    # --- Graphics module tests ---
    add_executable(test_gfx unit/test_gfx.c)
    target_link_libraries(test_gfx PRIVATE nt_gfx_stub unity)
    nt_set_warning_flags(test_gfx)
    nt_set_sanitizer_flags(test_gfx)
    set_target_properties(test_gfx PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/tests/${NT_PRESET_NAME}"
    )
    add_test(NAME test_gfx COMMAND test_gfx)

    # --- Shape module tests ---
    add_executable(test_shape unit/test_shape.c)
    target_link_libraries(test_shape PRIVATE nt_shape_renderer nt_gfx_stub unity)
    target_compile_definitions(test_shape PRIVATE NT_SHAPE_RENDERER_TEST_ACCESS)
    nt_set_warning_flags(test_shape)
    nt_set_sanitizer_flags(test_shape)
    set_target_properties(test_shape PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/tests/${NT_PRESET_NAME}"
    )
    add_test(NAME test_shape COMMAND test_shape)

    # --- Pack format tests ---
    add_executable(test_pack_format unit/test_pack_format.c)
    target_link_libraries(test_pack_format PRIVATE nt_shared unity)
    nt_set_warning_flags(test_pack_format)
    nt_set_sanitizer_flags(test_pack_format)
    set_target_properties(test_pack_format PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/tests/${NT_PRESET_NAME}"
    )
    add_test(NAME test_pack_format COMMAND test_pack_format)

    # --- Entity module tests ---
    add_executable(test_entity unit/test_entity.c)
    target_link_libraries(test_entity PRIVATE nt_entity unity)
    nt_set_warning_flags(test_entity)
    nt_set_sanitizer_flags(test_entity)
    set_target_properties(test_entity PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/tests/${NT_PRESET_NAME}"
    )
    add_test(NAME test_entity COMMAND test_entity)
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
    find_program(NT_NODE_EXECUTABLE NAMES node nodejs)
    if(NT_NODE_EXECUTABLE)
        add_test(
            NAME test_wasm_smoke
            COMMAND
                "${NT_NODE_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/wasm/smoke_test.js"
                "$<TARGET_FILE:wasm_smoke>"
        )
    else()
        message(STATUS "Node.js not found; skipping CTest registration for test_wasm_smoke")
    endif()
endif()