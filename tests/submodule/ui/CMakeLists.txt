# Engine directory policy does not propagate back up into its embedding project.
get_directory_property(_nt_ui_link_options DIRECTORY "${ENGINE_ROOT}" LINK_OPTIONS)
get_directory_property(_nt_ui_compile_options DIRECTORY "${ENGINE_ROOT}" COMPILE_OPTIONS)
get_directory_property(_nt_ui_compile_definitions DIRECTORY "${ENGINE_ROOT}" COMPILE_DEFINITIONS)

function(nt_add_ui_composition name rich effects link_rich)
    add_executable(${name} composition.c)
    nt_set_warning_flags(${name})
    nt_set_sanitizer_flags(${name})
    nt_set_static_crt(${name})
    target_compile_options(${name} PRIVATE ${_nt_ui_compile_options})
    target_compile_definitions(${name} PRIVATE
        ${_nt_ui_compile_definitions}
        NT_COMPOSITION_RICH=${rich}
        NT_COMPOSITION_EFFECTS=${effects})
    target_link_options(${name} PRIVATE ${_nt_ui_link_options})
    if(link_rich)
        target_link_libraries(${name} PRIVATE nt_ui_rich)
    endif()
    if(effects GREATER 0)
        target_link_libraries(${name} PRIVATE nt_ui_rich_fx)
    endif()
    target_link_libraries(${name} PRIVATE
        nt_ui nt_sprite_renderer nt_text_renderer nt_font nt_material
        nt_resource nt_hash nt_pool nt_atlas nt_core nt_mem_scratch
        nt_gfx_stub nt_basisu_transcoder_stub nt_meshwire_stub
        nt_clipboard_stub nt_http_stub)
    if(NOT EMSCRIPTEN)
        target_link_libraries(${name} PRIVATE nt_fs_stub)
    endif()
    target_link_libraries(${name} PRIVATE nt_log_stub)
    if(NOT WIN32)
        target_link_libraries(${name} PRIVATE m)
    endif()
    set_target_properties(${name} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/out")
    if(EMSCRIPTEN)
        target_link_options(${name} PRIVATE
            --emit-symbol-map
            "-Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/out/${name}.map")
        file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/out/${name}.paths"
            CONTENT "$<TARGET_FILE:${name}>\n$<TARGET_FILE_DIR:${name}>/$<TARGET_FILE_BASE_NAME:${name}>.wasm\n$<TARGET_FILE:${name}>.symbols\n")
    endif()
endfunction()

nt_add_ui_composition(ui_plain 0 0 FALSE)
nt_add_ui_composition(ui_custom 1 0 TRUE)
nt_add_ui_composition(ui_wave 1 1 TRUE)
nt_add_ui_composition(ui_all_fx 1 8 TRUE)
nt_add_ui_composition(ui_font_contract 0 0 FALSE)
set_property(TARGET ui_font_contract PROPERTY SOURCES font_contract.c)
nt_add_ui_composition(ui_missing_rich 1 0 FALSE)
set_target_properties(ui_missing_rich PROPERTIES EXCLUDE_FROM_ALL TRUE)

file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/archives.txt"
    CONTENT "$<TARGET_FILE:nt_ui>\n$<TARGET_FILE:nt_ui_rich>\n$<TARGET_FILE:nt_ui_rich_fx>\n")
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/executables.txt" CONTENT "$<TARGET_FILE:ui_plain>\n$<TARGET_FILE:ui_custom>\n$<TARGET_FILE:ui_wave>\n$<TARGET_FILE:ui_all_fx>\n")
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/font-contract.txt" CONTENT "$<TARGET_FILE:ui_font_contract>\n")
