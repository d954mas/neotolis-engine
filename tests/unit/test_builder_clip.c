/* Clip import from glTF: what nt_builder_add_scene_clip ships for the animated
 * fixture of rigged_glb.c -- grid samples, STEP keys, folded constants -- and
 * what its report and bounds measure, each checked against an evaluation of
 * the fixture's curves written here from their closed forms, never through the
 * builder's own evaluator. The Khronos assets pin the importer on real data. */

/* System headers before Unity to avoid noreturn / __declspec conflict on MSVC */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Windows SDK must be included early (before stdnoreturn.h from C17 headers) */
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

/* clang-format off */
#include "nt_builder.h"
#include "nt_builder_internal.h"
#include "nt_pack_format.h"
#include "hash/nt_hash.h"
#include "log/nt_log.h"
#include "skeletal/nt_skeletal.h"
#include "test_helpers/build_assert_trap.h"
#include "test_helpers/rigged_glb.h"
#include "unity.h"
/* clang-format on */

#ifdef _WIN32
#include <direct.h>
#define MKDIR(p) _mkdir(p)
#else
#include <sys/stat.h>
#define MKDIR(p) mkdir(p, 0755)
#endif

#define TMP_DIR "build/tests/tmp"
#define RIG_GLB TMP_DIR "/clip_rig.glb"
#define OTHER_GLB TMP_DIR "/clip_other.glb"
#define PACK_PATH TMP_DIR "/builder_clip.ntpack"
#define CLIP_ID "clips/fixture"
#define FPS 24.0F
#define GRID 25U /* round(1.0 * 24) + 1 */
#define FPS_COARSE 10.0F
#define GRID_COARSE 11U /* the 0.25 / 0.75 keys fall between samples */
#define SUBSAMPLES 3U
#define QUARTER_PI 0.78539816339744830962 /* pi / 4: the half-angle of one 90 degree key step */

// #region log sink
static uint32_t s_log_warnings;
static char s_log_last[NT_LOG_BUF_SIZE];

static void log_sink(nt_log_level_t level, const char *domain, const char *msg, void *user) {
    (void)domain;
    (void)user;
    if (level >= NT_LOG_LEVEL_WARN) {
        s_log_warnings++;
        (void)snprintf(s_log_last, sizeof(s_log_last), "%s", msg);
    }
}

void setUp(void) {
    (void)MKDIR("build");
    (void)MKDIR("build/tests");
    (void)MKDIR(TMP_DIR);
    s_log_warnings = 0;
    s_log_last[0] = '\0';
    nt_log_add_sink(log_sink, NULL);
}
/* A Unity failure inside a trapped call leaves the trap installed; the next
 * NT_BUILD_ASSERT must abort, not jump into a dead frame. */
void tearDown(void) {
    nt_log_remove_sink(log_sink, NULL);
    nt_build_assert_handler = NULL;
}
// #endregion

// #region helpers
static uint32_t f32_bits(float v) {
    uint32_t bits = 0;
    memcpy(&bits, &v, sizeof(bits));
    return bits;
}
#define ASSERT_F32(expected, actual) TEST_ASSERT_EQUAL_HEX32(f32_bits(expected), f32_bits(actual))

/* Within one float ulp: the two sides evaluate the same double formula in a
 * different order and may cast to neighbouring floats. */
static void assert_ulp(float expected, float actual) {
    if (f32_bits(expected) == f32_bits(actual)) {
        return;
    }
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(f32_bits(nextafterf(expected, actual)), f32_bits(actual), "value is more than one ulp off");
}

static void assert_close(double expected, double actual, double tolerance) {
    if (fabs(expected - actual) > tolerance) {
        char msg[128];
        (void)snprintf(msg, sizeof(msg), "expected %.9g, got %.9g", expected, actual);
        TEST_FAIL_MESSAGE(msg);
    }
}

/* The payload of the pack's asset with this resource id, so a test reads the
 * bytes the build shipped. Caller frees. */
static uint8_t *read_pack_clip(const char *pack_path, const char *resource_id, uint32_t *out_size) {
    FILE *f = fopen(pack_path, "rb");
    TEST_ASSERT_NOT_NULL(f);
    TEST_ASSERT_EQUAL(0, fseek(f, 0, SEEK_END));
    const long file_size = ftell(f);
    TEST_ASSERT_TRUE(file_size > (long)sizeof(NtPackHeader));
    TEST_ASSERT_EQUAL(0, fseek(f, 0, SEEK_SET));
    uint8_t *file = (uint8_t *)malloc((size_t)file_size);
    TEST_ASSERT_NOT_NULL(file);
    TEST_ASSERT_EQUAL((size_t)file_size, fread(file, 1, (size_t)file_size, f));
    (void)fclose(f);

    const uint64_t want = nt_hash64_str(resource_id).value;
    NtPackHeader hdr;
    memcpy(&hdr, file, sizeof(hdr));
    for (uint16_t i = 0; i < hdr.asset_count; i++) {
        NtAssetEntry entry;
        memcpy(&entry, file + sizeof(NtPackHeader) + ((size_t)i * sizeof(NtAssetEntry)), sizeof(entry));
        if (entry.resource_id != want || entry.asset_type != (uint8_t)NT_ASSET_CLIP) {
            continue;
        }
        TEST_ASSERT_TRUE((size_t)entry.offset + entry.size <= (size_t)file_size);
        /* The view reads the arrays in place, so the copy is 4-aligned like a
         * pack asset. */
        uint8_t *payload = (uint8_t *)malloc(entry.size);
        TEST_ASSERT_NOT_NULL(payload);
        memcpy(payload, file + entry.offset, entry.size);
        free(file);
        *out_size = entry.size;
        return payload;
    }
    free(file);
    TEST_FAIL_MESSAGE("the pack holds no clip with the requested id");
    return NULL;
}

/* One fixture export: writes the glb with opts (rig and clip from the same
 * file unless clip_opts differs), imports the rig at skeleton_root, exports
 * the clip and reads it back. The scene and rig stay alive for the caller. */
typedef struct {
    nt_glb_scene_t scene;
    nt_glb_scene_t clip_scene;
    nt_builder_rig_t rig;
    nt_builder_clip_report_t report;
    uint8_t *payload;
    uint32_t payload_size;
    nt_skeletal_clip_t view;
} fixture_export_t;

static void fixture_export_at(fixture_export_t *fx, const rigged_glb_opts_t *rig_opts, const rigged_glb_opts_t *clip_opts, uint32_t skeleton_root, float fps) {
    memset(fx, 0, sizeof(*fx));
    rigged_glb_write(RIG_GLB, rig_opts);
    TEST_ASSERT_EQUAL(NT_BUILD_OK, nt_builder_parse_glb_scene(&fx->scene, RIG_GLB));
    nt_builder_import_rig(&fx->scene, 0, skeleton_root, &fx->rig);
    const nt_glb_scene_t *clip_scene = &fx->scene;
    if (clip_opts != rig_opts) {
        rigged_glb_write(OTHER_GLB, clip_opts);
        TEST_ASSERT_EQUAL(NT_BUILD_OK, nt_builder_parse_glb_scene(&fx->clip_scene, OTHER_GLB));
        clip_scene = &fx->clip_scene;
    }
    TEST_ASSERT_EQUAL_UINT32(1, clip_scene->animation_count);
    TEST_ASSERT_EQUAL_STRING("Clip", clip_scene->animations[0].name);

    (void)remove(PACK_PATH);
    NtBuilderContext *ctx = nt_builder_start_pack(PACK_PATH);
    TEST_ASSERT_NOT_NULL(ctx);
    nt_builder_add_scene_clip(ctx, clip_scene, 0, &fx->rig, fps, CLIP_ID, &fx->report);
    TEST_ASSERT_EQUAL(NT_BUILD_OK, nt_builder_finish_pack(ctx));
    nt_builder_free_pack(ctx);

    fx->payload = read_pack_clip(PACK_PATH, CLIP_ID, &fx->payload_size);
    nt_skeletal_clip_view(fx->payload, &fx->view);
}

static void fixture_export(fixture_export_t *fx, const rigged_glb_opts_t *rig_opts, const rigged_glb_opts_t *clip_opts, uint32_t skeleton_root) {
    fixture_export_at(fx, rig_opts, clip_opts, skeleton_root, FPS);
}

static void fixture_free(fixture_export_t *fx) {
    free(fx->payload);
    nt_builder_free_rig(&fx->rig);
    nt_builder_free_glb_scene(&fx->clip_scene);
    nt_builder_free_glb_scene(&fx->scene);
}

static uint16_t joint_named(const nt_skeletal_skeleton_t *skel, const char *name) {
    const uint32_t id = nt_hash32_str(name).value;
    for (uint16_t j = 0; j < skel->joint_count; j++) {
        if (skel->joint_id[j] == id) {
            return j;
        }
    }
    TEST_FAIL_MESSAGE("no joint carries that name");
    return 0;
}

/* Row k of a sampled table whose joint is j, or UINT32_MAX. */
static uint32_t sampled_row(const uint16_t *table, uint16_t count, uint16_t j) {
    for (uint16_t k = 0; k < count; k++) {
        if (table[k] == j) {
            return k;
        }
    }
    return UINT32_MAX;
}

static void assert_rounded_up(double expected, float actual);

static double grid_time(uint32_t i, uint32_t n) { return ((double)i * (double)RIGGED_GLB_ANIM_DURATION) / (double)(n - 1U); }
// #endregion

// #region reference curves
/* The fixture's curves from their closed forms (rigged_glb.h). Rotations about
 * Z by an angle a are (0, 0, sin(a/2), cos(a/2)); the Joint1 keys at 0.5 and
 * 1.0 are 90 and 180 degrees, authored as (0, 0, 0.6, 0.6) and (0, 0, -1, 0),
 * so a key-aligned sample is the authored key normalized in double. */
static void ref_normalize_key(const float *key, float *out) {
    double q[4];
    double len2 = 0.0;
    for (int c = 0; c < 4; c++) {
        q[c] = (double)key[c];
        len2 += q[c] * q[c];
    }
    for (int c = 0; c < 4; c++) {
        out[c] = (float)(q[c] / sqrt(len2));
    }
}

static const float k_j1_keys[4][4] = {{0.0F, 0.0F, 0.0F, 1.0F}, {0.0F, 0.0F, 0.0F, 1.0F}, {0.0F, 0.0F, 0.6F, 0.6F}, {0.0F, 0.0F, -1.0F, 0.0F}};

/* q and -q are one rotation: the importer keeps whichever hemisphere the
 * shortest path or the held key lands in, so rotations compare up to sign. */
static void assert_rotation_close(const double *ref, const float *actual, double tolerance) {
    double dot = 0.0;
    for (int c = 0; c < 4; c++) {
        dot += ref[c] * (double)actual[c];
    }
    const double sign = (dot < 0.0) ? -1.0 : 1.0;
    for (int c = 0; c < 4; c++) {
        assert_close(sign * ref[c], (double)actual[c], tolerance);
    }
}

/* Joint1 rotation: a spherical interpolation of rotations about one axis is a
 * linear interpolation of the angle, so the reference is the angle itself. */
static void ref_j1_q(double t, double *out) {
    double half_angle = 0.0;
    if (t >= 0.5) {
        half_angle = QUARTER_PI + ((fmin(t, 1.0) - 0.5) / 0.5 * QUARTER_PI);
    } else if (t >= 0.25) {
        half_angle = ((t - 0.25) / 0.25) * QUARTER_PI;
    }
    out[0] = 0.0;
    out[1] = 0.0;
    out[2] = sin(half_angle);
    out[3] = cos(half_angle);
}

/* Joint2 translation: the glTF cubic in its Bezier form, p0, p0 + m0/3,
 * p1 - m1/3, p1 with m the interval-scaled tangents, evaluated by de
 * Casteljau -- a different arithmetic route to the same curve. */
static void ref_j2_t(double t, double *out) {
    static const double p0[3] = {0.0, 0.0, 0.0};
    static const double p1[3] = {1.0, 2.0, 4.0};
    static const double out_tangent[3] = {8.0, 0.0, 0.0};
    static const double in_tangent[3] = {0.0, 8.0, 0.0};
    if (t <= 0.25) {
        memcpy(out, p0, sizeof(p0));
        return;
    }
    if (t >= 0.75) {
        memcpy(out, p1, sizeof(p1));
        return;
    }
    const double dt = 0.5;
    const double u = (t - 0.25) / dt;
    for (int c = 0; c < 3; c++) {
        double b[4] = {p0[c], p0[c] + (dt * out_tangent[c] / 3.0), p1[c] - (dt * in_tangent[c] / 3.0), p1[c]};
        for (int level = 3; level > 0; level--) {
            for (int i = 0; i < level; i++) {
                b[i] = ((1.0 - u) * b[i]) + (u * b[i + 1]);
            }
        }
        out[c] = b[0];
    }
}

/* Joint2 rotation: the same Bezier route on the quaternion components, then
 * normalized, as the glTF rules require of a cubic rotation. */
static void ref_j2_q(double t, double *out) {
    static const double q0[4] = {0.0, 0.0, 0.0, 1.0};
    static const double q1[4] = {0.0, 0.0, 1.0, 0.0};
    static const double m[4] = {0.0, 0.0, 2.0, 0.0};
    const double u = fmin(fmax(t, 0.0), 1.0);
    double len2 = 0.0;
    for (int c = 0; c < 4; c++) {
        double b[4] = {q0[c], q0[c] + (m[c] / 3.0), q1[c] - (m[c] / 3.0), q1[c]};
        for (int level = 3; level > 0; level--) {
            for (int i = 0; i < level; i++) {
                b[i] = ((1.0 - u) * b[i]) + (u * b[i + 1]);
            }
        }
        out[c] = b[0];
        len2 += b[0] * b[0];
    }
    for (int c = 0; c < 4; c++) {
        out[c] /= sqrt(len2);
    }
}

static void ref_j3_s(double t, double *out) {
    const double v = (t >= 0.75) ? 2.0 : 1.0;
    out[0] = v;
    out[1] = v;
    out[2] = v;
}

/* The exact local pose of every joint at t: rest, with the animated channels
 * replaced by the reference curves cast to float, as the builder does. */
static void ref_local_pose(const nt_skeletal_skeleton_t *skel, double t, bool step_only, nt_skeletal_trs_t *out) {
    memcpy(out, skel->rest, (size_t)skel->joint_count * sizeof(nt_skeletal_trs_t));
    double v[4];
    if (!step_only) {
        ref_j1_q(t, v);
        for (int c = 0; c < 4; c++) {
            out[joint_named(skel, "Joint1")].q[c] = (float)v[c];
        }
        ref_j2_t(t, v);
        for (int c = 0; c < 3; c++) {
            out[joint_named(skel, "Joint2")].t[c] = (float)v[c];
        }
        ref_j2_q(t, v);
        for (int c = 0; c < 4; c++) {
            out[joint_named(skel, "Joint2")].q[c] = (float)v[c];
        }
    }
    ref_j3_s(t, v);
    for (int c = 0; c < 3; c++) {
        out[joint_named(skel, "Joint3")].s[c] = (float)v[c];
    }
    out[joint_named(skel, "Joint4")].t[1] = 0.5F;
}

/* The dense set the importer measures over, built from the formula the spec
 * pins: grid times, three interior sub-samples per interval at (4i + k) / 4
 * of the grid, and every authored key time. Sorted, unique; returns the count. */
static uint32_t ref_dense_times(bool step_only, uint32_t grid, double *out) {
    static const double k_keys[] = {0.0, 0.25, 0.5, 1.0, 0.25, 0.75, 0.0, 1.0, 0.25, 0.75, 0.0, 1.0};
    uint32_t n = 0;
    for (uint32_t i = 0; i < grid; i++) {
        out[n++] = grid_time(i, grid);
        if (i + 1U < grid) {
            for (uint32_t k = 1; k <= SUBSAMPLES; k++) {
                out[n++] = (((4.0 * (double)i) + (double)k) * (double)RIGGED_GLB_ANIM_DURATION) / (4.0 * (double)(grid - 1U));
            }
        }
    }
    const uint32_t key_count = step_only ? 4U : 12U;
    for (uint32_t k = 0; k < key_count; k++) {
        out[n++] = step_only ? k_keys[8 + k] : k_keys[k];
    }
    /* Insertion sort and dedupe; the set is a few hundred entries. */
    for (uint32_t i = 1; i < n; i++) {
        const double v = out[i];
        uint32_t j = i;
        while (j > 0 && out[j - 1U] > v) {
            out[j] = out[j - 1U];
            j--;
        }
        out[j] = v;
    }
    uint32_t m = 0;
    for (uint32_t i = 0; i < n; i++) {
        if (i == 0 || out[i] != out[m - 1U]) {
            out[m++] = out[i];
        }
    }
    return m;
}
// #endregion

// #region grid and modes
/* Every grid time reproduces its stored block bit for bit through the runtime
 * sampler, and the stored blocks are the reference curves: exact where the
 * closed form is exact (key-aligned and held times, the dyadic Hermite
 * points), one ulp elsewhere for T/S and 1e-6 for the normalized rotations. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void test_grid_samples_reproduce_the_source(void) {
    const rigged_glb_opts_t opts = {.animation = true};
    fixture_export_t fx;
    fixture_export(&fx, &opts, &opts, UINT32_MAX);
    const nt_skeletal_clip_t *view = &fx.view;
    const nt_skeletal_skeleton_t *skel = &fx.rig.skeleton;
    TEST_ASSERT_EQUAL_UINT32(0, s_log_warnings);
    TEST_ASSERT_EQUAL_UINT32(GRID, view->sample_count);
    TEST_ASSERT_TRUE(view->duration == (double)RIGGED_GLB_ANIM_DURATION);
    TEST_ASSERT_EQUAL_HEX64(skel->rig_compat_id.value, view->rig_compat_id.value);
    TEST_ASSERT_EQUAL_UINT16(skel->joint_count, view->joint_count);

    const uint16_t j1 = joint_named(skel, "Joint1");
    const uint16_t j2 = joint_named(skel, "Joint2");
    const uint32_t q1_row = sampled_row(view->q_joint, view->n_q, j1);
    const uint32_t t2_row = sampled_row(view->t_joint, view->n_t, j2);
    const uint32_t q2_row = sampled_row(view->q_joint, view->n_q, j2);
    TEST_ASSERT_TRUE(q1_row != UINT32_MAX && t2_row != UINT32_MAX && q2_row != UINT32_MAX);
    TEST_ASSERT_EQUAL_UINT16(1, view->n_t);
    TEST_ASSERT_EQUAL_UINT16(2, view->n_q);
    TEST_ASSERT_EQUAL_UINT16(0, view->n_s);

    nt_skeletal_trs_t *pose = (nt_skeletal_trs_t *)calloc(skel->joint_count, sizeof(nt_skeletal_trs_t));
    TEST_ASSERT_NOT_NULL(pose);
    /* The dyadic Hermite points of the Joint2 translation, from the basis
     * values at u = 1/4, 1/2, 3/4 times the fixture's tangents and endpoints. */
    static const float k_j2_dyadic[3][3] = {{0.71875F, 0.125F, 0.625F}, {1.0F, 0.5F, 2.0F}, {1.03125F, 1.125F, 3.375F}};
    for (uint32_t i = 0; i < GRID; i++) {
        const double t = grid_time(i, GRID);
        nt_skeletal_sample(view, t, skel->rest, pose);
        const float *block = view->blocks + ((size_t)i * view->block_floats);
        const float *t2 = block + ((size_t)3U * t2_row);
        const float *q1 = block + ((size_t)3U * view->n_t) + ((size_t)4U * q1_row);
        const float *q2 = block + ((size_t)3U * view->n_t) + ((size_t)4U * q2_row);
        for (int c = 0; c < 3; c++) {
            ASSERT_F32(t2[c], pose[j2].t[c]);
        }
        for (int c = 0; c < 4; c++) {
            ASSERT_F32(q1[c], pose[j1].q[c]);
            ASSERT_F32(q2[c], pose[j2].q[c]);
        }

        double ref[4];
        ref_j2_t(t, ref);
        if (i <= 6U || i >= 18U) {
            /* Held before the first key and after the last, and the keys. */
            for (int c = 0; c < 3; c++) {
                ASSERT_F32((float)ref[c], t2[c]);
            }
        } else if (i == 9U || i == 12U || i == 15U) {
            for (int c = 0; c < 3; c++) {
                ASSERT_F32(k_j2_dyadic[(i - 9U) / 3U][c], t2[c]);
            }
        } else {
            for (int c = 0; c < 3; c++) {
                assert_ulp((float)ref[c], t2[c]);
            }
        }

        ref_j1_q(t, ref);
        assert_rotation_close(ref, q1, 1e-6);
        if (i == 0U || i == 6U || i == 12U || i == 24U) {
            float key[4];
            ref_normalize_key(k_j1_keys[(i == 24U) ? 3 : (i / 6U)], key);
            for (int c = 0; c < 4; c++) {
                ASSERT_F32(key[c], q1[c]);
            }
        }
        ref_j2_q(t, ref);
        assert_rotation_close(ref, q2, 1e-6);
    }
    /* Grid 8 is a third of the way from identity to 90 degrees: the slerp
     * value sin(15 deg) = 0.2588 is 6e-3 away from the nlerp 0.2527 a source
     * evaluator that lerped would have stored. Grid 16 is a third of the way
     * from 90 to the 180 degree key authored as (0, 0, -1, 0): the short way
     * round is 120 degrees, (0, 0, sin 60, cos 60); the long way an evaluator
     * without the hemisphere flip takes passes through 0 and lands
     * elsewhere. */
    const float *q1_8 = view->blocks + ((size_t)8U * view->block_floats) + ((size_t)3U * view->n_t) + ((size_t)4U * q1_row);
    assert_close(0.25881905, (double)q1_8[2], 1e-6);
    const float *q1_16 = view->blocks + ((size_t)16U * view->block_floats) + ((size_t)3U * view->n_t) + ((size_t)4U * q1_row);
    assert_close(0.8660254, fabs((double)q1_16[2]), 1e-6);
    assert_close(0.5, fabs((double)q1_16[3]), 1e-6);
    free(pose);
    fixture_free(&fx);
}

/* Storage modes read from the shipped view: the STEP keys are the authored
 * ones, Joint4 folded to one constant, the object curve and every other
 * channel stay absent. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void test_step_keys_constants_and_absent_channels(void) {
    const rigged_glb_opts_t opts = {.animation = true};
    fixture_export_t fx;
    fixture_export(&fx, &opts, &opts, UINT32_MAX);
    const nt_skeletal_clip_t *view = &fx.view;
    const nt_skeletal_skeleton_t *skel = &fx.rig.skeleton;

    TEST_ASSERT_EQUAL_UINT32(1, view->n_steps);
    TEST_ASSERT_EQUAL_UINT16(joint_named(skel, "Joint3"), view->steps[0].joint);
    TEST_ASSERT_EQUAL_UINT8(2, view->steps[0].channel);
    TEST_ASSERT_EQUAL_UINT32(0, view->steps[0].first);
    TEST_ASSERT_EQUAL_UINT32(2, view->steps[0].count);
    ASSERT_F32(0.25F, view->keys[0].time);
    ASSERT_F32(0.75F, view->keys[1].time);
    for (int c = 0; c < 3; c++) {
        ASSERT_F32(1.0F, view->keys[0].v[c]);
        ASSERT_F32(2.0F, view->keys[1].v[c]);
    }
    ASSERT_F32(0.0F, view->keys[0].v[3]);
    ASSERT_F32(0.0F, view->keys[1].v[3]);

    TEST_ASSERT_EQUAL_UINT16(1, view->n_ct);
    TEST_ASSERT_EQUAL_UINT16(0, view->n_cq);
    TEST_ASSERT_EQUAL_UINT16(0, view->n_cs);
    TEST_ASSERT_EQUAL_UINT16(joint_named(skel, "Joint4"), view->ct_joint[0]);
    ASSERT_F32(0.0F, view->ct[0]);
    ASSERT_F32(0.5F, view->ct[1]);
    ASSERT_F32(0.0F, view->ct[2]);
    for (int c = 0; c < 3; c++) {
        TEST_ASSERT_EQUAL_UINT8(NT_SKELETAL_CHANNEL_ABSENT, view->object.mode[c]);
    }

    /* Sampling at a held time leaves every unanimated channel at the default
     * the caller passed, and the STEP scale holds its last key. */
    nt_skeletal_trs_t *pose = (nt_skeletal_trs_t *)calloc(skel->joint_count, sizeof(nt_skeletal_trs_t));
    TEST_ASSERT_NOT_NULL(pose);
    nt_skeletal_sample(view, 0.8, skel->rest, pose);
    const uint16_t j0 = joint_named(skel, "Joint0");
    for (int c = 0; c < 3; c++) {
        ASSERT_F32(skel->rest[j0].t[c], pose[j0].t[c]);
        ASSERT_F32(2.0F, pose[joint_named(skel, "Joint3")].s[c]);
    }
    nt_skeletal_sample(view, 0.7, skel->rest, pose);
    for (int c = 0; c < 3; c++) {
        ASSERT_F32(1.0F, pose[joint_named(skel, "Joint3")].s[c]);
    }
    free(pose);
    fixture_free(&fx);
}

// #endregion

// #region report and bounds
/* The report's maxima and the header bounds, recomputed here over the same
 * dense set with the reference curves, the runtime sampler and FK. */
typedef struct {
    double lin, lin_time;
    uint16_t lin_joint;
    double t, t_time;
    uint16_t t_joint;
    double r_joints, r_root, s_max;
} ref_measure_t;

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void ref_measure(const fixture_export_t *fx, bool step_only, uint32_t grid, ref_measure_t *m) {
    const nt_skeletal_skeleton_t *skel = &fx->rig.skeleton;
    const uint16_t joints = skel->joint_count;
    memset(m, 0, sizeof(*m));
    double *times = (double *)malloc((((size_t)grid * 4U) + 16U) * sizeof(double));
    nt_skeletal_trs_t *local_rt = (nt_skeletal_trs_t *)calloc(joints, sizeof(nt_skeletal_trs_t));
    nt_skeletal_trs_t *local_ex = (nt_skeletal_trs_t *)calloc(joints, sizeof(nt_skeletal_trs_t));
    nt_skeletal_mat34_t *g_rt = (nt_skeletal_mat34_t *)calloc(joints, sizeof(nt_skeletal_mat34_t));
    nt_skeletal_mat34_t *g_ex = (nt_skeletal_mat34_t *)calloc(joints, sizeof(nt_skeletal_mat34_t));
    double *stretch = (double *)calloc(joints, sizeof(double));
    TEST_ASSERT_TRUE(times && local_rt && local_ex && g_rt && g_ex && stretch);
    const uint32_t n = ref_dense_times(step_only, grid, times);
    for (uint32_t i = 0; i < n; i++) {
        const double time = times[i];
        nt_skeletal_sample(&fx->view, time, skel->rest, local_rt);
        ref_local_pose(skel, time, step_only, local_ex);
        nt_skeletal_fk(skel, local_rt, g_rt, 0, joints);
        nt_skeletal_fk(skel, local_ex, g_ex, 0, joints);
        for (uint16_t j = 0; j < joints; j++) {
            double lin2 = 0.0;
            double dt2 = 0.0;
            double origin2 = 0.0;
            for (int r = 0; r < 3; r++) {
                for (int c = 0; c < 3; c++) {
                    const double d = (double)g_rt[j].r[r][c] - (double)g_ex[j].r[r][c];
                    lin2 += d * d;
                }
                const double d = (double)g_rt[j].r[r][3] - (double)g_ex[j].r[r][3];
                dt2 += d * d;
                origin2 += (double)g_rt[j].r[r][3] * (double)g_rt[j].r[r][3];
            }
            if (sqrt(lin2) > m->lin) {
                m->lin = sqrt(lin2);
                m->lin_time = time;
                m->lin_joint = j;
            }
            if (sqrt(dt2) > m->t) {
                m->t = sqrt(dt2);
                m->t_time = time;
                m->t_joint = j;
            }
            if (sqrt(origin2) > m->r_joints) {
                m->r_joints = sqrt(origin2);
            }
            const uint16_t parent = skel->parent[j];
            const float *s = local_rt[j].s;
            const double smax = fmax(fabs((double)s[0]), fmax(fabs((double)s[1]), fabs((double)s[2])));
            stretch[j] = ((parent == NT_SKELETAL_NO_PARENT) ? 1.0 : stretch[parent]) * smax;
            if (stretch[j] > m->s_max) {
                m->s_max = stretch[j];
            }
            if (parent == NT_SKELETAL_NO_PARENT) {
                const float *t = local_rt[j].t;
                const double root = sqrt(((double)t[0] * (double)t[0]) + ((double)t[1] * (double)t[1]) + ((double)t[2] * (double)t[2]));
                if (root > m->r_root) {
                    m->r_root = root;
                }
            }
        }
    }
    free(stretch);
    free(g_ex);
    free(g_rt);
    free(local_ex);
    free(local_rt);
    free(times);
}

/* A double bound stored as float rounds towards +infinity; the direction is
 * pinned on a value that a plain cast would round down. */
static void assert_rounded_up(double expected, float actual) {
    float f = (float)expected;
    if ((double)f < expected) {
        f = nextafterf(f, INFINITY);
    }
    ASSERT_F32(f, actual);
    ASSERT_F32(nextafterf(1.0F, 2.0F), nt_builder_round_up(1.0 + 1e-9));
}

static void check_report(float fps, uint32_t grid) {
    const rigged_glb_opts_t opts = {.animation = true};
    fixture_export_t fx;
    fixture_export_at(&fx, &opts, &opts, UINT32_MAX, fps);
    ref_measure_t m;
    ref_measure(&fx, false, grid, &m);

    TEST_ASSERT_EQUAL_UINT32(grid, fx.report.sample_count);
    ASSERT_F32(RIGGED_GLB_ANIM_DURATION, fx.report.duration);
    /* The grid misses the Hermite bulge and the slerp arc between samples, so
     * both errors are real numbers, not zeros that any report would match. */
    TEST_ASSERT_TRUE(m.lin > 1e-4 && m.t > 1e-4);
    assert_close(m.lin, (double)fx.report.cpu_error_lin, 1e-6 * m.lin);
    assert_close(m.t, (double)fx.report.cpu_error_t, 1e-6 * m.t);
    TEST_ASSERT_TRUE(m.lin_time == fx.report.worst_time_lin);
    TEST_ASSERT_TRUE(m.t_time == fx.report.worst_time_t);
    TEST_ASSERT_EQUAL_UINT16(m.lin_joint, fx.report.worst_joint_lin);
    TEST_ASSERT_EQUAL_UINT16(m.t_joint, fx.report.worst_joint_t);
    fixture_free(&fx);
}

/* At 24 fps every authored key is a grid time; at 10 fps the 0.25 / 0.75
 * keys fall between samples, so the dense set's key times carry weight of
 * their own and the STEP jump is measured where it happens. */
void test_report_matches_an_independent_measurement(void) {
    check_report(FPS, GRID);
    check_report(FPS_COARSE, GRID_COARSE);
}

/* Nothing sampled: one sample, no frame block, no error anywhere. */
void test_step_only_clip_ships_no_grid(void) {
    const rigged_glb_opts_t opts = {.animation_step_only = true};
    fixture_export_t fx;
    fixture_export(&fx, &opts, &opts, UINT32_MAX);
    TEST_ASSERT_EQUAL_UINT32(1, fx.view.sample_count);
    TEST_ASSERT_NULL(fx.view.blocks);
    TEST_ASSERT_EQUAL_UINT32(0, fx.view.block_floats);
    TEST_ASSERT_TRUE(fx.view.duration == (double)RIGGED_GLB_ANIM_DURATION);
    TEST_ASSERT_EQUAL_UINT32(1, fx.view.n_steps);
    TEST_ASSERT_EQUAL_UINT16(1, fx.view.n_ct);
    TEST_ASSERT_EQUAL_UINT32(1, fx.report.sample_count);
    ASSERT_F32(RIGGED_GLB_ANIM_DURATION, fx.report.duration);
    ASSERT_F32(0.0F, fx.report.cpu_error_lin);
    ASSERT_F32(0.0F, fx.report.cpu_error_t);
    TEST_ASSERT_TRUE(fx.report.worst_time_lin == 0.0 && fx.report.worst_time_t == 0.0);
    TEST_ASSERT_EQUAL_UINT16(0, fx.report.worst_joint_lin);
    TEST_ASSERT_EQUAL_UINT16(0, fx.report.worst_joint_t);
    /* Helper's scale 2 times the STEP scale 2 from 0.75 on. */
    ASSERT_F32(4.0F, fx.view.s_max);
    ASSERT_F32(0.0F, fx.view.r_root);
    ref_measure_t m;
    ref_measure(&fx, true, GRID, &m);
    assert_rounded_up(m.r_joints, fx.view.r_joints);
    assert_rounded_up(m.s_max, fx.view.s_max);
    fixture_free(&fx);
}

/* Bounds under the default cut (Root and Helper are joints: Helper's scale 2
 * doubles the STEP scale) and under a cut at Joint0 (its rest translation is
 * the only root translation; the Joint4 chain is 2 * 0.5). */
void test_bounds_match_the_recurrence_under_two_cuts(void) {
    const rigged_glb_opts_t opts = {.animation = true};
    fixture_export_t fx;
    ref_measure_t m;

    fixture_export(&fx, &opts, &opts, UINT32_MAX);
    ref_measure(&fx, false, GRID, &m);
    assert_rounded_up(m.r_joints, fx.view.r_joints);
    assert_rounded_up(m.r_root, fx.view.r_root);
    assert_rounded_up(m.s_max, fx.view.s_max);
    ASSERT_F32(4.0F, fx.view.s_max);
    ASSERT_F32(0.0F, fx.view.r_root);
    TEST_ASSERT_TRUE(fx.view.r_joints > 0.0F);
    fixture_free(&fx);

    fixture_export(&fx, &opts, &opts, RIGGED_GLB_NODE_JOINT0);
    ref_measure(&fx, false, GRID, &m);
    assert_rounded_up(m.r_joints, fx.view.r_joints);
    assert_rounded_up(m.r_root, fx.view.r_root);
    assert_rounded_up(m.s_max, fx.view.s_max);
    ASSERT_F32(2.0F, fx.view.s_max);
    assert_rounded_up(sqrt(14.0), fx.view.r_root);
    fixture_free(&fx);
}
// #endregion

// #region content errors
/* Each knob fires the rule it names, after a diagnostic that names the node. */
#define EXPECT_CLIP_REJECTED(knob_opts, expected, node)                                                                                                                                                \
    do {                                                                                                                                                                                               \
        fixture_export_t fx;                                                                                                                                                                           \
        EXPECT_BUILD_ASSERT_MATCH(fixture_export(&fx, &(knob_opts), &(knob_opts), UINT32_MAX), expected);                                                                                              \
        TEST_ASSERT_TRUE_MESSAGE(s_log_warnings > 0, "no diagnostic before the assert");                                                                                                               \
        TEST_ASSERT_NOT_NULL_MESSAGE(strstr(s_log_last, node), "the diagnostic does not name the node");                                                                                               \
    } while (0)

void test_channel_outside_the_rig_is_rejected(void) {
    const rigged_glb_opts_t opts = {.animation_outside_rig = true};
    EXPECT_CLIP_REJECTED(opts, "targets a node outside the rig", "MeshNode");
}

void test_morph_weights_channel_is_rejected(void) {
    const rigged_glb_opts_t opts = {.animation_weights = true};
    EXPECT_CLIP_REJECTED(opts, "animates morph weights", "MeshNode");
}

void test_duplicate_channel_is_rejected(void) {
    const rigged_glb_opts_t opts = {.animation_duplicate = true};
    EXPECT_CLIP_REJECTED(opts, "two channels animate one node property", "Joint1");
}

void test_matrix_node_channel_is_rejected(void) {
    const rigged_glb_opts_t opts = {.animation_matrix_node = true};
    EXPECT_CLIP_REJECTED(opts, "animated node carries a matrix", "Helper");
}

void test_step_key_past_the_snapped_end_is_dropped(void) {
    const rigged_glb_opts_t opts = {.animation_step_past_end = true};
    fixture_export_t fx;
    fixture_export(&fx, &opts, &opts, UINT32_MAX);
    /* 1.05 s is 25.2 frames: the clip ships 25, and the key at 1.05 is past
     * its 1.0417 s end. Both the fraction and the dropped key are logged. */
    TEST_ASSERT_EQUAL_UINT32(26, fx.view.sample_count);
    ASSERT_F32((float)(25.0 / 24.0), fx.report.duration);
    TEST_ASSERT_EQUAL_UINT32(1, fx.view.n_steps);
    TEST_ASSERT_EQUAL_UINT32(2, fx.view.steps[0].count);
    ASSERT_F32(0.75F, fx.view.keys[1].time);
    TEST_ASSERT_EQUAL_UINT32(2, s_log_warnings);
    TEST_ASSERT_NOT_NULL(strstr(s_log_last, "Joint3.scale drops 1 STEP key"));
    fixture_free(&fx);
}

void test_reparented_joint_is_rejected(void) {
    const rigged_glb_opts_t rig_opts = {0};
    const rigged_glb_opts_t clip_opts = {.animation = true, .reparent_joint2 = true};
    fixture_export_t fx;
    EXPECT_BUILD_ASSERT_MATCH(fixture_export(&fx, &rig_opts, &clip_opts, UINT32_MAX), "different parent than the rig");
    TEST_ASSERT_NOT_NULL(strstr(s_log_last, "Joint2"));
}

void test_backwards_key_times_are_rejected(void) {
    const rigged_glb_opts_t opts = {.animation_bad_times = true};
    EXPECT_CLIP_REJECTED(opts, "strictly increasing", "Joint3");
}

void test_animation_without_channels_is_rejected(void) {
    const rigged_glb_opts_t opts = {.animation_no_channels = true};
    fixture_export_t fx;
    EXPECT_BUILD_ASSERT_MATCH(fixture_export(&fx, &opts, &opts, UINT32_MAX), "animation has no channels");
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void test_bad_arguments_are_rejected(void) {
    const rigged_glb_opts_t opts = {.animation = true};
    rigged_glb_write(RIG_GLB, &opts);
    nt_glb_scene_t scene;
    TEST_ASSERT_EQUAL(NT_BUILD_OK, nt_builder_parse_glb_scene(&scene, RIG_GLB));
    nt_builder_rig_t rig;
    nt_builder_import_rig(&scene, 0, UINT32_MAX, &rig);
    (void)remove(PACK_PATH);
    NtBuilderContext *ctx = nt_builder_start_pack(PACK_PATH);
    nt_builder_clip_report_t report;
    EXPECT_BUILD_ASSERT_MATCH(nt_builder_add_scene_clip(ctx, &scene, 1, &rig, FPS, CLIP_ID, &report), "animation index out of range");
    EXPECT_BUILD_ASSERT_MATCH(nt_builder_add_scene_clip(ctx, &scene, 0, &rig, 0.0F, CLIP_ID, &report), "sample_fps must be finite and positive");
    EXPECT_BUILD_ASSERT_MATCH(nt_builder_add_scene_clip(ctx, &scene, 0, &rig, NAN, CLIP_ID, &report), "sample_fps must be finite and positive");
    EXPECT_BUILD_ASSERT_MATCH(nt_builder_add_scene_clip(ctx, &scene, 0, &rig, 1e30F, CLIP_ID, &report), "overflows the sample grid");
    EXPECT_BUILD_ASSERT_MATCH(nt_builder_add_scene_clip(ctx, &scene, 0, &rig, FPS, CLIP_ID, NULL), "invalid add_scene_clip args");
    nt_builder_free_pack(ctx);
    nt_builder_free_rig(&rig);
    nt_builder_free_glb_scene(&scene);
}

/* A clip from a second file with the same names and rest maps onto the rig
 * and carries its identity; one rest component one ulp off is a different rig. */
void test_clip_from_another_glb_maps_by_name_and_rest(void) {
    const rigged_glb_opts_t rig_opts = {0};
    const rigged_glb_opts_t clip_opts = {.animation = true};
    fixture_export_t fx;
    fixture_export(&fx, &rig_opts, &clip_opts, UINT32_MAX);
    TEST_ASSERT_EQUAL_UINT32(0, s_log_warnings);
    TEST_ASSERT_EQUAL_HEX64(fx.rig.skeleton.rig_compat_id.value, fx.view.rig_compat_id.value);
    TEST_ASSERT_EQUAL_UINT32(GRID, fx.view.sample_count);
    TEST_ASSERT_EQUAL_UINT16(joint_named(&fx.rig.skeleton, "Joint3"), fx.view.steps[0].joint);
    fixture_free(&fx);

    const rigged_glb_opts_t mismatch = {.animation = true, .rest_mismatch = true};
    fixture_export_t bad;
    EXPECT_BUILD_ASSERT_MATCH(fixture_export(&bad, &rig_opts, &mismatch, UINT32_MAX), "rest pose differs from the rig");
    TEST_ASSERT_NOT_NULL(strstr(s_log_last, "Joint2"));
}
// #endregion

// #region khronos assets
typedef struct {
    const char *path;
    uint32_t animation;
    uint32_t sample_count; /* round(last key * 24) + 1, from the input accessors */
    bool snapped;          /* the source is not a whole number of frames at 24 fps */
    float max_lin, max_t;  /* ceilings above the measured errors */
} khronos_clip_t;

/* Fox's three clips end at 3.4167, 0.7083 and 1.1583 s -- 82, 17 and 27.8
 * frames; Run's keys are not uniform (frames 0..16, then 20.8..27.8), so it
 * snaps to 28 frames with a warning, and its late keys sit 0.8 frames off
 * the grid, which the report shows as 4.7 degrees and 1.8 cm -- the number a
 * developer reads before raising the rate. CesiumMan's unnamed clip ends at
 * 2.0 s and starts at 1/24 s, so it holds before its first key. The other
 * ceilings sit above Walk's 0.29 degrees (a Frobenius distance of
 * 2 sqrt(2) sin(theta / 2) = 0.0072) and 0.06 cm. */
static const khronos_clip_t k_khronos[4] = {
    {"examples/skeletal_showcase/raw/Fox.glb", 0, 83, false, 0.02F, 0.5F},
    {"examples/skeletal_showcase/raw/Fox.glb", 1, 18, false, 0.02F, 0.5F},
    {"examples/skeletal_showcase/raw/Fox.glb", 2, 29, true, 0.2F, 2.5F},
    {"examples/skeletal_showcase/raw/CesiumMan.glb", 0, 49, false, 0.02F, 0.5F},
};

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void test_khronos_clips_export_at_24_fps(void) {
    for (uint32_t i = 0; i < 4; i++) {
        const khronos_clip_t *asset = &k_khronos[i];
        nt_glb_scene_t scene;
        TEST_ASSERT_EQUAL(NT_BUILD_OK, nt_builder_parse_glb_scene(&scene, asset->path));
        nt_builder_rig_t rig;
        nt_builder_import_rig(&scene, 0, UINT32_MAX, &rig);
        (void)remove(PACK_PATH);
        NtBuilderContext *ctx = nt_builder_start_pack(PACK_PATH);
        nt_builder_clip_report_t report;
        nt_builder_add_scene_clip(ctx, &scene, asset->animation, &rig, FPS, CLIP_ID, &report);
        TEST_ASSERT_EQUAL(NT_BUILD_OK, nt_builder_finish_pack(ctx));
        nt_builder_free_pack(ctx);
        TEST_ASSERT_EQUAL_UINT32(asset->snapped ? 1U : 0U, s_log_warnings);
        if (asset->snapped) {
            TEST_ASSERT_NOT_NULL(strstr(s_log_last, "not a whole number"));
        }
        s_log_warnings = 0;

        uint32_t size = 0;
        uint8_t *payload = read_pack_clip(PACK_PATH, CLIP_ID, &size);
        nt_skeletal_clip_t view;
        nt_skeletal_clip_view(payload, &view);
        (void)printf("%s animation[%u] %s: samples %u, cpu_error_lin %.3g at t=%.4f joint %u, cpu_error_t %.3g at t=%.4f joint %u, r_joints %.4g r_root %.4g s_max %.4g\n", asset->path,
                     asset->animation, scene.animations[asset->animation].name ? scene.animations[asset->animation].name : "(unnamed)", report.sample_count, (double)report.cpu_error_lin,
                     report.worst_time_lin, report.worst_joint_lin, (double)report.cpu_error_t, report.worst_time_t, report.worst_joint_t, (double)view.r_joints, (double)view.r_root,
                     (double)view.s_max);

        TEST_ASSERT_EQUAL_UINT32(asset->sample_count, report.sample_count);
        TEST_ASSERT_EQUAL_UINT32(asset->sample_count, view.sample_count);
        /* The grid step is exactly one frame: the shipped duration is the frame
         * count over 24, whatever the source length was. */
        ASSERT_F32((float)((double)(asset->sample_count - 1U) / 24.0), report.duration);
        TEST_ASSERT_TRUE(view.duration == (double)report.duration);
        TEST_ASSERT_TRUE(report.cpu_error_lin > 0.0F && report.cpu_error_lin <= asset->max_lin);
        TEST_ASSERT_TRUE(report.cpu_error_t > 0.0F && report.cpu_error_t <= asset->max_t);
        TEST_ASSERT_TRUE(view.r_joints > 0.0F && view.s_max >= 1.0F);
        TEST_ASSERT_EQUAL_HEX64(rig.skeleton.rig_compat_id.value, view.rig_compat_id.value);
        /* Every source channel is LINEAR with a moving value, and both skins
         * leave some joints unanimated; nothing folds and nothing steps. */
        TEST_ASSERT_EQUAL_UINT32(0, view.n_steps);
        TEST_ASSERT_TRUE(view.n_ct == 0 && view.n_cq == 0 && view.n_cs == 0);
        TEST_ASSERT_TRUE(view.n_q < view.joint_count);
        free(payload);
        nt_builder_free_rig(&rig);
        nt_builder_free_glb_scene(&scene);
    }
}
// #endregion

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_grid_samples_reproduce_the_source);
    RUN_TEST(test_step_keys_constants_and_absent_channels);
    RUN_TEST(test_step_only_clip_ships_no_grid);
    RUN_TEST(test_report_matches_an_independent_measurement);
    RUN_TEST(test_bounds_match_the_recurrence_under_two_cuts);
    RUN_TEST(test_channel_outside_the_rig_is_rejected);
    RUN_TEST(test_morph_weights_channel_is_rejected);
    RUN_TEST(test_duplicate_channel_is_rejected);
    RUN_TEST(test_matrix_node_channel_is_rejected);
    RUN_TEST(test_step_key_past_the_snapped_end_is_dropped);
    RUN_TEST(test_reparented_joint_is_rejected);
    RUN_TEST(test_backwards_key_times_are_rejected);
    RUN_TEST(test_animation_without_channels_is_rejected);
    RUN_TEST(test_bad_arguments_are_rejected);
    RUN_TEST(test_clip_from_another_glb_maps_by_name_and_rest);
    RUN_TEST(test_khronos_clips_export_at_24_fps);
    return UNITY_END();
}
