#ifndef NT_RENDER_DEFS_H
#define NT_RENDER_DEFS_H

#include <stdint.h>

/* ---- Globals UBO (slot 0, std140 layout, 256 bytes) ---- */

typedef struct {
    float view_proj[16]; /* mat4, 64 bytes */
    float view[16];      /* mat4, 64 bytes */
    float proj[16];      /* mat4, 64 bytes */
    float camera_pos[4]; /* vec4: xyz=pos, w=reserved */
    float time[4];       /* vec4: .x=elapsed, .y=delta, .zw=reserved */
    float resolution[4]; /* vec4: .x=w, .y=h, .z=1/w, .w=1/h */
    float near_far[4];   /* vec4: .x=near, .y=far, .z=1/near, .w=1/far */
} nt_frame_uniforms_t;

_Static_assert(sizeof(nt_frame_uniforms_t) == 256, "globals must be 256 bytes for std140");

/* ---- Color mode for per-instance color precision ---- */

typedef enum {
    NT_COLOR_MODE_NONE = 0, /* No per-instance color, 48 bytes */
    NT_COLOR_MODE_RGBA8,    /* 4 bytes packed color, 52 bytes */
    NT_COLOR_MODE_FLOAT4,   /* 16 bytes float color, 64 bytes */
} nt_color_mode_t;

/* ---- Instance stride constants (bytes per instance by color mode) ---- */

#define NT_INSTANCE_STRIDE_NONE 48   /* mat4x3 only */
#define NT_INSTANCE_STRIDE_RGBA8 56  /* mat4x3 + uint8[4] + 4 pad (aligned to 8) */
#define NT_INSTANCE_STRIDE_FLOAT4 64 /* mat4x3 + float[4] */
#define NT_INSTANCE_STRIDE_MAX 64    /* worst-case for buffer sizing */

_Static_assert(NT_INSTANCE_STRIDE_NONE == 3 * 4 * 4, "NONE = 3 rows of vec4");
_Static_assert(NT_INSTANCE_STRIDE_RGBA8 == 3 * 4 * 4 + 4 + 4, "RGBA8 = mat4x3 + 4 bytes color + 4 pad");
_Static_assert(NT_INSTANCE_STRIDE_FLOAT4 == 3 * 4 * 4 + 4 * 4, "FLOAT4 = mat4x3 + vec4");
_Static_assert(NT_COLOR_MODE_FLOAT4 == 2, "update s_instance_layouts if enum grows");

/* ---- Render item for sort and draw (16 bytes, naturally aligned) ---- */

typedef struct {
    uint64_t sort_key;
    uint32_t entity;    /* raw entity id (not nt_entity_t) */
    uint32_t batch_key; /* state compatibility: same material+mesh = same key, game fills this */
} nt_render_item_t;

_Static_assert(sizeof(nt_render_item_t) == 16, "render item must be 16 bytes");

#endif /* NT_RENDER_DEFS_H */
