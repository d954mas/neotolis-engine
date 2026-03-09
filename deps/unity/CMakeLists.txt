add_library(unity STATIC src/unity.c)
target_include_directories(unity PUBLIC src)

# Exclude float support to avoid _fdclass linker issues on Windows with Clang.
# Engine tests do not need float assertions.
target_compile_definitions(unity PUBLIC UNITY_EXCLUDE_FLOAT UNITY_EXCLUDE_DOUBLE)

# No warning flags -- third-party code compiles with its own defaults
