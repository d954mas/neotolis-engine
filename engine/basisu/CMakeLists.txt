# nt_basisu_transcoder -- Basis Universal transcoder with C API
# C++ module -- enable_language(CXX) scoped to this subdirectory
enable_language(CXX)

add_library(nt_basisu_transcoder STATIC
    nt_basisu_transcoder.cpp
    ${CMAKE_SOURCE_DIR}/deps/basisu/transcoder/basisu_transcoder.cpp
)

target_include_directories(nt_basisu_transcoder PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(nt_basisu_transcoder PRIVATE ${CMAKE_SOURCE_DIR}/deps/basisu/transcoder)

# -fno-strict-aliasing required by Basis transcoder (TEX-10)
target_compile_options(nt_basisu_transcoder PRIVATE -fno-strict-aliasing)

# Suppress warnings in vendored C++ code
target_compile_options(nt_basisu_transcoder PRIVATE -w)

# Disable unused transcode targets to minimize WASM size (TEX-09, D-16, D-17)
target_compile_definitions(nt_basisu_transcoder PRIVATE
    BASISD_SUPPORT_DXT1=0
    BASISD_SUPPORT_DXT5A=0
    BASISD_SUPPORT_PVRTC1=0
    BASISD_SUPPORT_PVRTC2=0
    BASISD_SUPPORT_FXT1=0
    BASISD_SUPPORT_ATC=0
    BASISD_SUPPORT_KTX2=0
    BASISD_SUPPORT_KTX2_ZSTD=0
    BASISD_SUPPORT_BC7=1
    BASISD_SUPPORT_ASTC=1
    BASISD_SUPPORT_ETC2_EAC_A8=1
)

# Match ASAN runtime: release static CRT + _ITERATOR_DEBUG_LEVEL=0 in debug
target_compile_definitions(nt_basisu_transcoder PRIVATE
    $<$<CONFIG:Debug>:_ITERATOR_DEBUG_LEVEL=0>
)

set_target_properties(nt_basisu_transcoder PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/engine/${NT_PRESET_NAME}"
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:>"
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)
