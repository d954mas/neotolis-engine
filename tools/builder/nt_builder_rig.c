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

    for (int i = 0; i < 16; i++) {
        if (!nt_builder_finite(m[i])) {
            NT_LOG_ERROR("node %s: matrix element %d is not a finite number", label, i);
            NT_BUILD_ASSERT(0 && "matrix is not finite");
        }
    }
    /* A projective bottom row has no TRS; column-major puts it at 3, 7, 11, 15. */
    if (m[3] != 0.0F || m[7] != 0.0F || m[11] != 0.0F || m[15] != 1.0F) {
        NT_LOG_ERROR("node %s: matrix bottom row is (%g, %g, %g, %g), an affine matrix ends in (0, 0, 0, 1)", label, (double)m[3], (double)m[7], (double)m[11], (double)m[15]);
        NT_BUILD_ASSERT(0 && "matrix is not affine");
    }

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

    /* Anything the recompose does not rebuild is shear the runtime cannot
     * represent. The budget follows each column: float rounding is a fraction
     * of the column length, so a 0.01-scale wrapper gets no extra absolute slack. */
    nt_skeletal_mat34_t re;
    nt_skeletal_mat34_from_trs(out, &re);
    for (int row = 0; row < 3; row++) {
        for (int col = 0; col < 3; col++) {
            const float tolerance = 64.0F * FLT_EPSILON * (float)fabs(scale[col]);
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
    const cgltf_data *data;
    const uint8_t *mark;
    uint16_t *joint_of; /* scene node -> joint index */
    uint16_t *parent;
    uint16_t *subtree_end;
    uint32_t *node_index;
    uint16_t next;
} rig_walk_t;

/* Preorder over the marked nodes, children in glTF order, so each subtree is
 * the contiguous range [j, subtree_end[j]) the skeleton format requires. The
 * recursion is as deep as the joint chain, so the chain is capped well below
 * the native stack. */
#define RIG_MAX_DEPTH 256U

// NOLINTNEXTLINE(misc-no-recursion) -- the walk follows the node hierarchy, which cgltf_validate proved acyclic
static void rig_visit(rig_walk_t *w, uint32_t node, uint16_t parent_joint, uint32_t depth) {
    if (depth > RIG_MAX_DEPTH) {
        NT_LOG_ERROR("import_rig: node[%u] sits %u joints below the rig root, the importer walks at most %u", node, depth, RIG_MAX_DEPTH);
        NT_BUILD_ASSERT(0 && "rig hierarchy is too deep");
    }
    const uint16_t j = w->next++;
    w->joint_of[node] = j;
    w->parent[j] = parent_joint;
    w->node_index[j] = node;

    const cgltf_node *cn = &w->data->nodes[node];
    for (cgltf_size i = 0; i < cn->children_count; i++) {
        const uint32_t child = (uint32_t)(cn->children[i] - w->data->nodes);
        if (w->mark[child] != 0U) {
            rig_visit(w, child, j, depth + 1U);
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
typedef struct {
    uint32_t id;
    uint32_t joint;
} rig_id_slot_t;

/* By id, then by joint, so a collision names the lower joint first. */
static int rig_id_slot_cmp(const void *a, const void *b) {
    const rig_id_slot_t *x = (const rig_id_slot_t *)a;
    const rig_id_slot_t *y = (const rig_id_slot_t *)b;
    if (x->id != y->id) {
        return (x->id > y->id) ? 1 : -1;
    }
    return (x->joint > y->joint) - (x->joint < y->joint);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity) -- NT_BUILD_ASSERT expansions dominate the count
void nt_builder_import_rig(const nt_glb_scene_t *scene, uint32_t skin_index, uint32_t skeleton_root, nt_builder_rig_t *out) {
    NT_BUILD_ASSERT(scene && out && "invalid import_rig args");
    const cgltf_data *data = (const cgltf_data *)scene->_internal;
    NT_BUILD_ASSERT(data != NULL && "import_rig: the scene holds no parsed glTF");

    if (skin_index >= (uint32_t)data->skins_count) {
        NT_LOG_ERROR("import_rig: skin index %u, the scene has %u skins", skin_index, (uint32_t)data->skins_count);
        NT_BUILD_ASSERT(0 && "rig skin index out of range");
    }
    const cgltf_skin *skin = &data->skins[skin_index];
    if (skin->joints_count == 0) {
        NT_LOG_ERROR("import_rig: skin[%u] has no joints", skin_index);
        NT_BUILD_ASSERT(0 && "skin has no joints");
    }
    const uint32_t palette_count = (uint32_t)skin->joints_count;
    if (palette_count > UINT16_MAX) {
        NT_LOG_ERROR("import_rig: skin[%u] lists %u joints, the palette index is a u16", skin_index, palette_count);
        NT_BUILD_ASSERT(0 && "skin palette exceeds the u16 palette index");
    }
    const uint32_t node_count = scene->node_count;
    NT_BUILD_ASSERT((skeleton_root == UINT32_MAX || skeleton_root < node_count) && "skeleton_root out of range");

    // #region select
    uint8_t *mark = (uint8_t *)calloc(node_count, 1);
    uint8_t *in_palette = (uint8_t *)calloc(node_count, 1);
    uint16_t *joint_of = (uint16_t *)calloc(node_count, sizeof(uint16_t));
    NT_BUILD_ASSERT(mark && in_palette && joint_of && "import_rig: alloc failed (OOM)");

    uint32_t root = UINT32_MAX;
    for (uint32_t p = 0; p < palette_count; p++) {
        NT_BUILD_ASSERT(skin->joints[p] != NULL && "skin joint is null");
        const uint32_t node = (uint32_t)(skin->joints[p] - data->nodes);
        NT_BUILD_ASSERT(node < node_count && "skin joint is not a node of this scene");
        /* glTF lists each joint once; twice would give one joint two palette
         * entries, each free to carry its own inverse bind. */
        if (in_palette[node] != 0U) {
            NT_LOG_ERROR("import_rig: skin[%u] lists node[%u] twice in its joints", skin_index, node);
            NT_BUILD_ASSERT(0 && "skin lists one joint twice");
        }
        in_palette[node] = 1U;
        const uint32_t reached = rig_mark_path(scene, mark, node, skeleton_root);
        if (root == UINT32_MAX) {
            root = reached;
        } else if (root != reached) {
            NT_LOG_ERROR("import_rig: skin joints reach scene roots node[%u] and node[%u]; one rig has one root", root, reached);
            NT_BUILD_ASSERT(0 && "rig joints span several scene roots");
        }
    }
    NT_BUILD_ASSERT(root != UINT32_MAX && "rig has no root");

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
    const size_t parent_bytes = (size_t)joint_count * sizeof(uint16_t);
    const size_t palette_bytes = (size_t)palette_count * sizeof(uint16_t);
    uint8_t *storage = (uint8_t *)malloc(rest_bytes + id_bytes + (2U * parent_bytes) + palette_bytes);
    uint32_t *node_index = (uint32_t *)malloc((size_t)joint_count * sizeof(uint32_t));
    NT_BUILD_ASSERT(storage && node_index && "import_rig: alloc failed (OOM)");

    nt_skeletal_trs_t *rest = (nt_skeletal_trs_t *)storage;
    uint32_t *joint_id = (uint32_t *)(storage + rest_bytes);
    uint16_t *parent = (uint16_t *)(storage + rest_bytes + id_bytes);
    uint16_t *subtree_end = (uint16_t *)(storage + rest_bytes + id_bytes + parent_bytes);
    uint16_t *palette_joint = (uint16_t *)(storage + rest_bytes + id_bytes + (2U * parent_bytes));
    // #endregion

    // #region preorder, rest pose and ids
    rig_walk_t walk = {
        .data = data,
        .mark = mark,
        .joint_of = joint_of,
        .parent = parent,
        .subtree_end = subtree_end,
        .node_index = node_index,
        .next = 0,
    };
    rig_visit(&walk, root, NT_SKELETAL_NO_PARENT, 0);
    NT_BUILD_ASSERT(walk.next == joint_count && "preorder visited a different joint set than the selection marked");

    for (uint32_t j = 0; j < joint_count; j++) {
        const uint32_t node = node_index[j];
        const cgltf_node *cn = &data->nodes[node];
        if (cn->name == NULL || cn->name[0] == '\0') {
            NT_LOG_ERROR("import_rig: rig node[%u] has no name, and a joint id is the hash of its name", node);
            NT_BUILD_ASSERT(0 && "rig node has no name");
        }
        joint_id[j] = nt_hash32_str(cn->name).value;
        /* glTF allows one form of transform per node; a matrix next to TRS
         * would have the TRS silently lose. */
        if (cn->has_matrix && (cn->has_translation || cn->has_rotation || cn->has_scale)) {
            NT_LOG_ERROR("import_rig: node %s carries both a matrix and translation/rotation/scale", cn->name);
            NT_BUILD_ASSERT(0 && "rig node has both a matrix and TRS");
        }
        if (cn->has_matrix) {
            nt_builder_decompose_trs(cn->matrix, cn->name, &rest[j]);
        } else {
            memcpy(rest[j].t, cn->translation, sizeof(rest[j].t));
            memcpy(rest[j].q, cn->rotation, sizeof(rest[j].q));
            memcpy(rest[j].s, cn->scale, sizeof(rest[j].s));
        }
        /* The encoder asserts the same two rules; here they name the node. */
        if (!nt_builder_finite_n(rest[j].t, 3) || !nt_builder_finite_n(rest[j].s, 3)) {
            NT_LOG_ERROR("import_rig: node %s has a non-finite rest translation or scale", cn->name);
            NT_BUILD_ASSERT(0 && "rest translation or scale is not finite");
        }
        if (!nt_builder_unit_quat(rest[j].q)) {
            NT_LOG_ERROR("import_rig: node %s rest rotation (%g, %g, %g, %g) is not a unit quaternion", cn->name, (double)rest[j].q[0], (double)rest[j].q[1], (double)rest[j].q[2],
                         (double)rest[j].q[3]);
            NT_BUILD_ASSERT(0 && "rest rotation is not a unit quaternion");
        }
    }

    /* Joint ids are name hashes, so two rig nodes may collide by name or by
     * hash; either way the skeleton could not tell them apart. */
    {
        rig_id_slot_t *slots = (rig_id_slot_t *)malloc((size_t)joint_count * sizeof(rig_id_slot_t));
        NT_BUILD_ASSERT(slots && "import_rig: alloc failed (OOM)");
        for (uint32_t j = 0; j < joint_count; j++) {
            slots[j] = (rig_id_slot_t){.id = joint_id[j], .joint = j};
        }
        qsort(slots, joint_count, sizeof(rig_id_slot_t), rig_id_slot_cmp);
        for (uint32_t i = 1; i < joint_count; i++) {
            if (slots[i].id == slots[i - 1U].id) {
                const uint32_t a = slots[i - 1U].joint;
                const uint32_t b = slots[i].joint;
                NT_LOG_ERROR("import_rig: rig nodes \"%s\" (node[%u]) and \"%s\" (node[%u]) share joint id 0x%08X", data->nodes[node_index[a]].name, node_index[a], data->nodes[node_index[b]].name,
                             node_index[b], slots[i].id);
                NT_BUILD_ASSERT(0 && "two rig nodes share one joint id");
            }
        }
        free(slots);
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
    out->scene = scene;
    out->palette_joint = palette_joint;
    out->skin_index = skin_index;
    out->palette_count = (uint16_t)palette_count;
    out->storage = storage;

    free(node_index);
    free(joint_of);
    free(in_palette);
    free(mark);

    NT_LOG_INFO("Imported rig from skin[%u]: %u joints, %u palette entries, rig 0x%016llX", skin_index, joint_count, palette_count, (unsigned long long)out->skeleton.rig_compat_id.value);
}

void nt_builder_free_rig(nt_builder_rig_t *rig) {
    if (rig == NULL) {
        return;
    }
    free(rig->storage);
    memset(rig, 0, sizeof(*rig));
}
// #endregion

// #region skinned mesh
// NOLINTNEXTLINE(readability-function-cognitive-complexity) -- NT_BUILD_ASSERT expansions dominate the count
void nt_builder_add_scene_skinned_mesh(NtBuilderContext *ctx, const nt_builder_rig_t *rig, uint32_t mesh_index, uint32_t primitive_index, float skin_drop_tolerance, const char *resource_id,
                                       const nt_mesh_opts_t *opts) {
    NT_BUILD_ASSERT(ctx && rig && rig->scene && resource_id && opts && opts->layout && "invalid scene_skinned_mesh args");
    const nt_glb_scene_t *scene = rig->scene;
    NT_BUILD_ASSERT(mesh_index < scene->mesh_count && "mesh_index out of range");
    NT_BUILD_ASSERT(primitive_index < scene->meshes[mesh_index].primitive_count && "primitive_index out of range");
    NT_BUILD_ASSERT(rig->palette_count >= 1 && "rig has no palette entries");
    NT_BUILD_ASSERT(skin_drop_tolerance >= 0.0F && skin_drop_tolerance <= 1.0F && "skin_drop_tolerance must lie in [0, 1]");

    /* The joint lanes address this rig's palette, so a mesh no node instantiates
     * with this skin would ship indices into someone else's joints. */
    bool skinned_here = false;
    for (uint32_t n = 0; n < scene->node_count && !skinned_here; n++) {
        skinned_here = scene->nodes[n].mesh_index == mesh_index && scene->nodes[n].skin_index == rig->skin_index;
    }
    if (!skinned_here) {
        NT_LOG_ERROR("add_scene_skinned_mesh: no node instantiates mesh[%u] with skin[%u], the skin of this rig", mesh_index, rig->skin_index);
        NT_BUILD_ASSERT(0 && "mesh is not skinned by this rig's skin");
    }

    const nt_builder_skin_ctx_t skin = {.palette_count = rig->palette_count, .drop_tolerance = skin_drop_tolerance};
    uint8_t *mesh_data = NULL;
    uint32_t mesh_size = 0;
    const nt_build_result_t r = nt_builder_decode_scene_mesh_skinned(scene, mesh_index, primitive_index, opts->layout, opts->stream_count, opts->tangent_mode, &skin, &mesh_data, &mesh_size);
    NT_BUILD_ASSERT(r == NT_BUILD_OK && "add_scene_skinned_mesh: decode failed");

    const uint64_t hash = nt_hash64(mesh_data, mesh_size).value;
    nt_builder_add_entry(ctx, resource_id, NT_BUILD_ASSET_MESH, NULL, mesh_data, mesh_size, hash);
}
// #endregion

// #region skin binding
/* A bound measured in double must still contain its vertices once stored as
 * float, so the conversion rounds towards +infinity. */
static float rig_round_up(double x) {
    const float f = (float)x;
    return ((double)f < x) ? nextafterf(f, INFINITY) : f;
}

/* |inverse_bind * v| with v a mesh-space point: how far the vertex sits from
 * the joint it is bound to, which is what reach bounds. */
static double rig_bound_distance(const nt_skeletal_mat34_t *ib, const float v[3]) {
    double len2 = 0.0;
    for (int r = 0; r < 3; r++) {
        const double e = ((double)ib->r[r][0] * (double)v[0]) + ((double)ib->r[r][1] * (double)v[1]) + ((double)ib->r[r][2] * (double)v[2]) + (double)ib->r[r][3];
        len2 += e * e;
    }
    return sqrt(len2);
}

/* Over every primitive of every node this skin deforms, every vertex and every
 * source influence with a non-zero weight -- all sets, unreduced: the top-four
 * reduction is a property of one exported mesh, the binding is shared. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity) -- five nested ranges plus NT_BUILD_ASSERT expansions
static double rig_skin_reach(const nt_glb_scene_t *scene, const cgltf_data *data, const nt_builder_rig_t *rig, const nt_skeletal_mat34_t *inverse_bind) {
    double reach = 0.0;
    bool scanned = false;
    for (uint32_t n = 0; n < scene->node_count; n++) {
        if (scene->nodes[n].skin_index != rig->skin_index || scene->nodes[n].mesh_index == UINT32_MAX) {
            continue;
        }
        const uint32_t mesh_index = scene->nodes[n].mesh_index;
        const cgltf_mesh *mesh = &data->meshes[mesh_index];
        for (cgltf_size pi = 0; pi < mesh->primitives_count; pi++) {
            const cgltf_primitive *prim = &mesh->primitives[pi];
            char label[64];
            (void)snprintf(label, sizeof(label), "mesh[%u] prim[%u]", mesh_index, (uint32_t)pi);

            const cgltf_accessor *pos = cgltf_find_accessor(prim, cgltf_attribute_type_position, 0);
            if (pos == NULL || pos->count == 0) {
                NT_LOG_ERROR("%s: skinned primitive has no POSITION accessor, so its reach cannot be measured", label);
                NT_BUILD_ASSERT(0 && "skinned primitive has no positions");
            }
            if (pos->type != cgltf_type_vec3) {
                NT_LOG_ERROR("%s: POSITION accessor has type %d, a position is a VEC3", label, (int)pos->type);
                NT_BUILD_ASSERT(0 && "POSITION accessor is not VEC3");
            }
            const uint32_t vertex_count = (uint32_t)pos->count;
            const cgltf_size want = (cgltf_size)vertex_count * 3U;
            float *xyz = (float *)calloc(want, sizeof(float));
            NT_BUILD_ASSERT(xyz && "skin reach: alloc failed (OOM)");
            const cgltf_size got = cgltf_accessor_unpack_floats(pos, xyz, want);
            if (got != want) {
                NT_LOG_ERROR("%s: POSITION unpacked %u of %u floats", label, (uint32_t)got, (uint32_t)want);
                NT_BUILD_ASSERT(0 && "POSITION accessor could not be unpacked as VEC3");
            }

            nt_builder_influences_t inf;
            nt_builder_read_influences(prim, label, vertex_count, &inf);
            const size_t set_floats = (size_t)vertex_count * 4U;
            for (uint32_t v = 0; v < vertex_count; v++) {
                for (uint32_t s = 0; s < inf.set_count; s++) {
                    for (uint32_t c = 0; c < 4U; c++) {
                        const size_t at = ((size_t)s * set_floats) + ((size_t)v * 4U) + c;
                        if (inf.weights[at] == 0.0F) {
                            continue;
                        }
                        const uint32_t p = (uint32_t)inf.joints[at];
                        if (p >= (uint32_t)rig->palette_count) {
                            NT_LOG_ERROR("%s: vertex %u addresses palette entry %u, the skin has %u", label, v, p, (uint32_t)rig->palette_count);
                            NT_BUILD_ASSERT(0 && "joint index lies outside the palette");
                        }
                        const double d = rig_bound_distance(&inverse_bind[p], &xyz[(size_t)v * 3U]);
                        if (!isfinite(d)) {
                            NT_LOG_ERROR("%s: vertex %u bound to palette entry %u sits at a non-finite distance from its joint", label, v, p);
                            NT_BUILD_ASSERT(0 && "skinned position is not finite");
                        }
                        if (d > reach) {
                            reach = d;
                        }
                    }
                }
            }
            nt_builder_free_influences(&inf);
            free(xyz);
            scanned = true;
        }
    }
    if (!scanned) {
        NT_LOG_ERROR("add_scene_skin_binding: no node instantiates a mesh with skin[%u], so the binding would carry a zero reach", rig->skin_index);
        NT_BUILD_ASSERT(0 && "no mesh is skinned by this rig's skin");
    }
    return reach;
}

/* Section 14 over the rest hierarchy: a bounds accumulated stretch, d bounds
 * distance from the root, and the binding stores the palette maximum. parent[j]
 * precedes j, so one forward pass is the whole recurrence. */
static double rig_any_pose_radius(const nt_skeletal_skeleton_t *skel, const uint16_t *remap, uint16_t palette_count, double reach) {
    const uint32_t joint_count = skel->joint_count;
    double *stretch = (double *)calloc(joint_count, sizeof(double));
    double *distance = (double *)calloc(joint_count, sizeof(double));
    NT_BUILD_ASSERT(stretch && distance && "any_pose_radius: alloc failed (OOM)");

    for (uint32_t j = 0; j < joint_count; j++) {
        const nt_skeletal_trs_t *rest = &skel->rest[j];
        double m = 0.0;
        for (int c = 0; c < 3; c++) {
            const double s = fabs((double)rest->s[c]);
            if (s > m) {
                m = s;
            }
        }
        const uint16_t parent = skel->parent[j];
        if (parent == NT_SKELETAL_NO_PARENT) {
            stretch[j] = m;
            distance[j] = 0.0;
        } else {
            double t2 = 0.0;
            for (int c = 0; c < 3; c++) {
                t2 += (double)rest->t[c] * (double)rest->t[c];
            }
            stretch[j] = stretch[parent] * m;
            distance[j] = distance[parent] + (stretch[parent] * sqrt(t2));
        }
    }

    double radius = 0.0;
    for (uint16_t p = 0; p < palette_count; p++) {
        const uint16_t j = remap[p];
        const double r = distance[j] + (stretch[j] * reach);
        if (r > radius) {
            radius = r;
        }
    }
    free(stretch);
    free(distance);
    return radius;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity) -- NT_BUILD_ASSERT expansions dominate the count
void nt_builder_add_scene_skin_binding(NtBuilderContext *ctx, const nt_builder_rig_t *rig, const char *resource_id) {
    NT_BUILD_ASSERT(ctx && rig && rig->scene && resource_id && "invalid scene_skin_binding args");
    const nt_glb_scene_t *scene = rig->scene;
    const cgltf_data *data = (const cgltf_data *)scene->_internal;
    NT_BUILD_ASSERT(data != NULL && "add_scene_skin_binding: the scene holds no parsed glTF");
    NT_BUILD_ASSERT(rig->skin_index < (uint32_t)data->skins_count && "rig skin index out of range");
    const uint32_t palette_count = rig->palette_count;
    NT_BUILD_ASSERT(palette_count >= 1 && "rig has no palette entries");
    const cgltf_skin *skin = &data->skins[rig->skin_index];
    NT_BUILD_ASSERT((uint32_t)skin->joints_count == palette_count && "the rig's palette does not match its skin");

    nt_skeletal_mat34_t *inverse_bind = (nt_skeletal_mat34_t *)calloc(palette_count, sizeof(nt_skeletal_mat34_t));
    NT_BUILD_ASSERT(inverse_bind && "add_scene_skin_binding: alloc failed (OOM)");

    // #region inverse binds
    const cgltf_accessor *ibm = skin->inverse_bind_matrices;
    if (ibm == NULL) {
        /* glTF: an absent accessor means identity, never inverse(rest). */
        for (uint32_t p = 0; p < palette_count; p++) {
            for (int r = 0; r < 3; r++) {
                inverse_bind[p].r[r][r] = 1.0F;
            }
        }
    } else {
        if (ibm->type != cgltf_type_mat4 || ibm->component_type != cgltf_component_type_r_32f || (uint32_t)ibm->count < palette_count) {
            NT_LOG_ERROR("add_scene_skin_binding: inverseBindMatrices must be MAT4 FLOAT over at least %u joints, the skin declares %u elements of type %d", palette_count, (uint32_t)ibm->count,
                         (int)ibm->type);
            NT_BUILD_ASSERT(0 && "inverseBindMatrices accessor is invalid");
        }
        /* Only the palette's prefix is read. A sparse accessor is unpacked whole:
         * its second pass writes wherever its indices point. */
        const cgltf_size want = (ibm->is_sparse ? ibm->count : (cgltf_size)palette_count) * 16U;
        float *m = (float *)calloc(want, sizeof(float));
        NT_BUILD_ASSERT(m && "add_scene_skin_binding: alloc failed (OOM)");
        const cgltf_size got = cgltf_accessor_unpack_floats(ibm, m, want);
        if (got != want) {
            NT_LOG_ERROR("add_scene_skin_binding: inverseBindMatrices unpacked %u of %u floats", (uint32_t)got, (uint32_t)want);
            NT_BUILD_ASSERT(0 && "inverseBindMatrices could not be unpacked");
        }
        for (uint32_t p = 0; p < palette_count; p++) {
            const float *src = &m[(size_t)p * 16U];
            for (int i = 0; i < 16; i++) {
                if (!nt_builder_finite(src[i])) {
                    NT_LOG_ERROR("add_scene_skin_binding: inverse bind matrix %u element %d is not a finite number", p, i);
                    NT_BUILD_ASSERT(0 && "inverse bind matrix is not finite");
                }
            }
            /* The 3x4 view drops the bottom row, so a projective one would
             * vanish silently; column-major puts it at 3, 7, 11, 15. */
            if (src[3] != 0.0F || src[7] != 0.0F || src[11] != 0.0F || src[15] != 1.0F) {
                NT_LOG_ERROR("add_scene_skin_binding: inverse bind matrix %u bottom row is (%g, %g, %g, %g), an affine matrix ends in (0, 0, 0, 1)", p, (double)src[3], (double)src[7], (double)src[11],
                             (double)src[15]);
                NT_BUILD_ASSERT(0 && "inverse bind matrix is not affine");
            }
            nt_skeletal_mat34_from_mat4(src, &inverse_bind[p]);
        }
        free(m);
    }
    // #endregion

    const double reach = rig_skin_reach(scene, data, rig, inverse_bind);
    const nt_skin_binding_t binding = {
        .rig_compat_id = rig->skeleton.rig_compat_id,
        .remap = rig->palette_joint,
        .inverse_bind = inverse_bind,
        .reach = rig_round_up(reach),
        .any_pose_radius = rig_round_up(rig_any_pose_radius(&rig->skeleton, rig->palette_joint, (uint16_t)palette_count, reach)),
        .palette_count = (uint16_t)palette_count,
    };
    nt_builder_add_skin_binding(ctx, &binding, resource_id);

    NT_LOG_INFO("Imported skin binding from skin[%u]: %u palette entries, reach %g, any-pose radius %g", rig->skin_index, palette_count, (double)binding.reach, (double)binding.any_pose_radius);
    free(inverse_bind);
}
// #endregion
