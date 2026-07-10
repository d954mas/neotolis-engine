# nt_fs -- file system module with swappable backends

# Header-only interface carries nt_fs.h's include root; the exe selects the impl.
nt_declare_interface(nt_fs)

if(EMSCRIPTEN)
    nt_add_module(nt_fs LOG_DOMAIN "fs" nt_fs.c web/nt_fs_web.c nt_fs.h)
else()
    nt_add_module(nt_fs LOG_DOMAIN "fs" nt_fs.c native/nt_fs_native.c nt_fs.h)
endif()
target_link_libraries(nt_fs PUBLIC nt_core nt_fs_interface)
if(NOT EMSCRIPTEN)
    target_compile_definitions(nt_fs PRIVATE _CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_DEPRECATE)
    nt_set_static_crt(nt_fs)
endif()

nt_add_module(nt_fs_stub LOG_DOMAIN "fs_stub" nt_fs.c stub/nt_fs_stub.c nt_fs.h)
target_link_libraries(nt_fs_stub PUBLIC nt_core nt_fs_interface)
