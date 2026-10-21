# Material System

Material = shader + render state + values. Numeric params are the bytes of the
program's `Material` uniform block, copied to the frame at each apply;
one material lives in one pool slot and is shared by handle — per-entity
variation goes through components, not material mutation. Sort order is
game-controlled, not a material property. Pack-loadable material assets are
planned but not yet implemented.

Related: [Shader System](shader.md), [Render Components](render-components.md), [Items, Sorting, Batching](items-sorting-batching.md)

## MaterialAsset purpose

`nt_material_init(desc)` initializes the material pool and returns `void`.
The descriptor must be non-NULL with positive `max_materials`. Invalid arguments,
double initialization and allocation failure assert; there is no recoverable
initialization result.

Material = shader + render state + values.

## Params

A material's numeric params are the bytes of the program's `Material` uniform
block ([layout rules](../core/api-contracts.md#program-handles)). The game mirrors
the block with a C struct and passes it as
`nt_material_create_desc_t.params` / `params_size`: NULL with size 0, or a
multiple of 16 up to `NT_MATERIAL_MAX_PARAM_BYTES` (256), asserted at create.
There are no param names: the block layout is the contract between the shader
and the struct.

A material keeps its params as CPU bytes and owns no GPU state.
`nt_material_set_params(mat, data, size)` replaces them (`size` must equal the
create-time `params_size`). `nt_material_apply` copies them into the uniform
frame stream, so a draw reads the bytes of its apply and a write reaches the
draws applied after it. Values that differ per object still belong in instance
data or a `Draw` block: one material per object costs a block per apply.

The stream copy follows `nt_gfx_bind_uniform_block`: bytes equal to the last
`Material` block of the frame copy nothing, other bytes cost `params_size` plus
up to 256 bytes of alignment. Budget `frame_capacity[NT_GFX_FRAME_UNIFORM]` for
the applies of params materials in all passes of a frame; materials sorted by
material copy once per run, interleaved ones once per apply. A context loss
leaves nothing to restore.

## Textures and draw textures

A material declares the samplers it owns by name: `textures[i] = {name, resource,
sampler}`, at most `NT_MATERIAL_MAX_TEXTURES` (4). The name is the sampler uniform;
`nt_material_create` asserts a non-NULL name and keeps its `nt_hash32_str` hash.
The slot index is not the texture unit: gfx resolves the name through immutable
program reflection, and two materials may list the same samplers in any order. Two
slots naming one sampler assert at create. A declared name the program does not
sample is ignored before its handle is inspected, so one material can over-declare
for a family of shaders. Normally each active slot must resolve to a texture, so
register a placeholder with `nt_resource_set_placeholder_texture` for slots that
load asynchronously.

A texture whose value is the draw's, not the material's, is a draw texture: the
caller passes it to `nt_material_apply` next to the material's declarations, and
the material never declares it. Each renderer owns one fixed name and always uses
the texture's default sampler (filter is a property of the texture): the sprite
renderer's atlas page is `u_texture`, the text renderer's font curve texture is
`u_curve_texture`, and the skinned mesh draws bind the deformation texture as
`u_skin_matrices` ([Mesh draws](architecture.md#mesh-draws)). A game drawing its own
pass with a material passes its per-draw textures (a render target, for instance)
the same way. A texture shared by a frame (a shadow map) stays a material slot on a
published resource.

## Apply

`nt_material_apply(mat, draw_textures, count)` binds a material for the next draws,
after `nt_gfx_bind_pipeline` in a pass. It binds the params at
`NT_GFX_SLOT_MATERIAL` (a material without params unbinds that slot, so a program
that reads `Material` asserts at its next draw instead of reading another
material's values) and applies the complete texture set: the material's textures,
resolved with `nt_resource_get` at the call, followed by the draw textures
(borrowed for the call; NULL iff `count` is 0; the total fits the program's sampler
units). gfx validates the set and drops what did not change, so applying an equal
material again ends `CACHE`. Every renderer applies through this one call.

## MaterialAsset binary layout

> **Status:** materials today are created at runtime from
> `nt_material_create_desc_t` (see `engine/material/nt_material.h`). There is
> no `NT_ASSET_MATERIAL` activator and no pack-loadable material format yet.
> The layout below describes the planned on-disk shape once material assets
> become pack-loadable. `ShaderAssetRef` and `TextureAssetRef` are also
> planned types. A material does not reference shaders: it stores a borrowed
> `nt_program_t` that the game links from `NT_ASSET_SHADER_CODE` stages or from
> embedded sources. `nt_material_set_program` replaces it, including assignment
> from or to `NT_PROGRAM_INVALID`. Assigning the same handle is a no-op, so a
> per-frame gate needs no assignment latch. The material module never links,
> destroys, or inspects the program.
>
> A material has no readiness field or version. Use
> `nt_gfx_program_ready(info->program)` before drawing with it: it is false
> before assignment, while the program links, after context loss is processed,
> or after program destruction. The material survives recovery and retains its old program
> handle until reassignment.
>
> Pipeline identity includes the program handle. Destroying the replaced
> program frees its pipelines; it happens before the first pass or after
> `nt_gfx_end_frame` (the frame rule in
> [render architecture](architecture.md#draw-phase-command-stream)). For staged-work behavior, see
> [Program handles](../core/api-contracts.md#program-handles).

```c
// In-memory header (NOT a C struct with FAM) — PLANNED, not yet implemented
typedef struct MaterialAssetHeader {
    ShaderAssetRef vertex_shader;
    ShaderAssetRef fragment_shader;

    nt_blend_state_t blend;
    bool depth_test;
    bool depth_write;
    CullMode cull_mode;

    uint16_t params_size; /* bytes of the `Material` block, a multiple of 16 */
    uint16_t texture_count;
} MaterialAssetHeader;
```

    Binary layout in pack :

```text
┌─────────────────────────┐
│ MaterialAssetHeader      │
├─────────────────────────┤
│ uint8_t params[params_size] │
├─────────────────────────┤
│ TextureAssetRef          │
│   textures[texture_count]│
└─────────────────────────┘
```

At runtime, params and textures are accessed via computed offset from header pointer
(the params are the std140 bytes of the `Material` block, bound as they are):

```c
const uint8_t *material_get_params(const MaterialAssetHeader *h) { return (const uint8_t *)h + sizeof(MaterialAssetHeader); }

const TextureAssetRef *material_get_textures(const MaterialAssetHeader *h) { return (const TextureAssetRef *)(material_get_params(h) + h->params_size); }
```

**Note:** C does not allow two flexible array members in one struct. The layout above uses computed offsets instead.

## One material, one copy

No duplicated material data. Material is created once (either from code via descriptor or loaded from pack asset in the future) and lives in a single pool slot. Multiple entities reference the same material handle.

Per-entity variation (e.g. per-character color, dissolve progress) goes through instance data (a game instance layout, see [render architecture, Mesh draws](architecture.md#mesh-draws)) or a `Draw` block, not material mutation — the material stays shared.

Material-wide params (e.g. a global alpha cutoff or a fog color) can be replaced at runtime with `nt_material_set_params`. This changes the values for every entity sharing the material, for the draws applied after the write ([Params](#params)).

## Attr map

`attr_map` names custom per-vertex attributes of the sprite path (UI widgets,
sprite custom-attr blocks). A mesh feeds every stream at the location its pack
carries ([shader, Attribute locations](shader.md#attribute-locations)), so a
mesh or skinned mesh material declares no `attr_map`; the mesh renderer asserts
that the material has no `attr_map` when a run changes material.

## Attr defaults

`has_attr_defaults` opts a material into per-attr defaults:
`attr_map[i].default_value` is the vec4 a sprite emit without its own custom
block bakes for attr *i* ([Sprite custom-attr block](items-sorting-batching.md#sprite-custom-attr-block)).
Zero-init means no defaults. What the values mean is the shader's contract. Defaults are values: they
never enter pipeline or vertex-input keys. Only the sprite renderer reads them;
the mesh renderer ignores them.

## Texture resolve

A material stores the declared `nt_resource_t` for each texture slot and never a
resolved handle; `nt_material_apply` resolves them with `nt_resource_get` at each
call. A sprite or text draw waits for its own draw texture: a sprite whose atlas
page does not resolve skips its emit or run whatever its shader samples (the page is
the renderer's draw texture, not a material slot), while an unresolved material slot
the program samples asserts (the placeholder contract above). There is no material
step.

Between `nt_resource_init` and `nt_resource_shutdown`, the published view of a
handle changes only inside `nt_resource_step`. `nt_resource_shutdown` unpublishes
every slot: a material that outlives the resource module resolves to 0 and trips
the gfx coverage assert. A frame that steps resources before it emits or draws sees one set of
handles across every renderer, and a material created mid-frame binds its
textures on its first draw. Stepping resources between submissions within one
frame is unsupported: commands already open keep the handles they resolved.

Publication is not GPU lifetime. `nt_resource_invalidate` and
`nt_resource_unmount` destroy the GPU objects of file-pack assets immediately
(virtual-pack handles stay game-owned), so they run outside a drawn frame (the
frame rule), while the slot keeps publishing the dead
handle until the next `nt_resource_step`. A game that invalidates textures —
after a context restore, for instance — steps resources before it emits or
draws, or skips that frame.

## Render state and material

Material stores render state (blend state, depth test/write, cull mode) because it is a property of the surface, not the pass. Pipeline (GPU state object) is derived from material render state at render time; the mesh vertex layout is baked into a separately bound vertex-input object (see render/architecture.md).

`nt_blend_state_t` is the public, backend-neutral fixed-function blend state. It
contains separate source and destination factors and operations for RGB and
alpha, plus the constant blend color. Constant color components must be finite
and in `[0, 1]`. A zero-filled state disables blending. The complete WebGL 2
core factor and operation sets are public. `nt_material_create` asserts the
render-state ranges up front (blend factors and ops, enabled or not,
`cull_mode`, every `attr_map` location below `NT_GFX_MAX_VERTEX_ATTRS`): the
pipeline identity packs the render state, and the sprite renderer's
vertex-input cache key packs the `attr_map` locations, into fixed bit lanes, so
the trap belongs at the API boundary, not on the first draw. Invalid WebGL combinations assert
during pipeline creation.

The fragment shader output is the source (`src`); the color already stored in
the pass target is the destination (`dst`). For each channel, the enabled blend
state computes:

```text
result = operation(src * src_factor, dst * dst_factor)
```

RGB and alpha use their own factors and operations. `MIN` and `MAX` follow the
graphics API rule and ignore factors. The pass does not reinterpret the state
based on its target.

Common states are returned by functions, so callers can use a preset directly
or edit the returned struct before material creation:

| Function | RGB result | Alpha result |
| --- | --- | --- |
| `nt_blend_opaque()` | blending disabled | source replaces destination |
| `nt_blend_alpha()` | `src.rgb * src.a + dst.rgb * (1 - src.a)` | source-over |
| `nt_blend_alpha_premultiplied()` | `src.rgb + dst.rgb * (1 - src.a)` | source-over |
| `nt_blend_additive()` | `src.rgb * src.a + dst.rgb` | destination preserved |
| `nt_blend_additive_premultiplied()` | `src.rgb + dst.rgb` | destination preserved |
| `nt_blend_subtractive()` | `dst.rgb - src.rgb * src.a` | destination preserved |
| `nt_blend_subtractive_premultiplied()` | `dst.rgb - src.rgb` | destination preserved |
| `nt_blend_multiply()` | `dst.rgb * src.rgb` | destination preserved |

Straight and premultiplied presets are separate because the blend unit cannot
infer how the shader encoded `src.rgb`. A premultiplied shader must output RGB
already multiplied by source alpha; a straight shader must not. Multiply uses
RGB as a multiplier instead: white is a no-op and black fully darkens. To shape
it by coverage, the fragment shader outputs
`mix(vec3(1.0), tint, coverage)` as RGB.

Blend state is immutable after `nt_material_create`. A game that switches blend
state creates separate materials sharing the same shader and texture resource
handles, then selects the required material. Params remain mutable through
`nt_material_set_params`.

Sort order is **not** a material property. Sorting is game-controlled: game code gets entities by tag, sorts them (by material for opaques, by depth for transparents, or any custom order), and submits draw items to the renderer in that order. The renderer draws in submission order and batches consecutive compatible items. See [Sorting Policy](items-sorting-batching.md).
