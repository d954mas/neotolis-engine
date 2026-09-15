/*
 * anim_layout -- pose-storage experiment for the #473 pose ABI.
 *
 * Runs a synthetic sample -> mix -> FK loop over three storages of the same
 * local pose and reports ns per skeleton joint per stage:
 *
 *   1. AoS 40 B  -- nt_anim_trs_t, the shipped ABI, FK through nt_anim_fk.
 *   2. AoS 48 B  -- 16-byte-aligned t/q/s, SIMD-friendly padding, local FK.
 *   3. SoA       -- ten float planes (tx ty tz qx qy qz qw sx sy sz), local FK.
 *
 * The arithmetic is shared between the three (lerp_parts, mix_add/mix_finish,
 * mat34_from_parts + nt_anim_mat34_mul), so only the memory layout differs; a
 * startup cross-check compares the three FK outputs before any measurement.
 *
 * The joint x character x track matrix is the workload named in
 * docs/spec/anim/skeletal-animation.md section 18, which #487 measures on the
 * real kernels and #492 revisits with SIMD.
 *
 * Research tool: plain printf, single translation unit, not an engine target.
 */

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <time.h>
#endif

#include "anim/nt_anim.h"

#define MAX_JOINTS 100U
#define MAX_CHARS 1000U
#define MAX_TRACKS 4U
#define BENCH_REPS 5
/* Three stages at ~16.7 ms each keep a measured (config, layout) near 50 ms. */
#define STAGE_SECONDS 0.0167
#define RIG_SEED 0x9E3779B97F4A7C15ULL

// #region timing

static double get_time_sec(void) {
#ifdef _WIN32
    LARGE_INTEGER freq;
    LARGE_INTEGER count;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&count);
    return (double)count.QuadPart / (double)freq.QuadPart;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + ((double)ts.tv_nsec * 1e-9);
#endif
}
// #endregion

// #region layouts and storage

typedef struct {
    _Alignas(16) float t[4];
    float q[4];
    float s[4];
} pose48_t;

_Static_assert(sizeof(pose48_t) == 48, "padded AoS candidate is 48 bytes");
_Static_assert(_Alignof(pose48_t) == 16, "padded AoS candidate is 16-byte aligned");

/* Ten channel planes. Passed as const: the struct is a borrowed set of plane
 * pointers, the planes themselves are what the kernels write. */
typedef struct {
    float *tx;
    float *ty;
    float *tz;
    float *qx;
    float *qy;
    float *qz;
    float *qw;
    float *sx;
    float *sy;
    float *sz;
} soa_pose_t;

typedef enum { LAYOUT_AOS40 = 0, LAYOUT_AOS48 = 1, LAYOUT_SOA = 2, LAYOUT_COUNT = 3 } layout_t;

typedef struct {
    uint16_t parent[MAX_JOINTS];
    uint16_t subtree_end[MAX_JOINTS];
    uint32_t joint_id[MAX_JOINTS];
    nt_anim_trs_t rest[MAX_JOINTS];
    nt_anim_skeleton_t view;
} rig_t;

typedef struct {
    nt_anim_trs_t *key40; /* 2 * MAX_TRACKS * MAX_JOINTS */
    pose48_t *key48;
    soa_pose_t key_soa;
    nt_anim_trs_t *trk40; /* MAX_TRACKS * MAX_CHARS * MAX_JOINTS */
    pose48_t *trk48;
    soa_pose_t trk_soa;
    nt_anim_trs_t *mix40; /* MAX_CHARS * MAX_JOINTS */
    pose48_t *mix48;
    soa_pose_t mix_soa;
    nt_anim_mat34_t *model[LAYOUT_COUNT]; /* MAX_CHARS * MAX_JOINTS */
    float gain[MAX_TRACKS];
    float phase[MAX_TRACKS];
    float step[MAX_TRACKS];
} bench_t;

static const char *layout_name(layout_t layout) {
    switch (layout) {
    case LAYOUT_AOS40:
        return "AoS 40 B";
    case LAYOUT_AOS48:
        return "AoS 48 B";
    case LAYOUT_SOA:
        return "SoA 10ch";
    default:
        return "?";
    }
}

static void *xmalloc(size_t bytes) {
    void *p = malloc(bytes);
    if (p == NULL) {
        (void)fprintf(stderr, "anim_layout: out of memory (%zu bytes)\n", bytes);
        abort();
    }
    return p;
}

static void soa_alloc(soa_pose_t *s, size_t count) {
    const size_t bytes = count * sizeof(float);
    s->tx = (float *)xmalloc(bytes);
    s->ty = (float *)xmalloc(bytes);
    s->tz = (float *)xmalloc(bytes);
    s->qx = (float *)xmalloc(bytes);
    s->qy = (float *)xmalloc(bytes);
    s->qz = (float *)xmalloc(bytes);
    s->qw = (float *)xmalloc(bytes);
    s->sx = (float *)xmalloc(bytes);
    s->sy = (float *)xmalloc(bytes);
    s->sz = (float *)xmalloc(bytes);
}

static void soa_free(const soa_pose_t *s) {
    free(s->tx);
    free(s->ty);
    free(s->tz);
    free(s->qx);
    free(s->qy);
    free(s->qz);
    free(s->qw);
    free(s->sx);
    free(s->sy);
    free(s->sz);
}

static void bench_alloc(bench_t *b) {
    const size_t keys = (size_t)2U * MAX_TRACKS * MAX_JOINTS;
    const size_t tracks = (size_t)MAX_TRACKS * MAX_CHARS * MAX_JOINTS;
    const size_t poses = (size_t)MAX_CHARS * MAX_JOINTS;

    b->key40 = (nt_anim_trs_t *)xmalloc(keys * sizeof(nt_anim_trs_t));
    b->key48 = (pose48_t *)xmalloc(keys * sizeof(pose48_t));
    soa_alloc(&b->key_soa, keys);
    b->trk40 = (nt_anim_trs_t *)xmalloc(tracks * sizeof(nt_anim_trs_t));
    b->trk48 = (pose48_t *)xmalloc(tracks * sizeof(pose48_t));
    soa_alloc(&b->trk_soa, tracks);
    b->mix40 = (nt_anim_trs_t *)xmalloc(poses * sizeof(nt_anim_trs_t));
    b->mix48 = (pose48_t *)xmalloc(poses * sizeof(pose48_t));
    soa_alloc(&b->mix_soa, poses);
    for (int l = 0; l < LAYOUT_COUNT; ++l) {
        b->model[l] = (nt_anim_mat34_t *)xmalloc(poses * sizeof(nt_anim_mat34_t));
    }
    for (uint32_t t = 0; t < MAX_TRACKS; ++t) {
        b->gain[t] = 1.0F / (float)(t + 1U);
        b->phase[t] = 0.1F * (float)t;
        b->step[t] = 0.013F * (float)(t + 1U);
    }
}

static void bench_free(const bench_t *b) {
    free(b->key40);
    free(b->key48);
    soa_free(&b->key_soa);
    free(b->trk40);
    free(b->trk48);
    soa_free(&b->trk_soa);
    free(b->mix40);
    free(b->mix48);
    soa_free(&b->mix_soa);
    for (int l = 0; l < LAYOUT_COUNT; ++l) {
        free(b->model[l]);
    }
}
// #endregion

// #region deterministic data

static uint32_t lcg_next(uint64_t *s) {
    *s = (*s * 6364136223846793005ULL) + 1442695040888963407ULL;
    return (uint32_t)(*s >> 33U);
}

static float lcg_signed(uint64_t *s) { return ((float)lcg_next(s) / 1073741824.0F) - 1.0F; }

static void random_trs(uint64_t *s, nt_anim_trs_t *out) {
    for (int i = 0; i < 3; ++i) {
        out->t[i] = lcg_signed(s) * 0.5F;
        out->s[i] = 0.75F + (lcg_signed(s) * 0.25F);
    }
    float n2 = 0.0F;
    for (int i = 0; i < 4; ++i) {
        out->q[i] = lcg_signed(s);
        n2 += out->q[i] * out->q[i];
    }
    if (n2 < 1e-6F) {
        out->q[0] = 0.0F;
        out->q[1] = 0.0F;
        out->q[2] = 0.0F;
        out->q[3] = 1.0F;
        return;
    }
    const float inv = 1.0F / sqrtf(n2);
    for (int i = 0; i < 4; ++i) {
        out->q[i] *= inv;
    }
}

static void pose48_store(pose48_t *p, const nt_anim_trs_t *v) {
    for (int i = 0; i < 3; ++i) {
        p->t[i] = v->t[i];
        p->s[i] = v->s[i];
    }
    p->t[3] = 0.0F;
    p->s[3] = 0.0F;
    for (int i = 0; i < 4; ++i) {
        p->q[i] = v->q[i];
    }
}

static void soa_store(const soa_pose_t *s, size_t i, const nt_anim_trs_t *v) {
    s->tx[i] = v->t[0];
    s->ty[i] = v->t[1];
    s->tz[i] = v->t[2];
    s->qx[i] = v->q[0];
    s->qy[i] = v->q[1];
    s->qz[i] = v->q[2];
    s->qw[i] = v->q[3];
    s->sx[i] = v->s[0];
    s->sy[i] = v->s[1];
    s->sz[i] = v->s[2];
}

/* Raw tree: 80 % of joints continue the chain, the rest branch off a random
 * earlier joint. parent[j] < j holds, contiguous subtrees do not -- rig_preorder
 * relabels the result so nt_anim_fk's preorder precondition holds. */
static void rig_gen_parents(uint16_t *raw_parent, uint16_t joint_count, uint64_t *s) {
    raw_parent[0] = NT_ANIM_NO_PARENT;
    for (uint16_t j = 1; j < joint_count; ++j) {
        if ((lcg_next(s) % 100U) < 80U) {
            raw_parent[j] = (uint16_t)(j - 1U);
        } else {
            raw_parent[j] = (uint16_t)(lcg_next(s) % (uint32_t)j);
        }
    }
}

/* CSR child lists in ascending child order (deterministic). */
static void rig_build_children(const uint16_t *raw_parent, uint16_t joint_count, uint16_t *start, uint16_t *list) {
    for (uint16_t j = 0; j <= joint_count; ++j) {
        start[j] = 0U;
    }
    for (uint16_t j = 1; j < joint_count; ++j) {
        start[raw_parent[j] + 1U] = (uint16_t)(start[raw_parent[j] + 1U] + 1U);
    }
    for (uint16_t j = 1; j <= joint_count; ++j) {
        start[j] = (uint16_t)(start[j] + start[j - 1U]);
    }
    uint16_t fill[MAX_JOINTS];
    for (uint16_t j = 0; j < joint_count; ++j) {
        fill[j] = start[j];
    }
    for (uint16_t j = 1; j < joint_count; ++j) {
        const uint16_t p = raw_parent[j];
        list[fill[p]] = j;
        fill[p] = (uint16_t)(fill[p] + 1U);
    }
}

/* Iterative preorder DFS from the single root; writes new_index/order and the
 * subtree_end of each relabelled joint. */
static void rig_preorder(const uint16_t *start, const uint16_t *list, uint16_t *new_index, uint16_t *order, uint16_t *subtree_end) {
    uint16_t stack_node[MAX_JOINTS];
    uint16_t stack_next[MAX_JOINTS];
    uint16_t visited = 0U;
    uint16_t sp = 0U;

    new_index[0] = visited;
    order[visited] = 0U;
    visited = (uint16_t)(visited + 1U);
    stack_node[sp] = 0U;
    stack_next[sp] = start[0];
    sp = (uint16_t)(sp + 1U);

    while (sp > 0U) {
        const uint16_t top = (uint16_t)(sp - 1U);
        const uint16_t node = stack_node[top];
        if (stack_next[top] < start[node + 1U]) {
            const uint16_t child = list[stack_next[top]];
            stack_next[top] = (uint16_t)(stack_next[top] + 1U);
            new_index[child] = visited;
            order[visited] = child;
            visited = (uint16_t)(visited + 1U);
            stack_node[sp] = child;
            stack_next[sp] = start[child];
            sp = (uint16_t)(sp + 1U);
        } else {
            subtree_end[new_index[node]] = visited;
            sp = (uint16_t)(sp - 1U);
        }
    }
}

static void rig_build(rig_t *rig, uint16_t joint_count, uint64_t seed) {
    uint16_t raw_parent[MAX_JOINTS];
    uint16_t start[MAX_JOINTS + 1U];
    uint16_t list[MAX_JOINTS];
    /* Zero-initialized: the relabelling is a DFS the analyzer cannot follow. */
    uint16_t new_index[MAX_JOINTS] = {0};
    uint16_t order[MAX_JOINTS] = {0};
    uint64_t s = seed;

    rig_gen_parents(raw_parent, joint_count, &s);
    rig_build_children(raw_parent, joint_count, start, list);
    rig_preorder(start, list, new_index, order, rig->subtree_end);

    for (uint16_t j = 0; j < joint_count; ++j) {
        const uint16_t raw = order[j];
        rig->parent[j] = (raw_parent[raw] == NT_ANIM_NO_PARENT) ? NT_ANIM_NO_PARENT : new_index[raw_parent[raw]];
        rig->joint_id[j] = (uint32_t)j + 1U;
        random_trs(&s, &rig->rest[j]);
    }
    memset(&rig->view.rig_compat_id, 0, sizeof(rig->view.rig_compat_id));
    rig->view.parent = rig->parent;
    rig->view.subtree_end = rig->subtree_end;
    rig->view.joint_id = rig->joint_id;
    rig->view.rest = rig->rest;
    rig->view.joint_count = joint_count;
}

static void keys_init(const bench_t *b, uint16_t joints, uint16_t tracks, uint64_t seed) {
    uint64_t s = seed;
    const size_t count = (size_t)2U * tracks * joints;
    for (size_t i = 0; i < count; ++i) {
        nt_anim_trs_t v;
        random_trs(&s, &v);
        b->key40[i] = v;
        pose48_store(&b->key48[i], &v);
        soa_store(&b->key_soa, i, &v);
    }
}
// #endregion

// #region shared arithmetic

static float wrap01(float x) { return x - floorf(x); }

/* Two-key sample: lerp on T and S, shortest-path nlerp on Q. */
static void lerp_parts(const float ta[3], const float qa[4], const float sa[3], const float tb[3], const float qb[4], const float sb[3], float alpha, float out_t[3], float out_q[4], float out_s[3]) {
    for (int i = 0; i < 3; ++i) {
        out_t[i] = ta[i] + (alpha * (tb[i] - ta[i]));
        out_s[i] = sa[i] + (alpha * (sb[i] - sa[i]));
    }
    float dot = 0.0F;
    for (int i = 0; i < 4; ++i) {
        dot += qa[i] * qb[i];
    }
    const float sgn = (dot < 0.0F) ? -1.0F : 1.0F;
    float acc[4];
    float n2 = 0.0F;
    for (int i = 0; i < 4; ++i) {
        acc[i] = qa[i] + (alpha * ((sgn * qb[i]) - qa[i]));
        n2 += acc[i] * acc[i];
    }
    const float inv = 1.0F / sqrtf(n2);
    for (int i = 0; i < 4; ++i) {
        out_q[i] = acc[i] * inv;
    }
}

typedef struct {
    float acc_t[3];
    float acc_s[3];
    float acc_q[4];
    float w_sum;
    int count;
} mix_acc_t;

/* Largest-magnitude component positive, tie order x, y, z, w (spec 7.3). */
static void quat_canonicalize(const float q[4], float out[4]) {
    int best = 0;
    float best_abs = fabsf(q[0]);
    for (int i = 1; i < 4; ++i) {
        const float a = fabsf(q[i]);
        if (a > best_abs) {
            best_abs = a;
            best = i;
        }
    }
    const float sgn = (q[best] < 0.0F) ? -1.0F : 1.0F;
    for (int i = 0; i < 4; ++i) {
        out[i] = q[i] * sgn;
    }
}

static void mix_begin(mix_acc_t *m) { memset(m, 0, sizeof(*m)); }

/* Running accumulator per spec 7.3: seed canonicalized, later inputs sign-
 * flipped so dot >= 0, one normalize per joint in mix_finish. */
static void mix_add(mix_acc_t *m, const float t[3], const float q[4], const float s[3], float w) {
    m->w_sum += w;
    for (int i = 0; i < 3; ++i) {
        m->acc_t[i] += w * t[i];
        m->acc_s[i] += w * s[i];
    }
    if (m->count == 0) {
        float c[4];
        quat_canonicalize(q, c);
        for (int i = 0; i < 4; ++i) {
            m->acc_q[i] = w * c[i];
        }
    } else {
        float dot = 0.0F;
        for (int i = 0; i < 4; ++i) {
            dot += m->acc_q[i] * q[i];
        }
        const float sw = (dot < 0.0F) ? -w : w;
        for (int i = 0; i < 4; ++i) {
            m->acc_q[i] += sw * q[i];
        }
    }
    m->count += 1;
}

static void mix_finish(const mix_acc_t *m, const nt_anim_trs_t *def, float out_t[3], float out_q[4], float out_s[3]) {
    if (m->w_sum <= 0.0F) {
        for (int i = 0; i < 3; ++i) {
            out_t[i] = def->t[i];
            out_s[i] = def->s[i];
        }
        for (int i = 0; i < 4; ++i) {
            out_q[i] = def->q[i];
        }
        return;
    }
    const float inv_w = 1.0F / m->w_sum;
    for (int i = 0; i < 3; ++i) {
        out_t[i] = m->acc_t[i] * inv_w;
        out_s[i] = m->acc_s[i] * inv_w;
    }
    float n2 = 0.0F;
    for (int i = 0; i < 4; ++i) {
        n2 += m->acc_q[i] * m->acc_q[i];
    }
    const float inv_n = 1.0F / sqrtf(n2);
    for (int i = 0; i < 4; ++i) {
        out_q[i] = m->acc_q[i] * inv_n;
    }
}

/* Same expressions as nt_anim_mat34_from_trs, reading loose parts. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void mat34_from_parts(const float t[3], const float q[4], const float s[3], nt_anim_mat34_t *out) {
    const float x = q[0];
    const float y = q[1];
    const float z = q[2];
    const float w = q[3];

    const float xx = x * x;
    const float yy = y * y;
    const float zz = z * z;
    const float xy = x * y;
    const float xz = x * z;
    const float yz = y * z;
    const float wx = w * x;
    const float wy = w * y;
    const float wz = w * z;

    const float sx = s[0];
    const float sy = s[1];
    const float sz = s[2];

    out->r[0][0] = (1.0F - (2.0F * (yy + zz))) * sx;
    out->r[0][1] = (2.0F * (xy - wz)) * sy;
    out->r[0][2] = (2.0F * (xz + wy)) * sz;
    out->r[0][3] = t[0];

    out->r[1][0] = (2.0F * (xy + wz)) * sx;
    out->r[1][1] = (1.0F - (2.0F * (xx + zz))) * sy;
    out->r[1][2] = (2.0F * (yz - wx)) * sz;
    out->r[1][3] = t[1];

    out->r[2][0] = (2.0F * (xz - wy)) * sx;
    out->r[2][1] = (2.0F * (yz + wx)) * sy;
    out->r[2][2] = (1.0F - (2.0F * (xx + yy))) * sz;
    out->r[2][3] = t[2];
}
// #endregion

// #region sample stage

static void stage_sample_aos40(const bench_t *b, uint32_t chars_n, uint16_t joints, uint16_t tracks, uint32_t frame) {
    for (uint16_t t = 0; t < tracks; ++t) {
        const nt_anim_trs_t *ka = &b->key40[(size_t)2U * t * joints];
        const nt_anim_trs_t *kb = &ka[joints];
        const float phase = b->phase[t] + ((float)frame * b->step[t]);
        for (uint32_t c = 0; c < chars_n; ++c) {
            const float alpha = wrap01(phase + ((float)c * 1e-4F));
            nt_anim_trs_t *out = &b->trk40[(((size_t)t * chars_n) + c) * joints];
            for (uint16_t j = 0; j < joints; ++j) {
                lerp_parts(ka[j].t, ka[j].q, ka[j].s, kb[j].t, kb[j].q, kb[j].s, alpha, out[j].t, out[j].q, out[j].s);
            }
        }
    }
}

static void stage_sample_aos48(const bench_t *b, uint32_t chars_n, uint16_t joints, uint16_t tracks, uint32_t frame) {
    for (uint16_t t = 0; t < tracks; ++t) {
        const pose48_t *ka = &b->key48[(size_t)2U * t * joints];
        const pose48_t *kb = &ka[joints];
        const float phase = b->phase[t] + ((float)frame * b->step[t]);
        for (uint32_t c = 0; c < chars_n; ++c) {
            const float alpha = wrap01(phase + ((float)c * 1e-4F));
            pose48_t *out = &b->trk48[(((size_t)t * chars_n) + c) * joints];
            for (uint16_t j = 0; j < joints; ++j) {
                lerp_parts(ka[j].t, ka[j].q, ka[j].s, kb[j].t, kb[j].q, kb[j].s, alpha, out[j].t, out[j].q, out[j].s);
            }
        }
    }
}

static void soa_load(const soa_pose_t *s, size_t i, float t[3], float q[4], float sc[3]) {
    t[0] = s->tx[i];
    t[1] = s->ty[i];
    t[2] = s->tz[i];
    q[0] = s->qx[i];
    q[1] = s->qy[i];
    q[2] = s->qz[i];
    q[3] = s->qw[i];
    sc[0] = s->sx[i];
    sc[1] = s->sy[i];
    sc[2] = s->sz[i];
}

static void soa_write(const soa_pose_t *s, size_t i, const float t[3], const float q[4], const float sc[3]) {
    s->tx[i] = t[0];
    s->ty[i] = t[1];
    s->tz[i] = t[2];
    s->qx[i] = q[0];
    s->qy[i] = q[1];
    s->qz[i] = q[2];
    s->qw[i] = q[3];
    s->sx[i] = sc[0];
    s->sy[i] = sc[1];
    s->sz[i] = sc[2];
}

static void stage_sample_soa(const bench_t *b, uint32_t chars_n, uint16_t joints, uint16_t tracks, uint32_t frame) {
    for (uint16_t t = 0; t < tracks; ++t) {
        const size_t base_a = (size_t)2U * t * joints;
        const size_t base_b = base_a + joints;
        const float phase = b->phase[t] + ((float)frame * b->step[t]);
        for (uint32_t c = 0; c < chars_n; ++c) {
            const float alpha = wrap01(phase + ((float)c * 1e-4F));
            const size_t base_o = (((size_t)t * chars_n) + c) * joints;
            for (uint16_t j = 0; j < joints; ++j) {
                float ta[3];
                float qa[4];
                float sa[3];
                float tb[3];
                float qb[4];
                float sb[3];
                float ot[3];
                float oq[4];
                float os[3];
                soa_load(&b->key_soa, base_a + j, ta, qa, sa);
                soa_load(&b->key_soa, base_b + j, tb, qb, sb);
                lerp_parts(ta, qa, sa, tb, qb, sb, alpha, ot, oq, os);
                soa_write(&b->trk_soa, base_o + j, ot, oq, os);
            }
        }
    }
}
// #endregion

// #region mix stage

static void stage_mix_aos40(const bench_t *b, const nt_anim_skeleton_t *skel, uint32_t chars_n, uint16_t tracks) {
    const uint16_t joints = skel->joint_count;
    for (uint32_t c = 0; c < chars_n; ++c) {
        nt_anim_trs_t *out = &b->mix40[(size_t)c * joints];
        for (uint16_t j = 0; j < joints; ++j) {
            mix_acc_t m;
            mix_begin(&m);
            for (uint16_t t = 0; t < tracks; ++t) {
                const nt_anim_trs_t *in = &b->trk40[((((size_t)t * chars_n) + c) * joints) + j];
                mix_add(&m, in->t, in->q, in->s, b->gain[t]);
            }
            mix_finish(&m, &skel->rest[j], out[j].t, out[j].q, out[j].s);
        }
    }
}

static void stage_mix_aos48(const bench_t *b, const nt_anim_skeleton_t *skel, uint32_t chars_n, uint16_t tracks) {
    const uint16_t joints = skel->joint_count;
    for (uint32_t c = 0; c < chars_n; ++c) {
        pose48_t *out = &b->mix48[(size_t)c * joints];
        for (uint16_t j = 0; j < joints; ++j) {
            mix_acc_t m;
            mix_begin(&m);
            for (uint16_t t = 0; t < tracks; ++t) {
                const pose48_t *in = &b->trk48[((((size_t)t * chars_n) + c) * joints) + j];
                mix_add(&m, in->t, in->q, in->s, b->gain[t]);
            }
            mix_finish(&m, &skel->rest[j], out[j].t, out[j].q, out[j].s);
        }
    }
}

static void stage_mix_soa(const bench_t *b, const nt_anim_skeleton_t *skel, uint32_t chars_n, uint16_t tracks) {
    const uint16_t joints = skel->joint_count;
    for (uint32_t c = 0; c < chars_n; ++c) {
        const size_t base_o = (size_t)c * joints;
        for (uint16_t j = 0; j < joints; ++j) {
            mix_acc_t m;
            mix_begin(&m);
            for (uint16_t t = 0; t < tracks; ++t) {
                float it[3];
                float iq[4];
                float is[3];
                soa_load(&b->trk_soa, (((((size_t)t * chars_n) + c) * joints) + j), it, iq, is);
                mix_add(&m, it, iq, is, b->gain[t]);
            }
            float ot[3];
            float oq[4];
            float os[3];
            mix_finish(&m, &skel->rest[j], ot, oq, os);
            soa_write(&b->mix_soa, base_o + j, ot, oq, os);
        }
    }
}
// #endregion

// #region FK stage

static void fk_aos48(const nt_anim_skeleton_t *skel, const pose48_t *local, nt_anim_mat34_t *model) {
    for (uint16_t j = 0; j < skel->joint_count; ++j) {
        nt_anim_mat34_t l;
        mat34_from_parts(local[j].t, local[j].q, local[j].s, &l);
        const uint16_t p = skel->parent[j];
        if (p == NT_ANIM_NO_PARENT) {
            model[j] = l;
        } else {
            nt_anim_mat34_mul(&model[p], &l, &model[j]);
        }
    }
}

static void fk_soa(const nt_anim_skeleton_t *skel, const soa_pose_t *local, size_t base, nt_anim_mat34_t *model) {
    for (uint16_t j = 0; j < skel->joint_count; ++j) {
        float t[3];
        float q[4];
        float s[3];
        soa_load(local, base + j, t, q, s);
        nt_anim_mat34_t l;
        mat34_from_parts(t, q, s, &l);
        const uint16_t p = skel->parent[j];
        if (p == NT_ANIM_NO_PARENT) {
            model[j] = l;
        } else {
            nt_anim_mat34_mul(&model[p], &l, &model[j]);
        }
    }
}
// #endregion

// #region stage dispatch

static void run_sample(const bench_t *b, const nt_anim_skeleton_t *skel, uint32_t chars_n, uint16_t tracks, layout_t layout, uint32_t frame) {
    switch (layout) {
    case LAYOUT_AOS40:
        stage_sample_aos40(b, chars_n, skel->joint_count, tracks, frame);
        break;
    case LAYOUT_AOS48:
        stage_sample_aos48(b, chars_n, skel->joint_count, tracks, frame);
        break;
    case LAYOUT_SOA:
        stage_sample_soa(b, chars_n, skel->joint_count, tracks, frame);
        break;
    default:
        break;
    }
}

static void run_mix(const bench_t *b, const nt_anim_skeleton_t *skel, uint32_t chars_n, uint16_t tracks, layout_t layout) {
    switch (layout) {
    case LAYOUT_AOS40:
        stage_mix_aos40(b, skel, chars_n, tracks);
        break;
    case LAYOUT_AOS48:
        stage_mix_aos48(b, skel, chars_n, tracks);
        break;
    case LAYOUT_SOA:
        stage_mix_soa(b, skel, chars_n, tracks);
        break;
    default:
        break;
    }
}

static void run_fk(const bench_t *b, const nt_anim_skeleton_t *skel, uint32_t chars_n, layout_t layout) {
    const uint16_t joints = skel->joint_count;
    switch (layout) {
    case LAYOUT_AOS40:
        for (uint32_t c = 0; c < chars_n; ++c) {
            const size_t base = (size_t)c * joints;
            nt_anim_fk(skel, &b->mix40[base], &b->model[LAYOUT_AOS40][base], 0U, joints);
        }
        break;
    case LAYOUT_AOS48:
        for (uint32_t c = 0; c < chars_n; ++c) {
            const size_t base = (size_t)c * joints;
            fk_aos48(skel, &b->mix48[base], &b->model[LAYOUT_AOS48][base]);
        }
        break;
    case LAYOUT_SOA:
        for (uint32_t c = 0; c < chars_n; ++c) {
            const size_t base = (size_t)c * joints;
            fk_soa(skel, &b->mix_soa, base, &b->model[LAYOUT_SOA][base]);
        }
        break;
    default:
        break;
    }
}

static void run_frame(const bench_t *b, const nt_anim_skeleton_t *skel, uint32_t chars_n, uint16_t tracks, layout_t layout, uint32_t frame) {
    run_sample(b, skel, chars_n, tracks, layout, frame);
    run_mix(b, skel, chars_n, tracks, layout);
    run_fk(b, skel, chars_n, layout);
}
// #endregion

// #region cross-layout check

static int verify_layouts(const bench_t *b, const nt_anim_skeleton_t *skel, uint16_t tracks) {
    for (int l = 0; l < LAYOUT_COUNT; ++l) {
        run_frame(b, skel, 1U, tracks, (layout_t)l, 0U);
    }
    const size_t floats = (size_t)skel->joint_count * 12U;
    const float *ref = &b->model[LAYOUT_AOS40][0].r[0][0];
    for (int l = 1; l < LAYOUT_COUNT; ++l) {
        const float *got = &b->model[l][0].r[0][0];
        for (size_t i = 0; i < floats; ++i) {
            const float d = fabsf(ref[i] - got[i]);
            if (!(d <= 1e-5F)) {
                (void)printf("FK mismatch: %s vs %s, joint %zu element %zu: %.9g vs %.9g (delta %.9g)\n", layout_name(LAYOUT_AOS40), layout_name((layout_t)l), i / 12U, i % 12U, (double)ref[i],
                             (double)got[i], (double)d);
                return 0;
            }
        }
    }
    return 1;
}
// #endregion

// #region measurement

typedef struct {
    double sample;
    double mix;
    double fk;
} stage_ns_t;

static double median5(const double v[BENCH_REPS]) {
    double a[BENCH_REPS];
    memcpy(a, v, sizeof(a));
    for (int i = 1; i < BENCH_REPS; ++i) {
        const double key = a[i];
        int k = i - 1;
        while (k >= 0 && a[k] > key) {
            a[k + 1] = a[k];
            k--;
        }
        a[k + 1] = key;
    }
    return a[BENCH_REPS / 2];
}

static void run_one(const bench_t *b, const nt_anim_skeleton_t *skel, uint32_t chars_n, uint16_t tracks, layout_t layout, int stage, uint32_t frame) {
    switch (stage) {
    case 0:
        run_sample(b, skel, chars_n, tracks, layout, frame);
        break;
    case 1:
        run_mix(b, skel, chars_n, tracks, layout);
        break;
    default:
        run_fk(b, skel, chars_n, layout);
        break;
    }
}

static uint32_t clamp_batch(double want) {
    if (!(want > 1.0)) {
        return 1U;
    }
    if (want > 1e8) {
        return 100000000U;
    }
    return (uint32_t)want;
}

/* One warm-up frame, then a batch grown until it clears 1 ms so the ~100 ns
 * timer tick is amortized, then the measured run. Returns ns per skeleton joint
 * per frame for the stage. */
static double time_stage(const bench_t *b, const nt_anim_skeleton_t *skel, uint32_t chars_n, uint16_t tracks, layout_t layout, int stage) {
    run_frame(b, skel, chars_n, tracks, layout, 0U);

    uint32_t probe = 1U;
    double per = 0.0;
    while (probe <= 100000000U) {
        const double c0 = get_time_sec();
        for (uint32_t f = 0; f < probe; ++f) {
            run_one(b, skel, chars_n, tracks, layout, stage, f);
        }
        const double cal = get_time_sec() - c0;
        if (cal >= 0.001) {
            per = cal / (double)probe;
            break;
        }
        probe *= 4U;
    }
    const uint32_t batch = (per > 0.0) ? clamp_batch(STAGE_SECONDS / per) : 1U;

    const double t0 = get_time_sec();
    for (uint32_t f = 0; f < batch; ++f) {
        run_one(b, skel, chars_n, tracks, layout, stage, f);
    }
    const double elapsed = get_time_sec() - t0;
    const double joints = (double)batch * (double)chars_n * (double)skel->joint_count;
    return (elapsed * 1e9) / joints;
}

static stage_ns_t measure(const bench_t *b, const nt_anim_skeleton_t *skel, uint32_t chars_n, uint16_t tracks, layout_t layout) {
    stage_ns_t out;
    out.sample = time_stage(b, skel, chars_n, tracks, layout, 0);
    out.mix = time_stage(b, skel, chars_n, tracks, layout, 1);
    out.fk = time_stage(b, skel, chars_n, tracks, layout, 2);
    return out;
}

static void run_config(const bench_t *b, const rig_t *rig, uint32_t chars_n, uint16_t tracks) {
    double sample[LAYOUT_COUNT][BENCH_REPS];
    double mix[LAYOUT_COUNT][BENCH_REPS];
    double fk[LAYOUT_COUNT][BENCH_REPS];

    /* Repetitions outermost so drift hits every layout the same way. */
    for (int r = 0; r < BENCH_REPS; ++r) {
        for (int l = 0; l < LAYOUT_COUNT; ++l) {
            const stage_ns_t t = measure(b, &rig->view, chars_n, tracks, (layout_t)l);
            sample[l][r] = t.sample;
            mix[l][r] = t.mix;
            fk[l][r] = t.fk;
        }
    }
    for (int l = 0; l < LAYOUT_COUNT; ++l) {
        const double s = median5(sample[l]);
        const double m = median5(mix[l]);
        const double f = median5(fk[l]);
        (void)printf("| %u | %u | %u | %s | %.2f | %.2f | %.2f | %.2f |\n", (unsigned)rig->view.joint_count, (unsigned)chars_n, (unsigned)tracks, layout_name((layout_t)l), s, m, f, s + m + f);
    }
    (void)fflush(stdout);
}
// #endregion

int main(int argc, char **argv) {
    int quick = 0;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--quick") == 0) {
            quick = 1;
        } else {
            (void)fprintf(stderr, "usage: anim_layout [--quick]\n");
            return 1;
        }
    }

    static const uint16_t joint_set[3] = {30U, 60U, 100U};
    static const uint32_t char_set[3] = {1U, 100U, 1000U};
    static const uint16_t track_set[3] = {1U, 2U, 4U};

    bench_t b;
    bench_alloc(&b);
    rig_t rig;

    rig_build(&rig, (uint16_t)MAX_JOINTS, RIG_SEED);
    keys_init(&b, (uint16_t)MAX_JOINTS, (uint16_t)MAX_TRACKS, RIG_SEED ^ 0xD1B54A32D192ED03ULL);
    if (verify_layouts(&b, &rig.view, (uint16_t)MAX_TRACKS) == 0) {
        bench_free(&b);
        return 1;
    }
    (void)printf("FK cross-layout check: identical within 1e-5 (J=%u, T=%u)\n\n", MAX_JOINTS, MAX_TRACKS);

    (void)printf("ns per skeleton joint per frame (C*J joints), median of %d repetitions\n\n", BENCH_REPS);
    (void)printf("| J | C | T | Layout | sample | mix | FK | total |\n");
    (void)printf("|--:|--:|--:|:-------|-------:|----:|---:|------:|\n");

    const int j_lo = quick ? 1 : 0;
    const int j_hi = quick ? 2 : 3;
    const int c_lo = quick ? 1 : 0;
    const int c_hi = quick ? 2 : 3;
    const int t_lo = quick ? 1 : 0;
    const int t_hi = quick ? 2 : 3;

    for (int ji = j_lo; ji < j_hi; ++ji) {
        rig_build(&rig, joint_set[ji], RIG_SEED);
        for (int ti = t_lo; ti < t_hi; ++ti) {
            keys_init(&b, joint_set[ji], track_set[ti], RIG_SEED ^ 0xD1B54A32D192ED03ULL);
            for (int ci = c_lo; ci < c_hi; ++ci) {
                run_config(&b, &rig, char_set[ci], track_set[ti]);
            }
        }
    }

    bench_free(&b);
    return 0;
}
