# Native unit tests (Unity framework via CTest)
# WASM tests run through CTest via Node.js when available
#
# NT_TEST_ACCESS: single shared define that gates every module's test
# surface (the `// #region test_access` block at the bottom of each
# header). Applied at directory level here so all binaries below pick
# it up automatically; engine library targets that expose test surface
# get it explicitly so their compilation matches.

if(NOT EMSCRIPTEN)
    # Tests-wide define + engine libs that have a test surface. New module
    # with test surface? Add its target name to NT_TEST_ACCESS_LIBS below.
    add_compile_definitions(NT_TEST_ACCESS)
    set(NT_TEST_ACCESS_LIBS
        nt_atlas nt_ui nt_gfx nt_gfx_stub nt_font nt_text_renderer
        nt_resource nt_sprite_renderer nt_mesh_renderer nt_debug_overlay
        nt_shape_renderer nt_postfx_blur nt_mem_scratch nt_metrics nt_devapi_net
    )
    foreach(_lib IN LISTS NT_TEST_ACCESS_LIBS)
        if(TARGET ${_lib})
            target_compile_definitions(${_lib} PRIVATE NT_TEST_ACCESS)
        endif()
    endforeach()

    # --- Color math tests (header-only nt_color.h; no lib, just the engine include root) ---
    add_executable(test_nt_color unit/test_nt_color.c)
    target_link_libraries(test_nt_color PRIVATE unity)
    # nt_color.h uses powf/cbrtf; standalone (no engine lib), so GNU ld needs libm explicitly (Windows has it in the CRT).
    if(NOT WIN32)
        target_link_libraries(test_nt_color PRIVATE m)
    endif()
    target_include_directories(test_nt_color PRIVATE "${CMAKE_SOURCE_DIR}/engine")
    nt_set_warning_flags(test_nt_color)
    nt_set_sanitizer_flags(test_nt_color)
    set_target_properties(test_nt_color PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/tests/${NT_PRESET_NAME}"
    )
    add_test(NAME test_nt_color COMMAND test_nt_color)

    add_executable(test_core unit/test_core.c)
    target_link_libraries(test_core PRIVATE nt_core nt_log_stub unity)

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
    # Triggers UB to prove ASan+UBSan are detecting. Skipped on Windows+Clang
    # where sanitizers are disabled (clang interception_win can't patch UCRT).
    if(NOT (WIN32 AND CMAKE_C_COMPILER_ID STREQUAL "Clang"))
        add_executable(test_sanitizer_proof unit/test_sanitizer_proof.c)
        target_link_libraries(test_sanitizer_proof PRIVATE nt_core nt_log_stub unity)

        nt_set_warning_flags(test_sanitizer_proof)
        nt_set_sanitizer_flags(test_sanitizer_proof)

        set_target_properties(test_sanitizer_proof PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/tests/${NT_PRESET_NAME}"
        )

        add_test(NAME sanitizer_proof COMMAND test_sanitizer_proof)
        set_tests_properties(sanitizer_proof PROPERTIES WILL_FAIL TRUE)
    endif()

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

    # --- Log sink + ring tests (L1 without devapi) ---
    # Drives the REAL nt_log (nt_log_write must fan out to the attached ring sink).
    # GNU-ld single-pass order: consumer (nt_log_ring) precedes provider (nt_log);
    # nt_core/nt_hash trail last. nt_log already pulls nt_core for the assert handler.
    add_executable(test_log_ring unit/test_log_ring.c)
    target_link_libraries(test_log_ring PRIVATE nt_log_ring nt_log nt_hash nt_core unity)
    target_compile_definitions(test_log_ring PRIVATE _CRT_SECURE_NO_WARNINGS)
    nt_set_warning_flags(test_log_ring)
    nt_set_sanitizer_flags(test_log_ring)
    set_target_properties(test_log_ring PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/tests/${NT_PRESET_NAME}"
    )
    add_test(NAME test_log_ring COMMAND test_log_ring)

    # --- Platform memory probe (L1 native RSS) ---
    # nt_platform PUBLIC-links nt_core (+ psapi on Windows); nt_log_stub resolves
    # nt_core's log interface. Consumer (test) precedes provider (nt_platform).
    add_executable(test_platform_mem unit/test_platform_mem.c)
    target_link_libraries(test_platform_mem PRIVATE nt_platform nt_log_stub unity)
    nt_set_warning_flags(test_platform_mem)
    nt_set_sanitizer_flags(test_platform_mem)
    set_target_properties(test_platform_mem PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/tests/${NT_PRESET_NAME}"
    )
    add_test(NAME test_platform_mem COMMAND test_platform_mem)

    # --- Perf collector (nt_metrics, L1 without devapi) ---
    # nt_metrics is the perf source of truth now: the host pushes nt_metrics_frame_t + sets user
    # counters, and the test drives those same public paths — so it links nt_metrics + nt_hash only,
    # no overlay/gfx/scratch/platform providers. GNU-ld order: consumer (nt_metrics) before nt_hash;
    # nt_core/nt_log_stub trail last.
    # nt_assert_trap.c backs the collision death-test (NT_TEST_EXPECT_ASSERT); needs unit/ on the
    # include path so it resolves "test_helpers/nt_assert_trap.h".
    add_executable(test_metrics unit/test_metrics.c unit/test_helpers/nt_assert_trap.c)
    target_link_libraries(test_metrics PRIVATE
        nt_metrics nt_hash nt_log_stub unity
    )
    target_include_directories(test_metrics PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/unit")
    target_compile_definitions(test_metrics PRIVATE _CRT_SECURE_NO_WARNINGS)
    nt_setup_test_target(test_metrics)

    # --- Window pre-swap hook registry (L1, no GL/GLFW) ---
    # nt_window_stub = the real shared nt_window.c (hook registry) + a stubbed backend. nt_assert_trap.c
    # backs the table-full death-test; needs unit/ on the include path for "test_helpers/nt_assert_trap.h".
    add_executable(test_window_hooks unit/test_window_hooks.c unit/test_helpers/nt_assert_trap.c)
    target_link_libraries(test_window_hooks PRIVATE nt_window_stub nt_core nt_log_stub unity)
    target_include_directories(test_window_hooks PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/unit")
    nt_setup_test_target(test_window_hooks)

    # --- Log API tests (domain functions, macros, level filtering) ---
    add_executable(test_log_api unit/test_log_api.c)
    target_link_libraries(test_log_api PRIVATE nt_log unity)
    target_compile_definitions(test_log_api PRIVATE NT_LOG_DOMAIN_DEFAULT="test_mod" NT_LOG_REAL_IMPL=1)
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
    target_link_libraries(test_app PRIVATE nt_app_stub nt_log_stub unity)
    nt_set_warning_flags(test_app)
    nt_set_sanitizer_flags(test_app)
    set_target_properties(test_app PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/tests/${NT_PRESET_NAME}"
    )
    add_test(NAME test_app COMMAND test_app)

    # --- Managed loop (time-control) tests (L1: pause/manual/scale loop + sim-advance counter) ---
    add_executable(test_app_managed unit/test_app_managed.c unit/test_helpers/nt_assert_trap.c)
    target_link_libraries(test_app_managed PRIVATE nt_app_stub nt_log_stub unity)
    target_include_directories(test_app_managed PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/unit")
    nt_set_warning_flags(test_app_managed)
    nt_set_sanitizer_flags(test_app_managed)
    set_target_properties(test_app_managed PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/tests/${NT_PRESET_NAME}"
    )
    add_test(NAME test_app_managed COMMAND test_app_managed)

    # --- Window module tests ---
    add_executable(test_window unit/test_window.c)
    target_link_libraries(test_window PRIVATE nt_window_stub nt_log_stub unity)
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
    target_link_libraries(test_input PRIVATE nt_input_stub nt_log_stub unity)
    nt_set_warning_flags(test_input)
    nt_set_sanitizer_flags(test_input)
    set_target_properties(test_input PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/tests/${NT_PRESET_NAME}"
    )
    add_test(NAME test_input COMMAND test_input)

    # --- Input inject + player-gate tests ---
    add_executable(test_input_inject unit/test_input_inject.c)
    target_link_libraries(test_input_inject PRIVATE nt_input_stub nt_log_stub unity)
    nt_set_warning_flags(test_input_inject)
    nt_set_sanitizer_flags(test_input_inject)
    set_target_properties(test_input_inject PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/tests/${NT_PRESET_NAME}"
    )
    add_test(NAME test_input_inject COMMAND test_input_inject)

    # --- Input char ring tests (FIFO + UTF-32 + overflow) ---
    add_executable(test_input_char unit/test_input_char.c)
    target_link_libraries(test_input_char PRIVATE nt_input_stub nt_log_stub unity)
    nt_set_warning_flags(test_input_char)
    nt_set_sanitizer_flags(test_input_char)
    set_target_properties(test_input_char PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/tests/${NT_PRESET_NAME}"
    )
    add_test(NAME test_input_char COMMAND test_input_char)

    # --- Graphics module tests ---
    add_executable(test_gfx unit/test_gfx.c)
    target_link_libraries(test_gfx PRIVATE nt_gfx_stub nt_log_stub unity)
    nt_set_warning_flags(test_gfx)
    nt_set_sanitizer_flags(test_gfx)
    set_target_properties(test_gfx PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/tests/${NT_PRESET_NAME}"
    )
    add_test(NAME test_gfx COMMAND test_gfx)

    # --- Graphics readback tests (nt_gfx_read_pixels) ---
    # nt_gfx_stub bundles the shared nt_gfx.c (nt_gfx_read_pixels orchestration)
    # with the synthetic readback backend, so it alone proves the readback contract headless.
    add_executable(test_gfx_read_pixels unit/test_gfx_read_pixels.c)
    target_link_libraries(test_gfx_read_pixels PRIVATE nt_gfx_stub nt_log_stub unity)
    nt_set_warning_flags(test_gfx_read_pixels)
    nt_set_sanitizer_flags(test_gfx_read_pixels)
    set_target_properties(test_gfx_read_pixels PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/tests/${NT_PRESET_NAME}"
    )
    add_test(NAME test_gfx_read_pixels COMMAND test_gfx_read_pixels)

    # --- Graphics render-target tests ---
    add_executable(test_nt_gfx_render_target unit/test_nt_gfx_render_target.c unit/test_helpers/nt_assert_trap.c)
    target_link_libraries(test_nt_gfx_render_target PRIVATE nt_gfx_stub nt_log_stub unity)
    target_include_directories(test_nt_gfx_render_target PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/unit")
    target_compile_definitions(test_nt_gfx_render_target PRIVATE _CRT_SECURE_NO_WARNINGS)
    nt_setup_test_target(test_nt_gfx_render_target WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

    # Real OpenGL framebuffer smoke: fills every texture slot, resizes, and reads back pixels.
    add_executable(test_nt_gfx_render_target_native unit/test_nt_gfx_render_target_native.c)
    target_link_libraries(test_nt_gfx_render_target_native PRIVATE nt_gfx nt_window nt_input_stub nt_log unity glad)
    nt_setup_test_target(test_nt_gfx_render_target_native)
    set_tests_properties(test_nt_gfx_render_target_native PROPERTIES LABELS "native")

    # --- Post-FX blur tests ---
    add_executable(test_nt_postfx_blur unit/test_nt_postfx_blur.c unit/test_helpers/nt_assert_trap.c)
    target_link_libraries(test_nt_postfx_blur PRIVATE nt_postfx_blur nt_gfx_stub nt_log_stub unity)
    target_include_directories(test_nt_postfx_blur PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/unit")
    target_compile_definitions(test_nt_postfx_blur PRIVATE _CRT_SECURE_NO_WARNINGS)
    nt_setup_test_target(test_nt_postfx_blur WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

    # --- base64 util (standalone nt_base64 module, no devapi) ---
    add_executable(test_base64 unit/test_base64.c)
    target_link_libraries(test_base64 PRIVATE nt_base64 unity)
    nt_setup_test_target(test_base64)

    # --- Graphics scissor/viewport tests ---
    # restore_gpu cases call into nt_sprite_renderer + nt_text_renderer; their
    # renderer libs (and transitive font/resource/hash/pool/material deps) are
    # required at link time.
    add_executable(test_nt_gfx_scissor unit/test_nt_gfx_scissor.c)
    target_link_libraries(test_nt_gfx_scissor PRIVATE
        nt_sprite_renderer nt_text_renderer nt_font nt_material
        nt_resource nt_hash nt_pool nt_gfx_stub nt_http_stub nt_fs_stub nt_log_stub unity
    )
    target_compile_definitions(test_nt_gfx_scissor PRIVATE _CRT_SECURE_NO_WARNINGS)
    nt_setup_test_target(test_nt_gfx_scissor)

    # --- Shape module tests ---
    add_executable(test_shape unit/test_shape.c)
    target_link_libraries(test_shape PRIVATE nt_shape_renderer nt_gfx_stub nt_log_stub unity)
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
    target_link_libraries(test_entity PRIVATE nt_entity nt_log_stub unity)
    nt_set_warning_flags(test_entity)
    nt_set_sanitizer_flags(test_entity)
    set_target_properties(test_entity PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/tests/${NT_PRESET_NAME}"
    )
    add_test(NAME test_entity COMMAND test_entity)

    # --- Entity introspection facility (gated: the walk + text sink emit real output only with
    # NT_INTROSPECT_ENABLED; the OFF mirror compiles stubs that would fail these assertions). nt_introspect
    # consumes nt_entity (list it first) + nt_log_write (nt_log_stub trails). ---
    if(NT_INTROSPECT_ENABLED)
        add_executable(test_introspect unit/test_introspect.c)
        target_link_libraries(test_introspect PRIVATE nt_introspect nt_entity nt_core nt_log_stub unity)
        target_compile_definitions(test_introspect PRIVATE _CRT_SECURE_NO_WARNINGS)
        nt_set_warning_flags(test_introspect)
        nt_set_sanitizer_flags(test_introspect)
        set_target_properties(test_introspect PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/tests/${NT_PRESET_NAME}"
        )
        add_test(NAME test_introspect COMMAND test_introspect)
    endif()

    # --- Transform component tests ---
    add_executable(test_transform_comp unit/test_transform_comp.c)
    target_link_libraries(test_transform_comp PRIVATE nt_transform_comp nt_entity nt_log_stub unity)
    nt_set_warning_flags(test_transform_comp)
    nt_set_sanitizer_flags(test_transform_comp)
    set_target_properties(test_transform_comp PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/tests/${NT_PRESET_NAME}"
    )
    add_test(NAME test_transform_comp COMMAND test_transform_comp)

    # --- Component tests (mesh, material, drawable) ---
    add_executable(test_components unit/test_components.c)
    target_link_libraries(test_components PRIVATE nt_mesh_comp nt_material_comp nt_drawable_comp nt_entity nt_hash nt_log_stub unity)
    nt_set_warning_flags(test_components)
    nt_set_sanitizer_flags(test_components)
    set_target_properties(test_components PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/tests/${NT_PRESET_NAME}"
    )
    add_test(NAME test_components COMMAND test_components)

    # --- Render items tests ---
    add_executable(test_render_items unit/test_render_items.c)
    target_link_libraries(test_render_items PRIVATE nt_render nt_entity nt_drawable_comp nt_transform_comp nt_hash nt_gfx_stub nt_log_stub unity)
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
    target_link_libraries(test_font PRIVATE nt_font nt_gfx_stub nt_resource nt_hash nt_pool nt_http_stub nt_fs_stub nt_log_stub stb_truetype unity)
    target_compile_definitions(test_font PRIVATE _CRT_SECURE_NO_WARNINGS NT_LILITA_TTF="${CMAKE_SOURCE_DIR}/assets/fonts/LilitaOne-RussianChineseKo.ttf")
    nt_setup_test_target(test_font)

    # --- Font measure benchmark ---
    # Measures hit vs miss ns/call for the nt_font_measure_n LRU cache.
    # Uses nt_time_nanos() for the ns clock (same module used by test_time).
    add_executable(test_nt_font_measure_bench unit/test_nt_font_measure_bench.c)
    target_link_libraries(test_nt_font_measure_bench PRIVATE nt_font nt_gfx_stub nt_resource nt_hash nt_pool nt_time nt_http_stub nt_fs_stub nt_log_stub unity)
    target_compile_definitions(test_nt_font_measure_bench PRIVATE _CRT_SECURE_NO_WARNINGS)
    nt_setup_test_target(test_nt_font_measure_bench)

    # --- UI 3D-vs-2D hit-test + compose benchmark ---
    # Validates Plan A perf cost before engine refactor. Synthetic mat4 vs current 2D path.
    add_executable(test_nt_ui_3d_bench unit/test_nt_ui_3d_bench.c)
    target_link_libraries(test_nt_ui_3d_bench PRIVATE nt_math nt_time unity)
    target_compile_definitions(test_nt_ui_3d_bench PRIVATE _CRT_SECURE_NO_WARNINGS)
    nt_setup_test_target(test_nt_ui_3d_bench)

    # --- Text renderer module tests ---
    add_executable(test_text_renderer unit/test_text_renderer.c)
    target_link_libraries(test_text_renderer PRIVATE nt_text_renderer nt_font nt_gfx_stub nt_resource nt_hash nt_pool nt_material nt_time nt_http_stub nt_fs_stub nt_log_stub unity)
    target_compile_definitions(test_text_renderer PRIVATE _CRT_SECURE_NO_WARNINGS)
    nt_setup_test_target(test_text_renderer)

    # --- Material module tests ---
    add_executable(test_material unit/test_material.c)
    target_link_libraries(test_material PRIVATE nt_material nt_resource nt_hash nt_http_stub nt_fs_stub nt_log_stub unity)
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
    # Real nt_fs/nt_http: test_resource drives the real loader pipeline (native
    # file reads + request-fail paths). Stubs make nt_fs_read_file return INVALID,
    # so assets never activate and the activation/resolve callbacks never fire.
    target_link_libraries(test_resource PRIVATE nt_resource nt_http nt_fs nt_log_stub unity)
    target_compile_definitions(test_resource PRIVATE _CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_DEPRECATE)
    nt_setup_test_target(test_resource WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
    # Only test that drives REAL nt_fs/nt_http + nt_time_sleep TTL paths; flaky under
    # parallel ctest (CPU contention skews the sub-ms timing), clean serially. Run alone.
    set_tests_properties(test_resource PROPERTIES RUN_SERIAL TRUE)

    # --- Atlas module tests ---
    add_executable(test_atlas unit/test_atlas.c unit/test_helpers/nt_assert_trap.c)
    target_link_libraries(test_atlas PRIVATE nt_atlas nt_resource nt_hash nt_http_stub nt_fs_stub nt_log_stub unity)
    target_include_directories(test_atlas PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/unit")
    target_compile_definitions(test_atlas PRIVATE _CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_DEPRECATE)
    nt_setup_test_target(test_atlas)

    # Parser stays independently testable without linking the benchmark executable.
    add_executable(test_atlas_bench_parse
        unit/test_atlas_bench_parse.c
        ${CMAKE_SOURCE_DIR}/tools/research/atlas_bench/ntpack_parse.c
    )
    target_link_libraries(test_atlas_bench_parse PRIVATE unity)
    target_include_directories(test_atlas_bench_parse PRIVATE
        ${CMAKE_SOURCE_DIR}/shared/include
        ${CMAKE_SOURCE_DIR}/tools/builder
        ${CMAKE_SOURCE_DIR}/tools/research/atlas_bench
    )
    target_compile_definitions(test_atlas_bench_parse PRIVATE _CRT_SECURE_NO_WARNINGS)
    nt_setup_test_target(test_atlas_bench_parse)

    add_executable(test_atlas_bench_cli
        unit/test_atlas_bench_cli.c
        ${CMAKE_SOURCE_DIR}/tools/research/atlas_bench/atlas_bench_cli.c
    )
    target_link_libraries(test_atlas_bench_cli PRIVATE unity)
    target_include_directories(test_atlas_bench_cli PRIVATE
        ${CMAKE_SOURCE_DIR}/tools/research/atlas_bench
    )
    target_compile_definitions(test_atlas_bench_cli PRIVATE _CRT_SECURE_NO_WARNINGS)
    nt_setup_test_target(test_atlas_bench_cli WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

    # --- Sprite component tests ---
    add_executable(test_sprite_comp unit/test_sprite_comp.c unit/test_helpers/nt_assert_trap.c)
    target_link_libraries(test_sprite_comp PRIVATE
        nt_sprite_comp nt_atlas nt_entity nt_resource nt_hash nt_pool nt_shared nt_http_stub nt_fs_stub nt_log_stub unity
    )
    target_include_directories(test_sprite_comp PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/unit")
    target_compile_definitions(test_sprite_comp PRIVATE _CRT_SECURE_NO_WARNINGS)
    nt_setup_test_target(test_sprite_comp)

    # --- HTTP module tests ---
    add_executable(test_http unit/test_http.c)
    target_link_libraries(test_http PRIVATE nt_http_stub unity)
    nt_set_warning_flags(test_http)
    nt_set_sanitizer_flags(test_http)
    set_target_properties(test_http PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/tests/${NT_PRESET_NAME}"
    )
    add_test(NAME test_http COMMAND test_http)

    # cJSON standalone round-trip. Links cjson only (proves cjson works WITHOUT devapi),
    # so it must NOT sit behind the NT_DEVAPI_ENABLED gate — cjson is built unconditionally.
    add_executable(test_devapi_cjson unit/test_devapi_cjson.c)
    target_link_libraries(test_devapi_cjson PRIVATE cjson unity)
    target_compile_definitions(test_devapi_cjson PRIVATE _CRT_SECURE_NO_WARNINGS)
    nt_setup_test_target(test_devapi_cjson)

    # --- DevAPI module tests (NT_DEVAPI_ENABLED-gated) ---
    if(NT_DEVAPI_ENABLED)
        # Each devapi test links nt_devapi (framework) + only the group lib(s) it exercises, so it pulls
        # only those groups' providers. A group lib's PUBLIC NT_DEVAPI_GROUP_<X> define + its PRIVATE deps
        # propagate to the test, so no per-test group defines or provider lists are needed here. The
        # discovery test links the nt_devapi_default bundle (every group) and gets every group define.

        # registry register / dup-reject / owned-copy survival. Group-independent (registers its own
        # commands; the only built-in method it names is in a submit-before-init death test that
        # asserts before dispatch), so no group gate.
        # nt_core before nt_log_stub: nt_core references nt_log_write and nothing earlier does,
        # so on GNU ld (left-to-right) the log impl must trail nt_core or it is dropped.
        add_executable(test_devapi_registry unit/test_devapi_registry.c unit/test_helpers/nt_assert_trap.c)
        target_link_libraries(test_devapi_registry PRIVATE nt_devapi cjson nt_app_stub nt_core nt_log_stub unity)
        # unit/ on the include path so nt_assert_trap.c resolves "test_helpers/nt_assert_trap.h" (death tests).
        target_include_directories(test_devapi_registry PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/unit")
        target_compile_definitions(test_devapi_registry PRIVATE
            _CRT_SECURE_NO_WARNINGS NT_DEVAPI_ENABLED=1
        )
        nt_setup_test_target(test_devapi_registry)

        # submit() envelope, request_id echo, unknown-method, batch order +
        # continue-on-error, pointer lifetime (ASan via sanitizer flags). Group-independent.
        add_executable(test_devapi_envelope unit/test_devapi_envelope.c)
        target_link_libraries(test_devapi_envelope PRIVATE nt_devapi cjson nt_app_stub nt_core nt_log_stub unity)
        target_compile_definitions(test_devapi_envelope PRIVATE
            _CRT_SECURE_NO_WARNINGS NT_DEVAPI_ENABLED=1
        )
        nt_setup_test_target(test_devapi_envelope)

        # deferred-response mechanism: submit()->NULL on defer, K-1 NULL polls, K-th yields
        # the {ok,result,request_id} envelope, overflow rejected (ASan via sanitizer flags). Group-independent.
        add_executable(test_devapi_deferred unit/test_devapi_deferred.c)
        target_link_libraries(test_devapi_deferred PRIVATE nt_devapi cjson nt_app_stub nt_core nt_log_stub unity)
        target_compile_definitions(test_devapi_deferred PRIVATE
            _CRT_SECURE_NO_WARNINGS NT_DEVAPI_ENABLED=1
        )
        nt_setup_test_target(test_devapi_deferred)

        # Loopback transport backpressure: a large response survives a full send buffer (EWOULDBLOCK ->
        # select-wait) intact, and a wedged peer is dropped on a bounded timeout, not busy-spun. Links
        # nt_devapi_net (socket code + ws2_32) ahead of nt_devapi + stubs. Group-independent.
        add_executable(test_devapi_net_backpressure unit/test_devapi_net_backpressure.c)
        target_link_libraries(test_devapi_net_backpressure PRIVATE nt_devapi_net nt_devapi cjson nt_app_stub nt_core nt_log_stub unity)
        target_compile_definitions(test_devapi_net_backpressure PRIVATE
            _CRT_SECURE_NO_WARNINGS NT_DEVAPI_ENABLED=1
        )
        nt_setup_test_target(test_devapi_net_backpressure)

        # core command group via submit(). engine.info.version comes from nt_core, so the
        # test asserts it against nt_engine_version_string() directly. Needs the core group.
        if(NT_DEVAPI_GROUP_CORE)
            add_executable(test_devapi_core unit/test_devapi_core.c)
            target_link_libraries(test_devapi_core PRIVATE nt_devapi_core nt_devapi cjson nt_window_stub nt_app_stub nt_core nt_log_stub unity)
            target_compile_definitions(test_devapi_core PRIVATE
                _CRT_SECURE_NO_WARNINGS NT_DEVAPI_ENABLED=1
            )
            nt_setup_test_target(test_devapi_core)
        endif()

        # discovery group (endpoints / command.describe / features) + the zero-engine-edits
        # game-layer registration path. Needs the discovery group; the active-group defines let it
        # #ifdef-gate its per-group `features` assertions + expected group count.
        if(NT_DEVAPI_GROUP_DISCOVERY)
            add_executable(test_devapi_discovery unit/test_devapi_discovery.c)
            # Discovery links the nt_devapi_default bundle (register_default registers every compiled-in
            # group). GNU-ld is single-pass: each group lib must precede the provider stubs, so list the
            # existing group libs explicitly (the bundle pulls them too; this pins the order) before the
            # stubs. obs/entity_write/capture pull real providers -- list them gated so a discovery-only
            # config (those groups OFF) links a minimal set.
            set(_disc_group_provider_libs "")
            if(NT_DEVAPI_GROUP_OBS)
                list(APPEND _disc_group_provider_libs
                    nt_log_ring nt_metrics nt_introspect nt_entity
                    nt_material nt_pool nt_resource nt_shared nt_hash nt_http_stub nt_fs_stub)
            endif()
            if(NT_DEVAPI_GROUP_ENTITY_WRITE)
                list(APPEND _disc_group_provider_libs
                    nt_introspect nt_entity nt_transform_comp nt_drawable_comp nt_comp_storage
                    nt_resource nt_math nt_shared nt_http_stub nt_fs_stub)
            endif()
            if(NT_DEVAPI_GROUP_CAPTURE)
                list(APPEND _disc_group_provider_libs nt_fpng)
            endif()
            target_link_libraries(test_devapi_discovery PRIVATE
                nt_devapi_default
                $<TARGET_NAME_IF_EXISTS:nt_devapi_core>
                $<TARGET_NAME_IF_EXISTS:nt_devapi_time>
                $<TARGET_NAME_IF_EXISTS:nt_devapi_input>
                $<TARGET_NAME_IF_EXISTS:nt_devapi_ui>
                $<TARGET_NAME_IF_EXISTS:nt_devapi_obs>
                $<TARGET_NAME_IF_EXISTS:nt_devapi_entity_write>
                $<TARGET_NAME_IF_EXISTS:nt_devapi_capture>
                $<TARGET_NAME_IF_EXISTS:nt_devapi_discovery>
                nt_devapi cjson
                nt_ui_stub nt_window_stub nt_app_stub nt_gfx_stub nt_input_stub
                ${_disc_group_provider_libs}
                nt_core nt_log_stub unity)
            target_compile_definitions(test_devapi_discovery PRIVATE
                _CRT_SECURE_NO_WARNINGS NT_DEVAPI_ENABLED=1
            )
            nt_setup_test_target(test_devapi_discovery)
        endif()

        # L2 time/render/frame group via submit(): discovery + forwarding to nt_app L1 +
        # bad_params + render.info + frame.current + yield-after-N-sim-advances + end-to-end step drain.
        # Links nt_app_stub (g_nt_app + nt_app_* the handlers forward to) and nt_gfx_stub
        # (nt_gfx_get_frame_draw_calls -> 0 for render.info; the light gfx stub, no font/material chain).
        if(NT_DEVAPI_GROUP_TIME)
            add_executable(test_devapi_time unit/test_devapi_time.c)
            target_link_libraries(test_devapi_time PRIVATE nt_devapi_time nt_devapi_discovery nt_devapi cjson nt_window_stub nt_app_stub nt_gfx_stub nt_core nt_log_stub unity)
            target_compile_definitions(test_devapi_time PRIVATE
                _CRT_SECURE_NO_WARNINGS NT_DEVAPI_ENABLED=1
            )
            nt_setup_test_target(test_devapi_time)
        endif()

        # L2 input group via submit(): discovery + forwarding to the L1 inject API + bad_params +
        # input.text UTF-8 decode + command.describe. Links nt_input_stub (the input.* handlers call
        # the L1 inject symbols nt_input_inject_* / nt_input_set_player_enabled).
        # Links nt_devapi_net for nt_devapi_update: the scheduling tests drive the real per-tick path
        # (net_poll + advance-gated schedule tick keyed off g_nt_app.frame), not just the schedule TU.
        if(NT_DEVAPI_GROUP_INPUT)
            add_executable(test_devapi_input unit/test_devapi_input.c)
            target_link_libraries(test_devapi_input PRIVATE nt_devapi_net nt_devapi_input nt_devapi_discovery nt_devapi cjson nt_window_stub nt_app_stub nt_input_stub nt_core nt_log_stub unity)
            target_compile_definitions(test_devapi_input PRIVATE
                _CRT_SECURE_NO_WARNINGS NT_DEVAPI_ENABLED=1
            )
            nt_setup_test_target(test_devapi_input)
        endif()

        # L2 obs group via submit(): log.*/perf.*/entity.*/resource.* immediate reads. Links the real
        # obs-referenced L1 libs (the dev ring + metrics collector + overlay + entity/components +
        # resource) so the handlers read live state; nt_gfx_stub satisfies the overlay/resource gfx
        # symbols, nt_http_stub/nt_fs_stub the resource loader. Consumer-before-provider link order:
        # nt_devapi first, the obs libs next, nt_core / nt_log trailing last (GNU-ld single pass).
        # perf.* reads nt_metrics directly now (no overlay), and the test feeds nt_metrics via the
        # public count + sample paths — so no nt_debug_overlay/text_renderer/font/material deps. The
        # REAL nt_log is linked (not nt_log_stub) so nt_log_write fans out to the registered
        # nt_log_ring sink — the obs test feeds the ring via real log writes.
        if(NT_DEVAPI_GROUP_OBS)
            add_executable(test_devapi_obs unit/test_devapi_obs.c)
            target_link_libraries(test_devapi_obs PRIVATE
                nt_devapi_obs nt_devapi_discovery nt_devapi cjson nt_window_stub nt_app_stub
                nt_log_ring nt_metrics
                nt_introspect nt_entity nt_transform_comp nt_drawable_comp nt_mesh_comp nt_material_comp nt_comp_storage
                nt_material nt_pool nt_resource nt_math nt_shared
                nt_gfx_stub nt_input_stub nt_http_stub nt_fs_stub
                nt_core nt_log nt_hash unity
            )
            target_compile_definitions(test_devapi_obs PRIVATE
                _CRT_SECURE_NO_WARNINGS NT_DEVAPI_ENABLED=1
            )
            nt_setup_test_target(test_devapi_obs)
        endif()

        # L2 entity-write group (entity.set) via submit(): writes through the component apply() hooks.
        # Links the REAL transform/drawable (apply hooks + setters) + nt_introspect (nt_write_value type)
        # + nt_entity. nt_devapi also carries the obs group here, so its providers (metrics / log_ring /
        # resource) must resolve too. Consumer-before-provider: nt_devapi first, providers next, core/log last.
        if(NT_DEVAPI_GROUP_ENTITY_WRITE)
            add_executable(test_devapi_entity_write unit/test_devapi_entity_write.c)
            target_link_libraries(test_devapi_entity_write PRIVATE
                nt_devapi_entity_write nt_devapi cjson nt_window_stub nt_app_stub
                nt_introspect nt_entity nt_transform_comp nt_drawable_comp nt_comp_storage
                nt_resource nt_math nt_shared
                nt_gfx_stub nt_input_stub nt_http_stub nt_fs_stub
                nt_core nt_log_stub nt_hash unity
            )
            target_compile_definitions(test_devapi_entity_write PRIVATE
                _CRT_SECURE_NO_WARNINGS NT_DEVAPI_ENABLED=1
            )
            nt_setup_test_target(test_devapi_entity_write)
        endif()

        # capture group (capture.frame / capture.region) via submit(): the synthetic nt_gfx_stub
        # readback feeds the real strip+box+fpng+base64 chain when the seam is driven manually (no GL).
        # Consumer-before-provider GNU-ld order: nt_devapi first, nt_gfx_stub/nt_fpng next, core/log last.
        # The TU is #if-gated on NT_DEVAPI_GROUP_CAPTURE with an #else stub-main, so a group-absent
        # compile still links clean.
        if(NT_DEVAPI_GROUP_CAPTURE)
            add_executable(test_devapi_capture unit/test_devapi_capture.c)
            target_link_libraries(test_devapi_capture PRIVATE
                nt_devapi_capture nt_devapi cjson nt_window_stub nt_app_stub
                nt_gfx_stub nt_fpng nt_core nt_log_stub unity
            )
            target_compile_definitions(test_devapi_capture PRIVATE
                _CRT_SECURE_NO_WARNINGS NT_DEVAPI_ENABLED=1
            )
            nt_setup_test_target(test_devapi_capture)
        endif()
    endif()

    # --- File system module tests ---
    add_executable(test_fs unit/test_fs.c)
    target_link_libraries(test_fs PRIVATE nt_fs unity)
    target_compile_definitions(test_fs PRIVATE _CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_DEPRECATE)
    nt_setup_test_target(test_fs WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

    # --- Clipboard module tests (real/stub twins, same source) ---
    # Native impl: real round-trip needs an initialized GLFW (nt_window pulls glfw).
    add_executable(test_clipboard unit/test_clipboard.c)
    target_link_libraries(test_clipboard PRIVATE nt_clipboard nt_window nt_input_stub nt_log_stub unity)
    target_compile_definitions(test_clipboard PRIVATE NT_CLIPBOARD_TEST_NATIVE)
    nt_setup_test_target(test_clipboard)

    # Stub impl: get -> "", set -> noop (same source, no GLFW).
    add_executable(test_clipboard_stub unit/test_clipboard.c)
    target_link_libraries(test_clipboard_stub PRIVATE nt_clipboard_stub nt_log_stub unity)
    nt_setup_test_target(test_clipboard_stub)

    # --- Builder tests ---
    add_executable(test_builder unit/test_builder.c)
    target_link_libraries(test_builder PRIVATE nt_builder nt_log unity)
    target_compile_definitions(test_builder PRIVATE _CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_DEPRECATE)
    nt_setup_test_target(test_builder WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
    # Builder assert tests longjmp mid-pipeline; leaked allocations are expected.
    set_tests_properties(test_builder PROPERTIES ENVIRONMENT "LSAN_OPTIONS=detect_leaks=0")

    add_executable(test_atlas_hull_visual_report
        unit/test_atlas_hull_visual_report.c
        ${CMAKE_SOURCE_DIR}/tools/research/atlas_bench/hull_visual_report.c
        ${CMAKE_SOURCE_DIR}/tools/research/atlas_bench/ntpack_parse.c
    )
    target_link_libraries(test_atlas_hull_visual_report PRIVATE nt_builder nt_log unity)
    target_include_directories(test_atlas_hull_visual_report PRIVATE
        ${CMAKE_SOURCE_DIR}/shared/include
        ${CMAKE_SOURCE_DIR}/tools/builder
        ${CMAKE_SOURCE_DIR}/tools/research/atlas_bench
    )
    target_compile_definitions(test_atlas_hull_visual_report PRIVATE _CRT_SECURE_NO_WARNINGS)
    nt_setup_test_target(test_atlas_hull_visual_report WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

    # Proves the graceful transparent-after-trim error path is leak-free:
    # runs under live LSan, no detect_leaks=0 masking.
    add_executable(test_atlas_transparent_trim unit/test_atlas_transparent_trim.c)
    target_link_libraries(test_atlas_transparent_trim PRIVATE nt_builder nt_log unity)
    target_compile_definitions(test_atlas_transparent_trim PRIVATE _CRT_SECURE_NO_WARNINGS)
    nt_setup_test_target(test_atlas_transparent_trim WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

    # Live LSan covers graceful atlas errors and worker-pool cleanup.
    # Assert-heavy builder tests disable leak detection because longjmp skips cleanup.
    add_executable(test_atlas_unfittable unit/test_atlas_unfittable.c)
    target_link_libraries(test_atlas_unfittable PRIVATE nt_builder nt_log unity)
    target_compile_definitions(test_atlas_unfittable PRIVATE _CRT_SECURE_NO_WARNINGS)
    nt_setup_test_target(test_atlas_unfittable WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

    # Packer determinism tripwire: fixed in-memory corpus keeps density and hull
    # metrics deterministic without timing assertions (bench parser is the probe).
    add_executable(test_atlas_pack_determinism
        unit/test_atlas_pack_determinism.c
        ${CMAKE_SOURCE_DIR}/tools/research/atlas_bench/ntpack_parse.c
    )
    target_link_libraries(test_atlas_pack_determinism PRIVATE nt_builder nt_log unity)
    target_include_directories(test_atlas_pack_determinism PRIVATE
        ${CMAKE_SOURCE_DIR}/shared/include
        ${CMAKE_SOURCE_DIR}/tools/builder
        ${CMAKE_SOURCE_DIR}/tools/research/atlas_bench
    )
    target_compile_definitions(test_atlas_pack_determinism PRIVATE _CRT_SECURE_NO_WARNINGS)
    nt_setup_test_target(test_atlas_pack_determinism WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

    # Compiles the complete pre-tolerance positional option lists as an external consumer would.
    add_executable(test_atlas_options_compat unit/test_atlas_options_compat.c)
    target_link_libraries(test_atlas_options_compat PRIVATE nt_builder)
    if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(test_atlas_options_compat PRIVATE
            -Wno-missing-field-initializers
            -Wno-error=missing-field-initializers
        )
    endif()
    nt_setup_test_target(test_atlas_options_compat)

    if(WIN32)
        find_program(NT_GIT_BASH_EXECUTABLE bash
            PATHS "C:/Program Files/Git/bin" "C:/Program Files/Git/usr/bin"
            NO_DEFAULT_PATH
        )
    else()
        find_program(NT_GIT_BASH_EXECUTABLE bash)
    endif()
    if(NT_GIT_BASH_EXECUTABLE)
        add_test(
            NAME test_bench_hull_tolerance_guard
            COMMAND "${NT_GIT_BASH_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/scripts/test_bench_hull_tolerance_guard.sh"
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        )
    else()
        message(STATUS "bash not found; skipping CTest registration for test_bench_hull_tolerance_guard")
    endif()

    # --- Basis encode + runtime transcode in one exe ---
    # Transcoder listed first on purpose: the runtime-first consumer link line that
    # must resolve against a single shared basisu TU. Do not reorder.
    add_executable(test_basisu_roundtrip unit/test_basisu_roundtrip.c)
    target_link_libraries(test_basisu_roundtrip PRIVATE nt_basisu_transcoder nt_builder nt_log unity)
    nt_setup_test_target(test_basisu_roundtrip)

    # --- Mesh renderer tests ---
    add_executable(test_mesh_renderer unit/test_mesh_renderer.c)
    target_link_libraries(test_mesh_renderer PRIVATE
        nt_mesh_renderer nt_render nt_gfx_stub
        nt_entity nt_transform_comp nt_mesh_comp nt_material_comp nt_drawable_comp
        nt_material nt_resource nt_hash nt_http_stub nt_fs_stub nt_log_stub unity
    )
    target_compile_definitions(test_mesh_renderer PRIVATE _CRT_SECURE_NO_WARNINGS)
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
        nt_resource nt_hash nt_pool nt_entity nt_shared nt_gfx_stub nt_http_stub nt_fs_stub nt_log_stub unity
    )
    target_compile_definitions(test_sprite_renderer PRIVATE
        _CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_DEPRECATE
    )
    nt_setup_test_target(test_sprite_renderer)

    # test_ui_radial registers in the nt_ui walker-test group below: it needs the
    # shared ui_walker_fixture + full UI libs.

    # --- nt_mem_scratch tests ---
    add_executable(test_nt_mem_scratch unit/test_nt_mem_scratch.c unit/test_helpers/nt_assert_trap.c)
    target_link_libraries(test_nt_mem_scratch PRIVATE
        nt_mem_scratch nt_core nt_log_stub unity
    )
    target_include_directories(test_nt_mem_scratch PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/unit")
    target_compile_definitions(test_nt_mem_scratch PRIVATE _CRT_SECURE_NO_WARNINGS)
    nt_setup_test_target(test_nt_mem_scratch)

    # --- nt_ui element_data macro tests (exercises macros -> scratch path) ---
    add_executable(test_nt_ui_element_data unit/test_nt_ui_element_data.c unit/test_helpers/nt_assert_trap.c)
    target_link_libraries(test_nt_ui_element_data PRIVATE
        nt_ui nt_mem_scratch nt_core
        nt_gfx_stub nt_input_stub nt_resource nt_font nt_material nt_atlas
        nt_sprite_renderer nt_text_renderer nt_hash nt_pool
        nt_http_stub nt_fs_stub nt_log_stub unity
    )
    target_include_directories(test_nt_ui_element_data PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/unit")
    target_compile_definitions(test_nt_ui_element_data PRIVATE _CRT_SECURE_NO_WARNINGS)
    nt_setup_test_target(test_nt_ui_element_data)

    # --- Debug overlay module tests (overlay as nt_metrics consumer + draw bind) ---
    # The overlay reads nt_metrics for the HUD, so the test feeds nt_metrics (count + sample); the
    # draw test still needs the text_renderer/gfx bind chain.
    add_executable(test_debug_overlay unit/test_debug_overlay.c)
    target_link_libraries(test_debug_overlay PRIVATE
        nt_debug_overlay nt_metrics nt_text_renderer nt_font nt_material nt_resource
        nt_gfx_stub nt_hash nt_pool nt_http_stub nt_fs_stub nt_log_stub unity
    )
    target_compile_definitions(test_debug_overlay PRIVATE _CRT_SECURE_NO_WARNINGS)
    nt_setup_test_target(test_debug_overlay)

    # --- nt_ui module + walker tests ---
    # Stub gfx backend + Unity. Death-tests use the shared
    # NT_TEST_EXPECT_ASSERT macro (test_helpers/nt_assert_trap).
    set(_nt_ui_test_helper_sources
        unit/test_helpers/ui_atlas.c
        unit/test_helpers/nt_assert_trap.c
        unit/test_helpers/ui_walker_fixture.c
        # In-memory clipboard impl: nt_ui now references nt_clipboard_get/set_text
        # (Ctrl+C/X/V). The real native backend is headless-flaky and the stub never
        # round-trips, so the test exes link a deterministic fake instead.
        unit/test_helpers/nt_clipboard_fake.c
    )

    # Helpers need `tests/unit/` on the include path so
    # `#include "test_helpers/ui_atlas.h"` resolves.
    set(_nt_ui_test_helper_includes
        "${CMAKE_CURRENT_SOURCE_DIR}/unit"
    )

    add_executable(test_nt_sprite_renderer_emit_region
        unit/test_nt_sprite_renderer_emit_region.c
        ${_nt_ui_test_helper_sources}
    )
    target_include_directories(test_nt_sprite_renderer_emit_region PRIVATE ${_nt_ui_test_helper_includes})
    # nt_ui is linked because the shared ui_walker_fixture helper -- which
    # every nt_ui-test binary depends on -- now compiles unconditionally
    # under the unified NT_TEST_ACCESS define.
    target_link_libraries(test_nt_sprite_renderer_emit_region PRIVATE
        nt_ui nt_sprite_renderer nt_text_renderer nt_font nt_material
        nt_resource nt_hash nt_pool nt_atlas nt_gfx_stub nt_core nt_input_stub nt_http_stub nt_fs_stub nt_log_stub unity
    )
    target_compile_definitions(test_nt_sprite_renderer_emit_region PRIVATE _CRT_SECURE_NO_WARNINGS)
    nt_setup_test_target(test_nt_sprite_renderer_emit_region)

    # Shared list of libs every nt_ui test binary links against. Stub gfx
    # backend; nt_debug_overlay (+ nt_metrics, the perf source of truth it consumes) included for
    # walker tests that publish draw_calls / element_count into nt_metrics and read the HUD back.
    set(_nt_ui_test_libs
        nt_ui nt_sprite_renderer nt_text_renderer nt_font nt_material
        nt_resource nt_hash nt_pool nt_atlas nt_debug_overlay nt_metrics nt_gfx_stub nt_core
        nt_input_stub nt_http_stub nt_fs_stub nt_log_stub unity
    )

    # devapi ui group via submit() over a REAL headless nt_ui ctx (the ONLY automated coverage of
    # ui.tree/element/contexts/click + the Y-up flip + drag whole-or-nothing). Declared HERE (not in the
    # devapi block above) because it reuses the nt_ui test-helper sources/includes, which are defined in
    # this section — a CMake variable read before its set() is empty. Gated on the same devapi options.
    # The heaviest link in the suite: real nt_ui pulls its renderer/resource chain (the ui_walker_fixture
    # helper builds + binds a probe-able ctx headlessly), and nt_devapi_net drives the shared inject
    # schedule so a resolved ui.click coord is observable in g_nt_input.
    #   GNU ld single-pass ordering: consumers before providers. nt_devapi_net/nt_devapi (reference the
    # probe + scheduler symbols) precede nt_ui; nt_ui (references the renderer/resource chain) precedes
    # those providers; nt_input_stub (the inject API the ui group delegates to) + nt_core/nt_log_stub
    # trail last. NT_DEVAPI_GROUP_INPUT is defined too: the ui group delegates scheduling to the input
    # group's single scheduler, and the whole-or-nothing test pre-fills it via input.* commands.
    if(NT_DEVAPI_ENABLED AND NT_DEVAPI_GROUP_UI)
        add_executable(test_devapi_ui
            unit/test_devapi_ui.c
            ${_nt_ui_test_helper_sources}
        )
        target_include_directories(test_devapi_ui PRIVATE ${_nt_ui_test_helper_includes})
        target_link_libraries(test_devapi_ui PRIVATE
            nt_devapi_net nt_devapi_ui nt_devapi_input nt_devapi cjson
            nt_ui nt_sprite_renderer nt_text_renderer nt_font nt_material
            nt_resource nt_hash nt_pool nt_atlas nt_debug_overlay
            nt_window_stub nt_app_stub nt_gfx_stub nt_input_stub
            nt_core nt_http_stub nt_fs_stub nt_log_stub unity
        )
        target_compile_definitions(test_devapi_ui PRIVATE
            _CRT_SECURE_NO_WARNINGS NT_DEVAPI_ENABLED=1
        )
        nt_setup_test_target(test_devapi_ui)
    endif()

    set(_nt_ui_test_names
        test_nt_ui_lifecycle
        test_nt_ui_multictx
        test_nt_ui_begin_end
        test_nt_ui_measure_cb
        test_nt_ui_pointer_state
        test_nt_ui_walker_dispatch
        test_nt_ui_walker_scissor
        test_nt_ui_walker_custom
        test_nt_ui_walker_flush
        test_nt_ui_walker_readonly
        test_nt_ui_walker_batching
        test_nt_ui_label
        test_nt_ui_image
        test_nt_ui_panel
        test_nt_ui_scale
        test_nt_ui_fit
        test_nt_ui_transform
        test_nt_ui_interaction
        test_nt_ui_arbitration
        test_nt_ui_hittest
        test_nt_ui_viewport
        test_nt_ui_3d_hittest
        test_nt_ui_clip_hittest
        test_nt_ui_anim
        test_nt_ui_button
        test_nt_ui_checkbox
        test_nt_ui_radio
        test_nt_ui_toggle
        test_nt_ui_tree_build
        test_nt_ui_state
        test_nt_ui_scroll
        test_nt_ui_modal
        test_nt_ui_slider
        test_nt_ui_vlist
        test_ui_input
        test_nt_ui_progress
        test_ui_events
        test_ui_popup
        test_ui_menu
        test_ui_dropdown
        test_ui_tabbar
        test_ui_tooltip
        test_ui_radial
        test_nt_ui_rich_build
        test_nt_ui_rich_wrap
        test_nt_ui_rich_emit
    )
    # Inspector + hit-zone tests are always compiled; their bodies are guarded
    # by `#if NT_UI_DEBUG_TOOLS` internally so OFF builds compile to a stub
    # main returning 0. Keeps compile_commands.json complete for tidy.
    list(APPEND _nt_ui_test_names
        test_nt_ui_debug_hit_zones
        test_nt_ui_inspector
        test_nt_ui_probe
    )

    foreach(_t IN LISTS _nt_ui_test_names)
        add_executable(${_t}
            unit/${_t}.c
            ${_nt_ui_test_helper_sources}
        )
        target_include_directories(${_t} PRIVATE ${_nt_ui_test_helper_includes})
        target_link_libraries(${_t} PRIVATE ${_nt_ui_test_libs})
        target_compile_definitions(${_t} PRIVATE _CRT_SECURE_NO_WARNINGS)
        nt_setup_test_target(${_t})
    endforeach()

    add_executable(test_nt_ui_stats
        unit/test_nt_ui_stats.c
        ${_nt_ui_test_helper_sources}
    )
    target_include_directories(test_nt_ui_stats PRIVATE ${_nt_ui_test_helper_includes})
    target_link_libraries(test_nt_ui_stats PRIVATE ${_nt_ui_test_libs})
    target_compile_definitions(test_nt_ui_stats PRIVATE _CRT_SECURE_NO_WARNINGS)
    nt_setup_test_target(test_nt_ui_stats)

    # The rich-text PARSER test installs a real log sink to prove malformed markup logs once per
    # distinct message (nt_log_warn_unique dedup). It links the REAL nt_log impl instead of the no-op
    # nt_log_stub the other ui tests use -- so nt_log_add_sink / nt_log_write_unique actually fan out.
    # Provider (nt_log) trails its consumers; nt_hash/nt_core trail last (GNU-ld single-pass order).
    string(REPLACE "nt_log_stub" "nt_log;nt_hash;nt_core" _nt_ui_rich_parse_libs "${_nt_ui_test_libs}")
    add_executable(test_nt_ui_rich_parse
        unit/test_nt_ui_rich_parse.c
        ${_nt_ui_test_helper_sources}
    )
    target_include_directories(test_nt_ui_rich_parse PRIVATE ${_nt_ui_test_helper_includes})
    target_link_libraries(test_nt_ui_rich_parse PRIVATE ${_nt_ui_rich_parse_libs})
    target_compile_definitions(test_nt_ui_rich_parse PRIVATE _CRT_SECURE_NO_WARNINGS)
    nt_setup_test_target(test_nt_ui_rich_parse)

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
    # nt_core links nt_log_interface (header only); the exe picks the impl. Smoke
    # test needs no real logging -> nt_log_stub.
    target_link_libraries(wasm_smoke PRIVATE nt_core nt_log_stub)

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
