nt_add_module(nt_skeletal LOG_DOMAIN "skeletal"
    nt_skeletal.c
    nt_skeletal.h
)

target_link_libraries(nt_skeletal PUBLIC nt_core nt_hash)
# The clip view reads the NANM wire header.
target_link_libraries(nt_skeletal PRIVATE nt_shared)
# The sampler's nlerp normalizes with sqrtf; on Linux/GNU ld libm is separate
# from libc (Windows/macOS fold it in). PUBLIC so a test over hand-built clip
# views links it too. Mirrors engine/math + engine/metrics.
if(NOT WIN32)
    target_link_libraries(nt_skeletal PUBLIC m)
endif()

target_compile_definitions(nt_skeletal PUBLIC NT_SKELETAL_CHECKS=$<BOOL:${NT_SKELETAL_CHECKS}>)
