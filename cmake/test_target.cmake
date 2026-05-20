# Shared per-test-target setup. Bundles the boilerplate repeated under
# every add_executable in tests/CMakeLists.txt:
#   - -U_DLL: MSVC CRT workaround (link against static CRT, not the DLL one)
#   - nt_set_warning_flags / nt_set_sanitizer_flags
#   - RUNTIME_OUTPUT_DIRECTORY: keep test binaries in build/tests/<preset>/
#   - add_test for ctest registration
#
# Optional WORKING_DIRECTORY argument: sets the cwd for ctest invocation.
# Tests that need to read files from the source tree use this.
#
# Usage:
#   nt_setup_test_target(test_foo)
#   nt_setup_test_target(test_foo WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

function(nt_setup_test_target target_name)
    cmake_parse_arguments(ARG "" "WORKING_DIRECTORY" "" ${ARGN})

    target_compile_options(${target_name} PRIVATE -U_DLL)
    nt_set_warning_flags(${target_name})
    nt_set_sanitizer_flags(${target_name})
    set_target_properties(${target_name} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/tests/${NT_PRESET_NAME}"
    )

    if(ARG_WORKING_DIRECTORY)
        add_test(NAME ${target_name} COMMAND ${target_name}
            WORKING_DIRECTORY "${ARG_WORKING_DIRECTORY}"
        )
    else()
        add_test(NAME ${target_name} COMMAND ${target_name})
    endif()
endfunction()
