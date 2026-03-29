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

# stb_image_resize2 -- image resizer (single-header, vendored)
add_library(stb_image_resize2 STATIC stb_image_resize2_impl.c)
target_include_directories(stb_image_resize2 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(stb_image_resize2 PRIVATE -U_DLL)
if(NOT WIN32 AND NOT EMSCRIPTEN)
    target_link_libraries(stb_image_resize2 PUBLIC m)
endif()

# stb_truetype -- TrueType font parser (single-header, vendored, builder-only)
add_library(stb_truetype STATIC stb_truetype_impl.c)
target_include_directories(stb_truetype PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(stb_truetype PRIVATE -U_DLL)
# Suppress warnings in vendored code
target_compile_options(stb_truetype PRIVATE -w)
if(NOT WIN32 AND NOT EMSCRIPTEN)
    target_link_libraries(stb_truetype PUBLIC m)
endif()
