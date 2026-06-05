set(UI_BUTTONS_DEMO_OUTPUT_DIR "${CMAKE_SOURCE_DIR}/build/examples/ui_buttons_demo/${NT_PRESET_NAME}")
set(UI_BUTTONS_DEMO_ASSETS_DIR "${UI_BUTTONS_DEMO_OUTPUT_DIR}/assets")
# Shared pack location -- builder writes here once; every preset copies from here.
set(UI_BUTTONS_DEMO_PACK_DIR "${CMAKE_SOURCE_DIR}/build/examples/ui_buttons_demo")

add_executable(ui_buttons_demo main.c)
target_link_libraries(ui_buttons_demo PRIVATE
    nt_core nt_app nt_input nt_window nt_math nt_log nt_time nt_resource
    nt_material nt_shared nt_atlas
    nt_sprite_renderer nt_text_renderer nt_font nt_ui
    nt_render nt_hash nt_mem_scratch nt_stats
)
# Generated asset headers from builder (ui_buttons_demo_assets.h etc.)
target_include_directories(ui_buttons_demo PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/generated")

if(EMSCRIPTEN)
    target_link_libraries(ui_buttons_demo PRIVATE nt_platform_web nt_gfx nt_http nt_fs)
else()
    target_link_libraries(ui_buttons_demo PRIVATE nt_gfx nt_http_stub nt_fs)
endif()

nt_set_warning_flags(ui_buttons_demo)
nt_set_sanitizer_flags(ui_buttons_demo)

set_target_properties(ui_buttons_demo PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${UI_BUTTONS_DEMO_OUTPUT_DIR}"
)

# Pack builder (native only)
if(NOT EMSCRIPTEN)
    add_executable(build_ui_buttons_demo_packs build_packs.c)
    target_link_libraries(build_ui_buttons_demo_packs PRIVATE nt_builder)
    target_compile_definitions(build_ui_buttons_demo_packs PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_compile_options(build_ui_buttons_demo_packs PRIVATE -U_DLL)
    nt_set_warning_flags(build_ui_buttons_demo_packs)
    nt_set_sanitizer_flags(build_ui_buttons_demo_packs)
    set_target_properties(build_ui_buttons_demo_packs PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${UI_BUTTONS_DEMO_OUTPUT_DIR}"
    )
endif()

# Per-preset asset sync: copy ui_buttons_demo.ntpack into preset's assets/
set(UI_BUTTONS_DEMO_PACK_NAMES ui_buttons_demo.ntpack)
set(UI_BUTTONS_DEMO_PACKS_PRESENT TRUE)
foreach(PACK ${UI_BUTTONS_DEMO_PACK_NAMES})
    if(NOT EXISTS "${UI_BUTTONS_DEMO_PACK_DIR}/${PACK}")
        set(UI_BUTTONS_DEMO_PACKS_PRESENT FALSE)
    endif()
endforeach()

if(UI_BUTTONS_DEMO_PACKS_PRESENT)
    foreach(PACK ${UI_BUTTONS_DEMO_PACK_NAMES})
        add_custom_command(
            OUTPUT "${UI_BUTTONS_DEMO_ASSETS_DIR}/${PACK}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${UI_BUTTONS_DEMO_ASSETS_DIR}"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${UI_BUTTONS_DEMO_PACK_DIR}/${PACK}" "${UI_BUTTONS_DEMO_ASSETS_DIR}/${PACK}"
            DEPENDS "${UI_BUTTONS_DEMO_PACK_DIR}/${PACK}"
            COMMENT "Copying ${PACK} -> ${NT_PRESET_NAME}/assets/"
        )
        list(APPEND UI_BUTTONS_DEMO_PACK_OUTPUTS "${UI_BUTTONS_DEMO_ASSETS_DIR}/${PACK}")
    endforeach()
    add_custom_target(ui_buttons_demo_packs DEPENDS ${UI_BUTTONS_DEMO_PACK_OUTPUTS})
    add_dependencies(ui_buttons_demo ui_buttons_demo_packs)
else()
    message(WARNING
        "ui_buttons_demo: source packs not found at ${UI_BUTTONS_DEMO_PACK_DIR}. "
        "Run build_ui_buttons_demo_packs before building ui_buttons_demo, then "
        "re-run cmake configure to wire the per-preset copy step.")
endif()

# Optional CDN base URL
if(NT_CDN_URL)
    target_compile_definitions(ui_buttons_demo PRIVATE NT_CDN_URL="${NT_CDN_URL}")
endif()

# WASM-specific settings
if(EMSCRIPTEN)
    set_target_properties(ui_buttons_demo PROPERTIES SUFFIX ".html" OUTPUT_NAME "index")
    nt_configure_shell(ui_buttons_demo TITLE "UI Buttons Demo - Neotolis Engine" SIMD_WASM_PATH "${NT_WASM_SIMD_WASM_PATH}")
    target_link_options(ui_buttons_demo PRIVATE
        "SHELL:-sUSE_WEBGL2=1"
        "SHELL:-sMAX_WEBGL_VERSION=2"
        "SHELL:-sMIN_WEBGL_VERSION=2"
    )
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        target_link_options(ui_buttons_demo PRIVATE
            "SHELL:-sASSERTIONS=2"
            "SHELL:-gsource-map"
        )
    endif()
endif()
