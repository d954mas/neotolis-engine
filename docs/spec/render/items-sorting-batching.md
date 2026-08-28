# Render Items, Sorting, Batching

The CPU-side draw pipeline: game-defined render tags, the 16-byte thin render
item (sort_key + entity + batch_key), pass-controlled sorting policy, and
renderer-specific batching/instancing (sprite CPU batching, mesh instancing).
sort_key controls draw order; batch_key controls instancing compatibility —
independent concerns.

Related: [Rendering Architecture](architecture.md), [Render Components](render-components.md), [Material System](material.md)

## Render Tags

### RenderTag philosophy

Render tags are **game-defined**, not engine-enum-defined. Tags are `nt_hash32_t` values created via `nt_hash32_str()`.

```c
nt_hash32_t TAG_WORLD = nt_hash32_str("world");
nt_hash32_t TAG_UI = nt_hash32_str("ui");
nt_hash32_t TAG_DEBUG = nt_hash32_str("debug");
```

### What tags mean

RenderTag means: pass category, render grouping chosen by game, filter for pass building.

RenderTag does **not** mean: component type, mesh vs sprite vs text, material type.

The renderer backend and low-level render API do not know about tags. Tags are used by game code for filtering, grouping by pass, choosing sort/batch policy.

## Render Items, Sort Keys, Batch Keys

### RenderItem concept

A RenderItem is a **CPU-side prepared draw record**, not a GPU object.

```text
Entity / components
    → RenderItem build
    → sort
    → renderer consumes
    → GPU draw calls
```

### RenderItem model

Minimal render item — sorted draw record, not a fat data carrier. Renderer reads per-entity data (world matrix, color) from components at draw time.

```c
typedef struct nt_render_item_t {
    uint64_t sort_key;  // 8 bytes — encodes material+mesh for opaque, depth for transparent
    uint32_t entity;    // 4 bytes — raw entity id
    uint32_t batch_key; // 4 bytes — renderer-defined compatibility token
} nt_render_item_t;     // 16 bytes, naturally aligned
```

**batch_key vs sort_key:** `sort_key` controls draw order and is pass-defined.
`batch_key` identifies compatibility as defined by the renderer consuming the
item. The two policies are independent: a depth/layer key may control order
while the renderer's token identifies reusable state.

**Why no inline world_matrix:** Instance packing reads world_matrix + color from component arrays via entity lookup (scattered access). Inlining them in the render item (96B) would make packing sequential, and a wider element costs proportionally more per sort pass. `nt_sort_by_key` is already the typed radix sort (`engine/render/nt_render_items.h`), so the remaining lever at 10K+ is an indirect sort over indices.


### Sort key meaning

Sort key determines item order in the final draw sequence for a pass. It is pass-dependent. There is not one universal sort key layout for all passes.

**Material-sorted pass:** sort by material/state/pipeline/texture.

**Depth-sorted pass:** sort by depth first, then other fields as tie-break.

### Batch key / run detection

The game fills `batch_key` through the helper owned by the concrete renderer.
Renderers compare consecutive tokens to detect runs:

```c
while (run_end < count && items[run_end].batch_key == items[run_start].batch_key) run_end++;
```

Equality is authoritative: it allows the renderer to reuse state resolved from
the run leader. Equal tokens for incompatible state violate the caller
contract and may draw with the wrong state. Store renderer-helper tokens
unchanged. To force a boundary between otherwise compatible items, split them
across separate `draw_list()` calls.

`nt_mesh_renderer_batch_key(material, mesh)` packs the two 16-bit pool slot
indices as `material_slot << 16 | mesh_slot`. This is exact for simultaneously
live handles without widening the render item. Generation bits are omitted
because the list has a bounded lifetime: each key is built from the current
material and mesh bindings of that same `item.entity`; until
`nt_mesh_renderer_draw_list()` returns, the entity and required components stay
alive, neither binding changes, and neither referenced live resource is
destroyed or has its slot reused.

`nt_sprite_renderer_batch_key(material, page_resource)` packs the material's
16-bit pool slot and the resolved atlas page resource's 16-bit slot. The page
resource comes from the sprite component's resolved-region cache, so render-item
construction does not search the atlas. The game excludes unresolved sprites and
resolved tombstones before calling the helper; tombstones have no page resource.
The same bounded-lifetime rule applies through
`nt_sprite_renderer_draw_list()`: the material binding and resolved page must not
change. SpriteRenderer still checks the actual page while emitting, so a page
mismatch inside a run splits safely; this does not relax the material-key
contract. Transform and drawable color may change after item construction because
neither renderer's key encodes them.

## Sorting Policy

### Sort policy is pass-controlled

The game decides sort mode for each pass. Typical modes: sort by material/state, sort by depth, no sort, custom order + tie-break.

`nt_sort_by_key(items, count, scratch)` sorts ascending and stably by
`sort_key` only. It deliberately ignores `batch_key`, so equal primary keys
preserve the game's input order.

`nt_sort_by_key_then_batch(items, count, scratch)` is the opt-in stable
lexicographic policy: `sort_key` is primary and `batch_key` is secondary. It is
useful when the primary key deliberately creates coarse ties and reordering
inside a tie is permitted. Do not use it for transparent, UI, or other passes
whose equal-key input order is semantically significant. Both functions use
caller-provided separate scratch storage and allocate no heap memory.

### Depth sorting

Depth is computed on CPU only when needed. Transparent/depth-sensitive passes compute depth. Opaque/material passes may skip depth entirely.

**Do not compute depth for all items by default.**

## Batching and Instancing

### Renderer-specific batching

#### SpriteRenderer

CPU batch: SpriteRenderer consumes consecutive `batch_key` runs, emits dynamic
sprite vertices into a shared staging VBO, records draw commands on state/page
changes, and flushes when staging capacity, uint16 index range, or command
capacity requires it. Rect and polygon sprites share the same generic dynamic
IBO path — see [Sprite batching strategy](#sprite-batching-strategy). Atlas regions still carry `NT_ATLAS_REGION_FLAG_QUAD_*`
metadata for a future GPU-instanced rect renderer (Issue #176); the current
SpriteRenderer ignores those flags.

#### MeshRenderer

No true merging for arbitrary meshes. Same batch key means no state changes. Later add instancing for same mesh/material runs.

### Mesh instancing

Mesh instancing is desired early. Works best when: same mesh, same material, same shader layout, different world/object params only.

WebGL 2 provides native `drawArraysInstanced` / `drawElementsInstanced` — no extension management needed.

### Sprite batching strategy

Sprite renderer: gather sorted sprite render items, resolve component SoA views
once, pack sprite vertices into one dynamic vertex buffer per flush chunk, and
draw recorded commands. The renderer owns atlas page correctness: `batch_key`
is the renderer-defined material compatibility token, while SpriteRenderer
verifies actual atlas page textures and splits commands when a run crosses
pages.

Rect and polygon sprites use the same generic dynamic IBO path. The renderer
does not keep a separate static-quad fast path unless measurements show a clear
win on the target workload; this keeps the sprite batching code small and makes
draw splitting depend only on capacity and state changes.

## UI draw ordering (nt_ui walker)

The UI walker has **three independent ordering axes** — do not conflate them:

1. **zIndex** — the stacking axis. Draw order is zIndex ascending, then layer
   ascending, then declaration order.
2. **Scissor / custom commands** — hard flush barriers. A segment is a run of
   same-zIndex segmentable commands; SCISSOR and CUSTOM cut it, forcing a batch
   flush on each side.
3. **layer** — batch order *within* a segment (256 layers, `uint8_t`; 240-255
   are engine-reserved for debug overlays; bitmask multipass). Layer comes from
   the **widget call** (`data->layer`, `label_layer`), never from a style — so
   a game can batch e.g. all sprites before all text.

Rich text's [per-atom z-layers](../ui/rich-text.md#per-atom-z-layers-explicit-draw-order)
are a *different*, block-internal mechanism (band flushes inside one CUSTOM
command) — they do not interact with the walker's layer axis.

Floating subtrees that must stay inside a scroll clip declare
`clipTo = ATTACHED_PARENT`; a raw floating leaks past the scroll's scissor.
