#include "skeletal/nt_skeletal.h"

#include <string.h>

#include "core/nt_builtins.h"
#include "nt_skeletal_format.h"

void nt_skeletal_mat34_from_mat4(const float m[16], nt_skeletal_mat34_t *out) {
    NT_ASSERT(m != NULL);
    NT_ASSERT(out != NULL);

    for (int r = 0; r < 3; ++r) {
        out->r[r][0] = m[r];
        out->r[r][1] = m[4 + r];
        out->r[r][2] = m[8 + r];
        out->r[r][3] = m[12 + r];
    }
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void nt_skeletal_fk(const nt_skeletal_skeleton_t *skel, const nt_skeletal_trs_t *restrict local, nt_skeletal_mat34_t *restrict model, uint16_t first, uint16_t count) {
    NT_ASSERT(skel != NULL);
    NT_ASSERT(skel->parent != NULL);
    NT_ASSERT(skel->subtree_end != NULL);
    NT_ASSERT(local != NULL);
    NT_ASSERT(model != NULL);
    NT_ASSERT(count >= 1U);
    NT_ASSERT((uint32_t)first + (uint32_t)count <= (uint32_t)skel->joint_count);
    NT_ASSERT(skel->parent[first] == NT_SKELETAL_NO_PARENT || (uint32_t)first + (uint32_t)count <= (uint32_t)skel->subtree_end[first]);
    /* A model buffer overlapping the locals would feed later joints matrices
     * built from their own output. */
    NT_ASSERT((uintptr_t)(local + skel->joint_count) <= (uintptr_t)model || (uintptr_t)(model + skel->joint_count) <= (uintptr_t)local);

    const uint16_t end = (uint16_t)(first + count);
    for (uint16_t j = first; j < end; ++j) {
        const uint16_t p = skel->parent[j];
        NT_ASSERT(p == NT_SKELETAL_NO_PARENT || p < j);
        nt_skeletal_mat34_t l;
        nt_skeletal_mat34_from_trs(&local[j], &l);
        if (p == NT_SKELETAL_NO_PARENT) {
            model[j] = l;
        } else {
            /* parent[j] < j in preorder, so model[j] never aliases model[p]. */
            nt_skeletal_mat34_mul(&model[p], &l, &model[j]);
        }
    }
}

// #region skin
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void nt_skin_palette_build(const nt_skin_binding_t *binding, const nt_skeletal_mat34_t *restrict model, uint16_t model_count, nt_skeletal_mat34_t *restrict out, uint16_t capacity) {
    NT_ASSERT(binding != NULL);
    NT_ASSERT(binding->remap != NULL);
    NT_ASSERT(binding->inverse_bind != NULL);
    NT_ASSERT(model != NULL);
    NT_ASSERT(out != NULL);
    NT_ASSERT(binding->palette_count <= capacity);
    NT_ASSERT((uintptr_t)(out + binding->palette_count) <= (uintptr_t)model || (uintptr_t)(model + model_count) <= (uintptr_t)out);

    for (uint16_t p = 0; p < binding->palette_count; ++p) {
        NT_ASSERT(binding->remap[p] < model_count);
        nt_skeletal_mat34_mul(&model[binding->remap[p]], &binding->inverse_bind[p], &out[p]);
    }
}
// #endregion

// #region clip sampling
/* Grid interval holding time: index i and interpolant u in [0, 1]. A time that
 * lands on the grid yields u == 0, or u == 1 at the very end, which the caller
 * turns into an exact copy of a stored sample. */
static uint32_t nt_skeletal_grid_index(double time, double inv_step, uint32_t sample_count, float *out_u) {
    NT_ASSERT(sample_count >= 2U);
    const uint32_t last = sample_count - 1U;

    double f = time * inv_step;
    /* inv_step is a rounded quotient, so duration * inv_step can land a ulp
     * past the last sample; without the clamp u would extrapolate. */
    if (f > (double)last) {
        f = (double)last;
    }

    uint32_t i = (uint32_t)f; /* time >= 0, so truncation is the floor */
    if (i >= last) {
        i = last - 1U;
    }
    float u = (float)(f - (double)i);
    /* time * inv_step lands a ulp off an integer for most grid times of a
     * non-binary duration; the snap keeps those exact copies instead of lerps.
     * 2^-20 of one grid interval is far below any authored key spacing. */
    if (u < 0x1p-20F) {
        u = 0.0F;
    } else if (u > 1.0F - 0x1p-20F) {
        u = 1.0F;
    }
    *out_u = u;
    return i;
}

/* q and -q are the same rotation: the sign that makes the largest component
 * positive, ties broken by the first maximum in x,y,z,w order. */
static float nt_skeletal_canonical_sign(const float q[4]) {
    int best = 0;
    float best_abs = (q[0] < 0.0F) ? -q[0] : q[0];
    for (int c = 1; c < 4; ++c) {
        const float a = (q[c] < 0.0F) ? -q[c] : q[c];
        if (a > best_abs) {
            best_abs = a;
            best = c;
        }
    }
    return (q[best] < 0.0F) ? -1.0F : 1.0F;
}

static void nt_skeletal_lerp3(const float *a, const float *b, float u, float *out) {
    for (int c = 0; c < 3; ++c) {
        out[c] = (a[c] * (1.0F - u)) + (b[c] * u);
    }
}

/* Shortest-path normalized lerp: q and -q are the same rotation, so a pair
 * pointing into opposite hemispheres takes the near way round. */
static void nt_skeletal_nlerp(const float *a, const float *b, float u, float *out) {
    const float d = (a[0] * b[0]) + (a[1] * b[1]) + (a[2] * b[2]) + (a[3] * b[3]);
    const float sign = (d < 0.0F) ? -1.0F : 1.0F;

    float q[4];
    float len2 = 0.0F;
    for (int c = 0; c < 4; ++c) {
        q[c] = (a[c] * (1.0F - u)) + (b[c] * sign * u);
        len2 += q[c] * q[c];
    }

    const float inv = 1.0F / sqrtf(len2);
    for (int c = 0; c < 4; ++c) {
        out[c] = q[c] * inv;
    }
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void nt_skeletal_sample(const nt_skeletal_clip_t *clip, double time, nt_skeletal_trs_t *out) {
    NT_ASSERT(clip != NULL);
    NT_ASSERT(clip->base != NULL);
    NT_ASSERT(out != NULL);
    NT_ASSERT(clip->joint_count >= 1U);
    NT_ASSERT(clip->sample_count >= 1U);
    NT_ASSERT(time >= 0.0 && time <= clip->duration);
    /* memcpy requires disjoint buffers. */
    NT_ASSERT((uintptr_t)(clip->base + clip->joint_count) <= (uintptr_t)out || (uintptr_t)(out + clip->joint_count) <= (uintptr_t)clip->base);

    memcpy(out, clip->base, (size_t)clip->joint_count * sizeof(nt_skeletal_trs_t));

    const size_t stride = ((size_t)3U * clip->n_t) + ((size_t)4U * clip->n_q) + ((size_t)3U * clip->n_s);
    if (stride == 0U) {
        return;
    }
    NT_ASSERT(clip->blocks != NULL);
    NT_ASSERT(clip->t_joint != NULL || clip->n_t == 0U);
    NT_ASSERT(clip->q_joint != NULL || clip->n_q == 0U);
    NT_ASSERT(clip->s_joint != NULL || clip->n_s == 0U);
    /* Interpolation reads two adjacent grid entries. */
    NT_ASSERT(clip->sample_count >= 2U && clip->duration > 0.0);

    /* The step stays in double: a float quotient drifts grid times off their samples. */
    const double inv_step = (double)(clip->sample_count - 1U) / clip->duration;
    float u = 0.0F;
    const uint32_t i = nt_skeletal_grid_index(time, inv_step, clip->sample_count, &u);
    const float *a = clip->blocks + ((size_t)i * stride);
    const float *b = a + stride;
    const size_t q_off = (size_t)3U * clip->n_t;
    const size_t s_off = q_off + ((size_t)4U * clip->n_q);

    // #region exact grid sample
    /* A grid time must reproduce its stored block bit for bit, which neither
     * the lerp's rounding nor the nlerp's normalization would guarantee. */
    if (u == 0.0F || u == 1.0F) {
        const float *blk = (u == 0.0F) ? a : b;
        for (uint16_t k = 0; k < clip->n_t; ++k) {
            NT_ASSERT(clip->t_joint[k] < clip->joint_count);
            memcpy(out[clip->t_joint[k]].t, blk + ((size_t)3U * k), 3U * sizeof(float));
        }
        for (uint16_t k = 0; k < clip->n_q; ++k) {
            NT_ASSERT(clip->q_joint[k] < clip->joint_count);
            memcpy(out[clip->q_joint[k]].q, blk + q_off + ((size_t)4U * k), 4U * sizeof(float));
        }
        for (uint16_t k = 0; k < clip->n_s; ++k) {
            NT_ASSERT(clip->s_joint[k] < clip->joint_count);
            memcpy(out[clip->s_joint[k]].s, blk + s_off + ((size_t)3U * k), 3U * sizeof(float));
        }
        return;
    }
    // #endregion

    for (uint16_t k = 0; k < clip->n_t; ++k) {
        NT_ASSERT(clip->t_joint[k] < clip->joint_count);
        nt_skeletal_lerp3(a + ((size_t)3U * k), b + ((size_t)3U * k), u, out[clip->t_joint[k]].t);
    }
    for (uint16_t k = 0; k < clip->n_q; ++k) {
        NT_ASSERT(clip->q_joint[k] < clip->joint_count);
        nt_skeletal_nlerp(a + q_off + ((size_t)4U * k), b + q_off + ((size_t)4U * k), u, out[clip->q_joint[k]].q);
    }
    for (uint16_t k = 0; k < clip->n_s; ++k) {
        NT_ASSERT(clip->s_joint[k] < clip->joint_count);
        nt_skeletal_lerp3(a + s_off + ((size_t)3U * k), b + s_off + ((size_t)3U * k), u, out[clip->s_joint[k]].s);
    }
}
// #endregion

// #region composition
#if NT_ASSERT_MODE != NT_ASSERT_OFF
static bool nt_skeletal_poses_disjoint(const nt_skeletal_trs_t *a, const nt_skeletal_trs_t *b, uint16_t joint_count) {
    return (uintptr_t)(a + joint_count) <= (uintptr_t)b || (uintptr_t)(b + joint_count) <= (uintptr_t)a;
}
#endif

#if NT_SKELETAL_CHECKS
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void nt_skeletal_check_trs(const nt_skeletal_trs_t *v) {
    for (int c = 0; c < 3; ++c) {
        NT_ASSERT(nt_skeletal_finite((double)v->t[c]));
        NT_ASSERT(nt_skeletal_finite((double)v->s[c]));
    }
    const float len2 = (v->q[0] * v->q[0]) + (v->q[1] * v->q[1]) + (v->q[2] * v->q[2]) + (v->q[3] * v->q[3]);
    NT_ASSERT((len2 - 1.0F) < 1e-3F && (1.0F - len2) < 1e-3F);
}
#endif

/* The sign that makes w positive. Any fixed rule keeps q and -q equivalent;
 * this one costs a compare instead of a search, and only w == 0 exactly falls
 * back to the largest-component rule of rig identity. */
static float nt_skeletal_mix_seed_sign(const float q[4]) {
    if (q[3] != 0.0F) {
        return copysignf(1.0F, q[3]);
    }
    return nt_skeletal_canonical_sign(q);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void nt_skeletal_mix(const nt_skeletal_mix_input_t *inputs, uint32_t input_count, const nt_skeletal_trs_t *defaults, uint16_t joint_count, nt_skeletal_trs_t *restrict out) {
    NT_ASSERT(inputs != NULL || input_count == 0U);
    NT_ASSERT(defaults != NULL);
    NT_ASSERT(out != NULL);
    NT_ASSERT(nt_skeletal_poses_disjoint(out, defaults, joint_count));
    for (uint32_t i = 0; i < input_count; ++i) {
        NT_ASSERT(inputs[i].pose != NULL);
        /* Also rejects NaN; infinity is left to the numerical checks. */
        NT_ASSERT(inputs[i].gain >= 0.0F);
        NT_ASSERT(nt_skeletal_poses_disjoint(out, inputs[i].pose, joint_count));
#if NT_SKELETAL_CHECKS
        NT_ASSERT(nt_skeletal_finite((double)inputs[i].gain));
#endif
    }

    for (uint16_t j = 0; j < joint_count; ++j) {
        float t[3] = {0.0F, 0.0F, 0.0F};
        float q[4] = {0.0F, 0.0F, 0.0F, 0.0F};
        float s[3] = {0.0F, 0.0F, 0.0F};
        float w_sum = 0.0F;
        for (uint32_t i = 0; i < input_count; ++i) {
            const nt_skeletal_mix_input_t *in = &inputs[i];
            float w = in->gain;
            if (in->weights != NULL) {
                NT_ASSERT(in->weights[j] >= 0.0F);
#if NT_SKELETAL_CHECKS
                NT_ASSERT(nt_skeletal_finite((double)in->weights[j]));
#endif
                w *= in->weights[j];
            }
            if (w == 0.0F) {
                continue;
            }
            const nt_skeletal_trs_t *v = &in->pose[j];
#if NT_SKELETAL_CHECKS
            nt_skeletal_check_trs(v);
#endif
            /* Align against the running sum, not a fixed reference: a
             * dominant-input or rest reference flips sign as gains change.
             * The empty sum is orthogonal to everything, so the first
             * contributor takes the seed sign like any exact tie. */
            const float d = (q[0] * v->q[0]) + (q[1] * v->q[1]) + (q[2] * v->q[2]) + (q[3] * v->q[3]);
            /* The sign of d is data, not control flow: a branch on it
             * mispredicts on inputs from both hemispheres. */
            float wq = copysignf(w, d);
            if (d == 0.0F) {
                wq = w * nt_skeletal_mix_seed_sign(v->q);
            }
            for (int c = 0; c < 3; ++c) {
                t[c] += w * v->t[c];
                s[c] += w * v->s[c];
            }
            for (int c = 0; c < 4; ++c) {
                q[c] += wq * v->q[c];
            }
            w_sum += w;
        }

        if (w_sum == 0.0F) {
            out[j] = defaults[j];
            continue;
        }
        /* Every aligned addend has dot >= 0 with the sum, so the sum never
         * shrinks and is nonzero once any influence is. */
        const float inv_w = 1.0F / w_sum;
        const float inv_len = 1.0F / sqrtf((q[0] * q[0]) + (q[1] * q[1]) + (q[2] * q[2]) + (q[3] * q[3]));
        for (int c = 0; c < 3; ++c) {
            out[j].t[c] = t[c] * inv_w;
            out[j].s[c] = s[c] * inv_w;
        }
        for (int c = 0; c < 4; ++c) {
            out[j].q[c] = q[c] * inv_len;
        }
    }
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void nt_skeletal_override(const nt_skeletal_trs_t *base, const nt_skeletal_trs_t *top, const float *mask, float alpha, uint16_t joint_count, nt_skeletal_trs_t *out) {
    NT_ASSERT(base != NULL);
    NT_ASSERT(top != NULL);
    NT_ASSERT(out != NULL);
    NT_ASSERT(alpha >= 0.0F && alpha <= 1.0F);
    NT_ASSERT(out == base || nt_skeletal_poses_disjoint(out, base, joint_count));
    NT_ASSERT(nt_skeletal_poses_disjoint(out, top, joint_count));

    for (uint16_t j = 0; j < joint_count; ++j) {
        float a = alpha;
        if (mask != NULL) {
            NT_ASSERT(mask[j] >= 0.0F && mask[j] <= 1.0F);
            a *= mask[j];
        }
        if (a == 0.0F) {
            out[j] = base[j];
            continue;
        }
        if (a == 1.0F) {
            out[j] = top[j];
            continue;
        }
#if NT_SKELETAL_CHECKS
        nt_skeletal_check_trs(&base[j]);
        nt_skeletal_check_trs(&top[j]);
#endif
        nt_skeletal_lerp3(base[j].t, top[j].t, a, out[j].t);
        nt_skeletal_nlerp(base[j].q, top[j].q, a, out[j].q);
        nt_skeletal_lerp3(base[j].s, top[j].s, a, out[j].s);
    }
}
// #endregion

// #region clip view
void nt_skeletal_clip_view(const uint8_t *payload, nt_skeletal_clip_t *out) {
    NT_ASSERT(payload != NULL && out != NULL);
    NT_ASSERT((((uintptr_t)payload) & 3U) == 0U && "NANM payload must be 4-aligned: the view reads its arrays in place");

    NtAnmHeader header;
    memcpy(&header, payload, sizeof(header));
    const size_t stride = ((size_t)3U * header.n_t) + ((size_t)4U * header.n_q) + ((size_t)3U * header.n_s);

    /* Walk the arrays in the one order the format defines; the encoder writes
     * them in exactly this sequence. Every product fits size_t on wasm32: the
     * activator proved size == nt_anm_size(header) in 64 bits, and the encoder
     * wrote exactly that many bytes. */
    const uint8_t *at = payload + sizeof(NtAnmHeader);
    const nt_skeletal_trs_t *base = (const nt_skeletal_trs_t *)at;
    at += (size_t)header.joint_count * sizeof(nt_skeletal_trs_t);
    const float *blocks = (const float *)at;
    at += (size_t)header.sample_count * stride * 4U;
    const uint16_t *t_joint = (const uint16_t *)at;
    at += (size_t)header.n_t * 2U;
    const uint16_t *q_joint = (const uint16_t *)at;
    at += (size_t)header.n_q * 2U;
    const uint16_t *s_joint = (const uint16_t *)at;

    *out = (nt_skeletal_clip_t){
        .rig_compat_id = (nt_hash64_t){.value = header.rig_compat_id},
        .duration = (double)header.duration,
        .base = base,
        .blocks = (stride != 0U) ? blocks : NULL,
        .t_joint = t_joint,
        .q_joint = q_joint,
        .s_joint = s_joint,
        .r_joints = header.r_joints,
        .r_root = header.r_root,
        .s_max = header.s_max,
        .sample_count = header.sample_count,
        .joint_count = header.joint_count,
        .n_t = header.n_t,
        .n_q = header.n_q,
        .n_s = header.n_s,
    };
}
// #endregion

// #region rig identity
/* Version of the rig identity byte schema; a new value is a new rig identity. */
#define NT_SKELETAL_RIG_SCHEMA_VERSION 1
/* Reserved: every rig this engine hashes is glTF metres, Y-up, right-handed, so
 * the byte is always 1. */
#define NT_SKELETAL_RIG_CONVENTION_GLTF 1

static uint32_t nt_skeletal_put_u8(uint8_t *bytes, uint32_t offset, uint8_t v) {
    bytes[offset] = v;
    return offset + 1U;
}

static uint32_t nt_skeletal_put_u16(uint8_t *bytes, uint32_t offset, uint16_t v) {
    bytes[offset] = (uint8_t)(v & 0xFFU);
    bytes[offset + 1U] = (uint8_t)((v >> 8U) & 0xFFU);
    return offset + 2U;
}

static uint32_t nt_skeletal_put_u32(uint8_t *bytes, uint32_t offset, uint32_t v) {
    bytes[offset] = (uint8_t)(v & 0xFFU);
    bytes[offset + 1U] = (uint8_t)((v >> 8U) & 0xFFU);
    bytes[offset + 2U] = (uint8_t)((v >> 16U) & 0xFFU);
    bytes[offset + 3U] = (uint8_t)((v >> 24U) & 0xFFU);
    return offset + 4U;
}

static uint32_t nt_skeletal_put_f32(uint8_t *bytes, uint32_t offset, float v) {
    NT_ASSERT(nt_skeletal_finite((double)v));

    /* -0 and +0 describe the same rest pose, so only +0 is ever hashed. */
    const float canonical = (v == 0.0F) ? 0.0F : v;
    uint32_t bits = 0;
    memcpy(&bits, &canonical, sizeof(bits));
    return nt_skeletal_put_u32(bytes, offset, bits);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
nt_hash64_t nt_skeletal_rig_compat_id(const nt_skeletal_skeleton_t *skel, void *scratch, uint32_t scratch_size) {
    NT_ASSERT(skel != NULL);
    NT_ASSERT(skel->parent != NULL);
    NT_ASSERT(skel->joint_id != NULL);
    NT_ASSERT(skel->rest != NULL);
    NT_ASSERT(scratch != NULL);

    const uint32_t size = NT_SKELETAL_RIG_ID_BYTES(skel->joint_count);
    NT_ASSERT(scratch_size >= size);

    uint8_t *bytes = (uint8_t *)scratch;
    uint32_t offset = 0;
    offset = nt_skeletal_put_u8(bytes, offset, (uint8_t)'N');
    offset = nt_skeletal_put_u8(bytes, offset, (uint8_t)'R');
    offset = nt_skeletal_put_u8(bytes, offset, (uint8_t)'I');
    offset = nt_skeletal_put_u8(bytes, offset, (uint8_t)'G');
    offset = nt_skeletal_put_u8(bytes, offset, (uint8_t)NT_SKELETAL_RIG_SCHEMA_VERSION);
    offset = nt_skeletal_put_u8(bytes, offset, (uint8_t)NT_SKELETAL_RIG_CONVENTION_GLTF);
    offset = nt_skeletal_put_u16(bytes, offset, skel->joint_count);

    for (uint16_t j = 0; j < skel->joint_count; ++j) {
        const nt_skeletal_trs_t *rest = &skel->rest[j];
        offset = nt_skeletal_put_u32(bytes, offset, skel->joint_id[j]);
        offset = nt_skeletal_put_u16(bytes, offset, skel->parent[j]);
        for (int c = 0; c < 3; ++c) {
            offset = nt_skeletal_put_f32(bytes, offset, rest->t[c]);
        }
        const float sign = nt_skeletal_canonical_sign(rest->q);
        for (int c = 0; c < 4; ++c) {
            offset = nt_skeletal_put_f32(bytes, offset, rest->q[c] * sign);
        }
        for (int c = 0; c < 3; ++c) {
            offset = nt_skeletal_put_f32(bytes, offset, rest->s[c]);
        }
    }

    NT_ASSERT(offset == size);
    return nt_hash64(bytes, size);
}
// #endregion

// #region tracks
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void nt_skeletal_tracks_advance(nt_skeletal_track_t *tracks, uint32_t count, double dt) {
    NT_ASSERT(tracks != NULL);
    NT_ASSERT(dt >= 0.0);
    /* A non-finite step would reach the int64 cast of the cycle count below. */
    NT_ASSERT(nt_skeletal_finite(dt));

    for (uint32_t i = 0; i < count; ++i) {
        nt_skeletal_track_t *track = &tracks[i];
        if ((track->flags & NT_SKELETAL_TRACK_OCCUPIED) == 0U) {
            continue;
        }
        NT_ASSERT(track->duration >= 0.0);
        NT_ASSERT(nt_skeletal_finite((double)track->speed));

        if (track->duration == 0.0) {
            track->time = 0.0;
            continue;
        }

        double time = track->time + ((double)track->speed * dt);
        if ((track->flags & NT_SKELETAL_TRACK_LOOPING) != 0U) {
            /* Floor/modulo rather than repeated subtraction: reverse playback
             * and a step spanning several cycles both normalize in one go. */
            const double cycles = time / track->duration;
            /* The int64 cast is undefined past 2^63 and traps on wasm; only a
             * caller passing an absurd dt or a sub-attosecond duration gets there. */
            NT_ASSERT(cycles > -9.2e18 && cycles < 9.2e18);
            double whole = (double)(int64_t)cycles;
            if (whole > cycles) {
                whole -= 1.0;
            }
            time -= whole * track->duration;
            /* The quotient and the product round, so an exact cycle boundary
             * can come back as duration or a hair below zero; the cycle starts
             * over at 0 either way. */
            if (time < 0.0 || time >= track->duration) {
                time = 0.0;
            }
        } else if (time < 0.0) {
            time = 0.0;
        } else if (time > track->duration) {
            time = track->duration;
        }
        track->time = time;
    }
}
// #endregion
