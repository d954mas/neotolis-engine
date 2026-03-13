# glad -- generated OpenGL 3.3 Core loader
add_library(glad STATIC src/gl.c)
target_include_directories(glad PUBLIC include)
