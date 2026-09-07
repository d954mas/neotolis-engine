# nt_fs -- file system module with swappable backends

# Header-only interface carries nt_fs.h's include root; the exe selects the impl.
nt_declare_interface(nt_fs)

# No web impl: the browser has no filesystem, and nt_resource routes pack loads
# through nt_http there. A wasm link asking for nt_fs fails loudly, by design.
if(NOT EMSCRIPTEN)
    nt_add_module(nt_fs LOG_DOMAIN "fs" nt_fs.c native/nt_fs_native.c nt_fs.h)
    target_link_libraries(nt_fs PUBLIC nt_core nt_fs_interface)
    target_compile_definitions(nt_fs PRIVATE _CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_DEPRECATE)
    nt_set_static_crt(nt_fs)

    nt_add_module(nt_fs_stub LOG_DOMAIN "fs_stub" nt_fs.c stub/nt_fs_stub.c nt_fs.h)
    target_link_libraries(nt_fs_stub PUBLIC nt_core nt_fs_interface)
endif()
