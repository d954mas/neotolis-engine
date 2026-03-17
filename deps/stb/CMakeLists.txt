# stb_image -- image loader (single-header, vendored)
add_library(stb_image STATIC stb_image_impl.c)
target_include_directories(stb_image PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
# No project warning flags -- third-party code
# Static CRT: remove _DLL so CRT calls resolve to static symbols
target_compile_options(stb_image PRIVATE -U_DLL)
# Link math library on Linux (pow, etc.)
if(NOT WIN32 AND NOT EMSCRIPTEN)
    target_link_libraries(stb_image PUBLIC m)
endif()
