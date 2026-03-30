/* gpos_diag.c — Diagnose why GPOS kerning returns 0
 * Usage: gpos_diag <font.ttf>
 */

/* clang-format off */
#include "nt_builder_internal.h"
#include "stb_truetype.h"
/* clang-format on */

#include <stdio.h>

/* Minimal big-endian readers (same as stb_truetype internals) */
static uint16_t rd16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }
static uint32_t rd32(const uint8_t *p) { return (uint32_t)((p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]); }

/* Count set bits in ValueFormat to get ValueRecord size in bytes */
static uint32_t value_record_size(uint16_t fmt) {
    uint32_t n = 0;
    for (uint16_t f = fmt; f != 0; f >>= 1) {
        n += f & 1;
    }
    return n * 2; /* each field is int16 or offset16 */
}

static void tag_str(const uint8_t *p, char out[5]) {
    out[0] = (char)p[0];
    out[1] = (char)p[1];
    out[2] = (char)p[2];
    out[3] = (char)p[3];
    out[4] = '\0';
}

int main(int argc, char **argv) {
    if (argc < 2) {
        (void)fprintf(stderr, "Usage: gpos_diag <font.ttf>\n");
        return 1;
    }

    uint32_t file_size = 0;
    char *file_data = nt_builder_read_file(argv[1], &file_size);
    if (!file_data) {
        (void)fprintf(stderr, "Cannot read %s\n", argv[1]);
        return 1;
    }

    stbtt_fontinfo font;
    if (!stbtt_InitFont(&font, (const unsigned char *)file_data,
                        stbtt_GetFontOffsetForIndex((const unsigned char *)file_data, 0))) {
        (void)fprintf(stderr, "stbtt_InitFont failed\n");
        free(file_data);
        return 1;
    }

    printf("Font: %s (%u bytes)\n", argv[1], file_size);
    printf("kern table offset: %d\n", font.kern);
    printf("gpos table offset: %d\n\n", font.gpos);

    if (!font.gpos) {
        printf("No GPOS table.\n");
        free(file_data);
        return 0;
    }

    const uint8_t *gpos = (const uint8_t *)file_data + font.gpos;

    // #region GPOS header
    uint16_t major = rd16(gpos);
    uint16_t minor = rd16(gpos + 2);
    printf("GPOS version: %u.%u", major, minor);
    if (major == 1 && minor != 0) {
        printf("  *** stb_truetype rejects minor != 0! ***");
    }
    printf("\n");

    uint16_t script_list_off = rd16(gpos + 4);
    uint16_t feature_list_off = rd16(gpos + 6);
    uint16_t lookup_list_off = rd16(gpos + 8);
    (void)script_list_off;
    // #endregion

    // #region Feature list — find kern features
    const uint8_t *feat_list = gpos + feature_list_off;
    uint16_t feat_count = rd16(feat_list);
    printf("\nFeature list: %u features\n", feat_count);

    for (uint16_t i = 0; i < feat_count; i++) {
        const uint8_t *rec = feat_list + 2 + i * 6;
        char tag[5];
        tag_str(rec, tag);
        uint16_t feat_off = rd16(rec + 4);
        const uint8_t *feat_table = feat_list + feat_off;
        uint16_t lookup_index_count = rd16(feat_table + 2);

        if (tag[0] == 'k' && tag[1] == 'e' && tag[2] == 'r' && tag[3] == 'n') {
            printf("  [%u] '%s' -> %u lookup indices: ", i, tag, lookup_index_count);
            for (uint16_t j = 0; j < lookup_index_count; j++) {
                printf("%u ", rd16(feat_table + 4 + j * 2));
            }
            printf("  <-- KERN FEATURE\n");
        }
    }
    // #endregion

    // #region Lookup list — dump all lookups
    const uint8_t *lookup_list = gpos + lookup_list_off;
    uint16_t lookup_count = rd16(lookup_list);
    printf("\nLookup list: %u lookups\n", lookup_count);

    for (uint16_t i = 0; i < lookup_count; i++) {
        uint16_t lookup_off = rd16(lookup_list + 2 + i * 2);
        const uint8_t *lookup = lookup_list + lookup_off;
        uint16_t lookup_type = rd16(lookup);
        uint16_t lookup_flag = rd16(lookup + 2);
        uint16_t sub_count = rd16(lookup + 4);

        const char *type_name = "?";
        switch (lookup_type) {
        case 1: type_name = "SinglePos"; break;
        case 2: type_name = "PairPos"; break;
        case 3: type_name = "CursivePos"; break;
        case 4: type_name = "MarkBasePos"; break;
        case 5: type_name = "MarkLigPos"; break;
        case 6: type_name = "MarkMarkPos"; break;
        case 7: type_name = "ContextPos"; break;
        case 8: type_name = "ChainContextPos"; break;
        case 9: type_name = "ExtensionPos"; break;
        default: break;
        }

        printf("  [%u] type=%u (%s) flag=0x%04X subtables=%u", i, lookup_type, type_name, lookup_flag, sub_count);

        /* For PairPos or Extension, show subtable details */
        if (lookup_type == 2 || lookup_type == 9) {
            for (uint16_t s = 0; s < sub_count && s < 3; s++) {
                uint16_t sub_off = rd16(lookup + 6 + s * 2);
                const uint8_t *sub = lookup + sub_off;

                if (lookup_type == 9) {
                    /* Extension: extensionLookupType + extensionOffset */
                    uint16_t ext_format = rd16(sub);
                    uint16_t ext_type = rd16(sub + 2);
                    uint32_t ext_off = rd32(sub + 4);
                    printf("\n    ext[%u]: format=%u wraps type=%u offset=%u", s, ext_format, ext_type, ext_off);

                    if (ext_type == 2) {
                        const uint8_t *real_sub = sub + ext_off;
                        uint16_t pos_format = rd16(real_sub);
                        uint16_t vf1 = rd16(real_sub + 4);
                        uint16_t vf2 = rd16(real_sub + 6);
                        printf(" -> PairPos fmt=%u vf1=0x%04X vf2=0x%04X (vr1=%uB vr2=%uB)",
                               pos_format, vf1, vf2,
                               value_record_size(vf1), value_record_size(vf2));
                        if (vf1 == 4 && vf2 == 0) {
                            printf(" [stb OK]");
                        } else {
                            printf(" [stb REJECTS]");
                        }
                    }
                } else {
                    uint16_t pos_format = rd16(sub);
                    uint16_t vf1 = rd16(sub + 4);
                    uint16_t vf2 = rd16(sub + 6);
                    printf("\n    sub[%u]: PairPos fmt=%u vf1=0x%04X vf2=0x%04X (vr1=%uB vr2=%uB)",
                           s, pos_format, vf1, vf2,
                           value_record_size(vf1), value_record_size(vf2));
                    if (vf1 == 4 && vf2 == 0) {
                        printf(" [stb OK]");
                    } else {
                        printf(" [stb REJECTS]");
                    }
                }
            }
        }
        printf("\n");
    }
    // #endregion

    // #region Quick kern test: try common pairs
    printf("\nKern test (stbtt_GetGlyphKernAdvance):\n");
    const char *pairs[][2] = {{"A","V"}, {"T","o"}, {"A","W"}, {"W","a"}, {"V","a"}, {"L","T"}};
    for (int i = 0; i < 6; i++) {
        int g1 = stbtt_FindGlyphIndex(&font, pairs[i][0][0]);
        int g2 = stbtt_FindGlyphIndex(&font, pairs[i][1][0]);
        int kern = stbtt_GetGlyphKernAdvance(&font, g1, g2);
        printf("  %s%s: glyph(%d,%d) kern=%d\n", pairs[i][0], pairs[i][1], g1, g2, kern);
    }
    // #endregion

    free(file_data);
    return 0;
}
