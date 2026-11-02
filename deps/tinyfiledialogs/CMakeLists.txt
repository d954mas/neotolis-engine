# tinyfiledialogs v3.22.1 (sourceforge.net/p/tinyfiledialogs, commit 1fb8c7, zlib) -- native file dialogs.
# Windows: the system dialog (comdlg32, ole32, shell32, user32); Linux: zenity, kdialog or other found helpers.
# The nt_ prefix keeps the name free for a game that vendors its own copy.
add_library(nt_tinyfiledialogs STATIC tinyfiledialogs.c)
target_include_directories(nt_tinyfiledialogs PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
# Suppress warnings in vendored code
target_compile_options(nt_tinyfiledialogs PRIVATE -w)
target_compile_definitions(nt_tinyfiledialogs PRIVATE _CRT_SECURE_NO_WARNINGS)
nt_set_static_crt(nt_tinyfiledialogs)
if(WIN32)
    target_link_libraries(nt_tinyfiledialogs PUBLIC comdlg32 ole32 shell32 user32)
endif()
