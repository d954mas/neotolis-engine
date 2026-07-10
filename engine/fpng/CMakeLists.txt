# nt_fpng -- fpng PNG encoder with C API
# C++ module -- enable_language(CXX) scoped to this subdirectory
enable_language(CXX)

# EXCLUDE_FROM_ALL: nt_fpng is linked only by the capture group (engine/devapi, capture-gated) +
# capture_host + the capture unit test. Keep the 3222-line vendored fpng.cpp out of the default `all`
# target in configs that never link it (native-release, all WASM presets, devapi-ON/capture-OFF); it
# is still built on demand when a non-excluded consumer (the capture group) links it.
add_library(nt_fpng STATIC EXCLUDE_FROM_ALL
    nt_fpng.cpp
    ${NT_ENGINE_ROOT}/deps/fpng/fpng.cpp
)

target_include_directories(nt_fpng PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>)
target_include_directories(nt_fpng PRIVATE ${NT_ENGINE_ROOT}/deps/fpng)

# Suppress warnings in vendored C++ code
target_compile_options(nt_fpng PRIVATE -w)

# fpng auto-enables its SSE4.1/pclmul path only on x86/x64 (deps/fpng/fpng.cpp:30-36); GCC/Clang
# need the target features explicitly THERE or the always_inline intrinsics fail to compile. MSVC
# enables them without a flag; WASM (EMSCRIPTEN) and non-x86 native (ARM / Apple Silicon) take fpng's
# scalar path, where -msse4.1/-mpclmul are INVALID driver options (a hard compile error -w cannot
# suppress). Gate the SSE flags on the target ARCH, mirroring tools/builder (x86 only, not WASM/ARM).
if(NOT EMSCRIPTEN AND (CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU"))
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|i[3-6]86")
        target_compile_options(nt_fpng PRIVATE -msse4.1 -mpclmul)
    endif()
    # fpng type-puns uint32/uint64 through uint8_t* on the native unaligned-loads path
    # (FPNG_USE_UNALIGNED_LOADS=1) — UB under default -fstrict-aliasing at -O2. Arch-independent.
    target_compile_options(nt_fpng PRIVATE -fno-strict-aliasing)
endif()

nt_set_static_crt_cxx(nt_fpng)

# Link C++ stdlib — needed on Linux where C linker doesn't pull it automatically
if(NOT WIN32 AND NOT EMSCRIPTEN)
    target_link_libraries(nt_fpng PUBLIC stdc++)
endif()

set_target_properties(nt_fpng PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/engine/${NT_PRESET_NAME}"
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)
