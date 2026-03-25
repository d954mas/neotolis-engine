# miniz -- deflate/inflate compression (vendored v3.0.2)
add_library(miniz STATIC miniz.c miniz_tdef.c miniz_tinfl.c miniz_zip.c)
target_include_directories(miniz PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
# Suppress warnings in vendored code
target_compile_options(miniz PRIVATE -w)
# Static CRT: remove _DLL so CRT calls resolve to static symbols
target_compile_options(miniz PRIVATE -U_DLL)
target_compile_definitions(miniz PRIVATE _CRT_SECURE_NO_WARNINGS)
set_target_properties(miniz PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/deps/miniz/${NT_PRESET_NAME}"
)
