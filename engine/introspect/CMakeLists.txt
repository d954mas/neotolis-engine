# nt_introspect -- dev-only entity introspection facility.
# One TU always compiles; the build-wide NT_INTROSPECT_ENABLED define selects real bodies vs
# zero-footprint no-op stubs for the release/OFF mirror. Components self-register a describe()
# through the entity storage registry; this module renders it to a format-agnostic sink (text here,
# JSON in devapi). Independent of NT_DEVAPI_ENABLED.

nt_add_module(nt_introspect LOG_DOMAIN "introspect"
    nt_introspect.c
    nt_introspect.h
)

# nt_entity PUBLIC: the header takes nt_entity_t and the walk iterates the storage registry.
# nt_core PUBLIC: the header uses its types. nt_log PRIVATE: nt_log_entity formats then writes
# (the symbol resolves at the final-exe link, like the other interface deps).
target_link_libraries(nt_introspect
    PUBLIC nt_core nt_entity
    PRIVATE nt_log_interface
)
