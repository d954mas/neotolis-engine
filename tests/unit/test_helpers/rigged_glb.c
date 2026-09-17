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
typedef struct {
    char *data;
    size_t len;
    size_t cap;
} json_buf_t;

static void jb_reserve(json_buf_t *jb, size_t extra) {
    if (jb->data != NULL && jb->len + extra + 1 <= jb->cap) {
        return;
    }
    size_t cap = jb->cap > 0 ? jb->cap : 4096;
    while (cap < jb->len + extra + 1) {
        cap *= 2;
    }
    char *grown = (char *)realloc(jb->data, cap);
    if (grown == NULL) {
        free(jb->data);
        abort();
    }
    jb->data = grown;
    jb->cap = cap;
}

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
    va_list measure;
    va_copy(measure, args);
    int needed = vsnprintf(NULL, 0, fmt, measure);
    va_end(measure);
    if (needed < 0) {
        va_end(args);
        abort();
    }
    jb_reserve(jb, (size_t)needed);
    (void)vsnprintf(jb->data + jb->len, jb->cap - jb->len, fmt, args);
    va_end(args);
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
    SEC_NORMAL,
    SEC_TEXCOORD,
    SEC_JOINTS0,
    SEC_JOINTS1,
    SEC_WEIGHTS0,
    SEC_WEIGHTS1,
    SEC_INDICES,
    SEC_IBM,
    SEC_TIMES3,
    SEC_ROT3,
    SEC_TRANS3,
    SEC_SCALE9,
    SEC_TIMES2,
    SEC_TRANS2,
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
    const float root_matrix[16] = {0.0F, 0.0F, -1.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F};
    /* Rotation 90 degrees about Z times scale (2, 1, 0.5), translated. */
    float helper_matrix[16] = {0.0F, 2.0F, 0.0F, 0.0F, -1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.5F, 0.0F, 0.25F, -0.5F, 1.0F, 1.0F};
    if (o.matrix_shear) {
        helper_matrix[6] = 0.001F;
    }

    const float joint_t[RIGGED_GLB_SKIN_JOINT_COUNT][3] = {
        {1.0F, 2.0F, 3.0F}, {0.0F, -0.0F, 0.5F}, {0.0F, 0.75F, 0.0F}, {-0.5F, 0.25F, 0.0F}, {0.0F, 0.5F, 0.0F},
    };
    const float joint_q[RIGGED_GLB_SKIN_JOINT_COUNT][4] = {
        {0.0F, 0.0F, 0.0F, 1.0F}, {0.0F, 0.0F, -0.70710678F, -0.70710678F}, {0.70710678F, 0.0F, 0.0F, 0.70710678F}, {0.0F, 0.0F, 0.0F, 1.0F}, {0.0F, 0.0F, 0.0F, 1.0F},
    };
    const float joint_s[RIGGED_GLB_SKIN_JOINT_COUNT][3] = {
        {1.0F, 1.0F, 1.0F}, {1.0F, 1.0F, 1.0F}, {1.0F, 1.0F, 1.0F}, {1.0F, 1.0F, 1.0F}, {0.5F, 0.5F, 0.5F},
    };

    const float mesh_t[3] = {2.0F, 0.0F, -1.0F};
    const float mesh_q[4] = {0.0F, 0.38268343F, 0.0F, 0.92387953F};
    const float mesh_s[3] = {1.5F, 1.5F, 1.5F};
    const float object_t[3] = {0.0F, 0.0F, 5.0F};
    const float object_q[4] = {0.0F, 0.0F, 0.0F, 1.0F};
    const float object_s[3] = {1.0F, 1.0F, 1.0F};
    // #endregion

    // #region vertex data
    const float positions[RIGGED_GLB_VERTEX_COUNT * 3] = {0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 1.0F, 1.0F, 0.0F, 0.0F, 1.0F, 0.0F};
    const float normals[RIGGED_GLB_VERTEX_COUNT * 3] = {0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 1.0F};
    const float uvs[RIGGED_GLB_VERTEX_COUNT * 2] = {0.0F, 0.0F, 1.0F, 0.0F, 1.0F, 1.0F, 0.0F, 1.0F};
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
    if (o.duplicate_joint) {
        weights0[12] = 0.65F;
        weights0[14] = 0.10F;
    }
    if (o.zero_weights) {
        weights0[12] = 0.0F;
        weights0[13] = 0.0F;
    }

    float joints0_f32[RIGGED_GLB_VERTEX_COUNT * 4];
    float joints1_f32[RIGGED_GLB_VERTEX_COUNT * 4];
    for (uint32_t i = 0; i < RIGGED_GLB_VERTEX_COUNT * 4; i++) {
        joints0_f32[i] = (float)joints0[i];
        joints1_f32[i] = (float)joints1[i];
    }
    // #endregion

    // #region skin and animation data
    uint32_t joint_count = RIGGED_GLB_SKIN_JOINT_COUNT;
    if (o.joint_outside_root || o.multi_root) {
        joint_count = RIGGED_GLB_SKIN_JOINT_COUNT + 1;
    }

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

    const float times3[3] = {0.0F, 0.25F, 0.5F};
    const float rot3[3 * 4] = {0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.38268343F, 0.92387953F, 0.0F, 0.0F, 0.70710678F, 0.70710678F};
    const float trans3[3 * 3] = {0.0F, 0.0F, 0.0F, 0.0F, 0.5F, 0.0F, 0.0F, 1.0F, 0.0F};
    /* CUBICSPLINE stores in-tangent, value and out-tangent per key. */
    const float scale9[9 * 3] = {
        0.0F, 0.0F, 0.0F, 1.0F, 1.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.5F, 1.5F, 1.5F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 1.0F, 1.0F, 0.0F, 0.0F, 0.0F,
    };
    const float times2[2] = {0.0F, RIGGED_GLB_IDLE_DURATION};
    const float trans2[2 * 3] = {0.0F, 0.25F, 0.0F, 0.0F, 0.25F, 0.0F};
    // #endregion

    // #region section table
    rigged_sec_t sec[SEC_COUNT];
    sec[SEC_POSITION] = (rigged_sec_t){positions, (uint32_t)sizeof(positions)};
    sec[SEC_NORMAL] = (rigged_sec_t){normals, (uint32_t)sizeof(normals)};
    sec[SEC_TEXCOORD] = (rigged_sec_t){uvs, (uint32_t)sizeof(uvs)};
    if (o.joints_float_type) {
        sec[SEC_JOINTS0] = (rigged_sec_t){joints0_f32, (uint32_t)sizeof(joints0_f32)};
        sec[SEC_JOINTS1] = (rigged_sec_t){joints1_f32, (uint32_t)sizeof(joints1_f32)};
    } else {
        sec[SEC_JOINTS0] = (rigged_sec_t){joints0, (uint32_t)sizeof(joints0)};
        sec[SEC_JOINTS1] = (rigged_sec_t){joints1, (uint32_t)sizeof(joints1)};
    }
    sec[SEC_WEIGHTS0] = (rigged_sec_t){weights0, (uint32_t)sizeof(weights0)};
    sec[SEC_WEIGHTS1] = (rigged_sec_t){weights1, (uint32_t)sizeof(weights1)};
    sec[SEC_INDICES] = (rigged_sec_t){indices, (uint32_t)sizeof(indices)};
    sec[SEC_IBM] = (rigged_sec_t){ibm, joint_count * 16U * (uint32_t)sizeof(float)};
    sec[SEC_TIMES3] = (rigged_sec_t){times3, (uint32_t)sizeof(times3)};
    sec[SEC_ROT3] = (rigged_sec_t){rot3, (uint32_t)sizeof(rot3)};
    sec[SEC_TRANS3] = (rigged_sec_t){trans3, (uint32_t)sizeof(trans3)};
    sec[SEC_SCALE9] = (rigged_sec_t){scale9, (uint32_t)sizeof(scale9)};
    sec[SEC_TIMES2] = (rigged_sec_t){times2, (uint32_t)sizeof(times2)};
    sec[SEC_TRANS2] = (rigged_sec_t){trans2, (uint32_t)sizeof(trans2)};

    uint32_t offset[SEC_COUNT];
    uint32_t bin_size = 0;
    for (uint32_t i = 0; i < SEC_COUNT; i++) {
        offset[i] = bin_size;
        bin_size += pad4(sec[i].size);
    }
    // #endregion

    // #region json
    json_buf_t jb = {0};
    const uint32_t joints_ctype = o.joints_float_type ? 5126U : 5121U;
    const uint32_t mesh_skin = o.mesh_other_skin ? 1U : 0U;

    jb_addf(&jb, "{\"asset\":{\"version\":\"2.0\"},\"scene\":0,");
    jb_addf(&jb, "\"scenes\":[{\"nodes\":[0%s]}],", o.multi_root ? ",8" : "");

    jb_addf(&jb, "\"nodes\":[");
    jb_addf(&jb, "{\"name\":\"Root\",\"matrix\":");
    jb_floats(&jb, root_matrix, 16);
    jb_addf(&jb, ",\"children\":[1,7%s]},", o.multi_root ? "" : ",8");
    if (o.unnamed_node) {
        jb_addf(&jb, "{\"matrix\":");
    } else {
        jb_addf(&jb, "{\"name\":\"Helper\",\"matrix\":");
    }
    jb_floats(&jb, helper_matrix, 16);
    jb_addf(&jb, ",\"children\":[2]},");
    for (uint32_t j = 0; j < RIGGED_GLB_SKIN_JOINT_COUNT; j++) {
        jb_addf(&jb, "{\"name\":\"Joint%u\",", j);
        jb_trs(&jb, joint_t[j], joint_q[j], joint_s[j]);
        if (j == 0) {
            jb_addf(&jb, ",\"children\":[3,5]");
        } else if (j == 1) {
            jb_addf(&jb, ",\"children\":[4]");
        } else if (j == 3) {
            jb_addf(&jb, ",\"children\":[6]");
        } else if (j == 4 && o.cycle) {
            jb_addf(&jb, ",\"children\":[0]");
        }
        jb_addf(&jb, "},");
    }
    jb_addf(&jb, "{\"name\":\"MeshNode\",");
    jb_trs(&jb, mesh_t, mesh_q, mesh_s);
    jb_addf(&jb, ",\"mesh\":0,\"skin\":%u},", mesh_skin);
    jb_addf(&jb, "{\"name\":\"Object\",");
    jb_trs(&jb, object_t, object_q, object_s);
    jb_addf(&jb, "}],");

    jb_addf(&jb, "\"meshes\":[{\"name\":\"Quad\",\"primitives\":[{\"attributes\":{");
    jb_addf(&jb, "\"POSITION\":0,\"NORMAL\":1,\"TEXCOORD_0\":2,\"JOINTS_0\":3,\"WEIGHTS_0\":5,\"JOINTS_1\":4");
    if (!o.unpaired_sets) {
        jb_addf(&jb, ",\"WEIGHTS_1\":6");
    }
    jb_addf(&jb, "}");
    if (!o.no_indices) {
        jb_addf(&jb, ",\"indices\":7");
    }
    if (o.morph_target) {
        jb_addf(&jb, ",\"targets\":[{\"POSITION\":0}]");
    }
    jb_addf(&jb, "}]}],");

    jb_addf(&jb, "\"skins\":[{\"name\":\"RigSkin\",");
    if (!o.no_ibm) {
        jb_addf(&jb, "\"inverseBindMatrices\":8,");
    }
    jb_addf(&jb, "\"joints\":[2,3,4,5,6%s]}", joint_count > RIGGED_GLB_SKIN_JOINT_COUNT ? ",8" : "");
    if (o.mesh_other_skin) {
        jb_addf(&jb, ",{\"name\":\"OtherSkin\",\"joints\":[2,3]}");
    }
    jb_addf(&jb, "],");

    jb_addf(&jb, "\"animations\":[{\"name\":\"Walk\",\"samplers\":[");
    jb_addf(&jb, "{\"input\":9,\"output\":10,\"interpolation\":\"LINEAR\"},");
    jb_addf(&jb, "{\"input\":9,\"output\":11,\"interpolation\":\"STEP\"},");
    jb_addf(&jb, "{\"input\":9,\"output\":12,\"interpolation\":\"CUBICSPLINE\"},");
    jb_addf(&jb, "{\"input\":9,\"output\":11,\"interpolation\":\"LINEAR\"}],\"channels\":[");
    jb_addf(&jb, "{\"sampler\":0,\"target\":{\"node\":3,\"path\":\"rotation\"}},");
    jb_addf(&jb, "{\"sampler\":1,\"target\":{\"node\":4,\"path\":\"translation\"}},");
    jb_addf(&jb, "{\"sampler\":2,\"target\":{\"node\":5,\"path\":\"scale\"}},");
    jb_addf(&jb, "{\"sampler\":3,\"target\":{\"node\":8,\"path\":\"translation\"}}]},");
    jb_addf(&jb, "{\"name\":\"Idle\",\"samplers\":[{\"input\":13,\"output\":14,\"interpolation\":\"LINEAR\"}],");
    jb_addf(&jb, "\"channels\":[{\"sampler\":0,\"target\":{\"node\":2,\"path\":\"translation\"}}]}],");

    jb_addf(&jb, "\"accessors\":[");
    jb_addf(&jb, "{\"bufferView\":0,\"componentType\":5126,\"count\":4,\"type\":\"VEC3\",\"min\":[0,0,0],\"max\":[1,1,0]},");
    jb_addf(&jb, "{\"bufferView\":1,\"componentType\":5126,\"count\":4,\"type\":\"VEC3\"},");
    jb_addf(&jb, "{\"bufferView\":2,\"componentType\":5126,\"count\":4,\"type\":\"VEC2\"},");
    jb_addf(&jb, "{\"bufferView\":3,\"componentType\":%u,\"count\":4,\"type\":\"VEC4\"},", joints_ctype);
    jb_addf(&jb, "{\"bufferView\":4,\"componentType\":%u,\"count\":4,\"type\":\"VEC4\"},", joints_ctype);
    jb_addf(&jb, "{\"bufferView\":5,\"componentType\":5126,\"count\":4,\"type\":\"VEC4\"},");
    jb_addf(&jb, "{\"bufferView\":6,\"componentType\":5126,\"count\":4,\"type\":\"VEC4\"},");
    jb_addf(&jb, "{\"bufferView\":7,\"componentType\":5123,\"count\":6,\"type\":\"SCALAR\"},");
    jb_addf(&jb, "{\"bufferView\":8,\"componentType\":5126,\"count\":%u,\"type\":\"MAT4\"},", joint_count);
    jb_addf(&jb, "{\"bufferView\":9,\"componentType\":5126,\"count\":3,\"type\":\"SCALAR\",\"min\":[0],\"max\":[0.5]},");
    jb_addf(&jb, "{\"bufferView\":10,\"componentType\":5126,\"count\":3,\"type\":\"VEC4\"},");
    jb_addf(&jb, "{\"bufferView\":11,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\"},");
    jb_addf(&jb, "{\"bufferView\":12,\"componentType\":5126,\"count\":9,\"type\":\"VEC3\"},");
    jb_addf(&jb, "{\"bufferView\":13,\"componentType\":5126,\"count\":2,\"type\":\"SCALAR\",\"min\":[0],\"max\":[1.5]},");
    jb_addf(&jb, "{\"bufferView\":14,\"componentType\":5126,\"count\":2,\"type\":\"VEC3\"}],");

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
        free(jb.data);
        return;
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
    free(jb.data);
    // #endregion
}
