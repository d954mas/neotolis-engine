#ifndef NT_BASISU_CODEC_H
#define NT_BASISU_CODEC_H

#include <stdint.h>

typedef enum {
    NT_BASISU_CODEC_NONE = 0, /* Builder RAW path; not a Basis file codec. */
    NT_BASISU_CODEC_ETC1S = 1,
    NT_BASISU_CODEC_UASTC_LDR = 2,
} nt_basisu_codec_t;

/* Only the branch selected by codec is read. Zero RDO disables that stage. */
typedef struct {
    nt_basisu_codec_t codec;
    union {
        struct {
            uint32_t quality;             /* 1..255 */
            float endpoint_rdo_threshold; /* finite, 0..1e10 */
            float selector_rdo_threshold; /* finite, 0..1e10 */
        } etc1s;
        struct {
            uint32_t pack_level; /* 0..4 */
            float rdo_lambda;    /* 0 (off), or finite 0.001..50 */
        } uastc;
    };
} nt_basisu_encode_opts_t;

#endif /* NT_BASISU_CODEC_H */
