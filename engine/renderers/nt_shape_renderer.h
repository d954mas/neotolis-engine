#ifndef NT_SHAPE_RENDERER_H
#define NT_SHAPE_RENDERER_H

#include "core/nt_types.h"

/* ---- Compile-time limits (overridable) ---- */

#ifndef NT_SHAPE_RENDERER_MAX_VERTICES
#define NT_SHAPE_RENDERER_MAX_VERTICES 16384
#endif

#ifndef NT_SHAPE_RENDERER_MAX_INDICES
#define NT_SHAPE_RENDERER_MAX_INDICES 32768
#endif

#ifndef NT_SHAPE_RENDERER_MAX_LINES
#define NT_SHAPE_RENDERER_MAX_LINES 8192
#endif

/* ---- Vertex format ---- */

typedef struct {
    float pos[3];     /* world-space position */
    uint8_t color[4]; /* RGBA packed (0-255, GPU normalizes to 0.0-1.0) */
} nt_shape_renderer_vertex_t;

/* ---- Lifecycle ---- */

void nt_shape_renderer_init(void);
void nt_shape_renderer_shutdown(void);
void nt_shape_renderer_flush(void);

/* ---- State setters ---- */

void nt_shape_renderer_set_vp(const float vp[16]);
void nt_shape_renderer_set_cam_pos(const float pos[3]);
void nt_shape_renderer_set_line_width(float width);
void nt_shape_renderer_set_depth(bool enabled);

/* ---- Line ---- */

void nt_shape_renderer_line(const float a[3], const float b[3], const float color[4]);

/* ---- Rectangle ---- */

void nt_shape_renderer_rect(const float pos[3], const float size[2], const float color[4]);
void nt_shape_renderer_rect_wire(const float pos[3], const float size[2], const float color[4]);
void nt_shape_renderer_rect_rot(const float pos[3], const float size[2], const float rot[4], const float color[4]);
void nt_shape_renderer_rect_wire_rot(const float pos[3], const float size[2], const float rot[4], const float color[4]);

/* ---- Triangle ---- */

void nt_shape_renderer_triangle(const float a[3], const float b[3], const float c[3], const float color[4]);
void nt_shape_renderer_triangle_wire(const float a[3], const float b[3], const float c[3], const float color[4]);
void nt_shape_renderer_triangle_col(const float a[3], const float b[3], const float c[3], const float color_a[4], const float color_b[4], const float color_c[4]);

/* ---- Circle ---- */

void nt_shape_renderer_circle(const float center[3], float radius, const float color[4]);
void nt_shape_renderer_circle_wire(const float center[3], float radius, const float color[4]);
void nt_shape_renderer_circle_rot(const float center[3], float radius, const float rot[4], const float color[4]);
void nt_shape_renderer_circle_wire_rot(const float center[3], float radius, const float rot[4], const float color[4]);

/* ---- Cube ---- */

void nt_shape_renderer_cube(const float center[3], const float size[3], const float color[4]);
void nt_shape_renderer_cube_wire(const float center[3], const float size[3], const float color[4]);
void nt_shape_renderer_cube_rot(const float center[3], const float size[3], const float rot[4], const float color[4]);
void nt_shape_renderer_cube_wire_rot(const float center[3], const float size[3], const float rot[4], const float color[4]);

/* ---- Sphere ---- */

void nt_shape_renderer_sphere(const float center[3], float radius, const float color[4]);
void nt_shape_renderer_sphere_wire(const float center[3], float radius, const float color[4]);

/* ---- Cylinder ---- */

void nt_shape_renderer_cylinder(const float center[3], float radius, float height, const float color[4]);
void nt_shape_renderer_cylinder_wire(const float center[3], float radius, float height, const float color[4]);
void nt_shape_renderer_cylinder_rot(const float center[3], float radius, float height, const float rot[4], const float color[4]);
void nt_shape_renderer_cylinder_wire_rot(const float center[3], float radius, float height, const float rot[4], const float color[4]);

/* ---- Capsule ---- */

void nt_shape_renderer_capsule(const float center[3], float radius, float height, const float color[4]);
void nt_shape_renderer_capsule_wire(const float center[3], float radius, float height, const float color[4]);
void nt_shape_renderer_capsule_rot(const float center[3], float radius, float height, const float rot[4], const float color[4]);
void nt_shape_renderer_capsule_wire_rot(const float center[3], float radius, float height, const float rot[4], const float color[4]);

/* ---- Mesh ---- */

void nt_shape_renderer_mesh(const float *positions, uint32_t num_vertices, const uint16_t *indices, uint32_t num_indices, const float color[4]);
void nt_shape_renderer_mesh_wire(const float *positions, uint32_t num_vertices, const uint16_t *indices, uint32_t num_indices, const float color[4]);

/* ---- Test accessors (test builds only) ---- */

#ifdef NT_SHAPE_RENDERER_TEST_ACCESS

/* Instanced shape types (for test_instance_count) */
enum {
    NT_SHAPE_TEST_RECT = 0,
    NT_SHAPE_TEST_CUBE,
    NT_SHAPE_TEST_CIRCLE,
    NT_SHAPE_TEST_SPHERE,
    NT_SHAPE_TEST_CYLINDER,
    NT_SHAPE_TEST_CAPSULE,
};

uint32_t nt_shape_renderer_test_instance_count(int type);
uint32_t nt_shape_renderer_test_vertex_count(void);
uint32_t nt_shape_renderer_test_index_count(void);
uint32_t nt_shape_renderer_test_line_count(void);
const float *nt_shape_renderer_test_cam_pos(void);
float nt_shape_renderer_test_line_width(void);
bool nt_shape_renderer_test_initialized(void);
#endif

#endif /* NT_SHAPE_RENDERER_H */
