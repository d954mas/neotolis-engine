#ifndef ASTEROIDS_REFERENCE_SCENE_FORMAT_H
#define ASTEROIDS_REFERENCE_SCENE_FORMAT_H

#include <stdint.h>

/* Offline file layout: little-endian IEEE-754 binary32, followed by records,
 * then mesh_subset_texture_count uint32_t texture indices in subdivision-major order. */
typedef struct ast_reference_scene_header {
    char magic[8];
    uint32_t version;
    uint32_t header_bytes;
    uint32_t record_bytes;
    uint32_t complexity;
    uint32_t random_seed;
    uint32_t instance_count;
    uint32_t unique_mesh_count;
    uint32_t subdivisions_count;
    uint32_t textures_count;
    uint32_t mesh_subset_texture_count;
    float scene_scale;
    float min_scale_ratio;
    float max_scale_ratio;
    uint32_t reserved;
} ast_reference_scene_header;

typedef struct ast_reference_instance {
    uint32_t index;
    uint32_t mesh_instance_index;
    uint32_t texture_index;
    float deep_color[3];
    float shallow_color[3];
    /* Original left-handed, row-vector matrix, stored in row-major order. */
    float scale_translate_matrix[16];
    float spin_axis[3];
    float scale;
    float orbit_speed;
    float spin_speed;
    float spin_angle_rad;
    float orbit_angle_rad;
} ast_reference_instance;

enum { AST_REFERENCE_RUNTIME_MAGIC = 0x49545341U };

typedef struct ast_reference_runtime_header {
    uint32_t magic;
    uint32_t version;
    uint32_t complexity;
    uint32_t instance_count;
} ast_reference_runtime_header;

typedef struct ast_reference_runtime_instance {
    float scale_translate[16];
    float spin_axis[3];
    float scale;
    float deep[3];
    float spin_speed;
    float shallow[3];
    float orbit_speed;
    float spin_angle;
    float orbit_angle;
    uint32_t mesh_index;
    uint32_t texture_index;
} ast_reference_runtime_instance;

#if defined(__cplusplus)
static_assert(sizeof(ast_reference_scene_header) == 64);
static_assert(sizeof(ast_reference_instance) == 132);
static_assert(sizeof(ast_reference_runtime_header) == 16);
static_assert(sizeof(ast_reference_runtime_instance) == 128);
#else
_Static_assert(sizeof(ast_reference_scene_header) == 64, "Reference scene header ABI");
_Static_assert(sizeof(ast_reference_instance) == 132, "Reference instance ABI");
_Static_assert(sizeof(ast_reference_runtime_header) == 16, "Runtime reference header ABI");
_Static_assert(sizeof(ast_reference_runtime_instance) == 128, "Runtime reference instance ABI");
#endif

#endif
