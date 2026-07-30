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
    uint32_t batch_key; // 4 bytes — state compatibility (same material+mesh = same key)
} nt_render_item_t;     // 16 bytes, naturally aligned
```

**batch_key vs sort_key:** sort_key controls draw order (can be anything: material, depth, layer). batch_key controls instancing compatibility (same material+mesh). These are independent — depth-sorted items still batch by material+mesh.

**Why no inline world_matrix:** Instance packing reads world_matrix + color from component arrays via entity lookup (scattered access). Inlining them in the render item (96B) would make packing sequential, and a wider element costs proportionally more per sort pass. `nt_sort_by_key` is already the typed radix sort (`engine/render/nt_render_items.h`), so the remaining lever at 10K+ is an indirect sort over indices.


### Sort key meaning

Sort key determines item order in the final draw sequence for a pass. It is pass-dependent. There is not one universal sort key layout for all passes.

**Material-sorted pass:** sort by material/state/pipeline/texture.

**Depth-sorted pass:** sort by depth first, then other fields as tie-break.

### Batch key / run detection

`batch_key` encodes state compatibility (same material+mesh = same key). `sort_key` controls draw order. These are independent concerns — sort order can be anything (material, depth, layer) without affecting batch detection.

Game fills `batch_key` via `nt_batch_key(material_id, mesh_id)`. Renderer compares consecutive batch_keys to detect instancing runs:

```c
while (run_end < count && items[run_end].batch_key == items[run_start].batch_key) run_end++;
```

## Sorting Policy

### Sort policy is pass-controlled

The game decides sort mode for each pass. Typical modes: sort by material/state, sort by depth, no sort, custom order + tie-break.

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
is a compatibility hint from the game, while SpriteRenderer verifies actual
atlas page textures and splits commands when a run crosses pages.

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
