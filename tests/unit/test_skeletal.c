/* System and engine headers before Unity: <stdnoreturn.h> and the Windows SDK
 * clash over __declspec(noreturn) in the other order. */
#include <math.h>
#include <stddef.h>
#include <string.h>

#include "math/nt_math.h"
#include "skeletal/nt_skeletal.h"
#include "test_helpers/skeletal_rig.h"

#include "unity.h"

#include "test_helpers/nt_assert_trap.h"

/* Unity is built with UNITY_EXCLUDE_FLOAT, so float comparisons go through
 * fabsf like everywhere else in the suite. */
#define ASSERT_FLOAT_NEAR(expected, actual, tol) TEST_ASSERT_TRUE_MESSAGE(fabsf((expected) - (actual)) <= (tol), "float not within tolerance")

enum { JOINT_ROOT_A = 0, JOINT_SPINE = 1, JOINT_HELPER = 2, JOINT_ARM = 3, JOINT_HAND = 4, JOINT_LEG = 5, JOINT_ROOT_B = 6, JOINT_TAIL1 = 7, JOINT_TAIL2 = 8 };

static skeletal_rig_t g_rig;

void setUp(void) { skeletal_rig_asymmetric(&g_rig); }

void tearDown(void) {}

static nt_skeletal_trs_t make_trs(float tx, float ty, float tz, float ax, float ay, float az, float degrees, float sx, float sy, float sz) {
    nt_skeletal_trs_t o;
    o.t[0] = tx;
    o.t[1] = ty;
    o.t[2] = tz;
    o.s[0] = sx;
    o.s[1] = sy;
    o.s[2] = sz;
    skeletal_rig_set_axis_angle(&o, ax, ay, az, degrees);
    return o;
}

/* ---- Kernels against cglm ---- */

void test_mat34_from_trs_matches_cglm(void) {
    nt_skeletal_trs_t local[SKELETAL_RIG_JOINT_COUNT];
    memcpy(local, g_rig.skel.rest, sizeof(local));

    for (uint16_t j = 0; j < SKELETAL_RIG_JOINT_COUNT; ++j) {
        mat4 ref;
        skeletal_rig_ref_mat4_from_trs(&local[j], ref);
        nt_skeletal_mat34_t m;
        nt_skeletal_mat34_from_trs(&local[j], &m);
        skeletal_rig_assert_mat34_equals_mat4(&m, ref, 1e-5F);

        mat4 ref_bind;
        skeletal_rig_ref_mat4_from_trs(&g_rig.bind[j], ref_bind);
        nt_skeletal_mat34_t m_bind;
        nt_skeletal_mat34_from_trs(&g_rig.bind[j], &m_bind);
        skeletal_rig_assert_mat34_equals_mat4(&m_bind, ref_bind, 1e-5F);
    }
}

void test_mat34_mul_matches_cglm(void) {
    nt_skeletal_trs_t a = make_trs(0.3F, -1.2F, 2.0F, 0.0F, 1.0F, 0.0F, 37.0F, 1.0F, 2.5F, 0.4F);
    nt_skeletal_trs_t b = make_trs(-0.7F, 0.9F, 0.15F, 0.0F, 0.0F, 1.0F, -112.0F, 1.7F, 1.0F, 0.6F);

    mat4 ra;
    skeletal_rig_ref_mat4_from_trs(&a, ra);
    mat4 rb;
    skeletal_rig_ref_mat4_from_trs(&b, rb);
    mat4 rab;
    glm_mat4_mul(ra, rb, rab);

    nt_skeletal_mat34_t ma;
    nt_skeletal_mat34_from_trs(&a, &ma);
    nt_skeletal_mat34_t mb;
    nt_skeletal_mat34_from_trs(&b, &mb);
    nt_skeletal_mat34_t mab;
    nt_skeletal_mat34_mul(&ma, &mb, &mab);

    skeletal_rig_assert_mat34_equals_mat4(&mab, rab, 1e-5F);
}

void test_mat34_from_mat4_takes_top_three_rows(void) {
    nt_skeletal_trs_t a = make_trs(4.0F, -2.0F, 0.5F, 1.0F, 0.0F, 0.0F, 63.0F, 0.75F, 1.0F, 1.25F);
    mat4 ref;
    skeletal_rig_ref_mat4_from_trs(&a, ref);

    nt_skeletal_mat34_t m;
    nt_skeletal_mat34_from_mat4((const float *)ref, &m);
    skeletal_rig_assert_mat34_equals_mat4(&m, ref, 0.0F);
}

/* The fourth row of the mat4 is the implicit [0 0 0 1] and must never be read. */
void test_mat34_from_mat4_ignores_the_fourth_row(void) {
    nt_skeletal_trs_t a = make_trs(4.0F, -2.0F, 0.5F, 1.0F, 0.0F, 0.0F, 63.0F, 0.75F, 1.0F, 1.25F);
    mat4 ref;
    skeletal_rig_ref_mat4_from_trs(&a, ref);

    float junk[16];
    memcpy(junk, ref, sizeof(junk));
    junk[3] = 7.5F;
    junk[7] = -13.0F;
    junk[11] = 0.25F;
    junk[15] = -4.0F;

    nt_skeletal_mat34_t m;
    nt_skeletal_mat34_from_mat4(junk, &m);
    skeletal_rig_assert_mat34_equals_mat4(&m, ref, 0.0F);
}

/* ---- FK ---- */

void test_fk_full_rig_matches_cglm_chain(void) {
    nt_skeletal_trs_t local[SKELETAL_RIG_JOINT_COUNT];
    memcpy(local, g_rig.bind, sizeof(local));

    nt_skeletal_mat34_t model[SKELETAL_RIG_JOINT_COUNT];
    nt_skeletal_fk(&g_rig.skel, local, model, 0, SKELETAL_RIG_JOINT_COUNT);

    mat4 ref[SKELETAL_RIG_JOINT_COUNT];
    skeletal_rig_ref_fk(local, ref);

    for (uint16_t j = 0; j < SKELETAL_RIG_JOINT_COUNT; ++j) {
        skeletal_rig_assert_mat34_equals_mat4(&model[j], ref[j], 1e-5F);
    }
}

void test_fk_transforms_points_as_column_vectors(void) {
    nt_skeletal_trs_t local[SKELETAL_RIG_JOINT_COUNT];
    memcpy(local, g_rig.bind, sizeof(local));

    nt_skeletal_mat34_t model[SKELETAL_RIG_JOINT_COUNT];
    nt_skeletal_fk(&g_rig.skel, local, model, 0, SKELETAL_RIG_JOINT_COUNT);

    /* Sequential cglm application from hand up to its root. */
    vec3 v = {0.0F, 0.3F, 0.0F};
    uint16_t j = JOINT_HAND;
    while (j != NT_SKELETAL_NO_PARENT) {
        mat4 l;
        skeletal_rig_ref_mat4_from_trs(&local[j], l);
        vec3 next;
        glm_mat4_mulv3(l, v, 1.0F, next);
        glm_vec3_copy(next, v);
        j = g_rig.skel.parent[j];
    }

    const nt_skeletal_mat34_t *g = &model[JOINT_HAND];
    for (int i = 0; i < 3; ++i) {
        const float x = (g->r[i][0] * 0.0F) + (g->r[i][1] * 0.3F) + (g->r[i][2] * 0.0F) + g->r[i][3];
        ASSERT_FLOAT_NEAR(v[i], x, 1e-5F);
    }
}

void test_fk_subtree_matches_full_pass(void) {
    nt_skeletal_trs_t local[SKELETAL_RIG_JOINT_COUNT];
    memcpy(local, g_rig.skel.rest, sizeof(local));

    nt_skeletal_mat34_t model[SKELETAL_RIG_JOINT_COUNT];
    nt_skeletal_fk(&g_rig.skel, local, model, 0, SKELETAL_RIG_JOINT_COUNT);

    nt_skeletal_mat34_t before[SKELETAL_RIG_JOINT_COUNT];
    memcpy(before, model, sizeof(before));

    skeletal_rig_set_axis_angle(&local[JOINT_ARM], 0.0F, 0.0F, 1.0F, 33.0F);
    nt_skeletal_fk(&g_rig.skel, local, model, JOINT_ARM, 2);

    nt_skeletal_mat34_t full[SKELETAL_RIG_JOINT_COUNT];
    nt_skeletal_fk(&g_rig.skel, local, full, 0, SKELETAL_RIG_JOINT_COUNT);

    for (int i = 0; i < 3; ++i) {
        for (int c = 0; c < 4; ++c) {
            ASSERT_FLOAT_NEAR(full[JOINT_ARM].r[i][c], model[JOINT_ARM].r[i][c], 1e-6F);
            ASSERT_FLOAT_NEAR(full[JOINT_HAND].r[i][c], model[JOINT_HAND].r[i][c], 1e-6F);
        }
    }

    const uint16_t untouched[] = {JOINT_ROOT_A, JOINT_SPINE, JOINT_HELPER, JOINT_LEG, JOINT_ROOT_B, JOINT_TAIL1, JOINT_TAIL2};
    for (size_t k = 0; k < sizeof(untouched) / sizeof(untouched[0]); ++k) {
        TEST_ASSERT_EQUAL_MEMORY(&before[untouched[k]], &model[untouched[k]], sizeof(nt_skeletal_mat34_t));
    }
}

void test_fk_root_spanning_range_equals_per_root_ranges(void) {
    nt_skeletal_trs_t local[SKELETAL_RIG_JOINT_COUNT];
    memcpy(local, g_rig.bind, sizeof(local));

    nt_skeletal_mat34_t whole[SKELETAL_RIG_JOINT_COUNT];
    nt_skeletal_fk(&g_rig.skel, local, whole, 0, SKELETAL_RIG_JOINT_COUNT);

    nt_skeletal_mat34_t split[SKELETAL_RIG_JOINT_COUNT];
    nt_skeletal_fk(&g_rig.skel, local, split, 0, 6);
    nt_skeletal_fk(&g_rig.skel, local, split, 6, 3);

    TEST_ASSERT_EQUAL_MEMORY(whole, split, sizeof(whole));
}

/* A partial pass must leave exactly the full pass behind: the range is poisoned
 * first, so an untouched joint fails instead of passing on a stale value. */
static void fk_partial_matches_full(uint16_t first, uint16_t count) {
    nt_skeletal_trs_t local[SKELETAL_RIG_JOINT_COUNT];
    memcpy(local, g_rig.bind, sizeof(local));

    nt_skeletal_mat34_t full[SKELETAL_RIG_JOINT_COUNT];
    nt_skeletal_fk(&g_rig.skel, local, full, 0, SKELETAL_RIG_JOINT_COUNT);

    nt_skeletal_mat34_t model[SKELETAL_RIG_JOINT_COUNT];
    memcpy(model, full, sizeof(model));
    memset(&model[first], 0x5A, (size_t)count * sizeof(nt_skeletal_mat34_t));

    nt_skeletal_fk(&g_rig.skel, local, model, first, count);
    TEST_ASSERT_EQUAL_MEMORY(full, model, sizeof(full));
}

void test_fk_single_root_range_equals_full_pass(void) { fk_partial_matches_full(JOINT_ROOT_B, 1); }

/* A leaf alone, inside a subtree whose parent the full pass already made current. */
void test_fk_leaf_range_equals_full_pass(void) { fk_partial_matches_full(JOINT_HAND, 1); }

/* [0, 3) starts at a root and stops inside spine's subtree: legal because the
 * range spans no parent it has not computed. */
void test_fk_root_range_ending_mid_subtree_equals_full_pass(void) { fk_partial_matches_full(0, 3); }

void test_fk_whole_subtree_range_is_valid(void) {
    nt_skeletal_trs_t local[SKELETAL_RIG_JOINT_COUNT];
    memcpy(local, g_rig.bind, sizeof(local));

    nt_skeletal_mat34_t model[SKELETAL_RIG_JOINT_COUNT];
    nt_skeletal_fk(&g_rig.skel, local, model, 0, SKELETAL_RIG_JOINT_COUNT);

    nt_skeletal_mat34_t again[SKELETAL_RIG_JOINT_COUNT];
    memcpy(again, model, sizeof(again));
    /* spine's subtree is [1, 6): a range that ends exactly at subtree_end. */
    nt_skeletal_fk(&g_rig.skel, local, again, JOINT_SPINE, 5);

    TEST_ASSERT_EQUAL_MEMORY(model, again, sizeof(model));
}

#if NT_ASSERT_MODE == NT_ASSERT_FULL
void test_fk_traps_on_invalid_ranges(void) {
    nt_skeletal_trs_t local[SKELETAL_RIG_JOINT_COUNT];
    memcpy(local, g_rig.bind, sizeof(local));
    nt_skeletal_mat34_t model[SKELETAL_RIG_JOINT_COUNT];

    NT_TEST_EXPECT_ASSERT(nt_skeletal_fk(&g_rig.skel, local, model, 0, 0));
    NT_TEST_EXPECT_ASSERT(nt_skeletal_fk(&g_rig.skel, local, model, 7, 3));
    /* [3, 6) leaves arm's subtree, which ends at 5. */
    NT_TEST_EXPECT_ASSERT(nt_skeletal_fk(&g_rig.skel, local, model, JOINT_ARM, 3));
}

/* One buffer read as both poses: writing model[j] would overwrite locals that
 * later joints still have to read. */
void test_fk_traps_on_overlap(void) {
    union {
        nt_skeletal_trs_t local[SKELETAL_RIG_JOINT_COUNT];
        nt_skeletal_mat34_t model[SKELETAL_RIG_JOINT_COUNT];
    } shared;
    memcpy(shared.local, g_rig.bind, sizeof(shared.local));

    NT_TEST_EXPECT_ASSERT(nt_skeletal_fk(&g_rig.skel, shared.local, shared.model, 0, SKELETAL_RIG_JOINT_COUNT));
}

#endif

/* ---- Rig identity ---- */

enum { RIG_VECTOR_JOINTS = 2, RIG_VECTOR_BYTES = 8 + (46 * RIG_VECTOR_JOINTS) };

typedef struct {
    uint16_t parent[RIG_VECTOR_JOINTS];
    uint16_t subtree_end[RIG_VECTOR_JOINTS];
    uint32_t joint_id[RIG_VECTOR_JOINTS];
    nt_skeletal_trs_t rest[RIG_VECTOR_JOINTS];
    nt_skeletal_skeleton_t skel;
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
    r->parent[0] = NT_SKELETAL_NO_PARENT;
    r->parent[1] = 0;
    r->subtree_end[0] = RIG_VECTOR_JOINTS;
    r->subtree_end[1] = RIG_VECTOR_JOINTS;
    r->joint_id[0] = 0x11111111U;
    r->joint_id[1] = 0x22222222U;

    const nt_skeletal_trs_t j0 = {{1.0F, 2.0F, 3.0F}, {0.0F, 0.0F, 0.0F, 1.0F}, {1.0F, 1.0F, 1.0F}};
    const nt_skeletal_trs_t j1 = {{0.0F, -0.0F, 0.5F}, {0.0F, 0.0F, -0.70710678F, -0.70710678F}, {1.0F, 1.0F, 1.0F}};
    r->rest[0] = j0;
    r->rest[1] = j1;

    r->skel.rig_compat_id = (nt_hash64_t){0};
    r->skel.joint_count = RIG_VECTOR_JOINTS;
    vector_rig_rebind(r);
}

static uint64_t vector_rig_id(const vector_rig_t *r) {
    uint8_t scratch[RIG_VECTOR_BYTES];
    return nt_skeletal_rig_compat_id(&r->skel, scratch, (uint32_t)sizeof(scratch)).value;
}

void test_rig_id_bytes(void) {
    TEST_ASSERT_EQUAL_UINT32(8U, NT_SKELETAL_RIG_ID_BYTES(0));
    TEST_ASSERT_EQUAL_UINT32(422U, NT_SKELETAL_RIG_ID_BYTES(9));
    TEST_ASSERT_EQUAL_UINT32(3014618U, NT_SKELETAL_RIG_ID_BYTES(UINT16_MAX));
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
    const nt_hash64_t id = nt_skeletal_rig_compat_id(&rig.skel, scratch, (uint32_t)sizeof(scratch));

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
    (void)nt_skeletal_rig_compat_id(&rig.skel, scratch, (uint32_t)sizeof(scratch));

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
    const nt_hash64_t id = nt_skeletal_rig_compat_id(&rig.skel, scratch, (uint32_t)sizeof(scratch));

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
    rig.parent[1] = NT_SKELETAL_NO_PARENT;
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

    NT_TEST_EXPECT_ASSERT(nt_skeletal_rig_compat_id(&rig.skel, scratch, RIG_VECTOR_BYTES - 1));
}

/* A non-finite rest value has no canonical binary32 form, so the id would not
 * identify the data it was hashed from. */
void test_rig_compat_id_traps_on_non_finite_rest(void) {
    vector_rig_t rig;
    make_vector_rig(&rig);
    rig.rest[1].t[0] = NAN;
    uint8_t scratch[RIG_VECTOR_BYTES];

    NT_TEST_EXPECT_ASSERT(nt_skeletal_rig_compat_id(&rig.skel, scratch, (uint32_t)sizeof(scratch)));
}

/* Preorder is what lets FK write model[j] while it reads model[parent[j]]. */
void test_fk_traps_on_forward_parent(void) {
    nt_skeletal_trs_t local[SKELETAL_RIG_JOINT_COUNT];
    memcpy(local, g_rig.bind, sizeof(local));
    nt_skeletal_mat34_t model[SKELETAL_RIG_JOINT_COUNT];

    uint16_t parent[SKELETAL_RIG_JOINT_COUNT];
    memcpy(parent, g_rig.skel.parent, sizeof(parent));
    parent[JOINT_ARM] = JOINT_HAND;

    nt_skeletal_skeleton_t broken = g_rig.skel;
    broken.parent = parent;
    NT_TEST_EXPECT_ASSERT(nt_skeletal_fk(&broken, local, model, 0, SKELETAL_RIG_JOINT_COUNT));
}

void test_mat34_mul_traps_on_alias(void) {
    nt_skeletal_trs_t a = make_trs(0.3F, -1.2F, 2.0F, 0.0F, 1.0F, 0.0F, 37.0F, 1.0F, 2.5F, 0.4F);
    nt_skeletal_mat34_t ma;
    nt_skeletal_mat34_from_trs(&a, &ma);
    nt_skeletal_mat34_t mb;
    nt_skeletal_mat34_from_trs(&a, &mb);

    NT_TEST_EXPECT_ASSERT(nt_skeletal_mat34_mul(&ma, &mb, &ma));
    NT_TEST_EXPECT_ASSERT(nt_skeletal_mat34_mul(&ma, &mb, &mb));
}
#endif

/* ---- Numerical checks ---- */

#if NT_SKELETAL_CHECKS && (NT_ASSERT_MODE == NT_ASSERT_FULL)
void test_mat34_from_trs_traps_on_invalid_quaternion(void) {
    const float invalid[] = {0.0F, 2.0F, NAN, INFINITY};
    for (size_t i = 0; i < sizeof invalid / sizeof invalid[0]; ++i) {
        nt_skeletal_trs_t trs = {{0.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 0.0F, invalid[i]}, {1.0F, 1.0F, 1.0F}};
        nt_skeletal_mat34_t out;
        NT_TEST_EXPECT_ASSERT(nt_skeletal_mat34_from_trs(&trs, &out));
    }
}

void test_mat34_from_trs_traps_on_non_finite_translation(void) {
    const float invalid[] = {NAN, INFINITY, -INFINITY};
    for (size_t i = 0; i < sizeof invalid / sizeof invalid[0]; ++i) {
        for (int c = 0; c < 3; ++c) {
            nt_skeletal_trs_t trs = {{0.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 0.0F, 1.0F}, {1.0F, 1.0F, 1.0F}};
            trs.t[c] = invalid[i];
            nt_skeletal_mat34_t out;
            NT_TEST_EXPECT_ASSERT(nt_skeletal_mat34_from_trs(&trs, &out));
        }
    }
}

void test_mat34_from_trs_traps_on_non_finite_scale(void) {
    const float invalid[] = {NAN, INFINITY, -INFINITY};
    for (size_t i = 0; i < sizeof invalid / sizeof invalid[0]; ++i) {
        for (int c = 0; c < 3; ++c) {
            nt_skeletal_trs_t trs = {{0.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 0.0F, 1.0F}, {1.0F, 1.0F, 1.0F}};
            trs.s[c] = invalid[i];
            nt_skeletal_mat34_t out;
            NT_TEST_EXPECT_ASSERT(nt_skeletal_mat34_from_trs(&trs, &out));
        }
    }
}

void test_fk_traps_on_non_finite_translation(void) {
    nt_skeletal_trs_t local[SKELETAL_RIG_JOINT_COUNT];
    memcpy(local, g_rig.bind, sizeof(local));
    nt_skeletal_mat34_t model[SKELETAL_RIG_JOINT_COUNT];

    local[JOINT_HELPER].t[0] = NAN;
    NT_TEST_EXPECT_ASSERT(nt_skeletal_fk(&g_rig.skel, local, model, 0, SKELETAL_RIG_JOINT_COUNT));
}

void test_fk_traps_on_infinite_translation(void) {
    nt_skeletal_trs_t local[SKELETAL_RIG_JOINT_COUNT];
    memcpy(local, g_rig.bind, sizeof(local));
    nt_skeletal_mat34_t model[SKELETAL_RIG_JOINT_COUNT];

    local[JOINT_HELPER].t[2] = INFINITY;
    NT_TEST_EXPECT_ASSERT(nt_skeletal_fk(&g_rig.skel, local, model, 0, SKELETAL_RIG_JOINT_COUNT));
}

void test_fk_traps_on_infinite_scale(void) {
    nt_skeletal_trs_t local[SKELETAL_RIG_JOINT_COUNT];
    memcpy(local, g_rig.bind, sizeof(local));
    nt_skeletal_mat34_t model[SKELETAL_RIG_JOINT_COUNT];

    local[JOINT_HELPER].s[1] = INFINITY;
    NT_TEST_EXPECT_ASSERT(nt_skeletal_fk(&g_rig.skel, local, model, 0, SKELETAL_RIG_JOINT_COUNT));
}

/* An all-zero quaternion is the other side of the unit check from an oversized
 * one, and the one a zeroed pose buffer produces. */
void test_fk_traps_on_zero_quaternion(void) {
    nt_skeletal_trs_t local[SKELETAL_RIG_JOINT_COUNT];
    memcpy(local, g_rig.bind, sizeof(local));
    nt_skeletal_mat34_t model[SKELETAL_RIG_JOINT_COUNT];

    memset(local[JOINT_HELPER].q, 0, sizeof(local[JOINT_HELPER].q));
    NT_TEST_EXPECT_ASSERT(nt_skeletal_fk(&g_rig.skel, local, model, 0, SKELETAL_RIG_JOINT_COUNT));
}

void test_fk_traps_on_non_unit_quaternion(void) {
    nt_skeletal_trs_t local[SKELETAL_RIG_JOINT_COUNT];
    memcpy(local, g_rig.bind, sizeof(local));
    nt_skeletal_mat34_t model[SKELETAL_RIG_JOINT_COUNT];

    local[JOINT_HELPER].q[0] = 0.0F;
    local[JOINT_HELPER].q[1] = 0.0F;
    local[JOINT_HELPER].q[2] = 0.0F;
    local[JOINT_HELPER].q[3] = 2.0F;
    NT_TEST_EXPECT_ASSERT(nt_skeletal_fk(&g_rig.skel, local, model, 0, SKELETAL_RIG_JOINT_COUNT));
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
    RUN_TEST(test_fk_single_root_range_equals_full_pass);
    RUN_TEST(test_fk_leaf_range_equals_full_pass);
    RUN_TEST(test_fk_root_range_ending_mid_subtree_equals_full_pass);
    RUN_TEST(test_rig_id_bytes);
    RUN_TEST(test_rig_compat_id_published_vector);
    RUN_TEST(test_rig_compat_id_ignores_quaternion_sign);
    RUN_TEST(test_rig_compat_id_tie_break_follows_the_earlier_component);
    RUN_TEST(test_rig_compat_id_ignores_negated_identity_quaternion);
    RUN_TEST(test_rig_compat_id_of_an_empty_rig_is_the_header);
    RUN_TEST(test_rig_compat_id_ignores_negative_zero);
    RUN_TEST(test_rig_compat_id_changes_with_the_rig);
#if NT_ASSERT_MODE == NT_ASSERT_FULL
    RUN_TEST(test_fk_traps_on_invalid_ranges);
    RUN_TEST(test_fk_traps_on_overlap);
    RUN_TEST(test_rig_compat_id_traps_on_small_scratch);
    RUN_TEST(test_rig_compat_id_traps_on_non_finite_rest);
    RUN_TEST(test_mat34_mul_traps_on_alias);
    RUN_TEST(test_fk_traps_on_forward_parent);
#endif
#if NT_SKELETAL_CHECKS && (NT_ASSERT_MODE == NT_ASSERT_FULL)
    RUN_TEST(test_mat34_from_trs_traps_on_invalid_quaternion);
    RUN_TEST(test_mat34_from_trs_traps_on_non_finite_translation);
    RUN_TEST(test_mat34_from_trs_traps_on_non_finite_scale);
    RUN_TEST(test_fk_traps_on_non_finite_translation);
    RUN_TEST(test_fk_traps_on_infinite_translation);
    RUN_TEST(test_fk_traps_on_infinite_scale);
    RUN_TEST(test_fk_traps_on_zero_quaternion);
    RUN_TEST(test_fk_traps_on_non_unit_quaternion);
#endif
    return UNITY_END();
}
