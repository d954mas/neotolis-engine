set(SP_OUTPUT_DIR "${CMAKE_SOURCE_DIR}/build/examples/sponza/${NT_PRESET_NAME}")
set(SP_ASSETS_DIR "${SP_OUTPUT_DIR}/assets")
set(SP_PACK_DIR "${CMAKE_SOURCE_DIR}/build/examples/sponza")

# Runtime demo (placeholder -- Plan 04 adds main.c)
# add_executable(sponza main.c) -- added in Plan 04

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

# Pack copy targets (added in Plan 04 when main.c exists)
