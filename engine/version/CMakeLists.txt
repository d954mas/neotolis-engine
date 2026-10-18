# nt_version -- engine version with the git commit, generated at build time.
# Opt-in: only its consumers rerun git and relink on a new commit; nt_core (and so
# the builder and every pack) stays independent of the commit.

find_package(Git QUIET)

set(_nt_version_header "${CMAKE_CURRENT_BINARY_DIR}/generated/nt_version_commit.h")
set(_nt_version_cmd "${CMAKE_COMMAND}" "-DGIT=${GIT_EXECUTABLE}" "-DROOT=${NT_ENGINE_ROOT}" "-DOUT=${_nt_version_header}"
    -P "${NT_ENGINE_ROOT}/cmake/nt_version.cmake")
# Configure-time copy: compile-database consumers (clang-tidy, clangd) never run the build step.
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated")
execute_process(COMMAND ${_nt_version_cmd})
add_custom_target(nt_version_gen
    COMMAND ${_nt_version_cmd}
    BYPRODUCTS "${_nt_version_header}"
    VERBATIM
)

nt_add_module(nt_version LOG_DOMAIN "version" nt_version.c nt_version.h)
target_link_libraries(nt_version PUBLIC nt_core)
target_include_directories(nt_version PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
add_dependencies(nt_version nt_version_gen)
