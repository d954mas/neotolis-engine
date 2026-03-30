/* contour_verify.c — Verify contour data round-trip correctness
 * Converts a glyph, reads back contour data, compares with stb_truetype.
 * Usage: contour_verify <font.ttf>
 */

/* clang-format off */
#include "nt_builder_internal.h"
#include "nt_font_format.h"
#include "stb_truetype.h"
/* clang-format on */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Read contour data back and compare with stb_truetype vertices */
static int verify_glyph(const uint8_t *font_data, const NtFontAssetHeader *hdr,
                         const NtFontGlyphEntry *ge, const stbtt_fontinfo *stb_font) {
    int glyph_idx = stbtt_FindGlyphIndex(stb_font, (int)ge->codepoint);
    if (glyph_idx == 0) {
        return 1; /* skip missing glyphs */
    }

    stbtt_vertex *verts = NULL;
    int nv = stbtt_GetGlyphShape(stb_font, glyph_idx, &verts);

    /* Empty glyph (space) — no contour data */
    if (ge->curve_count == 0) {
        stbtt_FreeShape(stb_font, verts);
        return 0;
    }

    /* Navigate to contour data (past kern entries) */
    const uint8_t *data = (const uint8_t *)hdr + ge->data_offset;
    data += ge->kern_count * sizeof(NtFontKernEntry);

    /* Read contour_count */
    uint16_t contour_count = 0;
    memcpy(&contour_count, data, 2);
    data += 2;

    int vi = 0; /* stb vertex index */
    int errors = 0;
    uint16_t total_segs = 0;

    for (uint16_t ci = 0; ci < contour_count; ci++) {
        /* Read contour header */
        uint16_t seg_count = 0;
        int16_t start_x = 0;
        int16_t start_y = 0;
        memcpy(&seg_count, data, 2); data += 2;
        memcpy(&start_x, data, 2); data += 2;
        memcpy(&start_y, data, 2); data += 2;

        /* Find moveto in stb vertices */
        while (vi < nv && verts[vi].type != STBTT_vmove) {
            vi++;
        }
        if (vi >= nv) {
            printf("    ERROR: expected moveto at vi=%d, nv=%d\n", vi, nv);
            errors++;
            break;
        }

        /* Verify moveto coordinates */
        if (start_x != verts[vi].x || start_y != verts[vi].y) {
            printf("    ERROR: moveto mismatch contour %u: got (%d,%d) expected (%d,%d)\n",
                   ci, start_x, start_y, verts[vi].x, verts[vi].y);
            errors++;
        }
        vi++; /* past moveto */

        /* Read bitmask */
        uint32_t bitmask_bytes = ((uint32_t)seg_count + 7) / 8;
        const uint8_t *bitmask = data;
        data += bitmask_bytes;

        /* Verify each segment (delta-encoded: reconstruct absolute coords) */
        int16_t prev_x = start_x;
        int16_t prev_y = start_y;
        for (uint16_t s = 0; s < seg_count; s++) {
            if (vi >= nv) {
                printf("    ERROR: ran out of stb vertices at seg %u of contour %u\n", s, ci);
                errors++;
                break;
            }

            int is_quad = (bitmask[s / 8] >> (s % 8)) & 1;

            if (is_quad) {
                /* Read delta p1, delta p2, reconstruct absolute */
                int16_t dp1x = 0, dp1y = 0, dp2x = 0, dp2y = 0;
                memcpy(&dp1x, data, 2); data += 2;
                memcpy(&dp1y, data, 2); data += 2;
                memcpy(&dp2x, data, 2); data += 2;
                memcpy(&dp2y, data, 2); data += 2;
                int16_t p1x = (int16_t)(prev_x + dp1x);
                int16_t p1y = (int16_t)(prev_y + dp1y);
                int16_t p2x = (int16_t)(prev_x + dp2x);
                int16_t p2y = (int16_t)(prev_y + dp2y);

                if (verts[vi].type != STBTT_vcurve) {
                    printf("    ERROR: seg %u expected vcurve, got type %d\n", s, verts[vi].type);
                    errors++;
                } else {
                    if (p1x != verts[vi].cx || p1y != verts[vi].cy ||
                        p2x != verts[vi].x || p2y != verts[vi].y) {
                        printf("    ERROR: quad mismatch seg %u: got p1(%d,%d) p2(%d,%d) expected p1(%d,%d) p2(%d,%d)\n",
                               s, p1x, p1y, p2x, p2y, verts[vi].cx, verts[vi].cy, verts[vi].x, verts[vi].y);
                        errors++;
                    }
                }
                prev_x = p2x;
                prev_y = p2y;
            } else {
                /* Read delta p2, reconstruct absolute */
                int16_t dp2x = 0, dp2y = 0;
                memcpy(&dp2x, data, 2); data += 2;
                memcpy(&dp2y, data, 2); data += 2;
                int16_t p2x = (int16_t)(prev_x + dp2x);
                int16_t p2y = (int16_t)(prev_y + dp2y);

                if (verts[vi].type != STBTT_vline) {
                    printf("    ERROR: seg %u expected vline, got type %d\n", s, verts[vi].type);
                    errors++;
                } else {
                    if (p2x != verts[vi].x || p2y != verts[vi].y) {
                        printf("    ERROR: line mismatch seg %u: got (%d,%d) expected (%d,%d)\n",
                               s, p2x, p2y, verts[vi].x, verts[vi].y);
                        errors++;
                    }
                }
                prev_x = p2x;
                prev_y = p2y;
            }
            vi++;
            total_segs++;
        }
    }

    if (total_segs != ge->curve_count) {
        printf("    ERROR: segment count mismatch: header says %u, read %u\n",
               ge->curve_count, total_segs);
        errors++;
    }

    stbtt_FreeShape(stb_font, verts);
    return errors;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        (void)fprintf(stderr, "Usage: contour_verify <font.ttf>\n");
        return 1;
    }

    const char *path = argv[1];
    uint32_t file_size = 0;
    char *file_data = nt_builder_read_file(path, &file_size);
    NT_BUILD_ASSERT(file_data && "cannot read font");

    stbtt_fontinfo stb_font;
    int ok = stbtt_InitFont(&stb_font, (const unsigned char *)file_data,
                            stbtt_GetFontOffsetForIndex((const unsigned char *)file_data, 0));
    NT_BUILD_ASSERT(ok && "stbtt_InitFont failed");

    /* Convert with full ASCII + Cyrillic charset */
    const char *charset =
        " !\"#$%&'()*+,-./0123456789:;<=>?@"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`"
        "abcdefghijklmnopqrstuvwxyz{|}~"
        "\xd0\x90\xd0\x91\xd0\x92\xd0\x93\xd0\x94\xd0\x95\xd0\x96\xd0\x97"  /* А-З */
        "\xd0\x98\xd0\x99\xd0\x9a\xd0\x9b\xd0\x9c\xd0\x9d\xd0\x9e\xd0\x9f"  /* И-П */
        "\xd0\xa0\xd0\xa1\xd0\xa2\xd0\xa3\xd0\xa4\xd0\xa5\xd0\xa6\xd0\xa7"  /* Р-Ч */
        "\xd0\xa8\xd0\xa9\xd0\xaa\xd0\xab\xd0\xac\xd0\xad\xd0\xae\xd0\xaf"  /* Ш-Я */
        "\xd0\xb0\xd0\xb1\xd0\xb2\xd0\xb3\xd0\xb4\xd0\xb5\xd0\xb6\xd0\xb7"  /* а-з */
        "\xd0\xb8\xd0\xb9\xd0\xba\xd0\xbb\xd0\xbc\xd0\xbd\xd0\xbe\xd0\xbf"  /* и-п */
        "\xd1\x80\xd1\x81\xd1\x82\xd1\x83\xd1\x84\xd1\x85\xd1\x86\xd1\x87"  /* р-ч */
        "\xd1\x88\xd1\x89\xd1\x8a\xd1\x8b\xd1\x8c\xd1\x8d\xd1\x8e\xd1\x8f"  /* ш-я */
        "\xd0\x81\xd1\x91"; /* Ё ё */

    uint8_t *nt_data = NULL;
    uint32_t nt_size = 0;
    nt_build_result_t r = nt_builder_decode_font(path, charset, &nt_data, &nt_size);
    NT_BUILD_ASSERT(r == NT_BUILD_OK && "decode_font failed");

    const NtFontAssetHeader *hdr = (const NtFontAssetHeader *)nt_data;
    const NtFontGlyphEntry *glyphs = (const NtFontGlyphEntry *)(nt_data + sizeof(NtFontAssetHeader));

    printf("Verifying %u glyphs from %s...\n", hdr->glyph_count, path);

    int total_errors = 0;
    int verified = 0;
    for (uint16_t i = 0; i < hdr->glyph_count; i++) {
        int errs = verify_glyph(nt_data, hdr, &glyphs[i], &stb_font);
        if (errs > 0) {
            printf("  U+%04X: %d errors\n", glyphs[i].codepoint, errs);
            total_errors += errs;
        }
        verified++;
    }

    if (total_errors == 0) {
        printf("OK: %d glyphs verified, all coordinates match stb_truetype.\n", verified);
    } else {
        printf("FAILED: %d errors in %d glyphs.\n", total_errors, verified);
    }

    free(nt_data);
    free(file_data);
    return total_errors > 0 ? 1 : 0;
}
