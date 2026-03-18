# Phase 16: Shape Renderer - Context

**Gathered:** 2026-03-12
**Status:** Ready for planning

<domain>
## Phase Boundary

Immediate-mode debug shape drawing module (nt_shape). 8 shape types (line, rect, circle, triangle, cube, sphere, cylinder, capsule) with fill, wireframe, and per-vertex color. Shapes batched into a single dynamic VBO+IBO per frame using preallocated CPU buffers with no heap allocation in the draw path. Wireframe uses triangle-based billboard quads since glLineWidth is a no-op in WebGL.

</domain>

<decisions>
## Implementation Decisions

### API style — function-per-shape
- One function per shape type, `_wire` suffix for wireframe variant
- `_rot` suffix for rotated variant (only for shapes that need it)
- Each shape = up to 4 functions: base, `_wire`, `_rot`, `_wire_rot`
- Shapes without rotation: line (2 points), triangle (3 points), sphere (symmetric)
- Shapes with rotation: rect, circle, cube, cylinder, capsule
- Total: ~27 functions

### Coordinate system
- All positions in world space, center-based
- Rect at (5,0,3) with size (2,1) means center at (5,0,3), extends ±1 on X, ±0.5 on Y
- Vertices computed on CPU as `center + offset * half_size`, written directly to VBO
- Shader does only `gl_Position = u_vp * vec4(pos, 1.0)` — no model matrix in shader

### Per-call parameters
- Position, size, color — always per call
- Rotation (versor/quaternion) — per call via `_rot` functions, no rotation = no NULL noise
- Clean common case: `nt_shape_cube(pos, size, color)` — no optional params

### Global state setters (rarely change)
- `nt_shape_set_vp(mat4)` — view-projection matrix, set once per frame
- `nt_shape_set_line_width(float)` — world-space units, default 0.02
- `nt_shape_set_depth(bool)` — switches between depth-on/off pipeline, default true
- `nt_shape_set_blend(bool)` — switches between blend-on/off pipeline, default false
- `nt_shape_set_segments(int)` — tessellation for circles/spheres/cylinders/capsules, 0 = default 32

### Per-vertex color
- Claude's discretion on API (uniform color + gradient helpers, or other approach)
- Must satisfy SHAPE-07: per-vertex color visually distinguishable across vertices of same shape

### Explicit flush
- `nt_shape_flush()` — game must call to submit batch to GPU
- Auto-flush on buffer overflow (transparent, no shapes lost, extra draw call)
- No auto-flush at end of pass — explicit over implicit
- Game controls draw order: flush opaque shapes, draw game objects, flush overlay shapes

### Wireframe — billboard quads
- Each wireframe edge = thin quad (2 triangles) that twists around edge axis to face camera
- Industry standard approach (Unity, Unreal, bgfx, im3d)
- Edge endpoints fixed in world space — shape orientation fully preserved
- Needs camera position on CPU (extractable from VP matrix)
- Line thickness in world-space units (consistent with positions)

### Depth — two pipelines + toggle
- `nt_shape_set_depth(true)` — shapes occluded normally (default)
- `nt_shape_set_depth(false)` — overlay mode, shapes draw on top of everything
- Shape renderer is "dumb" — doesn't manage depth logic, just switches pipeline

### Blend — two more pipelines + toggle
- `nt_shape_set_blend(true)` — alpha blending enabled (SRC_ALPHA, ONE_MINUS_SRC_ALPHA)
- `nt_shape_set_blend(false)` — opaque (default)
- Game controls draw order for correct transparency (opaque first, transparent second)
- 4 internal pipelines total: depth × blend combinations
- All share same shader, differ only in state flags

### Pipeline switch auto-flush
- Changing pipeline state (set_depth, set_blend) auto-flushes current batch if buffer not empty
- Game doesn't need to manually flush before switching — handled internally
- Prevents mixing vertices from different pipeline states in same draw call

### Z-fighting prevention
- Fill pipeline uses glPolygonOffset(1.0, 1.0) to push filled geometry slightly back
- Wireframe draws at exact depth — always wins over fill
- Internal detail, no API exposure

### Tessellation — Unreal style
- `set_segments(N)` — one setter for all rounded shapes
- Circle: N segments around circumference
- Cylinder: N segments around circumference
- Sphere: N segments (longitude) × N/2 rings (latitude) — equal angular density
- Capsule: same as sphere for hemisphere caps
- Default: 32 segments (sphere = 32×16)

### Buffer strategy — preallocated with index buffer
- `#define NT_SHAPE_MAX_VERTICES 65536` — compile-time limit
- `#define NT_SHAPE_MAX_INDICES 131072` — compile-time limit
- uint16 indices — intentional choice: 65535 vertex limit per batch is optimal with auto-flush
- uint16 saves 50% on index buffer vs uint32
- One VBO + one IBO, both dynamic (NT_USAGE_STREAM)
- Index buffer enables vertex sharing (cube: 8 unique verts + 36 indices vs 36 duplicate verts)
- Auto-flush when either buffer is full — flush current batch, reset, continue drawing
- No shapes lost on overflow, just extra draw calls
- Assert at init: NT_SHAPE_MAX_VERTICES <= 65535 (uint16 index range)
- Game can override #define before include to resize

### Shapes — 8 types (6 original + cylinder + capsule)
- Line: billboard quad, always "wireframe" style
- Rect: fill + wireframe, center-based, needs rotation
- Circle: fill + wireframe, CPU tessellation, needs rotation (flat disk orientation)
- Triangle: fill + wireframe, defined by 3 points, no rotation
- Cube: fill + wireframe, center-based, needs rotation
- Sphere: fill + wireframe, UV tessellation, no rotation (symmetric)
- Cylinder: fill + wireframe, flat caps, needs rotation
- Capsule: fill + wireframe, hemisphere caps, needs rotation

### Mesh drawing
- `nt_shape_mesh(vertices, indices, count, color)` + `_wire` variant
- Draw arbitrary geometry (navmesh, collision mesh visualization)
- Added alongside primitive shapes

### Fill + wireframe combo
- No special combo function — call fill then wire separately
- Both go into same batch, no extra cost
- glPolygonOffset prevents z-fighting

### Claude's Discretion
- Per-vertex color API design (uniform color helpers vs gradient variants)
- Shader source (static const char[] embedded in module)
- Internal vertex struct layout
- Billboard quad expansion direction computation
- Camera position extraction from VP matrix
- Default line width value
- Exact tessellation algorithms (UV sphere, circle fan)
- Module file organization within engine/
- CMakeLists.txt specifics
- glPolygonOffset exact values
- Pipeline creation details

</decisions>

<specifics>
## Specific Ideas

- "Renderer is dumb — just draws what it's given. Game controls order and state"
- Debug renderer for physics visualization — colliders, contact points, navmesh
- API inspired by Unreal's DrawDebug* functions (per-call rotation, single segments param)
- Fill + wireframe via two calls, not a special function
- uint16 indices are an optimization, not a limitation — auto-flush means 65535 is enough

</specifics>

<code_context>
## Existing Code Insights

### Reusable Assets
- `nt_gfx.h`: Full renderer API — shaders, pipelines, buffers, draw calls, uniforms
- `nt_gfx_update_buffer()`: Dynamic buffer upload for per-frame VBO/IBO data
- `nt_gfx_make_pipeline()`: Pipeline creation with depth, blend, cull, vertex layout
- `nt_gfx_draw()` / `nt_gfx_draw_indexed()`: Draw calls (non-indexed and indexed)
- `nt_gfx_set_uniform_mat4()`: Set VP matrix uniform
- Fixed attribute locations: NT_ATTR_POSITION=0, NT_ATTR_COLOR=2
- NT_FORMAT_FLOAT3 (position), NT_FORMAT_FLOAT4 or NT_FORMAT_UBYTE4N (color)
- NT_USAGE_STREAM for per-frame dynamic buffers
- cglm (vendored): vec3, vec4, versor (quaternion), mat4 types and math

### Established Patterns
- Swappable backend: shared .c + per-platform implementations
- Global extern struct: `g_nt_` prefix (g_nt_app, g_nt_window, g_nt_input, g_nt_gfx)
- File-scope statics (`s_`) for internal module state
- nt_add_module() CMake helper
- Phase 15 decision: "No built-in shaders in nt_gfx — shape renderer embeds its own as static const char[]"

### Integration Points
- nt_shape uses nt_gfx for pipeline, buffer, shader, draw calls
- nt_shape reads VP matrix from game (set_vp), not from any global
- Demo (Phase 17) uses nt_shape for spinning cube visualization
- nt_shape links to nt_core (types, platform defines) and nt_gfx

</code_context>

<deferred>
## Deferred Ideas

Migrated to GitHub issues — see label `deferred`.

</deferred>

---

*Phase: 16-shape-renderer*
*Context gathered: 2026-03-12*
