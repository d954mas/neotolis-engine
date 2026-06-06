# miniaudio -- cross-platform audio playback (single-header, vendored, v0.11.25)
add_library(miniaudio STATIC miniaudio_impl.c)
target_include_directories(miniaudio PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
# Third-party code: suppress its own warnings, no project warning flags.
target_compile_options(miniaudio PRIVATE -w)
# Static CRT on Windows: drop _DLL so CRT calls resolve to static symbols
# (matches deps/stb and deps/glfw).
target_compile_options(miniaudio PRIVATE -U_DLL)
# POSIX backends need pthreads + libm + libdl for the device thread and math.
# Windows loads its audio APIs dynamically; Emscripten needs nothing extra.
if(NOT WIN32 AND NOT EMSCRIPTEN)
    find_package(Threads REQUIRED)
    target_link_libraries(miniaudio PUBLIC Threads::Threads m ${CMAKE_DL_LIBS})
endif()
