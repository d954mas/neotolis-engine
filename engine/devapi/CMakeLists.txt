nt_add_module(nt_devapi LOG_DOMAIN "devapi"
    nt_devapi.c
    nt_devapi_net.c
    nt_devapi.h
)

target_link_libraries(nt_devapi
    PUBLIC
        nt_core
        nt_input
        nt_ui
        nt_entity
        nt_comp_storage
        nt_transform_comp
        nt_sprite_comp
        nt_drawable_comp
    PRIVATE
        nt_log
)

# Compile-time gate (mirrors NT_UI_DEBUG_TOOLS). PUBLIC so consumers see the
# same value through the header's #if.
if(NT_DEVAPI_ENABLED)
    target_compile_definitions(nt_devapi PUBLIC NT_DEVAPI_ENABLED=1)
else()
    target_compile_definitions(nt_devapi PUBLIC NT_DEVAPI_ENABLED=0)
endif()

# Winsock for the native TCP transport.
if(WIN32)
    target_link_libraries(nt_devapi PRIVATE ws2_32)
endif()
