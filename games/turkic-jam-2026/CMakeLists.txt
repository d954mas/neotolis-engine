set(TURKIC_JAM_OUTPUT_DIR "${CMAKE_SOURCE_DIR}/build/games/turkic-jam-2026/${NT_PRESET_NAME}")
set(TURKIC_JAM_ASSETS_DIR "${TURKIC_JAM_OUTPUT_DIR}/assets")
# Shared pack location -- builder writes here once; every preset copies from here.
set(TURKIC_JAM_PACK_DIR "${CMAKE_SOURCE_DIR}/build/games/turkic-jam-2026")

add_executable(turkic_jam
    main.c
    src/i18n.c
    src/save.c
    src/ui_kit.c
    src/scenes/scene_menu.c
    src/scenes/scene_game.c
)
target_link_libraries(turkic_jam PRIVATE
    nt_core nt_app nt_input nt_window nt_math nt_log nt_time nt_resource
    nt_material nt_shared nt_atlas
    nt_sprite_renderer nt_text_renderer nt_font nt_ui
    nt_render nt_hash nt_mem_scratch
)
# Generated asset headers from builder + game headers in src/.
target_include_directories(turkic_jam PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/generated"
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

if(EMSCRIPTEN)
    target_link_libraries(turkic_jam PRIVATE nt_platform_web nt_gfx nt_http nt_fs)
else()
    target_link_libraries(turkic_jam PRIVATE nt_gfx nt_http_stub nt_fs)
    # save.c uses fopen for the native KV file.
    target_compile_definitions(turkic_jam PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

nt_set_warning_flags(turkic_jam)
nt_set_sanitizer_flags(turkic_jam)

set_target_properties(turkic_jam PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${TURKIC_JAM_OUTPUT_DIR}"
)

# Pack builder (native only)
if(NOT EMSCRIPTEN)
    add_executable(build_turkic_jam_packs build_packs.c)
    target_link_libraries(build_turkic_jam_packs PRIVATE nt_builder)
    target_compile_definitions(build_turkic_jam_packs PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_compile_options(build_turkic_jam_packs PRIVATE -U_DLL)
    nt_set_warning_flags(build_turkic_jam_packs)
    nt_set_sanitizer_flags(build_turkic_jam_packs)
    set_target_properties(build_turkic_jam_packs PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${TURKIC_JAM_OUTPUT_DIR}"
    )
endif()

# Per-preset asset sync: copy turkic_jam.ntpack into preset's assets/
set(TURKIC_JAM_PACK_NAMES turkic_jam.ntpack)
set(TURKIC_JAM_PACKS_PRESENT TRUE)
foreach(PACK ${TURKIC_JAM_PACK_NAMES})
    if(NOT EXISTS "${TURKIC_JAM_PACK_DIR}/${PACK}")
        set(TURKIC_JAM_PACKS_PRESENT FALSE)
    endif()
endforeach()

if(TURKIC_JAM_PACKS_PRESENT)
    foreach(PACK ${TURKIC_JAM_PACK_NAMES})
        add_custom_command(
            OUTPUT "${TURKIC_JAM_ASSETS_DIR}/${PACK}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${TURKIC_JAM_ASSETS_DIR}"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${TURKIC_JAM_PACK_DIR}/${PACK}" "${TURKIC_JAM_ASSETS_DIR}/${PACK}"
            DEPENDS "${TURKIC_JAM_PACK_DIR}/${PACK}"
            COMMENT "Copying ${PACK} -> ${NT_PRESET_NAME}/assets/"
        )
        list(APPEND TURKIC_JAM_PACK_OUTPUTS "${TURKIC_JAM_ASSETS_DIR}/${PACK}")
    endforeach()
    add_custom_target(turkic_jam_packs DEPENDS ${TURKIC_JAM_PACK_OUTPUTS})
    add_dependencies(turkic_jam turkic_jam_packs)
else()
    message(WARNING
        "turkic_jam: source packs not found at ${TURKIC_JAM_PACK_DIR}. "
        "Run build_turkic_jam_packs before building turkic_jam, then "
        "re-run cmake configure to wire the per-preset copy step.")
endif()

# Optional CDN base URL
if(NT_CDN_URL)
    target_compile_definitions(turkic_jam PRIVATE NT_CDN_URL="${NT_CDN_URL}")
endif()

# WASM-specific settings
if(EMSCRIPTEN)
    set_target_properties(turkic_jam PROPERTIES SUFFIX ".html" OUTPUT_NAME "index")
    nt_configure_shell(turkic_jam TITLE "Turkic Jam 2026 - Neotolis Engine" SIMD_WASM_PATH "${NT_WASM_SIMD_WASM_PATH}")
    target_link_options(turkic_jam PRIVATE
        "SHELL:-sUSE_WEBGL2=1"
        "SHELL:-sMAX_WEBGL_VERSION=2"
        "SHELL:-sMIN_WEBGL_VERSION=2"
    )
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        target_link_options(turkic_jam PRIVATE
            "SHELL:-sASSERTIONS=2"
            "SHELL:-gsource-map"
        )
    endif()
endif()
