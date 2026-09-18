#ifndef SKELETAL_SHOWCASE_LIMITS_H
#define SKELETAL_SHOWCASE_LIMITS_H

/* Pose buffers of main.c are sized for this many joints; build_packs.c refuses
 * a rig above it, so a bigger asset fails at pack build rather than at runtime. */
#define SKELETAL_SHOWCASE_MAX_JOINTS 32U

#endif /* SKELETAL_SHOWCASE_LIMITS_H */
