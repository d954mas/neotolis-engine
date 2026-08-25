# nt_basisu_transcoder -- Basis Universal transcoder with C API
# C++ module -- enable_language(CXX) scoped to this subdirectory
enable_language(CXX)

# Header-only interface carries the C API header; the exe selects the impl
# (real transcoder or stub) like every other swappable module.
nt_declare_interface(nt_basisu_transcoder)

# Shared transcoder build config. The builder's encoder compiles the same basisu
# headers and links this TU, so its defines must match exactly (ODR).
add_library(nt_basisu_transcoder_config INTERFACE)
# Disable unused transcode targets: minimizes WASM size and caps the formats
# available to BOTH consumers (the builder's validate/stats paths included)
target_compile_definitions(nt_basisu_transcoder_config INTERFACE
    BASISD_SUPPORT_DXT1=0
    BASISD_SUPPORT_DXT5A=0
    BASISD_SUPPORT_PVRTC1=0
    BASISD_SUPPORT_PVRTC2=0
    BASISD_SUPPORT_FXT1=0
    BASISD_SUPPORT_ATC=0
    BASISD_SUPPORT_KTX2_ZSTD=0
    BASISD_SUPPORT_BC7=1
    BASISD_SUPPORT_ASTC=1
    BASISD_SUPPORT_ETC2_EAC_A8=1
)
if(EMSCRIPTEN)
    # Runtime .basis loading never reads KTX2; the encoder does not build on WASM
    target_compile_definitions(nt_basisu_transcoder_config INTERFACE BASISD_SUPPORT_KTX2=0)
else()
    # Native superset: the encoder requires KTX2 support in the shared TU
    target_compile_definitions(nt_basisu_transcoder_config INTERFACE BASISD_SUPPORT_KTX2=1)
endif()

add_library(nt_basisu_transcoder STATIC
    nt_basisu_transcoder.cpp
    ${NT_ENGINE_ROOT}/deps/basisu/transcoder/basisu_transcoder.cpp
)

target_include_directories(nt_basisu_transcoder PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>)
target_include_directories(nt_basisu_transcoder PRIVATE ${NT_ENGINE_ROOT}/deps/basisu/transcoder)
target_link_libraries(nt_basisu_transcoder PRIVATE nt_basisu_transcoder_config)
target_link_libraries(nt_basisu_transcoder PUBLIC nt_basisu_transcoder_interface)

# -fno-strict-aliasing required by Basis transcoder (TEX-10)
target_compile_options(nt_basisu_transcoder PRIVATE -fno-strict-aliasing)

# Suppress warnings in vendored C++ code
target_compile_options(nt_basisu_transcoder PRIVATE -w)

nt_set_static_crt_cxx(nt_basisu_transcoder)

# Link C++ stdlib — needed on Linux where C linker doesn't pull it automatically
if(NOT WIN32 AND NOT EMSCRIPTEN)
    target_link_libraries(nt_basisu_transcoder PUBLIC stdc++)
endif()

set_target_properties(nt_basisu_transcoder PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/engine/${NT_PRESET_NAME}"
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)

# Loud-fail stub: texture-less builds skip the C++ transcoder (and its stdlib
# on wasm). Pure C so a stub exe stays C-only.
nt_add_module(nt_basisu_transcoder_stub LOG_DOMAIN "basisu_stub"
    stub/nt_basisu_stub.c
    nt_basisu_transcoder.h
)
target_link_libraries(nt_basisu_transcoder_stub PUBLIC nt_core nt_basisu_transcoder_interface)
