# nt_introspect -- dev-only entity introspection.
# One TU always compiles; the build-wide NT_INTROSPECT_ENABLED define selects real bodies vs
# zero-footprint no-op stubs. Independent of NT_DEVAPI_ENABLED.

nt_add_module(nt_introspect LOG_DOMAIN "introspect"
    nt_introspect.c
    nt_introspect.h
)

# nt_log_interface (PRIVATE): nt_log_entity's symbol resolves at the final-exe link, like other interface deps.
target_link_libraries(nt_introspect
    PUBLIC nt_core nt_entity
    PRIVATE nt_log_interface
)
