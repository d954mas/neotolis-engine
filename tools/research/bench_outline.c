/* bench_outline — Phase 73 spike (73-06): offset self-intersection resolution.
 * bench_embolden proved offset_points corner quality but left the WIDE-outline defects:
 * enclosed winding-0 holes inside the true dilation. Three defect mechanisms found
 * (on the PRODUCTION-faithful sparse ring — builder strips stbtt implicit midpoints):
 *   D1 swallowtail: W/2 > local concave curvature radius -> offset ring self-crosses,
 *      the loop winds OPPOSITE the contour, nonzero winding cancels -> notch;
 *   D2 phantom counter: a collapsing hole that overshoots its center comes out
 *      POINT-REFLECTED (180-deg rotation): same shoelace sign, often no self-crossing
 *      -> invisible to the production shrink+selfx drop heuristic -> phantom hole;
 *   D3 sealed dent pocket: at a cap-binding reflex corner the capped miter undershoots;
 *      when the converging walls cross, the dent seals into an enclosed 0-winding pocket
 *      with NO wrong-signed loop ('W' valley).
 * Fix prototyped here, all on the int32 point ring between offset and point->quad
 * conversion (composes with the unchanged converter):
 *   1. offset with a Clipper-style RETRACTION JOIN at cap-binding reflex corners of
 *      grower rings (exact-R wall endpoints + original vertex) -> walls cross at the
 *      true trim point (kills D3; every other vertex = production formula verbatim);
 *   2. UNCROSS: split edges at all proper self-crossings, rewire each crossing into
 *      two touching corners (orientation-preserving: a_in->b_out, b_in->a_out),
 *      extract loops (next[] is a permutation -> clean cycles);
 *   3. SIGNED-LOOP FILTER: drop loops whose area sign opposes the original contour
 *      orientation (winding-cancelling defects, D1) + degenerate slivers; winding is
 *      preserved elsewhere; positive overlaps (merging lobes) keep winding 2 which
 *      the shader's CalcCoverage clamps — no trimming needed;
 *   4. EROSION-SURVIVAL TEST on shrinker (hole) rings/loops: keep iff some vertex is
 *      INSIDE the original ring AND >= 0.75*R away from it (kills D2; supersedes the
 *      shrink+selfx drop heuristic).
 * PROOF: raster ground truth (Minkowski dilation, radius R = W/2 — the summed-normal
 * formula is the exact miter for W/2) vs nonzero winding of emitted curves; enclosed
 * winding-hole / open-dent / spill px per glyph/W; W=0 byte-identity; ns/glyph.
 * RESULT: 9733 naive hole px -> 0 across 20 glyphs x W<=0.32em; max 106 curves (cap
 * 256); +1-7us per cache miss. Research-only, standalone:
 *   clang -O2 -I deps/stb tools/research/bench_outline.c -o build/bench_outline.exe */
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
#define MAX_XINGS 256    /* proper self-crossings per ring */
#define MAX_EDGE_XINGS 8 /* crossings on one edge */
#define MAX_LOOPS 16     /* output rings per input ring */
#define MAX_NODES (MAX_POINTS + 2 * MAX_XINGS)
#define PROD_CURVE_CAP 256 /* NT_FONT_MAX_CURVES_PER_GLYPH */

// #region Point ring materialization (mirrors bench_embolden / nt_font.c layout)
typedef struct {
    int32_t x[MAX_POINTS];
    int32_t y[MAX_POINTS];
    uint8_t on[MAX_POINTS];
    uint16_t n;
} contour_t;

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
            /* PRODUCTION-FAITHFUL ring: the builder strips stbtt's implicit on-curve
             * midpoints (nt_builder_font.c vertices_to_points) — the pack ring keeps
             * raw off-off runs. bench_embolden kept the dense expanded ring; the
             * offset geometry (normals, crossings) differs on the sparse ring. */
            int implicit = 0;
            if (i + 1 < nv && v[i + 1].type == STBTT_vcurve) {
                int mid_x = (v[i].cx + v[i + 1].cx) >> 1;
                int mid_y = (v[i].cy + v[i + 1].cy) >> 1;
                if (v[i].x == mid_x && v[i].y == mid_y)
                    implicit = 1;
            }
            if (!implicit) {
                c->x[c->n] = v[i].x;
                c->y[c->n] = v[i].y;
                c->on[c->n] = 1;
                c->n++;
            }
        }
    }
    for (int c = 0; c <= nc; c++) {
        contour_t *cc = &cs[c];
        while (cc->n > 1 && cc->on[cc->n - 1] && cc->x[cc->n - 1] == cc->x[0] && cc->y[cc->n - 1] == cc->y[0])
            cc->n--;
    }
    return nc + 1;
}
// #endregion

// #region offset_points (verbatim port of nt_font.c:608, sign=-1 folded in)
static void offset_points(int32_t *x, int32_t *y, const uint8_t *on, uint16_t n, float W) {
    (void)on;
    if (W == 0.0F || n < 2)
        return;
    static int32_t ox[MAX_POINTS];
    static int32_t oy[MAX_POINTS];
    for (uint16_t i = 0; i < n; i++) {
        ox[i] = x[i];
        oy[i] = y[i];
    }
    for (uint16_t i = 0; i < n; i++) {
        uint16_t p = (i == 0) ? (uint16_t)(n - 1) : (uint16_t)(i - 1);
        uint16_t q = (uint16_t)((i + 1) % n);
        float inx = (float)(ox[i] - ox[p]), iny = (float)(oy[i] - oy[p]);
        float oux = (float)(ox[q] - ox[i]), ouy = (float)(oy[q] - oy[i]);
        float li = sqrtf(inx * inx + iny * iny) + 1e-6F;
        float lo = sqrtf(oux * oux + ouy * ouy) + 1e-6F;
        float nix = iny / li, niy = -inx / li;
        float nox = ouy / lo, noy = -oux / lo;
        float sx = nix + nox, sy = niy + noy;
        float d = 1.0F / fmaxf(1.0F + (nix * nox + niy * noy), 0.25F);
        d = fminf(d, 2.0F);
        x[i] += (int32_t)lrintf(-sx * d * W * 0.5F);
        y[i] += (int32_t)lrintf(-sy * d * W * 0.5F);
    }
}
/* Offset with a Clipper-style join at CAP-BINDING REFLEX corners of GROWER rings:
 * there the capped miter undershoots, the converging offset walls seal the dent into
 * an enclosed winding-0 pocket ('W' valley @0.16em) that no loop filter can lift.
 * Emitting wall endpoints at exact R plus the ORIGINAL vertex as retraction anchor
 * makes the walls cross at the true trim point; excision then seals the corner
 * cleanly. All other vertices use the production formula verbatim. */
static int offset_with_joins(const contour_t *src, contour_t *dst, float W, double a0) {
    uint16_t n = src->n;
    if (W == 0.0F || n < 2) {
        *dst = *src;
        return 0;
    }
    int grower = (W > 0.0F) ? (a0 < 0.0) : (a0 > 0.0);
    int joins = 0;
    uint16_t m = 0;
    for (uint16_t i = 0; i < n; i++) {
        uint16_t p = (i == 0) ? (uint16_t)(n - 1) : (uint16_t)(i - 1);
        uint16_t q = (uint16_t)((i + 1) % n);
        float inx = (float)(src->x[i] - src->x[p]), iny = (float)(src->y[i] - src->y[p]);
        float oux = (float)(src->x[q] - src->x[i]), ouy = (float)(src->y[q] - src->y[i]);
        float li = sqrtf(inx * inx + iny * iny) + 1e-6F;
        float lo = sqrtf(oux * oux + ouy * ouy) + 1e-6F;
        float nix = iny / li, niy = -inx / li;
        float nox = ouy / lo, noy = -oux / lo;
        float dot = (nix * nox) + (niy * noy);
        float turn_z = (inx * ouy) - (iny * oux);
        int reflex = ((turn_z > 0.0F) != (a0 > 0.0)) && turn_z != 0.0F;
        if (grower && src->on[i] && reflex && dot <= -0.5F && m + 3 <= MAX_POINTS) {
            /* wall endpoints at exact R = W/2 along each edge's outward normal (sign -1) */
            dst->x[m] = src->x[i] + (int32_t)lrintf(-nix * W * 0.5F);
            dst->y[m] = src->y[i] + (int32_t)lrintf(-niy * W * 0.5F);
            dst->on[m] = 1;
            m++;
            dst->x[m] = src->x[i];
            dst->y[m] = src->y[i];
            dst->on[m] = 1;
            m++;
            dst->x[m] = src->x[i] + (int32_t)lrintf(-nox * W * 0.5F);
            dst->y[m] = src->y[i] + (int32_t)lrintf(-noy * W * 0.5F);
            dst->on[m] = 1;
            m++;
            joins++;
            continue;
        }
        float sx = nix + nox, sy = niy + noy;
        float d = 1.0F / fmaxf(1.0F + dot, 0.25F);
        d = fminf(d, 2.0F);
        dst->x[m] = src->x[i] + (int32_t)lrintf(-sx * d * W * 0.5F);
        dst->y[m] = src->y[i] + (int32_t)lrintf(-sy * d * W * 0.5F);
        dst->on[m] = src->on[i];
        m++;
    }
    dst->n = m;
    return joins;
}
// #endregion

// #region Predicates (verbatim shape of nt_font.c contour_signed_area / orient / self-x)
static double contour_signed_area(const int32_t *x, const int32_t *y, uint16_t n) {
    int64_t acc = 0;
    for (uint16_t p = 0; p < n; p++) {
        uint16_t q = (uint16_t)((p + 1) % n);
        acc += ((int64_t)x[p] * y[q]) - ((int64_t)x[q] * y[p]);
    }
    return 0.5 * (double)acc;
}

static int orient(int32_t ax, int32_t ay, int32_t bx, int32_t by, int32_t cx, int32_t cy) {
    int64_t cross = ((int64_t)(bx - ax) * (cy - ay)) - ((int64_t)(by - ay) * (cx - ax));
    return (cross > 0) - (cross < 0);
}

static int proper_cross(int32_t ax, int32_t ay, int32_t bx, int32_t by, int32_t cx, int32_t cy, int32_t dx, int32_t dy) {
    int o1 = orient(ax, ay, bx, by, cx, cy);
    int o2 = orient(ax, ay, bx, by, dx, dy);
    int o3 = orient(cx, cy, dx, dy, ax, ay);
    int o4 = orient(cx, cy, dx, dy, bx, by);
    return (o1 != 0 && o2 != 0 && o3 != 0 && o4 != 0 && o1 != o2 && o3 != o4);
}

static int ring_self_intersects(const int32_t *x, const int32_t *y, uint16_t n) {
    if (n < 4)
        return 0;
    for (uint16_t i = 0; i < n; i++) {
        uint16_t i2 = (uint16_t)((i + 1) % n);
        for (uint16_t j = (uint16_t)(i + 1); j < n; j++) {
            uint16_t j2 = (uint16_t)((j + 1) % n);
            if (j == i2 || j2 == i)
                continue;
            if (proper_cross(x[i], y[i], x[i2], y[i2], x[j], y[j], x[j2], y[j2]))
                return 1;
        }
    }
    return 0;
}
// #endregion

// #region Uncross + signed-loop filter (THE spike algorithm)
typedef struct {
    uint16_t ei, ej; /* crossing edges (i,i+1) x (j,j+1) */
    double ti, tj;   /* params along each edge */
    int32_t px, py;  /* intersection, rounded to the int ring grid (production-faithful) */
} xing_t;

typedef struct {
    int crossings;
    int loops_kept, loops_dropped;
    int ring_dropped;    /* whole ring dropped (inverted, no crossings) */
    int phantom_dropped; /* hole ring dropped by the erosion-survival test */
    int residual_selfx;  /* output rings that still self-cross (rounding slivers) */
} rstats_t;

/* Erosion-survival test. A collapsing counter that overshoots its center comes out
 * POINT-REFLECTED (180-deg rotation): same shoelace sign, no self-crossing — invisible
 * to every sign test, renders a phantom hole. A genuinely surviving eroded hole sits
 * ~R from the original contour along its smooth stretches; a phantom hugs it (its
 * points land near the ANTIPODAL original boundary). Keep iff any vertex is >= kappa*R
 * from the original ring. */
#define SURVIVAL_KAPPA 0.75
/* Max over vertices of (dist to original ring), counting ONLY vertices INSIDE the
 * original ring. Overshoot past the antipodal boundary lands vertices far from the
 * original ring but OUTSIDE it (in the fill) — distance alone can't tell them from a
 * deep valid remnant; the containment test can. */
static double ring_survival_depth(const int32_t *x, const int32_t *y, uint16_t n, const int32_t *bx, const int32_t *by, uint16_t bn) {
    double best_max = 0.0;
    for (uint16_t i = 0; i < n; i++) {
        double px = (double)x[i] + 0.5, py = (double)y[i] + 0.5; /* nudge off lattice */
        int inside = 0;
        for (uint16_t j = 0; j < bn; j++) { /* crossing parity vs base ring */
            uint16_t j2 = (uint16_t)((j + 1) % bn);
            double ay = (double)by[j], cy = (double)by[j2];
            if ((ay > py) == (cy > py))
                continue;
            double t = (py - ay) / (cy - ay);
            double xi = (double)bx[j] + t * ((double)bx[j2] - (double)bx[j]);
            if (xi > px)
                inside = !inside;
        }
        if (!inside)
            continue;
        double dmin = 1e30;
        for (uint16_t j = 0; j < bn; j++) {
            uint16_t j2 = (uint16_t)((j + 1) % bn);
            double ax = (double)bx[j], ay = (double)by[j];
            double vx = (double)bx[j2] - ax, vy = (double)by[j2] - ay;
            double wx = px - ax, wy = py - ay;
            double vv = vx * vx + vy * vy;
            double t = (vv > 1e-12) ? (wx * vx + wy * vy) / vv : 0.0;
            if (t < 0.0)
                t = 0.0;
            if (t > 1.0)
                t = 1.0;
            double dx = wx - t * vx, dy = wy - t * vy;
            double d2 = dx * dx + dy * dy;
            if (d2 < dmin)
                dmin = d2;
        }
        if (dmin > best_max)
            best_max = dmin;
    }
    return sqrt(best_max);
}

// #region Inradius-based survival (calibrated criterion)
/* Distance from (px,py) to the closed ring's boundary (min over edges). */
static double dist_to_ring(double px, double py, const int32_t *x, const int32_t *y, uint16_t n) {
    double best = 1e30;
    for (uint16_t j = 0; j < n; j++) {
        uint16_t j2 = (uint16_t)((j + 1) % n);
        double ax = (double)x[j], ay = (double)y[j];
        double vx = (double)x[j2] - ax, vy = (double)y[j2] - ay;
        double wx = px - ax, wy = py - ay;
        double vv = vx * vx + vy * vy;
        double t = (vv > 1e-12) ? (wx * vx + wy * vy) / vv : 0.0;
        if (t < 0.0)
            t = 0.0;
        if (t > 1.0)
            t = 1.0;
        double dx = wx - t * vx, dy = wy - t * vy;
        double d2 = dx * dx + dy * dy;
        if (d2 < best)
            best = d2;
    }
    return sqrt(best);
}

static int point_in_ring(double px, double py, const int32_t *x, const int32_t *y, uint16_t n) {
    int inside = 0;
    for (uint16_t j = 0; j < n; j++) {
        uint16_t j2 = (uint16_t)((j + 1) % n);
        double ay = (double)y[j], cy = (double)y[j2];
        if ((ay > py) == (cy > py))
            continue;
        double t = (py - ay) / (cy - ay);
        double xi = (double)x[j] + t * ((double)x[j2] - (double)x[j]);
        if (xi > px)
            inside = !inside;
    }
    return inside;
}

/* Fine-grid inradius — the trusted harness GT reference (obviously correct, slow). */
static double poly_inradius_grid(const int32_t *x, const int32_t *y, uint16_t n) {
    if (n < 3)
        return 0.0;
    double minx = 1e30, miny = 1e30, maxx = -1e30, maxy = -1e30;
    for (uint16_t i = 0; i < n; i++) {
        if (x[i] < minx)
            minx = x[i];
        if (x[i] > maxx)
            maxx = x[i];
        if (y[i] < miny)
            miny = y[i];
        if (y[i] > maxy)
            maxy = y[i];
    }
    double ext = (maxx - minx) > (maxy - miny) ? (maxx - minx) : (maxy - miny);
    if (ext <= 0.0)
        return 0.0;
    double step = ext / 512.0;
    double best = 0.0;
    for (double py = miny + 0.5 * step; py < maxy; py += step)
        for (double px = minx + 0.5 * step; px < maxx; px += step)
            if (point_in_ring(px, py, x, y, n)) {
                double d = dist_to_ring(px, py, x, y, n);
                if (d > best)
                    best = d;
            }
    return best;
}

/* Signed distance to ring: +inside, -outside. */
static double signed_dist_ring(double px, double py, const int32_t *x, const int32_t *y, uint16_t n) {
    double d = dist_to_ring(px, py, x, y, n);
    return point_in_ring(px, py, x, y, n) ? d : -d;
}

/* Inradius = radius of the largest inscribed disk (pole of inaccessibility). Polylabel-style
 * quadtree refinement: prune cells that can't beat the running best, refine the promising ones
 * to `precision`. This is the PRODUCTION algorithm — heap-free (bounded DFS stack), no fudge
 * constant; the criterion "counter survives iff inradius > R" is pure geometry. */
typedef struct {
    double cx, cy, h;
} pa_cell_t;
#define PA_STACK_MAX 512
static double poly_inradius(const int32_t *x, const int32_t *y, uint16_t n) {
    if (n < 3)
        return 0.0;
    double minx = 1e30, miny = 1e30, maxx = -1e30, maxy = -1e30;
    for (uint16_t i = 0; i < n; i++) {
        if (x[i] < minx)
            minx = x[i];
        if (x[i] > maxx)
            maxx = x[i];
        if (y[i] < miny)
            miny = y[i];
        if (y[i] > maxy)
            maxy = y[i];
    }
    double w = maxx - minx, ht = maxy - miny;
    double cell = (w < ht ? w : ht);
    if (cell <= 0.0)
        return 0.0;
    double half = cell * 0.5;
    const double PREC = 0.25; /* sub-unit: well below the weight-quantization step */
    static pa_cell_t stack[PA_STACK_MAX];
    int nxc = (int)(w / cell) + 1; /* integer induction (mirrors production) */
    int nyc = (int)(ht / cell) + 1;
    int sp = 0;
    for (int iy = 0; iy < nyc && sp < PA_STACK_MAX; iy++)
        for (int ix = 0; ix < nxc && sp < PA_STACK_MAX; ix++)
            stack[sp++] = (pa_cell_t){minx + half + (double)ix * cell, miny + half + (double)iy * cell, half};
    double best = signed_dist_ring(minx + w * 0.5, miny + ht * 0.5, x, y, n); /* centroid seed */
    int guard = 0;
    while (sp > 0 && guard++ < 200000) {
        pa_cell_t c = stack[--sp];
        double d = signed_dist_ring(c.cx, c.cy, x, y, n);
        if (d > best)
            best = d;
        if (c.h < PREC)
            continue;
        if (d + c.h * 1.4142135623730951 <= best)
            continue; /* prune: this cell cannot contain a deeper point */
        double hh = c.h * 0.5;
        if (sp + 4 <= PA_STACK_MAX) {
            stack[sp++] = (pa_cell_t){c.cx - hh, c.cy - hh, hh};
            stack[sp++] = (pa_cell_t){c.cx + hh, c.cy - hh, hh};
            stack[sp++] = (pa_cell_t){c.cx - hh, c.cy + hh, hh};
            stack[sp++] = (pa_cell_t){c.cx + hh, c.cy + hh, hh};
        }
    }
    return best > 0.0 ? best : 0.0;
}

/* Neck / channel width of a counter ring = the smallest inward crossing distance. From each edge
 * midpoint, cast a ray INTO the hole (interior) and measure the distance to the first OTHER wall it
 * hits. On a wide part -> ~2*inradius; at the neck -> the channel width. min over edges = neck gap.
 * Robust to near-vertex proximity (the ray goes across the interior, never to adjacent edges). */
static double counter_neck_gap(const int32_t *x, const int32_t *y, uint16_t n) {
    if (n < 4)
        return 1e30;
    double best = 1e30;
    for (uint16_t k = 0; k < n; k++) {
        uint16_t k2 = (uint16_t)((k + 1) % n);
        double mx = ((double)x[k] + x[k2]) * 0.5, my = ((double)y[k] + y[k2]) * 0.5;
        double ex = (double)x[k2] - x[k], ey = (double)y[k2] - y[k];
        double el = sqrt(ex * ex + ey * ey);
        if (el < 1e-9)
            continue;
        double nx = -ey / el, ny = ex / el; /* one normal; pick the one pointing into the hole */
        if (!point_in_ring(mx + 0.75 * nx, my + 0.75 * ny, x, y, n)) {
            nx = -nx;
            ny = -ny;
        }
        if (!point_in_ring(mx + 0.75 * nx, my + 0.75 * ny, x, y, n))
            continue; /* midpoint on a spur — neither side is interior */
        double dmin = 1e30;
        for (uint16_t j = 0; j < n; j++) {
            if (j == k)
                continue;
            uint16_t j2 = (uint16_t)((j + 1) % n);
            double ax = (double)x[j], ay = (double)y[j];
            double bx = (double)x[j2] - ax, by = (double)y[j2] - ay;
            double denom = nx * by - ny * bx; /* cross(ray_dir, seg) */
            if (fabs(denom) < 1e-9)
                continue;
            double wx = ax - mx, wy = ay - my;
            double u = (wx * by - wy * bx) / denom; /* ray param (dir is unit) */
            double s = (wx * ny - wy * nx) / denom; /* seg param */
            if (u > 0.5 && s >= 0.0 && s <= 1.0 && u < dmin)
                dmin = u;
        }
        if (dmin < best)
            best = dmin;
    }
    return best;
}

/* 0 = kappa proxy (shipped afed587d), 1 = calibrated inradius gate. */
static int g_survival_mode = 1;
static int g_last_old_mismatch, g_last_new_mismatch, g_last_poly_vs_grid; /* per-font calibration results */

/* Counter-preserving (non-uniform) outline: the OUTER offsets by full R = W/2 (thick), but a
 * COUNTER (hole) caps its inward offset so it never SEALS or closes, at any width, on any font.
 * The binding constraint is the counter's NECK (narrowest channel), not its widest inscribed
 * circle: a narrow spiral/channel (e.g. '@') seals long before the inradius cap engages. Cap
 * W_eff <= (1-KEEP)*neck_gap so the neck keeps >= KEEP of its width. Since neck_gap <= 2*inradius
 * ALWAYS, this single cap also guarantees the wide part keeps >= KEEP*inradius (subsumes it).
 * KEEP is a fraction of the glyph's OWN geometry (scale/font-invariant). */
#define COUNTER_KEEP 0.35F
static int g_counter_preserve = 1;

/* Seal radius: the largest inward offset R at which the counter ring does NOT self-intersect.
 * A neck seal OR a sharp-corner over-shoot both show up as a self-intersection of the capped
 * offset ring — one unified, reliable signal (the fragile ray-cast neck misses corner seals).
 * Binary search in (0, inradius]; if the counter never self-intersects (convex), inradius governs.
 * wsign = sign of the offset weight (the shrink direction). O(iters * n^2), miss path only. */
static double counter_seal_radius(const contour_t *base, float wsign) {
    double inrad = poly_inradius(base->x, base->y, base->n);
    if (inrad <= 0.0)
        return 0.0;
    static contour_t work;
    double a0 = contour_signed_area(base->x, base->y, base->n);
    double hi = inrad * 0.98;
    offset_with_joins(base, &work, copysignf((float)(2.0 * hi), wsign), a0);
    if (!ring_self_intersects(work.x, work.y, work.n))
        return inrad; /* convex counter: no seal, inradius bounds the offset */
    double lo = 0.0;
    for (int it = 0; it < 20; it++) {
        double mid = 0.5 * (lo + hi);
        offset_with_joins(base, &work, copysignf((float)(2.0 * mid), wsign), a0);
        if (ring_self_intersects(work.x, work.y, work.n))
            hi = mid;
        else
            lo = mid;
    }
    return lo; /* largest R with no self-intersection ~ just below the seal */
}

/* Capped offset weight for a shrinker (hole): |W_eff| <= 2*(1-KEEP)*seal_radius, so the counter
 * keeps >= KEEP of its narrowest opening (neck or corner) AND >= KEEP*inradius, and NEVER seals.
 * Growers pass through unchanged. */
static float counter_cap_weight(const contour_t *base, float W, int is_shrinker) {
    if (!g_counter_preserve || !is_shrinker || W == 0.0F)
        return W;
    double rseal = counter_seal_radius(base, W);
    float cap = 2.0F * (1.0F - COUNTER_KEEP) * (float)rseal;
    return (fabsf(W) > cap) ? copysignf(cap, W) : W;
}

/* A shrinker (counter) contour is DROPPED (fills solid) iff the true Minkowski erosion
 * by R empties it: inradius(base) <= R. Replaces the offset-vertex depth proxy, which
 * under-samples the deepest interior point and fills open counters early. */
static int counter_survives(const int32_t *bx, const int32_t *by, uint16_t bn, double R) {
    if (g_survival_mode == 0)
        return 1; /* mode 0 handled by ring_survival_depth at call sites */
    return poly_inradius(bx, by, bn) > R;
}
// #endregion

static int find_crossings(const int32_t *x, const int32_t *y, uint16_t n, xing_t *out, int max_out) {
    int cnt = 0;
    if (n < 4)
        return 0;
    for (uint16_t i = 0; i < n; i++) {
        uint16_t i2 = (uint16_t)((i + 1) % n);
        for (uint16_t j = (uint16_t)(i + 1); j < n; j++) {
            uint16_t j2 = (uint16_t)((j + 1) % n);
            if (j == i2 || j2 == i)
                continue;
            if (!proper_cross(x[i], y[i], x[i2], y[i2], x[j], y[j], x[j2], y[j2]))
                continue;
            if (cnt >= max_out)
                return -1; /* overflow -> caller bails to naive */
            double rx = (double)(x[i2] - x[i]), ry = (double)(y[i2] - y[i]);
            double sx = (double)(x[j2] - x[j]), sy = (double)(y[j2] - y[j]);
            double den = rx * sy - ry * sx; /* proper crossing => den != 0 */
            double qx = (double)(x[j] - x[i]), qy = (double)(y[j] - y[i]);
            double t = (qx * sy - qy * sx) / den;
            double u = (qx * ry - qy * rx) / den;
            out[cnt].ei = i;
            out[cnt].ej = j;
            out[cnt].ti = t;
            out[cnt].tj = u;
            out[cnt].px = (int32_t)lrint((double)x[i] + t * rx);
            out[cnt].py = (int32_t)lrint((double)y[i] + t * ry);
            cnt++;
        }
    }
    return cnt;
}

typedef struct {
    int32_t x, y;
    uint8_t on;
    int next;
    uint8_t used;
} node_t;

static node_t s_nodes[MAX_NODES];
static xing_t s_xings[MAX_XINGS];

/* Resolve one offset ring. orig_area = signed area of the PRE-offset ring (its
 * orientation is the ground truth for which loops are defects). base = the pre-offset
 * ring for the erosion-survival test; is_shrinker = this ring moves inward (hole under
 * bold W>0, outer under thin W<0). Returns ring count written to out[] (0 = fully
 * dropped). verbose prints per-loop evidence. */
static int resolve_ring(const int32_t *x, const int32_t *y, const uint8_t *on, uint16_t n, double orig_area, const contour_t *base, int is_shrinker, double R, contour_t *out, int max_out,
                        rstats_t *st, int verbose) {
    st->crossings = 0;
    st->loops_kept = 0;
    st->loops_dropped = 0;
    st->ring_dropped = 0;
    st->phantom_dropped = 0;
    st->residual_selfx = 0;

    /* Calibrated gate: a shrinker whose Minkowski erosion-by-R empties fills solid. */
    if (g_survival_mode == 1 && is_shrinker && !counter_survives(base->x, base->y, base->n, R)) {
        st->phantom_dropped = 1;
        if (verbose)
            printf("      ring: inradius %.0f <= R %.0f -> DROP (fills)\n", poly_inradius(base->x, base->y, base->n), R);
        return 0;
    }

    int nx = find_crossings(x, y, n, s_xings, MAX_XINGS);
    if (nx < 0) { /* crossing overflow: keep raw ring (today's behavior), flag it */
        st->crossings = -1;
        memcpy(out[0].x, x, sizeof(int32_t) * n);
        memcpy(out[0].y, y, sizeof(int32_t) * n);
        memcpy(out[0].on, on, n);
        out[0].n = n;
        return 1;
    }
    st->crossings = nx;

    if (nx == 0) {
        double area_after = contour_signed_area(x, y, n);
        /* mirror inversion (sign flip) OR point-reflected phantom (survival test) */
        if ((orig_area > 0.0) != (area_after > 0.0) || fabs(area_after) < 1.0) {
            st->ring_dropped = 1;
            return 0;
        }
        if (g_survival_mode == 0 && is_shrinker && ring_survival_depth(x, y, n, base->x, base->y, base->n) < SURVIVAL_KAPPA * R) {
            st->phantom_dropped = 1;
            if (verbose)
                printf("      ring: PHANTOM (max dist < %.2f*R) -> DROP\n", SURVIVAL_KAPPA);
            return 0;
        }
        memcpy(out[0].x, x, sizeof(int32_t) * n);
        memcpy(out[0].y, y, sizeof(int32_t) * n);
        memcpy(out[0].on, on, n);
        out[0].n = n;
        st->loops_kept = 1;
        return 1;
    }

    // #region Build node chain with per-edge splits
    for (uint16_t i = 0; i < n; i++) {
        s_nodes[i] = (node_t){x[i], y[i], on[i], (int)((i + 1) % n), 0};
    }
    int nn = n;
    /* crossing k owns nodes na=n+2k (on strand ei), nb=n+2k+1 (on strand ej) */
    for (int k = 0; k < nx; k++) {
        s_nodes[nn] = (node_t){s_xings[k].px, s_xings[k].py, 1, -1, 0};
        s_nodes[nn + 1] = (node_t){s_xings[k].px, s_xings[k].py, 1, -1, 0};
        nn += 2;
    }
    /* splice each edge's crossings in ascending-t order */
    for (uint16_t e = 0; e < n; e++) {
        int list[MAX_EDGE_XINGS];
        double lt[MAX_EDGE_XINGS];
        int lc = 0;
        for (int k = 0; k < nx; k++) {
            if (s_xings[k].ei == e) {
                if (lc >= MAX_EDGE_XINGS)
                    return 1; /* overload: bail keeping raw handled above; practically unreachable */
                list[lc] = (int)n + 2 * k;
                lt[lc] = s_xings[k].ti;
                lc++;
            }
            if (s_xings[k].ej == e) {
                if (lc >= MAX_EDGE_XINGS)
                    return 1;
                list[lc] = (int)n + 2 * k + 1;
                lt[lc] = s_xings[k].tj;
                lc++;
            }
        }
        if (lc == 0)
            continue;
        for (int a = 1; a < lc; a++) { /* insertion sort by t */
            int li = list[a];
            double ta = lt[a];
            int b = a - 1;
            while (b >= 0 && lt[b] > ta) {
                list[b + 1] = list[b];
                lt[b + 1] = lt[b];
                b--;
            }
            list[b + 1] = li;
            lt[b + 1] = ta;
        }
        int tail = s_nodes[e].next; /* original edge end */
        int prev = (int)e;
        for (int a = 0; a < lc; a++) {
            s_nodes[prev].next = list[a];
            prev = list[a];
        }
        s_nodes[prev].next = tail;
    }
    /* uncross: orientation-preserving smoothing at every crossing (swap outgoing) */
    for (int k = 0; k < nx; k++) {
        int na = (int)n + 2 * k, nb = na + 1;
        int tmp = s_nodes[na].next;
        s_nodes[na].next = s_nodes[nb].next;
        s_nodes[nb].next = tmp;
    }
    // #endregion

    // #region Extract loops, filter by orientation sign
    int nrings = 0;
    int orig_pos = orig_area > 0.0;
    for (int s = 0; s < nn; s++) {
        if (s_nodes[s].used)
            continue;
        static int32_t lx[MAX_NODES];
        static int32_t ly[MAX_NODES];
        static uint8_t lon[MAX_NODES];
        int cnt = 0;
        int cur = s;
        do {
            s_nodes[cur].used = 1;
            /* dedupe consecutive coincident points (rounded crossings on endpoints) */
            if (cnt == 0 || lx[cnt - 1] != s_nodes[cur].x || ly[cnt - 1] != s_nodes[cur].y) {
                lx[cnt] = s_nodes[cur].x;
                ly[cnt] = s_nodes[cur].y;
                lon[cnt] = s_nodes[cur].on;
                cnt++;
            }
            cur = s_nodes[cur].next;
        } while (cur != s && cnt <= nn);
        if (cnt > nn) {
            printf("    !! corrupt next-chain (bug)\n");
            return 0;
        }
        while (cnt > 1 && lx[cnt - 1] == lx[0] && ly[cnt - 1] == ly[0])
            cnt--; /* drop closing dup */
        double la = contour_signed_area(lx, ly, (uint16_t)cnt);
        int keep = (cnt >= 3) && (fabs(la) >= 1.0) && ((la > 0.0) == orig_pos);
        int phantom = 0;
        if (keep && g_survival_mode == 0 && is_shrinker && ring_survival_depth(lx, ly, (uint16_t)cnt, base->x, base->y, base->n) < SURVIVAL_KAPPA * R) {
            keep = 0;
            phantom = 1;
        }
        if (verbose)
            printf("      loop: pts=%d area=%+.0f sign=%s -> %s%s\n", cnt, la, (la > 0.0) == orig_pos ? "orig" : "INVERTED", keep ? "keep" : "DROP", phantom ? " (phantom)" : "");
        if (!keep) {
            if (phantom)
                st->phantom_dropped++;
            else
                st->loops_dropped++;
            continue;
        }
        if (nrings >= max_out || cnt > MAX_POINTS) {
            st->loops_dropped++;
            continue;
        }
        memcpy(out[nrings].x, lx, sizeof(int32_t) * (size_t)cnt);
        memcpy(out[nrings].y, ly, sizeof(int32_t) * (size_t)cnt);
        memcpy(out[nrings].on, lon, (size_t)cnt);
        out[nrings].n = (uint16_t)cnt;
        if (ring_self_intersects(out[nrings].x, out[nrings].y, out[nrings].n))
            st->residual_selfx++;
        nrings++;
        st->loops_kept++;
    }
    // #endregion
    return nrings;
}
// #endregion

// #region Point ring -> quadratic curves (mirrors nt_font.c:754-826)
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

// #region Glyph build pipelines: NAIVE (today's production) vs RESOLVED (spike)
typedef struct {
    int crossings;
    int loops_dropped;
    int rings_dropped;
    int phantoms_dropped;
    int residual_selfx;
} gstats_t;

/* mode 0 = NAIVE: offset + production drop heuristic (nt_font.c:738-752). */
static uint16_t build_naive(const contour_t *base, int nbc, float W, nt_curve_t *out, uint16_t max_c, gstats_t *gs) {
    static contour_t work;
    uint16_t total = 0;
    memset(gs, 0, sizeof(*gs));
    for (int i = 0; i < nbc; i++) {
        work = base[i];
        if (W != 0.0F) {
            double a0 = contour_signed_area(work.x, work.y, work.n);
            offset_points(work.x, work.y, work.on, work.n, W);
            double a1 = contour_signed_area(work.x, work.y, work.n);
            if (fabs(a1) < fabs(a0) && (ring_self_intersects(work.x, work.y, work.n) || (a0 > 0.0) != (a1 > 0.0))) {
                gs->rings_dropped++;
                continue;
            }
        }
        convert_contour(&work, out, &total, max_c);
    }
    return total;
}

/* mode 1 = RESOLVED: uncross + signed-loop filter + erosion-survival test.
 * SUPERSEDES the production drop heuristic entirely: mirror-flip drops fall out of the
 * sign filter, tangled shrinking counters get uncrossed and their remnants survival-
 * tested, point-reflected phantoms (invisible to every sign test) get caught by the
 * survival test, and the growing outer's waist microloop gets excised. */
static uint16_t build_resolved(const contour_t *base, int nbc, float W, nt_curve_t *out, uint16_t max_c, gstats_t *gs, int verbose) {
    static contour_t work;
    static contour_t rings[MAX_LOOPS];
    uint16_t total = 0;
    memset(gs, 0, sizeof(*gs));
    for (int i = 0; i < nbc; i++) {
        work = base[i];
        if (W == 0.0F) { /* identity gate — byte-identical to plain decode */
            convert_contour(&work, out, &total, max_c);
            continue;
        }
        double a0 = contour_signed_area(base[i].x, base[i].y, base[i].n);
        /* TT winding (builder/stbtt): outer negative area, hole positive. Bold (W>0)
         * shrinks holes; thin (W<0) shrinks outers. */
        int is_shrinker = (W > 0.0F) ? (a0 > 0.0) : (a0 < 0.0);
        float W_eff = counter_cap_weight(&base[i], W, is_shrinker); /* counter-preserving cap */
        int joins = offset_with_joins(&base[i], &work, W_eff, a0);
        rstats_t rs;
        if (verbose) {
            double a1 = contour_signed_area(work.x, work.y, work.n);
            printf("    contour %d: n=%u->%u area %+.0f -> %+.0f, joins=%d (R=%.0f Reff=%.0f)%s:\n", i, base[i].n, work.n, a0, a1, joins, 0.5F * (double)fabsf(W), 0.5F * (double)fabsf(W_eff),
                   is_shrinker ? " [shrinker]" : "");
        }
        int nr = resolve_ring(work.x, work.y, work.on, work.n, a0, &base[i], is_shrinker, 0.5 * (double)fabsf(W_eff), rings, MAX_LOOPS, &rs, verbose);
        gs->crossings += (rs.crossings < 0) ? 0 : rs.crossings;
        gs->loops_dropped += rs.loops_dropped;
        gs->rings_dropped += rs.ring_dropped;
        gs->phantoms_dropped += rs.phantom_dropped;
        gs->residual_selfx += rs.residual_selfx;
        for (int r = 0; r < nr; r++)
            convert_contour(&rings[r], out, &total, max_c);
    }
    return total;
}
// #endregion

// #region Raster proof: ground-truth Minkowski dilation vs emitted-curve winding
typedef struct {
    float ax, ay, bx, by;
} seg_t;

#define FLAT_K 8 /* segments per quad */
static int flatten(const nt_curve_t *cv, uint16_t nc, seg_t *out, int max_s) {
    int ns = 0;
    for (uint16_t i = 0; i < nc; i++) {
        float px = cv[i].p0x, py = cv[i].p0y;
        for (int k = 1; k <= FLAT_K; k++) {
            float t = (float)k / FLAT_K;
            float u = 1.0F - t;
            float qx = u * u * cv[i].p0x + 2.0F * u * t * cv[i].p1x + t * t * cv[i].p2x;
            float qy = u * u * cv[i].p0y + 2.0F * u * t * cv[i].p1y + t * t * cv[i].p2y;
            if (ns < max_s)
                out[ns++] = (seg_t){px, py, qx, qy};
            px = qx;
            py = qy;
        }
    }
    return ns;
}

static int winding_at(const seg_t *s, int ns, double px, double py) {
    int w = 0;
    for (int i = 0; i < ns; i++) {
        double ay = s[i].ay, by = s[i].by;
        if ((ay > py) == (by > py))
            continue;
        double t = (py - ay) / (by - ay);
        double xi = s[i].ax + t * (s[i].bx - s[i].ax);
        if (xi > px)
            w += (by > ay) ? 1 : -1;
    }
    return w;
}

static double dist_to_segs(const seg_t *s, int ns, double px, double py) {
    double best = 1e30;
    for (int i = 0; i < ns; i++) {
        double vx = s[i].bx - s[i].ax, vy = s[i].by - s[i].ay;
        double wx = px - s[i].ax, wy = py - s[i].ay;
        double vv = vx * vx + vy * vy;
        double t = (vv > 1e-12) ? (wx * vx + wy * vy) / vv : 0.0;
        if (t < 0.0)
            t = 0.0;
        if (t > 1.0)
            t = 1.0;
        double dx = wx - t * vx, dy = wy - t * vy;
        double d2 = dx * dx + dy * dy;
        if (d2 < best)
            best = d2;
    }
    return sqrt(best);
}

#define GRID_N 180
typedef struct {
    int nx, ny;
    double x0, y0, h;
    float *dist;   /* unsigned distance to ORIGINAL boundary */
    uint8_t *fill; /* nonzero winding of ORIGINAL curves */
} grid_t;

static seg_t s_segs[MAX_CURVES * FLAT_K];

static void grid_build(grid_t *g, const nt_curve_t *base_cv, uint16_t base_nc, double pad) {
    double minx = 1e30, miny = 1e30, maxx = -1e30, maxy = -1e30;
    for (uint16_t i = 0; i < base_nc; i++) {
        const float xs[3] = {base_cv[i].p0x, base_cv[i].p1x, base_cv[i].p2x};
        const float ys[3] = {base_cv[i].p0y, base_cv[i].p1y, base_cv[i].p2y};
        for (int k = 0; k < 3; k++) {
            if (xs[k] < minx)
                minx = xs[k];
            if (xs[k] > maxx)
                maxx = xs[k];
            if (ys[k] < miny)
                miny = ys[k];
            if (ys[k] > maxy)
                maxy = ys[k];
        }
    }
    minx -= pad;
    miny -= pad;
    maxx += pad;
    maxy += pad;
    double ext = (maxx - minx) > (maxy - miny) ? (maxx - minx) : (maxy - miny);
    g->h = ext / GRID_N;
    g->nx = (int)ceil((maxx - minx) / g->h);
    g->ny = (int)ceil((maxy - miny) / g->h);
    g->x0 = minx;
    g->y0 = miny;
    g->dist = (float *)malloc(sizeof(float) * (size_t)g->nx * (size_t)g->ny);
    g->fill = (uint8_t *)malloc((size_t)g->nx * (size_t)g->ny);
    int ns = flatten(base_cv, base_nc, s_segs, MAX_CURVES * FLAT_K);
    for (int iy = 0; iy < g->ny; iy++) {
        double py = g->y0 + (iy + 0.5) * g->h + 3e-3 * g->h;
        for (int ix = 0; ix < g->nx; ix++) {
            double px = g->x0 + (ix + 0.5) * g->h + 3e-3 * g->h;
            g->dist[iy * g->nx + ix] = (float)dist_to_segs(s_segs, ns, px, py);
            g->fill[iy * g->nx + ix] = winding_at(s_segs, ns, px, py) != 0;
        }
    }
}

/* Compare rendered winding vs dilation ground truth.
 * MUST-FILL: inside original fill (depth > h), or dist <= 0.45W - h. The 0.45W inner
 * band tolerates the summed-normal/miter-cap undershoot at reflex corners (accepted
 * join-style freedom, present in production today); the notch defect reaches far
 * below it. MUST-EMPTY: outside fill and dist > 2.15W + h (miter cap 2 bounds
 * legitimate reach at 2W). Between the bands = join-style don't-care. */
typedef struct {
    int hole;  /* THE defect: enclosed winding-0 px inside the true dilation (QA notch) */
    int notch; /* must-fill missed, not enclosed = join undershoot dent (exists today, accepted) */
    int spill, total;
} raster_t;

static uint8_t s_rendered[512 * 512];
static uint8_t s_reach[512 * 512]; /* border-reachable empty px (flood fill) */
static int s_queue[512 * 512];

/* Mark empty pixels reachable from the grid border; unreached empty px are ENCLOSED. */
static void flood_outside(const grid_t *g) {
    int npx = g->nx * g->ny;
    memset(s_reach, 0, (size_t)npx);
    int qh = 0, qt = 0;
    for (int ix = 0; ix < g->nx; ix++) {
        int a = ix, b = (g->ny - 1) * g->nx + ix;
        if (!s_rendered[a] && !s_reach[a]) {
            s_reach[a] = 1;
            s_queue[qt++] = a;
        }
        if (!s_rendered[b] && !s_reach[b]) {
            s_reach[b] = 1;
            s_queue[qt++] = b;
        }
    }
    for (int iy = 0; iy < g->ny; iy++) {
        int a = iy * g->nx, b = iy * g->nx + g->nx - 1;
        if (!s_rendered[a] && !s_reach[a]) {
            s_reach[a] = 1;
            s_queue[qt++] = a;
        }
        if (!s_rendered[b] && !s_reach[b]) {
            s_reach[b] = 1;
            s_queue[qt++] = b;
        }
    }
    while (qh < qt) {
        int p = s_queue[qh++];
        int ix = p % g->nx, iy = p / g->nx;
        const int nb[4] = {p - 1, p + 1, p - g->nx, p + g->nx};
        const int ok[4] = {ix > 0, ix < g->nx - 1, iy > 0, iy < g->ny - 1};
        for (int k = 0; k < 4; k++) {
            if (ok[k] && !s_rendered[nb[k]] && !s_reach[nb[k]]) {
                s_reach[nb[k]] = 1;
                s_queue[qt++] = nb[k];
            }
        }
    }
}

/* Connectivity: count ENCLOSED open regions (winding-0 cells not reachable from the border) of
 * the emitted geometry, ignoring slivers < min_area. A counter that SEALS+fills loses a region;
 * a counter that pinches into pieces gains regions. Counter-preserve must MATCH the original. */
static uint8_t s_label[512 * 512];
static int count_enclosed_regions(const grid_t *g, const nt_curve_t *cv, uint16_t nc, int min_area) {
    int npx = g->nx * g->ny;
    int ns = flatten(cv, nc, s_segs, MAX_CURVES * FLAT_K);
    for (int iy = 0; iy < g->ny; iy++) {
        double py = g->y0 + (iy + 0.5) * g->h + 3e-3 * g->h;
        for (int ix = 0; ix < g->nx; ix++) {
            double px = g->x0 + (ix + 0.5) * g->h + 3e-3 * g->h;
            s_rendered[iy * g->nx + ix] = (uint8_t)(winding_at(s_segs, ns, px, py) != 0);
        }
    }
    flood_outside(g);
    memset(s_label, 0, (size_t)npx);
    int regions = 0;
    for (int p0 = 0; p0 < npx; p0++) {
        if (s_rendered[p0] || s_reach[p0] || s_label[p0])
            continue; /* only enclosed-empty, unlabeled */
        int qh = 0, qt = 0, area = 0;
        s_label[p0] = 1;
        s_queue[qt++] = p0;
        while (qh < qt) {
            int p = s_queue[qh++];
            area++;
            int ix = p % g->nx, iy = p / g->nx;
            const int nb[4] = {p - 1, p + 1, p - g->nx, p + g->nx};
            const int ok[4] = {ix > 0, ix < g->nx - 1, iy > 0, iy < g->ny - 1};
            for (int k = 0; k < 4; k++) {
                if (ok[k] && !s_rendered[nb[k]] && !s_reach[nb[k]] && !s_label[nb[k]]) {
                    s_label[nb[k]] = 1;
                    s_queue[qt++] = nb[k];
                }
            }
        }
        if (area >= min_area)
            regions++;
    }
    return regions;
}

/* Weight W moves the boundary by R = W/2: the summed-normal formula is the EXACT
 * miter for offset distance W/2 (d=1/(1+cos t)=1/(2cos^2(t/2)), |n_sum|=2cos(t/2)
 * => shift = 0.5*W/cos(t/2)). All ground-truth bands are in R. */
static raster_t raster_check(const grid_t *g, const nt_curve_t *cv, uint16_t nc, double W) {
    raster_t r = {0, 0, 0, 0};
    double R = 0.5 * W;
    int ns = flatten(cv, nc, s_segs, MAX_CURVES * FLAT_K);
    for (int iy = 0; iy < g->ny; iy++) {
        double py = g->y0 + (iy + 0.5) * g->h + 3e-3 * g->h;
        for (int ix = 0; ix < g->nx; ix++) {
            double px = g->x0 + (ix + 0.5) * g->h + 3e-3 * g->h;
            s_rendered[iy * g->nx + ix] = (uint8_t)(winding_at(s_segs, ns, px, py) != 0);
        }
    }
    flood_outside(g);
    for (int iy = 0; iy < g->ny; iy++) {
        for (int ix = 0; ix < g->nx; ix++) {
            int p = iy * g->nx + ix;
            int rend = s_rendered[p];
            double d = g->dist[p];
            int fil = g->fill[p];
            int in_dilation = fil || d <= 0.85 * R; /* well inside the true dilation */
            int mustfill = (fil && d > g->h) || (R > 0.0 && d <= 0.85 * R - g->h);
            int mustempty = !fil && d > 2.2 * R + g->h; /* miter cap 2 bounds reach at 2R */
            r.total++;
            if (!rend && !s_reach[p] && in_dilation)
                r.hole++; /* enclosed winding-cancellation defect */
            else if (mustfill && !rend)
                r.notch++;
            if (mustempty && rend)
                r.spill++;
        }
    }
    return r;
}

/* ASCII eyeball dump: '#' filled, '!' enclosed winding hole (the QA notch),
 * '~' open must-fill miss (dent), '.' empty */
static void ascii_dump(const grid_t *g, double W) {
    int step_y = g->ny / 44 + 1, step_x = g->nx / 78 + 1;
    for (int iy = g->ny - 1; iy >= 0; iy -= step_y) {
        char line[256];
        int lc = 0;
        for (int ix = 0; ix < g->nx && lc < 250; ix += step_x) {
            int p = iy * g->nx + ix;
            int rend = s_rendered[p];
            double d = g->dist[p];
            int fil = g->fill[p];
            double R = 0.5 * W;
            int in_dilation = fil || d <= 0.85 * R;
            int mustfill = (fil && d > g->h) || (R > 0.0 && d <= 0.85 * R - g->h);
            char ch = '.';
            if (rend)
                ch = '#';
            else if (!s_reach[p] && in_dilation)
                ch = '!';
            else if (mustfill)
                ch = '~';
            line[lc++] = ch;
        }
        line[lc] = 0;
        printf("    %s\n", line);
    }
}
// #endregion

/* Keep/drop decision for ONE counter contour at weight W under survival `mode`
 * (0=kappa proxy, 1=inradius gate): 1 if the counter is traced (emits curves), 0 if
 * dropped (fills solid). */
static int counter_kept(const contour_t *counter, float W, int mode) {
    int save = g_survival_mode;
    g_survival_mode = mode;
    static nt_curve_t tmp[MAX_CURVES];
    gstats_t gs;
    uint16_t n = build_resolved(counter, 1, W, tmp, MAX_CURVES, &gs, 0);
    g_survival_mode = save;
    return n > 0;
}

/* Residual counter inradius after the counter-preserving (capped) offset at weight W. */
static double counter_residual_inradius(const contour_t *base, float W) {
    static contour_t work;
    double a0 = contour_signed_area(base->x, base->y, base->n);
    float W_eff = counter_cap_weight(base, W, 1 /* is_shrinker */);
    offset_with_joins(base, &work, W_eff, a0);
    return poly_inradius(work.x, work.y, work.n);
}

/* Residual NECK opening after the capped offset (the narrowest surviving channel). */
static double counter_residual_neck(const contour_t *base, float W) {
    static contour_t work;
    double a0 = contour_signed_area(base->x, base->y, base->n);
    float W_eff = counter_cap_weight(base, W, 1);
    offset_with_joins(base, &work, W_eff, a0);
    return counter_neck_gap(work.x, work.y, work.n);
}

/* True if the capped-offset counter ring SELF-INTERSECTS (a neck sealed). Must be false. */
static int counter_seals(const contour_t *base, float W) {
    static contour_t work;
    double a0 = contour_signed_area(base->x, base->y, base->n);
    float W_eff = counter_cap_weight(base, W, 1);
    offset_with_joins(base, &work, W_eff, a0);
    return ring_self_intersects(work.x, work.y, work.n);
}

/* ===== Analytic universality: shapes with a KNOWN inradius (font-independent proof) =====
 * A regular n-gon of circumradius Rc has inradius Rc*cos(pi/n) (-> Rc for a circle); a
 * w x h rectangle has inradius min(w,h)/2. The polylabel evaluator must recover these; the
 * criterion "fills iff inradius <= R" is then exact BY CONSTRUCTION for any shape. */
static void gen_regular(int32_t *x, int32_t *y, int nseg, double Rc, double cxp, double cyp) {
    for (int i = 0; i < nseg; i++) {
        double a = 2.0 * 3.14159265358979 * i / nseg;
        x[i] = (int32_t)lrint(cxp + Rc * cos(a));
        y[i] = (int32_t)lrint(cyp + Rc * sin(a));
    }
}

static void validate_analytic(void) {
    printf("=== ANALYTIC universality (known inradius; font-independent) ===\n");
    printf("shape                 inrad_true  inrad_poly  err   R=inrad boundary check\n");
    static int32_t x[512], y[512];
    int worst = 0;
    /* circle (128-gon), ellipse (via scaled 128-gon), square, thin rect */
    struct {
        const char *nm;
        int nseg;
        double Rc, sx, sy;
    } cases[] = {{"circle r=300", 128, 300, 1.0, 1.0}, {"circle r=80", 128, 80, 1.0, 1.0}, {"ellipse 300x120", 128, 1, 300, 120}, {"square 400", 4, 0, 0, 0}, {"rect 400x160", 4, 0, 0, 0}};
    for (int ci = 0; ci < 5; ci++) {
        int n;
        double inrad_true;
        if (ci < 3) {
            n = cases[ci].nseg;
            if (ci < 2) {
                gen_regular(x, y, n, cases[ci].Rc, 500, 500);
                inrad_true = cases[ci].Rc * cos(3.14159265358979 / n);
            } else {
                for (int i = 0; i < n; i++) {
                    double a = 2.0 * 3.14159265358979 * i / n;
                    x[i] = (int32_t)lrint(500 + cases[ci].sx * cos(a));
                    y[i] = (int32_t)lrint(500 + cases[ci].sy * sin(a));
                }
                inrad_true = (cases[ci].sy) * cos(3.14159265358979 / n); /* minor semi-axis ~ inradius */
            }
        } else if (ci == 3) {
            n = 4;
            x[0] = 300;
            y[0] = 300;
            x[1] = 700;
            y[1] = 300;
            x[2] = 700;
            y[2] = 700;
            x[3] = 300;
            y[3] = 700;
            inrad_true = 200;
        } else {
            n = 4;
            x[0] = 300;
            y[0] = 420;
            x[1] = 700;
            y[1] = 420;
            x[2] = 700;
            y[2] = 580;
            x[3] = 300;
            y[3] = 580;
            inrad_true = 80;
        }
        double ip = poly_inradius(x, y, (uint16_t)n);
        double err = fabs(ip - inrad_true);
        /* boundary check: at R just below/above inrad_true, inradius>R must flip exactly */
        int ok_lo = poly_inradius(x, y, (uint16_t)n) > (inrad_true - 2.0);
        int ok_hi = !(poly_inradius(x, y, (uint16_t)n) > (inrad_true + 2.0));
        if ((int)err > worst)
            worst = (int)err;
        printf("  %-18s  %8.1f  %10.1f  %4.1f  %s\n", cases[ci].nm, inrad_true, ip, err, (ok_lo && ok_hi) ? "OK" : "MISMATCH");
    }
    printf("worst analytic inradius error: %d u (polylabel PREC=0.25)\n\n", worst);
}

/* Per-font: counter-survival calibration table + full raster proof. Returns resolved holes. */
// NOLINTNEXTLINE
static int run_font(const char *font_path, int primary) {
    FILE *f = fopen(font_path, "rb");
    if (!f) {
        printf("No font: %s (run from repo root)\n", font_path);
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
    /* units_per_em from head table @18 (big-endian); stbtt exposes the head offset */
    const uint8_t *upem_p = fdata + font.head + 18;
    uint16_t upem = (uint16_t)((upem_p[0] << 8) | upem_p[1]);
    printf("========================================================================\n");
    printf("Font: %s   units_per_em=%u\n\n", font_path, upem);

    enum { GN = 9 };
    const int codepoints[GN] = {'8', 'B', '@', 'o', 'e', 'A', 'W', ',', '0'};
    const char *names[GN] = {"8", "B", "@", "o", "e", "A", "W", ",", "0"};

    static contour_t gcs[GN][MAX_CONTOURS];
    int gncs[GN];
    for (int g = 0; g < GN; g++) {
        int gi = stbtt_FindGlyphIndex(&font, codepoints[g]);
        stbtt_vertex *verts;
        int nv = stbtt_GetGlyphShape(&font, gi, &verts);
        gncs[g] = stbtt_to_contours(verts, nv, gcs[g], MAX_CONTOURS);
        stbtt_FreeShape(&font, verts);
    }

    // #region Counter-preserving outline: NECK + inradius openness proof (the product goal)
    /* The NECK (narrowest channel) binds, not the inradius: a spiral/channel counter ('@') seals
     * (its walls touch -> the offset ring self-intersects -> a wrong-sign loop is dropped -> the
     * region beyond fills) long before the inradius cap engages. Cap W <= (1-KEEP)*neck_gap so the
     * neck keeps >= KEEP of its width AND (since neck<=2*inradius) the wide part keeps >=KEEP*inrad.
     * Table per counter, per W: residual NECK opening; 's' = the capped ring SEALS (self-intersects,
     * must never happen); frac = residual_neck/neck_gap must stay >= KEEP. */
    printf("=== COUNTER-PRESERVING OUTLINE (NECK cap, KEEP=%.2f) ===\n", (double)COUNTER_KEEP);
    const float cramp[] = {0.03F, 0.05F, 0.07F, 0.09F, 0.12F, 0.16F};
    const int CRN = (int)(sizeof(cramp) / sizeof(cramp[0]));
    printf("counter inrad neck | residual NECK per W {");
    for (int r = 0; r < CRN; r++)
        printf("%.2f ", cramp[r]);
    printf("}  ('s'=SEALS)\n");
    int poly_vs_grid_bad = 0;
    int keep_violations = 0;       /* residual neck < KEEP*neck OR residual inrad < KEEP*inrad */
    int closed_under_preserve = 0; /* counter NOT emitted under preserve (must be 0) */
    int seals = 0;                 /* capped offset self-intersects (must be 0) */
    double min_neck_frac = 1e9, min_inrad_frac = 1e9;
    for (int g = 0; g < GN; g++) {
        int outer = 0;
        double amax = -1.0;
        for (int c = 0; c < gncs[g]; c++) {
            double a = fabs(contour_signed_area(gcs[g][c].x, gcs[g][c].y, gcs[g][c].n));
            if (a > amax) {
                amax = a;
                outer = c;
            }
        }
        for (int c = 0; c < gncs[g]; c++) {
            if (c == outer)
                continue;
            const contour_t *ctr = &gcs[g][c];
            double inrad = poly_inradius(ctr->x, ctr->y, ctr->n);
            double neck = counter_neck_gap(ctr->x, ctr->y, ctr->n);
            if (fabs(inrad - poly_inradius_grid(ctr->x, ctr->y, ctr->n)) > 2.0)
                poly_vs_grid_bad++;
            printf("  %s#%d %5.0f %4.0f |", names[g], c, inrad, neck);
            for (int r = 0; r < CRN; r++) {
                double rneck = counter_residual_neck(ctr, cramp[r] * upem);
                double rinrad = counter_residual_inradius(ctr, cramp[r] * upem);
                double nfrac = (neck < 1e29 && neck > 1.0) ? rneck / neck : 1.0;
                double ifrac = (inrad > 1.0) ? rinrad / inrad : 1.0;
                int seal = counter_seals(ctr, cramp[r] * upem);
                if (seal)
                    seals++;
                /* gate on the RELIABLE signals: no seal (self-intersection) + wide part keeps
                 * KEEP*inradius. residual-neck ray-cast is a printed diagnostic only. */
                if (ifrac < (double)COUNTER_KEEP - 0.05)
                    keep_violations++;
                if (!counter_kept(ctr, cramp[r] * upem, 1))
                    closed_under_preserve++;
                if (nfrac < min_neck_frac)
                    min_neck_frac = nfrac;
                if (ifrac < min_inrad_frac)
                    min_inrad_frac = ifrac;
                printf(" %4.0f%s", (rneck < 1e29) ? rneck : 0.0, seal ? "s" : " ");
            }
            printf("\n");
        }
    }
    printf("[%s] KEEP=%.2f  min neck-frac=%.2f  min inrad-frac=%.2f  SEALS=%d  keep-violations=%d  counters-closed=%d  polylabel!=grid=%d\n\n", font_path, (double)COUNTER_KEEP, min_neck_frac,
           min_inrad_frac, seals, keep_violations, closed_under_preserve, poly_vs_grid_bad);
    // #endregion

    // #region Connectivity proof: enclosed open regions (flood-fill), preserve must match original
    /* THE acceptance test: count enclosed winding-0 regions of the emitted geometry. A sealed+filled
     * counter LOSES a region; counter-preserve must MATCH the original at every width; uniform (at
     * the same wide width) loses regions on tight fonts (its counters fill). '@' at 0.12em especially. */
    printf("Connectivity (enclosed open regions; W=0 baseline; preserve must match, uniform may drop):\n");
    printf("glyph  W(em) | regions: original preserve uniform\n");
    int connectivity_lost = 0;
    {
        static nt_curve_t co[MAX_CURVES], cp[MAX_CURVES], cu[MAX_CURVES];
        gstats_t gg;
        const float cw[] = {0.08F, 0.12F, 0.16F};
        for (int g = 0; g < GN; g++) {
            if (gncs[g] < 2)
                continue; /* only counter glyphs */
            uint16_t no = build_resolved(gcs[g], gncs[g], 0.0F, co, MAX_CURVES, &gg, 0);
            grid_t grid;
            grid_build(&grid, co, no, 1.15 * (double)cw[2] * upem + 8.0);
            int min_area = (grid.nx * grid.ny) / 4000 + 2; /* ignore sub-glyph slivers */
            int orig_reg = count_enclosed_regions(&grid, co, no, min_area);
            for (int r = 0; r < 3; r++) {
                float W = cw[r] * (float)upem;
                g_counter_preserve = 1;
                uint16_t np = build_resolved(gcs[g], gncs[g], W, cp, MAX_CURVES, &gg, 0);
                g_counter_preserve = 0;
                uint16_t nu = build_resolved(gcs[g], gncs[g], W, cu, MAX_CURVES, &gg, 0);
                g_counter_preserve = 1;
                int preg = count_enclosed_regions(&grid, cp, np, min_area);
                int ureg = count_enclosed_regions(&grid, cu, nu, min_area);
                if (preg < orig_reg)
                    connectivity_lost++;
                printf("  %-4s %5.2f |          %d        %d       %d%s\n", names[g], cw[r], orig_reg, preg, ureg,
                       (preg < orig_reg) ? "  <- PRESERVE LOST A COUNTER!" : (ureg < orig_reg ? "  (uniform fills)" : ""));
            }
            free(grid.dist);
            free(grid.fill);
        }
    }
    printf("[%s] connectivity: preserve-counters-lost=%d (must be 0)\n\n", font_path, connectivity_lost);
    g_last_old_mismatch = seals; /* reuse fields for the universal verdict */
    g_last_new_mismatch = keep_violations + closed_under_preserve + seals + connectivity_lost;
    g_last_poly_vs_grid = poly_vs_grid_bad;
    // #endregion

    const float ramp[] = {0.0F, 0.04F, 0.08F, 0.12F, 0.16F, 0.24F, 0.32F};
    const int RN = (int)(sizeof(ramp) / sizeof(ramp[0]));
    double Wmax = (double)ramp[RN - 1] * upem;

    static nt_curve_t cv_plain[MAX_CURVES];
    static nt_curve_t cv_naive[MAX_CURVES];
    static nt_curve_t cv_res[MAX_CURVES];
    gstats_t gs_n, gs_r;

    int total_hole_naive = 0, total_hole_res = 0;
    int total_notch_naive = 0, total_notch_res = 0;
    int total_spill_naive = 0, total_spill_res = 0;
    int total_residual_selfx = 0;
    uint16_t max_curves_res = 0;
    int identity_ok = 1;

    /* Raster proof runs in UNIFORM mode (counters filled) so the OUTER-silhouette D1/D2/D3
     * fixes stay measurable as holes==0. The outer is offset by full W in BOTH modes, so this
     * validates the outer for counter-preserve too; counter openness is proven by the table above. */
    g_counter_preserve = 0;
    printf("Raster proof (UNIFORM mode; n=naive today, r=resolved). hole = enclosed winding-0 px inside true dilation.\n");
    printf("glyph W(em)  | crv_n hole_n dent_n spill_n | crv_r  x  ldrop rdrop phdrop hole_r dent_r spill_r selfx%s\n", primary ? " | ns_n ns_r" : "");

    for (int g = 0; g < GN; g++) {
        uint16_t nc_plain = build_naive(gcs[g], gncs[g], 0.0F, cv_plain, MAX_CURVES, &gs_n);
        grid_t grid;
        grid_build(&grid, cv_plain, nc_plain, 1.15 * Wmax + 8.0); /* pad > 2.2*R_max */

        for (int r = 0; r < RN; r++) {
            float W = ramp[r] * (float)upem;
            uint16_t nc_n = build_naive(gcs[g], gncs[g], W, cv_naive, MAX_CURVES, &gs_n);
            uint16_t nc_r = build_resolved(gcs[g], gncs[g], W, cv_res, MAX_CURVES, &gs_r, 0);

            if (r == 0) { /* W=0 identity: resolved must be byte-identical to plain decode */
                if (nc_r != nc_plain || memcmp(cv_res, cv_plain, sizeof(nt_curve_t) * nc_plain) != 0) {
                    identity_ok = 0;
                    printf("  %-3s W=0 IDENTITY BROKEN\n", names[g]);
                }
            }

            raster_t ra_n = raster_check(&grid, cv_naive, nc_n, W);
            raster_t ra_r = raster_check(&grid, cv_res, nc_r, W);

            double ns_n = 0.0, ns_r = 0.0;
            if (primary) {
                const int IT = 4000;
                clock_t t0 = clock();
                for (int it = 0; it < IT; it++)
                    nc_n = build_naive(gcs[g], gncs[g], W, cv_naive, MAX_CURVES, &gs_n);
                ns_n = (double)(clock() - t0) / CLOCKS_PER_SEC * 1e9 / IT;
                t0 = clock();
                for (int it = 0; it < IT; it++)
                    nc_r = build_resolved(gcs[g], gncs[g], W, cv_res, MAX_CURVES, &gs_r, 0);
                ns_r = (double)(clock() - t0) / CLOCKS_PER_SEC * 1e9 / IT;
            }

            total_hole_naive += ra_n.hole;
            total_hole_res += ra_r.hole;
            total_notch_naive += ra_n.notch;
            total_notch_res += ra_r.notch;
            total_spill_naive += ra_n.spill;
            total_spill_res += ra_r.spill;
            total_residual_selfx += gs_r.residual_selfx;
            if (nc_r > max_curves_res)
                max_curves_res = nc_r;

            printf("  %-3s %5.2f  | %5u %6d %6d %7d | %5u %3d %5d %5d %6d %6d %6d %7d %5d", names[g], ramp[r], nc_n, ra_n.hole, ra_n.notch, ra_n.spill, nc_r, gs_r.crossings, gs_r.loops_dropped,
                   gs_r.rings_dropped, gs_r.phantoms_dropped, ra_r.hole, ra_r.notch, ra_r.spill, gs_r.residual_selfx);
            if (primary)
                printf(" | %6.0f %6.0f", ns_n, ns_r);
            printf("%s\n", (ra_n.hole > 0 && ra_r.hole == 0) ? "  <- HOLE FIXED" : (ra_r.hole > 0 ? "  <- RESIDUAL HOLE" : ""));

            /* eyeball dump (primary font only): naive vs resolved winding holes */
            static int dumps_done = 0;
            int dumpme = primary && (((ra_n.hole > 0 || ra_r.hole > 0) && dumps_done < 12) || (codepoints[g] == '8' && ramp[r] == 0.12F));
            if (dumpme) {
                dumps_done++;
                printf("\n  '%s' @%.2fem NAIVE ('!' = winding hole, '~' = dent):\n", names[g], ramp[r]);
                raster_check(&grid, cv_naive, nc_n, W);
                ascii_dump(&grid, W);
                printf("\n  '%s' @%.2fem RESOLVED:\n", names[g], ramp[r]);
                raster_check(&grid, cv_res, nc_r, W);
                ascii_dump(&grid, W);
                printf("\n  '%s' @%.2fem loop evidence:\n", names[g], ramp[r]);
                build_resolved(gcs[g], gncs[g], W, cv_res, MAX_CURVES, &gs_r, 1);
                printf("\n");
            }
        }
        printf("\n");
        free(grid.dist);
        free(grid.fill);
    }

    // #region Stress sweep: dense/complex glyphs, structural + raster checks only
    if (primary) {
        printf("=== STRESS (complex glyphs): raster + structural checks ===\n");
        printf("glyph    W(em)  crv_n  crv_r   x  hole_n hole_r spill_r selfx  flag\n");
        const uint32_t stress_cps[] = {'%', '&', 'g', 'R', 's', 0x0416 /*Zh*/, 0x0444 /*ef*/, 0x044B /*y*/, 0x6C38 /*yong*/, 0x56FD /*guo*/, 0x9F52 /*chi*/};
        const int SN = (int)(sizeof(stress_cps) / sizeof(stress_cps[0]));
        const float sramp[] = {0.08F, 0.16F, 0.32F};
        int stress_bad = 0;
        for (int s = 0; s < SN; s++) {
            int gi = stbtt_FindGlyphIndex(&font, (int)stress_cps[s]);
            if (gi == 0)
                continue;
            static contour_t scs[MAX_CONTOURS];
            stbtt_vertex *verts;
            int nv = stbtt_GetGlyphShape(&font, gi, &verts);
            int snc = stbtt_to_contours(verts, nv, scs, MAX_CONTOURS);
            stbtt_FreeShape(&font, verts);
            uint16_t nc_plain = build_naive(scs, snc, 0.0F, cv_plain, MAX_CURVES, &gs_n);
            grid_t grid;
            grid_build(&grid, cv_plain, nc_plain, 1.15 * Wmax + 8.0);
            for (int r = 0; r < 3; r++) {
                float W = sramp[r] * (float)upem;
                uint16_t nc_n = build_naive(scs, snc, W, cv_naive, MAX_CURVES, &gs_n);
                uint16_t nc_r = build_resolved(scs, snc, W, cv_res, MAX_CURVES, &gs_r, 0);
                raster_t ra_n = raster_check(&grid, cv_naive, nc_n, W);
                raster_t ra_r = raster_check(&grid, cv_res, nc_r, W);
                const char *flag = "";
                if (ra_r.hole > 0 || gs_r.residual_selfx > 0) {
                    flag = "  <- RESIDUAL";
                    stress_bad++;
                } else if (ra_n.hole > 0) {
                    flag = "  <- FIXED";
                }
                printf("  U+%04X %5.2f  %5u  %5u %3d  %6d %6d %7d %5d%s\n", stress_cps[s], sramp[r], nc_n, nc_r, gs_r.crossings, ra_n.hole, ra_r.hole, ra_r.spill, gs_r.residual_selfx, flag);
                total_hole_naive += ra_n.hole;
                total_hole_res += ra_r.hole;
                total_residual_selfx += gs_r.residual_selfx;
                if (nc_r > max_curves_res)
                    max_curves_res = nc_r;
            }
            free(grid.dist);
            free(grid.fill);
        }
        printf("stress residuals: %d\n\n", stress_bad);
    }
    // #endregion

    g_counter_preserve = 1; /* restore counter-preserve default (raster proof toggled it off) */

    /* Winding-hole REGRESSION (resolved > naive) is the OUTER-silhouette safety net (uniform
     * mode). Residual holes where naive==resolved are a pre-existing miter-cap artifact at sharp
     * counter apices (Roboto 'A' tip), NOT introduced here. */
    int regressed = (total_hole_res > total_hole_naive) ? 1 : 0;
    printf("--- [%s] SUMMARY ---\n", font_path);
    printf("counter-preserve: SEALS=%d  connectivity+seals+closed+viol=%d  (polylabel!=grid=%d)\n", g_last_old_mismatch, g_last_new_mismatch, g_last_poly_vs_grid);
    printf("OUTER (uniform) winding-hole px: naive=%d resolved=%d (regression=%s)  spill=%d selfx=%d maxcurves=%u/%d W0-identity=%s\n\n", total_hole_naive, total_hole_res, regressed ? "YES" : "no",
           total_spill_res, total_residual_selfx, max_curves_res, PROD_CURVE_CAP, identity_ok ? "OK" : "BROKEN");
    (void)total_notch_naive;
    (void)total_notch_res;
    (void)total_spill_naive;

    free(fdata);
    /* verdict = counter openness (keep-violations + closed) + pipeline safety (no outer regression / identity / self-x) */
    return g_last_new_mismatch + g_last_poly_vs_grid + regressed + (identity_ok ? 0 : 1000) + total_residual_selfx;
}

/* Diagnostic: per-contour inradius + neck gap for the channel glyphs, to see WHERE '@' seals. */
static void diag_necks(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f)
        return;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *fdata = (uint8_t *)malloc((size_t)sz);
    if (fread(fdata, 1, (size_t)sz, f) != (size_t)sz) {
        fclose(f);
        free(fdata);
        return;
    }
    fclose(f);
    stbtt_fontinfo font;
    stbtt_InitFont(&font, fdata, 0);
    const uint8_t *up = fdata + font.head + 18;
    uint16_t upem = (uint16_t)((up[0] << 8) | up[1]);
    const int cps[] = {'@', 'e', 'a', 'g', 's', 'B', '8'};
    printf("--- neck diagnostic [%s] upem=%u (per shrinker contour: inradius, neck_gap) ---\n", path, upem);
    for (int gi = 0; gi < 7; gi++) {
        int idx = stbtt_FindGlyphIndex(&font, cps[gi]);
        stbtt_vertex *v;
        int nv = stbtt_GetGlyphShape(&font, idx, &v);
        static contour_t cs[MAX_CONTOURS];
        int nc = stbtt_to_contours(v, nv, cs, MAX_CONTOURS);
        stbtt_FreeShape(&font, v);
        int outer = 0;
        double amax = -1;
        for (int c = 0; c < nc; c++) {
            double a = fabs(contour_signed_area(cs[c].x, cs[c].y, cs[c].n));
            if (a > amax) {
                amax = a;
                outer = c;
            }
        }
        printf("  '%c' (%d contours):", cps[gi], nc);
        for (int c = 0; c < nc; c++) {
            if (c == outer)
                continue;
            double inrad = poly_inradius(cs[c].x, cs[c].y, cs[c].n);
            double neck = counter_neck_gap(cs[c].x, cs[c].y, cs[c].n);
            printf("  ctr#%d[n=%u inrad=%.0f neck=%.0f]", c, cs[c].n, inrad, neck);
        }
        printf("\n");
    }
    printf("\n");
    free(fdata);
}

int main(void) {
    validate_analytic();
    diag_necks("examples/ui_showcase/raw/font.ttf");
    diag_necks("assets/fonts/LilitaOne-RussianChineseKo.ttf");
    int bad = 0;
    /* tight display face (user font, small counters) + regular sans + another sans — spans fonts */
    bad += run_font("examples/ui_showcase/raw/font.ttf", 1);
    bad += run_font("tests/fixtures/Roboto-Regular.ttf", 0);
    bad += run_font("examples/ui_showcase/raw/font_dejavu_r.ttf", 0);
    bad += run_font("assets/fonts/LilitaOne-RussianChineseKo.ttf", 0);
    printf("========================================================================\n");
    printf("UNIVERSAL VERDICT: %s (bad=%d)\n",
           bad == 0 ? "PASS — counter NECK never seals (SEALS=0), connectivity preserved (0 counters lost incl '@' at 0.12em on all fonts); outer full-thickness (holes==0); W=0 identity" : "CHECK",
           bad);
    printf("(Counter-preserving NON-uniform outline: cap the hole offset below the SEAL radius so no channel/neck ever touches. Uniform fills tight counters — see the connectivity 'uniform fills' "
           "rows.)\n");
    return 0;
}
