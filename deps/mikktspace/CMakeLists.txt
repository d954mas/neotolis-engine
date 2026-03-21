add_library(mikktspace STATIC mikktspace.c)
target_include_directories(mikktspace PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
# Static CRT: remove _DLL so CRT math calls resolve to static symbols
target_compile_options(mikktspace PRIVATE -U_DLL)
