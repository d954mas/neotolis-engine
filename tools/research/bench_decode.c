#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

typedef struct { float p0x, p0y, p1x, p1y, p2x, p2y; } nt_curve_t;

#define NT_FONT_DELTA_SENTINEL 0x80
#define NT_FONT_BITMASK_BYTES(n) ((((uint32_t)(n) + 15U) / 8U) & ~1U)

static inline int16_t read_varlen_delta(const uint8_t **rp) {
    uint8_t b = **rp; (*rp)++;
    if (b != NT_FONT_DELTA_SENTINEL) { return (int16_t)(int8_t)b; }
    int16_t val; memcpy(&val, *rp, 2); (*rp) += 2;
    return val;
}

static uint16_t decode_contours_v3(const uint8_t *data, nt_curve_t *curves, uint16_t max_c) {
    const uint8_t *rp = data;
    uint16_t cc; memcpy(&cc, rp, 2); rp += 2;
    uint16_t total = 0;
    for (uint16_t ci = 0; ci < cc; ci++) {
        uint16_t sc; memcpy(&sc, rp, 2); rp += 2;
        int16_t sx, sy; memcpy(&sx, rp, 2); rp += 2; memcpy(&sy, rp, 2); rp += 2;
        uint32_t bm_b = NT_FONT_BITMASK_BYTES(sc);
        const uint8_t *bm = rp; rp += bm_b;
        int32_t px = sx, py = sy;
        for (uint16_t s = 0; s < sc; s++) {
            int is_q = (bm[s/8] >> (s%8)) & 1;
            float f0x = (float)px, f0y = (float)py;
            if (is_q) {
                int16_t d1x=read_varlen_delta(&rp), d1y=read_varlen_delta(&rp);
                int16_t d2x=read_varlen_delta(&rp), d2y=read_varlen_delta(&rp);
                if (total<max_c) curves[total++]=(nt_curve_t){f0x,f0y,(float)(px+d1x),(float)(py+d1y),(float)(px+d2x),(float)(py+d2y)};
                px+=d2x; py+=d2y;
            } else {
                int16_t d2x=read_varlen_delta(&rp), d2y=read_varlen_delta(&rp);
                float f2x=(float)(px+d2x), f2y=(float)(py+d2y);
                if (total<max_c) curves[total++]=(nt_curve_t){f0x,f0y,(f0x+f2x)*0.5f,(f0y+f2y)*0.5f,f2x,f2y};
                px+=d2x; py+=d2y;
            }
        }
    }
    return total;
}

static inline void write_vl(uint8_t **wp, int d) {
    if (d>=-127 && d<=127) { **wp=(uint8_t)(int8_t)d; (*wp)++; }
    else { **wp=0x80; (*wp)++; int16_t v=(int16_t)d; memcpy(*wp,&v,2); (*wp)+=2; }
}

static uint32_t encode_glyph_v3(const stbtt_vertex *v, int nv, uint8_t *buf) {
    uint8_t *wp = buf;
    uint16_t cc = 0;
    for (int i = 0; i < nv; i++) {
        if (v[i].type == STBTT_vmove) {
            int has = 0;
            for (int j=i+1; j<nv && v[j].type!=STBTT_vmove; j++)
                if (v[j].type==STBTT_vline||v[j].type==STBTT_vcurve) { has=1; break; }
            if (has) cc++;
        }
    }
    memcpy(wp, &cc, 2); wp += 2;
    int vi = 0;
    for (uint16_t ci = 0; ci < cc; ci++) {
        uint16_t sc = 0;
        while (vi < nv) {
            if (v[vi].type != STBTT_vmove) { vi++; continue; }
            for (int p=vi+1; p<nv && v[p].type!=STBTT_vmove; p++)
                if (v[p].type==STBTT_vline||v[p].type==STBTT_vcurve) sc++;
            if (sc>0) break; vi++;
        }
        memcpy(wp,&sc,2); wp+=2;
        int16_t sx=v[vi].x, sy=v[vi].y;
        memcpy(wp,&sx,2); wp+=2; memcpy(wp,&sy,2); wp+=2; vi++;
        uint32_t bm_b=NT_FONT_BITMASK_BYTES(sc);
        uint8_t *bm=wp; memset(bm,0,bm_b); wp+=bm_b;
        int px=sx, py=sy;
        for (uint16_t s=0; s<sc; s++) {
            if (v[vi].type==STBTT_vcurve) {
                bm[s/8]|=(uint8_t)(1U<<(s%8));
                write_vl(&wp,v[vi].cx-px); write_vl(&wp,v[vi].cy-py);
                write_vl(&wp,v[vi].x-px); write_vl(&wp,v[vi].y-py);
            } else {
                write_vl(&wp,v[vi].x-px); write_vl(&wp,v[vi].y-py);
            }
            px=v[vi].x; py=v[vi].y; vi++;
        }
    }
    return (uint32_t)(wp - buf);
}

int main(void) {
    FILE *f = fopen("assets/fonts/LilitaOne-RussianChineseKo.ttf", "rb");
    if (!f) { printf("No font\n"); return 1; }
    fseek(f,0,SEEK_END); long sz=ftell(f); fseek(f,0,SEEK_SET);
    uint8_t *fdata = (uint8_t*)malloc((size_t)sz);
    fread(fdata,1,(size_t)sz,f); fclose(f);
    stbtt_fontinfo font;
    stbtt_InitFont(&font, fdata, 0);

    #define NGLYPH 1000
    int glyph_ids[NGLYPH];
    stbtt_vertex *vert_cache[NGLYPH];
    int vert_counts[NGLYPH];
    uint8_t *v3_data[NGLYPH];

    int found = 0;
    for (uint32_t cp = 0x4E00; cp <= 0x9FFF && found < NGLYPH; cp++) {
        int gi = stbtt_FindGlyphIndex(&font, (int)cp);
        if (gi == 0) continue;
        glyph_ids[found] = gi;
        vert_counts[found] = stbtt_GetGlyphShape(&font, gi, &vert_cache[found]);
        v3_data[found] = (uint8_t*)malloc(65536);
        encode_glyph_v3(vert_cache[found], vert_counts[found], v3_data[found]);
        found++;
    }
    printf("Benchmarking %d CJK glyphs\n\n", found);

    nt_curve_t curves[4096];
    int ITERS = 1000;

    /* stbtt_GetGlyphShape (TTF parse) */
    {
        clock_t t0 = clock();
        volatile int dummy = 0;
        for (int iter = 0; iter < ITERS; iter++) {
            for (int g = 0; g < found; g++) {
                stbtt_vertex *verts;
                int nv = stbtt_GetGlyphShape(&font, glyph_ids[g], &verts);
                dummy += nv;
                stbtt_FreeShape(&font, verts);
            }
        }
        double ms = (double)(clock()-t0) / CLOCKS_PER_SEC * 1000.0;
        printf("stbtt_GetGlyphShape:  %7.1f ms  = %.0f ns/glyph (malloc+free each)\n", ms, ms*1e6/(found*ITERS));
    }

    /* Our v3 decode */
    {
        clock_t t0 = clock();
        volatile int dummy = 0;
        for (int iter = 0; iter < ITERS; iter++) {
            for (int g = 0; g < found; g++) {
                uint16_t nc = decode_contours_v3(v3_data[g], curves, 4096);
                dummy += nc;
            }
        }
        double ms = (double)(clock()-t0) / CLOCKS_PER_SEC * 1000.0;
        printf("decode_contours_v3:   %7.1f ms  = %.0f ns/glyph (no malloc)\n", ms, ms*1e6/(found*ITERS));
    }

    /* stbtt + float expand (full "TTF in pack" path) */
    {
        clock_t t0 = clock();
        volatile int dummy = 0;
        for (int iter = 0; iter < ITERS; iter++) {
            for (int g = 0; g < found; g++) {
                stbtt_vertex *verts;
                int nv = stbtt_GetGlyphShape(&font, glyph_ids[g], &verts);
                int nc = 0; int16_t px=0, py=0;
                for (int i = 0; i < nv; i++) {
                    if (verts[i].type == STBTT_vmove) { px=verts[i].x; py=verts[i].y; continue; }
                    if (verts[i].type == STBTT_vcurve && nc < 4096)
                        curves[nc++]=(nt_curve_t){(float)px,(float)py,(float)verts[i].cx,(float)verts[i].cy,(float)verts[i].x,(float)verts[i].y};
                    else if (verts[i].type == STBTT_vline && nc < 4096) {
                        float f2x=(float)verts[i].x, f2y=(float)verts[i].y;
                        curves[nc++]=(nt_curve_t){(float)px,(float)py,((float)px+f2x)*0.5f,((float)py+f2y)*0.5f,f2x,f2y};
                    }
                    px=verts[i].x; py=verts[i].y;
                }
                dummy += nc;
                stbtt_FreeShape(&font, verts);
            }
        }
        double ms = (double)(clock()-t0) / CLOCKS_PER_SEC * 1000.0;
        printf("stbtt + expand:       %7.1f ms  = %.0f ns/glyph (full TTF path)\n", ms, ms*1e6/(found*ITERS));
    }

    for (int g=0; g<found; g++) { stbtt_FreeShape(&font, vert_cache[g]); free(v3_data[g]); }
    free(fdata);
    return 0;
}
