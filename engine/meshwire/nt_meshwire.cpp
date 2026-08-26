#include "nt_meshwire.h"

#include "meshoptimizer.h"

#include <string.h>

/* Re-interleave lives in the impl (not the header) so a SOA mesh reaching a
   stub build traps instead of silently uploading plane-ordered bytes. */

bool nt_meshwire_decode_indices(void *dst, uint32_t index_count, uint32_t elem_size, const uint8_t *src, uint32_t src_size) {
    if (dst == NULL || src == NULL || (elem_size != 2 && elem_size != 4)) {
        return false;
    }
    return meshopt_decodeIndexBuffer(dst, index_count, elem_size, src, src_size) == 0;
}

bool nt_meshwire_reinterleave(uint8_t *dst, const uint8_t *src, uint32_t vertex_count, const uint32_t *stream_elem_sizes, uint32_t stream_count) {
    if (dst == NULL || src == NULL || stream_elem_sizes == NULL || stream_count == 0) {
        return false;
    }
    uint32_t stride = 0;
    for (uint32_t s = 0; s < stream_count; ++s) {
        if (stream_elem_sizes[s] == 0) {
            return false;
        }
        stride += stream_elem_sizes[s];
    }
    const uint8_t *plane = src;
    uint32_t offset = 0;
    for (uint32_t s = 0; s < stream_count; ++s) {
        const uint32_t elem = stream_elem_sizes[s];
        /* Per-element memcpy: planes may start at unaligned offsets. */
        for (uint32_t v = 0; v < vertex_count; ++v) {
            memcpy(dst + (static_cast<size_t>(v) * stride) + offset, plane + (static_cast<size_t>(v) * elem), elem);
        }
        plane += static_cast<size_t>(vertex_count) * elem;
        offset += elem;
    }
    return true;
}
