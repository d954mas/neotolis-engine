set(BUNNYMARK_OUTPUT_DIR "${CMAKE_SOURCE_DIR}/build/examples/bunnymark/${NT_PRESET_NAME}")
set(BUNNYMARK_ASSETS_DIR "${BUNNYMARK_OUTPUT_DIR}/assets")
# Shared pack location -- builder writes here once, all presets copy from here
set(BUNNYMARK_PACK_DIR "${CMAKE_SOURCE_DIR}/build/examples/bunnymark")

add_executable(bunnymark_demo main.c)
target_link_libraries(bunnymark_demo PRIVATE
    nt_core nt_app nt_input nt_window nt_math nt_log nt_time nt_resource
    nt_material nt_shared nt_entity nt_transform_comp nt_material_comp
    nt_drawable_comp nt_sprite_comp nt_atlas nt_sprite_renderer
    nt_stats nt_text_renderer nt_font
    nt_render nt_hash
)
# Generated asset headers from builder (bunnymark_assets.h etc.)
target_include_directories(bunnymark_demo PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/generated")

if(EMSCRIPTEN)
    target_link_libraries(bunnymark_demo PRIVATE nt_platform_web nt_gfx nt_http nt_fs)
else()
    target_link_libraries(bunnymark_demo PRIVATE nt_gfx nt_http_stub nt_fs)
endif()

nt_set_warning_flags(bunnymark_demo)
nt_set_sanitizer_flags(bunnymark_demo)

set_target_properties(bunnymark_demo PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${BUNNYMARK_OUTPUT_DIR}"
)

# Pack builder (native only)
if(NOT EMSCRIPTEN)
    add_executable(build_bunnymark_packs build_packs.c)
    target_link_libraries(build_bunnymark_packs PRIVATE nt_builder)
    target_compile_definitions(build_bunnymark_packs PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_compile_options(build_bunnymark_packs PRIVATE -U_DLL)
    nt_set_warning_flags(build_bunnymark_packs)
    nt_set_sanitizer_flags(build_bunnymark_packs)
    set_target_properties(build_bunnymark_packs PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${BUNNYMARK_OUTPUT_DIR}"
    )
endif()

# HD pack guard (Open Q3 — HD art may not exist yet).
# The demo ships SD-only when examples/bunnymark/raw/hd/ is absent. When the
# user drops 5 PNGs in raw/hd/ and reconfigures, the HD branch in build_packs.c
# emits bunnymark_hd.ntpack and main.c sees BUNNYMARK_HD_AVAILABLE = 1.
set(BUNNYMARK_PACK_NAMES bunnymark_sd.ntpack)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/raw/hd")
    set(BUNNYMARK_HD_AVAILABLE TRUE)
    list(APPEND BUNNYMARK_PACK_NAMES bunnymark_hd.ntpack)
    target_compile_definitions(bunnymark_demo PRIVATE BUNNYMARK_HD_AVAILABLE=1)
    if(NOT EMSCRIPTEN)
        target_compile_definitions(build_bunnymark_packs PRIVATE BUNNYMARK_HD_AVAILABLE=1)
    endif()
    message(STATUS "Bunnymark: HD art detected at examples/bunnymark/raw/hd/ — building both SD and HD packs")
else()
    set(BUNNYMARK_HD_AVAILABLE FALSE)
    message(STATUS "Bunnymark: HD art not present at examples/bunnymark/raw/hd/ — SD-only build")
endif()

foreach(PACK ${BUNNYMARK_PACK_NAMES})
    add_custom_command(
        OUTPUT "${BUNNYMARK_ASSETS_DIR}/${PACK}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${BUNNYMARK_ASSETS_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${BUNNYMARK_PACK_DIR}/${PACK}" "${BUNNYMARK_ASSETS_DIR}/${PACK}"
        DEPENDS "${BUNNYMARK_PACK_DIR}/${PACK}"
        COMMENT "Copying ${PACK} -> ${NT_PRESET_NAME}/assets/"
    )
    list(APPEND BUNNYMARK_PACK_OUTPUTS "${BUNNYMARK_ASSETS_DIR}/${PACK}")
endforeach()
add_custom_target(bunnymark_demo_packs DEPENDS ${BUNNYMARK_PACK_OUTPUTS})
add_dependencies(bunnymark_demo bunnymark_demo_packs)

# Optional CDN base URL
if(NT_CDN_URL)
    target_compile_definitions(bunnymark_demo PRIVATE NT_CDN_URL="${NT_CDN_URL}")
endif()

# WASM-specific settings
if(EMSCRIPTEN)
    set_target_properties(bunnymark_demo PROPERTIES SUFFIX ".html" OUTPUT_NAME "index")
    nt_configure_shell(bunnymark_demo TITLE "Bunnymark Demo - Neotolis Engine" SIMD_WASM_PATH "${NT_WASM_SIMD_WASM_PATH}")
    target_link_options(bunnymark_demo PRIVATE
        "SHELL:-sUSE_WEBGL2=1"
        "SHELL:-sMAX_WEBGL_VERSION=2"
        "SHELL:-sMIN_WEBGL_VERSION=2"
    )
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        target_link_options(bunnymark_demo PRIVATE
            "SHELL:-sASSERTIONS=2"
            "SHELL:-gsource-map"
        )
    endif()
    if(NT_WASM_ANALYSIS)
        target_link_options(bunnymark_demo PRIVATE "SHELL:--profiling-funcs")
    endif()
endif()
