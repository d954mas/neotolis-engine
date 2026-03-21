# cmake/nt_module.cmake
#
# nt_add_module(<target_name> LOG_DOMAIN <domain> <source_files...>)
#
# Creates a STATIC library with standard engine build policy:
# - Include root: engine/ (parent of module directory)
# - Warning flags via nt_set_warning_flags()
# - Sanitizer flags via nt_set_sanitizer_flags()
# - Output directory: build/engine/<preset>/
# - nt:: namespace ALIAS (nt_core -> nt::core, nt_log -> nt::log)
# - NT_LOG_DOMAIN_DEFAULT compile definition for auto domain injection
#
# Example: nt_add_module(nt_core LOG_DOMAIN "core" nt_core.c nt_core.h)

function(nt_add_module name)
    cmake_parse_arguments(PARSE_ARGV 1 NT_MOD "" "LOG_DOMAIN" "")

    if(NOT DEFINED NT_MOD_LOG_DOMAIN)
        message(FATAL_ERROR
            "nt_add_module(${name}): LOG_DOMAIN is required. "
            "Add LOG_DOMAIN \"<domain>\" after the module name."
        )
    endif()

    add_library(${name} STATIC ${NT_MOD_UNPARSED_ARGUMENTS})

    # engine/ is the include root for all modules
    # Enables: #include "core/nt_core.h", #include "log/nt_log.h", etc.
    target_include_directories(${name} PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/..
    )

    # Auto-inject log domain so NT_LOG_* macros resolve
    target_compile_definitions(${name} PRIVATE
        NT_LOG_DOMAIN_DEFAULT="${NT_MOD_LOG_DOMAIN}"
    )

    # Project-wide build policy (from cmake/warnings.cmake)
    nt_set_warning_flags(${name})
    nt_set_sanitizer_flags(${name})

    # Consistent output directory layout
    set_target_properties(${name} PROPERTIES
        ARCHIVE_OUTPUT_DIRECTORY
            "${CMAKE_SOURCE_DIR}/build/engine/${NT_PRESET_NAME}"
    )

    # nt:: namespace ALIAS for configure-time typo detection
    # nt_core -> nt::core, nt_log -> nt::log, nt_log_stub -> nt::log_stub
    string(REPLACE "nt_" "" _alias_suffix "${name}")
    add_library(nt::${_alias_suffix} ALIAS ${name})
endfunction()
