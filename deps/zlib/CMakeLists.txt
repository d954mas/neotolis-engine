# zlib 1.3.1 (vendored, sources only) -- thin wrapper like deps/cjson.
# Upstream's own CMakeLists is deliberately absent: it renames zconf.h inside
# the source tree during configure. The stock zconf.h works as shipped.
# Consumed by deps/curl through find_package(ZLIB) redirected via
# FetchContent OVERRIDE_FIND_PACKAGE (see the curl block in the root CMakeLists).

add_library(zlib_vendored STATIC
    adler32.c
    compress.c
    crc32.c
    deflate.c
    gzclose.c
    gzlib.c
    gzread.c
    gzwrite.c
    infback.c
    inffast.c
    inflate.c
    inftrees.c
    trees.c
    uncompr.c
    zutil.c
)
target_include_directories(zlib_vendored PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>)
if(WIN32)
    target_compile_definitions(zlib_vendored PRIVATE _CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_DEPRECATE)
endif()

add_library(ZLIB::ZLIB ALIAS zlib_vendored)
