# Shared per-test-target setup. Bundles the boilerplate repeated under
# every add_executable in tests/CMakeLists.txt:
#   - nt_set_static_crt: MSVC CRT pin (static CRT, gated by NT_STATIC_CRT)
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
    cmake_parse_arguments(ARG "" "WORKING_DIRECTORY;TIMEOUT" "" ${ARGN})

    nt_set_static_crt(${target_name})
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
    # An aborted builder assert otherwise hangs ctest indefinitely and keeps the exe locked.
    if(NOT ARG_TIMEOUT)
        set(ARG_TIMEOUT 120)
    endif()
    set_tests_properties(${target_name} PROPERTIES TIMEOUT ${ARG_TIMEOUT})
    # Display-touching tests (real GLFW/GL) share one xvfb display on headless
    # runners: never run two concurrently.
    if(target_name MATCHES "^(test_window_native|test_nt_gfx_render_target_native|test_shape_renderer_ring_native|test_clipboard)$")
        set_tests_properties(${target_name} PROPERTIES RESOURCE_LOCK gl_display)
    endif()
endfunction()
