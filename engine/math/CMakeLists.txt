# nt_math — engine math wrapper around cglm.
#
# Header-only (INTERFACE) module. Provides:
#   - cglm with platform workarounds (pragma guards, builtin math)
#   - Single include: "math/nt_math.h"
#
# Link nt_math instead of cglm_headers directly.

add_library(nt_math INTERFACE)
target_link_libraries(nt_math INTERFACE cglm_headers)
if(NOT WIN32)
    target_link_libraries(nt_math INTERFACE m)
endif()
