# tinyfiledialogs v3.22.1 (sourceforge.net/p/tinyfiledialogs, commit 1fb8c7, zlib) -- native file dialogs.
# Windows: the system dialog (comdlg32, ole32); Linux: zenity, kdialog or other found helpers.
add_library(tinyfiledialogs STATIC tinyfiledialogs.c)
target_include_directories(tinyfiledialogs PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
# No project warning flags -- third-party code
nt_set_static_crt(tinyfiledialogs)
if(WIN32)
    target_link_libraries(tinyfiledialogs PUBLIC comdlg32 ole32)
endif()
