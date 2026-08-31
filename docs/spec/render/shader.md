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
a broken one at build time. That validation needs a GL context: a headless build
host logs a skip and packs the stage unchecked, so the runtime compile stays the
backstop.

A program's linked executable and identity are immutable after
`nt_gfx_make_program`; uniform values and block bindings remain mutable.
Recovery requires the owner to destroy the old program and link a new handle;
a program whose readiness was lost never becomes ready again.

The program has a single owner — whoever called `nt_gfx_make_program` — and the
engine never dedupes: two calls with the same pair give two programs. A game
that wants one program behind many materials links it once and passes the same
handle to each, via `nt_material_set_program`. Materials, pipelines and pipeline
caches all borrow the handle and never destroy it.

A program linked from pack-loaded stages needs a per-frame gate, because the
stages arrive asynchronously and nothing can link before both resolve.
`nt_program_ref_t` (`material/nt_program_ref.h`) is that gate: the game gives it
the two resource handles once, calls `nt_program_ref_update` every frame, and
assigns on the frame it returns true. It stores the resource handles rather than
the compiled stages or the source text, because only the handles survive a
context loss -- `nt_program_ref_drop` clears the program and the same gate links
again once the stages re-activate. A shader embedded as a source string needs
none of this: compile and link at init, with nothing to wait for.

Pack priority does not reach a material's program. A material stores a linked
`nt_program_t`, not the `NT_ASSET_SHADER_CODE` stages behind it, so a
higher-priority pack republishing a stage changes only what `nt_resource_get`
returns: nothing relinks, and no material changes. A game that wants the new
stage links a second program and assigns it with `nt_material_set_program` --
a supported flat replace, and the only runtime shader replacement there is.
Pipeline cache keys include the program handle. Destroying the old program frees
its pipelines; renderers remove dead records during insertion after a cache miss
or on reset. Staged work retains its original pipeline and is discarded if that
pipeline is destroyed. Numeric material params remain mutable and are read at
flush; snapshot timing is specified in
[API contracts](../core/api-contracts.md#program-handles).

Uniform block bindings are program state, not material state: a program is
shared by many materials, so a material-declared binding would be
last-writer-wins across them. The engine keeps one global name -> slot registry
instead, and `nt_gfx_register_global_block` applies it to existing and future
programs that declare the block. The registry borrows each name without copying;
the string must remain valid and unchanged until `nt_gfx_shutdown`. Registrations
survive context loss. The buffer varies per draw via `nt_gfx_bind_uniform_buffer`.

The GL backend caches at most 16 active standalone uniform locations per
program. Each active array element consumes one entry; uniforms in blocks do
not consume entries. Exceeding the cache capacity asserts at link time instead
of silently omitting values. Uniform setters use complete names, including
explicit array indices such as `colors[1]` or `lights[0].color`. Reflection reads
the complete reported names and uses temporary storage only while linking;
setting a uniform performs no allocation.

A reflection query that reports nothing discards the new program before
publication, so the next frame links again rather than caching half a location
table. Nothing catches an exception thrown out of reflection: on the web the
Emscripten GL layer dereferences a null result in two of its own reflection
helpers, but the flag such a guard would have to test (`isContextLost`) is set a
task later than the throw, so the guard would rethrow anyway and only ever fire
for a synchronous `WEBGL_lose_context.loseContext()`.

A link failure is a developer error and traps (`NT_ASSERT`) rather than
returning an invalid handle. `nt_gfx_make_program` returns an invalid handle on
a lost context, including pending engine recovery after the browser has restored
it, and for a live stage handle whose GPU object an earlier loss discarded --
that stage is permanently unready, so the owner recreates it and links again.
A stale stage handle is a developer error and still traps. Because the builder validates each stage
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
