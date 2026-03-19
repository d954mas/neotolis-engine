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
} nt_globals_t;

_Static_assert(sizeof(nt_globals_t) == 256, "globals must be 256 bytes for std140");

/* ---- Per-instance data for mesh rendering (80 bytes) ---- */

typedef struct {
    float world_matrix[16]; /* mat4, 64 bytes */
    float color[4];         /* vec4 rgba, 16 bytes */
} nt_mesh_instance_t;

_Static_assert(sizeof(nt_mesh_instance_t) == 80, "mesh instance must be 80 bytes");

/* ---- Render item for sort and draw (12 bytes) ---- */

#pragma pack(push, 1)
typedef struct {
    uint64_t sort_key;
    uint32_t entity; /* raw entity id (not nt_entity_t to keep 12 bytes) */
} nt_render_item_t;
#pragma pack(pop)

_Static_assert(sizeof(nt_render_item_t) == 12, "render item must be 12 bytes");

#endif /* NT_RENDER_DEFS_H */
