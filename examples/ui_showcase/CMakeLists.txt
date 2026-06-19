set(UI_SHOWCASE_OUTPUT_DIR "${CMAKE_SOURCE_DIR}/build/examples/ui_showcase/${NT_PRESET_NAME}")
set(UI_SHOWCASE_ASSETS_DIR "${UI_SHOWCASE_OUTPUT_DIR}/assets")
# Shared pack location -- builder writes here once; every preset copies from here.
set(UI_SHOWCASE_PACK_DIR "${CMAKE_SOURCE_DIR}/build/examples/ui_showcase")

add_executable(ui_showcase main.c)
target_link_libraries(ui_showcase PRIVATE
    nt_core nt_app nt_input nt_window nt_math nt_log nt_time nt_resource
    nt_material nt_shared nt_atlas
    nt_sprite_renderer nt_text_renderer nt_font nt_ui
    nt_render nt_hash nt_mem_scratch nt_debug_overlay
)
# Generated asset headers from builder (ui_showcase_assets.h etc.)
target_include_directories(ui_showcase PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/generated")

# nt_clipboard (real, per platform): nt_ui links nt_clipboard_interface; the exe picks the
# real impl so Ctrl+C/X/V work in the Input tab. web = async cache-on-paste, native = glfw.
if(EMSCRIPTEN)
    target_link_libraries(ui_showcase PRIVATE nt_platform_web nt_gfx nt_http nt_fs nt_clipboard)
else()
    target_link_libraries(ui_showcase PRIVATE nt_gfx nt_http_stub nt_fs nt_clipboard)
endif()

nt_set_warning_flags(ui_showcase)
nt_set_sanitizer_flags(ui_showcase)
set_target_properties(ui_showcase PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${UI_SHOWCASE_OUTPUT_DIR}")

set(UI_SHOWCASE_PACK_NAMES ui_showcase.ntpack)

# Pack builder + copy wiring.
# Native: the builder runs at BUILD time to produce the pack, then the pack copies into the
# preset's assets/. The copy DEPENDS on the pack output, which DEPENDS on the builder target --
# so a clean configure+build always produces and copies the pack with no reconfigure step.
# Emscripten: the builder can't run under wasm, so the pack must already exist from a prior native
# build; the copy is a plain build-time command on that existing file.
if(NOT EMSCRIPTEN)
    add_executable(build_ui_showcase_packs build_packs.c)
    target_link_libraries(build_ui_showcase_packs PRIVATE nt_builder nt_log)
    target_compile_definitions(build_ui_showcase_packs PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_compile_options(build_ui_showcase_packs PRIVATE -U_DLL)
    nt_set_warning_flags(build_ui_showcase_packs)
    nt_set_sanitizer_flags(build_ui_showcase_packs)
    set_target_properties(build_ui_showcase_packs PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${UI_SHOWCASE_OUTPUT_DIR}"
    )

    # build_packs.c reads raw assets via paths relative to the project root.
    foreach(PACK ${UI_SHOWCASE_PACK_NAMES})
        list(APPEND UI_SHOWCASE_PACK_FILES "${UI_SHOWCASE_PACK_DIR}/${PACK}")
    endforeach()
    add_custom_command(
        OUTPUT ${UI_SHOWCASE_PACK_FILES}
        COMMAND ${CMAKE_COMMAND} -E make_directory "${UI_SHOWCASE_PACK_DIR}"
        COMMAND "$<TARGET_FILE:build_ui_showcase_packs>" "${UI_SHOWCASE_PACK_DIR}"
        DEPENDS build_ui_showcase_packs
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        COMMENT "Building ui_showcase pack(s)"
    )

    foreach(PACK ${UI_SHOWCASE_PACK_NAMES})
        add_custom_command(
            OUTPUT "${UI_SHOWCASE_ASSETS_DIR}/${PACK}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${UI_SHOWCASE_ASSETS_DIR}"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${UI_SHOWCASE_PACK_DIR}/${PACK}" "${UI_SHOWCASE_ASSETS_DIR}/${PACK}"
            DEPENDS "${UI_SHOWCASE_PACK_DIR}/${PACK}"
            COMMENT "Copying ${PACK} -> ${NT_PRESET_NAME}/assets/"
        )
        list(APPEND UI_SHOWCASE_PACK_OUTPUTS "${UI_SHOWCASE_ASSETS_DIR}/${PACK}")
    endforeach()
    add_custom_target(ui_showcase_packs DEPENDS ${UI_SHOWCASE_PACK_OUTPUTS})
    add_dependencies(ui_showcase ui_showcase_packs)
else()
    # WASM: builder can't run; copy the pack a prior native build produced into this preset.
    set(UI_SHOWCASE_PACKS_PRESENT TRUE)
    foreach(PACK ${UI_SHOWCASE_PACK_NAMES})
        if(NOT EXISTS "${UI_SHOWCASE_PACK_DIR}/${PACK}")
            set(UI_SHOWCASE_PACKS_PRESENT FALSE)
        endif()
    endforeach()
    if(UI_SHOWCASE_PACKS_PRESENT)
        foreach(PACK ${UI_SHOWCASE_PACK_NAMES})
            add_custom_command(
                OUTPUT "${UI_SHOWCASE_ASSETS_DIR}/${PACK}"
                COMMAND ${CMAKE_COMMAND} -E make_directory "${UI_SHOWCASE_ASSETS_DIR}"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${UI_SHOWCASE_PACK_DIR}/${PACK}" "${UI_SHOWCASE_ASSETS_DIR}/${PACK}"
                DEPENDS "${UI_SHOWCASE_PACK_DIR}/${PACK}"
                COMMENT "Copying ${PACK} -> ${NT_PRESET_NAME}/assets/"
            )
            list(APPEND UI_SHOWCASE_PACK_OUTPUTS "${UI_SHOWCASE_ASSETS_DIR}/${PACK}")
        endforeach()
        add_custom_target(ui_showcase_packs DEPENDS ${UI_SHOWCASE_PACK_OUTPUTS})
        add_dependencies(ui_showcase ui_showcase_packs)
    else()
        message(WARNING
            "ui_showcase: pack not found at ${UI_SHOWCASE_PACK_DIR}. "
            "Build a native preset first (it produces the pack), then build the wasm preset.")
    endif()
endif()

# Optional CDN base URL
if(NT_CDN_URL)
    target_compile_definitions(ui_showcase PRIVATE NT_CDN_URL="${NT_CDN_URL}")
endif()

# WASM-specific settings
if(EMSCRIPTEN)
    set_target_properties(ui_showcase PROPERTIES SUFFIX ".html" OUTPUT_NAME "index")
    nt_configure_shell(ui_showcase TITLE "UI Showcase - Neotolis Engine" SIMD_WASM_PATH "${NT_WASM_SIMD_WASM_PATH}")
    target_link_options(ui_showcase PRIVATE
        "SHELL:-sUSE_WEBGL2=1"
        "SHELL:-sMAX_WEBGL_VERSION=2"
        "SHELL:-sMIN_WEBGL_VERSION=2"
    )
endif()
