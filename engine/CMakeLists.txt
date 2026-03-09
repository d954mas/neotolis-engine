add_subdirectory(core)

# Transitional shim: nt_engine forwards to nt_core
# Consumers still link nt_engine -- migration happens in Phase 4
add_library(nt_engine INTERFACE)
target_link_libraries(nt_engine INTERFACE nt_core)

# cglm integration stays on nt_engine shim for now
if(TARGET cglm_headers)
    target_link_libraries(nt_engine INTERFACE cglm_headers)
endif()
