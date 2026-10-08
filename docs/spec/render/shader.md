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

Linking is asynchronous. `nt_gfx_make_program` starts the link and returns a
valid handle without reading its status: browsers link on a worker thread, and
asking for the status at once blocks the main thread until it finishes.
`nt_gfx_begin_frame` finishes pending links. With
`KHR_parallel_shader_compile` (web) or `KHR`/`ARB_parallel_shader_compile`
(native) it asks each pending program once per frame whether the link is
complete and leaves unfinished ones pending; without the extension it finishes
every pending link there, which may block. Finishing reads the link status,
binds the global blocks, reflects uniforms and samplers and writes the sampler
units. A program is therefore ready no earlier than the frame after
`nt_gfx_make_program`. Readiness turns true only in `nt_gfx_begin_frame` and
turns false only by a destroy or by the loss processing in `nt_gfx_begin_frame`,
so a program never becomes ready inside a frame. Until then
`nt_gfx_program_ready` is false: `nt_gfx_make_pipeline` accepts the program,
binding a pipeline on it asserts, and renderers and games skip draws that
need it.

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
Pipeline cache keys include the program handle. Destroying the old program
(outside a drawn frame, the frame rule) frees its pipelines; renderers remove dead records during insertion after a cache miss
or on reset. Sprite and text draws record with the pipeline resolved at
`set_material`; mesh draws with the pipeline resolved at their call. A program
is destroyed only outside a drawn frame (the frame rule in
[render architecture](architecture.md#draw-phase-command-stream)). Numeric material params
remain mutable and are read at the draw; snapshot timing is specified in
[API contracts](../core/api-contracts.md#program-handles).

Uniform block bindings are program state, not material state: a program is
shared by many materials, so a material-declared binding would be
last-writer-wins across them. The game declares one global name -> slot list
instead, in `nt_gfx_desc_t.global_blocks`, and every program that declares a
listed block gets its slot at link. Names are borrowed without copying; each
string must remain valid and unchanged until `nt_gfx_shutdown`. The list
survives context loss. The data varies per draw: `nt_gfx_bind_uniform_block` copies a
block into the uniform frame stream and binds it to a slot.

The GL backend caches at most 16 active standalone non-sampler uniform locations
per program. Each active array element consumes one entry; uniforms in blocks do
not consume entries, and samplers do not either — they live in a separate table
capped by `NT_GFX_MAX_TEXTURE_SLOTS`. Exceeding either capacity asserts when the link finishes
time instead of silently omitting values. Reflection reads the complete reported names into
a fixed link-time buffer (`NT_GFX_GL_MAX_UNIFORM_NAME`, 256 bytes; a longer name asserts at
link); neither linking nor setting a uniform allocates.

The cache is keyed by the `nt_hash32_str` hash of the uniform's complete name,
including explicit array indices such as `colors[1]` or `lights[0].color`. The
setters take that hash — `nt_gfx_set_uniform_vec4(nt_hash32_t name, …)` and its
three siblings — and there is no string form: a caller hashes the name once at
init, or inline where the cost does not matter.

For active standalone float vec4 uniforms, the backend also remembers the last
16 submitted bytes. Bit-identical repeats skip `glUniform4fv`; the first write,
including zero, always reaches GL. Signed zero and distinct NaN payloads are
compared by representation, without an epsilon. Values belong to the linked
program and survive pipeline switches, passes and frames. Recreated programs
start with invalid cached values. All gfx vec4 writers share this cache; other
uniform types retain their existing setter behavior, including boolean targets
that accept float-vector conversion. Inactive names remain no-ops. Cache lookup
and updates allocate nothing.

Sampler uniforms are program state, not material state: their texture units are
fixed at link and nobody writes them afterwards. Reflection classifies every
active uniform by type — `sampler2D`, `sampler2DShadow`, and `usampler2D` are
supported. `isampler2D` asserts when the link finishes because the engine exposes no signed
integer texture format; the other WebGL2 sampler types (cube, 3D, array, and
their integer forms) assert when the link finishes. The production gfx stub neither compiles
nor inspects shader sources and creates no programs; see
[stub semantics](../core/module-layout.md#stub-semantics-and-capability-queries). Each
sampler element, array elements included, takes one unit, numbered 0..n-1 in
reflection order. A program may not use more than `NT_GFX_MAX_TEXTURE_SLOTS`
sampler units (asserted when the link finishes). The backend writes the units once with
`glUniform1i` immediately after reflection, restoring the program that was
current.
The unit table is backend-private: `nt_gfx_apply_texture_bindings` consumes it to
map a complete name-keyed set to units, and callers never observe or choose unit
numbers. A name absent after driver optimization is inactive and ignored before
its texture or sampler handle is inspected.

A reflection query that reports nothing fails the finish rather than caching
half a location table: on a lost context the program stays unready and its
owner links a new one after the restore; on a live context it traps like a
failed link. Nothing catches an exception thrown out of reflection: on the web
the Emscripten GL layer dereferences a null result in two of its own reflection
helpers. The browser reports a loss through `isContextLost` at once (only the
lost event is queued), and the finish queries it after a failed link, so the
throw needs a loss that lands after a successful link status and before
reflection within one finish. The engine accepts that race rather than wrap
Emscripten's helpers.

A link failure is a developer error and traps (`NT_ASSERT`) in the
`nt_gfx_begin_frame` that finishes the link, after logging the program log
and the logs of its stages that are still alive; stage creation never reads
its compile status, so compile errors surface there too (keep stages until the
program is ready to see them on the web). A link the browser fails because the context
was lost latches the loss instead and leaves the program unready.
`nt_gfx_make_program` returns an invalid handle on
a lost context, including a loss the browser reports before its lost event
arrives (the failed call latches it) and pending engine recovery after the browser has restored it, and for a live stage handle whose GPU object an earlier loss discarded --
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

## Draw merge

Vertex and fragment shaders used with non-instanced draws do not read
`gl_PrimitiveID`: gfx joins contiguous draws into one
([draw merge](architecture.md#binding-dedup-and-draw-merge)), and the joined
draw continues the primitive numbering. WebGL2's GLSL ES 3.00 has no
`gl_PrimitiveID`; the rule keeps native GL builds identical. Instanced draws
never merge, so `gl_InstanceID` keeps its per-draw meaning.

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
