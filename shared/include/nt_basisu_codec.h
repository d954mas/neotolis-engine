#ifndef NT_BASISU_CODEC_H
#define NT_BASISU_CODEC_H

#include <stdint.h>

/* The admission set NT_BASISU_CODECS / NT_BASISU_TARGETS arrives as 0/1
 * definitions on nt_shared; an undefined macro would read as "off" silently. */
#if !defined(NT_BASISU_HAS_ETC1S) || !defined(NT_BASISU_HAS_UASTC) || !defined(NT_BASISU_HAS_ETC2) || !defined(NT_BASISU_HAS_BC7) || !defined(NT_BASISU_HAS_ASTC)
#error "NT_BASISU_HAS_* are missing: link nt_shared (the root CMakeLists.txt defines them from NT_BASISU_CODECS / NT_BASISU_TARGETS)"
#endif

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
