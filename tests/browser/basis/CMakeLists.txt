set(BASIS_FIXTURE_DIR "${CMAKE_SOURCE_DIR}/build/tests/browser/basis")
if(NOT EMSCRIPTEN)
    add_executable(build_basis_fixture EXCLUDE_FROM_ALL build_fixture.c)
    target_link_libraries(build_basis_fixture PRIVATE nt_builder nt_log nt_meshwire)
    target_compile_definitions(build_basis_fixture PRIVATE _CRT_SECURE_NO_WARNINGS)
    nt_set_static_crt(build_basis_fixture)
    nt_set_warning_flags(build_basis_fixture)
    nt_set_sanitizer_flags(build_basis_fixture)
    set_target_properties(build_basis_fixture PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${BASIS_FIXTURE_DIR}/${NT_PRESET_NAME}")
    add_custom_target(basis_fixture_packs
        COMMAND ${CMAKE_COMMAND} -E make_directory "${BASIS_FIXTURE_DIR}"
        COMMAND $<TARGET_FILE:build_basis_fixture> "${BASIS_FIXTURE_DIR}/fixture.ntpack"
        DEPENDS build_basis_fixture
        VERBATIM
    )
else()
    add_executable(browser_basis EXCLUDE_FROM_ALL main.c)
    target_link_libraries(browser_basis PRIVATE
        nt_core nt_app nt_input nt_window nt_log nt_time nt_resource nt_hash nt_shared
        nt_platform_web nt_platform nt_gfx nt_http nt_basisu_transcoder nt_meshwire_stub
    )
    target_compile_definitions(browser_basis PRIVATE NT_TEST_PRESET_NAME="${NT_PRESET_NAME}")
    nt_set_warning_flags(browser_basis)
    nt_set_sanitizer_flags(browser_basis)
    set_target_properties(browser_basis PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${BASIS_FIXTURE_DIR}/${NT_PRESET_NAME}")
    add_custom_target(browser_basis_pack
        COMMAND ${CMAKE_COMMAND} -E make_directory "${BASIS_FIXTURE_DIR}/${NT_PRESET_NAME}/assets"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${BASIS_FIXTURE_DIR}/fixture.ntpack" "${BASIS_FIXTURE_DIR}/${NT_PRESET_NAME}/assets/fixture.ntpack"
        DEPENDS "${BASIS_FIXTURE_DIR}/fixture.ntpack"
        VERBATIM
    )
    add_dependencies(browser_basis browser_basis_pack)
    nt_configure_shell(browser_basis TITLE "Basis LDR verification" SHELL_FILE "${CMAKE_CURRENT_SOURCE_DIR}/shell.html.in")
    set_property(TARGET browser_basis APPEND PROPERTY LINK_DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/browser_basis_shell.html")
endif()
