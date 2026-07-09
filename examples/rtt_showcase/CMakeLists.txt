set(RTT_SHOWCASE_OUTPUT_DIR "${CMAKE_SOURCE_DIR}/build/examples/rtt_showcase/${NT_PRESET_NAME}")
set(RTT_SHOWCASE_ASSETS_DIR "${RTT_SHOWCASE_OUTPUT_DIR}/assets")
set(RTT_SHOWCASE_PACK_DIR "${CMAKE_SOURCE_DIR}/build/examples/rtt_showcase")

add_executable(rtt_showcase main.c)
target_link_libraries(rtt_showcase PRIVATE
    nt_core nt_app nt_input nt_window nt_math nt_log nt_time nt_shape_renderer nt_material nt_postfx_blur
    nt_resource nt_atlas nt_hash nt_mem_scratch nt_sprite_renderer nt_text_renderer nt_font nt_ui
    nt_render nt_shared
)
target_include_directories(rtt_showcase PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/generated")
if(EMSCRIPTEN)
    target_link_libraries(rtt_showcase PRIVATE nt_platform_web nt_platform nt_gfx nt_http nt_fs nt_clipboard_stub)
else()
    target_link_libraries(rtt_showcase PRIVATE nt_gfx nt_http_stub nt_fs nt_clipboard_stub nt_platform)
endif()

nt_set_warning_flags(rtt_showcase)
nt_set_sanitizer_flags(rtt_showcase)

set_target_properties(rtt_showcase PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${RTT_SHOWCASE_OUTPUT_DIR}"
)

set(RTT_SHOWCASE_PACK_NAMES rtt_showcase.ntpack)

if(NOT EMSCRIPTEN)
    add_executable(rtt_showcase_build_packs build_packs.c)
    target_link_libraries(rtt_showcase_build_packs PRIVATE nt_builder nt_log)
    target_compile_definitions(rtt_showcase_build_packs PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_compile_options(rtt_showcase_build_packs PRIVATE -U_DLL)
    nt_set_warning_flags(rtt_showcase_build_packs)
    nt_set_sanitizer_flags(rtt_showcase_build_packs)
    set_target_properties(rtt_showcase_build_packs PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${RTT_SHOWCASE_OUTPUT_DIR}"
    )

    foreach(PACK ${RTT_SHOWCASE_PACK_NAMES})
        list(APPEND RTT_SHOWCASE_PACK_FILES "${RTT_SHOWCASE_PACK_DIR}/${PACK}")
    endforeach()
    add_custom_command(
        OUTPUT ${RTT_SHOWCASE_PACK_FILES}
        COMMAND ${CMAKE_COMMAND} -E make_directory "${RTT_SHOWCASE_PACK_DIR}"
        COMMAND "$<TARGET_FILE:rtt_showcase_build_packs>" "${RTT_SHOWCASE_PACK_DIR}"
        DEPENDS rtt_showcase_build_packs
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        COMMENT "Building rtt_showcase pack(s)"
    )

    foreach(PACK ${RTT_SHOWCASE_PACK_NAMES})
        add_custom_command(
            OUTPUT "${RTT_SHOWCASE_ASSETS_DIR}/${PACK}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${RTT_SHOWCASE_ASSETS_DIR}"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${RTT_SHOWCASE_PACK_DIR}/${PACK}" "${RTT_SHOWCASE_ASSETS_DIR}/${PACK}"
            DEPENDS "${RTT_SHOWCASE_PACK_DIR}/${PACK}"
            COMMENT "Copying ${PACK} -> ${NT_PRESET_NAME}/assets/"
        )
        list(APPEND RTT_SHOWCASE_PACK_OUTPUTS "${RTT_SHOWCASE_ASSETS_DIR}/${PACK}")
    endforeach()
    add_custom_target(rtt_showcase_packs DEPENDS ${RTT_SHOWCASE_PACK_OUTPUTS})
    add_dependencies(rtt_showcase rtt_showcase_packs)
else()
    set(RTT_SHOWCASE_PACKS_PRESENT TRUE)
    foreach(PACK ${RTT_SHOWCASE_PACK_NAMES})
        if(NOT EXISTS "${RTT_SHOWCASE_PACK_DIR}/${PACK}")
            set(RTT_SHOWCASE_PACKS_PRESENT FALSE)
        endif()
    endforeach()
    if(RTT_SHOWCASE_PACKS_PRESENT)
        foreach(PACK ${RTT_SHOWCASE_PACK_NAMES})
            add_custom_command(
                OUTPUT "${RTT_SHOWCASE_ASSETS_DIR}/${PACK}"
                COMMAND ${CMAKE_COMMAND} -E make_directory "${RTT_SHOWCASE_ASSETS_DIR}"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${RTT_SHOWCASE_PACK_DIR}/${PACK}" "${RTT_SHOWCASE_ASSETS_DIR}/${PACK}"
                DEPENDS "${RTT_SHOWCASE_PACK_DIR}/${PACK}"
                COMMENT "Copying ${PACK} -> ${NT_PRESET_NAME}/assets/"
            )
            list(APPEND RTT_SHOWCASE_PACK_OUTPUTS "${RTT_SHOWCASE_ASSETS_DIR}/${PACK}")
        endforeach()
        add_custom_target(rtt_showcase_packs DEPENDS ${RTT_SHOWCASE_PACK_OUTPUTS})
        add_dependencies(rtt_showcase rtt_showcase_packs)
    else()
        message(WARNING
            "rtt_showcase: pack not found at ${RTT_SHOWCASE_PACK_DIR}. "
            "Build a native preset first (it produces the pack), then build the wasm preset.")
    endif()
endif()

if(NT_CDN_URL)
    target_compile_definitions(rtt_showcase PRIVATE NT_CDN_URL="${NT_CDN_URL}")
endif()

if(EMSCRIPTEN)
    set_target_properties(rtt_showcase PROPERTIES SUFFIX ".html" OUTPUT_NAME "index")
    nt_configure_shell(rtt_showcase TITLE "RTT Showcase - Neotolis Engine" SIMD_WASM_PATH "${NT_WASM_SIMD_WASM_PATH}")

    target_link_options(rtt_showcase PRIVATE
        "SHELL:-sUSE_WEBGL2=1"
        "SHELL:-sMAX_WEBGL_VERSION=2"
        "SHELL:-sMIN_WEBGL_VERSION=2"
    )

    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        target_link_options(rtt_showcase PRIVATE
            "SHELL:-sASSERTIONS=2"
            "SHELL:-gsource-map"
        )
    endif()

    if(NT_WASM_ANALYSIS)
        target_link_options(rtt_showcase PRIVATE "SHELL:--profiling-funcs")
    endif()
endif()
