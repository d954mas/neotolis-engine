# cmake/nt_module.cmake
#
# nt_add_module(<target_name> <source_files...>)
#
# Creates a STATIC library with standard engine build policy:
# - Include root: engine/ (parent of module directory)
# - Warning flags via nt_set_warning_flags()
# - Sanitizer flags via nt_set_sanitizer_flags()
# - Output directory: build/engine/<preset>/
#
# Example: nt_add_module(nt_core nt_core.c nt_core.h nt_types.h nt_platform.h)

function(nt_add_module name)
    add_library(${name} STATIC ${ARGN})

    # engine/ is the include root for all modules
    # Enables: #include "core/nt_core.h", #include "log/nt_log.h", etc.
    target_include_directories(${name} PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/..
    )

    # Project-wide build policy (from cmake/warnings.cmake)
    nt_set_warning_flags(${name})
    nt_set_sanitizer_flags(${name})

    # Consistent output directory layout
    set_target_properties(${name} PROPERTIES
        ARCHIVE_OUTPUT_DIRECTORY
            "${CMAKE_SOURCE_DIR}/build/engine/${NT_PRESET_NAME}"
    )
endfunction()
