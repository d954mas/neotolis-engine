set(SLICE9_DEMO_OUTPUT_DIR "${CMAKE_SOURCE_DIR}/build/examples/slice9_demo/${NT_PRESET_NAME}")
set(SLICE9_DEMO_ASSETS_DIR "${SLICE9_DEMO_OUTPUT_DIR}/assets")
# Shared pack location -- builder writes here once; every preset copies from here.
set(SLICE9_DEMO_PACK_DIR "${CMAKE_SOURCE_DIR}/build/examples/slice9_demo")

add_executable(slice9_demo main.c)
target_link_libraries(slice9_demo PRIVATE
    nt_core nt_app nt_input nt_window nt_math nt_log nt_time nt_resource
    nt_material nt_shared nt_atlas
    nt_sprite_renderer nt_text_renderer nt_font nt_ui
    nt_render nt_hash nt_mem_scratch nt_stats
)
# Generated asset headers from builder (slice9_demo_assets.h etc.)
target_include_directories(slice9_demo PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/generated")

if(EMSCRIPTEN)
    target_link_libraries(slice9_demo PRIVATE nt_platform_web nt_gfx nt_http nt_fs)
else()
    target_link_libraries(slice9_demo PRIVATE nt_gfx nt_http_stub nt_fs)
endif()

nt_set_warning_flags(slice9_demo)
nt_set_sanitizer_flags(slice9_demo)

set_target_properties(slice9_demo PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${SLICE9_DEMO_OUTPUT_DIR}"
)

# Pack builder (native only)
if(NOT EMSCRIPTEN)
    add_executable(slice9_demo_build_packs build_packs.c)
    target_link_libraries(slice9_demo_build_packs PRIVATE nt_builder)
    target_compile_definitions(slice9_demo_build_packs PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_compile_options(slice9_demo_build_packs PRIVATE -U_DLL)
    nt_set_warning_flags(slice9_demo_build_packs)
    nt_set_sanitizer_flags(slice9_demo_build_packs)
    set_target_properties(slice9_demo_build_packs PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${SLICE9_DEMO_OUTPUT_DIR}"
    )
endif()

# Per-preset asset sync: copy slice9_demo.ntpack into preset's assets/
set(SLICE9_DEMO_PACK_NAMES slice9_demo.ntpack)
set(SLICE9_DEMO_PACKS_PRESENT TRUE)
foreach(PACK ${SLICE9_DEMO_PACK_NAMES})
    if(NOT EXISTS "${SLICE9_DEMO_PACK_DIR}/${PACK}")
        set(SLICE9_DEMO_PACKS_PRESENT FALSE)
    endif()
endforeach()

if(SLICE9_DEMO_PACKS_PRESENT)
    foreach(PACK ${SLICE9_DEMO_PACK_NAMES})
        add_custom_command(
            OUTPUT "${SLICE9_DEMO_ASSETS_DIR}/${PACK}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${SLICE9_DEMO_ASSETS_DIR}"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${SLICE9_DEMO_PACK_DIR}/${PACK}" "${SLICE9_DEMO_ASSETS_DIR}/${PACK}"
            DEPENDS "${SLICE9_DEMO_PACK_DIR}/${PACK}"
            COMMENT "Copying ${PACK} -> ${NT_PRESET_NAME}/assets/"
        )
        list(APPEND SLICE9_DEMO_PACK_OUTPUTS "${SLICE9_DEMO_ASSETS_DIR}/${PACK}")
    endforeach()
    add_custom_target(slice9_demo_packs DEPENDS ${SLICE9_DEMO_PACK_OUTPUTS})
    add_dependencies(slice9_demo slice9_demo_packs)
else()
    message(WARNING
        "slice9_demo: source packs not found at ${SLICE9_DEMO_PACK_DIR}. "
        "Run slice9_demo_build_packs before building slice9_demo, then "
        "re-run cmake configure to wire the per-preset copy step.")
endif()

# Optional CDN base URL
if(NT_CDN_URL)
    target_compile_definitions(slice9_demo PRIVATE NT_CDN_URL="${NT_CDN_URL}")
endif()

# WASM-specific settings
if(EMSCRIPTEN)
    set_target_properties(slice9_demo PROPERTIES SUFFIX ".html" OUTPUT_NAME "index")
    nt_configure_shell(slice9_demo TITLE "Slice9 Demo - Neotolis Engine" SIMD_WASM_PATH "${NT_WASM_SIMD_WASM_PATH}")
    target_link_options(slice9_demo PRIVATE
        "SHELL:-sUSE_WEBGL2=1"
        "SHELL:-sMAX_WEBGL_VERSION=2"
        "SHELL:-sMIN_WEBGL_VERSION=2"
    )
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        target_link_options(slice9_demo PRIVATE
            "SHELL:-sASSERTIONS=2"
            "SHELL:-gsource-map"
        )
    endif()
endif()
