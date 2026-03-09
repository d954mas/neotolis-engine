
# Neotolis Engine — Technical Specification

**Version:** v0.3-consolidated  
**Status:** Architectural baseline + implementation-oriented spec  
**Language target:** C17  
**Primary runtime target:** Web / WASM + WebGL 2  
**Secondary future target:** WebGPU  

This document consolidates all architectural decisions from v0.1 overview, v0.2 technical spec, and subsequent design sessions into a single authoritative reference.

Language baseline is C17 for broader compiler and Emscripten toolchain support.

---

# 1. Core Principles

## 1.1 Design Philosophy

The engine follows these principles:

1. **Code-first architecture**
   - render loop is game code
   - gameplay system order is game code
   - builder rules are code
   - no heavy declarative system

2. **Explicit over implicit**
   - no hidden system scheduler
   - no hidden render graph
   - no automatic ECS magic
   - no hidden asset conversion at runtime

3. **Runtime simplicity**
   - builder does heavy work
   - runtime only loads, validates, resolves, and renders
   - no source-format import in runtime

4. **Data-oriented where useful**
   - sparse component storages
   - dense iteration
   - typed handles/refs
   - sorted draw items

5. **Minimal abstraction**
   - enough abstraction to survive WebGL 2 → WebGPU later
   - not so much abstraction that the engine becomes hard to understand

## 1.2 Code-First Approach

Throughout the entire architecture:

- render pipeline is defined by **game code**
- system order is defined by **game code**
- builder rules are defined by **code**
- engine provides primitives and infrastructure, does not impose a ready-made pipeline

Engine gives low-level and mid-level infrastructure. Game defines concrete logic, passes, sorting, batching policy, and content build pipeline.

## 1.3 Simplicity Over Universality

Key constraints:

- no premature universal abstractions
- no complex runtime reflection system when layouts and indices suffice
- no complex plugin system
- no material graph, editor framework, or scripting at start
- no multi-platform/multi-backend core design upfront

If a decision can be deferred without loss of base architecture — it is deferred.

---

# 2. Scope

## 2.1 Included in baseline

- Web platform
- WASM runtime
- WebGL 2 renderer backend (sole baseline, no WebGL 1 fallback)
- component-based entity architecture
- hierarchy
- transform system
- resource system with async loading
- custom binary pack format (NEOPAK)
- custom runtime formats
- shader + material system
- mesh rendering
- sprite rendering
- text rendering (later-ready)
- fixed update + frame update
- builder in C
- input polling + pointer capture
- render items + sorting + batching policy
- mesh instancing planned
- sprite CPU batching planned
- audio system (platform-agnostic, handle-based)

## 2.2 Explicitly not in scope

- editor
- Lua scripting
- full physics engine
- material graph editor
- plugin architecture
- WebGPU backend implementation
- scene editor / authoring editor
- full UI framework
- hot reload of compiled native/WASM code
- generic reflection-heavy system architecture
- WebGL 1 support

---

# 3. Platform Architecture

## 3.1 Initial platform target

```
WEB / WASM + WebGL 2
```

Meaning:

- application runs in browser
- engine compiled to WASM
- rendering through WebGL 2 backend
- debugging via browser console and overlays

WebGL 2 is the sole baseline. WebGL 1 is not supported. Rationale: WebGL 2 coverage is 95%+ of devices; the remaining 5% cannot run the target content (large 3D worlds) regardless; WebGL 2 gives native instancing, UBO, NPOT textures, gl_VertexID without extension management overhead; cleaner migration path to future WebGPU.

Windows / desktop platform is not required in v0.1 but architecture must allow adding `platform_win32` or other platform backends later. All platform-specific code (window creation, input, audio, etc.) works through engine abstractions.

## 3.2 Platform layer

Platform is a **subsystem/module**, not an ECS component.

```text
platform/
    platform.h
    platform_web.c
    platform_web.h
```

Future optional additions:

```text
platform_win32.c
platform_linux.c
```

## 3.3 Platform responsibilities

Platform module handles:

- application startup/shutdown hooks
- canvas/window integration
- timing and frame delta
- input event forwarding
- browser-specific bridges (JS interop)
- file/network helpers (async fetch)
- frame scheduling hook
- canvas resize / device pixel ratio handling
- orientation change handling

Platform does **not** handle:

- gameplay
- scene logic
- render passes
- material logic
- resource manifests

## 3.4 Canvas, DPR, and Viewport

Platform layer must handle:

- **Device pixel ratio (DPR):** mobile devices may have DPR 2-3x. Rendering at native resolution on high-DPR is often prohibitive. Platform should expose current DPR and allow render resolution scaling.
- **Canvas resize:** window/orientation changes must update framebuffer size. Platform detects resize events and notifies engine.
- **Input coordinate mapping:** pointer positions arrive in CSS pixels, must be mapped to canvas/framebuffer coordinates.

```c
typedef struct PlatformDisplayInfo
{
    uint32_t canvas_width;      // CSS pixels
    uint32_t canvas_height;     // CSS pixels
    uint32_t framebuffer_width; // actual render pixels
    uint32_t framebuffer_height;
    float dpr;                  // device pixel ratio
    float render_scale;         // game-controlled quality scaling
} PlatformDisplayInfo;
```

---

# 4. Frame Lifecycle

The engine owns the top-level frame execution. The game provides callbacks.

## 4.1 Engine callbacks exposed to game

```c
void game_init(void);
void game_fixed_update(float dt);
void game_update(float dt);
void game_render(void);
void game_shutdown(void);
```

## 4.2 Engine frame order

```text
platform_step
input_begin_frame
    → if pointer pressed && audio suspended → audio_try_resume()
input_event_apply
resource_step         ← async loading processing
audio_update          ← voice state management
fixed_update loop
game_update
transform_update
game_render
frame_temp_reset
```

## 4.3 Fixed update loop

```c
accumulator += frame_dt;
int fixed_steps = 0;

while (accumulator >= fixed_dt && fixed_steps < max_fixed_steps)
{
    game_fixed_update(fixed_dt);
    accumulator -= fixed_dt;
    fixed_steps++;
}
```

Recommended defaults:

```c
fixed_dt = 1.0f / 60.0f
max_fixed_steps = 4
```

## 4.4 Update responsibilities

### `game_fixed_update(dt)`

Stable simulation: movement, AI, combat, timers, deterministic logic, future physics.

### `game_update(dt)`

Frame-based logic: camera, UI logic, effect fades, interpolation inputs, render-state preparation.

### `transform_update()`

Happens after gameplay movement, before render.

## 4.5 Optional interpolation factor

```c
float alpha = accumulator / fixed_dt;
```

Can be used for render interpolation between previous/current transform state.

## 4.6 Systems registry not used

The engine does not have a system registry. The game calls its systems explicitly in code. System order is defined explicitly. No phases, dependency graph, or scheduler.

```c
void game_fixed_update(float dt)
{
    movement_system_fixed(dt);
    ai_system_fixed(dt);
    combat_system_fixed(dt);
}

void game_update(float dt)
{
    camera_system_update(dt);
    ui_system_update(dt);
}
```

---

# 5. Memory Policy

## 5.1 High-level memory rules

- no heap allocation in hot path if avoidable
- component storages preallocated
- asset metadata always resident
- pack blobs transient
- frame temporary memory reset every frame
- renderer staging/batch buffers explicitly sized
- resource pools managed centrally

## 5.2 Memory categories

### Permanent memory

Lifetime ≈ engine/application lifetime.

Examples: entity tables, component storages, asset metadata, shader metadata, persistent runtime pools.

### Pack/blob transient memory

Lifetime ≈ load operation or recent-use cache window.

Examples: loaded pack blob, manifest read buffer, temporary decompression buffer if ever needed.

### Frame scratch memory

Lifetime = single frame.

Examples: render item arrays, temporary sort arrays, transient CPU batch buffers, build temp lists in render pass.

## 5.3 Compile-time limits

```c
#define MAX_ENTITIES            65536
#define MAX_TRANSFORMS          8192
#define MAX_RENDER_STATE        8192
#define MAX_MESH_COMPONENTS     8192
#define MAX_MATERIAL_COMPONENTS 8192
#define MAX_SPRITE_COMPONENTS   8192
#define MAX_TEXT_COMPONENTS     2048
#define MAX_SHADOW_COMPONENTS   4096

#define MAX_RESOURCES           65536
#define MAX_PACKS               256
#define MAX_RENDER_ITEMS        16384
#define MAX_POINTERS            8

#define MAX_AUDIO_CLIPS         256
#define MAX_AUDIO_VOICES        32
```

## 5.4 Runtime settings

```c
typedef struct EngineSettings
{
    float fixed_dt;
    int max_fixed_steps;
} EngineSettings;
```

No full config system (JSON/YAML/ini) required at start.

---

# 6. Entity System

## 6.1 Entity identity

Entities are lightweight IDs with generation validation.

```c
typedef uint16_t EntityIndex;
typedef uint16_t EntityGeneration;

typedef struct EntityHandle
{
    EntityIndex index;
    EntityGeneration generation;
} EntityHandle;
```

If larger scale is needed later, compile-time switch can allow 32-bit indices.

**Generation overflow note:** `uint16_t` generation overflows after 65535 create/destroy cycles on a single slot. For long-running web sessions with hot slots, monitor usage. If needed, upgrade to `uint32_t` generation (minimal cost increase).

## 6.2 Why generation exists

Without generation, stale entity handles can silently become valid again after slot reuse. Generation solves stale references and destroyed-then-reused slot ambiguity.

## 6.3 Entity table

Engine-level entity data lives in entity system, not in Transform.

Per entity slot:

```c
generation[index]
alive[index]
enabled[index]

parent[index]
first_child[index]
next_sibling[index]
prev_sibling[index]
```

This gives: valid/alive checks, hierarchy, enable/disable tree support, transform inheritance source tree, general logical tree use.

## 6.4 Hierarchy policy

Hierarchy belongs to **entity system**, not Transform.

Reasons: hierarchy useful beyond transform, enable/disable subtree, ownership/tree traversal, tree-based logic, render grouping if desired later.

## 6.5 Root detection

No separate root component needed. An entity is a root if:

```c
parent == INVALID_ENTITY_INDEX
```

## 6.6 Entity destruction and cleanup

When an entity is destroyed, systems that hold component data for that entity must be cleaned up. Two strategies are available:

1. **Deferred destruction queue:** mark entities for destruction, process queue at a defined point in frame (e.g., after game_update, before transform_update).
2. **Immediate destruction with per-storage cleanup:** entity_destroy iterates registered storages and removes components.

For v0.1, deferred destruction is recommended to avoid mid-frame structural changes to storages.

---

# 7. Component Storage Design

## 7.1 Storage model

Each component type has: unique component per entity, sparse lookup, dense storage, preallocated capacity.

## 7.2 Canonical storage layout

```c
typedef struct
{
    ComponentType data[CAPACITY];
    ComponentIndex entity_to_index[MAX_ENTITIES];
    EntityIndex index_to_entity[CAPACITY];
    uint32_t count;
} ComponentStorage;
```

Where:

- `entity_to_index` maps entity → dense index
- `index_to_entity` maps dense index → entity
- `data` stores actual dense components

Yes, sparse side is sized by max entities even if component capacity is smaller. This is correct and intentional.

## 7.3 Component index type

Default component indices = `uint16_t`. Only use `uint32_t` if truly needed later. Do not use odd-width runtime types like 12-bit indices.

## 7.4 Component API style

Typed APIs, not one generic mega-API:

```c
TransformComponent* transform_add(EntityHandle e);
TransformComponent* transform_get(EntityHandle e);
bool transform_has(EntityHandle e);
void transform_remove(EntityHandle e);
```

---

# 8. Transform System

## 8.1 Transform component data

```c
typedef struct TransformComponent
{
    vec3 local_position;
    quat local_rotation;
    vec3 local_scale;

    mat4 world_matrix;

    bool dirty;
} TransformComponent;
```

Optional future additions: previous_world_matrix, decomposed world data, bounds dirty flag.

## 8.2 Hierarchy source

Transform inheritance reads from entity hierarchy. Transform does not own parent/child links.

## 8.3 Update model

Update top-down. Roots are entities with no parent. Traversal uses entity hierarchy.

## 8.4 Dirty propagation

When local transform changes: mark this transform dirty, mark descendant transforms dirty. When parent changes: mark subtree dirty. Dirty only means world transform must be recomputed.

## 8.5 Non-transform nodes in hierarchy

Entities without Transform may still exist in hierarchy.

Traversal rule: walk entity tree; if node has transform, update world basis; if node has no transform, continue traversal with last valid inherited transform basis.

---

# 9. Render-Related Components

The architecture supports different renderable kinds via separate components, not one universal component.

## 9.1 Common render state

```c
typedef struct RenderStateComponent
{
    uint16_t tag;
    bool visible;
    vec4 color;
    vec4 params0;
} RenderStateComponent;
```

Defaults: `visible = true`, `color = (1,1,1,1)`, `params0 = (0,0,0,0)`.

- `tag`: pass/group filter chosen by game
- `visible`: render visibility only
- `color`: object tint / alpha multiplier
- `params0`: extra per-object shader values (blink, hit flash, dissolve, outline factor)

## 9.2 Mesh component

```c
typedef struct MeshComponent
{
    MeshAssetRef mesh;
} MeshComponent;
```

## 9.3 Material component

```c
typedef struct MaterialComponent
{
    MaterialAssetRef material;
} MaterialComponent;
```

## 9.4 Sprite component

```c
typedef struct SpriteComponent
{
    SpriteAssetRef sprite;
} SpriteComponent;
```

Sprite is a separate render kind, not a special mode of mesh.

## 9.5 Text component

```c
typedef struct TextComponent
{
    FontAssetRef font;
    StringId text;
} TextComponent;
```

StringId references a string in a string pool/intern table (detail deferred to implementation phase).

## 9.6 Shadow component

```c
typedef struct ShadowComponent
{
    bool enabled;
    MeshAssetRef mesh_override;
    MaterialAssetRef material_override;
} ShadowComponent;
```

If missing: object does not participate in shadow pass. If present and enabled: use override mesh/material if valid, otherwise use primary mesh/material or default shadow path.

---

# 10. Render Tags

## 10.1 RenderTag philosophy

Render tags are **game-defined**, not engine-enum-defined.

```c
RenderTag TAG_WORLD = render_tags_new();
RenderTag TAG_UI = render_tags_new();
RenderTag TAG_DEBUG = render_tags_new();
```

## 10.2 What tags mean

RenderTag means: pass category, render grouping chosen by game, filter for pass building.

RenderTag does **not** mean: component type, mesh vs sprite vs text, material type.

The renderer backend and low-level render API do not know about tags. Tags are used by game code for filtering, grouping by pass, choosing sort/batch policy.

---

# 11. Rendering Architecture

## 11.1 Engine/game boundary

Renderer backend and render primitives belong to engine. Render pipeline belongs to game.

### Engine provides

- renderer begin/end frame
- begin/end pass
- draw mesh primitive
- draw sprite primitive
- GPU resource creation and binding
- material/shader binding helpers

### Game decides

- pass order
- which tags are used
- sort policy for a pass
- whether a pass sorts by depth or material
- whether a given list uses batching or not

## 11.2 Renderer backend API shape

Engine-oriented, not WebGL-mirror and not full WebGPU abstraction:

```c
renderer_begin_frame();
renderer_end_frame();

renderer_begin_pass(&desc);
renderer_end_pass();

renderer_set_camera(&camera);

renderer_draw_mesh(...);
renderer_draw_sprite(...);
```

---

# 12. Render Items, Sort Keys, Batch Keys

## 12.1 RenderItem concept

A RenderItem is a **CPU-side prepared draw record**, not a GPU object.

```text
Entity/components
    → RenderItem build
    → sort
    → renderer consumes
    → GPU draw calls
```

## 12.2 Universal RenderItem model

```c
typedef enum RenderDrawType
{
    RENDER_DRAW_MESH,
    RENDER_DRAW_SPRITE,
    RENDER_DRAW_TEXT
} RenderDrawType;
```

```c
typedef struct RenderItem
{
    uint64_t sort_key;
    uint64_t batch_key;

    RenderDrawType draw_type;

    MaterialHandle material;

    float sort_depth;

    mat4 world_matrix;  // copied, not pointer — see note
    vec4 color;
    vec4 params0;

    union
    {
        MeshHandle mesh;
        SpriteHandle sprite;
        TextHandle text;
    } draw;
} RenderItem;
```

**Design note:** `world_matrix` is stored by value (64 bytes), not as pointer. This avoids dangling pointer risk if entity/component storages undergo structural changes between render item build phase and actual render. At MAX_RENDER_ITEMS = 16384, this costs ~1MB of frame scratch memory, which is acceptable.

## 12.3 Sort key meaning

Sort key determines item order in the final draw sequence for a pass. It is pass-dependent. There is not one universal sort key layout for all passes.

**Material-sorted pass:** sort by material/state/pipeline/texture.

**Depth-sorted pass:** sort by depth first, then other fields as tie-break.

## 12.4 Batch key meaning

Batch key determines state compatibility. A run of equal batch keys means likely same shader/material/state, likely no state switch needed, possibly batchable.

Equal batch key does **not** always mean single draw call is possible.

## 12.5 Why both keys exist

- `sort_key` controls ordering
- `batch_key` controls state compatibility grouping

```c
if (item.batch_key == current_batch_key)
{
    // continue run — no state change needed
}
else
{
    // flush and begin new run
}
```

---

# 13. Sorting Policy

## 13.1 Sort policy is pass-controlled

The game decides sort mode for each pass. Typical modes: sort by material/state, sort by depth, no sort, custom order + tie-break.

## 13.2 Depth sorting

Depth is computed on CPU only when needed. Transparent/depth-sensitive passes compute depth. Opaque/material passes may skip depth entirely.

**Do not compute depth for all items by default.**

---

# 14. Batching and Instancing

## 14.1 Renderer-specific batching

### SpriteRenderer

CPU batch: max 256 sprites per batch, flush when incompatible state or full batch.

### MeshRenderer

No true merging for arbitrary meshes. Same batch key means no state changes. Later add instancing for same mesh/material runs.

## 14.2 Mesh instancing

Mesh instancing is desired early. Works best when: same mesh, same material, same shader layout, different world/object params only.

WebGL 2 provides native `drawArraysInstanced` / `drawElementsInstanced` — no extension management needed.

## 14.3 Sprite batching strategy

Initial sprite renderer: gather sorted sprite render items, pack up to N sprites into one dynamic vertex buffer, flush on state change/full batch. Logic lives inside SpriteRenderer and can later be replaced with instancing without changing external pass architecture.

---

# 15. Shader System

## 15.1 ShaderAsset purpose

ShaderAsset defines interface, not values.

## 15.2 ShaderAsset fields

```c
typedef struct ShaderAsset
{
    ShaderCodeRef vs;
    ShaderCodeRef fs;

    uint32_t vertex_input_mask;

    uint16_t material_vec4_count;
    uint16_t texture_slot_count;

    uint16_t object_usage_mask;
    uint16_t global_usage_mask;

    BlendMode default_blend_mode;
    bool default_depth_test;
    bool default_depth_write;
    CullMode default_cull_mode;
    SortMode default_sort_mode;
} ShaderAsset;
```

## 15.3 Four levels of shader data

1. **vertex inputs** — from geometry/mesh
2. **material params** — from MaterialAsset
3. **object params** — from RenderState, Transform
4. **globals** — from renderer/pass

## 15.4 Vertex input mask

Possible semantics: POSITION, NORMAL, UV0, COLOR0. Mesh/shader compatibility validated in builder and sanity-checked at runtime.

## 15.5 Object params

Fixed object-level params for v0.1: world_matrix, object_color, object_params0.

## 15.6 Global params

Possible globals: view, proj, view_proj, camera_pos, time, light_dir. Start minimal, expand later.

WebGL 2 Uniform Buffer Objects can be used to share globals efficiently across shaders.

---

# 16. Material System

## 16.1 MaterialAsset purpose

Material = shader + render state + values.

## 16.2 Numeric params policy

All numeric material params stored as `vec4[]`. This is intentional.

Rule: float uses `.x`, vec2 uses `.xy`, vec3 uses `.xyz`, vec4 uses `.xyzw`.

Benefits: simple layout, simple alignment, easy future GPU block packing, no per-type runtime complexity.

## 16.3 MaterialAsset binary layout

```c
// In-memory header (NOT a C struct with FAM)
typedef struct MaterialAssetHeader
{
    ShaderAssetRef shader;

    BlendMode blend_mode;
    bool depth_test;
    bool depth_write;
    CullMode cull_mode;
    SortMode sort_mode;

    uint16_t param_count;
    uint16_t texture_count;
} MaterialAssetHeader;
```

Binary layout in pack:

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
const vec4* material_get_params(const MaterialAssetHeader* h)
{
    return (const vec4*)((const uint8_t*)h + sizeof(MaterialAssetHeader));
}

const TextureAssetRef* material_get_textures(const MaterialAssetHeader* h)
{
    return (const TextureAssetRef*)(material_get_params(h) + h->param_count);
}
```

**Note:** C does not allow two flexible array members in one struct. The layout above uses computed offsets instead.

## 16.4 No separate MaterialRuntime copy

Important decision: no full duplicated MaterialRuntime object by default. MaterialHandle points directly to MaterialAsset-style runtime-loaded asset storage. No unnecessary full-copy runtime material object.

## 16.5 Render state and material

Even if pass code on C directly sets depth/blend/cull state, material still stores render hints/state because they are needed for: bucket/pass selection, sort policy, batch compatibility.

---

# 17. Resource System

## 17.1 Core concepts

- `ResourceId`: stable resource identity
- `AssetRef`: typed ref into asset registry
- `AssetMeta`: metadata entry
- `Handle`: runtime typed handle to actual loaded runtime object

## 17.2 ResourceId

`ResourceId` is a stable resource key. By it the resource registry knows: whether the resource exists, its type, where it lives, what state it is in, whether a runtime representation exists.

## 17.3 Typed refs

```c
MeshAssetRef
TextureAssetRef
MaterialAssetRef
ShaderAssetRef
AudioAssetRef        // new
```

Also typed runtime handles:

```c
MeshHandle
TextureHandle
MaterialHandle
ShaderHandle
AudioClipHandle      // new
```

## 17.4 Stable AssetMeta entries

AssetMeta entries are stable for whole runtime. Unload clears runtime handle/state, not the asset metadata slot. AssetRef does not need generation if slot is stable.

## 17.5 AssetMeta

```c
typedef struct AssetMeta
{
    ResourceId id;
    AssetType type;
    uint32_t pack_index;
    uint32_t entry_offset;
    uint32_t entry_size;
    uint16_t format_version;
    AssetState state;
} AssetMeta;
```

## 17.6 Asset types

```c
typedef enum AssetType
{
    ASSET_TYPE_MESH,
    ASSET_TYPE_TEXTURE,
    ASSET_TYPE_SHADER,
    ASSET_TYPE_MATERIAL,
    ASSET_TYPE_AUDIO,
    ASSET_TYPE_SPRITE,
    ASSET_TYPE_FONT,
} AssetType;
```

## 17.7 Placeholder policy

If resource is not ready:

- typed resolve returns placeholder handle
- runtime continues working
- resource state remains UNLOADED / LOADING / FAILED as appropriate

Placeholder policy is part of the resource system, not ad-hoc code.

---

# 18. Async Loading System

## 18.1 Overview

On the web, all data loading is asynchronous. `fetch()` returns a Promise. The main thread cannot be blocked. Loading must be non-blocking and integrated into the frame loop.

The same async contract applies to all platforms for consistency — desktop implementations may complete instantly but the API contract remains "potentially async."

## 18.2 Pack state machine

```c
typedef enum PackState
{
    PACK_STATE_NONE,         // never loaded
    PACK_STATE_REQUESTED,    // fetch started
    PACK_STATE_DOWNLOADING,  // data incoming (optional, for progress)
    PACK_STATE_LOADED,       // blob received, manifest not parsed
    PACK_STATE_READY,        // manifest parsed, assets registered
    PACK_STATE_FAILED        // error
} PackState;
```

## 18.3 Asset state machine

```c
typedef enum AssetState
{
    ASSET_STATE_UNKNOWN,      // meta does not exist yet
    ASSET_STATE_REGISTERED,   // meta exists, data not loaded
    ASSET_STATE_LOADING,      // being created (e.g. GPU upload, audio decode)
    ASSET_STATE_READY,        // runtime handle created, usable
    ASSET_STATE_FAILED        // parse/creation error
} AssetState;
```

## 18.4 Pack loading flow

```text
game code: pack_request_load("world.pak")
  → PackMeta.state = REQUESTED
  → platform_web calls fetch() via JS bridge

... N frames pass ...

JS callback → WASM: platform_on_fetch_complete(request_id, blob_ptr, blob_size, success)
  → PackMeta.state = LOADED
  → PackMeta.blob_data = blob_ptr

Next resource_step():
  → sees LOADED pack
  → parses header/manifest (NEOPAK format, direct struct read)
  → registers AssetMeta entries (state = REGISTERED)
  → PackMeta.state = READY

Asset activation (eager with rate-limit):
  → resource_step() processes up to N assets per frame
  → reads data from blob by offset/size
  → parses runtime format
  → creates GPU resources / decodes audio
  → AssetState = READY
```

## 18.5 Loading progress

PackMeta includes progress tracking:

```c
typedef struct PackMeta
{
    PackId id;
    const char* url;
    PackState state;
    uint32_t blob_size;
    void* blob_data;

    uint32_t bytes_received;  // progress tracking
    uint32_t bytes_total;     // 0 = unknown (Content-Length absent)

    float last_used_time;
} PackMeta;
```

Progress is delivered via JS `ReadableStream` API through per-chunk callbacks. When `bytes_total` is unknown (no Content-Length header), game code can show a spinner instead of a percentage bar.

## 18.6 JS bridge — fetch contract

C exports:

```c
// Called from C → JS
void platform_request_fetch(uint32_t request_id, const char* url);

// Called from JS → C
EMSCRIPTEN_KEEPALIVE
void platform_on_fetch_progress(uint32_t request_id,
                                 uint32_t received,
                                 uint32_t total);

EMSCRIPTEN_KEEPALIVE
void platform_on_fetch_complete(uint32_t request_id,
                                 uint8_t* data,
                                 uint32_t size,
                                 uint32_t success);
```

## 18.7 Asset activation strategy

**Eager with rate-limit** (recommended for v0.1): when a pack becomes READY, `resource_step()` processes up to N assets per frame from the ready queue. This prevents frame spikes while ensuring assets become available quickly.

## 18.8 Retry policy

1-2 retries with exponential backoff. After retries fail: PackState = FAILED, log error, game code decides response (show error, retry later).

## 18.9 Memory note

Peak memory during loading = 2x pack size (JS fetch buffer + WASM heap copy). For packs in the low megabytes range this is acceptable.

---

# 19. Pack Format (NEOPAK)

## 19.1 Design rationale

Custom flat binary format instead of ZIP. Rationale:

- no external library dependency (no miniz in WASM, saves ~15-25KB binary size)
- trivial parsing: direct struct reads, no variable-length header parsing
- zero-copy asset access: pointer + offset into loaded blob
- manifest is embedded in header, not a separate file
- HTTP transport compression (gzip/brotli) handles delivery size
- partial loading via HTTP Range requests is straightforward (header first, then assets by offset)

## 19.2 Binary layout

```text
┌──────────────────────────────────────┐
│ PackHeader                            │
│   magic: "NPAK"  (4 bytes)           │
│   version: uint16                     │
│   pack_id: uint32                     │
│   asset_count: uint16                 │
│   header_size: uint32  ← data start  │
│   total_size: uint32                  │
│   checksum: uint32     ← CRC32       │
├──────────────────────────────────────┤
│ AssetEntry[0]                         │
│   resource_id: uint32                 │
│   asset_type: uint8                   │
│   format_version: uint16              │
│   offset: uint32  ← from file start  │
│   size: uint32                        │
├──────────────────────────────────────┤
│ AssetEntry[1]                         │
│   ...                                 │
├──────────────────────────────────────┤
│ AssetEntry[N-1]                       │
│   ...                                 │
╞══════════════════════════════════════╡
│ [padding to alignment]                │
│ [asset 0 binary data]                 │
│ [asset 1 binary data]                 │
│ ...                                   │
│ [asset N-1 binary data]               │
└──────────────────────────────────────┘
```

Assets aligned to 4 or 8 bytes within pack. Header/entries region aligned to 8 bytes before data start.

## 19.3 Runtime parsing

```c
bool pack_parse(const uint8_t* blob, uint32_t blob_size, PackMeta* pack)
{
    const PackHeader* h = (const PackHeader*)blob;

    if (h->magic != PACK_MAGIC) return false;
    if (h->version > PACK_VERSION_MAX) return false;
    if (h->header_size > blob_size) return false;
    if (h->total_size != blob_size) return false;

    const AssetEntry* entries = (const AssetEntry*)(blob + sizeof(PackHeader));

    for (uint16_t i = 0; i < h->asset_count; i++)
    {
        asset_registry_add(entries[i].resource_id,
                          entries[i].asset_type,
                          pack->pack_index,
                          entries[i].offset,
                          entries[i].size,
                          entries[i].format_version);
    }

    return true;
}
```

## 19.4 Asset data access

```c
const uint8_t* pack_get_asset_data(const PackMeta* pack,
                                    uint32_t offset,
                                    uint32_t size)
{
    return pack->blob_data + offset;
}
```

Zero copy. Data is already in WASM heap.

## 19.5 Debugging

Builder includes `pack_dump(filename)` utility command that prints pack contents to console. No external tool needed.

## 19.6 Future: partial loading

Flat layout allows HTTP Range requests: load first `header_size` bytes to get manifest, then load individual assets by offset/size on demand.

---

# 20. Runtime Formats

## 20.1 General rule

Runtime reads only runtime formats. Builder converts from source formats to runtime formats.

Examples:

- source `.glb` → runtime mesh binary
- source `.png` → runtime texture binary
- source material description → runtime material binary
- source `.wav`/`.ogg` → runtime audio binary (OGG Vorbis)

## 20.2 Runtime format validation

Runtime must validate: magic, version, type, sizes/offsets, required vertex/material compatibility. Builder validation is primary. Runtime validation is safety net.

## 20.3 Mesh format strategy

Runtime mesh format should be: compact, near GPU-ready, not authoring-friendly.

Attributes: POSITION (required), NORMAL (optional), UV0 (optional), COLOR0 (optional).

Preferred data types: position float16 or float32, normals snorm8 packed, uv unorm16, colors uint8 normalized. Avoid runtime unpacking.

---

# 21. Input System

## 21.1 Model

Input system is polling-based. Game queries state each frame, does not subscribe to callbacks.

## 21.2 Pointer state

```c
typedef struct InputPointer
{
    bool active;
    bool down;
    bool pressed;
    bool released;

    float x;
    float y;
    float prev_x;
    float prev_y;
    float dx;
    float dy;

    uint32_t capture_owner;
} InputPointer;
```

Mouse and touch unify under pointer model.

## 21.3 Input capture

Capture is stored centrally in input system.

```c
bool input_try_capture(int pointer, uint32_t owner);
void input_release_capture(int pointer, uint32_t owner);
bool input_is_owner(int pointer, uint32_t owner);
bool input_pointer_captured(int pointer);
```

Raw input always exists; capture only affects processing ownership.

Capture owner: not necessarily entity id, generic `uint32_t owner_id` chosen by game/systems. Auto-release on pointer release.

---

# 22. Audio System

## 22.1 Architecture overview

Audio is an **engine module**, analogous to input and platform. Not an ECS component, not game-side code.

```text
engine/
    audio/
        audio.h           // public API — single for all platforms
        audio_types.h     // handles, enums, defines
        audio_web.c       // Web Audio API via JS bridge
        audio_desktop.c   // miniaudio or custom mixer (future)
```

Build system compiles only one implementation file per platform.

## 22.2 Platform-agnostic design

**Public API contains zero platform-specific types.** Only handles, floats, and bools. Game code is identical across web and desktop.

Key contracts:

- `audio_clip_create` is always potentially async (desktop may complete instantly, but game code does not rely on this)
- `audio_try_resume()` exists on all platforms (no-op on desktop)
- Audio format in packs is OGG Vorbis — both platforms can decode it
- Internal structures are different per-platform, hidden from game code

## 22.3 Audio state

```c
typedef enum AudioState
{
    AUDIO_SUSPENDED,    // before first user gesture (web) or init failure
    AUDIO_RUNNING,      // ready to play
    AUDIO_FAILED        // AudioContext/backend creation failed
} AudioState;
```

All `audio_play` calls in SUSPENDED state return `AUDIO_VOICE_INVALID` without error. Game code continues normally.

## 22.4 Audio clips

```c
typedef struct AudioClipHandle { uint16_t index; } AudioClipHandle;
#define AUDIO_CLIP_INVALID ((AudioClipHandle){ 0xFFFF })

typedef enum AudioClipState
{
    AUDIO_CLIP_NONE,
    AUDIO_CLIP_DECODING,   // decodeAudioData in progress (web) or decoding (desktop)
    AUDIO_CLIP_READY,
    AUDIO_CLIP_FAILED
} AudioClipState;
```

Internal storage (web):

```c
typedef struct AudioClipInternal
{
    AudioClipState state;
    uint32_t js_buffer_id;    // index into JS-side AudioBuffer array
    float duration;
    uint16_t generation;
} AudioClipInternal;
```

Internal storage (desktop):

```c
typedef struct AudioClipInternal
{
    AudioClipState state;
    int16_t* pcm_data;        // decoded samples in C heap
    uint32_t sample_count;
    uint32_t sample_rate;
    uint8_t channels;
    float duration;
    uint16_t generation;
} AudioClipInternal;
```

## 22.5 Audio voices

```c
typedef struct AudioVoiceHandle { uint16_t index; } AudioVoiceHandle;
#define AUDIO_VOICE_INVALID ((AudioVoiceHandle){ 0xFFFF })

typedef enum AudioVoiceState
{
    VOICE_FREE,
    VOICE_PLAYING,
    VOICE_STOPPING
} AudioVoiceState;
```

Voice pool with eviction: when all 32 voices are occupied, evict the oldest non-looping voice. If all voices are looping, do not play the new sound.

## 22.6 Public API

```c
// === Lifecycle ===
void audio_init(void);
void audio_shutdown(void);
void audio_update(void);
AudioState audio_get_state(void);

// === Resume (call on user gesture) ===
void audio_try_resume(void);

// === Clips ===
AudioClipHandle audio_clip_create(const uint8_t* encoded_data, uint32_t size);
void audio_clip_destroy(AudioClipHandle clip);
AudioClipState audio_clip_get_state(AudioClipHandle clip);
float audio_clip_get_duration(AudioClipHandle clip);

// === Playback ===
AudioVoiceHandle audio_play(AudioClipHandle clip, float volume, float pitch, bool loop);
void audio_stop(AudioVoiceHandle voice);
void audio_stop_all(void);

// === Voice control ===
void audio_set_volume(AudioVoiceHandle voice, float volume);
void audio_set_pitch(AudioVoiceHandle voice, float pitch);
bool audio_is_playing(AudioVoiceHandle voice);

// === Global ===
void audio_set_master_volume(float volume);
float audio_get_master_volume(void);
```

## 22.7 JS bridge contract (web implementation)

C calls to JS:

```c
extern void js_audio_init(void);
extern void js_audio_shutdown(void);
extern void js_audio_resume(void);
extern uint32_t js_audio_decode(uint16_t clip_index, const uint8_t* data, uint32_t size);
extern uint32_t js_audio_play(uint32_t js_buffer_id, float volume, float pitch,
                               bool loop, uint16_t voice_index);
extern void js_audio_stop(uint32_t js_source_id);
extern void js_audio_set_volume(uint32_t js_source_id, float volume);
extern void js_audio_set_pitch(uint32_t js_source_id, float pitch);
extern void js_audio_set_master_volume(float volume);
```

JS calls to C:

```c
EMSCRIPTEN_KEEPALIVE
void audio_on_clip_decoded(uint16_t clip_index, uint32_t js_buffer_id,
                            float duration, uint32_t success);

EMSCRIPTEN_KEEPALIVE
void audio_on_voice_ended(uint16_t voice_index);

EMSCRIPTEN_KEEPALIVE
void audio_on_state_changed(uint32_t running);
```

## 22.8 Integration with frame loop

In `input_begin_frame` or `platform_step`:

```c
if (audio_get_state() == AUDIO_SUSPENDED && any_pointer_pressed)
{
    audio_try_resume();
}
```

`audio_update()` is called each frame for voice state management (safety timeout, future fade management).

## 22.9 Audio in resource pipeline

```c
// Builder
add_audio("assets/sfx/hit.wav");      // WAV → OGG conversion
add_audio("assets/music/theme.ogg");  // already OGG, pack as-is
```

Loading flow:

```text
Pack loaded → AssetMeta registered (REGISTERED)
  → asset_ensure_loaded() for audio:
      → read blob from pack by offset/size
      → call audio_clip_create(data, size)
      → AudioClipState = DECODING, AssetState = LOADING

  ... JS/native decoding ...

  → audio_on_clip_decoded callback:
      → AudioClipState = READY
      → AssetState = READY
```

## 22.10 What is intentionally absent

- 3D audio / positional panning (future: add PannerNode on web, positional mixing on desktop)
- Sound groups / buses (game code manages category volumes through own wrappers)
- Effects (reverb, delay)
- Fade-in / fade-out (game code does via audio_set_volume + tween)
- Streaming long tracks (OGG 128kbps ≈ 1MB/min, decodeAudioData handles 5-minute tracks in milliseconds)

---

# 23. Builder Architecture

## 23.1 Builder model

Builder is a standalone C binary. Rules are written in code.

```c
start_pack("base");
add_shaders("assets/shaders/*.shader");
add_textures("assets/textures/ui/*.png");
add_materials("assets/materials/ui/*.mat");
add_meshes("assets/meshes/common/*.glb");
add_audio("assets/sfx/*.wav");
add_audio("assets/music/*.ogg");
finish_pack();
```

## 23.2 Why code-based builder

Explicit control, no DSL needed, powerful grouping logic, easy custom per-project rules, aligns with engine philosophy.

## 23.3 Builder module layers

```text
builder/
    main_builder.c
    builder_pack.c
    builder_manifest.c
    builder_import_mesh.c
    builder_import_texture.c
    builder_import_shader.c
    builder_import_material.c
    builder_import_audio.c
    builder_project.c
```

## 23.4 Core builder API

```c
start_pack(const char* name);
finish_pack(void);

add_mesh(const char* path);
add_texture(const char* path);
add_shader(const char* path);
add_material(const char* path);
add_audio(const char* path);

add_meshes(const char* pattern);
add_textures(const char* pattern);
add_shaders(const char* pattern);
add_materials(const char* pattern);
add_audios(const char* pattern);
```

Prefer typed wildcard functions over one untyped `add_files()`.

## 23.5 Builder stages

1. source assets
2. import
3. validation
4. conversion to runtime format
5. pack placement with alignment
6. manifest generation (embedded in pack header)
7. write NEOPAK binary

## 23.6 Builder validation

Builder must check: references between assets, resource types, mesh/material/shader compatibility, required attributes, runtime format generation correctness, audio format validity.

---

# 24. Logging, Errors, Debugging

## 24.1 Logging levels

- INFO
- WARN
- ERROR
- ASSERT
- PANIC

## 24.2 Error policy

### Fatal

- backend init failure
- unsupported critical format version
- impossible startup state

### Recoverable

- missing texture → placeholder
- material mismatch → placeholder material
- resource load fail → asset state failed + log
- audio decode failure → clip state failed + log

## 24.3 Debug overlay

Recommended stats: frame time, fixed step count, draw call count, batch count, loaded resource count, active pack count, temp memory usage, audio voice count.

---

# 25. Engine/Game Boundary

This is one of the most important decisions.

## 25.1 Engine owns

- platform
- memory
- entities
- component storage pattern
- hierarchy storage
- resources
- packs
- runtime format loading
- GPU backend
- render primitives
- input system
- audio system

## 25.2 Game owns

- gameplay systems
- system order
- render passes
- render tags
- sort policy
- batching choice per pass
- level/scene logic
- capture ownership semantics
- high-level content organization

This boundary is strict.

---

# 26. Module Layout

```text
engine/
    core/
    memory/
    platform/
    entities/
    components/
        transform/
        render_state/
        mesh/
        material/
        sprite/
        text/
        shadow/
    resources/
    packs/
    formats/
    input/
    render/
    gpu/
    audio/

builder/   (separate program)

game/      (game-side code)
```

---

# 27. Suggested Implementation Order

1. platform_web + core loop + fixed/update lifecycle
2. memory arenas / alloc policy
3. entity system + hierarchy
4. sparse component storage template
5. transform + hierarchy update
6. resource ids / asset meta / pack meta
7. NEOPAK format: pack parsing + asset access
8. async loading (fetch bridge + resource_step)
9. shader asset + material asset parsing
10. texture asset handling
11. mesh asset/runtime handling
12. render backend basics (WebGL 2)
13. render item build/sort
14. mesh rendering
15. sprite renderer with CPU batching
16. input polling + capture
17. audio system (web backend)
18. builder binary + importers + pack generation
19. builder audio importer

---

# 28. Summary of Critical Locked Decisions

These decisions are **locked** unless a strong reason appears:

1. Web-first startup target
2. WebGL 2 as sole baseline (no WebGL 1)
3. Game-defined render loop
4. No system registry
5. Sparse unique component storages
6. Hierarchy lives in entity system
7. Builder is standalone C binary
8. Builder rules are code
9. Runtime formats are custom binary
10. Custom pack format (NEOPAK) — flat binary, no ZIP
11. Manifest embedded in pack header
12. Material numeric params are `vec4[]`
13. No full duplicated MaterialRuntime object initially
14. Asset metadata is stable and resident
15. Render tags are game-defined
16. sort_key and batch_key are separate concepts
17. Depth only computed when sort policy needs it
18. Sprite batching belongs inside SpriteRenderer
19. Input is polling-based with pointer capture
20. Compile-time capacities, minimal runtime settings
21. Audio is engine module, not game-side
22. Audio public API is handle-based, zero platform types
23. audio_clip_create contract is always potentially async
24. audio_try_resume() exists on all platforms
25. Audio format in packs is OGG Vorbis
26. Audio internal structures are per-platform, hidden from game code
27. Voice pool with oldest-non-looping eviction
28. RenderItem stores world_matrix by value, not pointer
29. Async loading is non-blocking with PackState/AssetState machines
30. Eager asset activation with per-frame rate-limit

---

# 29. Open but Non-Critical Future Questions

These do not block implementation:

- exact binary layout of each runtime format header
- precise bit packing of sort keys
- future WebGPU backend details
- exact sprite asset format and animation system
- exact text rendering strategy and string pool design
- camera component/structure definition
- whether some renderer-specific caches are worth adding later
- whether pack hot-reload becomes needed
- whether asset build database is needed immediately or later
- 3D audio / positional sound
- sound groups / mix buses
- desktop audio backend library choice (miniaudio recommended)
- entity destruction notification mechanism details
- generation overflow monitoring strategy

These can be solved incrementally without breaking the core architecture.

---

# 30. Final Architecture Snapshot

```text
Game code
    ├─ defines gameplay system order
    ├─ defines render passes
    ├─ defines tags
    ├─ calls engine subsystems
    └─ uses engine primitives

Engine
    ├─ runs frame lifecycle
    ├─ stores entities/components/resources
    ├─ updates transforms
    ├─ loads runtime assets from NEOPAK packs (async)
    ├─ provides render backend (WebGL 2)
    ├─ provides input + platform services
    └─ provides audio playback

Builder
    ├─ imports source assets
    ├─ validates compatibility
    ├─ converts to runtime formats
    ├─ builds NEOPAK packs
    └─ embeds manifest in pack header
```

This gives: explicit control, minimal runtime complexity, strong builder/runtime split, web-first practicality, platform-agnostic game code, future room for WebGPU and desktop platforms without redesigning the core.
