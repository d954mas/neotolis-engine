#include "meshwire/nt_meshwire.h"

#include "core/nt_assert.h"

/* Loud-fail stub: a wire-encoded mesh reaching a stub build is a
   build-composition bug (packed meshes present, decoder not linked) — assert,
   don't mask with garbage vertex data. Under NT_ASSERT_MODE=OFF the false
   returns route into nt_gfx's activate error path (log + FAILED asset). */
#define NT_MESHWIRE_STUB_TRAP() NT_ASSERT(0 && "wire-encoded mesh but nt_meshwire_stub linked -- link nt_meshwire")

bool nt_meshwire_decode_indices(void *dst, uint32_t index_count, uint32_t elem_size, const uint8_t *src, uint32_t src_size, uint32_t vertex_count) {
    (void)dst;
    (void)index_count;
    (void)elem_size;
    (void)src;
    (void)src_size;
    (void)vertex_count;
    NT_MESHWIRE_STUB_TRAP();
    return false;
}

// NOLINTNEXTLINE(readability-non-const-parameter) — out param signature must match the real decoder
bool nt_meshwire_reinterleave(uint8_t *dst, const uint8_t *src, uint32_t vertex_count, const uint32_t *stream_elem_sizes, uint32_t stream_count) {
    (void)dst;
    (void)src;
    (void)vertex_count;
    (void)stream_elem_sizes;
    (void)stream_count;
    NT_MESHWIRE_STUB_TRAP();
    return false;
}
