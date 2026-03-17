# cgltf -- glTF 2.0 parser (single-header, vendored v1.15)
add_library(cgltf STATIC cgltf_impl.c)
target_include_directories(cgltf PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
# No project warning flags -- third-party code
# Static CRT: remove _DLL so CRT calls resolve to static symbols
target_compile_options(cgltf PRIVATE -U_DLL)
