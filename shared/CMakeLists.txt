add_library(nt_shared INTERFACE)
target_include_directories(nt_shared INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)

add_library(nt_crc32 STATIC ${CMAKE_CURRENT_SOURCE_DIR}/include/nt_crc32.c)
target_include_directories(nt_crc32 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

# Apply project warning/sanitizer policy when building as part of main project
if(COMMAND nt_set_warning_flags)
    nt_set_warning_flags(nt_crc32)
    nt_set_sanitizer_flags(nt_crc32)
    set_target_properties(nt_crc32 PROPERTIES
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/shared/${NT_PRESET_NAME}"
    )
endif()

target_link_libraries(nt_shared INTERFACE nt_crc32)
