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
 *   2  "Joint0"    TRS, child of Helper, children [Joint3, Joint1] in that order
 *   3  "Joint1"    TRS, child of Joint0, child Joint2
 *   4  "Joint2"    TRS, child of Joint1
 *   5  "Joint3"    TRS, child of Joint0, child Joint4
 *   6  "Joint4"    TRS, child of Joint3
 *   7  "MeshNode"  TRS (2, 0, -1) / 45 deg about Y / scale 1.5; child of Root,
 *                  instantiates mesh 0 with skin 0; not a joint
 *   8  "Object"    translation (0, 0, 5) only; child of Root, outside the rig
 *
 * Joint0 lists Joint3 before Joint1, so the rig's preorder is not the node
 * order: Root, Helper, Joint0, Joint3, Joint4, Joint1, Joint2. A walk that
 * emitted marked nodes by index would differ from the rig at joint 3.
 *
 * Only Root and Helper carry a glTF "matrix"; every other node carries TRS.
 * Rest values of Joint0 and Joint1 are the published test vector of
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
 * Skin "RigSkin" (skin 0) lists its joints out of hierarchy order, so a
 * palette index and a rig joint index never coincide by accident:
 *
 *   palette entry p   0       1       2       3       4
 *   node              4       6       2       3       5
 *   name              Joint2  Joint4  Joint0  Joint1  Joint3
 *
 * Its inverse bind matrix p is a pure translation (-(p + 1), 0.5 * p, -1) by
 * palette index, which no import step can reproduce by accident.
 *
 * Mesh 0 is one indexed quad, 4 vertices, indices {0, 1, 2, 0, 2, 3}, with
 * POSITION / JOINTS_0 / JOINTS_1 / WEIGHTS_0 / WEIGHTS_1. JOINTS are
 * UNSIGNED_BYTE VEC4 (not normalized) holding palette indices, WEIGHTS are
 * FLOAT VEC4. The weights are raw binary32 in the BIN chunk; their decimal
 * sums are 1 but the binary32 sums are not exact, and the drop gate divides by
 * the source total rather than assuming 1. Later tests assert on these
 * numbers:
 *
 *   v0  five influences, dropped mass 0.01 (below the 0.02 default tolerance)
 *       JOINTS_0 (0, 1, 2, 3)  WEIGHTS_0 (0.40, 0.30, 0.20, 0.09)
 *       JOINTS_1 (4, 0, 0, 0)  WEIGHTS_1 (0.01, 0, 0, 0)
 *   v1  tie: three influences share weight 0.10, two of them fit; dropped
 *       mass 0.10, the first vertex the 0.02 default tolerance rejects
 *       JOINTS_0 (1, 0, 3, 2)  WEIGHTS_0 (0.40, 0.30, 0.10, 0.10)
 *       JOINTS_1 (4, 0, 0, 0)  WEIGHTS_1 (0.10, 0, 0, 0)
 *       top 4 = joints 1, 0 and the two lowest tied indices 2 and 3; joint 4 is
 *       dropped
 *   v2  five influences, dropped mass 0.05, admitted by the tests' 0.15
 *       fixture tolerance
 *       JOINTS_0 (0, 1, 2, 3)  WEIGHTS_0 (0.40, 0.30, 0.15, 0.10)
 *       JOINTS_1 (4, 0, 0, 0)  WEIGHTS_1 (0.05, 0, 0, 0)
 *   v3  two influences; every per-vertex defect knob mutates this vertex
 *       JOINTS_0 (0, 1, 0, 0)  WEIGHTS_0 (0.75, 0.25, 0, 0)
 *       JOINTS_1 (0, 0, 0, 0)  WEIGHTS_1 (0, 0, 0, 0)
 *
 * With second_primitive_far, mesh 0 gains a second non-indexed triangle whose
 * first vertex sits at (10, 0, 0) bound wholly to palette entry 0: through
 * inverse bind 0 that is (9, 0, -1), a distance of sqrt(82), farther than any
 * vertex of the quad (sqrt(30) for v0 through entry 4). second_node_far puts
 * the same triangle into a mesh of its own on a second node with skin 0.
 *
 * With animation, the file gains one animation "Clip" over [0, 1] s whose
 * channels cover every interpolation the clip importer handles; the test
 * values are chosen so a closed form exists at the 24 fps grid times:
 *
 *   Joint1 rotation  LINEAR  keys 0, 0.25, 0.5, 1.0:
 *                    identity, identity, 90 deg about Z authored off unit as
 *                    (0, 0, 0.6, 0.6), 180 deg about Z authored in the other
 *                    hemisphere as (0, 0, -1, 0). The identical first pair is
 *                    a degenerate slerp step inside a channel that does not
 *                    fold; the off-unit key must be normalized; the last key
 *                    makes the shortest-path flip load-bearing.
 *   Joint2 translation CUBICSPLINE keys 0.25, 0.75 (interval 0.5):
 *                    key0 in (100, 100, 100) value (0, 0, 0) out (8, 0, 0)
 *                    key1 in (0, 8, 0) value (1, 2, 4) out (100, 100, 100)
 *                    The unused tangents are loud so a swapped tangent order
 *                    shows; at t = 0.5 the spline is exactly (1, 0.5, 2).
 *   Joint2 rotation  CUBICSPLINE keys 0, 1.0: identity to (0, 0, 1, 0), both
 *                    used tangents (0, 0, 2, 0), the unused ones (7, 7, 7, 7);
 *                    the Hermite result is not unit and must be normalized.
 *   Joint3 scale     STEP keys 0.25, 0.75: (1, 1, 1) then (2, 2, 2); with
 *                    animation_step_past_end a third key (3, 3, 3) at 1.05,
 *                    past the whole-frame end the clip snaps to at 24 fps.
 *   Joint4 translation LINEAR keys 0, 1.0: (0, 0.5, 0) twice -- constant.
 *
 * animation_step_only keeps only the Joint3 and Joint4 channels, so nothing
 * is sampled. The other animation knobs add one defective channel each. */

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
#define RIGGED_GLB_FAR_VERTEX_COUNT 3
#define RIGGED_GLB_ANIM_DURATION 1.0F

/* Node behind palette entry p of skin 0, in the table order above. */
#define RIGGED_GLB_PALETTE_NODES {RIGGED_GLB_NODE_JOINT2, RIGGED_GLB_NODE_JOINT4, RIGGED_GLB_NODE_JOINT0, RIGGED_GLB_NODE_JOINT1, RIGGED_GLB_NODE_JOINT3}

/* One defect per knob; all false writes the valid fixture. */
typedef struct {
    bool matrix_shear;            /* Helper's matrix gets a 1e-3 shear, so it is not TRS */
    bool root_small_scale;        /* Root's matrix becomes a uniform 0.01 scale, the usual cm-to-m wrapper */
    bool root_small_shear;        /* the 0.01-scale Root gets a 1e-3 rad shear (element 1e-5): not TRS at any scale */
    bool unnamed_node;            /* Helper loses its name */
    bool empty_name;              /* Helper is named "" */
    bool duplicate_name;          /* Joint4 is named "Joint3", so two rig nodes share one joint id */
    bool bad_rotation;            /* Joint2's rotation is (0, 0, 0, 2), not a unit quaternion */
    bool cycle;                   /* Helper moves under Joint4, closing a parent cycle */
    bool matrix_and_trs;          /* Helper carries a translation next to its matrix */
    bool duplicate_skin_joint;    /* skin 0 lists Joint1 twice */
    bool deep_chain;              /* a 260-node chain under Object whose last node joins the skin */
    bool joint_outside_root;      /* Object joins the skin, outside a cut at Helper */
    bool multi_root;              /* Object becomes a second scene root and a skin joint */
    bool negative_weight;         /* v3 weight 1 turns negative */
    bool nan_weight;              /* v3 weight 1 turns NaN */
    bool index_ge_palette;        /* v3 joint 1 addresses palette entry 5 */
    bool zero_weight_lane_index;  /* v3 joint 2, whose weight is 0, addresses palette entry 200 */
    bool unpaired_sets;           /* JOINTS_1 without WEIGHTS_1 */
    bool nonconsecutive_sets;     /* the second pair is named JOINTS_2/WEIGHTS_2 */
    bool weights1_short;          /* the WEIGHTS_1 accessor covers one vertex too few */
    bool duplicate_joint;         /* v3 weights joint 0 twice with non-zero weight */
    bool zero_weights;            /* v3 weights sum to zero */
    bool weights_half;            /* v3 weights become 0.5/0.5, a quantization tie */
    bool joints_float_type;       /* JOINTS accessors become FLOAT */
    bool weights_bad_type;        /* WEIGHTS_0 becomes UNSIGNED_BYTE without normalized */
    bool morph_target;            /* the skinned primitive gains a morph target */
    bool no_indices;              /* the primitive drops its index accessor and keeps v0..v2, one triangle */
    bool second_primitive_far;    /* mesh 0 gains the far triangle described above */
    bool second_node_far;         /* the far triangle becomes mesh 1 on a new node "FarNode" with skin 0 */
    bool mesh_other_skin;         /* MeshNode uses a second skin; skin 0 has no mesh */
    bool no_ibm;                  /* skin 0 drops inverseBindMatrices */
    bool ibm_short;               /* the inverseBindMatrices accessor covers one joint too few */
    bool ibm_bad_type;            /* the inverseBindMatrices accessor is VEC4 FLOAT */
    bool ibm_nan;                 /* inverse bind matrix 0 holds one NaN element */
    bool ibm_projective;          /* inverse bind matrix 0 has a bottom row other than (0, 0, 0, 1) */
    bool animation;               /* the animation "Clip" described above; every knob below implies it */
    bool animation_step_only;     /* "Clip" keeps only its STEP and constant channels */
    bool animation_outside_rig;   /* "Clip" also translates MeshNode, which is not a joint */
    bool animation_weights;       /* "Clip" also animates the morph weights of MeshNode (implies morph_target) */
    bool animation_duplicate;     /* "Clip" lists the Joint1 rotation channel twice */
    bool animation_matrix_node;   /* "Clip" also translates Helper, a matrix node */
    bool animation_step_past_end; /* the Joint3 STEP track gains a key at 1.05 s, past the snapped end */
    bool animation_no_channels;   /* "Clip" has samplers but no channels */
    bool animation_bad_times;     /* the Joint3 STEP input runs backwards: 0.75 then 0.25 */
    bool reparent_joint2;         /* Joint2 hangs under Joint3 instead of Joint1 (same name, same rest) */
    bool rest_mismatch;           /* the rest translation y of Joint2 is one ulp above 0.75 */
} rigged_glb_opts_t;

/* opts may be NULL, which means every knob off. */
void rigged_glb_write(const char *path, const rigged_glb_opts_t *opts);

#endif /* NT_TEST_HELPER_RIGGED_GLB_H */
