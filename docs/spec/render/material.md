# Material System

Material = shader + render state + values. Numeric params are always `vec4[]`;
one material lives in one pool slot and is shared by handle — per-entity
variation goes through components, not material mutation. Sort order is
game-controlled, not a material property. Pack-loadable material assets are
planned but not yet implemented.

Related: [Shader System](shader.md), [Render Components](render-components.md), [Items, Sorting, Batching](items-sorting-batching.md)

## MaterialAsset purpose

Material = shader + render state + values.

## Numeric params policy

All numeric material params stored as `vec4[]`. This is intentional.

Rule: float uses `.x`, vec2 uses `.xy`, vec3 uses `.xyz`, vec4 uses `.xyzw`.

Benefits: simple layout, simple alignment, easy future GPU block packing, no per-type runtime complexity.

## MaterialAsset binary layout

> **Status:** materials today are created at runtime from
> `nt_material_create_desc_t` (see `engine/material/nt_material.h`). There is
> no `NT_ASSET_MATERIAL` activator and no pack-loadable material format yet.
> The layout below describes the planned on-disk shape once material assets
> become pack-loadable. `ShaderAssetRef` and `TextureAssetRef` are also
> planned types. A material does not reference shaders: it stores a borrowed
> `nt_program_t` that the game links from `NT_ASSET_SHADER_CODE` stages or from
> embedded sources. `nt_material_set_program` is the only way to change it, and
> a material is `ready` once a program is assigned — which says nothing about
> that program's GPU liveness (see [Shader System](shader.md)). The material
> module never links, destroys, or inspects the program.
>
> The borrowed handle moves only between renderer cache epochs:
> `NT_PROGRAM_INVALID` to a program is free, a program back to
> `NT_PROGRAM_INVALID` requires the renderers to have been reset first, and
> program A straight to program B asserts. The transition table and the reason
> live in [API Contracts](../core/api-contracts.md).

```c
// In-memory header (NOT a C struct with FAM) — PLANNED, not yet implemented
typedef struct MaterialAssetHeader {
    ShaderAssetRef vertex_shader;
    ShaderAssetRef fragment_shader;

    nt_blend_state_t blend;
    bool depth_test;
    bool depth_write;
    CullMode cull_mode;

    uint16_t param_count;
    uint16_t texture_count;
} MaterialAssetHeader;
```

    Binary layout in pack :

```text
┌─────────────────────────┐
│ MaterialAssetHeader      │
├─────────────────────────┤
│ vec4 params[param_count] │
├─────────────────────────┤
│ TextureAssetRef          │
│   textures[texture_count]│
└─────────────────────────┘
```

At runtime, params and textures are accessed via computed offset from header pointer:

```c
const vec4 *material_get_params(const MaterialAssetHeader *h) {
    return (const vec4 *)((const uint8_t *)h + sizeof(MaterialAssetHeader));
}

const TextureAssetRef *material_get_textures(const MaterialAssetHeader *h) { return (const TextureAssetRef *)(material_get_params(h) + h->param_count); }
```

**Note:** C does not allow two flexible array members in one struct. The layout above uses computed offsets instead.

## One material, one copy

No duplicated material data. Material is created once (either from code via descriptor or loaded from pack asset in the future) and lives in a single pool slot. Multiple entities reference the same material handle.

Per-entity variation (e.g. per-character color, dissolve progress) goes through entity param components, not material mutation — each entity carries its own values, the material stays shared.

Material-wide params (e.g. global alpha cutoff, roughness) can be mutated at runtime via `nt_material_set_param` / `nt_material_set_param_component`. This changes the value for all entities sharing that material. The renderer re-reads params every frame; no version bump is needed. Hash-based overloads (`_h` suffix) accept a pre-computed `nt_hash32_t` to avoid per-frame string hashing.

## Render state and material

Material stores render state (blend state, depth test/write, cull mode) because it is a property of the surface, not the pass. Pipeline (GPU state object) is derived from material render state + mesh vertex layout at render time.

`nt_blend_state_t` is the public, backend-neutral fixed-function blend state. It
contains separate source and destination factors and operations for RGB and
alpha, plus the constant blend color. Constant color components must be finite
and in `[0, 1]`. A zero-filled state disables blending. The complete WebGL 2
core factor and operation sets are public; invalid WebGL combinations assert
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
handles, then selects the required material. Numeric material params remain
mutable through the existing setters.

Sort order is **not** a material property. Sorting is game-controlled: game code gets entities by tag, sorts them (by material for opaques, by depth for transparents, or any custom order), and submits draw items to the renderer in that order. The renderer draws in submission order and batches consecutive compatible items. See [Sorting Policy](items-sorting-batching.md).
