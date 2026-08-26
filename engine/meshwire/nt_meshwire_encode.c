#include "nt_meshwire_encode.h"

#include <stdbool.h>
#include <string.h>

#include "nt_meshwire_codec_internal.h"

/* Port of meshopt_encodeIndexBuffer, pinned to stream format v1 (no version
 * global -- the format is a pack-internal contract, not a negotiation). */

/* Static table generated upstream from symbol frequency on a training mesh
 * set; doubles as the stream tail. Entries never encode feb/fec=15, and
 * entry 0 is 0 so a reset is expressible only as a non-table codeaux. */
static const uint8_t nt_mw_codeaux_table[NT_MW_TAIL_SIZE] = {
    0x00, 0x76, 0x87, 0x56, 0x67, 0x78, 0xA9, 0x86, 0x65, 0x89, 0x68, 0x98, 0x01, 0x69, 0, 0 /* last two entries aren't used for encoding */
};

/* Rotation so that the vertex equal to 'next' leads, when there is one */
static int nt_mw_rotate_triangle(uint32_t b, uint32_t c, uint32_t next) {
    if (b == next) {
        return 1;
    }
    return (c == next) ? 2 : 0;
}

/* fifo hit -> its code; v == next -> 0 and next advances; else 15 (free index).
 * The next-advance side effect must run in source order across a/b/c. */
static int nt_mw_encode_fe(bool fifo_hit, int hit_code, uint32_t v, uint32_t *next) {
    if (fifo_hit) {
        return hit_code;
    }
    if (v == *next) {
        (*next)++;
        return 0;
    }
    return 15;
}

static int nt_mw_get_codeaux_index(uint8_t v) {
    for (int i = 0; i < 16; ++i) {
        if (nt_mw_codeaux_table[i] == v) {
            return i;
        }
    }
    return -1;
}

uint32_t nt_meshwire_encode_indices_bound(uint32_t index_count, uint32_t vertex_count) {
    if (index_count % 3 != 0) {
        return 0;
    }
    /* number of bits required for each index */
    uint32_t vertex_bits = 1;
    while (vertex_bits < 32 && vertex_count > (uint32_t)((uint64_t)1 << vertex_bits)) {
        vertex_bits++;
    }
    /* worst-case encoding is 2 header bytes + 3 varint-7 encoded index deltas */
    uint32_t vertex_groups = (vertex_bits + 1 + 6) / 7;

    uint64_t bound = 1ULL + ((uint64_t)(index_count / 3) * (2 + (3 * vertex_groups))) + NT_MW_TAIL_SIZE;
    return bound <= UINT32_MAX ? (uint32_t)bound : 0;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity) — mirrors the reference encoder structure 1:1
uint32_t nt_meshwire_encode_indices(uint8_t *dst, uint32_t dst_size, const uint32_t *indices, uint32_t index_count) {
    if (dst == NULL || indices == NULL || index_count % 3 != 0) {
        return 0;
    }
    /* the minimum valid encoding is header, 1 byte per triangle and the 16-byte tail */
    if ((uint64_t)dst_size < 1ULL + (index_count / 3) + NT_MW_TAIL_SIZE) {
        return 0;
    }

    dst[0] = NT_MW_INDEX_HEADER | NT_MW_INDEX_VERSION;

    nt_mw_edge_fifo_t edgefifo;
    memset(edgefifo, -1, sizeof(edgefifo));
    nt_mw_vertex_fifo_t vertexfifo;
    memset(vertexfifo, -1, sizeof(vertexfifo));

    uint32_t edgefifooffset = 0;
    uint32_t vertexfifooffset = 0;

    uint32_t next = 0;
    uint32_t last = 0;

    uint8_t *code = dst + 1;
    uint8_t *data = code + (index_count / 3);
    uint8_t *data_safe_end = dst + dst_size - NT_MW_TAIL_SIZE;

    static const int rotations[5] = {0, 1, 2, 0, 1};

    for (uint32_t i = 0; i < index_count; i += 3) {
        /* each triangle writes at most 16 bytes: 1 codeaux + 5 per free index;
         * staying <= data_safe_end before writing keeps the writes in bounds */
        if (data > data_safe_end) {
            return 0;
        }

        int fer = nt_mw_get_edge_fifo(edgefifo, indices[i + 0], indices[i + 1], indices[i + 2], edgefifooffset);

        if (fer >= 0 && (fer >> 2) < 15) {
            /* getEdgeFifo implicitly rotates the triangle by matching a/b to an existing edge */
            const int *order = rotations + (fer & 3);

            uint32_t a = indices[i + (uint32_t)order[0]];
            uint32_t b = indices[i + (uint32_t)order[1]];
            uint32_t c = indices[i + (uint32_t)order[2]];

            int fe = fer >> 2;
            int fc = nt_mw_get_vertex_fifo(vertexfifo, c, vertexfifooffset);

            int fec = nt_mw_encode_fe(fc >= 1 && fc < NT_MW_FECMAX, fc, c, &next);

            if (fec == 15) {
                /* encode last-1 and last+1 to optimize strip-like sequences */
                if (c + 1 == last) {
                    fec = 13;
                    last = c;
                }
                if (c == last + 1) {
                    fec = 14;
                    last = c;
                }
            }

            *code++ = (uint8_t)((fe << 4) | fec);

            /* free indices are delta-encoded, so 'last' must track them */
            if (fec == 15) {
                nt_mw_encode_index(&data, c, last);
                last = c;
            }

            /* only the third vertex needs pushing: the first two are already likely in the fifo */
            if (fec == 0 || fec >= NT_MW_FECMAX) {
                nt_mw_push_vertex_fifo(vertexfifo, c, &vertexfifooffset, 1);
            }

            /* only two new edges: the third is already in the fifo */
            nt_mw_push_edge_fifo(edgefifo, c, b, &edgefifooffset);
            nt_mw_push_edge_fifo(edgefifo, a, c, &edgefifooffset);
        } else {
            int rotation = nt_mw_rotate_triangle(indices[i + 1], indices[i + 2], next);
            const int *order = rotations + rotation;

            uint32_t a = indices[i + (uint32_t)order[0]];
            uint32_t b = indices[i + (uint32_t)order[1]];
            uint32_t c = indices[i + (uint32_t)order[2]];

            /* if a/b/c are 0/1/2, emit a reset code */
            bool reset = false;
            if (a == 0 && b == 1 && c == 2 && next > 0) {
                reset = true;
                next = 0;
                /* clear the vertex fifo so later triangles can't reference pre-reset
                 * vertices, which would stall 'next' */
                memset(vertexfifo, -1, sizeof(vertexfifo));
            }

            int fb = nt_mw_get_vertex_fifo(vertexfifo, b, vertexfifooffset);
            int fc = nt_mw_get_vertex_fifo(vertexfifo, c, vertexfifooffset);

            /* after rotation a is almost always 'next', so no fifo bits are spent on a;
             * decoder assumes feb=fec=0 implies fea=0 (reset code) -- enforced by rotation */
            int fea = nt_mw_encode_fe(false, 0, a, &next);
            int feb = nt_mw_encode_fe(fb >= 0 && fb < 14, fb + 1, b, &next);
            int fec = nt_mw_encode_fe(fc >= 0 && fc < 14, fc + 1, c, &next);

            /* feb & fec fit 4 bits via the table when possible, else a full byte */
            uint8_t codeaux = (uint8_t)((feb << 4) | fec);
            int codeauxindex = nt_mw_get_codeaux_index(codeaux);

            /* <14 encodes a codeaux table index, 14 encodes fea=0, 15 encodes fea=15 */
            if (fea == 0 && codeauxindex >= 0 && codeauxindex < 14 && !reset) {
                *code++ = (uint8_t)((15 << 4) | codeauxindex);
            } else {
                *code++ = (uint8_t)((15 << 4) | 14 | fea);
                *data++ = codeaux;
            }

            /* free indices are delta-encoded, so 'last' must track them */
            if (fea == 15) {
                nt_mw_encode_index(&data, a, last);
                last = a;
            }
            if (feb == 15) {
                nt_mw_encode_index(&data, b, last);
                last = b;
            }
            if (fec == 15) {
                nt_mw_encode_index(&data, c, last);
                last = c;
            }

            /* only push vertices that weren't already in the fifo */
            if (fea == 0 || fea == 15) {
                nt_mw_push_vertex_fifo(vertexfifo, a, &vertexfifooffset, 1);
            }
            if (feb == 0 || feb == 15) {
                nt_mw_push_vertex_fifo(vertexfifo, b, &vertexfifooffset, 1);
            }
            if (fec == 0 || fec == 15) {
                nt_mw_push_vertex_fifo(vertexfifo, c, &vertexfifooffset, 1);
            }

            /* none of the three edges were in the fifo; push all so later triangles match */
            nt_mw_push_edge_fifo(edgefifo, b, a, &edgefifooffset);
            nt_mw_push_edge_fifo(edgefifo, c, b, &edgefifooffset);
            nt_mw_push_edge_fifo(edgefifo, a, c, &edgefifooffset);
        }
    }

    /* room for the codeaux table tail */
    if (data > data_safe_end) {
        return 0;
    }

    /* the table is the stream tail AND the decoder's 16-byte read-ahead padding */
    memcpy(data, nt_mw_codeaux_table, NT_MW_TAIL_SIZE);
    data += NT_MW_TAIL_SIZE;

    return (uint32_t)(data - dst);
}
