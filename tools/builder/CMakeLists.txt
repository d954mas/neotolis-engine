# Basis Universal encoder (C++ static library, native only)
enable_language(CXX)

file(GLOB BASISU_ENCODER_SOURCES "${NT_ENGINE_ROOT}/deps/basisu/encoder/*.cpp")
file(GLOB BASISU_ENCODER_3RDPARTY "${NT_ENGINE_ROOT}/deps/basisu/encoder/3rdparty/*.cpp")
set(BASISU_TRANSCODER_SRC "${NT_ENGINE_ROOT}/deps/basisu/transcoder/basisu_transcoder.cpp")

add_library(nt_basisu_encoder STATIC
    nt_basisu_encoder.cpp
    ${BASISU_ENCODER_SOURCES}
    ${BASISU_ENCODER_3RDPARTY}
    ${BASISU_TRANSCODER_SRC}
)
target_include_directories(nt_basisu_encoder PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${NT_ENGINE_ROOT}/deps/basisu/encoder
    ${NT_ENGINE_ROOT}/deps/basisu/transcoder
)
# Encoder requires KTX2 support enabled in transcoder
target_compile_definitions(nt_basisu_encoder PRIVATE
    BASISD_SUPPORT_KTX2=1
    BASISD_SUPPORT_KTX2_ZSTD=0
    _CRT_SECURE_NO_WARNINGS
    # Match ASAN runtime's iterator debug level (0) to avoid /failifmismatch link error
    $<$<CONFIG:Debug>:_ITERATOR_DEBUG_LEVEL=0>
)
# Suppress warnings in vendored C++ code
target_compile_options(nt_basisu_encoder PRIVATE -w)
# SSE4.1 for SIMD-accelerated encoding (x86 only, not WASM or ARM)
if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|i[3-6]86")
    target_compile_definitions(nt_basisu_encoder PRIVATE BASISU_SUPPORT_SSE=1)
    target_compile_options(nt_basisu_encoder PRIVATE -msse4.1)
endif()
# No sanitizers for encoder — vendored code has upstream UB (NaN→uchar in basisu_bc7enc.cpp:1660)
# Static CRT: remove _DLL so CRT calls resolve to static symbols.
# Use MSVC_RUNTIME_LIBRARY to match ASAN runtime (static release CRT) --
# C++ MSVC headers embed /failifmismatch directives that must agree with clang_rt.asan_cxx.
set_target_properties(nt_basisu_encoder PROPERTIES
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:>"
)
target_compile_options(nt_basisu_encoder PRIVATE -U_DLL)
# Link C++ stdlib — needed on Linux where C linker doesn't pull it automatically
if(NOT WIN32)
    target_link_libraries(nt_basisu_encoder PUBLIC stdc++)
endif()
set_target_properties(nt_basisu_encoder PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/tools/builder/${NT_PRESET_NAME}"
)

# nt_builder static library
add_library(nt_builder STATIC
    nt_builder.c
    nt_builder_hash.c
    nt_builder_dump.c
    nt_builder_mesh.c
    nt_builder_texture.c
    nt_builder_shader.c
    nt_builder_include.c
    nt_builder_glob.c
    nt_builder_scene.c
    nt_builder_blob.c
    nt_builder_tangent.c
)
target_include_directories(nt_builder PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(nt_builder PUBLIC nt_shared nt_core nt_log nt_time cgltf stb_image stb_image_resize2 nt_hash mikktspace nt_basisu_encoder)
# Builder uses standard C I/O (fopen, strdup, strncpy) -- suppress MSVC deprecation
target_compile_definitions(nt_builder PRIVATE _CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_DEPRECATE NT_LOG_DOMAIN_DEFAULT="builder")
# Static CRT: remove _DLL so CRT calls resolve to static symbols
target_compile_options(nt_builder PRIVATE -U_DLL)
nt_set_warning_flags(nt_builder)
nt_set_sanitizer_flags(nt_builder)
set_target_properties(nt_builder PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/tools/builder/${NT_PRESET_NAME}"
)

# Builder executable (placeholder, uses nt_builder library)
add_executable(builder main.c)
target_link_libraries(builder PRIVATE nt_builder)
target_compile_definitions(builder PRIVATE NT_LOG_DOMAIN_DEFAULT="builder")
target_compile_options(builder PRIVATE -U_DLL)
nt_set_warning_flags(builder)
nt_set_sanitizer_flags(builder)
set_target_properties(builder PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/tools/builder/${NT_PRESET_NAME}"
)
