/* System and engine headers before Unity: <stdnoreturn.h> and the Windows SDK
 * clash over __declspec(noreturn) in the other order. */
#include <math.h>
#include <stddef.h>
#include <string.h>

#include "anim/nt_anim.h"
#include "math/nt_math.h"
#include "test_helpers/anim_rig.h"

#include "unity.h"

#include "test_helpers/nt_assert_trap.h"

/* Unity is built with UNITY_EXCLUDE_FLOAT, so float comparisons go through
 * fabsf like everywhere else in the suite. */
#define ASSERT_FLOAT_NEAR(expected, actual, tol) TEST_ASSERT_TRUE_MESSAGE(fabsf((expected) - (actual)) <= (tol), "float not within tolerance")

enum { JOINT_ROOT_A = 0, JOINT_SPINE = 1, JOINT_HELPER = 2, JOINT_ARM = 3, JOINT_HAND = 4, JOINT_LEG = 5, JOINT_ROOT_B = 6, JOINT_TAIL1 = 7, JOINT_TAIL2 = 8 };

static anim_rig_t g_rig;

void setUp(void) { anim_rig_asymmetric(&g_rig); }

void tearDown(void) {}

/* ---- cglm reference path ---- */

/* Reads t/q/s straight out of the AoS pose as vec3/versor: the ABI-alignment
 * exercise that Debug UBSan checks. */
static void ref_mat4_from_trs(nt_anim_trs_t *trs, mat4 out) {
    mat4 t;
    glm_translate_make(t, trs->t);
    mat4 r;
    glm_quat_mat4(trs->q, r);
    mat4 s;
    glm_scale_make(s, trs->s);
    mat4 tr;
    glm_mat4_mul(t, r, tr);
    glm_mat4_mul(tr, s, out);
}

static void ref_fk(nt_anim_trs_t *local, mat4 *out) {
    for (uint16_t j = 0; j < ANIM_RIG_JOINT_COUNT; ++j) {
        mat4 l;
        ref_mat4_from_trs(&local[j], l);
        const uint16_t p = g_rig.skel.parent[j];
        if (p == NT_ANIM_NO_PARENT) {
            glm_mat4_copy(l, out[j]);
        } else {
            glm_mat4_mul(out[p], l, out[j]);
        }
    }
}

static void assert_mat34_equals_mat4(const nt_anim_mat34_t *m34, mat4 ref, float tol) {
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 4; ++c) {
            ASSERT_FLOAT_NEAR(ref[c][r], m34->r[r][c], tol);
        }
    }
}

static void set_axis_angle(nt_anim_trs_t *o, float ax, float ay, float az, float degrees) {
    const float half = (degrees * 0.5F) * 0.017453292519943295F;
    const float sn = sinf(half);
    o->q[0] = ax * sn;
    o->q[1] = ay * sn;
    o->q[2] = az * sn;
    o->q[3] = cosf(half);
}

static nt_anim_trs_t make_trs(float tx, float ty, float tz, float ax, float ay, float az, float degrees, float sx, float sy, float sz) {
    nt_anim_trs_t o;
    o.t[0] = tx;
    o.t[1] = ty;
    o.t[2] = tz;
    o.s[0] = sx;
    o.s[1] = sy;
    o.s[2] = sz;
    set_axis_angle(&o, ax, ay, az, degrees);
    return o;
}

/* ---- Kernels against cglm ---- */

void test_mat34_from_trs_matches_cglm(void) {
    nt_anim_trs_t local[ANIM_RIG_JOINT_COUNT];
    nt_anim_pose_rest(&g_rig.skel, local);

    for (uint16_t j = 0; j < ANIM_RIG_JOINT_COUNT; ++j) {
        mat4 ref;
        ref_mat4_from_trs(&local[j], ref);
        nt_anim_mat34_t m;
        nt_anim_mat34_from_trs(&local[j], &m);
        assert_mat34_equals_mat4(&m, ref, 1e-5F);

        mat4 ref_bind;
        ref_mat4_from_trs(&g_rig.bind[j], ref_bind);
        nt_anim_mat34_t m_bind;
        nt_anim_mat34_from_trs(&g_rig.bind[j], &m_bind);
        assert_mat34_equals_mat4(&m_bind, ref_bind, 1e-5F);
    }
}

void test_mat34_mul_matches_cglm(void) {
    nt_anim_trs_t a = make_trs(0.3F, -1.2F, 2.0F, 0.0F, 1.0F, 0.0F, 37.0F, 1.0F, 2.5F, 0.4F);
    nt_anim_trs_t b = make_trs(-0.7F, 0.9F, 0.15F, 0.0F, 0.0F, 1.0F, -112.0F, 1.7F, 1.0F, 0.6F);

    mat4 ra;
    ref_mat4_from_trs(&a, ra);
    mat4 rb;
    ref_mat4_from_trs(&b, rb);
    mat4 rab;
    glm_mat4_mul(ra, rb, rab);

    nt_anim_mat34_t ma;
    nt_anim_mat34_from_trs(&a, &ma);
    nt_anim_mat34_t mb;
    nt_anim_mat34_from_trs(&b, &mb);
    nt_anim_mat34_t mab;
    nt_anim_mat34_mul(&ma, &mb, &mab);

    assert_mat34_equals_mat4(&mab, rab, 1e-5F);
}

void test_mat34_from_mat4_takes_top_three_rows(void) {
    nt_anim_trs_t a = make_trs(4.0F, -2.0F, 0.5F, 1.0F, 0.0F, 0.0F, 63.0F, 0.75F, 1.0F, 1.25F);
    mat4 ref;
    ref_mat4_from_trs(&a, ref);

    nt_anim_mat34_t m;
    nt_anim_mat34_from_mat4((const float *)ref, &m);
    assert_mat34_equals_mat4(&m, ref, 0.0F);
}

/* The fourth row of the mat4 is the implicit [0 0 0 1] and must never be read. */
void test_mat34_from_mat4_ignores_the_fourth_row(void) {
    nt_anim_trs_t a = make_trs(4.0F, -2.0F, 0.5F, 1.0F, 0.0F, 0.0F, 63.0F, 0.75F, 1.0F, 1.25F);
    mat4 ref;
    ref_mat4_from_trs(&a, ref);

    float junk[16];
    memcpy(junk, ref, sizeof(junk));
    junk[3] = 7.5F;
    junk[7] = -13.0F;
    junk[11] = 0.25F;
    junk[15] = -4.0F;

    nt_anim_mat34_t m;
    nt_anim_mat34_from_mat4(junk, &m);
    assert_mat34_equals_mat4(&m, ref, 0.0F);
}

/* ---- FK ---- */

void test_fk_full_rig_matches_cglm_chain(void) {
    nt_anim_trs_t local[ANIM_RIG_JOINT_COUNT];
    memcpy(local, g_rig.bind, sizeof(local));

    nt_anim_mat34_t model[ANIM_RIG_JOINT_COUNT];
    nt_anim_fk(&g_rig.skel, local, model, 0, ANIM_RIG_JOINT_COUNT);

    mat4 ref[ANIM_RIG_JOINT_COUNT];
    ref_fk(local, ref);

    for (uint16_t j = 0; j < ANIM_RIG_JOINT_COUNT; ++j) {
        assert_mat34_equals_mat4(&model[j], ref[j], 1e-5F);
    }
}

void test_fk_transforms_points_as_column_vectors(void) {
    nt_anim_trs_t local[ANIM_RIG_JOINT_COUNT];
    memcpy(local, g_rig.bind, sizeof(local));

    nt_anim_mat34_t model[ANIM_RIG_JOINT_COUNT];
    nt_anim_fk(&g_rig.skel, local, model, 0, ANIM_RIG_JOINT_COUNT);

    /* Sequential cglm application from hand up to its root. */
    vec3 v = {0.0F, 0.3F, 0.0F};
    uint16_t j = JOINT_HAND;
    while (j != NT_ANIM_NO_PARENT) {
        mat4 l;
        ref_mat4_from_trs(&local[j], l);
        vec3 next;
        glm_mat4_mulv3(l, v, 1.0F, next);
        glm_vec3_copy(next, v);
        j = g_rig.skel.parent[j];
    }

    const nt_anim_mat34_t *g = &model[JOINT_HAND];
    for (int i = 0; i < 3; ++i) {
        const float x = (g->r[i][0] * 0.0F) + (g->r[i][1] * 0.3F) + (g->r[i][2] * 0.0F) + g->r[i][3];
        ASSERT_FLOAT_NEAR(v[i], x, 1e-5F);
    }
}

void test_fk_subtree_matches_full_pass(void) {
    nt_anim_trs_t local[ANIM_RIG_JOINT_COUNT];
    nt_anim_pose_rest(&g_rig.skel, local);

    nt_anim_mat34_t model[ANIM_RIG_JOINT_COUNT];
    nt_anim_fk(&g_rig.skel, local, model, 0, ANIM_RIG_JOINT_COUNT);

    nt_anim_mat34_t before[ANIM_RIG_JOINT_COUNT];
    memcpy(before, model, sizeof(before));

    set_axis_angle(&local[JOINT_ARM], 0.0F, 0.0F, 1.0F, 33.0F);
    nt_anim_fk(&g_rig.skel, local, model, JOINT_ARM, 2);

    nt_anim_mat34_t full[ANIM_RIG_JOINT_COUNT];
    nt_anim_fk(&g_rig.skel, local, full, 0, ANIM_RIG_JOINT_COUNT);

    for (int i = 0; i < 3; ++i) {
        for (int c = 0; c < 4; ++c) {
            ASSERT_FLOAT_NEAR(full[JOINT_ARM].r[i][c], model[JOINT_ARM].r[i][c], 1e-6F);
            ASSERT_FLOAT_NEAR(full[JOINT_HAND].r[i][c], model[JOINT_HAND].r[i][c], 1e-6F);
        }
    }

    const uint16_t untouched[] = {JOINT_ROOT_A, JOINT_SPINE, JOINT_HELPER, JOINT_LEG, JOINT_ROOT_B, JOINT_TAIL1, JOINT_TAIL2};
    for (size_t k = 0; k < sizeof(untouched) / sizeof(untouched[0]); ++k) {
        TEST_ASSERT_EQUAL_MEMORY(&before[untouched[k]], &model[untouched[k]], sizeof(nt_anim_mat34_t));
    }
}

void test_fk_root_spanning_range_equals_per_root_ranges(void) {
    nt_anim_trs_t local[ANIM_RIG_JOINT_COUNT];
    memcpy(local, g_rig.bind, sizeof(local));

    nt_anim_mat34_t whole[ANIM_RIG_JOINT_COUNT];
    nt_anim_fk(&g_rig.skel, local, whole, 0, ANIM_RIG_JOINT_COUNT);

    nt_anim_mat34_t split[ANIM_RIG_JOINT_COUNT];
    nt_anim_fk(&g_rig.skel, local, split, 0, 6);
    nt_anim_fk(&g_rig.skel, local, split, 6, 3);

    TEST_ASSERT_EQUAL_MEMORY(whole, split, sizeof(whole));
}

void test_fk_whole_subtree_range_is_valid(void) {
    nt_anim_trs_t local[ANIM_RIG_JOINT_COUNT];
    memcpy(local, g_rig.bind, sizeof(local));

    nt_anim_mat34_t model[ANIM_RIG_JOINT_COUNT];
    nt_anim_fk(&g_rig.skel, local, model, 0, ANIM_RIG_JOINT_COUNT);

    nt_anim_mat34_t again[ANIM_RIG_JOINT_COUNT];
    memcpy(again, model, sizeof(again));
    /* spine's subtree is [1, 6): a range that ends exactly at subtree_end. */
    nt_anim_fk(&g_rig.skel, local, again, JOINT_SPINE, 5);

    TEST_ASSERT_EQUAL_MEMORY(model, again, sizeof(model));
}

#if NT_ASSERT_MODE == NT_ASSERT_FULL
void test_fk_traps_on_invalid_ranges(void) {
    nt_anim_trs_t local[ANIM_RIG_JOINT_COUNT];
    memcpy(local, g_rig.bind, sizeof(local));
    nt_anim_mat34_t model[ANIM_RIG_JOINT_COUNT];

    NT_TEST_EXPECT_ASSERT(nt_anim_fk(&g_rig.skel, local, model, 0, 0));
    NT_TEST_EXPECT_ASSERT(nt_anim_fk(&g_rig.skel, local, model, 7, 3));
    /* [3, 6) leaves arm's subtree, which ends at 5. */
    NT_TEST_EXPECT_ASSERT(nt_anim_fk(&g_rig.skel, local, model, JOINT_ARM, 3));
}
#endif

/* ---- Rest pose ---- */

void test_pose_rest_then_fk_matches_fk_over_rest(void) {
    nt_anim_trs_t local[ANIM_RIG_JOINT_COUNT];
    nt_anim_pose_rest(&g_rig.skel, local);

    nt_anim_mat34_t copied[ANIM_RIG_JOINT_COUNT];
    nt_anim_fk(&g_rig.skel, local, copied, 0, ANIM_RIG_JOINT_COUNT);

    nt_anim_mat34_t direct[ANIM_RIG_JOINT_COUNT];
    nt_anim_fk(&g_rig.skel, g_rig.skel.rest, direct, 0, ANIM_RIG_JOINT_COUNT);

    TEST_ASSERT_EQUAL_MEMORY(direct, copied, sizeof(direct));
}

#if NT_ASSERT_MODE == NT_ASSERT_FULL
void test_pose_rest_traps_on_overlap(void) {
    NT_TEST_EXPECT_ASSERT(nt_anim_pose_rest(&g_rig.skel, (nt_anim_trs_t *)g_rig.skel.rest));

    /* Shifted by one joint inside one buffer: still overlapping, so memcpy would
     * read joints it had already written. */
    nt_anim_trs_t shared[ANIM_RIG_JOINT_COUNT + 1];
    memcpy(&shared[1], g_rig.skel.rest, (size_t)ANIM_RIG_JOINT_COUNT * sizeof(nt_anim_trs_t));
    nt_anim_skeleton_t overlapping = g_rig.skel;
    overlapping.rest = &shared[1];
    NT_TEST_EXPECT_ASSERT(nt_anim_pose_rest(&overlapping, &shared[0]));
}
#endif

/* ---- Sockets ---- */

void test_socket_matches_cglm_composition(void) {
    nt_anim_trs_t local[ANIM_RIG_JOINT_COUNT];
    memcpy(local, g_rig.bind, sizeof(local));

    nt_anim_mat34_t model[ANIM_RIG_JOINT_COUNT];
    nt_anim_fk(&g_rig.skel, local, model, 0, ANIM_RIG_JOINT_COUNT);

    mat4 ref[ANIM_RIG_JOINT_COUNT];
    ref_fk(local, ref);

    nt_anim_trs_t world_trs = make_trs(1.0F, 2.0F, -3.0F, 0.0F, 1.0F, 0.0F, 25.0F, 1.5F, 1.5F, 1.5F);
    mat4 world;
    ref_mat4_from_trs(&world_trs, world);

    nt_anim_trs_t socket_local = make_trs(0.05F, 0.0F, 0.1F, 0.0F, 1.0F, 0.0F, 10.0F, 1.0F, 1.0F, 1.0F);
    mat4 socket4;
    ref_mat4_from_trs(&socket_local, socket4);

    mat4 eg;
    glm_mat4_mul(world, ref[JOINT_HAND], eg);
    mat4 expected;
    glm_mat4_mul(eg, socket4, expected);

    nt_anim_mat34_t out;
    nt_anim_socket((const float *)world, &model[JOINT_HAND], &socket_local, &out);

    assert_mat34_equals_mat4(&out, expected, 1e-5F);
}

/* ---- Rig identity ---- */

enum { RIG_VECTOR_JOINTS = 2, RIG_VECTOR_BYTES = 8 + (46 * RIG_VECTOR_JOINTS) };

typedef struct {
    uint16_t parent[RIG_VECTOR_JOINTS];
    uint16_t subtree_end[RIG_VECTOR_JOINTS];
    uint32_t joint_id[RIG_VECTOR_JOINTS];
    nt_anim_trs_t rest[RIG_VECTOR_JOINTS];
    nt_anim_skeleton_t skel;
} vector_rig_t;

/* The view points into the struct's own arrays, so a copy has to be re-pointed. */
static void vector_rig_rebind(vector_rig_t *r) {
    r->skel.parent = r->parent;
    r->skel.subtree_end = r->subtree_end;
    r->skel.joint_id = r->joint_id;
    r->skel.rest = r->rest;
}

/* Published vector rig: joint 1 carries a -0 translation and a quaternion whose
 * two largest components tie, so both canonicalization rules are exercised. */
static void make_vector_rig(vector_rig_t *r) {
    r->parent[0] = NT_ANIM_NO_PARENT;
    r->parent[1] = 0;
    r->subtree_end[0] = RIG_VECTOR_JOINTS;
    r->subtree_end[1] = RIG_VECTOR_JOINTS;
    r->joint_id[0] = 0x11111111U;
    r->joint_id[1] = 0x22222222U;

    const nt_anim_trs_t j0 = {{1.0F, 2.0F, 3.0F}, {0.0F, 0.0F, 0.0F, 1.0F}, {1.0F, 1.0F, 1.0F}};
    const nt_anim_trs_t j1 = {{0.0F, -0.0F, 0.5F}, {0.0F, 0.0F, -0.70710678F, -0.70710678F}, {1.0F, 1.0F, 1.0F}};
    r->rest[0] = j0;
    r->rest[1] = j1;

    r->skel.rig_compat_id = (nt_hash64_t){0};
    r->skel.joint_count = RIG_VECTOR_JOINTS;
    vector_rig_rebind(r);
}

static uint64_t vector_rig_id(const vector_rig_t *r) {
    uint8_t scratch[RIG_VECTOR_BYTES];
    return nt_anim_rig_compat_id(&r->skel, scratch, (uint32_t)sizeof(scratch)).value;
}

void test_rig_compat_id_size(void) {
    TEST_ASSERT_EQUAL_UINT32(RIG_VECTOR_BYTES, nt_anim_rig_compat_id_size(RIG_VECTOR_JOINTS));
    TEST_ASSERT_EQUAL_UINT32(422U, nt_anim_rig_compat_id_size(9));
    TEST_ASSERT_EQUAL_UINT32(8U, nt_anim_rig_compat_id_size(0));
}

void test_rig_compat_id_published_vector(void) {
    /* Byte table (little-endian, packed):
     *   header  'N' 'R' 'I' 'G' | schema 01 | convention 01 | joint_count 0002
     *   joint 0 id 11111111 | parent FFFF | t 1,2,3 | q 0,0,0,1 | s 1,1,1
     *   joint 1 id 22222222 | parent 0000 | t 0,-0->+0,0.5
     *           | q (0,0,-0.70710678,-0.70710678) -> first largest |c| is z,
     *             negative, so all four flip -> (0,0,+0.70710678,+0.70710678)
     *           | s 1,1,1 */
    static const uint8_t expected[RIG_VECTOR_BYTES] = {
        0x4E, 0x52, 0x49, 0x47, 0x01, 0x01, 0x02, 0x00,                                                 /* header */
        0x11, 0x11, 0x11, 0x11, 0xFF, 0xFF,                                                             /* joint 0: id, parent */
        0x00, 0x00, 0x80, 0x3F, 0x00, 0x00, 0x00, 0x40, 0x00, 0x00, 0x40, 0x40,                         /* t 1, 2, 3 */
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x3F, /* q 0, 0, 0, 1 */
        0x00, 0x00, 0x80, 0x3F, 0x00, 0x00, 0x80, 0x3F, 0x00, 0x00, 0x80, 0x3F,                         /* s 1, 1, 1 */
        0x22, 0x22, 0x22, 0x22, 0x00, 0x00,                                                             /* joint 1: id, parent */
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3F,                         /* t 0, +0, 0.5 */
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF3, 0x04, 0x35, 0x3F, 0xF3, 0x04, 0x35, 0x3F, /* q 0, 0, +0.7071, +0.7071 */
        0x00, 0x00, 0x80, 0x3F, 0x00, 0x00, 0x80, 0x3F, 0x00, 0x00, 0x80, 0x3F,                         /* s 1, 1, 1 */
    };

    vector_rig_t rig;
    make_vector_rig(&rig);

    uint8_t scratch[RIG_VECTOR_BYTES];
    const nt_hash64_t id = nt_anim_rig_compat_id(&rig.skel, scratch, (uint32_t)sizeof(scratch));

    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, scratch, RIG_VECTOR_BYTES);
    TEST_ASSERT_EQUAL_HEX64(0x03E59E1475239034ULL, id.value);
}

void test_rig_compat_id_ignores_quaternion_sign(void) {
    vector_rig_t rig;
    make_vector_rig(&rig);
    const uint64_t id = vector_rig_id(&rig);

    for (int c = 0; c < 4; ++c) {
        rig.rest[1].q[c] = -rig.rest[1].q[c];
    }
    TEST_ASSERT_EQUAL_HEX64(id, vector_rig_id(&rig));
}

/* The tie rule picks the FIRST maximum in x, y, z, w order, so a |z| == |w| tie
 * is decided by z's sign and w takes the flip. */
void test_rig_compat_id_tie_break_follows_the_earlier_component(void) {
    /* header 8 + joint 0's 46 + joint 1's id 4 + parent 2 + t 12. */
    enum { JOINT1_Q_OFFSET = 8 + 46 + 4 + 2 + 12 };
    static const uint8_t expected_q[16] = {
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* x, y = 0 */
        0xF3, 0x04, 0x35, 0x3F,                         /* z = +0.70710678 */
        0xF3, 0x04, 0x35, 0xBF,                         /* w = -0.70710678 */
    };

    vector_rig_t rig;
    make_vector_rig(&rig);
    rig.rest[1].q[2] = -0.70710678F;
    rig.rest[1].q[3] = 0.70710678F;

    uint8_t scratch[RIG_VECTOR_BYTES];
    (void)nt_anim_rig_compat_id(&rig.skel, scratch, (uint32_t)sizeof(scratch));

    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected_q, &scratch[JOINT1_Q_OFFSET], sizeof(expected_q));
}

/* An identity quaternion written as (0, 0, 0, -1) is the same rotation; the -0
 * the flip produces in x, y, z must canonicalize away too. */
void test_rig_compat_id_ignores_negated_identity_quaternion(void) {
    vector_rig_t rig;
    make_vector_rig(&rig);
    const uint64_t id = vector_rig_id(&rig);

    rig.rest[0].q[3] = -1.0F;
    TEST_ASSERT_EQUAL_HEX64(id, vector_rig_id(&rig));
}

void test_rig_compat_id_of_an_empty_rig_is_the_header(void) {
    static const uint8_t header[8] = {0x4E, 0x52, 0x49, 0x47, 0x01, 0x01, 0x00, 0x00};

    vector_rig_t rig;
    make_vector_rig(&rig);
    rig.skel.joint_count = 0;

    uint8_t scratch[8];
    const nt_hash64_t id = nt_anim_rig_compat_id(&rig.skel, scratch, (uint32_t)sizeof(scratch));

    TEST_ASSERT_EQUAL_HEX8_ARRAY(header, scratch, sizeof(header));
    TEST_ASSERT_EQUAL_HEX64(nt_hash64(header, sizeof(header)).value, id.value);
}

void test_rig_compat_id_ignores_negative_zero(void) {
    vector_rig_t rig;
    make_vector_rig(&rig);
    const uint64_t id = vector_rig_id(&rig);

    rig.rest[1].t[1] = 0.0F;
    TEST_ASSERT_EQUAL_HEX64(id, vector_rig_id(&rig));
}

void test_rig_compat_id_changes_with_the_rig(void) {
    vector_rig_t base;
    make_vector_rig(&base);
    const uint64_t id = vector_rig_id(&base);

    vector_rig_t rig = base;
    vector_rig_rebind(&rig);
    rig.joint_id[1] = 0x22222223U;
    TEST_ASSERT_NOT_EQUAL_UINT64(id, vector_rig_id(&rig));

    rig = base;
    vector_rig_rebind(&rig);
    rig.parent[1] = NT_ANIM_NO_PARENT;
    TEST_ASSERT_NOT_EQUAL_UINT64(id, vector_rig_id(&rig));

    rig = base;
    vector_rig_rebind(&rig);
    rig.rest[0].t[2] = 3.0001F;
    TEST_ASSERT_NOT_EQUAL_UINT64(id, vector_rig_id(&rig));

    rig = base;
    vector_rig_rebind(&rig);
    rig.skel.joint_count = 1;
    TEST_ASSERT_NOT_EQUAL_UINT64(id, vector_rig_id(&rig));
}

#if NT_ASSERT_MODE == NT_ASSERT_FULL
void test_rig_compat_id_traps_on_small_scratch(void) {
    vector_rig_t rig;
    make_vector_rig(&rig);
    uint8_t scratch[RIG_VECTOR_BYTES];

    NT_TEST_EXPECT_ASSERT(nt_anim_rig_compat_id(&rig.skel, scratch, RIG_VECTOR_BYTES - 1));
}
#endif

/* ---- Per-element checks ---- */

#if NT_ANIM_CHECKS && (NT_ASSERT_MODE != NT_ASSERT_OFF)
void test_fk_traps_on_non_finite_translation(void) {
    nt_anim_trs_t local[ANIM_RIG_JOINT_COUNT];
    memcpy(local, g_rig.bind, sizeof(local));
    nt_anim_mat34_t model[ANIM_RIG_JOINT_COUNT];

    local[JOINT_HELPER].t[0] = NAN;
    NT_TEST_EXPECT_ASSERT(nt_anim_fk(&g_rig.skel, local, model, 0, ANIM_RIG_JOINT_COUNT));
}

void test_fk_traps_on_non_unit_quaternion(void) {
    nt_anim_trs_t local[ANIM_RIG_JOINT_COUNT];
    memcpy(local, g_rig.bind, sizeof(local));
    nt_anim_mat34_t model[ANIM_RIG_JOINT_COUNT];

    local[JOINT_HELPER].q[0] = 0.0F;
    local[JOINT_HELPER].q[1] = 0.0F;
    local[JOINT_HELPER].q[2] = 0.0F;
    local[JOINT_HELPER].q[3] = 2.0F;
    NT_TEST_EXPECT_ASSERT(nt_anim_fk(&g_rig.skel, local, model, 0, ANIM_RIG_JOINT_COUNT));
}

/* Preorder is what lets FK write model[j] while it reads model[parent[j]]. */
void test_fk_traps_on_forward_parent(void) {
    nt_anim_trs_t local[ANIM_RIG_JOINT_COUNT];
    memcpy(local, g_rig.bind, sizeof(local));
    nt_anim_mat34_t model[ANIM_RIG_JOINT_COUNT];

    uint16_t parent[ANIM_RIG_JOINT_COUNT];
    memcpy(parent, g_rig.skel.parent, sizeof(parent));
    parent[JOINT_ARM] = JOINT_HAND;

    nt_anim_skeleton_t broken = g_rig.skel;
    broken.parent = parent;
    NT_TEST_EXPECT_ASSERT(nt_anim_fk(&broken, local, model, 0, ANIM_RIG_JOINT_COUNT));
}

void test_mat34_mul_traps_on_alias(void) {
    nt_anim_trs_t a = make_trs(0.3F, -1.2F, 2.0F, 0.0F, 1.0F, 0.0F, 37.0F, 1.0F, 2.5F, 0.4F);
    nt_anim_mat34_t ma;
    nt_anim_mat34_from_trs(&a, &ma);
    nt_anim_mat34_t mb;
    nt_anim_mat34_from_trs(&a, &mb);

    NT_TEST_EXPECT_ASSERT(nt_anim_mat34_mul(&ma, &mb, &ma));
    NT_TEST_EXPECT_ASSERT(nt_anim_mat34_mul(&ma, &mb, &mb));
}

void test_socket_traps_on_non_unit_quaternion(void) {
    nt_anim_trs_t local[ANIM_RIG_JOINT_COUNT];
    memcpy(local, g_rig.bind, sizeof(local));
    nt_anim_mat34_t model[ANIM_RIG_JOINT_COUNT];
    nt_anim_fk(&g_rig.skel, local, model, 0, ANIM_RIG_JOINT_COUNT);

    nt_anim_trs_t world_trs = make_trs(1.0F, 2.0F, -3.0F, 0.0F, 1.0F, 0.0F, 25.0F, 1.0F, 1.0F, 1.0F);
    mat4 world;
    ref_mat4_from_trs(&world_trs, world);

    nt_anim_trs_t socket_local = make_trs(0.05F, 0.0F, 0.1F, 0.0F, 1.0F, 0.0F, 10.0F, 1.0F, 1.0F, 1.0F);
    socket_local.q[3] = 2.0F;

    nt_anim_mat34_t out;
    NT_TEST_EXPECT_ASSERT(nt_anim_socket((const float *)world, &model[JOINT_HAND], &socket_local, &out));
}
#endif

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_mat34_from_trs_matches_cglm);
    RUN_TEST(test_mat34_mul_matches_cglm);
    RUN_TEST(test_mat34_from_mat4_takes_top_three_rows);
    RUN_TEST(test_mat34_from_mat4_ignores_the_fourth_row);
    RUN_TEST(test_fk_full_rig_matches_cglm_chain);
    RUN_TEST(test_fk_transforms_points_as_column_vectors);
    RUN_TEST(test_fk_subtree_matches_full_pass);
    RUN_TEST(test_fk_root_spanning_range_equals_per_root_ranges);
    RUN_TEST(test_fk_whole_subtree_range_is_valid);
    RUN_TEST(test_pose_rest_then_fk_matches_fk_over_rest);
    RUN_TEST(test_socket_matches_cglm_composition);
    RUN_TEST(test_rig_compat_id_size);
    RUN_TEST(test_rig_compat_id_published_vector);
    RUN_TEST(test_rig_compat_id_ignores_quaternion_sign);
    RUN_TEST(test_rig_compat_id_tie_break_follows_the_earlier_component);
    RUN_TEST(test_rig_compat_id_ignores_negated_identity_quaternion);
    RUN_TEST(test_rig_compat_id_of_an_empty_rig_is_the_header);
    RUN_TEST(test_rig_compat_id_ignores_negative_zero);
    RUN_TEST(test_rig_compat_id_changes_with_the_rig);
#if NT_ASSERT_MODE == NT_ASSERT_FULL
    RUN_TEST(test_fk_traps_on_invalid_ranges);
    RUN_TEST(test_pose_rest_traps_on_overlap);
    RUN_TEST(test_rig_compat_id_traps_on_small_scratch);
#endif
#if NT_ANIM_CHECKS && (NT_ASSERT_MODE != NT_ASSERT_OFF)
    RUN_TEST(test_fk_traps_on_non_finite_translation);
    RUN_TEST(test_fk_traps_on_non_unit_quaternion);
    RUN_TEST(test_fk_traps_on_forward_parent);
    RUN_TEST(test_mat34_mul_traps_on_alias);
    RUN_TEST(test_socket_traps_on_non_unit_quaternion);
#endif
    return UNITY_END();
}
