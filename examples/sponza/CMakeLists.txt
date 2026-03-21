set(SP_OUTPUT_DIR "${CMAKE_SOURCE_DIR}/build/examples/sponza/${NT_PRESET_NAME}")
set(SP_ASSETS_DIR "${SP_OUTPUT_DIR}/assets")
set(SP_PACK_DIR "${CMAKE_SOURCE_DIR}/build/examples/sponza")

# Runtime demo
add_executable(sponza main.c)
target_link_libraries(sponza PRIVATE nt_core nt_app nt_input nt_window nt_math nt_log nt_time nt_resource nt_material nt_shared nt_entity nt_transform_comp nt_mesh_comp nt_material_comp nt_drawable_comp nt_mesh_renderer nt_render nt_hash)
if(EMSCRIPTEN)
    target_link_libraries(sponza PRIVATE nt_platform_web nt_gfx nt_http nt_fs)
else()
    target_link_libraries(sponza PRIVATE nt_gfx nt_http_stub nt_fs)
endif()

nt_set_warning_flags(sponza)
nt_set_sanitizer_flags(sponza)

set_target_properties(sponza PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${SP_OUTPUT_DIR}"
)

# Pack builder (native only)
if(NOT EMSCRIPTEN)
    add_executable(build_sponza_packs build_packs.c)
    target_link_libraries(build_sponza_packs PRIVATE nt_builder)
    target_compile_definitions(build_sponza_packs PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_compile_options(build_sponza_packs PRIVATE -U_DLL)
    nt_set_warning_flags(build_sponza_packs)
    nt_set_sanitizer_flags(build_sponza_packs)
    set_target_properties(build_sponza_packs PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${SP_OUTPUT_DIR}"
    )
endif()

# Copy packs from shared location to preset output
set(SP_PACK_NAMES sponza_core.ntpack sponza_geo.ntpack sponza_tex.ntpack sponza_full.ntpack)
foreach(PACK ${SP_PACK_NAMES})
    add_custom_command(
        OUTPUT "${SP_ASSETS_DIR}/${PACK}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${SP_ASSETS_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${SP_PACK_DIR}/${PACK}" "${SP_ASSETS_DIR}/${PACK}"
        DEPENDS "${SP_PACK_DIR}/${PACK}"
        COMMENT "Copying ${PACK} -> ${NT_PRESET_NAME}/assets/"
    )
    list(APPEND SP_PACK_OUTPUTS "${SP_ASSETS_DIR}/${PACK}")
endforeach()
add_custom_target(sponza_packs DEPENDS ${SP_PACK_OUTPUTS})
add_dependencies(sponza sponza_packs)

# Optional CDN base URL (set by CI for deployed builds)
if(NT_CDN_URL)
    target_compile_definitions(sponza PRIVATE NT_CDN_URL="${NT_CDN_URL}")
endif()

# WASM-specific settings
if(EMSCRIPTEN)
    set_target_properties(sponza PROPERTIES SUFFIX ".html" OUTPUT_NAME "index")
    nt_configure_shell(sponza TITLE "Sponza - Neotolis Engine")
    target_link_options(sponza PRIVATE
        "SHELL:-sUSE_WEBGL2=1"
        "SHELL:-sMAX_WEBGL_VERSION=2"
        "SHELL:-sMIN_WEBGL_VERSION=2"
    )
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        target_link_options(sponza PRIVATE
            "SHELL:-sASSERTIONS=2"
            "SHELL:-gsource-map"
        )
    endif()
endif()
