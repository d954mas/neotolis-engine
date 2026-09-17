#include "skeletal/nt_skeletal.h"

#include <string.h>

#include "core/nt_builtins.h"

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

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void nt_skeletal_socket(const float world[16], const nt_skeletal_mat34_t *g_joint, const nt_skeletal_trs_t *socket_local, nt_skeletal_mat34_t *out) {
    NT_ASSERT(world != NULL);
    NT_ASSERT(g_joint != NULL);
    NT_ASSERT(socket_local != NULL);
    NT_ASSERT(out != NULL);
    NT_ASSERT(out != g_joint);

    nt_skeletal_mat34_t e;
    nt_skeletal_mat34_from_mat4(world, &e);

    nt_skeletal_mat34_t eg;
    nt_skeletal_mat34_mul(&e, g_joint, &eg);

    nt_skeletal_mat34_t s;
    nt_skeletal_mat34_from_trs(socket_local, &s);

    nt_skeletal_mat34_mul(&eg, &s, out);
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
 * lands on the grid yields u == 0, or u == 1 at the very end, which the callers
 * turn into an exact copy of a stored sample. */
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

/* Value of the last key at or before time, or the first key when time precedes
 * it. Binary search, so a random seek into a long track costs log2(count)
 * instead of walking every earlier key. */
static const float *nt_skeletal_step_value(const nt_skeletal_step_key_t *keys, uint32_t first, uint32_t count, double time) {
    NT_ASSERT(keys != NULL);
    NT_ASSERT(count >= 1U);

    /* lo ends as the number of keys at or before time. */
    uint32_t lo = 0;
    uint32_t hi = count;
    while (lo < hi) {
        const uint32_t mid = lo + ((hi - lo) / 2U);
        if ((double)keys[first + mid].time <= time) {
            lo = mid + 1U;
        } else {
            hi = mid;
        }
    }
    const uint32_t k = (lo == 0U) ? first : (first + lo - 1U);
    return keys[k].v;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void nt_skeletal_apply_sampled(const nt_skeletal_clip_t *clip, double time, nt_skeletal_trs_t *out) {
    float u = 0.0F;
    const uint32_t i = nt_skeletal_grid_index(time, clip->inv_step, clip->sample_count, &u);
    const float *a = clip->blocks + ((size_t)i * clip->block_floats);
    const float *b = a + clip->block_floats;
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

/* The pose check is nothing but asserts, so NT_ASSERT_MODE=OFF empties it: the
 * function and its calls compile out together instead of leaving locals behind. */
#if NT_SKELETAL_CHECKS && NT_ASSERT_MODE != NT_ASSERT_OFF
#define NT_SKELETAL_CHECK_POSE 1
#else
#define NT_SKELETAL_CHECK_POSE 0
#endif

#if NT_SKELETAL_CHECK_POSE
/* x - x rejects non-finite values without libm; requires strict IEEE math. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void nt_skeletal_check_pose(const nt_skeletal_trs_t *pose, uint32_t count) {
    for (uint32_t j = 0; j < count; ++j) {
        const nt_skeletal_trs_t *p = &pose[j];
        for (int c = 0; c < 3; ++c) {
            NT_ASSERT((p->t[c] - p->t[c]) == 0.0F);
            NT_ASSERT((p->s[c] - p->s[c]) == 0.0F);
        }
        const float n = (p->q[0] * p->q[0]) + (p->q[1] * p->q[1]) + (p->q[2] * p->q[2]) + (p->q[3] * p->q[3]);
        NT_ASSERT((n - 1.0F) < 1e-3F && (1.0F - n) < 1e-3F);
    }
}
#endif

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void nt_skeletal_sample(const nt_skeletal_clip_t *clip, double time, const nt_skeletal_trs_t *restrict defaults, nt_skeletal_trs_t *restrict out) {
    NT_ASSERT(clip != NULL);
    NT_ASSERT(defaults != NULL);
    NT_ASSERT(out != NULL);
    NT_ASSERT(clip->joint_count >= 1U);
    NT_ASSERT(clip->sample_count >= 1U);
    NT_ASSERT(time >= 0.0 && time <= clip->duration);
    /* Absent channels are read from defaults after the copy, so an overlapping
     * output would feed later joints values the clip already overwrote. */
    NT_ASSERT((uintptr_t)(defaults + clip->joint_count) <= (uintptr_t)out || (uintptr_t)(out + clip->joint_count) <= (uintptr_t)defaults);

    memcpy(out, defaults, (size_t)clip->joint_count * sizeof(nt_skeletal_trs_t));

    // #region constants
    for (uint16_t k = 0; k < clip->n_ct; ++k) {
        NT_ASSERT(clip->ct_joint[k] < clip->joint_count);
        memcpy(out[clip->ct_joint[k]].t, clip->ct + ((size_t)3U * k), 3U * sizeof(float));
    }
    for (uint16_t k = 0; k < clip->n_cq; ++k) {
        NT_ASSERT(clip->cq_joint[k] < clip->joint_count);
        memcpy(out[clip->cq_joint[k]].q, clip->cq + ((size_t)4U * k), 4U * sizeof(float));
    }
    for (uint16_t k = 0; k < clip->n_cs; ++k) {
        NT_ASSERT(clip->cs_joint[k] < clip->joint_count);
        memcpy(out[clip->cs_joint[k]].s, clip->cs + ((size_t)3U * k), 3U * sizeof(float));
    }
    // #endregion

    if (clip->n_t != 0U || clip->n_q != 0U || clip->n_s != 0U) {
        NT_ASSERT(clip->blocks != NULL);
        NT_ASSERT(clip->block_floats == (3U * (uint32_t)clip->n_t) + (4U * (uint32_t)clip->n_q) + (3U * (uint32_t)clip->n_s));
        nt_skeletal_apply_sampled(clip, time, out);
    }

    // #region step tracks
    /* A channel has one mode, so a STEP track never contends with a constant or
     * a sampled row for the same joint channel. */
    for (uint32_t s = 0; s < clip->n_steps; ++s) {
        const nt_skeletal_step_t *track = &clip->steps[s];
        NT_ASSERT(track->joint < clip->joint_count);
        NT_ASSERT(track->channel <= 2U);
        const float *v = nt_skeletal_step_value(clip->keys, track->first, track->count, time);
        nt_skeletal_trs_t *o = &out[track->joint];
        if (track->channel == 0U) {
            memcpy(o->t, v, 3U * sizeof(float));
        } else if (track->channel == 1U) {
            memcpy(o->q, v, 4U * sizeof(float));
        } else {
            memcpy(o->s, v, 3U * sizeof(float));
        }
    }
    // #endregion

#if NT_SKELETAL_CHECK_POSE
    nt_skeletal_check_pose(out, clip->joint_count);
#endif
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void nt_skeletal_sample_object(const nt_skeletal_object_curve_t *curve, double time, const nt_skeletal_trs_t *defaults, nt_skeletal_trs_t *out) {
    NT_ASSERT(defaults != NULL);
    NT_ASSERT(out != NULL);
    NT_ASSERT(out != defaults);

    *out = *defaults;
    if (curve == NULL) {
        return;
    }

    const uint8_t mt = curve->mode[0];
    const uint8_t mq = curve->mode[1];
    const uint8_t ms = curve->mode[2];
    NT_ASSERT(mt <= NT_SKELETAL_CHANNEL_STEP && mq <= NT_SKELETAL_CHANNEL_STEP && ms <= NT_SKELETAL_CHANNEL_STEP);
    /* A curve with no channel is the same as no curve, so it carries no grid and
     * no time range to hold the caller to. */
    if (mt == NT_SKELETAL_CHANNEL_ABSENT && mq == NT_SKELETAL_CHANNEL_ABSENT && ms == NT_SKELETAL_CHANNEL_ABSENT) {
        return;
    }
    NT_ASSERT(time >= 0.0 && time <= curve->duration);

    // #region sampled pair
    const nt_skeletal_trs_t *a = NULL;
    const nt_skeletal_trs_t *b = NULL;
    float u = 0.0F;
    if (mt == NT_SKELETAL_CHANNEL_SAMPLED || mq == NT_SKELETAL_CHANNEL_SAMPLED || ms == NT_SKELETAL_CHANNEL_SAMPLED) {
        NT_ASSERT(curve->sampled != NULL);
        const uint32_t i = nt_skeletal_grid_index(time, curve->inv_step, curve->sample_count, &u);
        a = &curve->sampled[i];
        b = &curve->sampled[i + 1U];
        /* Same exactness rule as the joint grid: a grid time copies its sample. */
        if (u == 0.0F) {
            b = a;
        } else if (u == 1.0F) {
            a = b;
        }
    }
    // #endregion

    if (mt == NT_SKELETAL_CHANNEL_CONSTANT) {
        memcpy(out->t, curve->constant.t, sizeof(out->t));
    } else if (mt == NT_SKELETAL_CHANNEL_SAMPLED) {
        if (a == b) {
            memcpy(out->t, a->t, sizeof(out->t));
        } else {
            nt_skeletal_lerp3(a->t, b->t, u, out->t);
        }
    } else if (mt == NT_SKELETAL_CHANNEL_STEP) {
        memcpy(out->t, nt_skeletal_step_value(curve->keys, curve->step_first[0], curve->step_count[0], time), sizeof(out->t));
    }

    if (mq == NT_SKELETAL_CHANNEL_CONSTANT) {
        memcpy(out->q, curve->constant.q, sizeof(out->q));
    } else if (mq == NT_SKELETAL_CHANNEL_SAMPLED) {
        if (a == b) {
            memcpy(out->q, a->q, sizeof(out->q));
        } else {
            nt_skeletal_nlerp(a->q, b->q, u, out->q);
        }
    } else if (mq == NT_SKELETAL_CHANNEL_STEP) {
        memcpy(out->q, nt_skeletal_step_value(curve->keys, curve->step_first[1], curve->step_count[1], time), sizeof(out->q));
    }

    if (ms == NT_SKELETAL_CHANNEL_CONSTANT) {
        memcpy(out->s, curve->constant.s, sizeof(out->s));
    } else if (ms == NT_SKELETAL_CHANNEL_SAMPLED) {
        if (a == b) {
            memcpy(out->s, a->s, sizeof(out->s));
        } else {
            nt_skeletal_lerp3(a->s, b->s, u, out->s);
        }
    } else if (ms == NT_SKELETAL_CHANNEL_STEP) {
        memcpy(out->s, nt_skeletal_step_value(curve->keys, curve->step_first[2], curve->step_count[2], time), sizeof(out->s));
    }

#if NT_SKELETAL_CHECK_POSE
    nt_skeletal_check_pose(out, 1U);
#endif
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
    /* v - v rejects non-finite values without libm; requires strict IEEE math. */
    NT_ASSERT((v - v) == 0.0F);

    /* -0 and +0 describe the same rest pose, so only +0 is ever hashed. */
    const float canonical = (v == 0.0F) ? 0.0F : v;
    uint32_t bits = 0;
    memcpy(&bits, &canonical, sizeof(bits));
    return nt_skeletal_put_u32(bytes, offset, bits);
}

/* q and -q are the same rotation: keep the sign that makes the largest
 * component positive, ties broken by the first maximum in x,y,z,w order. */
static void nt_skeletal_canonical_quat(const float q[4], float out[4]) {
    int best = 0;
    float best_abs = (q[0] < 0.0F) ? -q[0] : q[0];
    for (int c = 1; c < 4; ++c) {
        const float a = (q[c] < 0.0F) ? -q[c] : q[c];
        if (a > best_abs) {
            best_abs = a;
            best = c;
        }
    }

    const float sign = (q[best] < 0.0F) ? -1.0F : 1.0F;
    for (int c = 0; c < 4; ++c) {
        out[c] = q[c] * sign;
    }
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
        float q[4];
        nt_skeletal_canonical_quat(rest->q, q);
        for (int c = 0; c < 4; ++c) {
            offset = nt_skeletal_put_f32(bytes, offset, q[c]);
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
    /* x - x rejects NaN and infinity without libm: a non-finite step would reach
     * the int64 cast of the cycle count below. */
    NT_ASSERT((dt - dt) == 0.0);

    for (uint32_t i = 0; i < count; ++i) {
        nt_skeletal_track_t *track = &tracks[i];
        if ((track->flags & NT_SKELETAL_TRACK_OCCUPIED) == 0U) {
            continue;
        }
        NT_ASSERT(track->duration >= 0.0);
        NT_ASSERT((track->speed - track->speed) == 0.0F);

        if (track->duration == 0.0) {
            track->time = 0.0;
            continue;
        }

        double time = track->time + ((double)track->speed * dt);
        if ((track->flags & NT_SKELETAL_TRACK_LOOPING) != 0U) {
            /* Floor/modulo rather than repeated subtraction: reverse playback
             * and a step spanning several cycles both normalize in one go.
             * Truncate-then-correct floor keeps the module free of libm. */
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
