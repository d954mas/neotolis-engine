add_library(nt_shared INTERFACE)
target_include_directories(nt_shared INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
# The Basis admission set (root CMakeLists.txt): every consumer of the shared
# codec/format headers sees the same values within one configure.
target_compile_definitions(nt_shared INTERFACE
    NT_BASISU_HAS_ETC1S=${NT_BASISU_HAS_ETC1S}
    NT_BASISU_HAS_UASTC=${NT_BASISU_HAS_UASTC}
    NT_BASISU_HAS_ETC2=${NT_BASISU_HAS_ETC2}
    NT_BASISU_HAS_BC7=${NT_BASISU_HAS_BC7}
    NT_BASISU_HAS_ASTC=${NT_BASISU_HAS_ASTC}
)

add_library(nt_crc32 STATIC ${CMAKE_CURRENT_SOURCE_DIR}/include/nt_crc32.c)
target_include_directories(nt_crc32 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

# Out of line so the float16 rounding paths are not inlined at every cold call site.
add_library(nt_half STATIC ${CMAKE_CURRENT_SOURCE_DIR}/include/nt_half.c)
target_include_directories(nt_half PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

# Apply project warning/sanitizer policy when building as part of main project
if(COMMAND nt_set_warning_flags)
    foreach(_shared_lib nt_crc32 nt_half)
        nt_set_warning_flags(${_shared_lib})
        nt_set_sanitizer_flags(${_shared_lib})
        set_target_properties(${_shared_lib} PROPERTIES
            ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/shared/${NT_PRESET_NAME}"
        )
    endforeach()
endif()

target_link_libraries(nt_shared INTERFACE nt_crc32 nt_half)
