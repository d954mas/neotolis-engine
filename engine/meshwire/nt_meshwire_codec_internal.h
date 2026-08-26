#ifndef NT_MESHWIRE_CODEC_INTERNAL_H
#define NT_MESHWIRE_CODEC_INTERNAL_H

#include <stdint.h>

/*
 * Internal helpers shared by the index codec encoder and decoder TUs.
 *
 * C port of the meshopt index codec, stream format version 1. Byte-compatible
 * with deps/meshoptimizer indexcodec.cpp (kept as a test-only reference;
 * test_meshwire_diff proves encode parity and cross-decode equality).
 * Based on:
 *   Fabian Giesen. Simple lossless index buffer compression & follow-up. 2013
 *   Conor Stokes. Vertex Cache Optimised Index Buffer Compression. 2014
 *
 * The encoder and decoder FIFO updates must mirror each other EXACTLY —
 * the stream carries FIFO positions, not values.
 */

#define NT_MW_INDEX_HEADER 0xE0U
#define NT_MW_INDEX_VERSION 1
/* Stream tail: 16-byte codeaux table doubling as read-ahead padding */
#define NT_MW_TAIL_SIZE 16U
/* Version 1 reserves fec 13/14 for last-1/last+1 strip codes */
#define NT_MW_FECMAX 13

typedef uint32_t nt_mw_vertex_fifo_t[16];
typedef uint32_t nt_mw_edge_fifo_t[16][2];

static inline int nt_mw_get_edge_fifo(const nt_mw_edge_fifo_t fifo, uint32_t a, uint32_t b, uint32_t c, uint32_t offset) {
    for (int i = 0; i < 16; ++i) {
        uint32_t index = (offset - 1U - (uint32_t)i) & 15U;
        uint32_t e0 = fifo[index][0];
        uint32_t e1 = fifo[index][1];
        if (e0 == a && e1 == b) {
            return (i << 2) | 0;
        }
        if (e0 == b && e1 == c) {
            return (i << 2) | 1;
        }
        if (e0 == c && e1 == a) {
            return (i << 2) | 2;
        }
    }
    return -1;
}

static inline void nt_mw_push_edge_fifo(nt_mw_edge_fifo_t fifo, uint32_t a, uint32_t b, uint32_t *offset) {
    fifo[*offset][0] = a;
    fifo[*offset][1] = b;
    *offset = (*offset + 1U) & 15U;
}

static inline int nt_mw_get_vertex_fifo(const nt_mw_vertex_fifo_t fifo, uint32_t v, uint32_t offset) {
    for (int i = 0; i < 16; ++i) {
        uint32_t index = (offset - 1U - (uint32_t)i) & 15U;
        if (fifo[index] == v) {
            return i;
        }
    }
    return -1;
}

/* cond 0 keeps the slot for overwrite by the next push -- must match between
 * encode and decode or FIFO positions in the stream go stale */
static inline void nt_mw_push_vertex_fifo(nt_mw_vertex_fifo_t fifo, uint32_t v, uint32_t *offset, uint32_t cond) {
    fifo[*offset] = v;
    *offset = (*offset + cond) & 15U;
}

/* 32-bit value in up to 5 7-bit groups, high bit = continuation */
static inline void nt_mw_encode_vbyte(uint8_t **data, uint32_t v) {
    do {
        *(*data)++ = (uint8_t)((v & 127U) | (v > 127U ? 128U : 0U));
        v >>= 7;
    } while (v != 0U);
}

static inline uint32_t nt_mw_decode_vbyte(const uint8_t **data) {
    uint8_t lead = *(*data)++;
    if (lead < 128U) {
        return lead;
    }
    /* up to 4 extra groups; bounded loop keeps malformed data terminating */
    uint32_t result = lead & 127U;
    uint32_t shift = 7;
    for (int i = 0; i < 4; ++i) {
        uint8_t group = *(*data)++;
        result |= (uint32_t)(group & 127U) << shift;
        shift += 7;
        if (group < 128U) {
            break;
        }
    }
    return result;
}

/* Free indices are zigzag deltas against the rolling 'last' index */
static inline void nt_mw_encode_index(uint8_t **data, uint32_t index, uint32_t last) {
    uint32_t d = index - last;
    uint32_t v = (d << 1) ^ (0U - (d >> 31));
    nt_mw_encode_vbyte(data, v);
}

static inline uint32_t nt_mw_decode_index(const uint8_t **data, uint32_t last) {
    uint32_t v = nt_mw_decode_vbyte(data);
    uint32_t d = (v >> 1) ^ (0U - (v & 1U));
    return last + d;
}

#endif /* NT_MESHWIRE_CODEC_INTERNAL_H */
