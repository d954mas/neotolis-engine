/* UI 3D-vs-2D hit-test + compose + memory benchmark.
 *
 * Validates Plan A (3D UI from start) by measuring synthetic cost of mat4
 * paths vs current 2D paths. No engine refactor required — uses standalone
 * mat4 math via cglm; reports ns/op and bytes.
 *
 * Workloads printed as [BENCH] lines to stdout:
 *   - hit_2d:           2D inverse-affine hit-test (current path)
 *   - hit_mat4_inv_trs: mat4 inverse via TRS shortcut (transpose+axis-scale)
 *   - hit_mat4_inv_gen: mat4 general inverse via cglm
 *   - raycast_ortho:    full raycast through inv(ortho) + plane intersect
 *   - raycast_persp:    full raycast through inv(perspective) + plane intersect
 *   - compose_2x3:      2x3 affine multiplication (current compose)
 *   - compose_mat4:     mat4 × mat4 via cglm (proposed compose)
 *   - memory:           tree_baked/hit_baked sizes for max_elements scan
 *   - clip_chain_2d:    nested clip ancestors at depth 1/4/16 (2D path)
 *   - clip_chain_3d:    same nesting (raycast path)
 *
 * Goal: empirical answer to "can we afford Plan A perf-wise?". */

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* clang-format off */
#include "core/nt_assert.h"
#include "math/nt_math.h"
#include "time/nt_time.h"
#include "unity.h"
/* clang-format on */

void setUp(void) {}
void tearDown(void) {}

/* ---- Workload constants ---- */

#define BENCH_WIDGETS 100
#define BENCH_FRAMES 1000
#define BENCH_REPS (BENCH_WIDGETS * BENCH_FRAMES)
#define BENCH_COMPOSE_ELEMS 8192

/* ---- Synthetic widget data ---- */

typedef struct {
    /* 2D affine: a,b,c,d,tx,ty */
    float a, b, c, d, tx, ty;
    /* Bbox: x, y, w, h (Clay layout space) */
    float bx, by, bw, bh;
    /* Pre-built mat4 (column-major, cglm) for 3D path */
    float m[16];
} bench_widget_t;

static bench_widget_t s_widgets[BENCH_WIDGETS];

/* Pre-randomized mouse positions (deterministic seed). */
static float s_mouse_x[BENCH_FRAMES];
static float s_mouse_y[BENCH_FRAMES];

/* Quick LCG so the bench is repeatable. */
static uint32_t s_rng = 0x1A2B3C4DU;
static float rng_float(float lo, float hi) {
    s_rng = (s_rng * 1664525U) + 1013904223U;
    return lo + (((float)(s_rng >> 8) / (float)(1U << 24)) * (hi - lo));
}

static void init_widgets(void) {
    s_rng = 0x1A2B3C4DU;
    for (uint32_t i = 0; i < BENCH_WIDGETS; ++i) {
        bench_widget_t *w = &s_widgets[i];
        /* Mild affine: scale 0.8..1.2, rotation -15..15°, translate over 1000×1000 */
        const float sx = rng_float(0.8F, 1.2F);
        const float sy = rng_float(0.8F, 1.2F);
        const float rot = rng_float(-0.26F, 0.26F);
        const float cs = cosf(rot);
        const float sn = sinf(rot);
        w->a = cs * sx;
        w->b = -sn * sy;
        w->c = sn * sx;
        w->d = cs * sy;
        w->tx = rng_float(0.0F, 900.0F);
        w->ty = rng_float(0.0F, 700.0F);
        w->bx = 0.0F;
        w->by = 0.0F;
        w->bw = rng_float(40.0F, 200.0F);
        w->bh = rng_float(20.0F, 80.0F);

        /* mat4 column-major: encode the same 2D affine + identity Z. */
        w->m[0] = w->a;
        w->m[1] = w->c;
        w->m[2] = 0.0F;
        w->m[3] = 0.0F;
        w->m[4] = w->b;
        w->m[5] = w->d;
        w->m[6] = 0.0F;
        w->m[7] = 0.0F;
        w->m[8] = 0.0F;
        w->m[9] = 0.0F;
        w->m[10] = 1.0F;
        w->m[11] = 0.0F;
        w->m[12] = w->tx;
        w->m[13] = w->ty;
        w->m[14] = 0.0F;
        w->m[15] = 1.0F;
    }
    for (uint32_t f = 0; f < BENCH_FRAMES; ++f) {
        s_mouse_x[f] = rng_float(0.0F, 1000.0F);
        s_mouse_y[f] = rng_float(0.0F, 800.0F);
    }
}

/* Anti-optimization sink so the compiler can't dead-strip the work. */
static volatile uint64_t s_sink;

/* ---- 2D inverse-affine hit-test (current engine path) ---- */

static bool hit_2d(const bench_widget_t *w, float px, float py) {
    const float det = (w->a * w->d) - (w->b * w->c);
    if (det == 0.0F) {
        return false;
    }
    const float inv_det = 1.0F / det;
    const float inv_a = w->d * inv_det;
    const float inv_b = -w->b * inv_det;
    const float inv_c = -w->c * inv_det;
    const float inv_d = w->a * inv_det;
    const float rx = px - w->tx;
    const float ry = py - w->ty;
    const float lx = (inv_a * rx) + (inv_b * ry);
    const float ly = (inv_c * rx) + (inv_d * ry);
    return (lx >= w->bx) && (lx <= w->bx + w->bw) && (ly >= w->by) && (ly <= w->by + w->bh);
}

/* ---- mat4 TRS inverse (fast path for affine-only matrices) ----
 * For TRS: inv(M) = [R^T, -R^T·T/s^2]. ~30 ops. */
static void mat4_inv_trs(const float m[16], float out[16]) {
    /* Read RS columns. */
    const float r00 = m[0];
    const float r01 = m[4];
    const float r02 = m[8];
    const float r10 = m[1];
    const float r11 = m[5];
    const float r12 = m[9];
    const float r20 = m[2];
    const float r21 = m[6];
    const float r22 = m[10];
    const float tx = m[12];
    const float ty = m[13];
    const float tz = m[14];
    /* Column scales². */
    const float s0_sq = (r00 * r00) + (r10 * r10) + (r20 * r20);
    const float s1_sq = (r01 * r01) + (r11 * r11) + (r21 * r21);
    const float s2_sq = (r02 * r02) + (r12 * r12) + (r22 * r22);
    const float inv_s0 = 1.0F / s0_sq;
    const float inv_s1 = 1.0F / s1_sq;
    const float inv_s2 = 1.0F / s2_sq;
    /* Inverse rotation = transpose of unit-rotation columns; here columns aren't
     * unit-length (scaled), so inv_R = transpose / |col|². */
    out[0] = r00 * inv_s0;
    out[1] = r01 * inv_s1;
    out[2] = r02 * inv_s2;
    out[3] = 0.0F;
    out[4] = r10 * inv_s0;
    out[5] = r11 * inv_s1;
    out[6] = r12 * inv_s2;
    out[7] = 0.0F;
    out[8] = r20 * inv_s0;
    out[9] = r21 * inv_s1;
    out[10] = r22 * inv_s2;
    out[11] = 0.0F;
    /* Translate: -inv_R · T */
    out[12] = -((out[0] * tx) + (out[4] * ty) + (out[8] * tz));
    out[13] = -((out[1] * tx) + (out[5] * ty) + (out[9] * tz));
    out[14] = -((out[2] * tx) + (out[6] * ty) + (out[10] * tz));
    out[15] = 1.0F;
}

static bool hit_mat4_inv_trs(const bench_widget_t *w, float px, float py) {
    float inv[16];
    mat4_inv_trs(w->m, inv);
    /* Apply inv to (px, py, 0, 1): local = inv · point. */
    const float lx = (inv[0] * px) + (inv[4] * py) + inv[12];
    const float ly = (inv[1] * px) + (inv[5] * py) + inv[13];
    return (lx >= w->bx) && (lx <= w->bx + w->bw) && (ly >= w->by) && (ly <= w->by + w->bh);
}

/* ---- mat4 general inverse via cglm (~104 ops + 1 div) ---- */

static bool hit_mat4_inv_gen(const bench_widget_t *w, float px, float py) {
    mat4 m_in;
    mat4 inv;
    memcpy(m_in, w->m, 64);
    glm_mat4_inv(m_in, inv);
    const float lx = (inv[0][0] * px) + (inv[1][0] * py) + inv[3][0];
    const float ly = (inv[0][1] * px) + (inv[1][1] * py) + inv[3][1];
    return (lx >= w->bx) && (lx <= w->bx + w->bw) && (ly >= w->by) && (ly <= w->by + w->bh);
}

/* ---- Full raycast: NDC → world via inv(view_proj) → ray-plane intersect ---- */

static bool raycast_widget(const bench_widget_t *w, const float inv_vp[16], float px_ndc, float py_ndc) {
    /* Near + far points in NDC. */
    const float p_near[4] = {px_ndc, py_ndc, -1.0F, 1.0F};
    const float p_far[4] = {px_ndc, py_ndc, 1.0F, 1.0F};
    /* Unproject. */
    float wn[4];
    float wf[4];
    for (int i = 0; i < 4; ++i) {
        wn[i] = (inv_vp[i] * p_near[0]) + (inv_vp[i + 4] * p_near[1]) + (inv_vp[i + 8] * p_near[2]) + (inv_vp[i + 12] * p_near[3]);
        wf[i] = (inv_vp[i] * p_far[0]) + (inv_vp[i + 4] * p_far[1]) + (inv_vp[i + 8] * p_far[2]) + (inv_vp[i + 12] * p_far[3]);
    }
    const float invw_n = 1.0F / wn[3];
    const float invw_f = 1.0F / wf[3];
    const float ox = wn[0] * invw_n;
    const float oy = wn[1] * invw_n;
    const float oz = wn[2] * invw_n;
    const float fx = wf[0] * invw_f;
    const float fy = wf[1] * invw_f;
    const float fz = wf[2] * invw_f;
    const float dx = fx - ox;
    const float dy = fy - oy;
    const float dz = fz - oz;

    /* Widget local plane Z=0; need plane in world: P_world = M · P_local.
     * Plane equation in world space: normal × (X - origin) = 0.
     * For local Z=0, world normal = M's Z column; world origin = M's translation.
     * Solve t such that ray.origin + t·ray.dir hits plane. */
    const float n_x = w->m[8];
    const float n_y = w->m[9];
    const float n_z = w->m[10];
    const float orig_x = w->m[12];
    const float orig_y = w->m[13];
    const float orig_z = w->m[14];
    const float denom = (n_x * dx) + (n_y * dy) + (n_z * dz);
    if (denom == 0.0F) {
        return false;
    }
    const float t = (((orig_x - ox) * n_x) + ((orig_y - oy) * n_y) + ((orig_z - oz) * n_z)) / denom;
    if (t < 0.0F) {
        return false;
    }
    const float hx = ox + (t * dx);
    const float hy = oy + (t * dy);
    const float hz = oz + (t * dz);
    /* Transform hit point into widget local via inverse. */
    float inv[16];
    mat4_inv_trs(w->m, inv);
    const float lx = (inv[0] * hx) + (inv[4] * hy) + (inv[8] * hz) + inv[12];
    const float ly = (inv[1] * hx) + (inv[5] * hy) + (inv[9] * hz) + inv[13];
    return (lx >= w->bx) && (lx <= w->bx + w->bw) && (ly >= w->by) && (ly <= w->by + w->bh);
}

/* ---- 2x3 affine compose (current) ----
 * Replicates engine's compose_transform_level shape. */
static void compose_2x3(const float p[6], const float c[6], float out[6]) {
    out[0] = (p[0] * c[0]) + (p[1] * c[2]);
    out[1] = (p[0] * c[1]) + (p[1] * c[3]);
    out[2] = (p[2] * c[0]) + (p[3] * c[2]);
    out[3] = (p[2] * c[1]) + (p[3] * c[3]);
    out[4] = (p[0] * c[4]) + (p[1] * c[5]) + p[4];
    out[5] = (p[2] * c[4]) + (p[3] * c[5]) + p[5];
}

/* ---- Benchmark scenarios ---- */

static void bench_hit_2d(void) {
    init_widgets();
    uint32_t hits = 0;
    const uint64_t t0 = nt_time_nanos();
    for (uint32_t f = 0; f < BENCH_FRAMES; ++f) {
        const float px = s_mouse_x[f];
        const float py = s_mouse_y[f];
        for (uint32_t i = 0; i < BENCH_WIDGETS; ++i) {
            if (hit_2d(&s_widgets[i], px, py)) {
                hits++;
            }
        }
    }
    const uint64_t t1 = nt_time_nanos();
    s_sink ^= hits;
    const double total_ns = (double)(t1 - t0);
    const double per_op_ns = total_ns / (double)BENCH_REPS;
    (void)printf("[BENCH] hit_2d:           %.2f ns/op (%u hits, %.0f ns total)\n", per_op_ns, hits, total_ns);
}

static void bench_hit_mat4_inv_trs(void) {
    init_widgets();
    uint32_t hits = 0;
    const uint64_t t0 = nt_time_nanos();
    for (uint32_t f = 0; f < BENCH_FRAMES; ++f) {
        const float px = s_mouse_x[f];
        const float py = s_mouse_y[f];
        for (uint32_t i = 0; i < BENCH_WIDGETS; ++i) {
            if (hit_mat4_inv_trs(&s_widgets[i], px, py)) {
                hits++;
            }
        }
    }
    const uint64_t t1 = nt_time_nanos();
    s_sink ^= hits;
    const double total_ns = (double)(t1 - t0);
    const double per_op_ns = total_ns / (double)BENCH_REPS;
    (void)printf("[BENCH] hit_mat4_inv_trs: %.2f ns/op (%u hits, %.0f ns total)\n", per_op_ns, hits, total_ns);
}

static void bench_hit_mat4_inv_gen(void) {
    init_widgets();
    uint32_t hits = 0;
    const uint64_t t0 = nt_time_nanos();
    for (uint32_t f = 0; f < BENCH_FRAMES; ++f) {
        const float px = s_mouse_x[f];
        const float py = s_mouse_y[f];
        for (uint32_t i = 0; i < BENCH_WIDGETS; ++i) {
            if (hit_mat4_inv_gen(&s_widgets[i], px, py)) {
                hits++;
            }
        }
    }
    const uint64_t t1 = nt_time_nanos();
    s_sink ^= hits;
    const double total_ns = (double)(t1 - t0);
    const double per_op_ns = total_ns / (double)BENCH_REPS;
    (void)printf("[BENCH] hit_mat4_inv_gen: %.2f ns/op (%u hits, %.0f ns total)\n", per_op_ns, hits, total_ns);
}

static void bench_raycast_ortho(void) {
    init_widgets();
    /* Ortho view_proj: maps [0..1000, 0..800] to NDC. */
    mat4 vp;
    glm_ortho(0.0F, 1000.0F, 0.0F, 800.0F, -1.0F, 1.0F, vp);
    mat4 inv_vp;
    glm_mat4_inv(vp, inv_vp);
    float inv_vp_flat[16];
    memcpy(inv_vp_flat, inv_vp, 64);

    uint32_t hits = 0;
    const uint64_t t0 = nt_time_nanos();
    for (uint32_t f = 0; f < BENCH_FRAMES; ++f) {
        /* NDC from random screen pixel. */
        const float px_ndc = ((s_mouse_x[f] / 1000.0F) * 2.0F) - 1.0F;
        const float py_ndc = ((s_mouse_y[f] / 800.0F) * 2.0F) - 1.0F;
        for (uint32_t i = 0; i < BENCH_WIDGETS; ++i) {
            if (raycast_widget(&s_widgets[i], inv_vp_flat, px_ndc, py_ndc)) {
                hits++;
            }
        }
    }
    const uint64_t t1 = nt_time_nanos();
    s_sink ^= hits;
    const double total_ns = (double)(t1 - t0);
    const double per_op_ns = total_ns / (double)BENCH_REPS;
    (void)printf("[BENCH] raycast_ortho:    %.2f ns/op (%u hits, %.0f ns total)\n", per_op_ns, hits, total_ns);
}

static void bench_raycast_persp(void) {
    init_widgets();
    /* Perspective camera, FOV 70°, aspect 16:9, near .1 far 100. */
    mat4 vp;
    glm_perspective(glm_rad(70.0F), 16.0F / 9.0F, 0.1F, 100.0F, vp);
    mat4 inv_vp;
    glm_mat4_inv(vp, inv_vp);
    float inv_vp_flat[16];
    memcpy(inv_vp_flat, inv_vp, 64);

    uint32_t hits = 0;
    const uint64_t t0 = nt_time_nanos();
    for (uint32_t f = 0; f < BENCH_FRAMES; ++f) {
        const float px_ndc = ((s_mouse_x[f] / 1000.0F) * 2.0F) - 1.0F;
        const float py_ndc = ((s_mouse_y[f] / 800.0F) * 2.0F) - 1.0F;
        for (uint32_t i = 0; i < BENCH_WIDGETS; ++i) {
            if (raycast_widget(&s_widgets[i], inv_vp_flat, px_ndc, py_ndc)) {
                hits++;
            }
        }
    }
    const uint64_t t1 = nt_time_nanos();
    s_sink ^= hits;
    const double total_ns = (double)(t1 - t0);
    const double per_op_ns = total_ns / (double)BENCH_REPS;
    (void)printf("[BENCH] raycast_persp:    %.2f ns/op (%u hits, %.0f ns total)\n", per_op_ns, hits, total_ns);
}

static float s_compose_parents_2x3[BENCH_COMPOSE_ELEMS][6];
static float s_compose_children_2x3[BENCH_COMPOSE_ELEMS][6];
static float s_compose_results_2x3[BENCH_COMPOSE_ELEMS][6];

static void bench_compose_2x3(void) {
    float (*parents)[6] = s_compose_parents_2x3;
    float (*children)[6] = s_compose_children_2x3;
    float (*results)[6] = s_compose_results_2x3;
    s_rng = 0x77AABBCCU;
    for (uint32_t i = 0; i < BENCH_COMPOSE_ELEMS; ++i) {
        for (uint32_t k = 0; k < 6; ++k) {
            parents[i][k] = rng_float(-1.0F, 1.0F);
            children[i][k] = rng_float(-1.0F, 1.0F);
        }
    }
    const uint64_t t0 = nt_time_nanos();
    for (uint32_t i = 0; i < BENCH_COMPOSE_ELEMS; ++i) {
        compose_2x3(parents[i], children[i], results[i]);
    }
    const uint64_t t1 = nt_time_nanos();
    s_sink ^= (uint64_t)results[0][0];
    const double total_ns = (double)(t1 - t0);
    const double per_op_ns = total_ns / (double)BENCH_COMPOSE_ELEMS;
    (void)printf("[BENCH] compose_2x3:      %.2f ns/op (%d ops, %.0f ns total)\n", per_op_ns, BENCH_COMPOSE_ELEMS, total_ns);
}

static mat4 s_compose_parents_mat4[BENCH_COMPOSE_ELEMS];
static mat4 s_compose_children_mat4[BENCH_COMPOSE_ELEMS];
static mat4 s_compose_results_mat4[BENCH_COMPOSE_ELEMS];

static void bench_compose_mat4(void) {
    mat4 *parents = s_compose_parents_mat4;
    mat4 *children = s_compose_children_mat4;
    mat4 *results = s_compose_results_mat4;
    s_rng = 0x77AABBCCU;
    for (uint32_t i = 0; i < BENCH_COMPOSE_ELEMS; ++i) {
        for (uint32_t r = 0; r < 4; ++r) {
            for (uint32_t c = 0; c < 4; ++c) {
                parents[i][r][c] = rng_float(-1.0F, 1.0F);
                children[i][r][c] = rng_float(-1.0F, 1.0F);
            }
        }
    }
    const uint64_t t0 = nt_time_nanos();
    for (uint32_t i = 0; i < BENCH_COMPOSE_ELEMS; ++i) {
        glm_mat4_mul(parents[i], children[i], results[i]);
    }
    const uint64_t t1 = nt_time_nanos();
    s_sink ^= (uint64_t)results[0][0][0];
    const double total_ns = (double)(t1 - t0);
    const double per_op_ns = total_ns / (double)BENCH_COMPOSE_ELEMS;
    (void)printf("[BENCH] compose_mat4:     %.2f ns/op (%d ops, %.0f ns total)\n", per_op_ns, BENCH_COMPOSE_ELEMS, total_ns);
}

/* ---- Memory delta ---- */

static void bench_memory(void) {
    (void)printf("[BENCH] memory deltas (assuming nt_ui_baked_xform_t: 32B → 80B):\n");
    const size_t scenarios[] = {1024, 4096, 8192, 16384};
    for (size_t i = 0; i < sizeof(scenarios) / sizeof(scenarios[0]); ++i) {
        const size_t n = scenarios[i];
        const size_t current = (size_t)32 * n * 2; /* tree_baked + hit_baked */
        const size_t proposed = (size_t)80 * n * 2;
        const size_t delta = proposed - current;
        (void)printf("[BENCH]   max_elements=%5zu: current=%zu B (%.1f KB), proposed=%zu B (%.1f KB), delta=+%zu B (+%.1f KB)\n", n, current, (double)current / 1024.0, proposed,
                     (double)proposed / 1024.0, delta, (double)delta / 1024.0);
    }
}

/* ---- Clip chain compound (nesting depth) ---- */

static void bench_clip_chain_2d(void) {
    init_widgets();
    /* Simulate: per hit-test, also invert N ancestor 2D affines (clip chain). */
    const uint32_t depths[] = {1, 4, 16};
    for (size_t d = 0; d < sizeof(depths) / sizeof(depths[0]); ++d) {
        const uint32_t depth = depths[d];
        uint32_t hits = 0;
        const uint64_t t0 = nt_time_nanos();
        for (uint32_t f = 0; f < BENCH_FRAMES; ++f) {
            const float px = s_mouse_x[f];
            const float py = s_mouse_y[f];
            for (uint32_t i = 0; i < BENCH_WIDGETS; ++i) {
                /* Leaf check + N ancestors. */
                if (hit_2d(&s_widgets[i], px, py)) {
                    hits++;
                }
                for (uint32_t a = 0; a < depth; ++a) {
                    /* Use neighbouring widget as proxy for ancestor. */
                    (void)hit_2d(&s_widgets[(i + a + 1) % BENCH_WIDGETS], px, py);
                }
            }
        }
        const uint64_t t1 = nt_time_nanos();
        s_sink ^= hits;
        const double total_ns = (double)(t1 - t0);
        const double per_op_ns = total_ns / (double)BENCH_REPS;
        (void)printf("[BENCH] clip_chain_2d depth=%2u: %.2f ns/op (%u hits)\n", depth, per_op_ns, hits);
    }
}

static void bench_clip_chain_3d(void) {
    init_widgets();
    mat4 vp;
    glm_ortho(0.0F, 1000.0F, 0.0F, 800.0F, -1.0F, 1.0F, vp);
    mat4 inv_vp;
    glm_mat4_inv(vp, inv_vp);
    float inv_vp_flat[16];
    memcpy(inv_vp_flat, inv_vp, 64);

    const uint32_t depths[] = {1, 4, 16};
    for (size_t d = 0; d < sizeof(depths) / sizeof(depths[0]); ++d) {
        const uint32_t depth = depths[d];
        uint32_t hits = 0;
        const uint64_t t0 = nt_time_nanos();
        for (uint32_t f = 0; f < BENCH_FRAMES; ++f) {
            const float px_ndc = ((s_mouse_x[f] / 1000.0F) * 2.0F) - 1.0F;
            const float py_ndc = ((s_mouse_y[f] / 800.0F) * 2.0F) - 1.0F;
            for (uint32_t i = 0; i < BENCH_WIDGETS; ++i) {
                if (raycast_widget(&s_widgets[i], inv_vp_flat, px_ndc, py_ndc)) {
                    hits++;
                }
                for (uint32_t a = 0; a < depth; ++a) {
                    (void)raycast_widget(&s_widgets[(i + a + 1) % BENCH_WIDGETS], inv_vp_flat, px_ndc, py_ndc);
                }
            }
        }
        const uint64_t t1 = nt_time_nanos();
        s_sink ^= hits;
        const double total_ns = (double)(t1 - t0);
        const double per_op_ns = total_ns / (double)BENCH_REPS;
        (void)printf("[BENCH] clip_chain_3d depth=%2u: %.2f ns/op (%u hits)\n", depth, per_op_ns, hits);
    }
}

/* ---- Smoke test gates the bench loops are sane (asserts hit counts > 0). ---- */

static void test_bench_smoke(void) {
    init_widgets();
    bool any_hit = false;
    for (uint32_t i = 0; i < BENCH_WIDGETS; ++i) {
        if (hit_2d(&s_widgets[i], 500.0F, 400.0F)) {
            any_hit = true;
            break;
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(any_hit, "bench widgets cover screen center; at least one should hit");
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_bench_smoke);
    (void)printf("\n=== UI 3D bench results (workload: %d widgets × %d frames = %d hit-tests) ===\n", BENCH_WIDGETS, BENCH_FRAMES, BENCH_REPS);
    bench_hit_2d();
    bench_hit_mat4_inv_trs();
    bench_hit_mat4_inv_gen();
    bench_raycast_ortho();
    bench_raycast_persp();
    (void)printf("\n=== Compose ===\n");
    bench_compose_2x3();
    bench_compose_mat4();
    (void)printf("\n=== Memory ===\n");
    bench_memory();
    (void)printf("\n=== Clip chain compound ===\n");
    bench_clip_chain_2d();
    bench_clip_chain_3d();
    (void)printf("\n=== End bench ===\n");
    return UNITY_END();
}
