#ifndef NT_MESHWIRE_ENCODE_H
#define NT_MESHWIRE_ENCODE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Builder-side index encoder (meshopt stream format v1). Separate TU from the
 * decoder so runtime binaries never carry encode code. Output decodes with
 * nt_meshwire_decode_indices; byte-parity with the vendored reference encoder
 * is pinned by test_meshwire_diff.
 */

/* Worst-case encoded size for index_count indices over vertex_count vertices.
 * Returns 0 on overflow (index_count beyond any builder limit). */
uint32_t nt_meshwire_encode_indices_bound(uint32_t index_count, uint32_t vertex_count);

/* Encode a triangle list (index_count % 3 == 0, every index < vertex_count of
 * the bound call). dst_size must come from nt_meshwire_encode_indices_bound.
 * Returns the encoded size, or 0 when dst is too small. */
uint32_t nt_meshwire_encode_indices(uint8_t *dst, uint32_t dst_size, const uint32_t *indices, uint32_t index_count);

#ifdef __cplusplus
}
#endif

#endif /* NT_MESHWIRE_ENCODE_H */
