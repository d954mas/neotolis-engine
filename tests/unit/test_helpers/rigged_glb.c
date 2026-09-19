/* The fixture is written as text JSON plus one BIN chunk, like the hand-written
 * writers in test_builder.c: a binary asset checked into the tree would hide
 * which byte a rig test actually depends on. */

#include "test_helpers/rigged_glb.h"

#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// #region json buffer
/* The JSON is ~4 KB, ~12 KB with the deep chain; a fixed buffer with an abort
 * on overflow needs no growth. */
typedef struct {
    char data[16384];
    size_t len;
} json_buf_t;

#if defined(__GNUC__) || defined(__clang__)
#define RIGGED_PRINTF_ATTR(fmt_idx, arg_idx) __attribute__((format(printf, fmt_idx, arg_idx)))
#else
#define RIGGED_PRINTF_ATTR(fmt_idx, arg_idx)
#endif

/* The attribute both type-checks the call sites and tells the compiler the
 * forwarded format string is trusted (-Wformat-nonliteral). */
static void jb_addf(json_buf_t *jb, const char *fmt, ...) RIGGED_PRINTF_ATTR(2, 3);

static void jb_addf(json_buf_t *jb, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    /* The SysV va_list is an array; the analyzer loses the va_start through the
     * forwarded pointer and reports it uninitialized on Linux only. */
    // NOLINTNEXTLINE(clang-analyzer-valist.Uninitialized)
    const int needed = vsnprintf(jb->data + jb->len, sizeof(jb->data) - jb->len, fmt, args);
    va_end(args);
    if (needed < 0 || jb->len + (size_t)needed >= sizeof(jb->data)) {
        abort();
    }
    jb->len += (size_t)needed;
}

/* 9 significant digits round-trip binary32 exactly, so the fixture's rest
 * values survive the text detour bit for bit. */
static void jb_floats(json_buf_t *jb, const float *v, uint32_t n) {
    jb_addf(jb, "[");
    for (uint32_t i = 0; i < n; i++) {
        jb_addf(jb, "%s%.9g", i > 0 ? "," : "", (double)v[i]);
    }
    jb_addf(jb, "]");
}

static void jb_trs(json_buf_t *jb, const float *t, const float *q, const float *s) {
    jb_addf(jb, "\"translation\":");
    jb_floats(jb, t, 3);
    jb_addf(jb, ",\"rotation\":");
    jb_floats(jb, q, 4);
    jb_addf(jb, ",\"scale\":");
    jb_floats(jb, s, 3);
}
// #endregion

// #region bin sections
enum {
    SEC_POSITION = 0,
    SEC_JOINTS0,
    SEC_JOINTS1,
    SEC_WEIGHTS0,
    SEC_WEIGHTS1,
    SEC_INDICES,
    SEC_IBM,
    SEC_FAR_POSITION,
    SEC_FAR_JOINTS,
    SEC_FAR_WEIGHTS,
    SEC_ANIM_Q_TIMES,
    SEC_ANIM_J1_Q,
    SEC_ANIM_CUBIC_TIMES,
    SEC_ANIM_J2_T,
    SEC_ANIM_ENDS,
    SEC_ANIM_J2_Q,
    SEC_ANIM_STEP_TIMES,
    SEC_ANIM_J3_S,
    SEC_ANIM_J4_T,
    SEC_COUNT
};

typedef struct {
    const void *data;
    uint32_t size;
} rigged_sec_t;

static uint32_t pad4(uint32_t v) { return (v + 3U) & ~3U; }
// #endregion

/* One function so every knob reads against the same data in one place. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void rigged_glb_write(const char *path, const rigged_glb_opts_t *opts) {
    rigged_glb_opts_t o = {0};
    if (opts != NULL) {
        o = *opts;
    }

    // #region node transforms
    /* Pure rotation, 90 degrees about Y (column-major). */
    float root_matrix[16] = {0.0F, 0.0F, -1.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F};
    if (o.root_small_scale || o.root_small_shear) {
        memset(root_matrix, 0, sizeof(root_matrix));
        root_matrix[0] = 0.01F;
        root_matrix[5] = 0.01F;
        root_matrix[10] = 0.01F;
        root_matrix[15] = 1.0F;
        if (o.root_small_shear) {
            root_matrix[1] = 1e-5F;
        }
    }
    /* Rotation 90 degrees about Z times scale (2, 1, 0.5), translated. */
    float helper_matrix[16] = {0.0F, 2.0F, 0.0F, 0.0F, -1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.5F, 0.0F, 0.25F, -0.5F, 1.0F, 1.0F};
    if (o.matrix_shear) {
        helper_matrix[6] = 0.001F;
    }

    float joint_t[RIGGED_GLB_SKIN_JOINT_COUNT][3] = {
        {1.0F, 2.0F, 3.0F}, {0.0F, -0.0F, 0.5F}, {0.0F, 0.75F, 0.0F}, {-0.5F, 0.25F, 0.0F}, {0.0F, 0.5F, 0.0F},
    };
    if (o.rest_mismatch) {
        joint_t[2][1] = nextafterf(0.75F, 1.0F);
    }
    float joint_q[RIGGED_GLB_SKIN_JOINT_COUNT][4] = {
        {0.0F, 0.0F, 0.0F, 1.0F}, {0.0F, 0.0F, -0.70710678F, -0.70710678F}, {0.70710678F, 0.0F, 0.0F, 0.70710678F}, {0.0F, 0.0F, 0.0F, 1.0F}, {0.0F, 0.0F, 0.0F, 1.0F},
    };
    if (o.bad_rotation) {
        joint_q[2][0] = 0.0F;
        joint_q[2][3] = 2.0F;
    }
    const float joint_s[RIGGED_GLB_SKIN_JOINT_COUNT][3] = {
        {1.0F, 1.0F, 1.0F}, {1.0F, 1.0F, 1.0F}, {1.0F, 1.0F, 1.0F}, {1.0F, 1.0F, 1.0F}, {0.5F, 0.5F, 0.5F},
    };

    const float mesh_t[3] = {2.0F, 0.0F, -1.0F};
    const float mesh_q[4] = {0.0F, 0.38268343F, 0.0F, 0.92387953F};
    const float mesh_s[3] = {1.5F, 1.5F, 1.5F};
    const float object_t[3] = {0.0F, 0.0F, 5.0F};
    // #endregion

    // #region animation
    const bool anim = o.animation || o.animation_step_only || o.animation_outside_rig || o.animation_weights || o.animation_duplicate || o.animation_matrix_node || o.animation_step_past_end ||
                      o.animation_no_channels || o.animation_bad_times;
    const float anim_q_times[4] = {0.0F, 0.25F, 0.5F, 1.0F};
    const float anim_j1_q[4][4] = {{0.0F, 0.0F, 0.0F, 1.0F}, {0.0F, 0.0F, 0.0F, 1.0F}, {0.0F, 0.0F, 0.6F, 0.6F}, {0.0F, 0.0F, -1.0F, 0.0F}};
    const float anim_cubic_times[2] = {0.25F, 0.75F};
    /* Per key: in-tangent, value, out-tangent. */
    const float anim_j2_t[2][3][3] = {{{100.0F, 100.0F, 100.0F}, {0.0F, 0.0F, 0.0F}, {8.0F, 0.0F, 0.0F}}, {{0.0F, 8.0F, 0.0F}, {1.0F, 2.0F, 4.0F}, {100.0F, 100.0F, 100.0F}}};
    const float anim_ends[2] = {0.0F, RIGGED_GLB_ANIM_DURATION};
    const float anim_j2_q[2][3][4] = {{{7.0F, 7.0F, 7.0F, 7.0F}, {0.0F, 0.0F, 0.0F, 1.0F}, {0.0F, 0.0F, 2.0F, 0.0F}}, {{0.0F, 0.0F, 2.0F, 0.0F}, {0.0F, 0.0F, 1.0F, 0.0F}, {7.0F, 7.0F, 7.0F, 7.0F}}};
    float anim_step_times[3] = {0.25F, 0.75F, 1.05F};
    if (o.animation_bad_times) {
        anim_step_times[0] = 0.75F;
        anim_step_times[1] = 0.25F;
    }
    const uint32_t step_keys = o.animation_step_past_end ? 3U : 2U;
    const float anim_j3_s[3][3] = {{1.0F, 1.0F, 1.0F}, {2.0F, 2.0F, 2.0F}, {3.0F, 3.0F, 3.0F}};
    const float anim_j4_t[2][3] = {{0.0F, 0.5F, 0.0F}, {0.0F, 0.5F, 0.0F}};
    // #endregion

    // #region vertex data
    const float positions[RIGGED_GLB_VERTEX_COUNT * 3] = {0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 1.0F, 1.0F, 0.0F, 0.0F, 1.0F, 0.0F};
    const uint16_t indices[RIGGED_GLB_INDEX_COUNT] = {0, 1, 2, 0, 2, 3};

    uint8_t joints0[RIGGED_GLB_VERTEX_COUNT * 4] = {0, 1, 2, 3, 1, 0, 3, 2, 0, 1, 2, 3, 0, 1, 0, 0};
    uint8_t joints1[RIGGED_GLB_VERTEX_COUNT * 4] = {4, 0, 0, 0, 4, 0, 0, 0, 4, 0, 0, 0, 0, 0, 0, 0};
    float weights0[RIGGED_GLB_VERTEX_COUNT * 4] = {
        0.40F, 0.30F, 0.20F, 0.09F, 0.40F, 0.30F, 0.10F, 0.10F, 0.40F, 0.30F, 0.15F, 0.10F, 0.75F, 0.25F, 0.0F, 0.0F,
    };
    float weights1[RIGGED_GLB_VERTEX_COUNT * 4] = {
        0.01F, 0.0F, 0.0F, 0.0F, 0.10F, 0.0F, 0.0F, 0.0F, 0.05F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F,
    };

    /* Every per-vertex defect lands on v3, the plain two-influence vertex. */
    if (o.negative_weight) {
        weights0[13] = -0.25F;
    }
    if (o.nan_weight) {
        weights0[13] = NAN;
    }
    if (o.index_ge_palette) {
        joints0[13] = RIGGED_GLB_SKIN_JOINT_COUNT;
    }
    if (o.zero_weight_lane_index) {
        joints0[14] = 200;
    }
    if (o.duplicate_joint) {
        weights0[12] = 0.65F;
        weights0[14] = 0.10F;
    }
    if (o.zero_weights) {
        weights0[12] = 0.0F;
        weights0[13] = 0.0F;
    }
    if (o.weights_half) {
        weights0[12] = 0.5F;
        weights0[13] = 0.5F;
    }
    /* Only the accessor type matters for the bad-type knob; the bytes are the
     * weights scaled, which a normalized reader would accept. Built only for
     * that knob, since a negative or NaN weight has no byte. */
    uint8_t weights0_u8[RIGGED_GLB_VERTEX_COUNT * 4] = {0};
    if (o.weights_bad_type) {
        for (uint32_t i = 0; i < RIGGED_GLB_VERTEX_COUNT * 4; i++) {
            weights0_u8[i] = (uint8_t)(weights0[i] * 255.0F);
        }
    }

    /* The far triangle: one vertex ten units out on X, bound wholly to palette
     * entry 0; the other two sit at the origin on entries 1 and 2. */
    const float far_positions[RIGGED_GLB_FAR_VERTEX_COUNT * 3] = {10.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F};
    const uint8_t far_joints[RIGGED_GLB_FAR_VERTEX_COUNT * 4] = {0, 0, 0, 0, 1, 0, 0, 0, 2, 0, 0, 0};
    const float far_weights[RIGGED_GLB_FAR_VERTEX_COUNT * 4] = {1.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F};

    float joints0_f32[RIGGED_GLB_VERTEX_COUNT * 4];
    float joints1_f32[RIGGED_GLB_VERTEX_COUNT * 4];
    for (uint32_t i = 0; i < RIGGED_GLB_VERTEX_COUNT * 4; i++) {
        joints0_f32[i] = (float)joints0[i];
        joints1_f32[i] = (float)joints1[i];
    }
    // #endregion

    // #region skin data
    uint32_t joint_count = RIGGED_GLB_SKIN_JOINT_COUNT;
    if (o.joint_outside_root || o.multi_root || o.deep_chain) {
        joint_count = RIGGED_GLB_SKIN_JOINT_COUNT + 1;
    }
    const uint32_t chain_len = o.deep_chain ? 260U : 0U;
    const uint32_t extra_joint = o.deep_chain ? RIGGED_GLB_NODE_COUNT + chain_len - 1U : RIGGED_GLB_NODE_OBJECT;

    float ibm[(RIGGED_GLB_SKIN_JOINT_COUNT + 1) * 16] = {0};
    for (uint32_t p = 0; p < joint_count; p++) {
        float *m = &ibm[(size_t)p * 16];
        m[0] = 1.0F;
        m[5] = 1.0F;
        m[10] = 1.0F;
        m[15] = 1.0F;
        m[12] = -((float)p + 1.0F);
        m[13] = 0.5F * (float)p;
        m[14] = -1.0F;
    }
    if (o.ibm_nan) {
        ibm[5] = NAN;
    }
    if (o.ibm_projective) {
        ibm[3] = 0.5F;
    }
    // #endregion

    // #region section table
    /* Without indices the primitive draws its vertices directly, and four of
     * them are not a whole number of triangles, so that variant keeps the first
     * three, so the lanes the test checks are still v0..v2. */
    const uint32_t vertex_count = o.no_indices ? 3U : RIGGED_GLB_VERTEX_COUNT;

    rigged_sec_t sec[SEC_COUNT];
    sec[SEC_POSITION] = (rigged_sec_t){positions, vertex_count * 3U * (uint32_t)sizeof(float)};
    if (o.joints_float_type) {
        sec[SEC_JOINTS0] = (rigged_sec_t){joints0_f32, vertex_count * 4U * (uint32_t)sizeof(float)};
        sec[SEC_JOINTS1] = (rigged_sec_t){joints1_f32, vertex_count * 4U * (uint32_t)sizeof(float)};
    } else {
        sec[SEC_JOINTS0] = (rigged_sec_t){joints0, vertex_count * 4U};
        sec[SEC_JOINTS1] = (rigged_sec_t){joints1, vertex_count * 4U};
    }
    if (o.weights_bad_type) {
        sec[SEC_WEIGHTS0] = (rigged_sec_t){weights0_u8, vertex_count * 4U};
    } else {
        sec[SEC_WEIGHTS0] = (rigged_sec_t){weights0, vertex_count * 4U * (uint32_t)sizeof(float)};
    }
    sec[SEC_WEIGHTS1] = (rigged_sec_t){weights1, vertex_count * 4U * (uint32_t)sizeof(float)};
    sec[SEC_INDICES] = (rigged_sec_t){indices, (uint32_t)sizeof(indices)};
    sec[SEC_IBM] = (rigged_sec_t){ibm, joint_count * 16U * (uint32_t)sizeof(float)};
    sec[SEC_FAR_POSITION] = (rigged_sec_t){far_positions, (uint32_t)sizeof(far_positions)};
    sec[SEC_FAR_JOINTS] = (rigged_sec_t){far_joints, (uint32_t)sizeof(far_joints)};
    sec[SEC_FAR_WEIGHTS] = (rigged_sec_t){far_weights, (uint32_t)sizeof(far_weights)};
    sec[SEC_ANIM_Q_TIMES] = (rigged_sec_t){anim_q_times, (uint32_t)sizeof(anim_q_times)};
    sec[SEC_ANIM_J1_Q] = (rigged_sec_t){anim_j1_q, (uint32_t)sizeof(anim_j1_q)};
    sec[SEC_ANIM_CUBIC_TIMES] = (rigged_sec_t){anim_cubic_times, (uint32_t)sizeof(anim_cubic_times)};
    sec[SEC_ANIM_J2_T] = (rigged_sec_t){anim_j2_t, (uint32_t)sizeof(anim_j2_t)};
    sec[SEC_ANIM_ENDS] = (rigged_sec_t){anim_ends, (uint32_t)sizeof(anim_ends)};
    sec[SEC_ANIM_J2_Q] = (rigged_sec_t){anim_j2_q, (uint32_t)sizeof(anim_j2_q)};
    sec[SEC_ANIM_STEP_TIMES] = (rigged_sec_t){anim_step_times, step_keys * (uint32_t)sizeof(float)};
    sec[SEC_ANIM_J3_S] = (rigged_sec_t){anim_j3_s, step_keys * 3U * (uint32_t)sizeof(float)};
    sec[SEC_ANIM_J4_T] = (rigged_sec_t){anim_j4_t, (uint32_t)sizeof(anim_j4_t)};

    uint32_t offset[SEC_COUNT];
    uint32_t bin_size = 0;
    for (uint32_t i = 0; i < SEC_COUNT; i++) {
        offset[i] = bin_size;
        bin_size += pad4(sec[i].size);
    }
    // #endregion

    // #region json
    json_buf_t jb = {.len = 0};
    const uint32_t joints_ctype = o.joints_float_type ? 5126U : 5121U;
    const uint32_t mesh_skin = o.mesh_other_skin ? 1U : 0U;

    jb_addf(&jb, "{\"asset\":{\"version\":\"2.0\"},\"scene\":0,");
    jb_addf(&jb, "\"scenes\":[{\"nodes\":[0%s]}],", o.multi_root ? ",8" : "");
    const uint32_t far_node = o.deep_chain ? RIGGED_GLB_NODE_COUNT + chain_len : RIGGED_GLB_NODE_COUNT;

    jb_addf(&jb, "\"nodes\":[");
    jb_addf(&jb, "{\"name\":\"Root\",\"matrix\":");
    jb_floats(&jb, root_matrix, 16);
    /* cgltf rejects a scene root that has a parent, so the cycle closes below
     * Root: Helper leaves Root's children and becomes a child of Joint4. */
    jb_addf(&jb, ",\"children\":[%s7%s", o.cycle ? "" : "1,", o.multi_root ? "" : ",8");
    if (o.second_node_far) {
        jb_addf(&jb, ",%u", far_node);
    }
    jb_addf(&jb, "]},");
    if (o.unnamed_node) {
        jb_addf(&jb, "{\"matrix\":");
    } else {
        jb_addf(&jb, "{\"name\":\"%s\",\"matrix\":", o.empty_name ? "" : "Helper");
    }
    jb_floats(&jb, helper_matrix, 16);
    if (o.matrix_and_trs) {
        jb_addf(&jb, ",\"translation\":[0,1,0]");
    }
    jb_addf(&jb, ",\"children\":[2]},");
    for (uint32_t j = 0; j < RIGGED_GLB_SKIN_JOINT_COUNT; j++) {
        jb_addf(&jb, "{\"name\":\"Joint%u\",", (j == 4 && o.duplicate_name) ? 3U : j);
        jb_trs(&jb, joint_t[j], joint_q[j], joint_s[j]);
        if (j == 0) {
            jb_addf(&jb, ",\"children\":[5,3]");
        } else if (j == 1 && !o.reparent_joint2) {
            jb_addf(&jb, ",\"children\":[4]");
        } else if (j == 3) {
            jb_addf(&jb, ",\"children\":[6%s]", o.reparent_joint2 ? ",4" : "");
        } else if (j == 4 && o.cycle) {
            jb_addf(&jb, ",\"children\":[1]");
        }
        jb_addf(&jb, "},");
    }
    jb_addf(&jb, "{\"name\":\"MeshNode\",");
    jb_trs(&jb, mesh_t, mesh_q, mesh_s);
    jb_addf(&jb, ",\"mesh\":0,\"skin\":%u},", mesh_skin);
    jb_addf(&jb, "{\"name\":\"Object\",\"translation\":");
    jb_floats(&jb, object_t, 3);
    if (o.deep_chain) {
        jb_addf(&jb, ",\"children\":[%u]}", (uint32_t)RIGGED_GLB_NODE_COUNT);
        for (uint32_t i = 0; i < chain_len; i++) {
            const uint32_t node = RIGGED_GLB_NODE_COUNT + i;
            jb_addf(&jb, ",{\"name\":\"D%u\"", i);
            if (i + 1U < chain_len) {
                jb_addf(&jb, ",\"children\":[%u]", node + 1U);
            }
            jb_addf(&jb, "}");
        }
    } else {
        jb_addf(&jb, "}");
    }
    if (o.second_node_far) {
        jb_addf(&jb, ",{\"name\":\"FarNode\",\"mesh\":1,\"skin\":0}");
    }
    jb_addf(&jb, "],");

    const char *second_set = o.nonconsecutive_sets ? "2" : "1";
    jb_addf(&jb, "\"meshes\":[{\"name\":\"Quad\",\"primitives\":[{\"attributes\":{");
    jb_addf(&jb, "\"POSITION\":0,\"JOINTS_0\":1,\"WEIGHTS_0\":3,\"JOINTS_%s\":2", second_set);
    if (!o.unpaired_sets) {
        jb_addf(&jb, ",\"WEIGHTS_%s\":4", second_set);
    }
    jb_addf(&jb, "}");
    if (!o.no_indices) {
        jb_addf(&jb, ",\"indices\":5");
    }
    if (o.morph_target || o.animation_weights) {
        jb_addf(&jb, ",\"targets\":[{\"POSITION\":0}]");
    }
    jb_addf(&jb, "}");
    if (o.second_primitive_far) {
        jb_addf(&jb, ",{\"attributes\":{\"POSITION\":7,\"JOINTS_0\":8,\"WEIGHTS_0\":9}}");
    }
    jb_addf(&jb, "]}");
    if (o.second_node_far) {
        jb_addf(&jb, ",{\"name\":\"Far\",\"primitives\":[{\"attributes\":{\"POSITION\":7,\"JOINTS_0\":8,\"WEIGHTS_0\":9}}]}");
    }
    jb_addf(&jb, "],");

    jb_addf(&jb, "\"skins\":[{\"name\":\"RigSkin\",");
    if (!o.no_ibm) {
        jb_addf(&jb, "\"inverseBindMatrices\":6,");
    }
    jb_addf(&jb, "\"joints\":[4,6,2,3,%s", o.duplicate_skin_joint ? "3" : "5");
    if (joint_count > RIGGED_GLB_SKIN_JOINT_COUNT) {
        jb_addf(&jb, ",%u", extra_joint);
    }
    jb_addf(&jb, "]}");
    if (o.mesh_other_skin) {
        jb_addf(&jb, ",{\"name\":\"OtherSkin\",\"joints\":[2,3]}");
    }
    jb_addf(&jb, "],");

    jb_addf(&jb, "\"accessors\":[");
    jb_addf(&jb, "{\"bufferView\":0,\"componentType\":5126,\"count\":%u,\"type\":\"VEC3\",\"min\":[0,0,0],\"max\":[1,1,0]},", vertex_count);
    jb_addf(&jb, "{\"bufferView\":1,\"componentType\":%u,\"count\":%u,\"type\":\"VEC4\"},", joints_ctype, vertex_count);
    jb_addf(&jb, "{\"bufferView\":2,\"componentType\":%u,\"count\":%u,\"type\":\"VEC4\"},", joints_ctype, vertex_count);
    jb_addf(&jb, "{\"bufferView\":3,\"componentType\":%u,\"count\":%u,\"type\":\"VEC4\"},", o.weights_bad_type ? 5121U : 5126U, vertex_count);
    jb_addf(&jb, "{\"bufferView\":4,\"componentType\":5126,\"count\":%u,\"type\":\"VEC4\"},", o.weights1_short ? vertex_count - 1U : vertex_count);
    jb_addf(&jb, "{\"bufferView\":5,\"componentType\":5123,\"count\":6,\"type\":\"SCALAR\"},");
    jb_addf(&jb, "{\"bufferView\":6,\"componentType\":5126,\"count\":%u,\"type\":\"%s\"},", o.ibm_short ? joint_count - 1U : joint_count, o.ibm_bad_type ? "VEC4" : "MAT4");
    jb_addf(&jb, "{\"bufferView\":7,\"componentType\":5126,\"count\":%u,\"type\":\"VEC3\",\"min\":[0,0,0],\"max\":[10,0,0]},", (uint32_t)RIGGED_GLB_FAR_VERTEX_COUNT);
    jb_addf(&jb, "{\"bufferView\":8,\"componentType\":5121,\"count\":%u,\"type\":\"VEC4\"},", (uint32_t)RIGGED_GLB_FAR_VERTEX_COUNT);
    jb_addf(&jb, "{\"bufferView\":9,\"componentType\":5126,\"count\":%u,\"type\":\"VEC4\"},", (uint32_t)RIGGED_GLB_FAR_VERTEX_COUNT);
    /* Animation accessors 10..18, one per section, in section order. */
    jb_addf(&jb, "{\"bufferView\":10,\"componentType\":5126,\"count\":4,\"type\":\"SCALAR\",\"min\":[0],\"max\":[1]},");
    jb_addf(&jb, "{\"bufferView\":11,\"componentType\":5126,\"count\":4,\"type\":\"VEC4\"},");
    jb_addf(&jb, "{\"bufferView\":12,\"componentType\":5126,\"count\":2,\"type\":\"SCALAR\",\"min\":[0.25],\"max\":[0.75]},");
    jb_addf(&jb, "{\"bufferView\":13,\"componentType\":5126,\"count\":6,\"type\":\"VEC3\"},");
    jb_addf(&jb, "{\"bufferView\":14,\"componentType\":5126,\"count\":2,\"type\":\"SCALAR\",\"min\":[0],\"max\":[1]},");
    jb_addf(&jb, "{\"bufferView\":15,\"componentType\":5126,\"count\":6,\"type\":\"VEC4\"},");
    jb_addf(&jb, "{\"bufferView\":16,\"componentType\":5126,\"count\":%u,\"type\":\"SCALAR\",\"min\":[0.25],\"max\":[%s]},", step_keys, o.animation_step_past_end ? "1.05" : "0.75");
    jb_addf(&jb, "{\"bufferView\":17,\"componentType\":5126,\"count\":%u,\"type\":\"VEC3\"},", step_keys);
    jb_addf(&jb, "{\"bufferView\":18,\"componentType\":5126,\"count\":2,\"type\":\"VEC3\"}],");

    if (anim) {
        /* Samplers: 0 Joint1 q, 1 Joint2 t cubic, 2 Joint2 q cubic, 3 Joint3 s
         * step, 4 Joint4 t, 5 the translation of the defect channels, 6 morph
         * weights (accessor 14 is two floats: a SCALAR output over one target). */
        jb_addf(&jb, "\"animations\":[{\"name\":\"Clip\",\"samplers\":[");
        jb_addf(&jb, "{\"input\":10,\"output\":11,\"interpolation\":\"LINEAR\"},");
        jb_addf(&jb, "{\"input\":12,\"output\":13,\"interpolation\":\"CUBICSPLINE\"},");
        jb_addf(&jb, "{\"input\":14,\"output\":15,\"interpolation\":\"CUBICSPLINE\"},");
        jb_addf(&jb, "{\"input\":16,\"output\":17,\"interpolation\":\"STEP\"},");
        jb_addf(&jb, "{\"input\":14,\"output\":18,\"interpolation\":\"LINEAR\"},");
        jb_addf(&jb, "{\"input\":14,\"output\":18,\"interpolation\":\"LINEAR\"},");
        jb_addf(&jb, "{\"input\":14,\"output\":14,\"interpolation\":\"LINEAR\"}");
        jb_addf(&jb, "],\"channels\":[");
        if (!o.animation_step_only && !o.animation_no_channels) {
            jb_addf(&jb, "{\"sampler\":0,\"target\":{\"node\":3,\"path\":\"rotation\"}},");
            jb_addf(&jb, "{\"sampler\":1,\"target\":{\"node\":4,\"path\":\"translation\"}},");
            jb_addf(&jb, "{\"sampler\":2,\"target\":{\"node\":4,\"path\":\"rotation\"}},");
        }
        if (!o.animation_no_channels) {
            jb_addf(&jb, "{\"sampler\":3,\"target\":{\"node\":5,\"path\":\"scale\"}},");
            jb_addf(&jb, "{\"sampler\":4,\"target\":{\"node\":6,\"path\":\"translation\"}}");
        }
        if (o.animation_outside_rig) {
            jb_addf(&jb, ",{\"sampler\":5,\"target\":{\"node\":%u,\"path\":\"translation\"}}", (uint32_t)RIGGED_GLB_NODE_MESH);
        }
        if (o.animation_weights) {
            jb_addf(&jb, ",{\"sampler\":6,\"target\":{\"node\":%u,\"path\":\"weights\"}}", (uint32_t)RIGGED_GLB_NODE_MESH);
        }
        if (o.animation_duplicate) {
            jb_addf(&jb, ",{\"sampler\":0,\"target\":{\"node\":3,\"path\":\"rotation\"}}");
        }
        if (o.animation_matrix_node) {
            jb_addf(&jb, ",{\"sampler\":5,\"target\":{\"node\":%u,\"path\":\"translation\"}}", (uint32_t)RIGGED_GLB_NODE_HELPER);
        }
        jb_addf(&jb, "]}],");
    }

    jb_addf(&jb, "\"bufferViews\":[");
    for (uint32_t i = 0; i < SEC_COUNT; i++) {
        jb_addf(&jb, "%s{\"buffer\":0,\"byteOffset\":%u,\"byteLength\":%u}", i > 0 ? "," : "", offset[i], sec[i].size);
    }
    jb_addf(&jb, "],\"buffers\":[{\"byteLength\":%u}]}", bin_size);
    // #endregion

    // #region write glb
    uint32_t json_len = (uint32_t)jb.len;
    uint32_t json_padded = pad4(json_len);
    uint32_t glb_magic = 0x46546C67;       /* "glTF" */
    uint32_t json_chunk_type = 0x4E4F534A; /* "JSON" */
    uint32_t bin_chunk_type = 0x004E4942;  /* "BIN\0" */
    uint32_t glb_version = 2;
    uint32_t total_length = 12 + 8 + json_padded + 8 + bin_size;

    FILE *f = fopen(path, "wb");
    if (f == NULL) {
        (void)fprintf(stderr, "rigged_glb_write: cannot open %s\n", path);
        abort();
    }
    (void)fwrite(&glb_magic, 4, 1, f);
    (void)fwrite(&glb_version, 4, 1, f);
    (void)fwrite(&total_length, 4, 1, f);

    (void)fwrite(&json_padded, 4, 1, f);
    (void)fwrite(&json_chunk_type, 4, 1, f);
    (void)fwrite(jb.data, 1, json_len, f);
    for (uint32_t i = json_len; i < json_padded; i++) {
        char space = ' ';
        (void)fwrite(&space, 1, 1, f);
    }

    (void)fwrite(&bin_size, 4, 1, f);
    (void)fwrite(&bin_chunk_type, 4, 1, f);
    for (uint32_t i = 0; i < SEC_COUNT; i++) {
        (void)fwrite(sec[i].data, 1, sec[i].size, f);
        for (uint32_t p = sec[i].size; p < pad4(sec[i].size); p++) {
            char zero = 0;
            (void)fwrite(&zero, 1, 1, f);
        }
    }
    (void)fclose(f);
    // #endregion
}
