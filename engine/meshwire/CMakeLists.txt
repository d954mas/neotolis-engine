# nt_meshwire -- mesh wire-layout decode: C port of the meshopt index codec
# (stream format v1) + SoA re-interleave. deps/meshoptimizer stays as the
# test-only byte-parity reference (test_meshwire_diff).

# Header-only interface carries the C API header; the exe selects the impl
# (real decoder or stub) like every other swappable module.
nt_declare_interface(nt_meshwire)

nt_add_module(nt_meshwire LOG_DOMAIN "meshwire"
    nt_meshwire.c
    nt_meshwire.h
    nt_meshwire_codec_internal.h
)
target_link_libraries(nt_meshwire PUBLIC nt_meshwire_interface)

# Builder-side encoder in its own TU/target: runtime binaries link only the
# decoder structurally, without relying on linker GC.
nt_add_module(nt_meshwire_encoder LOG_DOMAIN "meshwire"
    nt_meshwire_encode.c
    nt_meshwire_encode.h
    nt_meshwire_codec_internal.h
)

# Loud-fail stub: exes without builder-packed meshes skip the decoder.
nt_add_module(nt_meshwire_stub LOG_DOMAIN "meshwire_stub"
    stub/nt_meshwire_stub.c
    nt_meshwire.h
)
target_link_libraries(nt_meshwire_stub PUBLIC nt_core nt_meshwire_interface)
