#ifndef NT_SHAPE_RENDERER_H
#define NT_SHAPE_RENDERER_H

#include "core/nt_types.h"

/* ---- Compile-time limits (overridable) ----
 * CPU staging between flushes; a full queue flushes. Frame storage holds every flush of the frame. */

#ifndef NT_SHAPE_RENDERER_MAX_VERTICES
#define NT_SHAPE_RENDERER_MAX_VERTICES 16384
#endif

#ifndef NT_SHAPE_RENDERER_MAX_INSTANCES
#define NT_SHAPE_RENDERER_MAX_INSTANCES 2048
#endif

#ifndef NT_SHAPE_RENDERER_MAX_LINES
#define NT_SHAPE_RENDERER_MAX_LINES 8192
#endif

#ifndef NT_SHAPE_RENDERER_MAX_POLYLINE_SEGMENTS
#define NT_SHAPE_RENDERER_MAX_POLYLINE_SEGMENTS 1024
#endif

/* ---- Lifecycle ---- */

void nt_shape_renderer_init(void);
void nt_shape_renderer_shutdown(void);
void nt_shape_renderer_restore_gpu(void);
/* Draws pending fills, then strokes. The only draw-order barrier: render architecture, "Shape strokes". */
void nt_shape_renderer_flush(void);

/* ---- State setters ---- */

/* Also defines the camera that world-width strokes face. */
void nt_shape_renderer_set_vp(const float vp[16]);
/* Select world-space thickness (default 0.02). Width must be finite and positive. */
void nt_shape_renderer_set_line_width(float width);
/* Select framebuffer-pixel thickness. Width must be finite and positive; viewport dimensions must be positive.
 * Pass the active viewport dimensions, not CSS size. */
void nt_shape_renderer_set_line_width_pixels(float width, uint32_t viewport_width, uint32_t viewport_height);
void nt_shape_renderer_set_depth(bool enabled);
/* Frame vertex stream for instanced shapes (default NT_GFX_FRAME_VERTEX); a pass with its own
 * stream keeps its shapes' instance pointers while counts stay. Triangle batches stay on the
 * general stream, which must be sized. */
void nt_shape_renderer_set_stream(uint32_t stream);

/* ---- Line ---- */

void nt_shape_renderer_line(const float a[3], const float b[3], uint32_t color);

/* Connected finite world-space points, read during the call; consecutive duplicates are skipped.
 * Joins, ends and closing rules: render architecture, "Shape strokes". */
void nt_shape_renderer_polyline(const float (*points)[3], uint32_t count, bool closed, uint32_t color);

/* ---- Shapes ----
 * rot is a unit quaternion (x, y, z, w) or NULL for no rotation. */

void nt_shape_renderer_rect(const float pos[3], const float size[2], const float rot[4], uint32_t color);
void nt_shape_renderer_rect_wire(const float pos[3], const float size[2], const float rot[4], uint32_t color);

void nt_shape_renderer_triangle(const float a[3], const float b[3], const float c[3], uint32_t color);
void nt_shape_renderer_triangle_wire(const float a[3], const float b[3], const float c[3], uint32_t color);

void nt_shape_renderer_circle(const float center[3], float radius, const float rot[4], uint32_t color);
void nt_shape_renderer_circle_wire(const float center[3], float radius, const float rot[4], uint32_t color);

void nt_shape_renderer_cube(const float center[3], const float size[3], const float rot[4], uint32_t color);
void nt_shape_renderer_cube_wire(const float center[3], const float size[3], const float rot[4], uint32_t color);

void nt_shape_renderer_sphere(const float center[3], float radius, const float rot[4], uint32_t color);
void nt_shape_renderer_sphere_wire(const float center[3], float radius, const float rot[4], uint32_t color);

void nt_shape_renderer_cylinder(const float center[3], float radius, float height, const float rot[4], uint32_t color);
void nt_shape_renderer_cylinder_wire(const float center[3], float radius, float height, const float rot[4], uint32_t color);

void nt_shape_renderer_capsule(const float center[3], float radius, float height, const float rot[4], uint32_t color);
void nt_shape_renderer_capsule_wire(const float center[3], float radius, float height, const float rot[4], uint32_t color);

// #region test_access
#ifdef NT_TEST_ACCESS

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
uint32_t nt_shape_renderer_test_stroke_count(void);
const float *nt_shape_renderer_test_vp(void);
const float *nt_shape_renderer_test_eye(void);
float nt_shape_renderer_test_line_width(void);
bool nt_shape_renderer_test_depth_enabled(void);
bool nt_shape_renderer_test_initialized(void);
#endif
// #endregion

#endif /* NT_SHAPE_RENDERER_H */
