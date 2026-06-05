set(UI_3D_DEMO_OUTPUT_DIR "${CMAKE_SOURCE_DIR}/build/examples/ui_3d_demo/${NT_PRESET_NAME}")
set(UI_3D_DEMO_ASSETS_DIR "${UI_3D_DEMO_OUTPUT_DIR}/assets")
# Reuses ui_buttons_demo's pack for slug font + text shaders + button art.
set(UI_BUTTONS_DEMO_PACK_DIR "${CMAKE_SOURCE_DIR}/build/examples/ui_buttons_demo")

add_executable(ui_3d_demo main.c)
target_link_libraries(ui_3d_demo PRIVATE
    nt_core nt_app nt_input nt_window nt_math nt_log nt_time nt_resource
    nt_material nt_shared nt_atlas
    nt_sprite_renderer nt_text_renderer nt_font nt_ui
    nt_shape_renderer
    nt_render nt_hash nt_mem_scratch nt_stats
)
target_include_directories(ui_3d_demo PRIVATE "${CMAKE_SOURCE_DIR}/examples/ui_buttons_demo/generated")

if(EMSCRIPTEN)
    target_link_libraries(ui_3d_demo PRIVATE nt_platform_web nt_gfx nt_http nt_fs)
else()
    target_link_libraries(ui_3d_demo PRIVATE nt_gfx nt_http_stub nt_fs)
endif()

nt_set_warning_flags(ui_3d_demo)
nt_set_sanitizer_flags(ui_3d_demo)
set_target_properties(ui_3d_demo PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${UI_3D_DEMO_OUTPUT_DIR}")

set(UI_3D_DEMO_PACK_NAMES ui_buttons_demo.ntpack)
set(UI_3D_DEMO_PACKS_PRESENT TRUE)
foreach(PACK ${UI_3D_DEMO_PACK_NAMES})
    if(NOT EXISTS "${UI_BUTTONS_DEMO_PACK_DIR}/${PACK}")
        set(UI_3D_DEMO_PACKS_PRESENT FALSE)
    endif()
endforeach()

if(UI_3D_DEMO_PACKS_PRESENT)
    foreach(PACK ${UI_3D_DEMO_PACK_NAMES})
        add_custom_command(
            OUTPUT "${UI_3D_DEMO_ASSETS_DIR}/${PACK}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${UI_3D_DEMO_ASSETS_DIR}"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${UI_BUTTONS_DEMO_PACK_DIR}/${PACK}" "${UI_3D_DEMO_ASSETS_DIR}/${PACK}"
            DEPENDS "${UI_BUTTONS_DEMO_PACK_DIR}/${PACK}"
            COMMENT "Copying ${PACK} -> ui_3d_demo ${NT_PRESET_NAME}/assets/"
        )
        list(APPEND UI_3D_DEMO_PACK_OUTPUTS "${UI_3D_DEMO_ASSETS_DIR}/${PACK}")
    endforeach()
    add_custom_target(ui_3d_demo_packs DEPENDS ${UI_3D_DEMO_PACK_OUTPUTS})
    add_dependencies(ui_3d_demo ui_3d_demo_packs)
else()
    message(WARNING
        "ui_3d_demo: reused pack not found at ${UI_BUTTONS_DEMO_PACK_DIR}. "
        "Build ui_buttons_demo_packs first.")
endif()

if(NT_CDN_URL)
    target_compile_definitions(ui_3d_demo PRIVATE NT_CDN_URL="${NT_CDN_URL}")
endif()

if(EMSCRIPTEN)
    set_target_properties(ui_3d_demo PROPERTIES SUFFIX ".html" OUTPUT_NAME "index")
    nt_configure_shell(ui_3d_demo TITLE "UI 3D Demo - Neotolis Engine" SIMD_WASM_PATH "${NT_WASM_SIMD_WASM_PATH}")
    target_link_options(ui_3d_demo PRIVATE "SHELL:-sUSE_WEBGL2=1" "SHELL:-sMAX_WEBGL_VERSION=2" "SHELL:-sMIN_WEBGL_VERSION=2")
endif()
