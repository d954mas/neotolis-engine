# Basis Universal encoder (C++ static library, native only)
enable_language(CXX)

file(GLOB BASISU_ENCODER_SOURCES "${NT_ENGINE_ROOT}/deps/basisu/encoder/*.cpp")
file(GLOB BASISU_ENCODER_3RDPARTY "${NT_ENGINE_ROOT}/deps/basisu/encoder/3rdparty/*.cpp")

add_library(nt_basisu_encoder STATIC
    nt_basisu_encoder.cpp
    ${BASISU_ENCODER_SOURCES}
    ${BASISU_ENCODER_3RDPARTY}
)
target_include_directories(nt_basisu_encoder PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${NT_ENGINE_ROOT}/deps/basisu/encoder
    ${NT_ENGINE_ROOT}/deps/basisu/transcoder
)
# Transcoder TU comes from the shared runtime target (one copy per exe); the config
# target keeps the encoder's header defines identical to that TU (ODR).
target_link_libraries(nt_basisu_encoder PRIVATE nt_basisu_transcoder nt_basisu_transcoder_config)
target_compile_definitions(nt_basisu_encoder PRIVATE _CRT_SECURE_NO_WARNINGS)
# Suppress warnings in vendored C++ code
target_compile_options(nt_basisu_encoder PRIVATE -w)
# SSE4.1 for SIMD-accelerated encoding (x86 only, not WASM or ARM)
if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|i[3-6]86")
    target_compile_definitions(nt_basisu_encoder PRIVATE BASISU_SUPPORT_SSE=1)
    target_compile_options(nt_basisu_encoder PRIVATE -msse4.1)
endif()
# No sanitizers for encoder — vendored code has upstream UB (NaN→uchar in basisu_bc7enc.cpp:1660)
nt_set_static_crt_cxx(nt_basisu_encoder)
# Link C++ stdlib — needed on Linux where C linker doesn't pull it automatically
if(NOT WIN32)
    target_link_libraries(nt_basisu_encoder PUBLIC stdc++)
endif()
set_target_properties(nt_basisu_encoder PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/tools/builder/${NT_PRESET_NAME}"
)

# Clipper2 (polygon clipping, offset, Minkowski, CDT — upstream C++ sources,
# no nt_ prefix since this is pure vendored code; matches tinycthread/miniz
# convention for third-party libs wrapped without our own glue)
set(CLIPPER2_DIR "${NT_ENGINE_ROOT}/deps/clipper2/CPP/Clipper2Lib")
add_library(clipper2 STATIC
    "${CLIPPER2_DIR}/src/clipper.engine.cpp"
    "${CLIPPER2_DIR}/src/clipper.offset.cpp"
    "${CLIPPER2_DIR}/src/clipper.rectclip.cpp"
    "${CLIPPER2_DIR}/src/clipper.triangulation.cpp"
)
target_include_directories(clipper2 PUBLIC "${CLIPPER2_DIR}/include")
target_compile_definitions(clipper2 PRIVATE _CRT_SECURE_NO_WARNINGS)
target_compile_options(clipper2 PRIVATE -w)
nt_set_static_crt_cxx(clipper2)
set_target_properties(clipper2 PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/tools/builder/${NT_PRESET_NAME}"
)

# Clipper2 C bridge (our C++ → extern "C" wrapper; the nt_ prefix is legit
# because nt_clipper2_bridge.cpp is our code, not vendored).
# Isolated from nt_builder target (nt_builder depends on us, not vice versa)
# so we cannot include nt_builder.h without creating a link cycle on static libs.
add_library(nt_clipper2_bridge STATIC nt_clipper2_bridge.cpp)
target_include_directories(nt_clipper2_bridge PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(nt_clipper2_bridge PUBLIC clipper2)
target_compile_definitions(nt_clipper2_bridge PRIVATE _CRT_SECURE_NO_WARNINGS)
target_compile_options(nt_clipper2_bridge PRIVATE -w)
nt_set_static_crt_cxx(nt_clipper2_bridge)
set_target_properties(nt_clipper2_bridge PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/tools/builder/${NT_PRESET_NAME}"
)

# tinycthread (C11 threads API -- vendored from glfw/deps for cross-platform threading)
add_library(tinycthread STATIC
    "${NT_ENGINE_ROOT}/deps/tinycthread/tinycthread.c"
)
target_include_directories(tinycthread PUBLIC "${NT_ENGINE_ROOT}/deps/tinycthread")
target_compile_definitions(tinycthread PRIVATE _CRT_SECURE_NO_WARNINGS)
# Suppress warnings in vendored C code (upstream has sign-conversion warnings)
target_compile_options(tinycthread PRIVATE -w)
nt_set_static_crt(tinycthread)

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
    nt_builder_codegen.c
    nt_builder_cache.c
    nt_builder_font.c
    nt_builder_atlas.c
    nt_builder_atlas_geometry.c
    nt_builder_atlas_vpack.c
)
target_include_directories(nt_builder PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(nt_builder PUBLIC nt_shared nt_core nt_log_interface nt_time cgltf stb_image stb_image_resize2 stb_truetype stb_image_write nt_hash mikktspace nt_basisu_encoder glfw glad miniz tinycthread nt_clipper2_bridge)
# Builder uses standard C I/O (fopen, strdup, strncpy) -- suppress MSVC deprecation
target_compile_definitions(nt_builder PRIVATE _CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_DEPRECATE NT_LOG_DOMAIN_DEFAULT="builder")
nt_set_static_crt(nt_builder)
nt_set_warning_flags(nt_builder)
nt_set_sanitizer_flags(nt_builder)
set_target_properties(nt_builder PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/tools/builder/${NT_PRESET_NAME}"
)

# Builder executable (placeholder, uses nt_builder library)
add_executable(builder main.c)
target_link_libraries(builder PRIVATE nt_builder nt_log)
target_compile_definitions(builder PRIVATE NT_LOG_DOMAIN_DEFAULT="builder")
nt_set_static_crt(builder)
nt_set_warning_flags(builder)
nt_set_sanitizer_flags(builder)
set_target_properties(builder PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/tools/builder/${NT_PRESET_NAME}"
)
