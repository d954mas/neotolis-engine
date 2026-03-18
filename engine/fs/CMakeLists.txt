# nt_fs -- file system module with swappable backends

if(EMSCRIPTEN)
    nt_add_module(nt_fs nt_fs.c web/nt_fs_web.c nt_fs.h)
else()
    nt_add_module(nt_fs nt_fs.c native/nt_fs_native.c nt_fs.h)
endif()
target_link_libraries(nt_fs PUBLIC nt_core)
if(NOT EMSCRIPTEN)
    target_compile_definitions(nt_fs PRIVATE _CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_DEPRECATE)
    # Static CRT: remove _DLL so CRT calls resolve to static symbols
    target_compile_options(nt_fs PRIVATE -U_DLL)
endif()

nt_add_module(nt_fs_stub nt_fs.c stub/nt_fs_stub.c nt_fs.h)
target_link_libraries(nt_fs_stub PUBLIC nt_core)
