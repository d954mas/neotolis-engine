#ifndef NT_MESHWIRE_H
#define NT_MESHWIRE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Mesh wire-layout decode: turns the pack's wire form (nt_mesh_format.h v3)
 * into the exact GPU form. Swappable module — exes with no builder-packed
 * meshes link nt_meshwire_stub instead.
 */

/* Decode a MESHOPT index wire stream into a plain index array.
 * dst: index_count * elem_size bytes, elem_size 2 or 4 (per index_type).
 * Every decoded index must be < vertex_count -- a malformed stream that
 * decodes out-of-range (e.g. from unseeded FIFO slots) is rejected, not
 * published. Returns false on a malformed stream (dst contents undefined). */
bool nt_meshwire_decode_indices(void *dst, uint32_t index_count, uint32_t elem_size, const uint8_t *src, uint32_t src_size, uint32_t vertex_count);

/* Re-interleave SOA vertex planes into the interleaved GPU form.
 * src holds one plane per stream in order; plane i element size =
 * stream_elem_sizes[i] (whole attribute), plane i starts at
 * vertex_count * sum of previous element sizes. dst and src are both
 * vertex_count * sum(stream_elem_sizes) bytes and must not overlap (the
 * permutation cannot run in place). Returns false on invalid arguments
 * (stream_count == 0, a zero element size, or overlapping buffers). */
bool nt_meshwire_reinterleave(uint8_t *dst, const uint8_t *src, uint32_t vertex_count, const uint32_t *stream_elem_sizes, uint32_t stream_count);

#ifdef __cplusplus
}
#endif

#endif /* NT_MESHWIRE_H */
