# Scope-local shim: resolves find_package(ZLIB) inside the engine's curl build
# to the vendored deps/zlib. Reached only through the CMAKE_MODULE_PATH prepend
# in the curl block of the root CMakeLists, so a consumer superproject's own
# find_package(ZLIB) never sees it. ZLIB::ZLIB is IMPORTED without GLOBAL —
# visible only in the calling directory (deps/curl) and below.
if(TARGET ZLIB::ZLIB)
    # Same bring-your-own seam as CURL::libcurl — but say so: the consumer owns
    # that zlib's CRT/ABI story.
    message(STATUS "nt_http: curl links the pre-existing ZLIB::ZLIB target; vendored deps/zlib not built")
else()
    if(NOT TARGET zlib_vendored)
        get_filename_component(_nt_zlib_src "${CMAKE_CURRENT_LIST_DIR}/../../deps/zlib" ABSOLUTE)
        add_subdirectory("${_nt_zlib_src}" "${CMAKE_CURRENT_BINARY_DIR}/nt_vendored_zlib" EXCLUDE_FROM_ALL)
    endif()
    add_library(ZLIB::ZLIB INTERFACE IMPORTED)
    set_target_properties(ZLIB::ZLIB PROPERTIES INTERFACE_LINK_LIBRARIES zlib_vendored)
endif()
set(ZLIB_FOUND TRUE)
set(ZLIB_VERSION_STRING "1.3.1")
