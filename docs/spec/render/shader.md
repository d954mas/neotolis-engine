# Shader System

ShaderAsset defines the interface, not values: the attribute locations it reads
(see Attribute locations), material vec4/texture counts, object/global usage
masks, and default render state.
Shader data comes in four levels — vertex inputs, material params, object
params, and globals.

Related: [Material System](material.md), [Runtime Formats](../assets/runtime-formats.md), [Rendering Architecture](architecture.md)

## Runtime objects: ShaderCode, Program

`NT_ASSET_SHADER_CODE` is the text of ONE stage; there is no program asset.
`nt_gfx_make_shader` compiles a stage and `nt_gfx_make_program(vs, fs, label)` links
a pair, from either an embedded source string or two resolved resources. The
shader resource exists so the builder can compile each stage offline and reject
a broken one at build time. That validation needs a GL context: a headless build
host logs a skip and packs the stage unchecked, so the runtime compile stays the
backstop.

A program's linked executable and identity are immutable after
`nt_gfx_make_program`, and so are its sampler units and block bindings.
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
binds the uniform blocks, reflects uniforms and samplers and writes the sampler
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
the two resource handles and an optional label once, calls `nt_program_ref_update` every frame, and
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
Pipeline identity includes the program handle. Destroying the old program
(outside a drawn frame, the frame rule) frees its pipelines. Sprite and text draws record with the pipeline resolved at
`set_material`; mesh draws with the pipeline resolved at their call. A program
is destroyed only outside a drawn frame (the frame rule in
[render architecture](architecture.md#draw-phase-command-stream)). Numeric material params
stay mutable: a write reaches the draws applied after it
([material, Params](material.md#params)).

Uniform block bindings are program state, not material state: a program is
shared by many materials, so a material-declared binding would be
last-writer-wins across them. A block binds by name instead: the engine reserves
`Globals`, `Material` and `Draw` (slots 0-2, by update frequency) and the game
lists its own names in `nt_gfx_desc_t.game_blocks` (slots from 3); an unknown
active block name asserts at link. The contract (member types, std140, the draw
check and frame data) is in
[API contracts](../core/api-contracts.md#program-handles).

Every shader value arrives in a uniform block; there are no standalone value
uniforms. An active standalone non-sampler uniform asserts when the link finishes,
because no API could ever write it; GL built-ins such as `gl_DepthRange` are not
program inputs and are ignored. Uniforms in blocks are never looked up by
location. Reflection reads the complete reported names into a fixed link-time
buffer (`NT_GFX_GL_MAX_UNIFORM_NAME`, 256 bytes; a longer name asserts at link);
linking allocates nothing.

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
half a sampler unit table: on a lost context the program stays unready and its
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

A live-context link or reflection failure checks what the driver returned, so it
follows the [failure record](../debug/logging-errors-debugging.md#assert-policy)
rule: the record names the program slot, GL name and label, the phase (`link`,
`blocks` or `uniforms`), the violated contract, and the name and value the driver
returned: the uniform or block, `max_len`, or `gl_error` for a failed link or an
empty query. The link record follows the driver logs, so a journal that
keeps its tail keeps the identity. Context loss keeps its recoverable outcome.

Just before that line, a link failure logs
`program link stages: vs_compile=<v> fs_compile=<f>`: `1` means the stage
compiled, `0` that it did not, `-1` that the status is unavailable because the
owner destroyed the stage and the web backend no longer names it (native GL
keeps an attached stage readable). An empty driver log with `1 1` therefore
points at linking, a `0` at that stage's compilation. A context lost during
these queries can also read as `-1` or `0`; that record still ends in the
assertion. These queries exist only with ERROR logging compiled in and never run
for a successful link or an already-confirmed context loss.

On the web, the browser's translation of a stage is what the driver actually
compiled, and the browser may adapt it to the GPU. A link failure therefore logs
`<stage> translated:` and the text from the optional `WEBGL_debug_shaders`
extension for each live stage, cut to 2 KB, ahead of the driver logs that a
journal keeping its tail should retain. A stage without text logs nothing.
Native GL compiles the packed source as is and logs no translation.

The label is an optional debug name in static storage, like material labels; the
program stores the pointer. NULL or empty labels report `<unnamed>`. Labels do not
affect program identity or readiness.

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

    uint16_t material_block_size; /* bytes of the `Material` uniform block */
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

1. **vertex inputs** — from geometry/mesh, and per-instance data from the instance layouts
2. **`Material` block** — the material's params, bound at apply ([material, Params](material.md#params))
3. **`Draw` block** — per-draw data of a renderer or game shader, bound as frame data
4. **`Globals` and game blocks** — frame and pass data, bound as frame data

## Attribute locations

A mesh feeds each stream at the location its builder stream layout states and the
pack carries ([runtime-formats](../assets/runtime-formats.md#mesh-wire-layout)), and
the mesh renderer's instance layouts feed instances at a fixed instance head, so a mesh
shader declares its inputs at those locations. A shader may
`#include "nt/locations.glsl"`. The builder resolves this include from C
(`shared/include/nt_vertex_locations.h`), not from a file. It expands at every
include under its own `#ifndef` guard, so repeated or conditional includes are
safe. It holds only `#define` constants, never attribute declarations:

- `NT_LOC_*`, the default mesh stream locations: position 0, normal 1, color 2,
  uv0 3, tangent 4, uv1 5, joints 6, weights 7;
- `NT_INST_LOC_*`, the instance head: world rows 8-10 and color 11 for both
  instance layouts, skin frame origins 12 and skin alpha 13 for skinned meshes.

A shader that reads a stream at an explicitly assigned location writes that
number, or its own `#define`, matching the stream layout the mesh was built
with. Shaders that do not include it are unaffected; the engine mesh shaders
(`common/instance.glsl`, `common/skin.glsl` and their users) include it.

## Mesh streams and shader inputs that do not match

A mesh's vertex input enables every stream of the mesh; streams the shader does
not read cost nothing per draw. An input that neither a mesh stream nor the
bound instance layout supplies reads the GL generic default `(0, 0, 0, 1)`.
Nothing checks mesh/shader compatibility: the mesh family and its shaders agree on locations by
construction.

## Object and global data

Per-object values are instance data (world rows and color in the instance head) or
a `Draw` block; frame and view values (`view_proj`, `camera_pos`, `time`) are the
`Globals` block the game fills (`nt_frame_uniforms_t`, `common/globals.glsl`).
Shaders read no standalone numeric uniforms: every value arrives in a uniform
block or a vertex input.

## Draw merge

Vertex and fragment shaders used with non-instanced draws do not read
`gl_PrimitiveID`: gfx joins contiguous draws into one
([draw merge](architecture.md#binding-dedup-and-draw-merge)), and the joined
draw continues the primitive numbering. WebGL2's GLSL ES 3.00 has no
`gl_PrimitiveID`; the rule keeps native GL builds identical. Instanced draws
never merge, so `gl_InstanceID` keeps its per-draw meaning.

## Precision

GLSL ES guarantees `mediump` less range and precision than fp16; the target
Mali GPUs run it as fp16 (max 65504, 11-bit significand) in both stages, and
on such tilers a vertex output's precision sets how many bytes the varying
costs per vertex. Desktop GL and ANGLE on D3D compute everything in fp32, so
desktop pixels never show a precision bug. Shaders that render follow these
rules; builder test fixtures that only compile are exempt:

- Vertex shaders default to `highp`: positions and matrices need fp32.
- Varyings holding colors (UNORM8 sources), tints and unit normals/tangents are
  `mediump`; UVs, world positions and SDF/text coordinates stay `highp`.
- A fragment input may be interpolated at the lower of its two declared
  precisions, so a UV is `highp` on both sides, also in a `mediump` fragment
  shader.
- Fragment shaders may default to `mediump` for color math and keep `highp` for
  values that cancel or need range: view vectors from world positions,
  normal-map Z reconstruction, specular powers, derivative-based AA, text
  coverage.
- A uniform or uniform-block member declared in both stages must have the same
  precision, or the WebGL link fails; a fragment shader that includes
  `common/globals.glsl` keeps the `highp` default of the vertex stage.
- `sampler2D` defaults to `lowp`, and `texture()` returns the sampler's
  precision: a sampler over depth or numeric data is qualified explicitly.

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
