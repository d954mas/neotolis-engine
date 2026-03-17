# stb_image -- image loader (single-header, vendored)
add_library(stb_image STATIC stb_image_impl.c)
target_include_directories(stb_image PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
# No project warning flags -- third-party code
# Static CRT: remove _DLL so CRT calls resolve to static symbols
target_compile_options(stb_image PRIVATE -U_DLL)
