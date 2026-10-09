set(AST_OUTPUT_DIR "${CMAKE_SOURCE_DIR}/build/examples/asteroids/${NT_PRESET_NAME}")
set(AST_PACK_DIR "${CMAKE_SOURCE_DIR}/build/examples/asteroids")
add_executable(asteroids main.c)
target_link_libraries(asteroids PRIVATE nt_core nt_platform nt_app nt_input nt_window nt_math nt_log nt_time nt_resource nt_material nt_shared nt_mesh_renderer nt_hash nt_font nt_text_renderer
    nt_ui nt_atlas nt_sprite_renderer nt_mem_scratch)
if(EMSCRIPTEN)
    target_link_libraries(asteroids PRIVATE nt_platform_web nt_gfx nt_http nt_basisu_transcoder_stub nt_meshwire)
else()
    target_link_libraries(asteroids PRIVATE nt_gfx nt_http_stub nt_fs nt_basisu_transcoder_stub nt_meshwire)
endif()
nt_set_warning_flags(asteroids)
nt_set_sanitizer_flags(asteroids)
set_target_properties(asteroids PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${AST_OUTPUT_DIR}")
set(AST_CAN_EXPORT FALSE)
if(NOT EMSCRIPTEN AND NOT CMAKE_CROSSCOMPILING AND CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64|i[3-6]86)$")
    set(AST_CAN_EXPORT TRUE)
endif()
if(NOT EMSCRIPTEN AND NOT AST_CAN_EXPORT)
    # The source-parity data is deliberately pinned to the upstream SSE2 path.
    # Other hosts can run the C17 example using packs generated on an x86 host.
    list(APPEND NT_SKIP_EXAMPLE_PACKS asteroids)
    message(WARNING "asteroids: source-parity exporter requires an x86/SSE2 host; copying existing packs only. Generate packs on x86 and place them in ${AST_PACK_DIR}.")
endif()
if(AST_CAN_EXPORT AND NOT "asteroids" IN_LIST NT_SKIP_EXAMPLE_PACKS)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_subdirectory(reference_generator EXCLUDE_FROM_ALL)
    if(NT_BUILD_TESTS)
        add_custom_target(asteroids_reference_checks ALL DEPENDS asteroids_reference_parity)
    endif()
    set(AST_REFERENCE_DIR "${AST_PACK_DIR}/reference")
    set(AST_REFERENCE_COMMANDS "")
    foreach(AST_UNIQUE 35 50 75 100 200 300 400 500 750 1000)
        list(APPEND AST_REFERENCE_COMMANDS COMMAND $<TARGET_FILE:asteroids_reference_generator>
            --unique ${AST_UNIQUE} --seed 1123 --output "${AST_REFERENCE_DIR}/geometry/${AST_UNIQUE}"
            COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/reference_generator/check_export.py" "${AST_REFERENCE_DIR}/geometry/${AST_UNIQUE}")
    endforeach()
    add_custom_command(OUTPUT "${AST_REFERENCE_DIR}/.stamp"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${AST_REFERENCE_DIR}"
        ${AST_REFERENCE_COMMANDS}
        COMMAND $<TARGET_FILE:asteroids_reference_scene_exporter> "${AST_REFERENCE_DIR}/scene"
        COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/reference_scene_data/check_export.py" "${AST_REFERENCE_DIR}/scene"
        COMMAND ${CMAKE_COMMAND} -E touch "${AST_REFERENCE_DIR}/.stamp"
        DEPENDS asteroids_reference_generator asteroids_reference_scene_exporter
            reference_generator/check_export.py reference_scene_data/check_export.py
        COMMENT "Exporting pinned Methane source geometry, scenes and noise")
    add_custom_target(asteroids_reference_assets DEPENDS "${AST_REFERENCE_DIR}/.stamp")
    add_executable(build_asteroids_packs build_packs.c)
    add_dependencies(build_asteroids_packs asteroids_reference_assets)
    target_link_libraries(build_asteroids_packs PRIVATE nt_builder nt_log nt_meshwire)
    target_compile_definitions(build_asteroids_packs PRIVATE _CRT_SECURE_NO_WARNINGS)
    nt_set_static_crt(build_asteroids_packs)
    nt_set_warning_flags(build_asteroids_packs)
    nt_set_sanitizer_flags(build_asteroids_packs)
    set_target_properties(build_asteroids_packs PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${AST_OUTPUT_DIR}")
endif()
nt_example_packs(NAME asteroids TARGET asteroids BUILDER build_asteroids_packs
    PACK_DIR "${AST_PACK_DIR}" ASSETS_DIR "${AST_OUTPUT_DIR}/assets"
    PACKS asteroids_core.ntpack asteroids_space.ntpack
        asteroids_level_0.ntpack asteroids_level_1.ntpack asteroids_level_2.ntpack
        asteroids_level_3.ntpack asteroids_level_4.ntpack asteroids_level_5.ntpack
        asteroids_level_6.ntpack asteroids_level_7.ntpack asteroids_level_8.ntpack asteroids_level_9.ntpack
        asteroids_noise_10.ntpack asteroids_noise_20.ntpack asteroids_noise_30.ntpack
        asteroids_noise_40.ntpack asteroids_noise_50.ntpack asteroids_ui.ntpack
    INPUT_DIRS raw shaders "${NT_ENGINE_ROOT}/assets/shaders")
if(AST_CAN_EXPORT AND NOT "asteroids" IN_LIST NT_SKIP_EXAMPLE_PACKS)
    # Target ordering alone does not invalidate a completed pack after re-export.
    add_custom_command(OUTPUT "${AST_PACK_DIR}/.asteroids.stamp" APPEND
        DEPENDS "${AST_REFERENCE_DIR}/.stamp")
endif()
if(EMSCRIPTEN AND NT_CDN_URL)
    target_compile_definitions(asteroids PRIVATE NT_CDN_URL="${NT_CDN_URL}")
endif()
nt_configure_shell(asteroids TITLE "Asteroids - Neotolis Engine")
if(EMSCRIPTEN)
    # Emscripten 4.0.19's fullscreen restore helper omits this dependency;
    # keep Closure enabled and explicitly link the existing SDK helper.
    target_link_options(asteroids PRIVATE "-sDEFAULT_LIBRARY_FUNCS_TO_INCLUDE=['$getFullscreenElement']")
    target_link_options(asteroids PRIVATE "--pre-js=${CMAKE_CURRENT_SOURCE_DIR}/web_controls.js")
    set_property(TARGET asteroids APPEND PROPERTY LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/web_controls.js")
endif()

# Keep attribution beside both native and web distributions, not just in source.
configure_file(CREDITS.md "${AST_OUTPUT_DIR}/CREDITS.md" COPYONLY)
configure_file(PARITY.md "${AST_OUTPUT_DIR}/PARITY.md" COPYONLY)
file(GLOB AST_LICENSES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/licenses/*")
foreach(AST_LICENSE IN LISTS AST_LICENSES)
    get_filename_component(AST_LICENSE_NAME "${AST_LICENSE}" NAME)
    configure_file("${AST_LICENSE}" "${AST_OUTPUT_DIR}/licenses/${AST_LICENSE_NAME}" COPYONLY)
endforeach()
