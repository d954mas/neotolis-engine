# nt_platform -- OS/browser wrappers (process memory probe today).
# Web vs native impl selected at configure time, mirroring engine/window.

if(EMSCRIPTEN)
    nt_add_module(nt_platform LOG_DOMAIN "platform"
        web/nt_platform_mem_web.c
    )
else()
    nt_add_module(nt_platform LOG_DOMAIN "platform"
        native/nt_platform_mem_native.c
    )
endif()

target_link_libraries(nt_platform PUBLIC nt_core)

# Windows: GetProcessMemoryInfo lives in psapi. PUBLIC so an executable linking
# nt_platform inherits it (a static lib's PRIVATE link does not propagate),
# mirroring nt_devapi_net's ws2_32.
if(WIN32)
    target_link_libraries(nt_platform PUBLIC psapi)
endif()
