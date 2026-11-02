# nt_file_dialog -- the system's open-file dialog over tinyfiledialogs. Native only: the browser has no
# filesystem, so a wasm link naming nt_file_dialog fails and including the header there is a #error.
# EXCLUDE_FROM_ALL: built only when a target links it, so games never compile the dialog code.
if(NOT EMSCRIPTEN)
    nt_add_module(nt_file_dialog LOG_DOMAIN "file_dialog" native/nt_file_dialog_native.c nt_file_dialog.h)
    set_target_properties(nt_file_dialog PROPERTIES EXCLUDE_FROM_ALL ON)
    target_link_libraries(nt_file_dialog PUBLIC nt_core PRIVATE nt_log_interface nt_tinyfiledialogs)
endif()
