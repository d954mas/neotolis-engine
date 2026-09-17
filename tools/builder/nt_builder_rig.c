/* clang-format off */
#include "nt_builder_internal.h"
#include "hash/nt_hash.h"
#include "cgltf.h"
/* clang-format on */

#include <float.h>
#include <math.h>

/*
 * Rig import: which glTF nodes become joints, in which order, and what their
 * rest pose is. Skeleton space is glTF scene space -- the joints are every node
 * on the paths from the scene root of the joints' hierarchy to each skin joint,
 * identity wrappers included -- unless the caller cuts the hierarchy explicitly
 * with skeleton_root, whose parent space then becomes skeleton space.
 *
 * Every content failure is a logged diagnostic followed by NT_BUILD_ASSERT,
 * per the skeletal spec's builder policy.
 */

// #region matrix decomposition
/* Dividing a column by a length this small cannot recover a rotation. */
#define RIG_MIN_SCALE 1e-6
/* Normalized columns of a T*R*S matrix are orthonormal; anything above this is
 * authored shear, not float32 rounding of a conformant matrix. */
#define RIG_ORTHO_TOLERANCE 1e-5

/* glTF matrices are column-major: element (row, col) is m[(col * 4) + row]. */
static double rig_m(const float m[16], int row, int col) { return (double)m[(col * 4) + row]; }

/* Rotation part of a decomposition, normalized columns after the handedness
 * fix: rot[row][col]. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity) -- one linear pass per branch of the standard largest-diagonal extraction
static void rig_quat_from_rotation(const double rot[3][3], double q[4]) {
    const double trace = rot[0][0] + rot[1][1] + rot[2][2];
    if (trace > 0.0) {
        const double s = sqrt(trace + 1.0) * 2.0;
        q[0] = (rot[2][1] - rot[1][2]) / s;
        q[1] = (rot[0][2] - rot[2][0]) / s;
        q[2] = (rot[1][0] - rot[0][1]) / s;
        q[3] = 0.25 * s;
    } else if (rot[0][0] > rot[1][1] && rot[0][0] > rot[2][2]) {
        const double s = sqrt(1.0 + rot[0][0] - rot[1][1] - rot[2][2]) * 2.0;
        q[0] = 0.25 * s;
        q[1] = (rot[0][1] + rot[1][0]) / s;
        q[2] = (rot[0][2] + rot[2][0]) / s;
        q[3] = (rot[2][1] - rot[1][2]) / s;
    } else if (rot[1][1] > rot[2][2]) {
        const double s = sqrt(1.0 + rot[1][1] - rot[0][0] - rot[2][2]) * 2.0;
        q[0] = (rot[0][1] + rot[1][0]) / s;
        q[1] = 0.25 * s;
        q[2] = (rot[1][2] + rot[2][1]) / s;
        q[3] = (rot[0][2] - rot[2][0]) / s;
    } else {
        const double s = sqrt(1.0 + rot[2][2] - rot[0][0] - rot[1][1]) * 2.0;
        q[0] = (rot[0][2] + rot[2][0]) / s;
        q[1] = (rot[1][2] + rot[2][1]) / s;
        q[2] = 0.25 * s;
        q[3] = (rot[1][0] - rot[0][1]) / s;
    }

    const double len = sqrt((q[0] * q[0]) + (q[1] * q[1]) + (q[2] * q[2]) + (q[3] * q[3]));
    for (int c = 0; c < 4; c++) {
        q[c] /= len;
    }
    /* The branch that runs decides the sign, so one convention makes the
     * decomposed bits reproducible: w >= 0. */
    if (q[3] < 0.0) {
        for (int c = 0; c < 4; c++) {
            q[c] = -q[c];
        }
    }
    /* Negating a column or the whole quaternion turns an exact zero into -0,
     * which would make the stored rest bits depend on the branch taken. */
    for (int c = 0; c < 4; c++) {
        if (q[c] == 0.0) {
            q[c] = 0.0;
        }
    }
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity) -- NT_BUILD_ASSERT expansions dominate the count
void nt_builder_decompose_trs(const float m[16], const char *name, nt_skeletal_trs_t *out) {
    NT_BUILD_ASSERT(m && out && "invalid decompose_trs args");
    const char *label = (name != NULL) ? name : "(unnamed)";

    /* Column lengths in double: a float32 length of a float32 column loses the
     * precision the recompose check below spends its whole budget on. */
    double scale[3];
    double rot[3][3];
    for (int col = 0; col < 3; col++) {
        double len2 = 0.0;
        for (int row = 0; row < 3; row++) {
            const double v = rig_m(m, row, col);
            len2 += v * v;
        }
        scale[col] = sqrt(len2);
        if (!(scale[col] >= RIG_MIN_SCALE)) {
            NT_LOG_ERROR("node %s: matrix column %d has length %g, too small to decompose", label, col, scale[col]);
            NT_BUILD_ASSERT(0 && "matrix scale is degenerate");
        }
        for (int row = 0; row < 3; row++) {
            rot[row][col] = rig_m(m, row, col) / scale[col];
        }
    }

    for (int a = 0; a < 3; a++) {
        for (int b = a + 1; b < 3; b++) {
            double dot = 0.0;
            for (int row = 0; row < 3; row++) {
                dot += rot[row][a] * rot[row][b];
            }
            if (fabs(dot) > RIG_ORTHO_TOLERANCE) {
                NT_LOG_ERROR("node %s: matrix columns %d and %d are not perpendicular (dot %g)", label, a, b, dot);
                NT_BUILD_ASSERT(0 && "matrix is not TRS");
            }
        }
    }

    /* A mirrored basis is a negative scale, not a rotation: glTF allows it and
     * the X axis is where the convention puts the sign. */
    const double det = (rot[0][0] * ((rot[1][1] * rot[2][2]) - (rot[1][2] * rot[2][1]))) - (rot[0][1] * ((rot[1][0] * rot[2][2]) - (rot[1][2] * rot[2][0]))) +
                       (rot[0][2] * ((rot[1][0] * rot[2][1]) - (rot[1][1] * rot[2][0])));
    if (det < 0.0) {
        scale[0] = -scale[0];
        for (int row = 0; row < 3; row++) {
            rot[row][0] = -rot[row][0];
        }
    }

    double q[4];
    rig_quat_from_rotation(rot, q);

    for (int c = 0; c < 3; c++) {
        out->t[c] = m[12 + c];
        out->s[c] = (float)scale[c];
    }
    for (int c = 0; c < 4; c++) {
        out->q[c] = (float)q[c];
    }

    /* The decomposition is the rest pose only if it rebuilds the matrix the
     * artist authored; anything else is shear the runtime cannot represent. */
    nt_skeletal_mat34_t re;
    nt_skeletal_mat34_from_trs(out, &re);
    float max_abs = 1.0F;
    for (int row = 0; row < 3; row++) {
        for (int col = 0; col < 3; col++) {
            const float v = m[(col * 4) + row];
            const float a = (v < 0.0F) ? -v : v;
            if (a > max_abs) {
                max_abs = a;
            }
        }
    }
    const float tolerance = 64.0F * FLT_EPSILON * max_abs;
    for (int row = 0; row < 3; row++) {
        for (int col = 0; col < 3; col++) {
            const float d = re.r[row][col] - m[(col * 4) + row];
            if (((d < 0.0F) ? -d : d) > tolerance) {
                NT_LOG_ERROR("node %s: recomposed element (%d,%d) is %g, the matrix holds %g", label, row, col, (double)re.r[row][col], (double)m[(col * 4) + row]);
                NT_BUILD_ASSERT(0 && "matrix is not TRS");
            }
        }
        NT_BUILD_ASSERT(re.r[row][3] == m[12 + row] && "matrix translation survives decomposition exactly");
    }
}
// #endregion

// #region joint selection and preorder
typedef struct {
    const nt_glb_scene_t *scene;
    const cgltf_data *data;
    const uint8_t *mark;
    uint16_t *joint_of; /* scene node -> joint index */
    uint16_t *parent;
    uint16_t *subtree_end;
    uint32_t *node_index;
    uint16_t next;
} rig_walk_t;

/* Preorder over the marked nodes, children in glTF order, so each subtree is
 * the contiguous range [j, subtree_end[j]) the skeleton format requires. */
// NOLINTNEXTLINE(misc-no-recursion) -- the walk follows the node hierarchy, which the parse already proved acyclic
static void rig_visit(rig_walk_t *w, uint32_t node, uint16_t parent_joint) {
    const uint16_t j = w->next++;
    w->joint_of[node] = j;
    w->parent[j] = parent_joint;
    w->node_index[j] = node;

    const cgltf_node *cn = &w->data->nodes[node];
    for (cgltf_size i = 0; i < cn->children_count; i++) {
        const uint32_t child = (uint32_t)(cn->children[i] - w->data->nodes);
        if (w->mark[child] != 0U) {
            rig_visit(w, child, j);
        }
    }
    w->subtree_end[j] = w->next;
}

/* Marks the node and every ancestor up to the cut or to the scene root, and
 * returns the root the walk ended on. */
static uint32_t rig_mark_path(const nt_glb_scene_t *scene, uint8_t *mark, uint32_t node, uint32_t cut) {
    uint32_t cur = node;
    for (;;) {
        mark[cur] = 1U;
        if (cur == cut) {
            return cur;
        }
        const uint32_t parent = scene->nodes[cur].parent;
        if (parent == UINT32_MAX) {
            if (cut != UINT32_MAX) {
                NT_LOG_ERROR("import_rig: skin joint node[%u] is outside the subtree of skeleton_root node[%u]", node, cut);
                NT_BUILD_ASSERT(0 && "rig joint lies outside the skeleton root");
            }
            return cur;
        }
        cur = parent;
    }
}
// #endregion

// #region import
// NOLINTNEXTLINE(readability-function-cognitive-complexity) -- NT_BUILD_ASSERT expansions dominate the count
void nt_builder_import_rig(const nt_glb_scene_t *scene, const nt_builder_rig_selection_t *sel, nt_builder_rig_t *out) {
    NT_BUILD_ASSERT(scene && sel && out && "invalid import_rig args");
    const cgltf_data *data = (const cgltf_data *)scene->_internal;
    NT_BUILD_ASSERT(data != NULL && "import_rig: the scene holds no parsed glTF");

    if (sel->skin_index >= scene->skin_count) {
        NT_LOG_ERROR("import_rig: skin index %u, the scene has %u skins", sel->skin_index, scene->skin_count);
        NT_BUILD_ASSERT(0 && "rig skin index out of range");
    }
    const cgltf_skin *skin = &data->skins[sel->skin_index];
    const uint32_t palette_count = (uint32_t)skin->joints_count;
    NT_BUILD_ASSERT(palette_count >= 1 && palette_count <= UINT16_MAX && "skin palette size outside [1, 65535]");
    const uint32_t node_count = scene->node_count;
    NT_BUILD_ASSERT((sel->skeleton_root == UINT32_MAX || sel->skeleton_root < node_count) && "skeleton_root out of range");
    NT_BUILD_ASSERT((sel->object_node == UINT32_MAX || sel->object_node < node_count) && "object_node out of range");

    // #region select
    uint8_t *mark = (uint8_t *)calloc(node_count, 1);
    uint16_t *joint_of = (uint16_t *)calloc(node_count, sizeof(uint16_t));
    NT_BUILD_ASSERT(mark && joint_of && "import_rig: alloc failed (OOM)");

    uint32_t root = UINT32_MAX;
    for (uint32_t p = 0; p < palette_count; p++) {
        NT_BUILD_ASSERT(skin->joints[p] != NULL && "skin joint is null");
        const uint32_t node = (uint32_t)(skin->joints[p] - data->nodes);
        NT_BUILD_ASSERT(node < node_count && "skin joint is not a node of this scene");
        const uint32_t reached = rig_mark_path(scene, mark, node, sel->skeleton_root);
        if (root == UINT32_MAX) {
            root = reached;
        } else if (root != reached) {
            NT_LOG_ERROR("import_rig: skin joints reach scene roots node[%u] and node[%u]; one rig has one root", root, reached);
            NT_BUILD_ASSERT(0 && "rig joints span several scene roots");
        }
    }
    NT_BUILD_ASSERT(root != UINT32_MAX && "rig has no root");

    if (sel->object_node != UINT32_MAX && mark[sel->object_node] != 0U) {
        NT_LOG_ERROR("import_rig: object node[%u] is a joint of the rig; the object curve drives the character, not a bone", sel->object_node);
        NT_BUILD_ASSERT(0 && "object node lies inside the rig");
    }

    uint32_t joint_count = 0;
    for (uint32_t n = 0; n < node_count; n++) {
        joint_count += mark[n];
    }
    NT_BUILD_ASSERT(joint_count >= 1 && "rig has no joints");
    if (joint_count > UINT16_MAX) {
        NT_LOG_ERROR("import_rig: %u joints, the skeleton format addresses at most %u", joint_count, (uint32_t)UINT16_MAX);
        NT_BUILD_ASSERT(0 && "rig joint count exceeds the u16 joint index");
    }
    // #endregion

    // #region storage
    /* One allocation behind every array of the rig, the 4-aligned ones first so
     * the u16 tables need no padding of their own. */
    const size_t rest_bytes = (size_t)joint_count * sizeof(nt_skeletal_trs_t);
    const size_t id_bytes = (size_t)joint_count * sizeof(uint32_t);
    const size_t index_bytes = (size_t)joint_count * sizeof(uint32_t);
    const size_t parent_bytes = (size_t)joint_count * sizeof(uint16_t);
    const size_t palette_bytes = (size_t)palette_count * sizeof(uint16_t);
    uint8_t *storage = (uint8_t *)malloc(rest_bytes + id_bytes + index_bytes + (2U * parent_bytes) + palette_bytes);
    NT_BUILD_ASSERT(storage && "import_rig: alloc failed (OOM)");

    nt_skeletal_trs_t *rest = (nt_skeletal_trs_t *)storage;
    uint32_t *joint_id = (uint32_t *)(storage + rest_bytes);
    uint32_t *node_index = (uint32_t *)(storage + rest_bytes + id_bytes);
    uint16_t *parent = (uint16_t *)(storage + rest_bytes + id_bytes + index_bytes);
    uint16_t *subtree_end = (uint16_t *)(storage + rest_bytes + id_bytes + index_bytes + parent_bytes);
    uint16_t *palette_joint = (uint16_t *)(storage + rest_bytes + id_bytes + index_bytes + (2U * parent_bytes));
    // #endregion

    // #region preorder and rest pose
    rig_walk_t walk = {
        .scene = scene,
        .data = data,
        .mark = mark,
        .joint_of = joint_of,
        .parent = parent,
        .subtree_end = subtree_end,
        .node_index = node_index,
        .next = 0,
    };
    rig_visit(&walk, root, NT_SKELETAL_NO_PARENT);
    NT_BUILD_ASSERT(walk.next == joint_count && "preorder visited a different joint set than the selection marked");

    for (uint32_t j = 0; j < joint_count; j++) {
        const uint32_t node = node_index[j];
        const nt_glb_node_t *gn = &scene->nodes[node];
        if (gn->name == NULL || gn->name[0] == '\0') {
            NT_LOG_ERROR("import_rig: rig node[%u] has no name, and a joint id is the hash of its name", node);
            NT_BUILD_ASSERT(0 && "rig node has no name");
        }
        joint_id[j] = nt_hash32_str(gn->name).value;
        if (gn->has_matrix) {
            nt_builder_decompose_trs(data->nodes[node].matrix, gn->name, &rest[j]);
        } else {
            memcpy(rest[j].t, gn->local_t, sizeof(rest[j].t));
            memcpy(rest[j].q, gn->local_q, sizeof(rest[j].q));
            memcpy(rest[j].s, gn->local_s, sizeof(rest[j].s));
        }
    }

    for (uint32_t p = 0; p < palette_count; p++) {
        const uint32_t node = (uint32_t)(skin->joints[p] - data->nodes);
        palette_joint[p] = joint_of[node];
    }
    // #endregion

    out->skeleton = (nt_skeletal_skeleton_t){
        .rig_compat_id = (nt_hash64_t){0},
        .parent = parent,
        .subtree_end = subtree_end,
        .joint_id = joint_id,
        .rest = rest,
        .joint_count = (uint16_t)joint_count,
    };
    {
        const uint32_t scratch_size = NT_SKELETAL_RIG_ID_BYTES(joint_count);
        void *scratch = malloc(scratch_size);
        NT_BUILD_ASSERT(scratch && "import_rig: alloc failed (OOM)");
        out->skeleton.rig_compat_id = nt_skeletal_rig_compat_id(&out->skeleton, scratch, scratch_size);
        free(scratch);
    }
    out->node_index = node_index;
    out->palette_joint = palette_joint;
    out->skin_index = sel->skin_index;
    out->object_node = sel->object_node;
    out->palette_count = (uint16_t)palette_count;
    out->storage = storage;

    free(joint_of);
    free(mark);

    NT_LOG_INFO("Imported rig from skin[%u]: %u joints, %u palette entries, rig 0x%016llX", sel->skin_index, joint_count, palette_count, (unsigned long long)out->skeleton.rig_compat_id.value);
}

void nt_builder_free_rig(nt_builder_rig_t *rig) {
    if (rig == NULL) {
        return;
    }
    free(rig->storage);
    memset(rig, 0, sizeof(*rig));
}
// #endregion
