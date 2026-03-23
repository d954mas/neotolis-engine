# Phase 33: Compact Instance Data - Discussion Log

> **Audit trail only.** Do not use as input to planning, research, or execution agents.
> Decisions are captured in CONTEXT.md -- this log preserves the alternatives considered.

**Date:** 2026-03-23
**Phase:** 33-compact-instance-data
**Areas discussed:** Color mode API, Shader strategy, Shape renderer scope, Instance struct design

---

## Color mode API

### When is color mode set?

| Option | Description | Selected |
|--------|-------------|----------|
| Creation-time only | Immutable, set in nt_material_create_desc_t | |
| Mutable at runtime | nt_material_set_color_mode() | |

**User's choice:** Initially discussed mutable (architecture supports it), then settled on **immutable** after analyzing consistency with other pipeline-affecting properties (blend, depth, cull) and Vulkan/WebGPU forward compatibility.
**Notes:** User identified that changing color_mode requires shader change (different pipeline). Discussed at length whether mutable is safe -- concluded it IS safe architecturally (stream buffer + lazy cache) but inconsistent with existing immutable pattern and problematic for future Vulkan/WebGPU backends.

### Default color mode

| Option | Description | Selected |
|--------|-------------|----------|
| FLOAT4 | Backward compatible, 64 bytes | |
| RGBA8 | Compromise, 52 bytes | |
| NONE | Minimum size, 48 bytes | ✓ |

**User's choice:** NONE
**Notes:** User asked "who needs color uses it explicitly" -- matches code-first philosophy. Sponza doesn't use per-instance color, so NONE is the natural default.

### Color storage location

| Option | Description | Selected |
|--------|-------------|----------|
| Keep in drawable_comp | Always stores float[4], renderer reads conditionally | ✓ |
| Separate nt_color_comp | Opt-in component, saves RAM | |

**User's choice:** Keep in drawable_comp
**Notes:** User raised concern about entity hierarchy and color recalculation. Concluded: drawable comp stores color on CPU (cheap), game handles hierarchy recalc, renderer just reads the final value. Moving to separate component would add hot-path lookup cost.

### Enum design

| Option | Description | Selected |
|--------|-------------|----------|
| nt_color_mode_t (separate) | NONE, RGBA8, FLOAT4 -- prevents invalid values | ✓ |
| nt_vertex_format_t (reuse) | Flexible but allows nonsensical values for color | |

**User's choice:** nt_color_mode_t
**Notes:** User asked about reusing vertex formats. Analysis showed only 3 of 15 formats valid for color. Separate enum prevents invalid values.

### Enum naming

| Option | Description | Selected |
|--------|-------------|----------|
| NT_COLOR_MODE_* | Matches NT_<CATEGORY>_<VALUE> convention | ✓ |
| NT_INSTANCE_COLOR_* | Explicitly tied to instance data | |

**User's choice:** NT_COLOR_MODE_*

### Enum placement

| Option | Description | Selected |
|--------|-------------|----------|
| nt_render_defs.h | Next to nt_mesh_instance_t | ✓ |
| nt_material.h | Next to material descriptor | |

**User's choice:** nt_render_defs.h

---

## Shader strategy

### Shader adaptation approach

| Option | Description | Selected |
|--------|-------------|----------|
| GLSL default + glVertexAttrib4f | One shader, pipeline manages attributes | ✓ |
| Preprocessor variants | #ifdef in shaders | |
| Separate shaders | Different .vert per mode | |

**User's choice:** GLSL default + glVertexAttrib4f(7, 1,1,1,1)
**Notes:** User initially confused about GLSL default behavior. Clarified: unbound attribute reads generic value. Default is (0,0,0,1) = black, so glVertexAttrib4f sets white (1,1,1,1) = identity for multiplication. User confirmed this gives zero-overhead mode switching.

### Pipeline cache key

| Option | Description | Selected |
|--------|-------------|----------|
| color_mode in state_bits | 2 bits (8-9), simple | ✓ |
| Instance layout hash | Separate multiplier in key | |

**User's choice:** state_bits
**Notes:** User asked what's currently in the key and if color fits. Confirmed: state_bits is uint32, only 8 bits used, 24 free.

### Vulkan/WebGPU impact

**User's question:** Does future Vulkan/WebGPU change anything?
**Conclusion:** Immutable pipeline properties are the right approach. Vulkan/WebGPU pipelines are immutable GPU objects -- mutable properties cause expensive recompilation. Current immutable design is forward-compatible.

---

## Shape renderer scope

| Option | Description | Selected |
|--------|-------------|----------|
| Not in scope | Already compact (44B + 28B) | ✓ |
| Unify with mesh renderer | Add color_mode to shapes | |

**User's choice:** Not in scope
**Notes:** User noted shapes are debug-only. Asked about RGBA4 (2B) -- not supported by GL vertex attributes (minimum byte-sized). Shape renderer already at minimum (RGBA8).

---

## Instance struct design

### Data organization

| Option | Description | Selected |
|--------|-------------|----------|
| Byte buffer + stride | uint8_t[], manual packing, real bandwidth savings | ✓ |
| Max-size struct (64B) | Simple but NONE doesn't save bandwidth | |
| Three separate structs | Type safety but switch everywhere | |

**User's choice:** Byte buffer + stride
**Notes:** User asked about overhead -- clarified that max-size struct defeats the purpose (always uploads 64B). Byte buffer enables real savings.

### Buffer sizing

| Option | Description | Selected |
|--------|-------------|----------|
| max_instances * 64 (max stride) | User specifies count, buffer sized at worst case | ✓ |
| max_instances * 48 (min stride) | Smaller but FLOAT4 fits fewer instances | |

**User's choice:** max_instances * 64
**Notes:** User prefers thinking in instance count at max size. NONE/RGBA8 modes get bonus capacity (more instances fit in same memory).

---

## Claude's Discretion

- Instance layout selection logic (switch on color_mode)
- pack_rgba8 helper implementation
- Pipeline desc construction details
- Test structure and coverage
- glVertexAttrib4f call placement

## Deferred Ideas

- Mutable pipeline-affecting material properties (blend, depth, cull, color_mode)
- Render state separated from material for Vulkan/WebGPU
- HALF4 color mode (8 bytes, HDR)
- Shape renderer RGBA4 (GL limitation: min byte-sized vertex attributes)
