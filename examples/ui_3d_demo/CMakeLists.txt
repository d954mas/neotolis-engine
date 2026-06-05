set(UI_3D_DEMO_OUTPUT_DIR "${CMAKE_SOURCE_DIR}/build/examples/ui_3d_demo/${NT_PRESET_NAME}")

add_executable(ui_3d_demo main.c)
target_link_libraries(ui_3d_demo PRIVATE
    nt_core nt_app nt_input nt_window nt_math nt_log nt_time
    nt_shape_renderer
)

if(EMSCRIPTEN)
    target_link_libraries(ui_3d_demo PRIVATE nt_platform_web nt_gfx)
else()
    target_link_libraries(ui_3d_demo PRIVATE nt_gfx)
endif()

nt_set_warning_flags(ui_3d_demo)
nt_set_sanitizer_flags(ui_3d_demo)
set_target_properties(ui_3d_demo PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${UI_3D_DEMO_OUTPUT_DIR}")

if(EMSCRIPTEN)
    set_target_properties(ui_3d_demo PROPERTIES SUFFIX ".html" OUTPUT_NAME "index")
    nt_configure_shell(ui_3d_demo TITLE "UI 3D Demo - Neotolis Engine" SIMD_WASM_PATH "${NT_WASM_SIMD_WASM_PATH}")
    target_link_options(ui_3d_demo PRIVATE "SHELL:-sUSE_WEBGL2=1" "SHELL:-sMAX_WEBGL_VERSION=2" "SHELL:-sMIN_WEBGL_VERSION=2")
endif()
