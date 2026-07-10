set(UI_3D_DEMO_OUTPUT_DIR "${CMAKE_SOURCE_DIR}/build/examples/ui_3d_demo/${NT_PRESET_NAME}")
set(UI_3D_DEMO_ASSETS_DIR "${UI_3D_DEMO_OUTPUT_DIR}/assets")
# Shared pack location -- builder writes here once; every preset copies from here.
set(UI_3D_DEMO_PACK_DIR "${CMAKE_SOURCE_DIR}/build/examples/ui_3d_demo")

add_executable(ui_3d_demo main.c)
target_link_libraries(ui_3d_demo PRIVATE
    nt_core nt_app nt_input nt_window nt_math nt_log nt_time nt_resource
    nt_material nt_shared nt_atlas
    nt_sprite_renderer nt_text_renderer nt_font nt_ui
    nt_shape_renderer
    nt_render nt_hash nt_mem_scratch nt_debug_overlay nt_metrics
)
# Generated asset headers from builder (ui_3d_demo_assets.h etc.)
target_include_directories(ui_3d_demo PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/generated")

# nt_ui calls nt_clipboard_* (no text field here, but the symbols must resolve); stub = no exports.
if(EMSCRIPTEN)
    target_link_libraries(ui_3d_demo PRIVATE nt_platform_web nt_platform nt_gfx nt_http nt_fs nt_clipboard_stub)
else()
    # nt_platform supplies nt_platform_memory_usage for the metrics mem_used channel.
    target_link_libraries(ui_3d_demo PRIVATE nt_gfx nt_http_stub nt_fs nt_clipboard_stub nt_platform)
endif()

nt_set_warning_flags(ui_3d_demo)
nt_set_sanitizer_flags(ui_3d_demo)
set_target_properties(ui_3d_demo PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${UI_3D_DEMO_OUTPUT_DIR}")

set(UI_3D_DEMO_PACK_NAMES ui_3d_demo.ntpack)

# Pack builder + copy wiring.
# Native: the builder runs at BUILD time to produce the pack, then the pack copies into the
# preset's assets/. The copy DEPENDS on the pack output, which DEPENDS on the builder target --
# so a clean configure+build always produces and copies the pack with no reconfigure step.
# Emscripten: the builder can't run under wasm, so the pack must already exist from a prior native
# build; the copy is a plain build-time command on that existing file.
if(NOT EMSCRIPTEN)
    add_executable(build_ui_3d_demo_packs build_packs.c)
    target_link_libraries(build_ui_3d_demo_packs PRIVATE nt_builder nt_log)
    target_compile_definitions(build_ui_3d_demo_packs PRIVATE _CRT_SECURE_NO_WARNINGS)
    nt_set_static_crt(build_ui_3d_demo_packs)
    nt_set_warning_flags(build_ui_3d_demo_packs)
    nt_set_sanitizer_flags(build_ui_3d_demo_packs)
    set_target_properties(build_ui_3d_demo_packs PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${UI_3D_DEMO_OUTPUT_DIR}"
    )

    # build_packs.c reads raw assets via paths relative to the project root.
    foreach(PACK ${UI_3D_DEMO_PACK_NAMES})
        list(APPEND UI_3D_DEMO_PACK_FILES "${UI_3D_DEMO_PACK_DIR}/${PACK}")
    endforeach()
    add_custom_command(
        OUTPUT ${UI_3D_DEMO_PACK_FILES}
        COMMAND ${CMAKE_COMMAND} -E make_directory "${UI_3D_DEMO_PACK_DIR}"
        COMMAND "$<TARGET_FILE:build_ui_3d_demo_packs>" "${UI_3D_DEMO_PACK_DIR}"
        DEPENDS build_ui_3d_demo_packs
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        COMMENT "Building ui_3d_demo pack(s)"
    )

    foreach(PACK ${UI_3D_DEMO_PACK_NAMES})
        add_custom_command(
            OUTPUT "${UI_3D_DEMO_ASSETS_DIR}/${PACK}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${UI_3D_DEMO_ASSETS_DIR}"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${UI_3D_DEMO_PACK_DIR}/${PACK}" "${UI_3D_DEMO_ASSETS_DIR}/${PACK}"
            DEPENDS "${UI_3D_DEMO_PACK_DIR}/${PACK}"
            COMMENT "Copying ${PACK} -> ${NT_PRESET_NAME}/assets/"
        )
        list(APPEND UI_3D_DEMO_PACK_OUTPUTS "${UI_3D_DEMO_ASSETS_DIR}/${PACK}")
    endforeach()
    add_custom_target(ui_3d_demo_packs DEPENDS ${UI_3D_DEMO_PACK_OUTPUTS})
    add_dependencies(ui_3d_demo ui_3d_demo_packs)
else()
    # WASM: builder can't run; copy the pack a prior native build produced into this preset.
    set(UI_3D_DEMO_PACKS_PRESENT TRUE)
    foreach(PACK ${UI_3D_DEMO_PACK_NAMES})
        if(NOT EXISTS "${UI_3D_DEMO_PACK_DIR}/${PACK}")
            set(UI_3D_DEMO_PACKS_PRESENT FALSE)
        endif()
    endforeach()
    if(UI_3D_DEMO_PACKS_PRESENT)
        foreach(PACK ${UI_3D_DEMO_PACK_NAMES})
            add_custom_command(
                OUTPUT "${UI_3D_DEMO_ASSETS_DIR}/${PACK}"
                COMMAND ${CMAKE_COMMAND} -E make_directory "${UI_3D_DEMO_ASSETS_DIR}"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${UI_3D_DEMO_PACK_DIR}/${PACK}" "${UI_3D_DEMO_ASSETS_DIR}/${PACK}"
                DEPENDS "${UI_3D_DEMO_PACK_DIR}/${PACK}"
                COMMENT "Copying ${PACK} -> ${NT_PRESET_NAME}/assets/"
            )
            list(APPEND UI_3D_DEMO_PACK_OUTPUTS "${UI_3D_DEMO_ASSETS_DIR}/${PACK}")
        endforeach()
        add_custom_target(ui_3d_demo_packs DEPENDS ${UI_3D_DEMO_PACK_OUTPUTS})
        add_dependencies(ui_3d_demo ui_3d_demo_packs)
    else()
        message(WARNING
            "ui_3d_demo: pack not found at ${UI_3D_DEMO_PACK_DIR}. "
            "Build a native preset first (it produces the pack), then build the wasm preset.")
    endif()
endif()

# Optional CDN base URL
if(NT_CDN_URL)
    target_compile_definitions(ui_3d_demo PRIVATE NT_CDN_URL="${NT_CDN_URL}")
endif()

# WASM-specific settings
if(EMSCRIPTEN)
    set_target_properties(ui_3d_demo PROPERTIES SUFFIX ".html" OUTPUT_NAME "index")
    nt_configure_shell(ui_3d_demo TITLE "UI 3D Demo - Neotolis Engine" SIMD_WASM_PATH "${NT_WASM_SIMD_WASM_PATH}")
    target_link_options(ui_3d_demo PRIVATE
        "SHELL:-sUSE_WEBGL2=1"
        "SHELL:-sMAX_WEBGL_VERSION=2"
        "SHELL:-sMIN_WEBGL_VERSION=2"
    )
endif()
