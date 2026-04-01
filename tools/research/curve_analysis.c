#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

/* Read raw TTF glyf entry size and point count */
static uint32_t ru16(const uint8_t *p) { return (uint16_t)(p[0]<<8|p[1]); }
static uint32_t ru32(const uint8_t *p) { return (uint32_t)p[0]<<24|p[1]<<16|p[2]<<8|p[3]; }

int main(void) {
    FILE *f = fopen("assets/fonts/LilitaOne-RussianChineseKo.ttf", "rb");
    fseek(f,0,SEEK_END); long sz=ftell(f); fseek(f,0,SEEK_SET);
    uint8_t *d = (uint8_t*)malloc((size_t)sz); fread(d,1,(size_t)sz,f); fclose(f);

    stbtt_fontinfo font;
    stbtt_InitFont(&font, d, 0);

    /* Compare on 1000 CJK glyphs */
    uint64_t ttf_raw_points = 0;      /* total points in TTF glyf */
    uint64_t ttf_raw_coords = 0;      /* total coordinate values in TTF (points * 2) */
    uint64_t stb_vertices = 0;        /* stbtt output vertices (explicit segments) */
    uint64_t stb_coords = 0;          /* our coordinate count (2 per line, 4 per quad) */
    uint64_t consec_offcurve = 0;     /* consecutive off-curve points (implicit midpoints) */
    int glyphs = 0;

    for (uint32_t cp = 0x4E00; cp <= 0x9FFF && glyphs < 1000; cp++) {
        int gi = stbtt_FindGlyphIndex(&font, (int)cp);
        if (gi == 0) continue;

        /* stbtt explicit segments */
        stbtt_vertex *verts;
        int nv = stbtt_GetGlyphShape(&font, gi, &verts);
        stb_vertices += (uint64_t)nv;
        for (int i = 0; i < nv; i++) {
            if (verts[i].type == STBTT_vline) stb_coords += 2;
            else if (verts[i].type == STBTT_vcurve) stb_coords += 4;
        }

        /* Raw TTF point count from glyf table */
        int x0,y0,x1,y1;
        int offset = stbtt_GetGlyphBox(&font, gi, &x0, &y0, &x1, &y1);
        (void)offset;

        /* Use stbtt internals to get raw glyf data */
        int g1, g2;
        /* loca offsets */
        int loc_format = font.indexToLocFormat;
        if (loc_format == 0) {
            g1 = font.glyf + ru16(d + font.loca + gi*2) * 2;
            g2 = font.glyf + ru16(d + font.loca + gi*2 + 2) * 2;
        } else {
            g1 = font.glyf + (int)ru32(d + font.loca + gi*4);
            g2 = font.glyf + (int)ru32(d + font.loca + gi*4 + 4);
        }
        int glyf_size = g2 - g1;
        if (glyf_size > 0) {
            int16_t num_contours = (int16_t)(d[g1]<<8|d[g1+1]);
            if (num_contours > 0) {
                /* Simple glyph: count points from endPtsOfContours */
                uint16_t last_pt = (uint16_t)(d[g1 + 10 + (num_contours-1)*2]<<8 | d[g1 + 10 + (num_contours-1)*2 + 1]);
                uint32_t n_points = last_pt + 1;
                ttf_raw_points += n_points;
                ttf_raw_coords += n_points * 2;

                /* Count consecutive off-curve points */
                uint16_t insn_len = (uint16_t)(d[g1+10+num_contours*2]<<8|d[g1+10+num_contours*2+1]);
                const uint8_t *flags_start = d + g1 + 10 + num_contours*2 + 2 + insn_len;
                const uint8_t *fp = flags_start;
                uint8_t prev_oncurve = 1;
                uint32_t pts_read = 0;
                while (pts_read < n_points) {
                    uint8_t flag = *fp++;
                    uint32_t repeat = 1;
                    if (flag & 0x08) { repeat += *fp++; }
                    for (uint32_t r = 0; r < repeat && pts_read < n_points; r++, pts_read++) {
                        uint8_t oncurve = flag & 0x01;
                        if (!oncurve && !prev_oncurve) consec_offcurve++;
                        prev_oncurve = oncurve;
                    }
                }
            }
        }
        stbtt_FreeShape(&font, verts);
        glyphs++;
    }

    printf("=== 1000 CJK glyphs: TTF points vs our segments ===\n\n");
    printf("TTF raw points:         %llu\n", ttf_raw_points);
    printf("TTF raw coordinates:    %llu (points * 2)\n", ttf_raw_coords);
    printf("stbtt vertices:         %llu (explicit segments)\n", stb_vertices);
    printf("Our coordinates:        %llu (2/line + 4/quad)\n", stb_coords);
    printf("\n");
    printf("Ratio our/TTF coords:   %.2fx\n", (double)stb_coords / ttf_raw_coords);
    printf("Consecutive off-curve:  %llu (implicit midpoints in TTF)\n", consec_offcurve);
    printf("\nTTF stores %llu points. We store %llu coordinates.\n", ttf_raw_points, stb_coords);
    printf("TTF needs ~%.1f bytes/coord (variable). We need ~%.1f bytes/coord (v3 sentinel).\n",
           1.2, 1.19); /* rough estimates from earlier analysis */
    printf("\nEstimated TTF glyf for these glyphs:  ~%.1f MB\n", ttf_raw_coords * 1.2 / 1048576.0);
    printf("Estimated our v3 for these glyphs:    ~%.1f MB\n", stb_coords * 1.19 / 1048576.0);

    free(d);
    return 0;
}
