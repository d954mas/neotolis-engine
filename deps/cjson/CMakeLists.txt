# cJSON -- JSON parser/printer (vendored v1.7.19, MIT)
# Built with -w (not nt_add_module): cJSON.c trips the engine's -Werror -Wconversion.
add_library(cjson STATIC cJSON.c)
target_include_directories(cjson PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
# Suppress warnings in vendored code
target_compile_options(cjson PRIVATE -w)
# Static CRT: remove _DLL so CRT calls resolve to static symbols
target_compile_options(cjson PRIVATE -U_DLL)
target_compile_definitions(cjson PRIVATE _CRT_SECURE_NO_WARNINGS)
set_target_properties(cjson PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/deps/cjson/${NT_PRESET_NAME}"
)
