#ifndef NT_TEST_HELPER_RIGGED_GLB_H
#define NT_TEST_HELPER_RIGGED_GLB_H

#include <stdbool.h>

/* Writes a rigged .glb (one JSON chunk + one BIN chunk) for the rig, skin and
 * skinned-mesh import tests. Every knob is off by default, so the default file
 * is the valid fixture and each knob injects exactly one defect.
 *
 * Node table (index order is also the glTF "nodes" order):
 *
 *   0  "Root"      matrix, pure rotation 90 deg about Y; the single scene root
 *   1  "Helper"    matrix, rotation 90 deg about Z * scale (2, 1, 0.5),
 *                  translation (0.25, -0.5, 1); child of Root
 *   2  "Joint0"    TRS, child of Helper, children Joint1 and Joint3
 *   3  "Joint1"    TRS, child of Joint0, child Joint2
 *   4  "Joint2"    TRS, child of Joint1
 *   5  "Joint3"    TRS, child of Joint0, child Joint4
 *   6  "Joint4"    TRS, child of Joint3
 *   7  "MeshNode"  TRS (2, 0, -1) / 45 deg about Y / scale 1.5; child of Root,
 *                  instantiates mesh 0 with skin 0; not a joint
 *   8  "Object"    TRS (0, 0, 5); child of Root, outside the rig
 *
 * Only Root and Helper carry a glTF "matrix"; every other node carries TRS.
 * Rest values of the two first skin joints are the published test vector of
 * skeletal-animation.md 3.1:
 *
 *   Joint0  t (1, 2, 3)    q (0, 0, 0, 1)                       s (1, 1, 1)
 *   Joint1  t (0, -0, 0.5) q (0, 0, -0.70710678, -0.70710678)   s (1, 1, 1)
 *
 * The remaining joints are ordinary non-identity TRS nodes:
 *
 *   Joint2  t (0, 0.75, 0)   q (0.70710678, 0, 0, 0.70710678)   s (1, 1, 1)
 *   Joint3  t (-0.5, 0.25, 0) q identity                        s (1, 1, 1)
 *   Joint4  t (0, 0.5, 0)    q identity                         s (0.5, 0.5, 0.5)
 *
 * Skin "RigSkin" (skin 0) has joints [2, 3, 4, 5, 6], so palette entry p maps
 * to node p + 2. Its inverse bind matrix p is a pure translation
 * (-(p + 1), 0.5 * p, -1), which no import step can reproduce by accident.
 *
 * Mesh 0 is one indexed quad, 4 vertices, indices {0, 1, 2, 0, 2, 3}, with
 * POSITION / NORMAL / TEXCOORD_0 / JOINTS_0 / JOINTS_1 / WEIGHTS_0 / WEIGHTS_1.
 * JOINTS are UNSIGNED_BYTE VEC4 (not normalized), WEIGHTS are FLOAT VEC4.
 * Weights are exact in binary32 and every vertex sums to 1. Later tests assert
 * on these numbers:
 *
 *   v0  five influences, dropped mass 0.01 (below the 0.02 default tolerance)
 *       JOINTS_0 (0, 1, 2, 3)  WEIGHTS_0 (0.40, 0.30, 0.20, 0.09)
 *       JOINTS_1 (4, 0, 0, 0)  WEIGHTS_1 (0.01, 0, 0, 0)
 *   v1  tie: three influences share weight 0.10, two of them fit
 *       JOINTS_0 (1, 0, 3, 2)  WEIGHTS_0 (0.40, 0.30, 0.10, 0.10)
 *       JOINTS_1 (4, 0, 0, 0)  WEIGHTS_1 (0.10, 0, 0, 0)
 *       top 4 = joints 1, 0 and the two lowest tied indices 2 and 3; joint 4 is
 *       dropped
 *   v2  five influences, dropped mass 0.05 (above the 0.02 default tolerance)
 *       JOINTS_0 (0, 1, 2, 3)  WEIGHTS_0 (0.40, 0.30, 0.15, 0.10)
 *       JOINTS_1 (4, 0, 0, 0)  WEIGHTS_1 (0.05, 0, 0, 0)
 *   v3  two influences; every per-vertex defect knob mutates this vertex
 *       JOINTS_0 (0, 1, 0, 0)  WEIGHTS_0 (0.75, 0.25, 0, 0)
 *       JOINTS_1 (0, 0, 0, 0)  WEIGHTS_1 (0, 0, 0, 0)
 *
 * Animations:
 *
 *   0 "Walk" duration 0.5, keys at 0, 0.25, 0.5
 *       Joint1 rotation LINEAR, Joint2 translation STEP,
 *       Joint3 scale CUBICSPLINE (9 output elements), Object translation LINEAR
 *   1 "Idle" duration 1.5, keys at 0 and 1.5, one constant translation channel
 *       on Joint0
 */

#define RIGGED_GLB_NODE_ROOT 0
#define RIGGED_GLB_NODE_HELPER 1
#define RIGGED_GLB_NODE_JOINT0 2
#define RIGGED_GLB_NODE_JOINT1 3
#define RIGGED_GLB_NODE_JOINT2 4
#define RIGGED_GLB_NODE_JOINT3 5
#define RIGGED_GLB_NODE_JOINT4 6
#define RIGGED_GLB_NODE_MESH 7
#define RIGGED_GLB_NODE_OBJECT 8
#define RIGGED_GLB_NODE_COUNT 9

#define RIGGED_GLB_SKIN_JOINT_COUNT 5
#define RIGGED_GLB_VERTEX_COUNT 4
#define RIGGED_GLB_INDEX_COUNT 6

#define RIGGED_GLB_WALK_DURATION 0.5F
#define RIGGED_GLB_IDLE_DURATION 1.5F

/* One defect per knob; all false writes the valid fixture. */
typedef struct {
    bool matrix_shear;       /* Helper's matrix gets a 1e-3 shear, so it is not TRS */
    bool unnamed_node;       /* Helper loses its name */
    bool cycle;              /* Helper moves under Joint4, closing a parent cycle */
    bool joint_outside_root; /* Object joins the skin, outside a cut at Helper */
    bool multi_root;         /* Object becomes a second scene root and a skin joint */
    bool negative_weight;    /* v3 weight 1 turns negative */
    bool nan_weight;         /* v3 weight 1 turns NaN */
    bool index_ge_palette;   /* v3 joint 1 addresses palette entry 5 */
    bool unpaired_sets;      /* JOINTS_1 without WEIGHTS_1 */
    bool duplicate_joint;    /* v3 weights joint 0 twice with non-zero weight */
    bool zero_weights;       /* v3 weights sum to zero */
    bool joints_float_type;  /* JOINTS accessors become FLOAT */
    bool morph_target;       /* the skinned primitive gains a morph target */
    bool no_indices;         /* the primitive drops its index accessor */
    bool mesh_other_skin;    /* MeshNode uses a second skin; skin 0 has no mesh */
    bool no_ibm;             /* skin 0 drops inverseBindMatrices */
} rigged_glb_opts_t;

/* opts may be NULL, which means every knob off. */
void rigged_glb_write(const char *path, const rigged_glb_opts_t *opts);

#endif /* NT_TEST_HELPER_RIGGED_GLB_H */
