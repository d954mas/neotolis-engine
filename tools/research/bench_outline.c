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
        if (is_shrinker && ring_survival_depth(x, y, n, base->x, base->y, base->n) < SURVIVAL_KAPPA * R) {
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
        if (keep && is_shrinker && ring_survival_depth(lx, ly, (uint16_t)cnt, base->x, base->y, base->n) < SURVIVAL_KAPPA * R) {
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
        int joins = offset_with_joins(&base[i], &work, W, a0);
        /* TT winding (builder/stbtt): outer negative area, hole positive. Bold (W>0)
         * shrinks holes; thin (W<0) shrinks outers. */
        int is_shrinker = (W > 0.0F) ? (a0 > 0.0) : (a0 < 0.0);
        rstats_t rs;
        if (verbose) {
            double a1 = contour_signed_area(work.x, work.y, work.n);
            printf("    contour %d: n=%u->%u area %+.0f -> %+.0f, joins=%d (R=%.0f)%s:\n", i, base[i].n, work.n, a0, a1, joins, 0.5F * (double)fabsf(W), is_shrinker ? " [shrinker]" : "");
        }
        int nr = resolve_ring(work.x, work.y, work.on, work.n, a0, &base[i], is_shrinker, 0.5 * (double)fabsf(W), rings, MAX_LOOPS, &rs, verbose);
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

int main(void) {
    const char *font_path = "assets/fonts/LilitaOne-RussianChineseKo.ttf";
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
    printf("Font: %s   units_per_em=%u\n", font_path, upem);
    printf("Algorithm: offset_points (unchanged) -> uncross + signed-loop filter -> convert\n\n");

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

    /* weight key W = 2R; renderer folds weight+outline into one key (nt_text_renderer.c:592),
     * so real-world keys reach 0.2-0.3em -> ramp extends past the task's 0.16em */
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

    printf("Per glyph/W:  n=naive (production today), r=resolved (spike).\n");
    printf("hole = enclosed winding-cancellation px (THE QA notch); dent = open must-fill miss (join undershoot, exists today)\n");
    printf("glyph W(em)  | crv_n hole_n dent_n spill_n | crv_r  x  ldrop rdrop phdrop hole_r dent_r spill_r selfx | ns_n     ns_r\n");

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

            const int IT = 4000;
            clock_t t0 = clock();
            for (int it = 0; it < IT; it++)
                nc_n = build_naive(gcs[g], gncs[g], W, cv_naive, MAX_CURVES, &gs_n);
            double ns_n = (double)(clock() - t0) / CLOCKS_PER_SEC * 1e9 / IT;
            t0 = clock();
            for (int it = 0; it < IT; it++)
                nc_r = build_resolved(gcs[g], gncs[g], W, cv_res, MAX_CURVES, &gs_r, 0);
            double ns_r = (double)(clock() - t0) / CLOCKS_PER_SEC * 1e9 / IT;

            total_hole_naive += ra_n.hole;
            total_hole_res += ra_r.hole;
            total_notch_naive += ra_n.notch;
            total_notch_res += ra_r.notch;
            total_spill_naive += ra_n.spill;
            total_spill_res += ra_r.spill;
            total_residual_selfx += gs_r.residual_selfx;
            if (nc_r > max_curves_res)
                max_curves_res = nc_r;

            printf("  %-3s %5.2f  | %5u %6d %6d %7d | %5u %3d %5d %5d %6d %6d %6d %7d %5d | %8.0f %8.0f%s\n", names[g], ramp[r], nc_n, ra_n.hole, ra_n.notch, ra_n.spill, nc_r, gs_r.crossings,
                   gs_r.loops_dropped, gs_r.rings_dropped, gs_r.phantoms_dropped, ra_r.hole, ra_r.notch, ra_r.spill, gs_r.residual_selfx, ns_n, ns_r,
                   (ra_n.hole > 0 && ra_r.hole == 0) ? "  <- HOLE FIXED" : (ra_r.hole > 0 ? "  <- RESIDUAL HOLE" : ""));

            /* eyeball dump: whenever the QA glyphs show a naive winding hole (+ '8' at the
             * QA reference 0.12em), naive vs resolved side by side */
            static int dumps_done = 0;
            int dumpme = ((ra_n.hole > 0 || ra_r.hole > 0) && dumps_done < 20) || (codepoints[g] == '8' && ramp[r] == 0.12F);
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
    printf("=== STRESS (complex glyphs): raster + structural checks ===\n");
    printf("glyph    W(em)  crv_n  crv_r   x  hole_n hole_r spill_r selfx  flag\n");
    {
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

    printf("=== SUMMARY ===\n");
    printf("winding-hole px (THE QA notch): naive=%d  resolved=%d\n", total_hole_naive, total_hole_res);
    printf("open dent px (join undershoot): naive=%d  resolved=%d\n", total_notch_naive, total_notch_res);
    printf("spill px (must-empty hit):      naive=%d  resolved=%d\n", total_spill_naive, total_spill_res);
    printf("residual per-ring self-crossings after resolve: %d\n", total_residual_selfx);
    printf("max curves (resolved) = %u  (production cap NT_FONT_MAX_CURVES_PER_GLYPH = %d)\n", max_curves_res, PROD_CURVE_CAP);
    printf("W=0 identity (resolved == plain decode): %s\n", identity_ok ? "OK (byte-identical)" : "BROKEN");
    printf("verdict: %s\n", (total_hole_res == 0 && identity_ok) ? "PASS (no winding holes after resolution)" : "CHECK RESIDUALS");

    free(fdata);
    return 0;
}
