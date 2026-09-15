# nt_basisu_transcoder -- Basis Universal transcoder with C API
# C++ module -- enable_language(CXX) scoped to this subdirectory
enable_language(CXX)

# Header-only interface carries the C API header; the exe selects the impl
# (real transcoder or stub) like every other swappable module.
nt_declare_interface(nt_basisu_transcoder)
# The C API speaks nt_texture_format_t / nt_basisu_codec_t.
target_link_libraries(nt_basisu_transcoder_interface INTERFACE nt_shared)

# Shared transcoder build config. The builder's encoder compiles the same basisu
# headers and links this TU, so its defines must match exactly (ODR).
add_library(nt_basisu_transcoder_config INTERFACE)
# Unused transcode targets off for BOTH consumers (the builder's validate/stats
# paths included). ASTC_HIGHER_OPAQUE_QUALITY: 8-bit endpoints for ETC1S->ASTC
# opaque/grayscale blocks; worth its ~28 KB brotli table on web (upstream's
# Emscripten default is 0), and native/web bytes stay identical.
set(NT_BASISU_COMMON_DEFINITIONS
    BASISD_SUPPORT_DXT1=0
    BASISD_SUPPORT_DXT5A=0
    BASISD_SUPPORT_PVRTC1=0
    BASISD_SUPPORT_PVRTC2=0
    BASISD_SUPPORT_FXT1=0
    BASISD_SUPPORT_ATC=0
    BASISD_SUPPORT_KTX2_ZSTD=0
    BASISD_SUPPORT_ASTC_HIGHER_OPAQUE_QUALITY=1
)
target_compile_definitions(nt_basisu_transcoder_config INTERFACE ${NT_BASISU_COMMON_DEFINITIONS})

# Trimmed profile: only the decoders in NT_BASISU_HAS_ETC1S/UASTC and the ETC1S->X
# tables of NT_BASISU_HAS_ETC2/BC7/ASTC (UASTC->X does not use them). XUASTC/HDR/RG11/16bpp are
# never reached by the engine. Needs the local patch in deps/basisu/README.md.
function(nt_basisu_trimmed_definitions out_var)
    set(_defs
        BASISD_SUPPORT_KTX2=0
        BASISD_SUPPORT_ETC2_EAC_RG11=0
        BASISD_SUPPORT_XUASTC=0
        BASISD_SUPPORT_UASTC_HDR=0
        BASISD_SUPPORT_16BPP_FORMATS=0
        BASISD_SUPPORT_ETC1S=${NT_BASISU_HAS_ETC1S}
        BASISD_SUPPORT_UASTC=${NT_BASISU_HAS_UASTC}
    )
    foreach(_pair "BC7:BC7" "ASTC:ASTC" "ETC2_EAC_A8:ETC2")
        string(REPLACE ":" ";" _pair "${_pair}")
        list(GET _pair 0 _basisd)
        list(GET _pair 1 _nt)
        if(NT_BASISU_HAS_${_nt} AND NT_BASISU_HAS_ETC1S)
            list(APPEND _defs BASISD_SUPPORT_${_basisd}=1)
        else()
            list(APPEND _defs BASISD_SUPPORT_${_basisd}=0)
        endif()
    endforeach()
    set(${out_var} ${_defs} PARENT_SCOPE)
endfunction()

if(EMSCRIPTEN)
    # Runtime .basis loading never reads KTX2; the encoder does not build on WASM
    nt_basisu_trimmed_definitions(_nt_basisu_wasm_defs)
    target_compile_definitions(nt_basisu_transcoder_config INTERFACE ${_nt_basisu_wasm_defs})
else()
    # Native superset: the encoder shares this TU (its headers need KTX2 and
    # XUASTC, its validation paths call the transcoder), so native keeps the
    # upstream LDR set unchanged and the wrapper enforces NT_BASISU_* on top.
    target_compile_definitions(nt_basisu_transcoder_config INTERFACE
        BASISD_SUPPORT_KTX2=1
        BASISD_SUPPORT_ETC1S=1
        BASISD_SUPPORT_UASTC=1
        BASISD_SUPPORT_BC7=1
        BASISD_SUPPORT_ASTC=1
        BASISD_SUPPORT_ETC2_EAC_A8=1
    )
endif()

set(NT_BASISU_TRANSCODER_SOURCES
    nt_basisu_transcoder.cpp
    ${NT_ENGINE_ROOT}/deps/basisu/transcoder/basisu_transcoder.cpp
)

# Compiler/CRT settings the vendored TU needs, shared by the production library
# and the test-only trimmed mirror below.
function(nt_basisu_transcoder_settings target)
    target_include_directories(${target} PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>)
    target_include_directories(${target} PRIVATE ${NT_ENGINE_ROOT}/deps/basisu/transcoder)
    target_link_libraries(${target} PRIVATE nt_core)
    target_link_libraries(${target} PUBLIC nt_basisu_transcoder_interface)
    # The vendored transcoder type-puns block data and needs -fno-strict-aliasing.
    target_compile_options(${target} PRIVATE -fno-strict-aliasing)
    # Suppress warnings in vendored C++ code
    target_compile_options(${target} PRIVATE -w)
    nt_set_static_crt_cxx(${target})
    # Link C++ stdlib — needed on Linux where C linker doesn't pull it automatically
    if(NOT WIN32 AND NOT EMSCRIPTEN)
        target_link_libraries(${target} PUBLIC stdc++)
    endif()
    set_target_properties(${target} PROPERTIES
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/engine/${NT_PRESET_NAME}"
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED ON
    )
endfunction()

add_library(nt_basisu_transcoder STATIC ${NT_BASISU_TRANSCODER_SOURCES})
target_link_libraries(nt_basisu_transcoder PRIVATE nt_basisu_transcoder_config)
nt_basisu_transcoder_settings(nt_basisu_transcoder)

# Test-only native mirror of the WASM transcoder: the same TU with the trimmed
# defines, so ctest can decode against goldens exactly as the web build will.
# Top-level project only; it cannot share an executable with the encoder.
if(NT_BUILDING_TESTS AND NT_BUILD_TESTS AND NOT EMSCRIPTEN)
    nt_basisu_trimmed_definitions(_nt_basisu_trimmed_defs)
    add_library(nt_basisu_transcoder_trimmed_test STATIC EXCLUDE_FROM_ALL ${NT_BASISU_TRANSCODER_SOURCES})
    target_compile_definitions(nt_basisu_transcoder_trimmed_test PRIVATE ${NT_BASISU_COMMON_DEFINITIONS} ${_nt_basisu_trimmed_defs})
    nt_basisu_transcoder_settings(nt_basisu_transcoder_trimmed_test)
endif()

# Loud-fail stub: texture-less builds skip the C++ transcoder (and its stdlib
# on wasm). Pure C so a stub exe stays C-only.
nt_add_module(nt_basisu_transcoder_stub LOG_DOMAIN "basisu_stub"
    stub/nt_basisu_stub.c
    nt_basisu_transcoder.h
)
target_link_libraries(nt_basisu_transcoder_stub PUBLIC nt_core nt_basisu_transcoder_interface)
