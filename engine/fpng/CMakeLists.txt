# nt_fpng -- fpng PNG encoder with C API
# C++ module -- enable_language(CXX) scoped to this subdirectory
enable_language(CXX)

add_library(nt_fpng STATIC
    nt_fpng.cpp
    ${NT_ENGINE_ROOT}/deps/fpng/fpng.cpp
)

target_include_directories(nt_fpng PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>)
target_include_directories(nt_fpng PRIVATE ${NT_ENGINE_ROOT}/deps/fpng)

# Suppress warnings in vendored C++ code
target_compile_options(nt_fpng PRIVATE -w)

# fpng auto-enables its SSE4.1/pclmul path on x86/x64 (deps/fpng/fpng.cpp:36); GCC/Clang
# need the target features explicitly or the always_inline intrinsics fail to compile.
# MSVC enables them without a flag; WASM (EMSCRIPTEN) takes fpng's scalar path.
if(NOT EMSCRIPTEN AND (CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU"))
    target_compile_options(nt_fpng PRIVATE -msse4.1 -mpclmul)
endif()

# Match ASAN runtime: release static CRT + _ITERATOR_DEBUG_LEVEL=0 in debug
target_compile_definitions(nt_fpng PRIVATE
    $<$<CONFIG:Debug>:_ITERATOR_DEBUG_LEVEL=0>
)

# Link C++ stdlib — needed on Linux where C linker doesn't pull it automatically
if(NOT WIN32 AND NOT EMSCRIPTEN)
    target_link_libraries(nt_fpng PUBLIC stdc++)
endif()

set_target_properties(nt_fpng PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/engine/${NT_PRESET_NAME}"
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:>"
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)
