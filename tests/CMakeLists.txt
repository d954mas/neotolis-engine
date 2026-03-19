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

    # --- Transform component tests ---
    add_executable(test_transform_comp unit/test_transform_comp.c)
    target_link_libraries(test_transform_comp PRIVATE nt_transform_comp nt_entity unity)
    nt_set_warning_flags(test_transform_comp)
    nt_set_sanitizer_flags(test_transform_comp)
    set_target_properties(test_transform_comp PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/tests/${NT_PRESET_NAME}"
    )
    add_test(NAME test_transform_comp COMMAND test_transform_comp)

    # --- Component tests (mesh, material, drawable) ---
    add_executable(test_components unit/test_components.c)
    target_link_libraries(test_components PRIVATE nt_mesh_comp nt_material_comp nt_drawable_comp nt_entity nt_hash unity)
    nt_set_warning_flags(test_components)
    nt_set_sanitizer_flags(test_components)
    set_target_properties(test_components PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/tests/${NT_PRESET_NAME}"
    )
    add_test(NAME test_components COMMAND test_components)

    # --- Render items tests ---
    add_executable(test_render_items unit/test_render_items.c)
    target_link_libraries(test_render_items PRIVATE nt_render nt_entity nt_drawable_comp nt_transform_comp nt_hash nt_gfx_stub unity)
    nt_set_warning_flags(test_render_items)
    nt_set_sanitizer_flags(test_render_items)
    set_target_properties(test_render_items PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/tests/${NT_PRESET_NAME}"
    )
    add_test(NAME test_render_items COMMAND test_render_items)

    # --- Material module tests ---
    add_executable(test_material unit/test_material.c)
    target_link_libraries(test_material PRIVATE nt_material nt_resource nt_hash unity)
    target_compile_definitions(test_material PRIVATE _CRT_SECURE_NO_WARNINGS)
    nt_set_warning_flags(test_material)
    nt_set_sanitizer_flags(test_material)
    set_target_properties(test_material PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/tests/${NT_PRESET_NAME}"
    )
    add_test(NAME test_material COMMAND test_material)

    # --- Hash module tests ---
    add_executable(test_hash unit/test_hash.c)
    target_link_libraries(test_hash PRIVATE nt_hash unity)
    target_compile_definitions(test_hash PRIVATE NT_HASH_LABELS=1)
    target_compile_definitions(nt_hash PRIVATE NT_HASH_LABELS=1)
    nt_set_warning_flags(test_hash)
    nt_set_sanitizer_flags(test_hash)
    set_target_properties(test_hash PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/tests/${NT_PRESET_NAME}"
    )
    add_test(NAME test_hash COMMAND test_hash)

    # --- Resource module tests ---
    add_executable(test_resource unit/test_resource.c)
    target_link_libraries(test_resource PRIVATE nt_resource unity)
    target_compile_definitions(test_resource PRIVATE NT_RESOURCE_TEST_ACCESS _CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_DEPRECATE)
    target_compile_options(test_resource PRIVATE -U_DLL)
    target_compile_definitions(nt_resource PRIVATE NT_RESOURCE_TEST_ACCESS)
    nt_set_warning_flags(test_resource)
    nt_set_sanitizer_flags(test_resource)
    set_target_properties(test_resource PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/tests/${NT_PRESET_NAME}"
    )
    add_test(NAME test_resource COMMAND test_resource
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    )

    # --- HTTP module tests ---
    add_executable(test_http unit/test_http.c)
    target_link_libraries(test_http PRIVATE nt_http_stub unity)
    nt_set_warning_flags(test_http)
    nt_set_sanitizer_flags(test_http)
    set_target_properties(test_http PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/tests/${NT_PRESET_NAME}"
    )
    add_test(NAME test_http COMMAND test_http)

    # --- File system module tests ---
    add_executable(test_fs unit/test_fs.c)
    target_link_libraries(test_fs PRIVATE nt_fs unity)
    target_compile_definitions(test_fs PRIVATE _CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_DEPRECATE)
    target_compile_options(test_fs PRIVATE -U_DLL)
    nt_set_warning_flags(test_fs)
    nt_set_sanitizer_flags(test_fs)
    set_target_properties(test_fs PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/tests/${NT_PRESET_NAME}"
    )
    add_test(NAME test_fs COMMAND test_fs
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    )

    # --- Builder tests ---
    add_executable(test_builder unit/test_builder.c)
    target_link_libraries(test_builder PRIVATE nt_builder unity)
    target_compile_definitions(test_builder PRIVATE _CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_DEPRECATE)
    target_compile_options(test_builder PRIVATE -U_DLL)
    nt_set_warning_flags(test_builder)
    nt_set_sanitizer_flags(test_builder)
    set_target_properties(test_builder PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/tests/${NT_PRESET_NAME}"
    )
    add_test(NAME test_builder COMMAND test_builder
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    )

    # --- Mesh renderer tests ---
    add_executable(test_mesh_renderer unit/test_mesh_renderer.c)
    target_link_libraries(test_mesh_renderer PRIVATE
        nt_mesh_renderer nt_render nt_gfx_stub
        nt_entity nt_transform_comp nt_mesh_comp nt_material_comp nt_drawable_comp
        nt_material nt_resource nt_hash unity
    )
    target_compile_definitions(test_mesh_renderer PRIVATE
        NT_MESH_RENDERER_TEST_ACCESS
        _CRT_SECURE_NO_WARNINGS
    )
    nt_set_warning_flags(test_mesh_renderer)
    nt_set_sanitizer_flags(test_mesh_renderer)
    set_target_properties(test_mesh_renderer PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/tests/${NT_PRESET_NAME}"
    )
    add_test(NAME test_mesh_renderer COMMAND test_mesh_renderer)
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