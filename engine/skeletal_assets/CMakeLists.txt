nt_add_module(nt_skeletal_assets LOG_DOMAIN "skeletal"
    nt_skeletal_assets.c
    nt_skeletal_assets.h
)

# The adapters are the only skeletal code that knows about packs: nt_skeletal
# itself stays free of nt_resource so a headless CPU build links no pack code.
target_link_libraries(nt_skeletal_assets PUBLIC nt_skeletal nt_resource)
target_link_libraries(nt_skeletal_assets PRIVATE nt_shared nt_log_interface)
