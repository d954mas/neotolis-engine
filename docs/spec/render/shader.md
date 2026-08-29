# Shader System

ShaderAsset defines the interface, not values: vertex input mask, material
vec4/texture counts, object/global usage masks, and default render state.
Shader data comes in four levels — vertex inputs, material params, object
params, and globals.

Related: [Material System](material.md), [Runtime Formats](../assets/runtime-formats.md), [Rendering Architecture](architecture.md)

## Runtime objects: ShaderCode, Program

`NT_ASSET_SHADER_CODE` is the text of ONE stage; there is no program asset.
`nt_gfx_make_shader` compiles a stage and `nt_gfx_make_program(vs, fs)` links
a pair, from either an embedded source string or two resolved resources. The
shader resource exists so the builder can compile each stage offline and reject
a broken one at build time.

The program has a single owner — whoever called `nt_gfx_make_program` — and the
engine never dedupes: two calls with the same pair give two programs. A game
that wants one program behind many materials links it once and passes the same
handle to each, via `nt_material_set_program`. Materials, pipelines and pipeline
caches all borrow the handle and never destroy it.

Uniform block bindings are program state, not material state: a program is
shared by many materials, so a material-declared binding would be
last-writer-wins across them. The engine keeps one global name -> slot registry
instead, and `nt_gfx_register_global_block` applies it to every program,
existing and future. A block declared by a single shader needs no special case
-- the bind skips programs that do not declare it. What varies per draw is the
buffer, via `nt_gfx_bind_uniform_buffer`.

A link failure is a developer error and traps (`NT_ASSERT`) rather than
returning an invalid handle; the only invalid handle `nt_gfx_make_program`
returns is on a lost context. Because the builder validates each stage
separately and never links a pair, the trap is also where mismatched varyings
and device limits surface — offline linking arrives with `ShaderAsset`.

## ShaderAsset purpose

ShaderAsset defines interface, not values.

## ShaderAsset fields

> **Status:** `ShaderAsset` is planned. Runtime shaders are currently
> `NT_ASSET_SHADER_CODE` blobs; materials provide render state explicitly.

```c
typedef struct ShaderAsset {
    ShaderCodeRef vs;
    ShaderCodeRef fs;

    uint32_t vertex_input_mask;

    uint16_t material_vec4_count;
    uint16_t texture_slot_count;

    uint16_t object_usage_mask;
    uint16_t global_usage_mask;

    nt_blend_state_t default_blend;
    bool default_depth_test;
    bool default_depth_write;
    CullMode default_cull_mode;
} ShaderAsset;
```

## Four levels of shader data

1. **vertex inputs** — from geometry/mesh
2. **material params** — from MaterialAsset
3. **object params** — from RenderState, Transform
4. **globals** — from renderer/pass

## Vertex input mask

Possible semantics: POSITION, NORMAL, UV0, COLOR0. Mesh/shader compatibility validated in builder and sanity-checked at runtime.

## Object params

Fixed object-level params for v0.1: world_matrix, object_color, object_params0.

## Global params

Possible globals: view, proj, view_proj, camera_pos, time, light_dir. Start minimal, expand later.

WebGL 2 Uniform Buffer Objects can be used to share globals efficiently across shaders.

## Fragment output and blending

The fragment shader defines the source color representation; material blend
state defines how the fixed-function blend unit combines it with the target.
They form an explicit contract. Straight-alpha shaders pair with straight
presets, and premultiplied-alpha shaders pair with premultiplied presets. No
renderer converts between the representations.

Multiply is representation-independent with respect to source alpha because its
RGB multiplier carries coverage itself. An alpha-shaped darkening shader uses:

```glsl
vec3 multiplier = mix(vec3(1.0), tint, coverage);
frag_color = vec4(multiplier, 1.0);
```

With `nt_blend_multiply()`, white leaves the destination unchanged, black fully
darkens it, and destination alpha is preserved.
