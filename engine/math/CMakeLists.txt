# nt_math — engine math wrapper around cglm.
#
# Header-only (INTERFACE) module. Provides:
#   - cglm with platform workarounds (pragma guards, builtin math)
#   - Single include: "math/nt_math.h"
#
# Link nt_math instead of cglm_headers directly.

add_library(nt_math INTERFACE)
target_link_libraries(nt_math INTERFACE cglm_headers)
# Mark cglm includes as SYSTEM so clang-tidy and -Werror skip them
get_target_property(_cglm_inc cglm_headers INTERFACE_INCLUDE_DIRECTORIES)
if(_cglm_inc)
    target_include_directories(nt_math SYSTEM INTERFACE ${_cglm_inc})
endif()
if(NOT WIN32)
    target_link_libraries(nt_math INTERFACE m)
endif()
