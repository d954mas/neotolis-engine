/* clang-format off */
#include "nt_builder_internal.h"
#include "hash/nt_hash.h"
#include "cgltf.h"
/* clang-format on */

#include <math.h>

#include "skeletal/nt_skeletal.h"

/*
 * Clip import: one glTF animation becomes one NANM clip on a rig. The source
 * curves are evaluated exactly, in double, per the glTF animation rules; the
 * LINEAR and CUBICSPLINE ones are resampled onto the clip's uniform grid, STEP
 * ones keep their keys. The result is then read back through the runtime's own
 * decoder and sampler and compared against the exact curves over a dense set
 * of times, which yields both the error report and the header bounds.
 *
 * Every content failure is a logged diagnostic followed by NT_BUILD_ASSERT,
 * per the skeletal spec's builder policy.
 */

/* Closer than this to a full quaternion dot the slerp weights lose their
 * conditioning; a normalized lerp differs by O(theta^3), invisible in float. */
#define CLIP_SLERP_MIN_GAP 1e-9
/* glTF quantization leaves a rotation key at most ~8e-3 off unit; a length
 * this small is a zero key, which no normalization can repair. */
#define CLIP_MIN_Q_LEN2 0.5
/* Interior sub-samples per grid interval of the dense pass. */
#define CLIP_SUBSAMPLES 3U

/* Bit-identical floats: the fold and the rest check compare representations,
 * not values, so -0 and 0 or two NaNs stay distinct. */
static bool clip_bits_equal(const float *a, const float *b, uint32_t count) {
    for (uint32_t i = 0; i < count; i++) {
        uint32_t x = 0;
        uint32_t y = 0;
        memcpy(&x, &a[i], sizeof(x));
        memcpy(&y, &b[i], sizeof(y));
        if (x != y) {
            return false;
        }
    }
    return true;
}

// #region source tracks
/* One source channel, mapped to a rig joint component and unpacked to double.
 * values holds key_count * comps doubles, or three times that for CUBICSPLINE
 * in the glTF order in-tangent, value, out-tangent per key. */
typedef struct {
    const cgltf_animation_channel *channel;
    double *times;
    double *values;
    uint32_t key_count;
    uint32_t comps; /* 4 for a rotation, 3 otherwise */
    uint16_t joint;
    uint8_t kind; /* 0 translation, 1 rotation, 2 scale */
    cgltf_interpolation_type interpolation;
} clip_track_t;

static const char *clip_path_name(cgltf_animation_path_type path) {
    switch (path) {
    case cgltf_animation_path_type_translation:
        return "translation";
    case cgltf_animation_path_type_rotation:
        return "rotation";
    case cgltf_animation_path_type_scale:
        return "scale";
    default:
        return "weights";
    }
}

/* Rig joint whose id is the hash of this node's name, or UINT32_MAX. J is
 * small, so a linear scan beats building a lookup table per clip. */
static uint32_t clip_find_joint(const nt_skeletal_skeleton_t *skel, const char *name) {
    const uint32_t id = nt_hash32_str(name).value;
    for (uint32_t j = 0; j < skel->joint_count; j++) {
        if (skel->joint_id[j] == id) {
            return j;
        }
    }
    return UINT32_MAX;
}

/* Validates the channel's target and accessors and fills the track; the
 * animation name labels diagnostics. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void clip_map_channel(const char *label, const nt_builder_rig_t *rig, const cgltf_animation_channel *ch, uint32_t index, clip_track_t *track) {
    const cgltf_node *node = ch->target_node;
    if (node == NULL) {
        NT_LOG_ERROR("%s: channel[%u] has no target node", label, index);
        NT_BUILD_ASSERT(0 && "animation channel has no target node");
    }
    if (node->name == NULL || node->name[0] == '\0') {
        NT_LOG_ERROR("%s: channel[%u] targets an unnamed node, and a joint id is the hash of its name", label, index);
        NT_BUILD_ASSERT(0 && "animation channel targets an unnamed node");
    }
    if (ch->target_path == cgltf_animation_path_type_weights) {
        NT_LOG_ERROR("%s: channel[%u] animates the morph weights of %s; morph targets are not supported", label, index, node->name);
        NT_BUILD_ASSERT(0 && "animation channel animates morph weights");
    }
    if (ch->target_path != cgltf_animation_path_type_translation && ch->target_path != cgltf_animation_path_type_rotation && ch->target_path != cgltf_animation_path_type_scale) {
        NT_LOG_ERROR("%s: channel[%u] on %s has an unknown target path %d", label, index, node->name, (int)ch->target_path);
        NT_BUILD_ASSERT(0 && "animation channel has an unknown target path");
    }
    /* glTF forbids a matrix on an animated node: the channel would replace
     * one of three properties the node does not have. */
    if (node->has_matrix) {
        NT_LOG_ERROR("%s: channel[%u] animates %s, which carries a matrix instead of translation/rotation/scale", label, index, node->name);
        NT_BUILD_ASSERT(0 && "animated node carries a matrix");
    }
    const uint32_t joint = clip_find_joint(&rig->skeleton, node->name);
    if (joint == UINT32_MAX) {
        NT_LOG_ERROR("%s: channel[%u] animates node %s, which is not a joint of the rig", label, index, node->name);
        NT_BUILD_ASSERT(0 && "animation channel targets a node outside the rig");
    }
    /* A clip is valid for one rest pose: the same names over a different rest
     * are a different rig, whatever glb they come from. */
    const nt_skeletal_trs_t *rest = &rig->skeleton.rest[joint];
    if (!clip_bits_equal(rest->t, node->translation, 3) || !clip_bits_equal(rest->q, node->rotation, 4) || !clip_bits_equal(rest->s, node->scale, 3)) {
        NT_LOG_ERROR("%s: node %s has a different rest pose than the rig's joint %u (t %g %g %g q %g %g %g %g s %g %g %g vs t %g %g %g q %g %g %g %g s %g %g %g)", label, node->name, joint,
                     (double)node->translation[0], (double)node->translation[1], (double)node->translation[2], (double)node->rotation[0], (double)node->rotation[1], (double)node->rotation[2],
                     (double)node->rotation[3], (double)node->scale[0], (double)node->scale[1], (double)node->scale[2], (double)rest->t[0], (double)rest->t[1], (double)rest->t[2], (double)rest->q[0],
                     (double)rest->q[1], (double)rest->q[2], (double)rest->q[3], (double)rest->s[0], (double)rest->s[1], (double)rest->s[2]);
        NT_BUILD_ASSERT(0 && "animated node's rest pose differs from the rig's");
    }

    const cgltf_accessor *in = ch->sampler->input;
    const cgltf_accessor *out = ch->sampler->output;
    const bool rotation = ch->target_path == cgltf_animation_path_type_rotation;
    /* cgltf_validate holds only the count relation between the two accessors;
     * a wrong type would unpack to fewer floats than the evaluator reads. */
    if (in->type != cgltf_type_scalar || in->component_type != cgltf_component_type_r_32f || in->normalized || in->count < 1) {
        NT_LOG_ERROR("%s: channel[%u] on %s.%s: the input accessor must be SCALAR FLOAT with at least one key", label, index, node->name, clip_path_name(ch->target_path));
        NT_BUILD_ASSERT(0 && "animation input accessor has an invalid type");
    }
    const bool out_float = out->component_type == cgltf_component_type_r_32f;
    const bool out_norm_int = out->normalized != 0 && (out->component_type == cgltf_component_type_r_8 || out->component_type == cgltf_component_type_r_8u ||
                                                       out->component_type == cgltf_component_type_r_16 || out->component_type == cgltf_component_type_r_16u);
    if (rotation ? (out->type != cgltf_type_vec4 || (!out_float && !out_norm_int)) : (out->type != cgltf_type_vec3 || !out_float)) {
        NT_LOG_ERROR("%s: channel[%u] on %s.%s: the output accessor must be %s", label, index, node->name, clip_path_name(ch->target_path),
                     rotation ? "VEC4 FLOAT or normalized BYTE/SHORT" : "VEC3 FLOAT");
        NT_BUILD_ASSERT(0 && "animation output accessor has an invalid type");
    }

    track->channel = ch;
    track->key_count = (uint32_t)in->count;
    track->comps = rotation ? 4U : 3U;
    track->joint = (uint16_t)joint;
    track->kind = 0U;
    if (rotation) {
        track->kind = 1U;
    } else if (ch->target_path == cgltf_animation_path_type_scale) {
        track->kind = 2U;
    }
    track->interpolation = ch->sampler->interpolation;
}

/* Unpacks both accessors into the track's double arrays and checks what the
 * evaluator relies on: increasing finite times, finite values, rotation keys
 * long enough to normalize. Rotation keys are normalized here, so a quantized
 * source and a float one meet the encoder's unit rule the same way. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void clip_unpack_track(const char *label, clip_track_t *track, float *scratch) {
    const cgltf_animation_channel *ch = track->channel;
    const char *name = ch->target_node->name;
    const char *path = clip_path_name(ch->target_path);
    const uint32_t per_key = (track->interpolation == cgltf_interpolation_type_cubic_spline) ? 3U : 1U;
    const cgltf_size want_in = track->key_count;
    const cgltf_size want_out = (cgltf_size)track->key_count * per_key * track->comps;

    if (cgltf_accessor_unpack_floats(ch->sampler->input, scratch, want_in) != want_in) {
        NT_LOG_ERROR("%s: %s.%s input accessor could not be unpacked", label, name, path);
        NT_BUILD_ASSERT(0 && "animation input accessor could not be unpacked");
    }
    for (uint32_t k = 0; k < track->key_count; k++) {
        track->times[k] = (double)scratch[k];
        if (!nt_builder_finite(scratch[k]) || scratch[k] < 0.0F || (k > 0 && !(track->times[k] > track->times[k - 1U]))) {
            NT_LOG_ERROR("%s: %s.%s key %u time %g is negative, not finite or not after the previous key", label, name, path, k, (double)scratch[k]);
            NT_BUILD_ASSERT(0 && "animation input times must be finite, non-negative and strictly increasing");
        }
    }

    if (cgltf_accessor_unpack_floats(ch->sampler->output, scratch, want_out) != want_out) {
        NT_LOG_ERROR("%s: %s.%s output accessor could not be unpacked", label, name, path);
        NT_BUILD_ASSERT(0 && "animation output accessor could not be unpacked");
    }
    for (cgltf_size i = 0; i < want_out; i++) {
        if (!nt_builder_finite(scratch[i])) {
            NT_LOG_ERROR("%s: %s.%s output element %u is not finite", label, name, path, (uint32_t)i);
            NT_BUILD_ASSERT(0 && "animation output values must be finite");
        }
        track->values[i] = (double)scratch[i];
    }
    if (track->kind != 1U) {
        return;
    }
    for (uint32_t k = 0; k < track->key_count; k++) {
        /* CUBICSPLINE tangents are directions, not rotations; only the value
         * of each key is a quaternion. */
        double *q = track->values + ((((size_t)k * per_key) + (per_key / 2U)) * 4U);
        const double len2 = (q[0] * q[0]) + (q[1] * q[1]) + (q[2] * q[2]) + (q[3] * q[3]);
        if (len2 < CLIP_MIN_Q_LEN2) {
            NT_LOG_ERROR("%s: %s.rotation key %u (%g, %g, %g, %g) is too short to be a rotation", label, name, k, q[0], q[1], q[2], q[3]);
            NT_BUILD_ASSERT(0 && "animation rotation key is not a unit quaternion");
        }
        const double inv = 1.0 / sqrt(len2);
        for (int c = 0; c < 4; c++) {
            q[c] *= inv;
        }
    }
}
// #endregion

// #region source evaluation
static void clip_normalize4(double *q) {
    const double inv = 1.0 / sqrt((q[0] * q[0]) + (q[1] * q[1]) + (q[2] * q[2]) + (q[3] * q[3]));
    for (int c = 0; c < 4; c++) {
        q[c] *= inv;
    }
}

/* Shortest-path slerp per the glTF animation rules; b is flipped into a's
 * hemisphere first so the near-parallel fallback never lerps q against -q. */
static void clip_slerp(const double *a, const double *b_in, double u, double *out) {
    double b[4] = {b_in[0], b_in[1], b_in[2], b_in[3]};
    double dot = (a[0] * b[0]) + (a[1] * b[1]) + (a[2] * b[2]) + (a[3] * b[3]);
    if (dot < 0.0) {
        for (int c = 0; c < 4; c++) {
            b[c] = -b[c];
        }
        dot = -dot;
    }
    if (dot > 1.0) {
        dot = 1.0;
    }
    double wa = 1.0 - u;
    double wb = u;
    if (1.0 - dot >= CLIP_SLERP_MIN_GAP) {
        const double theta = acos(dot);
        const double inv_sin = 1.0 / sin(theta);
        wa = sin((1.0 - u) * theta) * inv_sin;
        wb = sin(u * theta) * inv_sin;
    }
    for (int c = 0; c < 4; c++) {
        out[c] = (wa * a[c]) + (wb * b[c]);
    }
    clip_normalize4(out);
}

/* Exact value of the source curve at time, per the glTF animation sampler
 * rules: held before the first key and after the last; LINEAR lerps
 * translation and scale and slerps rotation; STEP holds; CUBICSPLINE is the
 * cubic Hermite spline with the stored tangents scaled by the key interval. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void clip_eval(const clip_track_t *track, double time, double *out) {
    const uint32_t n = track->key_count;
    const uint32_t comps = track->comps;
    const bool cubic = track->interpolation == cgltf_interpolation_type_cubic_spline;
    const size_t stride = cubic ? (3U * (size_t)comps) : comps;
    const size_t value_off = cubic ? comps : 0U;

    /* Last key at or before time, by binary search; lo ends as their count. */
    uint32_t lo = 0;
    uint32_t hi = n;
    while (lo < hi) {
        const uint32_t mid = lo + ((hi - lo) / 2U);
        if (track->times[mid] <= time) {
            lo = mid + 1U;
        } else {
            hi = mid;
        }
    }
    if (lo == 0U || lo >= n || track->interpolation == cgltf_interpolation_type_step) {
        const uint32_t k = (lo == 0U) ? 0U : (lo - 1U);
        memcpy(out, track->values + ((size_t)k * stride) + value_off, comps * sizeof(double));
        return;
    }
    const uint32_t k = lo - 1U;
    const double t0 = track->times[k];
    const double dt = track->times[k + 1U] - t0;
    const double u = (time - t0) / dt;
    const double *key0 = track->values + ((size_t)k * stride);
    const double *key1 = key0 + stride;
    if (!cubic) {
        if (track->kind == 1U) {
            clip_slerp(key0, key1, u, out);
        } else {
            for (uint32_t c = 0; c < comps; c++) {
                out[c] = ((1.0 - u) * key0[c]) + (u * key1[c]);
            }
        }
        return;
    }
    const double u2 = u * u;
    const double u3 = u2 * u;
    const double h00 = (2.0 * u3) - (3.0 * u2) + 1.0;
    const double h10 = u3 - (2.0 * u2) + u;
    const double h01 = (-2.0 * u3) + (3.0 * u2);
    const double h11 = u3 - u2;
    const double *v0 = key0 + comps;
    const double *m0 = key0 + ((size_t)2U * comps); /* out-tangent of key k */
    const double *v1 = key1 + comps;
    const double *m1 = key1; /* in-tangent of key k + 1 */
    for (uint32_t c = 0; c < comps; c++) {
        out[c] = (h00 * v0[c]) + (h10 * dt * m0[c]) + (h01 * v1[c]) + (h11 * dt * m1[c]);
    }
    if (track->kind == 1U) {
        clip_normalize4(out);
    }
}
// #endregion

// #region resampling
/* Grid time i of N samples over duration, computed the one way both the builder
 * and a test reproduce it; i * duration is exact in double for a float
 * duration, so the last grid time is duration itself. */
static double clip_grid_time(uint32_t i, uint32_t n, float duration) { return ((double)i * (double)duration) / (double)(n - 1U); }

/* Fills one clip channel from its track: samples on the grid for LINEAR and
 * CUBICSPLINE, the authored keys for STEP, one value when nothing changes.
 * scratch holds n_grid * 4 floats, or key_count * 5 for STEP. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void clip_fill_channel(const clip_track_t *track, uint32_t n_grid, float duration, float *scratch, nt_builder_anim_channel_t *ch) {
    const uint32_t comps = track->comps;
    memset(ch, 0, sizeof(*ch));
    if (track->interpolation == cgltf_interpolation_type_step) {
        float *times = scratch;
        float *values = scratch + track->key_count;
        for (uint32_t k = 0; k < track->key_count; k++) {
            times[k] = (float)track->times[k];
            for (uint32_t c = 0; c < 4U; c++) {
                values[((size_t)k * 4U) + c] = (c < comps) ? (float)track->values[((size_t)k * comps) + c] : 0.0F;
            }
        }
        bool constant = true;
        for (uint32_t k = 1; k < track->key_count && constant; k++) {
            constant = clip_bits_equal(values, values + ((size_t)k * 4U), 4U);
        }
        if (constant) {
            ch->mode = NT_SKELETAL_CHANNEL_CONSTANT;
            memcpy(ch->constant, values, sizeof(ch->constant));
        } else {
            ch->mode = NT_SKELETAL_CHANNEL_STEP;
            ch->step_times = times;
            ch->step_values = values;
            ch->step_count = track->key_count;
        }
        return;
    }
    double v[4] = {0.0, 0.0, 0.0, 0.0};
    for (uint32_t i = 0; i < n_grid; i++) {
        clip_eval(track, clip_grid_time(i, n_grid, duration), v);
        for (uint32_t c = 0; c < comps; c++) {
            scratch[((size_t)i * comps) + c] = (float)v[c];
        }
    }
    bool constant = true;
    for (uint32_t i = 1; i < n_grid && constant; i++) {
        constant = clip_bits_equal(scratch, scratch + ((size_t)i * comps), comps);
    }
    if (constant) {
        ch->mode = NT_SKELETAL_CHANNEL_CONSTANT;
        memcpy(ch->constant, scratch, comps * sizeof(float));
    } else {
        ch->mode = NT_SKELETAL_CHANNEL_SAMPLED;
        ch->samples = scratch;
    }
}
// #endregion

// #region dense pass
typedef struct {
    const nt_skeletal_skeleton_t *skel;
    const nt_skeletal_clip_t *view;
    const clip_track_t *tracks;
    uint32_t track_count;
    nt_skeletal_trs_t *local_rt;
    nt_skeletal_trs_t *local_ex;
    nt_skeletal_mat34_t *g_rt;
    nt_skeletal_mat34_t *g_ex;
    double *stretch; /* per joint: product of max|s| along the ancestor chain */
    double r_joints, r_root, s_max;
    nt_builder_clip_report_t *report;
} clip_pass_t;

static int clip_cmp_double(const void *a, const void *b) {
    const double x = *(const double *)a;
    const double y = *(const double *)b;
    return (x > y) - (x < y);
}

/* One time of the dense set: the runtime pose against the exact one, joint by
 * joint, and the three bounds off the runtime pose. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void clip_pass_time(clip_pass_t *p, double time) {
    const uint16_t joints = p->skel->joint_count;
    nt_skeletal_sample(p->view, time, p->skel->rest, p->local_rt);
    memcpy(p->local_ex, p->skel->rest, (size_t)joints * sizeof(nt_skeletal_trs_t));
    for (uint32_t t = 0; t < p->track_count; t++) {
        const clip_track_t *track = &p->tracks[t];
        double v[4] = {0.0, 0.0, 0.0, 0.0};
        clip_eval(track, time, v);
        nt_skeletal_trs_t *dst = &p->local_ex[track->joint];
        float *lane = dst->t;
        if (track->kind == 1U) {
            lane = dst->q;
        } else if (track->kind == 2U) {
            lane = dst->s;
        }
        for (uint32_t c = 0; c < track->comps; c++) {
            lane[c] = (float)v[c];
        }
    }
    nt_skeletal_fk(p->skel, p->local_rt, p->g_rt, 0, joints);
    nt_skeletal_fk(p->skel, p->local_ex, p->g_ex, 0, joints);

    for (uint16_t j = 0; j < joints; j++) {
        double lin2 = 0.0;
        double dt2 = 0.0;
        double origin2 = 0.0;
        for (int r = 0; r < 3; r++) {
            for (int c = 0; c < 3; c++) {
                const double d = (double)p->g_rt[j].r[r][c] - (double)p->g_ex[j].r[r][c];
                lin2 += d * d;
            }
            const double d = (double)p->g_rt[j].r[r][3] - (double)p->g_ex[j].r[r][3];
            dt2 += d * d;
            origin2 += (double)p->g_rt[j].r[r][3] * (double)p->g_rt[j].r[r][3];
        }
        const double lin = sqrt(lin2);
        const double dt = sqrt(dt2);
        if (lin > (double)p->report->cpu_error_lin) {
            p->report->cpu_error_lin = (float)lin;
            p->report->worst_time_lin = time;
            p->report->worst_joint_lin = j;
        }
        if (dt > (double)p->report->cpu_error_t) {
            p->report->cpu_error_t = (float)dt;
            p->report->worst_time_t = time;
            p->report->worst_joint_t = j;
        }
        const double origin = sqrt(origin2);
        if (origin > p->r_joints) {
            p->r_joints = origin;
        }
        const nt_skeletal_trs_t *l = &p->local_rt[j];
        const uint16_t parent = p->skel->parent[j];
        if (parent == NT_SKELETAL_NO_PARENT) {
            const double root = sqrt(((double)l->t[0] * (double)l->t[0]) + ((double)l->t[1] * (double)l->t[1]) + ((double)l->t[2] * (double)l->t[2]));
            if (root > p->r_root) {
                p->r_root = root;
            }
        }
        /* Products of max|s| along the chain bound the largest singular value
         * of every model matrix's linear part (skeletal spec, Bounds and culling). */
        const double s = fmax(fabs((double)l->s[0]), fmax(fabs((double)l->s[1]), fabs((double)l->s[2])));
        p->stretch[j] = ((parent == NT_SKELETAL_NO_PARENT) ? 1.0 : p->stretch[parent]) * s;
        if (p->stretch[j] > p->s_max) {
            p->s_max = p->stretch[j];
        }
    }
}

/* The dense set: every grid time, every authored key time and three interior
 * sub-samples per grid interval, sorted and deduplicated. The grid is the
 * n_grid one even when no channel is sampled, so a STEP-only clip is still
 * measured between its keys. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void clip_dense_pass(clip_pass_t *p, uint32_t n_grid, float duration) {
    size_t count = (size_t)n_grid + ((size_t)(n_grid - 1U) * CLIP_SUBSAMPLES);
    for (uint32_t t = 0; t < p->track_count; t++) {
        count += p->tracks[t].key_count;
    }
    double *times = (double *)malloc(count * sizeof(double));
    NT_BUILD_ASSERT(times && "add_scene_clip: alloc failed (OOM)");
    size_t n = 0;
    for (uint32_t i = 0; i < n_grid; i++) {
        times[n++] = clip_grid_time(i, n_grid, duration);
        if (i + 1U < n_grid) {
            for (uint32_t k = 1; k <= CLIP_SUBSAMPLES; k++) {
                times[n++] = ((double)((4U * i) + k) * (double)duration) / (double)(4U * (n_grid - 1U));
            }
        }
    }
    for (uint32_t t = 0; t < p->track_count; t++) {
        for (uint32_t k = 0; k < p->tracks[t].key_count; k++) {
            times[n++] = p->tracks[t].times[k];
        }
    }
    qsort(times, n, sizeof(double), clip_cmp_double);
    for (size_t i = 0; i < n; i++) {
        if (i > 0 && times[i] == times[i - 1U]) {
            continue;
        }
        double time = times[i];
        if (time > (double)duration) {
            time = (double)duration;
        }
        clip_pass_time(p, time);
    }
    free(times);
}
// #endregion

// #region export
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void nt_builder_add_scene_clip(NtBuilderContext *ctx, const nt_glb_scene_t *scene, uint32_t animation_index, const nt_builder_rig_t *rig, float sample_fps, const char *resource_id,
                               nt_builder_clip_report_t *report) {
    NT_BUILD_ASSERT(ctx && scene && rig && resource_id && report && "invalid add_scene_clip args");
    const cgltf_data *data = (const cgltf_data *)scene->_internal;
    NT_BUILD_ASSERT(data && "add_scene_clip: scene is not parsed");
    if (animation_index >= scene->animation_count) {
        NT_LOG_ERROR("add_scene_clip: animation index %u, the scene has %u animations", animation_index, scene->animation_count);
        NT_BUILD_ASSERT(0 && "animation index out of range");
    }
    if (!nt_builder_finite(sample_fps) || !(sample_fps > 0.0F)) {
        NT_LOG_ERROR("add_scene_clip: sample_fps %g must be finite and positive", (double)sample_fps);
        NT_BUILD_ASSERT(0 && "sample_fps must be finite and positive");
    }
    const cgltf_animation *anim = &data->animations[animation_index];
    char label[128];
    (void)snprintf(label, sizeof(label), "add_scene_clip: animation[%u]%s%s", animation_index, anim->name ? " " : "", anim->name ? anim->name : "");
    if (anim->channels_count == 0) {
        NT_LOG_ERROR("%s has no channels", label);
        NT_BUILD_ASSERT(0 && "animation has no channels");
    }
    const nt_skeletal_skeleton_t *skel = &rig->skeleton;
    const uint32_t joints = skel->joint_count;
    const uint32_t channel_count = 3U * (joints + 1U);
    const uint32_t track_count = (uint32_t)anim->channels_count;

    // #region map and unpack
    clip_track_t *tracks = (clip_track_t *)calloc(track_count, sizeof(clip_track_t));
    uint32_t *track_of = (uint32_t *)malloc((size_t)channel_count * sizeof(uint32_t));
    NT_BUILD_ASSERT(tracks && track_of && "add_scene_clip: alloc failed (OOM)");
    memset(track_of, 0xFF, (size_t)channel_count * sizeof(uint32_t));
    size_t doubles = 0;
    size_t max_floats = 1; /* every track unpacks at least one key through scratch */
    for (uint32_t t = 0; t < track_count; t++) {
        clip_map_channel(label, rig, &anim->channels[t], t, &tracks[t]);
        const uint32_t c = (3U * tracks[t].joint) + tracks[t].kind;
        /* glTF forbids two channels of one animation on one node property;
         * cgltf does not check it, and the second would silently win. */
        if (track_of[c] != UINT32_MAX) {
            NT_LOG_ERROR("%s: channels[%u] and [%u] both animate %s.%s", label, track_of[c], t, anim->channels[t].target_node->name, clip_path_name(anim->channels[t].target_path));
            NT_BUILD_ASSERT(0 && "two channels animate one node property");
        }
        track_of[c] = t;
        const uint32_t per_key = (tracks[t].interpolation == cgltf_interpolation_type_cubic_spline) ? 3U : 1U;
        const size_t values = (size_t)tracks[t].key_count * per_key * tracks[t].comps;
        doubles += tracks[t].key_count + values;
        if (values > max_floats) {
            max_floats = values;
        }
    }
    double *arena = (double *)malloc(doubles * sizeof(double));
    float *scratch = (float *)malloc(max_floats * sizeof(float));
    NT_BUILD_ASSERT(arena && scratch && "add_scene_clip: alloc failed (OOM)");
    double *next = arena;
    float duration = 0.0F;
    for (uint32_t t = 0; t < track_count; t++) {
        const uint32_t per_key = (tracks[t].interpolation == cgltf_interpolation_type_cubic_spline) ? 3U : 1U;
        tracks[t].times = next;
        next += tracks[t].key_count;
        tracks[t].values = next;
        next += (size_t)tracks[t].key_count * per_key * tracks[t].comps;
        clip_unpack_track(label, &tracks[t], scratch);
        const float last = (float)tracks[t].times[tracks[t].key_count - 1U];
        if (last > duration) {
            duration = last;
        }
    }
    free(scratch);
    // #endregion

    // #region resample
    const double grid = (double)llround((double)duration * (double)sample_fps) + 1.0;
    NT_BUILD_ASSERT(grid < 4294967295.0 && "add_scene_clip: duration * sample_fps overflows the sample grid");
    const uint32_t n_grid = (grid < 2.0) ? 2U : (uint32_t)grid;
    size_t out_floats = 0;
    for (uint32_t t = 0; t < track_count; t++) {
        out_floats += (tracks[t].interpolation == cgltf_interpolation_type_step) ? ((size_t)tracks[t].key_count * 5U) : ((size_t)n_grid * 4U);
    }
    float *out_arena = (float *)calloc(out_floats, sizeof(float));
    nt_builder_anim_channel_t *channels = (nt_builder_anim_channel_t *)calloc(channel_count, sizeof(nt_builder_anim_channel_t));
    NT_BUILD_ASSERT(out_arena && channels && "add_scene_clip: alloc failed (OOM)");
    float *out_next = out_arena;
    bool sampled = false;
    for (uint32_t t = 0; t < track_count; t++) {
        const uint32_t c = (3U * tracks[t].joint) + tracks[t].kind;
        clip_fill_channel(&tracks[t], n_grid, duration, out_next, &channels[c]);
        out_next += (tracks[t].interpolation == cgltf_interpolation_type_step) ? ((size_t)tracks[t].key_count * 5U) : ((size_t)n_grid * 4U);
        sampled = sampled || channels[c].mode == NT_SKELETAL_CHANNEL_SAMPLED;
    }
    nt_builder_clip_t clip = {
        .rig_compat_id = skel->rig_compat_id,
        .additive_ref_id = (nt_hash64_t){.value = 0},
        .joint_count = (uint16_t)joints,
        .sample_count = sampled ? n_grid : 1U,
        .duration = duration,
        .channels = channels,
    };
    // #endregion

    // #region measure
    /* The encoded bytes are read back through the runtime's own decoder, so
     * the report measures what the game will sample, not a builder mirror. */
    uint8_t *payload = NULL;
    uint32_t payload_size = 0;
    nt_builder_encode_clip(&clip, &payload, &payload_size);
    nt_skeletal_clip_t view;
    nt_skeletal_clip_view(payload, &view);

    memset(report, 0, sizeof(*report));
    report->sample_count = clip.sample_count;
    clip_pass_t pass = {
        .skel = skel,
        .view = &view,
        .tracks = tracks,
        .track_count = track_count,
        .local_rt = (nt_skeletal_trs_t *)malloc((size_t)joints * sizeof(nt_skeletal_trs_t)),
        .local_ex = (nt_skeletal_trs_t *)malloc((size_t)joints * sizeof(nt_skeletal_trs_t)),
        .g_rt = (nt_skeletal_mat34_t *)malloc((size_t)joints * sizeof(nt_skeletal_mat34_t)),
        .g_ex = (nt_skeletal_mat34_t *)malloc((size_t)joints * sizeof(nt_skeletal_mat34_t)),
        .stretch = (double *)malloc((size_t)joints * sizeof(double)),
        .report = report,
    };
    NT_BUILD_ASSERT(pass.local_rt && pass.local_ex && pass.g_rt && pass.g_ex && pass.stretch && "add_scene_clip: alloc failed (OOM)");
    clip_dense_pass(&pass, n_grid, duration);
    free(pass.stretch);
    free(pass.g_ex);
    free(pass.g_rt);
    free(pass.local_ex);
    free(pass.local_rt);
    free(payload);

    clip.r_joints = nt_builder_round_up(pass.r_joints);
    clip.r_root = nt_builder_round_up(pass.r_root);
    clip.s_max = nt_builder_round_up(pass.s_max);
    nt_builder_add_clip(ctx, &clip, resource_id);
    // #endregion

    free(channels);
    free(out_arena);
    free(arena);
    free(track_of);
    free(tracks);
}
// #endregion
