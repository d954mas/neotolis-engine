/* bench_embolden — Wave-0 spike for Phase 73 CPU embolden (D-01, RESEARCH Pattern 1).
 * Builds the decoded point ring (pts_x/pts_y/pts_on) exactly as nt_font.c:604-633 does,
 * ports offset_points (FT_Outline_EmboldenXY-style) BEFORE the point->quadratic conversion,
 * and sweeps a W ramp over A W e o 8 , @ to find:
 *   - the max clean W (em-fraction cap),
 *   - the miter clamp d_max,
 *   - the outward-normal sign (A3),
 * plus a cross-check of the post/OS-2 table byte offsets (A1). Research-only, standalone.
 * The point ring is materialized straight from stbtt vertices (same source the real builder
 * uses); the pack encode/decode roundtrip is intentionally skipped — it adds no signal to
 * the corner-quality measurement and the seam offset_points transplants into is the ring. */
#define _CRT_SECURE_NO_WARNINGS
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

typedef struct {
    float p0x, p0y, p1x, p1y, p2x, p2y;
} nt_curve_t;

#define MAX_POINTS 4096
#define MAX_CONTOURS 64
#define MAX_CURVES 4096

// #region Point ring (mirrors nt_font.c pts_x/pts_y/pts_on layout)
typedef struct {
    int32_t x[MAX_POINTS];
    int32_t y[MAX_POINTS];
    uint8_t on[MAX_POINTS];
    uint16_t n;
} contour_t;

/* Materialize per-contour point rings from stbtt vertices — the on/off ring nt_font.c
 * holds in pts_x/pts_y/pts_on. vcurve emits control(off)+end(on); vline emits end(on).
 * stbtt already inserts implicit on-curve midpoints, so no off->off adjacency appears. */
static int stbtt_to_contours(const stbtt_vertex *v, int nv, contour_t *cs, int maxc) {
    int nc = -1;
    for (int i = 0; i < nv; i++) {
        if (v[i].type == STBTT_vmove) {
            if (nc + 1 >= maxc)
                break;
            nc++;
            cs[nc].n = 0;
            cs[nc].x[0] = v[i].x;
            cs[nc].y[0] = v[i].y;
            cs[nc].on[0] = 1;
            cs[nc].n = 1;
        } else if (nc >= 0 && v[i].type == STBTT_vline) {
            contour_t *c = &cs[nc];
            if (c->n >= MAX_POINTS)
                continue;
            c->x[c->n] = v[i].x;
            c->y[c->n] = v[i].y;
            c->on[c->n] = 1;
            c->n++;
        } else if (nc >= 0 && v[i].type == STBTT_vcurve) {
            contour_t *c = &cs[nc];
            if (c->n + 2 > MAX_POINTS)
                continue;
            c->x[c->n] = v[i].cx;
            c->y[c->n] = v[i].cy;
            c->on[c->n] = 0;
            c->n++;
            c->x[c->n] = v[i].x;
            c->y[c->n] = v[i].y;
            c->on[c->n] = 1;
            c->n++;
        }
    }
    /* drop trailing on-curve point(s) coinciding with the contour start (closing vertex) */
    for (int c = 0; c <= nc; c++) {
        contour_t *cc = &cs[c];
        while (cc->n > 1 && cc->on[cc->n - 1] && cc->x[cc->n - 1] == cc->x[0] && cc->y[cc->n - 1] == cc->y[0])
            cc->n--;
    }
    return nc + 1;
}

/* FT_Outline_EmboldenXY-style point-ring offset (RESEARCH Pattern 1).
 * A3 resolved in-spike: with stbtt/TrueType winding, +1 pushed the outer silhouette
 * INWARD (counters grew) — -1 is the correct outward embolden. */
static int g_normal_sign = -1;
static void offset_points(int32_t *x, int32_t *y, const uint8_t *on, uint16_t n, float W) {
    (void)on;
    if (W == 0.0F || n < 2)
        return;
    /* snapshot originals: adjacency must read pre-offset neighbors */
    static int32_t ox[MAX_POINTS];
    static int32_t oy[MAX_POINTS];
    for (uint16_t i = 0; i < n; i++) {
        ox[i] = x[i];
        oy[i] = y[i];
    }
    float s = (float)g_normal_sign;
    for (uint16_t i = 0; i < n; i++) {
        uint16_t p = (i == 0) ? (uint16_t)(n - 1) : (uint16_t)(i - 1);
        uint16_t q = (uint16_t)((i + 1) % n);
        float inx = (float)(ox[i] - ox[p]), iny = (float)(oy[i] - oy[p]);
        float oux = (float)(ox[q] - ox[i]), ouy = (float)(oy[q] - oy[i]);
        float li = sqrtf(inx * inx + iny * iny) + 1e-6F;
        float lo = sqrtf(oux * oux + ouy * ouy) + 1e-6F;
        /* normals: rotate tangent -90 => n=(ty,-tx), normalized */
        float nix = iny / li, niy = -inx / li;
        float nox = ouy / lo, noy = -oux / lo;
        float sx = nix + nox, sy = niy + noy;
        float d = 1.0F / fmaxf(1.0F + (nix * nox + niy * noy), 0.25F); /* miter, clamped */
        d = fminf(d, 2.0F);                                            /* hard cap: no spikes */
        x[i] += (int32_t)lrintf(s * sx * d * W * 0.5F);
        y[i] += (int32_t)lrintf(s * sy * d * W * 0.5F);
    }
}
// #endregion

// #region Convert point ring -> quadratic curves (mirrors nt_font.c:636-708)
static void emit_curve(nt_curve_t *curves, uint16_t *total, uint16_t max_c, float p0x, float p0y, float p1x, float p1y, float p2x, float p2y) {
    if (*total < max_c)
        curves[(*total)++] = (nt_curve_t){p0x, p0y, p1x, p1y, p2x, p2y};
}

static void convert_contour(const contour_t *c, nt_curve_t *curves, uint16_t *total, uint16_t max_c) {
    uint16_t n = c->n;
    if (n == 0)
        return;
    const int32_t *px = c->x;
    const int32_t *py = c->y;
    const uint8_t *pon = c->on;
    uint16_t start = 0;
    while (start < n && !pon[start])
        start++;
    if (start == n)
        start = 0;
    float cur_x, cur_y;
    uint16_t i;
    if (pon[start]) {
        cur_x = (float)px[start];
        cur_y = (float)py[start];
        i = (uint16_t)((start + 1) % n);
    } else {
        cur_x = (float)(px[0] + px[1]) * 0.5F;
        cur_y = (float)(py[0] + py[1]) * 0.5F;
        i = 0;
    }
    uint16_t steps = 0;
    while (steps < n) {
        uint16_t idx = (uint16_t)(i % n);
        if (pon[idx]) {
            float ex = (float)px[idx], ey = (float)py[idx];
            emit_curve(curves, total, max_c, cur_x, cur_y, (cur_x + ex) * 0.5F, (cur_y + ey) * 0.5F, ex, ey);
            cur_x = ex;
            cur_y = ey;
            i = (uint16_t)((idx + 1) % n);
            steps++;
        } else {
            float cx = (float)px[idx], cy = (float)py[idx];
            uint16_t next = (uint16_t)((idx + 1) % n);
            if (pon[next]) {
                float ex = (float)px[next], ey = (float)py[next];
                emit_curve(curves, total, max_c, cur_x, cur_y, cx, cy, ex, ey);
                cur_x = ex;
                cur_y = ey;
                i = (uint16_t)((next + 1) % n);
                steps += 2;
            } else {
                float mx = (cx + (float)px[next]) * 0.5F;
                float my = (cy + (float)py[next]) * 0.5F;
                emit_curve(curves, total, max_c, cur_x, cur_y, cx, cy, mx, my);
                cur_x = mx;
                cur_y = my;
                i = next;
                steps++;
            }
        }
    }
}
// #endregion

// #region Big-endian sfnt table reader (RESEARCH Pattern 4 — TTF is big-endian)
static uint16_t rd_u16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }
static int16_t rd_s16(const uint8_t *p) { return (int16_t)rd_u16(p); }
static uint32_t rd_u32(const uint8_t *p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }
static uint32_t find_table(const uint8_t *data, uint32_t fontstart, const char tag[4]) {
    uint16_t num = rd_u16(data + fontstart + 4);
    uint32_t dir = fontstart + 12;
    for (uint16_t i = 0; i < num; i++) {
        const uint8_t *rec = data + dir + 16u * i;
        if (rec[0] == (uint8_t)tag[0] && rec[1] == (uint8_t)tag[1] && rec[2] == (uint8_t)tag[2] && rec[3] == (uint8_t)tag[3])
            return rd_u32(rec + 8);
    }
    return 0;
}
// #endregion

typedef struct {
    float minx, miny, maxx, maxy;
    int finite;
} bbox_t;

static bbox_t curves_bbox(const nt_curve_t *cv, uint16_t nc) {
    bbox_t b = {1e30F, 1e30F, -1e30F, -1e30F, 1};
    for (uint16_t i = 0; i < nc; i++) {
        const float xs[3] = {cv[i].p0x, cv[i].p1x, cv[i].p2x};
        const float ys[3] = {cv[i].p0y, cv[i].p1y, cv[i].p2y};
        for (int k = 0; k < 3; k++) {
            if (!isfinite(xs[k]) || !isfinite(ys[k]))
                b.finite = 0;
            if (xs[k] < b.minx)
                b.minx = xs[k];
            if (xs[k] > b.maxx)
                b.maxx = xs[k];
            if (ys[k] < b.miny)
                b.miny = ys[k];
            if (ys[k] > b.maxy)
                b.maxy = ys[k];
        }
    }
    return b;
}

/* bbox of a single contour's raw point ring (for counter shrink test) */
static bbox_t contour_point_bbox(const contour_t *c) {
    bbox_t b = {1e30F, 1e30F, -1e30F, -1e30F, 1};
    for (uint16_t i = 0; i < c->n; i++) {
        float x = (float)c->x[i], y = (float)c->y[i];
        if (x < b.minx)
            b.minx = x;
        if (x > b.maxx)
            b.maxx = x;
        if (y < b.miny)
            b.miny = y;
        if (y > b.maxy)
            b.maxy = y;
    }
    return b;
}

/* Offset + convert a glyph's base contours at weight W_units; returns curve count + bbox.
 * Copies the base ring first so offset_points never accumulates across calls. */
static uint16_t emboldened(const contour_t *base, int nbc, float W, nt_curve_t *out, uint16_t max_c, bbox_t *bb) {
    static contour_t work[MAX_CONTOURS];
    uint16_t total = 0;
    for (int i = 0; i < nbc; i++) {
        work[i] = base[i];
        offset_points(work[i].x, work[i].y, work[i].on, work[i].n, W);
        convert_contour(&work[i], out, &total, max_c);
    }
    *bb = curves_bbox(out, total);
    return total;
}

int main(void) {
    const char *font_path = "assets/fonts/LilitaOne-RussianChineseKo.ttf";
    FILE *f = fopen(font_path, "rb");
    if (!f) {
        printf("No font: %s\n", font_path);
        return 1;
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *fdata = (uint8_t *)malloc((size_t)sz);
    if (fread(fdata, 1, (size_t)sz, f) != (size_t)sz) {
        printf("read fail\n");
        return 1;
    }
    fclose(f);

    stbtt_fontinfo font;
    stbtt_InitFont(&font, fdata, 0);
    uint32_t fontstart = (uint32_t)font.fontstart;

    /* units_per_em from head table @18 */
    uint32_t head = find_table(fdata, fontstart, "head");
    uint16_t upem = head ? rd_u16(fdata + head + 18) : 1000;
    printf("Font: %s   units_per_em=%u\n\n", font_path, upem);

    const int GN = 7;
    const int codepoints[7] = {'A', 'W', 'e', 'o', '8', ',', '@'};
    const char *names[7] = {"A", "W", "e", "o", "8", ",", "@"};

    /* materialize each glyph's base point ring (contour_t) from stbtt vertices */
    static contour_t gcs[7][MAX_CONTOURS];
    int gncs[7];
    for (int g = 0; g < GN; g++) {
        int gi = stbtt_FindGlyphIndex(&font, codepoints[g]);
        stbtt_vertex *verts;
        int nv = stbtt_GetGlyphShape(&font, gi, &verts);
        gncs[g] = stbtt_to_contours(verts, nv, gcs[g], MAX_CONTOURS);
        stbtt_FreeShape(&font, verts);
    }

    const float ramp[] = {0.0F, 0.01F, 0.02F, 0.03F, 0.05F, 0.08F, 0.12F};
    const int RN = (int)(sizeof(ramp) / sizeof(ramp[0]));

    printf("=== W-ramp: cost + bbox (sign=%+d, miter d_max=2.0) ===\n", g_normal_sign);
    printf("glyph  W(em)  W(u)   curves   ns/glyph   finite   bbox[minx,miny,maxx,maxy]   flag\n");
    static nt_curve_t curves[MAX_CURVES];
    static nt_curve_t base_curves[MAX_CURVES];
    for (int g = 0; g < GN; g++) {
        bbox_t base_bb;
        emboldened(gcs[g], gncs[g], 0.0F, base_curves, MAX_CURVES, &base_bb);
        float base_area = (base_bb.maxx - base_bb.minx) * (base_bb.maxy - base_bb.miny);
        float prev_area = base_area;
        for (int r = 0; r < RN; r++) {
            float W = ramp[r] * (float)upem;
            bbox_t bb;
            /* timed offset+convert */
            const int IT = 20000;
            clock_t t0 = clock();
            volatile uint16_t sink = 0;
            uint16_t nc = 0;
            for (int it = 0; it < IT; it++)
                nc = emboldened(gcs[g], gncs[g], W, curves, MAX_CURVES, &bb);
            sink += nc;
            (void)sink;
            double ns = (double)(clock() - t0) / CLOCKS_PER_SEC * 1e9 / IT;
            float area = (bb.maxx - bb.minx) * (bb.maxy - bb.miny);
            const char *flag = "ok";
            if (!bb.finite)
                flag = "NON-FINITE";
            else if (r > 0 && area + 1e-3F < prev_area)
                flag = "SHRINK!";
            else if (r > 0 && base_area > 1.0F && area > base_area * 3.0F)
                flag = "SPIKE?";
            printf("  %-4s %5.2f %5.0f %7u   %8.1f   %5s   [%7.1f,%7.1f,%7.1f,%7.1f]  %s\n", names[g], ramp[r], W, nc, ns, bb.finite ? "yes" : "NO", bb.minx, bb.miny, bb.maxx, bb.maxy, flag);
            prev_area = area;
        }
        printf("\n");
    }

    /* ===== Isolated offset_points-only cost (A6/D-05): the true ADDED per-miss cost.
     * The point->curve convert already runs today; only offset_points is new work. ===== */
    printf("=== offset_points-only cost @ W=0.03em (added work per glyph miss) ===\n");
    printf("glyph   points  ns/glyph(offset-only)\n");
    for (int g = 0; g < GN; g++) {
        int pts = 0;
        for (int c = 0; c < gncs[g]; c++)
            pts += gcs[g][c].n;
        float W = 0.03F * (float)upem;
        const int IT = 200000;
        static contour_t work[MAX_CONTOURS];
        clock_t t0 = clock();
        volatile int64_t sink = 0;
        for (int it = 0; it < IT; it++) {
            for (int c = 0; c < gncs[g]; c++) {
                work[c] = gcs[g][c];
                offset_points(work[c].x, work[c].y, work[c].on, work[c].n, W);
                sink += work[c].x[0];
            }
        }
        (void)sink;
        double ns = (double)(clock() - t0) / CLOCKS_PER_SEC * 1e9 / IT;
        printf("  %-4s %7d   %10.1f\n", names[g], pts, ns);
    }
    printf("\n");

    /* ================= Task 2: A1 offsets + A3 sign ================= */
    printf("=== A1: raw sfnt table dump (big-endian) ===\n");
    uint32_t post = find_table(fdata, fontstart, "post");
    uint32_t os2 = find_table(fdata, fontstart, "OS/2");
    if (post) {
        printf("post @%u  underlinePosition(int16@8)=%d  underlineThickness(int16@10)=%d\n", post, rd_s16(fdata + post + 8), rd_s16(fdata + post + 10));
    } else {
        printf("post table ABSENT -> heuristic fallback path\n");
    }
    if (os2) {
        printf("OS/2 @%u  yStrikeoutSize(int16@26)=%d  yStrikeoutPosition(int16@28)=%d\n", os2, rd_s16(fdata + os2 + 26), rd_s16(fdata + os2 + 28));
    } else {
        printf("OS/2 table ABSENT -> heuristic fallback path\n");
    }
    printf("(fonttools 'ttx' unavailable in this environment -> A1 offsets remain the\n"
           " RESEARCH Pattern-4 ASSUMED spec offsets, cross-check DEFERRED; see findings.)\n\n");

    printf("=== A3: counter (inner contour) bbox at W=0.08em, sign=%+d ===\n", g_normal_sign);
    /* TrueType outer contour first, counter(s) after. Offset the ring, then compare each
     * contour's point bbox area vs W=0 — outer must grow, counter must shrink. */
    for (int gi2 = 0; gi2 < GN; gi2++) {
        if (codepoints[gi2] != 'o' && codepoints[gi2] != 'e')
            continue;
        int nc0 = gncs[gi2];
        static contour_t w0[MAX_CONTOURS];
        float W = 0.08F * (float)upem;
        /* outer = largest-area contour (stbtt does not guarantee outer-first ordering) */
        int outer = 0;
        float amax = -1.0F;
        for (int c = 0; c < nc0; c++) {
            bbox_t b = contour_point_bbox(&gcs[gi2][c]);
            float a = (b.maxx - b.minx) * (b.maxy - b.miny);
            if (a > amax) {
                amax = a;
                outer = c;
            }
        }
        printf("'%s' contours=%d (outer=contour %d)\n", names[gi2], nc0, outer);
        for (int c = 0; c < nc0; c++) {
            w0[c] = gcs[gi2][c];
            bbox_t b0 = contour_point_bbox(&w0[c]);
            offset_points(w0[c].x, w0[c].y, w0[c].on, w0[c].n, W);
            bbox_t b1 = contour_point_bbox(&w0[c]);
            float a0 = (b0.maxx - b0.minx) * (b0.maxy - b0.miny);
            float a1 = (b1.maxx - b1.minx) * (b1.maxy - b1.miny);
            int is_outer = (c == outer);
            const char *role = is_outer ? "outer" : "counter";
            const char *verdict;
            if (is_outer)
                verdict = (a1 > a0) ? "  <- grows (sign OK)" : "  <- SHRINKS (flip sign!)";
            else
                verdict = (a1 < a0) ? "  <- shrinks (sign OK)" : "  <- GROWS (flip sign!)";
            printf("   [%d %-7s] area W0=%9.0f  W.08=%9.0f%s\n", c, role, a0, a1, verdict);
        }
    }
    printf("\n");

    /* ===== Counter-aperture ramp: the true W-cap driver (merge/collapse detection) =====
     * The outer silhouette grows cleanly to large W, but tight counters ('e','o','8')
     * close first. Track the SMALLEST counter's area fraction vs W=0 across the ramp;
     * the cap is the largest W before any counter drops below the collapse threshold. */
    printf("=== Counter-aperture ramp (fraction of W=0 area; collapse = merged stroke) ===\n");
    printf("glyph  ");
    for (int r = 0; r < RN; r++)
        printf(" %5.2f", ramp[r]);
    printf("   (em)\n");
    const float COLLAPSE_FRAC = 0.25F; /* below 25% of original aperture = visually merged */
    float worst_ok_cap = ramp[RN - 1];
    for (int gi2 = 0; gi2 < GN; gi2++) {
        int nc0 = gncs[gi2];
        if (nc0 < 2)
            continue; /* no counter */
        /* find outer (largest area) to exclude it */
        int outer = 0;
        float amax = -1.0F;
        for (int c = 0; c < nc0; c++) {
            bbox_t b = contour_point_bbox(&gcs[gi2][c]);
            float a = (b.maxx - b.minx) * (b.maxy - b.miny);
            if (a > amax) {
                amax = a;
                outer = c;
            }
        }
        printf("  %-4s ", names[gi2]);
        float glyph_cap = ramp[RN - 1];
        int collapsed = 0;
        for (int r = 0; r < RN; r++) {
            float W = ramp[r] * (float)upem;
            /* smallest counter area fraction at this W */
            float min_frac = 1e30F;
            for (int c = 0; c < nc0; c++) {
                if (c == outer)
                    continue;
                contour_t wc = gcs[gi2][c];
                bbox_t b0 = contour_point_bbox(&wc);
                offset_points(wc.x, wc.y, wc.on, wc.n, W);
                bbox_t b1 = contour_point_bbox(&wc);
                float a0 = (b0.maxx - b0.minx) * (b0.maxy - b0.miny);
                float a1 = (b1.maxx - b1.minx) * (b1.maxy - b1.miny);
                float frac = (a0 > 1.0F) ? a1 / a0 : 1.0F;
                if (frac < min_frac)
                    min_frac = frac;
            }
            printf(" %5.2f", min_frac);
            if (!collapsed && r > 0 && min_frac < COLLAPSE_FRAC) {
                glyph_cap = ramp[r - 1];
                collapsed = 1;
            }
        }
        printf("   cap<=%.2fem%s\n", glyph_cap, collapsed ? "  (counter collapses above)" : "  (no collapse in ramp)");
        if (glyph_cap < worst_ok_cap)
            worst_ok_cap = glyph_cap;
    }
    printf("\n>>> Tightest-counter W cap across A W e o 8 , @ : %.2f em <<<\n\n", worst_ok_cap);

    free(fdata);
    return 0;
}
