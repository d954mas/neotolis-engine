cmake_minimum_required(VERSION 3.21)
project(submodule_test LANGUAGES C)

# ENGINE_ROOT must be provided: path to neotolis-engine source root
# Usage: cmake -S tests/submodule -B build/submodule-test -DENGINE_ROOT=../..
if(NOT DEFINED ENGINE_ROOT)
    message(FATAL_ERROR "ENGINE_ROOT must be set to the engine source directory")
endif()

add_subdirectory(${ENGINE_ROOT} ${CMAKE_BINARY_DIR}/engine)

add_executable(submodule_test main.c)
target_link_libraries(submodule_test PRIVATE nt_core nt_log)
