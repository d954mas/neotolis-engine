#include "nt_meshwire.h"

#include <string.h>

#include "nt_meshwire_codec_internal.h"

/* Re-interleave lives in the impl (not the header) so a SOA mesh reaching a
   stub build traps instead of silently uploading plane-ordered bytes. */

static void nt_mw_write_triangle(void *destination, uint32_t tri, uint32_t index_size, uint32_t a, uint32_t b, uint32_t c) {
    if (index_size == 2) {
        uint16_t *dst = (uint16_t *)destination + ((size_t)tri * 3);
        dst[0] = (uint16_t)a;
        dst[1] = (uint16_t)b;
        dst[2] = (uint16_t)c;
    } else {
        uint32_t *dst = (uint32_t *)destination + ((size_t)tri * 3);
        dst[0] = a;
        dst[1] = b;
        dst[2] = c;
    }
}

/* Port of meshopt_decodeIndexBuffer (stream format v1 only -- the builder
 * never writes anything else; a v0 stream is rejected as malformed), plus a
 * range gate upstream lacks: every decoded index must be < vertex_count. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity) — mirrors the reference decoder structure 1:1
bool nt_meshwire_decode_indices(void *dst, uint32_t index_count, uint32_t elem_size, const uint8_t *src, uint32_t src_size, uint32_t vertex_count) {
    if (dst == NULL || src == NULL || (elem_size != 2 && elem_size != 4) || index_count % 3 != 0) {
        return false;
    }
    /* the minimum valid encoding is header, 1 byte per triangle and the 16-byte tail */
    if ((uint64_t)src_size < 1ULL + (index_count / 3) + NT_MW_TAIL_SIZE) {
        return false;
    }
    if (src[0] != (NT_MW_INDEX_HEADER | NT_MW_INDEX_VERSION)) {
        return false;
    }

    /* A u16 destination cannot represent indices above 65535: cap the range
       gate so a wide index is rejected instead of truncating in the write */
    uint32_t index_limit = (elem_size == 2 && vertex_count > 0x10000U) ? 0x10000U : vertex_count;

    nt_mw_edge_fifo_t edgefifo;
    memset(edgefifo, -1, sizeof(edgefifo));
    nt_mw_vertex_fifo_t vertexfifo;
    memset(vertexfifo, -1, sizeof(vertexfifo));

    uint32_t edgefifooffset = 0;
    uint32_t vertexfifooffset = 0;

    uint32_t next = 0;
    uint32_t last = 0;

    const uint8_t *code = src + 1;
    const uint8_t *code_end = code + (index_count / 3);
    const uint8_t *data = code_end;

    /* each triangle reads at most 16 bytes past 'data': 1 codeaux + 3 5-byte varints,
     * so staying <= data_safe_end before a read makes the reads themselves safe --
     * the tail provides the padding */
    const uint8_t *data_safe_end = src + src_size - NT_MW_TAIL_SIZE;
    const uint8_t *codeaux_table = data_safe_end;

    uint32_t tri = 0;
    while (code < code_end) {
        uint8_t codetri = *code++;

        if (codetri < 0xF0) {
            int fe = codetri >> 4;

            /* fifo reads wrap around the 16 entry buffer */
            uint32_t a = edgefifo[(edgefifooffset - 1U - (uint32_t)fe) & 15U][0];
            uint32_t b = edgefifo[(edgefifooffset - 1U - (uint32_t)fe) & 15U][1];
            uint32_t c = 0;

            int fec = codetri & 15;

            if (fec < NT_MW_FECMAX) {
                uint32_t cf = vertexfifo[(vertexfifooffset - 1U - (uint32_t)fec) & 15U];
                c = (fec == 0) ? next : cf;

                uint32_t fec0 = (fec == 0) ? 1U : 0U;
                next += fec0;

                nt_mw_push_vertex_fifo(vertexfifo, c, &vertexfifooffset, fec0);
            } else {
                /* covers the worst-case read for this triangle */
                if (data > data_safe_end) {
                    return false;
                }
                /* fec * 2 - 27 decodes 13, 14 into last-1, last+1 */
                c = (fec != 15) ? last + (uint32_t)((fec * 2) - 27) : nt_mw_decode_index(&data, last);
                last = c;
                nt_mw_push_vertex_fifo(vertexfifo, c, &vertexfifooffset, 1);
            }

            nt_mw_push_edge_fifo(edgefifo, c, b, &edgefifooffset);
            nt_mw_push_edge_fifo(edgefifo, a, c, &edgefifooffset);

            if (a >= index_limit || b >= index_limit || c >= index_limit) {
                return false;
            }
            nt_mw_write_triangle(dst, tri++, elem_size, a, b, c);
        } else if (codetri < 0xFE) {
            /* fast path: codeaux from the stream-tail table */
            uint8_t codeaux = codeaux_table[codetri & 15];

            /* note: the table can't contain feb/fec=15 */
            int feb = codeaux >> 4;
            int fec = codeaux & 15;

            /* next increments for all three vertices before index decode -- matches encoder */
            uint32_t a = next++;

            uint32_t bf = vertexfifo[(vertexfifooffset - (uint32_t)feb) & 15U];
            uint32_t b = (feb == 0) ? next : bf;
            uint32_t feb0 = (feb == 0) ? 1U : 0U;
            next += feb0;

            uint32_t cf = vertexfifo[(vertexfifooffset - (uint32_t)fec) & 15U];
            uint32_t c = (fec == 0) ? next : cf;
            uint32_t fec0 = (fec == 0) ? 1U : 0U;
            next += fec0;

            if (a >= index_limit || b >= index_limit || c >= index_limit) {
                return false;
            }
            nt_mw_write_triangle(dst, tri++, elem_size, a, b, c);

            nt_mw_push_vertex_fifo(vertexfifo, a, &vertexfifooffset, 1);
            nt_mw_push_vertex_fifo(vertexfifo, b, &vertexfifooffset, feb0);
            nt_mw_push_vertex_fifo(vertexfifo, c, &vertexfifooffset, fec0);

            nt_mw_push_edge_fifo(edgefifo, b, a, &edgefifooffset);
            nt_mw_push_edge_fifo(edgefifo, c, b, &edgefifooffset);
            nt_mw_push_edge_fifo(edgefifo, a, c, &edgefifooffset);
        } else {
            /* covers the worst-case read for this triangle */
            if (data > data_safe_end) {
                return false;
            }

            /* slow path: full codeaux byte instead of a table lookup */
            uint8_t codeaux = *data++;

            int fea = (codetri == 0xFE) ? 0 : 15;
            int feb = codeaux >> 4;
            int fec = codeaux & 15;

            /* reset: codeaux is 0 but encoded as not-a-table */
            if (codeaux == 0) {
                next = 0;
            }

            /* next increments for all three vertices before index decode -- matches encoder */
            uint32_t a = (fea == 0) ? next++ : 0;
            uint32_t b = (feb == 0) ? next++ : vertexfifo[(vertexfifooffset - (uint32_t)feb) & 15U];
            uint32_t c = (fec == 0) ? next++ : vertexfifo[(vertexfifooffset - (uint32_t)fec) & 15U];

            if (fea == 15) {
                last = a = nt_mw_decode_index(&data, last);
            }
            if (feb == 15) {
                last = b = nt_mw_decode_index(&data, last);
            }
            if (fec == 15) {
                last = c = nt_mw_decode_index(&data, last);
            }

            if (a >= index_limit || b >= index_limit || c >= index_limit) {
                return false;
            }
            nt_mw_write_triangle(dst, tri++, elem_size, a, b, c);

            nt_mw_push_vertex_fifo(vertexfifo, a, &vertexfifooffset, 1);
            nt_mw_push_vertex_fifo(vertexfifo, b, &vertexfifooffset, (feb == 0 || feb == 15) ? 1U : 0U);
            nt_mw_push_vertex_fifo(vertexfifo, c, &vertexfifooffset, (fec == 0 || fec == 15) ? 1U : 0U);

            nt_mw_push_edge_fifo(edgefifo, b, a, &edgefifooffset);
            nt_mw_push_edge_fifo(edgefifo, c, b, &edgefifooffset);
            nt_mw_push_edge_fifo(edgefifo, a, c, &edgefifooffset);
        }
    }

    /* all data bytes consumed, stopping exactly at the codeaux table */
    return data == data_safe_end;
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
            memcpy(dst + ((size_t)v * stride) + offset, plane + ((size_t)v * elem), elem);
        }
        plane += (size_t)vertex_count * elem;
        offset += elem;
    }
    return true;
}
