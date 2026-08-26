# nt_meshwire -- mesh wire-layout decode (meshopt index codec + SoA re-interleave)
# C++ module -- enable_language(CXX) scoped to this subdirectory
enable_language(CXX)

# Header-only interface carries the C API header; the exe selects the impl
# (real decoder or stub) like every other swappable module.
nt_declare_interface(nt_meshwire)

add_library(nt_meshwire STATIC
    nt_meshwire.cpp
    ${NT_ENGINE_ROOT}/deps/meshoptimizer/indexcodec.cpp
)

target_include_directories(nt_meshwire PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>)
target_include_directories(nt_meshwire PRIVATE ${NT_ENGINE_ROOT}/deps/meshoptimizer)
target_link_libraries(nt_meshwire PUBLIC nt_meshwire_interface)

# Suppress warnings in vendored C++ code
target_compile_options(nt_meshwire PRIVATE -w)

nt_set_static_crt_cxx(nt_meshwire)

# Link C++ stdlib — needed on Linux where C linker doesn't pull it automatically
if(NOT WIN32 AND NOT EMSCRIPTEN)
    target_link_libraries(nt_meshwire PUBLIC stdc++)
endif()

set_target_properties(nt_meshwire PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/engine/${NT_PRESET_NAME}"
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)

# Loud-fail stub: exes without builder-packed meshes skip the C++ codec (and
# its stdlib on wasm). Pure C so a stub exe stays C-only.
nt_add_module(nt_meshwire_stub LOG_DOMAIN "meshwire_stub"
    stub/nt_meshwire_stub.c
    nt_meshwire.h
)
target_link_libraries(nt_meshwire_stub PUBLIC nt_core nt_meshwire_interface)
