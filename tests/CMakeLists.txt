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

    # --- Log API tests (domain functions, macros, level filtering) ---
    add_executable(test_log_api unit/test_log_api.c)
    target_link_libraries(test_log_api PRIVATE nt_log unity)
    target_compile_definitions(test_log_api PRIVATE NT_LOG_DOMAIN_DEFAULT="test_mod")
    nt_set_warning_flags(test_log_api)
    nt_set_sanitizer_flags(test_log_api)
    set_target_properties(test_log_api PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/tests/${NT_PRESET_NAME}"
    )
    add_test(NAME test_log_api COMMAND test_log_api)

    # --- Log API tests with stub backend ---
    add_executable(test_log_api_stub unit/test_log_api.c)
    target_link_libraries(test_log_api_stub PRIVATE nt_log_stub unity)
    target_compile_definitions(test_log_api_stub PRIVATE NT_LOG_DOMAIN_DEFAULT="test_mod")
    nt_set_warning_flags(test_log_api_stub)
    nt_set_sanitizer_flags(test_log_api_stub)
    set_target_properties(test_log_api_stub PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/tests/${NT_PRESET_NAME}"
    )
    add_test(NAME test_log_api_stub COMMAND test_log_api_stub)

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

    # --- Graphics scissor/viewport tests ---
    # Plan 03 added restore_gpu cases that call into nt_sprite_renderer +
    # nt_text_renderer; the renderer libs (and their transitive deps for
    # font/resource/hash/pool/material) are required at link time.
    add_executable(test_nt_gfx_scissor unit/test_nt_gfx_scissor.c)
    target_link_libraries(test_nt_gfx_scissor PRIVATE
        nt_sprite_renderer nt_text_renderer nt_font nt_material
        nt_resource nt_hash nt_pool nt_gfx_stub unity
    )
    target_compile_definitions(test_nt_gfx_scissor PRIVATE NT_GFX_TEST_ACCESS _CRT_SECURE_NO_WARNINGS)
    target_compile_definitions(nt_gfx_stub PRIVATE NT_GFX_TEST_ACCESS)
    target_compile_options(test_nt_gfx_scissor PRIVATE -U_DLL)
    nt_set_warning_flags(test_nt_gfx_scissor)
    nt_set_sanitizer_flags(test_nt_gfx_scissor)
    set_target_properties(test_nt_gfx_scissor PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/tests/${NT_PRESET_NAME}"
    )
    add_test(NAME test_nt_gfx_scissor COMMAND test_nt_gfx_scissor)

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

    # --- Sort module tests ---
    add_executable(test_sort unit/test_sort.c)
    target_link_libraries(test_sort PRIVATE nt_sort unity)
    nt_set_warning_flags(test_sort)
    nt_set_sanitizer_flags(test_sort)
    set_target_properties(test_sort PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/tests/${NT_PRESET_NAME}"
    )
    add_test(NAME test_sort COMMAND test_sort)

    # --- Font module tests ---
    add_executable(test_font unit/test_font.c)
    target_link_libraries(test_font PRIVATE nt_font nt_gfx_stub nt_resource nt_hash nt_pool unity)
    target_compile_definitions(test_font PRIVATE NT_FONT_TEST_ACCESS _CRT_SECURE_NO_WARNINGS)
    target_compile_options(test_font PRIVATE -U_DLL)
    target_compile_definitions(nt_font PRIVATE NT_FONT_TEST_ACCESS)
    target_compile_options(nt_font PRIVATE -U_DLL)
    nt_set_warning_flags(test_font)
    nt_set_sanitizer_flags(test_font)
    set_target_properties(test_font PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/tests/${NT_PRESET_NAME}"
    )
    add_test(NAME test_font COMMAND test_font)

    # --- Font measure benchmark ---
    # Measures hit vs miss ns/call for the nt_font_measure_n LRU cache.
    # Uses nt_time_nanos() for the ns clock (same module used by test_time).
    add_executable(test_nt_font_measure_bench unit/test_nt_font_measure_bench.c)
    target_link_libraries(test_nt_font_measure_bench PRIVATE nt_font nt_gfx_stub nt_resource nt_hash nt_pool nt_time unity)
    target_compile_definitions(test_nt_font_measure_bench PRIVATE NT_FONT_TEST_ACCESS _CRT_SECURE_NO_WARNINGS)
    target_compile_options(test_nt_font_measure_bench PRIVATE -U_DLL)
    nt_set_warning_flags(test_nt_font_measure_bench)
    nt_set_sanitizer_flags(test_nt_font_measure_bench)
    set_target_properties(test_nt_font_measure_bench PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/tests/${NT_PRESET_NAME}"
    )
    add_test(NAME test_nt_font_measure_bench COMMAND test_nt_font_measure_bench)

    # --- Text renderer module tests ---
    add_executable(test_text_renderer unit/test_text_renderer.c)
    target_link_libraries(test_text_renderer PRIVATE nt_text_renderer nt_font nt_gfx_stub nt_resource nt_hash nt_pool nt_material nt_time unity)
    target_compile_definitions(test_text_renderer PRIVATE NT_TEXT_RENDERER_TEST_ACCESS NT_FONT_TEST_ACCESS _CRT_SECURE_NO_WARNINGS)
    target_compile_definitions(nt_text_renderer PRIVATE NT_TEXT_RENDERER_TEST_ACCESS)
    target_compile_options(test_text_renderer PRIVATE -U_DLL)
    nt_set_warning_flags(test_text_renderer)
    nt_set_sanitizer_flags(test_text_renderer)
    set_target_properties(test_text_renderer PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/tests/${NT_PRESET_NAME}"
    )
    add_test(NAME test_text_renderer COMMAND test_text_renderer)

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

    # --- Atlas module tests ---
    add_executable(test_atlas unit/test_atlas.c)
    target_link_libraries(test_atlas PRIVATE nt_atlas nt_resource nt_hash unity)
    target_compile_definitions(test_atlas PRIVATE NT_ATLAS_TEST_ACCESS _CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_DEPRECATE)
    target_compile_options(test_atlas PRIVATE -U_DLL)
    target_compile_definitions(nt_atlas PRIVATE NT_ATLAS_TEST_ACCESS)
    nt_set_warning_flags(test_atlas)
    nt_set_sanitizer_flags(test_atlas)
    set_target_properties(test_atlas PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/tests/${NT_PRESET_NAME}"
    )
    add_test(NAME test_atlas COMMAND test_atlas)

    # --- Sprite component tests ---
    add_executable(test_sprite_comp unit/test_sprite_comp.c)
    target_link_libraries(test_sprite_comp PRIVATE
        nt_sprite_comp nt_atlas nt_entity nt_resource nt_hash nt_pool nt_shared unity
    )
    target_compile_definitions(test_sprite_comp PRIVATE NT_ATLAS_TEST_ACCESS _CRT_SECURE_NO_WARNINGS)
    target_compile_options(test_sprite_comp PRIVATE -U_DLL)
    nt_set_warning_flags(test_sprite_comp)
    nt_set_sanitizer_flags(test_sprite_comp)
    set_target_properties(test_sprite_comp PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/tests/${NT_PRESET_NAME}"
    )
    add_test(NAME test_sprite_comp COMMAND test_sprite_comp)

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

    # --- Sprite renderer tests ---
    add_executable(test_sprite_renderer unit/test_sprite_renderer.c)
    target_link_libraries(test_sprite_renderer PRIVATE
        nt_sprite_renderer nt_atlas nt_sprite_comp nt_transform_comp
        nt_drawable_comp nt_material_comp nt_material nt_render
        nt_resource nt_hash nt_pool nt_entity nt_shared nt_gfx_stub unity
    )
    target_compile_definitions(test_sprite_renderer PRIVATE
        NT_SPRITE_RENDERER_TEST_ACCESS
        NT_ATLAS_TEST_ACCESS
        NT_GFX_STUB_TEST_ACCESS
        NT_GFX_TEST_ACCESS
        _CRT_SECURE_NO_WARNINGS
        _CRT_NONSTDC_NO_DEPRECATE
    )
    target_compile_definitions(nt_sprite_renderer PRIVATE NT_SPRITE_RENDERER_TEST_ACCESS)
    # nt_gfx_stub also compiles nt_gfx.c — needs NT_GFX_TEST_ACCESS for the
    # real-backend test hook (sampler cache inspection) the test uses.
    target_compile_definitions(nt_gfx_stub PRIVATE NT_GFX_STUB_TEST_ACCESS NT_GFX_TEST_ACCESS)
    target_compile_definitions(nt_gfx PRIVATE NT_GFX_TEST_ACCESS)
    target_compile_options(test_sprite_renderer PRIVATE -U_DLL)
    nt_set_warning_flags(test_sprite_renderer)
    nt_set_sanitizer_flags(test_sprite_renderer)
    set_target_properties(test_sprite_renderer PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/tests/${NT_PRESET_NAME}"
    )
    add_test(NAME test_sprite_renderer COMMAND test_sprite_renderer)

    # --- Stats module tests (DEMO-04..06 + Pitfall 9 / Issue 2 coverage) ---
    add_executable(test_stats unit/test_stats.c)
    target_link_libraries(test_stats PRIVATE
        nt_stats nt_text_renderer nt_font nt_material nt_resource
        nt_gfx_stub nt_hash nt_pool unity
    )
    target_compile_definitions(test_stats PRIVATE
        NT_STATS_TEST_ACCESS
        NT_TEXT_RENDERER_TEST_ACCESS
        _CRT_SECURE_NO_WARNINGS
    )
    target_compile_definitions(nt_stats PRIVATE NT_STATS_TEST_ACCESS)
    target_compile_definitions(nt_text_renderer PRIVATE NT_TEXT_RENDERER_TEST_ACCESS)
    target_compile_options(test_stats PRIVATE -U_DLL)
    nt_set_warning_flags(test_stats)
    nt_set_sanitizer_flags(test_stats)
    set_target_properties(test_stats PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/tests/${NT_PRESET_NAME}"
    )
    add_test(NAME test_stats COMMAND test_stats)

    # --- Phase 52: nt_ui module + walker MVP tests ---
    # All tests use stub gfx backend + Phase 51 NT_GFX_TEST_ACCESS probes.
    # nt_ui tests additionally use NT_UI_TEST_ACCESS for in-frame/walk probes.
    # Death-tests use the shared NT_TEST_EXPECT_ASSERT macro (Revision Issue 3).

    # Shared helper sources compiled into each binary.
    # - ui_atlas: produces minimal_ui_atlas via NT_ATLAS_TEST_ACCESS (Revision Issue 4)
    # - nt_assert_trap: NT_TEST_EXPECT_ASSERT death-test helper (Revision Issue 3)
    set(_nt_ui_test_helper_sources
        unit/test_helpers/ui_atlas.c
        unit/test_helpers/nt_assert_trap.c
        unit/test_helpers/ui_walker_fixture.c
    )

    # Helpers need `tests/unit/` on the include path so
    # `#include "test_helpers/ui_atlas.h"` resolves.
    set(_nt_ui_test_helper_includes
        "${CMAKE_CURRENT_SOURCE_DIR}/unit"
    )

    # Force nt_atlas to expose NT_ATLAS_TEST_ACCESS symbols
    # (nt_atlas_test_drive_resolve, nt_atlas_test_drive_cleanup) so the
    # ui_atlas helper can link.
    target_compile_definitions(nt_atlas PRIVATE NT_ATLAS_TEST_ACCESS)

    # Force nt_ui to expose NT_UI_TEST_ACCESS symbols
    # (nt_ui_test_inframe_ctx + walk-counter probes) so test binaries
    # that opt into NT_UI_TEST_ACCESS can link.
    target_compile_definitions(nt_ui PRIVATE NT_UI_TEST_ACCESS)

    # --- test_nt_sprite_renderer_emit_region -- D-52-01/02 + Drift 4 ---
    add_executable(test_nt_sprite_renderer_emit_region
        unit/test_nt_sprite_renderer_emit_region.c
        ${_nt_ui_test_helper_sources}
    )
    target_include_directories(test_nt_sprite_renderer_emit_region PRIVATE ${_nt_ui_test_helper_includes})
    target_link_libraries(test_nt_sprite_renderer_emit_region PRIVATE
        nt_sprite_renderer nt_text_renderer nt_font nt_material
        nt_resource nt_hash nt_pool nt_atlas nt_gfx_stub nt_core unity
    )
    target_compile_definitions(test_nt_sprite_renderer_emit_region PRIVATE
        NT_GFX_TEST_ACCESS NT_SPRITE_RENDERER_TEST_ACCESS NT_ATLAS_TEST_ACCESS
        _CRT_SECURE_NO_WARNINGS)
    target_compile_definitions(nt_gfx_stub PRIVATE NT_GFX_TEST_ACCESS)
    target_compile_options(test_nt_sprite_renderer_emit_region PRIVATE -U_DLL)
    nt_set_warning_flags(test_nt_sprite_renderer_emit_region)
    nt_set_sanitizer_flags(test_nt_sprite_renderer_emit_region)
    set_target_properties(test_nt_sprite_renderer_emit_region PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/tests/${NT_PRESET_NAME}")
    add_test(NAME test_nt_sprite_renderer_emit_region COMMAND test_nt_sprite_renderer_emit_region)

    # --- test_nt_ui_lifecycle -- UI-01, UI-02 ---
    add_executable(test_nt_ui_lifecycle
        unit/test_nt_ui_lifecycle.c
        ${_nt_ui_test_helper_sources}
    )
    target_include_directories(test_nt_ui_lifecycle PRIVATE ${_nt_ui_test_helper_includes})
    target_link_libraries(test_nt_ui_lifecycle PRIVATE
        nt_ui nt_sprite_renderer nt_text_renderer nt_font nt_material
        nt_resource nt_hash nt_pool nt_atlas nt_gfx_stub nt_core unity
    )
    target_compile_definitions(test_nt_ui_lifecycle PRIVATE
        NT_GFX_TEST_ACCESS NT_UI_TEST_ACCESS NT_ATLAS_TEST_ACCESS
        _CRT_SECURE_NO_WARNINGS)
    target_compile_options(test_nt_ui_lifecycle PRIVATE -U_DLL)
    nt_set_warning_flags(test_nt_ui_lifecycle)
    nt_set_sanitizer_flags(test_nt_ui_lifecycle)
    set_target_properties(test_nt_ui_lifecycle PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/tests/${NT_PRESET_NAME}")
    add_test(NAME test_nt_ui_lifecycle COMMAND test_nt_ui_lifecycle)

    # --- test_nt_ui_multictx -- UI-03 ---
    add_executable(test_nt_ui_multictx
        unit/test_nt_ui_multictx.c
        ${_nt_ui_test_helper_sources}
    )
    target_include_directories(test_nt_ui_multictx PRIVATE ${_nt_ui_test_helper_includes})
    target_link_libraries(test_nt_ui_multictx PRIVATE
        nt_ui nt_sprite_renderer nt_text_renderer nt_font nt_material
        nt_resource nt_hash nt_pool nt_atlas nt_gfx_stub nt_core unity
    )
    target_compile_definitions(test_nt_ui_multictx PRIVATE
        NT_GFX_TEST_ACCESS NT_UI_TEST_ACCESS NT_ATLAS_TEST_ACCESS
        _CRT_SECURE_NO_WARNINGS)
    target_compile_options(test_nt_ui_multictx PRIVATE -U_DLL)
    nt_set_warning_flags(test_nt_ui_multictx)
    nt_set_sanitizer_flags(test_nt_ui_multictx)
    set_target_properties(test_nt_ui_multictx PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/tests/${NT_PRESET_NAME}")
    add_test(NAME test_nt_ui_multictx COMMAND test_nt_ui_multictx)

    # --- test_nt_ui_begin_end -- UI-04, UI-05, UI-08 ---
    add_executable(test_nt_ui_begin_end
        unit/test_nt_ui_begin_end.c
        ${_nt_ui_test_helper_sources}
    )
    target_include_directories(test_nt_ui_begin_end PRIVATE ${_nt_ui_test_helper_includes})
    target_link_libraries(test_nt_ui_begin_end PRIVATE
        nt_ui nt_sprite_renderer nt_text_renderer nt_font nt_material
        nt_resource nt_hash nt_pool nt_atlas nt_gfx_stub nt_core unity
    )
    target_compile_definitions(test_nt_ui_begin_end PRIVATE
        NT_GFX_TEST_ACCESS NT_UI_TEST_ACCESS NT_ATLAS_TEST_ACCESS
        _CRT_SECURE_NO_WARNINGS)
    target_compile_options(test_nt_ui_begin_end PRIVATE -U_DLL)
    nt_set_warning_flags(test_nt_ui_begin_end)
    nt_set_sanitizer_flags(test_nt_ui_begin_end)
    set_target_properties(test_nt_ui_begin_end PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/tests/${NT_PRESET_NAME}")
    add_test(NAME test_nt_ui_begin_end COMMAND test_nt_ui_begin_end)

    # --- test_nt_ui_measure_cb -- CLAY-03 ---
    add_executable(test_nt_ui_measure_cb
        unit/test_nt_ui_measure_cb.c
        ${_nt_ui_test_helper_sources}
    )
    target_include_directories(test_nt_ui_measure_cb PRIVATE ${_nt_ui_test_helper_includes})
    target_link_libraries(test_nt_ui_measure_cb PRIVATE
        nt_ui nt_sprite_renderer nt_text_renderer nt_font nt_material
        nt_resource nt_hash nt_pool nt_atlas nt_gfx_stub nt_core unity
    )
    target_compile_definitions(test_nt_ui_measure_cb PRIVATE
        NT_GFX_TEST_ACCESS NT_UI_TEST_ACCESS NT_ATLAS_TEST_ACCESS NT_FONT_TEST_ACCESS
        _CRT_SECURE_NO_WARNINGS)
    target_compile_options(test_nt_ui_measure_cb PRIVATE -U_DLL)
    nt_set_warning_flags(test_nt_ui_measure_cb)
    nt_set_sanitizer_flags(test_nt_ui_measure_cb)
    set_target_properties(test_nt_ui_measure_cb PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/tests/${NT_PRESET_NAME}")
    add_test(NAME test_nt_ui_measure_cb COMMAND test_nt_ui_measure_cb)

    # --- test_nt_ui_pointer_state -- CLAY-04 ---
    add_executable(test_nt_ui_pointer_state
        unit/test_nt_ui_pointer_state.c
        ${_nt_ui_test_helper_sources}
    )
    target_include_directories(test_nt_ui_pointer_state PRIVATE ${_nt_ui_test_helper_includes})
    target_link_libraries(test_nt_ui_pointer_state PRIVATE
        nt_ui nt_sprite_renderer nt_text_renderer nt_font nt_material
        nt_resource nt_hash nt_pool nt_atlas nt_gfx_stub nt_core unity
    )
    target_compile_definitions(test_nt_ui_pointer_state PRIVATE
        NT_GFX_TEST_ACCESS NT_UI_TEST_ACCESS NT_ATLAS_TEST_ACCESS
        _CRT_SECURE_NO_WARNINGS)
    target_compile_options(test_nt_ui_pointer_state PRIVATE -U_DLL)
    nt_set_warning_flags(test_nt_ui_pointer_state)
    nt_set_sanitizer_flags(test_nt_ui_pointer_state)
    set_target_properties(test_nt_ui_pointer_state PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/tests/${NT_PRESET_NAME}")
    add_test(NAME test_nt_ui_pointer_state COMMAND test_nt_ui_pointer_state)

    # --- test_nt_ui_walker_dispatch -- WALK-01, WALK-04 ---
    add_executable(test_nt_ui_walker_dispatch
        unit/test_nt_ui_walker_dispatch.c
        ${_nt_ui_test_helper_sources}
    )
    target_include_directories(test_nt_ui_walker_dispatch PRIVATE ${_nt_ui_test_helper_includes})
    target_link_libraries(test_nt_ui_walker_dispatch PRIVATE
        nt_ui nt_sprite_renderer nt_text_renderer nt_font nt_material
        nt_resource nt_hash nt_pool nt_atlas nt_stats nt_gfx_stub nt_core unity
    )
    target_compile_definitions(test_nt_ui_walker_dispatch PRIVATE
        NT_GFX_TEST_ACCESS NT_UI_TEST_ACCESS NT_ATLAS_TEST_ACCESS
        NT_SPRITE_RENDERER_TEST_ACCESS NT_TEXT_RENDERER_TEST_ACCESS
        _CRT_SECURE_NO_WARNINGS)
    target_compile_options(test_nt_ui_walker_dispatch PRIVATE -U_DLL)
    nt_set_warning_flags(test_nt_ui_walker_dispatch)
    nt_set_sanitizer_flags(test_nt_ui_walker_dispatch)
    set_target_properties(test_nt_ui_walker_dispatch PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/tests/${NT_PRESET_NAME}")
    add_test(NAME test_nt_ui_walker_dispatch COMMAND test_nt_ui_walker_dispatch)

    # --- test_nt_ui_walker_scissor -- WALK-02, WALK-03 ---
    add_executable(test_nt_ui_walker_scissor
        unit/test_nt_ui_walker_scissor.c
        ${_nt_ui_test_helper_sources}
    )
    target_include_directories(test_nt_ui_walker_scissor PRIVATE ${_nt_ui_test_helper_includes})
    target_link_libraries(test_nt_ui_walker_scissor PRIVATE
        nt_ui nt_sprite_renderer nt_text_renderer nt_font nt_material
        nt_resource nt_hash nt_pool nt_atlas nt_stats nt_gfx_stub nt_core unity
    )
    target_compile_definitions(test_nt_ui_walker_scissor PRIVATE
        NT_GFX_TEST_ACCESS NT_UI_TEST_ACCESS NT_ATLAS_TEST_ACCESS
        _CRT_SECURE_NO_WARNINGS)
    target_compile_options(test_nt_ui_walker_scissor PRIVATE -U_DLL)
    nt_set_warning_flags(test_nt_ui_walker_scissor)
    nt_set_sanitizer_flags(test_nt_ui_walker_scissor)
    set_target_properties(test_nt_ui_walker_scissor PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/tests/${NT_PRESET_NAME}")
    add_test(NAME test_nt_ui_walker_scissor COMMAND test_nt_ui_walker_scissor)

    # --- test_nt_ui_walker_custom -- WALK-05 ---
    add_executable(test_nt_ui_walker_custom
        unit/test_nt_ui_walker_custom.c
        ${_nt_ui_test_helper_sources}
    )
    target_include_directories(test_nt_ui_walker_custom PRIVATE ${_nt_ui_test_helper_includes})
    target_link_libraries(test_nt_ui_walker_custom PRIVATE
        nt_ui nt_sprite_renderer nt_text_renderer nt_font nt_material
        nt_resource nt_hash nt_pool nt_atlas nt_stats nt_gfx_stub nt_core unity
    )
    target_compile_definitions(test_nt_ui_walker_custom PRIVATE
        NT_GFX_TEST_ACCESS NT_UI_TEST_ACCESS NT_ATLAS_TEST_ACCESS
        _CRT_SECURE_NO_WARNINGS)
    target_compile_options(test_nt_ui_walker_custom PRIVATE -U_DLL)
    nt_set_warning_flags(test_nt_ui_walker_custom)
    nt_set_sanitizer_flags(test_nt_ui_walker_custom)
    set_target_properties(test_nt_ui_walker_custom PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/tests/${NT_PRESET_NAME}")
    add_test(NAME test_nt_ui_walker_custom COMMAND test_nt_ui_walker_custom)

    # --- test_nt_ui_walker_flush -- WALK-06 ---
    add_executable(test_nt_ui_walker_flush
        unit/test_nt_ui_walker_flush.c
        ${_nt_ui_test_helper_sources}
    )
    target_include_directories(test_nt_ui_walker_flush PRIVATE ${_nt_ui_test_helper_includes})
    target_link_libraries(test_nt_ui_walker_flush PRIVATE
        nt_ui nt_sprite_renderer nt_text_renderer nt_font nt_material
        nt_resource nt_hash nt_pool nt_atlas nt_stats nt_gfx_stub nt_core unity
    )
    target_compile_definitions(test_nt_ui_walker_flush PRIVATE
        NT_GFX_TEST_ACCESS NT_UI_TEST_ACCESS NT_ATLAS_TEST_ACCESS
        NT_SPRITE_RENDERER_TEST_ACCESS NT_TEXT_RENDERER_TEST_ACCESS
        _CRT_SECURE_NO_WARNINGS)
    target_compile_options(test_nt_ui_walker_flush PRIVATE -U_DLL)
    nt_set_warning_flags(test_nt_ui_walker_flush)
    nt_set_sanitizer_flags(test_nt_ui_walker_flush)
    set_target_properties(test_nt_ui_walker_flush PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/tests/${NT_PRESET_NAME}")
    add_test(NAME test_nt_ui_walker_flush COMMAND test_nt_ui_walker_flush)

    # --- test_nt_ui_walker_readonly -- UI-06, UI-07 + pre-walk assert death-tests ---
    add_executable(test_nt_ui_walker_readonly
        unit/test_nt_ui_walker_readonly.c
        ${_nt_ui_test_helper_sources}
    )
    target_include_directories(test_nt_ui_walker_readonly PRIVATE ${_nt_ui_test_helper_includes})
    target_link_libraries(test_nt_ui_walker_readonly PRIVATE
        nt_ui nt_sprite_renderer nt_text_renderer nt_font nt_material
        nt_resource nt_hash nt_pool nt_atlas nt_stats nt_gfx_stub nt_core unity
    )
    target_compile_definitions(test_nt_ui_walker_readonly PRIVATE
        NT_GFX_TEST_ACCESS NT_UI_TEST_ACCESS NT_ATLAS_TEST_ACCESS
        _CRT_SECURE_NO_WARNINGS)
    target_compile_options(test_nt_ui_walker_readonly PRIVATE -U_DLL)
    nt_set_warning_flags(test_nt_ui_walker_readonly)
    nt_set_sanitizer_flags(test_nt_ui_walker_readonly)
    set_target_properties(test_nt_ui_walker_readonly PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/tests/${NT_PRESET_NAME}")
    add_test(NAME test_nt_ui_walker_readonly COMMAND test_nt_ui_walker_readonly)

    # --- test_nt_ui_stats -- WALK-09 ---
    add_executable(test_nt_ui_stats
        unit/test_nt_ui_stats.c
        ${_nt_ui_test_helper_sources}
    )
    target_include_directories(test_nt_ui_stats PRIVATE ${_nt_ui_test_helper_includes})
    target_link_libraries(test_nt_ui_stats PRIVATE
        nt_ui nt_sprite_renderer nt_text_renderer nt_font nt_material
        nt_resource nt_hash nt_pool nt_atlas nt_stats nt_gfx_stub nt_core unity
    )
    target_compile_definitions(test_nt_ui_stats PRIVATE
        NT_GFX_TEST_ACCESS NT_UI_TEST_ACCESS NT_ATLAS_TEST_ACCESS NT_STATS_TEST_ACCESS
        _CRT_SECURE_NO_WARNINGS)
    target_compile_options(test_nt_ui_stats PRIVATE -U_DLL)
    nt_set_warning_flags(test_nt_ui_stats)
    nt_set_sanitizer_flags(test_nt_ui_stats)
    set_target_properties(test_nt_ui_stats PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/tests/${NT_PRESET_NAME}")
    add_test(NAME test_nt_ui_stats COMMAND test_nt_ui_stats)

endif()

# --- WASM smoke test (Node.js loading via -sMODULARIZE) ---
if(EMSCRIPTEN)
    find_program(NT_NODE_EXECUTABLE NAMES node nodejs)

    if(NT_NODE_EXECUTABLE)
        add_test(
            NAME test_wasm_simd_loader
            COMMAND
                "${NT_NODE_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/wasm/simd_loader_test.js"
        )
    endif()

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
    if(NT_NODE_EXECUTABLE)
        add_test(
            NAME test_wasm_smoke
            COMMAND
                "${NT_NODE_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/wasm/smoke_test.js"
                --module
                "$<TARGET_FILE:wasm_smoke>"
        )
    else()
        message(STATUS "Node.js not found; skipping CTest registration for test_wasm_smoke")
    endif()
endif()
