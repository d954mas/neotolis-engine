#ifndef NT_SHAPE_H
#define NT_SHAPE_H

#include "core/nt_types.h"

/* ---- Compile-time limits (overridable) ---- */

#ifndef NT_SHAPE_MAX_VERTICES
#define NT_SHAPE_MAX_VERTICES 65535
#endif

#ifndef NT_SHAPE_MAX_INDICES
#define NT_SHAPE_MAX_INDICES 131072
#endif

/* ---- Vertex format ---- */

typedef struct {
    float pos[3];   /* world-space position */
    float color[4]; /* RGBA (0.0-1.0) */
} nt_shape_vertex_t;

/* ---- Lifecycle ---- */

void nt_shape_init(void);
void nt_shape_shutdown(void);
void nt_shape_flush(void);

/* ---- State setters ---- */

void nt_shape_set_vp(const float vp[16]);
void nt_shape_set_line_width(float width);
void nt_shape_set_depth(bool enabled);
void nt_shape_set_blend(bool enabled);
void nt_shape_set_segments(int segments);

/* ---- Line ---- */

void nt_shape_line(const float a[3], const float b[3], const float color[4]);
void nt_shape_line_col(const float a[3], const float b[3], const float color_a[4], const float color_b[4]);

/* ---- Rectangle ---- */

void nt_shape_rect(const float pos[3], const float size[2], const float color[4]);
void nt_shape_rect_wire(const float pos[3], const float size[2], const float color[4]);
void nt_shape_rect_rot(const float pos[3], const float size[2], const float rot[4], const float color[4]);
void nt_shape_rect_wire_rot(const float pos[3], const float size[2], const float rot[4], const float color[4]);

/* ---- Triangle ---- */

void nt_shape_triangle(const float a[3], const float b[3], const float c[3], const float color[4]);
void nt_shape_triangle_wire(const float a[3], const float b[3], const float c[3], const float color[4]);
void nt_shape_triangle_col(const float a[3], const float b[3], const float c[3], const float color_a[4], const float color_b[4], const float color_c[4]);
void nt_shape_triangle_wire_col(const float a[3], const float b[3], const float c[3], const float color_a[4], const float color_b[4], const float color_c[4]);

/* ---- Circle ---- */

void nt_shape_circle(const float center[3], float radius, const float color[4]);
void nt_shape_circle_wire(const float center[3], float radius, const float color[4]);
void nt_shape_circle_rot(const float center[3], float radius, const float rot[4], const float color[4]);
void nt_shape_circle_wire_rot(const float center[3], float radius, const float rot[4], const float color[4]);

/* ---- Cube ---- */

void nt_shape_cube(const float center[3], const float size[3], const float color[4]);
void nt_shape_cube_wire(const float center[3], const float size[3], const float color[4]);
void nt_shape_cube_rot(const float center[3], const float size[3], const float rot[4], const float color[4]);
void nt_shape_cube_wire_rot(const float center[3], const float size[3], const float rot[4], const float color[4]);

/* ---- Sphere ---- */

void nt_shape_sphere(const float center[3], float radius, const float color[4]);
void nt_shape_sphere_wire(const float center[3], float radius, const float color[4]);

/* ---- Cylinder ---- */

void nt_shape_cylinder(const float center[3], float radius, float height, const float color[4]);
void nt_shape_cylinder_wire(const float center[3], float radius, float height, const float color[4]);
void nt_shape_cylinder_rot(const float center[3], float radius, float height, const float rot[4], const float color[4]);
void nt_shape_cylinder_wire_rot(const float center[3], float radius, float height, const float rot[4], const float color[4]);

/* ---- Capsule ---- */

void nt_shape_capsule(const float center[3], float radius, float height, const float color[4]);
void nt_shape_capsule_wire(const float center[3], float radius, float height, const float color[4]);
void nt_shape_capsule_rot(const float center[3], float radius, float height, const float rot[4], const float color[4]);
void nt_shape_capsule_wire_rot(const float center[3], float radius, float height, const float rot[4], const float color[4]);

/* ---- Mesh ---- */

void nt_shape_mesh(const float *positions, const uint16_t *indices, uint32_t num_indices, const float color[4]);
void nt_shape_mesh_wire(const float *positions, const uint16_t *indices, uint32_t num_indices, const float color[4]);

/* ---- Test accessors (test builds only) ---- */

#ifdef NT_SHAPE_TEST_ACCESS
uint32_t nt_shape_test_vertex_count(void);
uint32_t nt_shape_test_index_count(void);
const float *nt_shape_test_cam_pos(void);
float nt_shape_test_line_width(void);
int nt_shape_test_segments(void);
bool nt_shape_test_initialized(void);
#endif

#endif /* NT_SHAPE_H */
