add_executable(devapi_host main.c)
target_link_libraries(devapi_host PRIVATE nt_core nt_app nt_input nt_window nt_log nt_devapi_net)
if(WIN32)
    # ws2_32 is inherited transitively from nt_devapi_net (PUBLIC link); nt_devapi (core) pulls it via nt_devapi_net.
    # getenv() for the NT_DEVAPI_PORT override (single-threaded host startup).
    target_compile_definitions(devapi_host PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

# Strict warning and sanitizer policy
nt_set_warning_flags(devapi_host)
nt_set_sanitizer_flags(devapi_host)

# Per-project output: build/examples/devapi_host/[preset]/
set_target_properties(devapi_host PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/examples/devapi_host/${NT_PRESET_NAME}"
)
