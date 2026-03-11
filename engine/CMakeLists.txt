add_subdirectory(core)
add_subdirectory(log)
add_subdirectory(time)
add_subdirectory(app)
add_subdirectory(window)

if(EMSCRIPTEN)
    add_subdirectory(platform/web)
endif()
