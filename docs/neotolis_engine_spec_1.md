
# Neotolis Engine — Technical Specification

**Version:** v0.3-consolidated  
**Status:** Architectural baseline + implementation-oriented spec  
**Language target:** C17 (vendored C++ allowed behind extern "C" boundary)
**Primary runtime target:** Web / WASM + WebGL 2
**Secondary future target:** WebGPU

This document consolidates all architectural decisions from v0.1 overview, v0.2 technical spec, and subsequent design sessions into a single authoritative reference.

Language baseline is C17 for broader compiler and Emscripten toolchain support. Vendored C++ dependencies (e.g. Basis Universal transcoder/encoder) are permitted when no C alternative exists, provided they are isolated behind `extern "C"` wrappers and `enable_language(CXX)` is scoped to their subdirectory CMakeLists — not the root.

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
- custom binary pack format (NTPACK)
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

#define MAX_ASSETS              2048
#define MAX_SLOTS               2048
#define MAX_PACKS               16
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
typedef struct DrawableComponent
{
    nt_hash32_t tag;  // hash-based, game-defined via nt_hash32_str()
    bool visible;
    vec4 color;
} DrawableComponent;
```

Defaults: `visible = true`, `color = (1,1,1,1)`, `tag = {0}`.

- `tag`: pass/group filter chosen by game, set via `nt_hash32_str("world")` etc.
- `visible`: render visibility only
- `color`: object tint / alpha multiplier

Per-entity shader params (`params0`) deferred to ShaderParamsComponent — add when per-entity shader effects are needed (#98).

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
    nt_material_t material; /* typed handle from nt_material module */
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
    nt_font_t font;    /* handle from nt_font_create / nt_font_add */
    StringId text;
} TextComponent;
```

`nt_font_t` is a pool-backed handle to a font instance. A font instance owns GPU textures (curve + band) and a glyph cache. Font data comes from one or more `nt_resource_t` assets attached via `nt_font_add()`, allowing fallback chains (base font + CJK extension pack, etc.). Glyphs are decoded and uploaded to GPU on first lookup, not on asset load.

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

Render tags are **game-defined**, not engine-enum-defined. Tags are `nt_hash32_t` values created via `nt_hash32_str()`.

```c
nt_hash32_t TAG_WORLD = nt_hash32_str("world");
nt_hash32_t TAG_UI    = nt_hash32_str("ui");
nt_hash32_t TAG_DEBUG = nt_hash32_str("debug");
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

## 12.2 RenderItem model

Minimal render item — sorted draw record, not a fat data carrier. Renderer reads per-entity data (world matrix, color) from components at draw time.

```c
typedef struct nt_render_item_t
{
    uint64_t sort_key;    // 8 bytes — encodes material+mesh for opaque, depth for transparent
    uint32_t entity;      // 4 bytes — raw entity id
    uint32_t batch_key;   // 4 bytes — state compatibility (same material+mesh = same key)
} nt_render_item_t;       // 16 bytes, naturally aligned
```

**batch_key vs sort_key:** sort_key controls draw order (can be anything: material, depth, layer). batch_key controls instancing compatibility (same material+mesh). These are independent — depth-sorted items still batch by material+mesh.

**Why no inline world_matrix:** Instance packing reads world_matrix + color from component arrays via entity lookup (scattered access). Inlining them in the render item (96B) would make packing sequential, but qsort on 96B elements is ~6× slower than on 16B. At typical scales (<5K entities), sort dominates over packing. If CPU-bound at 10K+: switch to radix sort or indirect sort, then fat items become free.


## 12.3 Sort key meaning

Sort key determines item order in the final draw sequence for a pass. It is pass-dependent. There is not one universal sort key layout for all passes.

**Material-sorted pass:** sort by material/state/pipeline/texture.

**Depth-sorted pass:** sort by depth first, then other fields as tie-break.

## 12.4 Batch key / run detection

`batch_key` encodes state compatibility (same material+mesh = same key). `sort_key` controls draw order. These are independent concerns — sort order can be anything (material, depth, layer) without affecting batch detection.

Game fills `batch_key` via `nt_batch_key(material_id, mesh_id)`. Renderer compares consecutive batch_keys to detect instancing runs:

```c
while (run_end < count && items[run_end].batch_key == items[run_start].batch_key)
    run_end++;
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
    ShaderAssetRef vertex_shader;
    ShaderAssetRef fragment_shader;

    BlendMode blend_mode;
    bool depth_test;
    bool depth_write;
    CullMode cull_mode;

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

## 16.4 One material, one copy

No duplicated material data. Material is created once (either from code via descriptor or loaded from pack asset in the future) and lives in a single pool slot. Multiple entities reference the same material handle.

Per-entity variation (e.g. per-character color, dissolve progress) goes through entity param components, not material mutation — each entity carries its own values, the material stays shared.

Material-wide params (e.g. global alpha cutoff, roughness) can be mutated at runtime via `nt_material_set_param` / `nt_material_set_param_component`. This changes the value for all entities sharing that material. The renderer re-reads params every frame; no version bump is needed. Hash-based overloads (`_h` suffix) accept a pre-computed `nt_hash32_t` to avoid per-frame string hashing.

## 16.5 Render state and material

Material stores render state (blend mode, depth test/write, cull mode) because it is a property of the surface, not the pass. Pipeline (GPU state object) is derived from material render state + mesh vertex layout at render time.

Sort order is **not** a material property. Sorting is game-controlled: game code gets entities by tag, sorts them (by material for opaques, by depth for transparents, or any custom order), and submits draw items to the renderer in that order. The renderer draws in submission order and batches consecutive compatible items. See section 13.1.

---

# 17. Resource System

## 17.1 Core concepts

- `resource_id`: 64-bit xxHash of asset path (`nt_hash64_t`) — stable resource identity
- `NtAssetMeta`: per-asset metadata entry (one per asset per pack)
- `NtResourceSlot`: per unique resource requested by game code — holds resolved handle and optional user_data
- `nt_resource_t`: generational handle to a slot — what game code holds and passes around

Two-level system:
- **Assets** (MAX_ASSETS): metadata from all packs. Same resource_id can appear in multiple packs.
- **Slots** (MAX_SLOTS): unique resources requested by game. One slot per resource_id, holds the best resolved handle.

## 17.2 ResourceId

`resource_id` is a `uint64_t` xxHash (XXH64) of the asset path, wrapped in `nt_hash64_t` for type safety. Game code obtains it via `nt_hash64_str("path")`. The `nt_hash` module provides centralized hashing for both builder and runtime. The registry uses resource_id to match assets across packs and resolve priority.

## 17.3 Generational handles

Game code receives `nt_resource_t` — a 32-bit handle encoding slot index (lower 16 bits) and generation (upper 16 bits). Generation detects stale handles within a single init/shutdown lifecycle. After shutdown, all handles are invalid — game code must re-request resources after reinit. Access functions (`nt_resource_get`, `nt_resource_is_ready`) validate generation before returning data.

Typed wrappers (MeshHandle, TextureHandle) live outside nt_resource — game code or future phases.

## 17.4 AssetMeta stability

**Unmount** removes asset entries (resource_id = 0) — slots are recycled for new packs. **Unload** (Phase 25) clears runtime handle/state but preserves metadata — enables fast reload without re-parsing.

## 17.5 NtAssetMeta

```c
typedef struct {
    uint64_t resource_id;    /* nt_hash64 value */
    uint32_t offset;         /* byte offset in pack blob */
    uint32_t size;           /* asset data size */
    uint32_t runtime_handle; /* resolved handle, 0 = none */
    uint16_t format_version; /* per-type binary format version */
    uint16_t pack_index;     /* index into packs[] */
    uint8_t  asset_type;     /* nt_asset_type_t */
    uint8_t  state;          /* nt_asset_state_t */
    uint8_t  is_dedup;       /* 1 = shares data with another asset in same pack */
    uint8_t  _pad;
    uint32_t meta_offset;    /* byte offset into pack's resident meta_data buffer (NT_NO_METADATA = no meta) */
} NtAssetMeta;
```

## 17.6 NtResourceSlot

```c
typedef struct {
    uint64_t resource_id;            /* nt_hash64 value */
    uint32_t runtime_handle;         /* current best resolved handle */
    uint16_t generation;             /* stale detection */
    int16_t  resolve_prio;           /* priority of current winner */
    uint8_t  asset_type;             /* nt_asset_type_t */
    uint8_t  state;                  /* nt_asset_state_t of resolved entry */
    uint16_t resolve_seq;            /* mount_seq of current winner (tiebreak) */
    uint16_t resolve_asset_idx;      /* index into assets[] of resolved winner */
    uint16_t prev_resolve_asset_idx; /* previous winner identity (change detection) */
    uint32_t prev_runtime_handle;    /* previous handle (detect re-activation) */
    void    *user_data;              /* per-slot auxiliary data (on_resolve/on_cleanup) */
} NtResourceSlot;
```

### 17.6.1 Resolve callbacks (on_resolve / on_cleanup)

Per-asset-type callbacks for auxiliary data that persists across pack stacking. Registered separately from activate/deactivate — asset types that don't use them pay nothing.

```c
typedef void (*nt_resolve_fn)(const uint8_t *data, uint32_t size, uint32_t runtime_handle, void **user_data);
typedef void (*nt_cleanup_fn)(void *user_data);

nt_resource_set_resolve_callbacks(asset_type, on_resolve, on_cleanup);
void *nt_resource_get_user_data(handle);
```

**on_resolve** fires in Phase D when the winner changes for a slot (different asset identity or same identity with a new runtime_handle after re-activation). Receives blob data, runtime_handle, and in/out `user_data` pointer. `data` may be NULL when the winner's pack blob is not resident (placeholder, virtual pack, or evicted blob). The data pointer is valid only for the duration of the call — callbacks must copy if needed.

**on_cleanup** fires when a slot loses its real winner (all packs unmounted) and during shutdown for remaining non-NULL user_data. `on_resolve` requires `on_cleanup` — registering resolve without cleanup is an assert.

Winner change detection uses two factors: `resolve_asset_idx` (winner identity) and `runtime_handle` (detect re-activation from context loss / invalidate). Placeholder substitution does not trigger on_resolve — placeholders are visual fallbacks, not real winners.

Callbacks must not call resource API (mount/unmount/request/step) — they fire during resolve iteration.

## 17.7 Virtual packs

Game code can create virtual packs to register runtime-created resources (procedural textures, generated meshes) into the registry. Virtual pack assets participate in priority stacking identically to file pack assets. Unmount clears registry entries but does not destroy resources — game owns them.

```c
nt_resource_create_pack(pack_id, priority);
nt_resource_register(pack_id, resource_id, asset_type, runtime_handle);
nt_resource_unregister(pack_id, resource_id);
```

## 17.8 Asset types

```c
typedef enum {
    NT_ASSET_MESH = 1,
    NT_ASSET_TEXTURE = 2,
    NT_ASSET_SHADER_CODE = 3,
    NT_ASSET_BLOB = 4,        /* generic binary data (game-defined) */
    NT_ASSET_FONT = 5,        /* font glyph data (Slug format) */
    NT_ASSET_ATLAS = 6,       /* atlas region metadata (vertices + UVs + origin) */
} nt_asset_type_t;
```

Additional types (material, audio) will be added as needed.

### NT_ASSET_FONT binary format

Builder produces font assets from TTF/OTF sources. Binary layout:

```
NtFontAssetHeader (16 bytes)
  magic:        u32   (0x544E4F46 "FONT")
  version:      u16   (2)
  glyph_count:  u16
  units_per_em: u16
  ascent:       i16
  descent:      i16   (negative)
  line_gap:     i16

NtFontGlyphEntry[glyph_count] (24 bytes each, sorted by codepoint for bsearch)
  codepoint:    u32
  data_offset:  u32   (byte offset from header start)
  advance:      i16
  bbox:         i16 x4 (x0, y0, x1, y1)
  curve_count:  u16
  kern_count:   u16
  _reserved:    u8 x2

Per-glyph data (at data_offset):
  NtFontKernEntry[kern_count] (4 bytes each, sorted by right_glyph_index)
    right_glyph_index: u16
    value:             i16
  Contour data (delta-encoded int16 coordinates, line/quadratic bitmask)
```

Runtime does not parse TTF. Glyph contours are delta-encoded quadratic Bezier curves (lines promoted to degenerate quadratics). At lookup time, contours are decoded into float control points, decomposed into horizontal bands, and uploaded to GPU textures for Slug-style vector rendering. Glyphs are cached with LRU eviction — not immutable once loaded.

### NT_ASSET_ATLAS binary format

Builder produces atlas assets from a set of sprite PNGs (or raw RGBA buffers). One atlas yields **two kinds of pack entries**: a single `NT_ASSET_ATLAS` blob with region metadata, plus N `NT_ASSET_TEXTURE` page entries (named `<atlas>/tex0`, `<atlas>/tex1`, …). Runtime keeps a 1:N relationship — one metadata blob references N textures.

Binary layout (`shared/include/nt_atlas_format.h`, packed, **v3**):

```
NtAtlasHeader (28 bytes)
  magic:               u32  (0x534C5441 "ATLS")
  version:             u16  (3)
  region_count:        u16  (one entry per source sprite)
  page_count:          u16  (number of texture pages)
  _pad:                u16
  vertex_offset:       u32  (byte offset from header start)
  total_vertex_count:  u32
  index_offset:        u32  (byte offset from header start)
  total_index_count:   u32

texture_resource_ids[page_count]: u64
  Each entry is nt_hash64_str("<atlas_name>/tex<N>") matching the
  page texture's resource_id in the same pack.

NtAtlasRegion[region_count] (36 bytes each)
  name_hash:      u64   (xxh64 of region name)
  source_w:       u16   (original image width in pixels, pre-trim)
  source_h:       u16   (original image height in pixels, pre-trim)
  trim_offset_x:  i16   (pixels stripped from the left edge during alpha trim)
  trim_offset_y:  i16   (pixels stripped from the top edge)
  origin_x:       f32   (pivot X, normalized over source_w — 0.5 = centre, 1.0 = right edge.
                         Values outside [0, 1] are allowed for off-frame pivots. Source-space
                         NOT trim-space — stable across animation frames with varying trim bounds.)
  origin_y:       f32   (pivot Y, normalized over source_h. Same semantics.)
  vertex_start:   u32   (index into vertex array — u32 in v3, was u16 in v2)
  index_start:    u32   (index into the index array — u32 in v3, was u16 in v2)
  vertex_count:   u8    (vertices for this region; ≤ max_vertices)
  page_index:     u8    (which texture page)
  transform:      u8    (3-bit D4 mask: bit0=flipH, bit1=flipV, bit2=diagonal)
  index_count:    u8    (triangle indices for this region; ≤ 255)

NtAtlasVertex[total_vertex_count] (8 bytes each, at vertex_offset)
  local_x:   i16  (corner X in trim-rect local space, 0..trim_w.
                   Polygon vertices use corner coordinates, not pixel centres.
                   Source-image pos: local_x + trim_offset_x
                   Pivot-relative:   (local_x + trim_offset_x) - origin_x * source_w)
  local_y:   i16  (corner Y in trim-rect local space, 0..trim_h. Same semantics.)
  atlas_u:   u16  (normalized 0..65535 over atlas page width)
  atlas_v:   u16  (normalized 0..65535 over atlas page height)

uint16[total_index_count] (at index_offset)
  Triangle list, indices local per region (0 .. vertex_count-1).
  Runtime offsets them by vertex_start when building GPU buffers.
```

Runtime is intentionally trivial: `mmap` the blob, read header, slice arrays. No parsing, no allocation. UVs are pre-normalized, triangles are pre-built (fan triangulation for convex regions, Clipper2 CDT for concave). Duplicate sprites share `vertex_start`/`index_start`; the u32 cursors let a single atlas carry more than 64K vertices/indices, which v2's u16 cursors could not.

**v2 → v3 changes:**

- `NtAtlasRegion.rotated` renamed to `transform` (same 3-bit D4 flags; name reflects its real meaning — it is a transform mask, not a bool).
- `NtAtlasRegion.vertex_start` and `.index_start` widened from u16 to u32. Large atlases with many unique high-vertex sprites overflow 65535 cumulative vertices/indices, and the builder used to assert on that limit.
- `NtAtlasRegion` grew from 32 bytes to 36 bytes (enforced by `_Static_assert` in `shared/include/nt_atlas_format.h`).
- `NtAtlasHeader` layout is unchanged. Only `version` bumps 2 → 3.

## 17.9 Placeholder policy

Texture-only placeholder: if a texture resource is not READY, `nt_resource_step()` resolves the placeholder resource_id and substitutes its handle. Non-texture resources return handle 0 when not ready — game code checks `nt_resource_is_ready()`.

```c
// Placeholder is a regular resource (e.g. from a virtual pack or base pack)
nt_resource_set_placeholder_texture(nt_hash64_str("textures/placeholder.png"));
```

The function automatically requests a slot for the placeholder resource_id if one does not exist. Placeholder participates in the same resolve system — if the placeholder resource itself is not READY, no substitution occurs.

## 17.10 nt_hash -- Identity Hashing

`nt_hash` provides xxHash (XXH32/XXH64) hashing in 32-bit and 64-bit widths. Used for resource identity (64-bit) and attribute/pack naming (32-bit). Both builder and runtime link this module -- single source of truth for hash computation. xxHash chosen over FNV-1a for superior avalanche properties (critical for open-addressing hash maps) and higher throughput on WASM.

Type-safe wrappers prevent accidental mixing of raw integers with hash values:

```c
typedef struct { uint32_t value; } nt_hash32_t;
typedef struct { uint64_t value; } nt_hash64_t;
```

API:

```c
nt_hash32_t nt_hash32(const void *data, uint32_t size);
nt_hash64_t nt_hash64(const void *data, uint32_t size);

static inline nt_hash32_t nt_hash32_str(const char *s);
static inline nt_hash64_t nt_hash64_str(const char *s);
```

Debug label system for hash-to-string reverse lookup (compile-time toggle `NT_HASH_LABELS`):

```c
void nt_hash_register_label64(nt_hash64_t hash, const char *label);
const char *nt_hash64_label(nt_hash64_t hash);
```

CRC32 remains in `shared/` for pack data checksum -- different purpose (error detection vs identity hashing).

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
typedef enum {
    NT_ASSET_STATE_REGISTERED = 0, /* meta exists, data not loaded */
    NT_ASSET_STATE_LOADING,        /* being activated (GPU upload etc.) */
    NT_ASSET_STATE_READY,          /* runtime handle valid, usable */
    NT_ASSET_STATE_FAILED,         /* error, permanent, no retry */
} nt_asset_state_t;
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
  → parses header/manifest (NTPACK format, direct struct read)
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

Current NtPackMeta (Phase 24 — registry only):

```c
typedef struct {
    uint32_t pack_id;     /* nt_hash32 value of pack name/path */
    int16_t  priority;    /* higher = wins on conflict */
    uint8_t  pack_type;   /* NT_PACK_FILE or NT_PACK_VIRTUAL */
    uint8_t  mounted;     /* 1 if mounted, 0 if slot available */
    const uint8_t *blob;  /* loaded pack data */
    uint32_t blob_size;   /* size of loaded blob */
} NtPackMeta;
```

Phase 25 will extend with PackState (NONE → REQUESTED → LOADED → READY → FAILED), progress tracking (bytes_received/bytes_total), and url for async web loading.

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

Each NtResourceSlot stores `resolve_prio` and `resolve_pack` of the current winner. When an individual asset becomes READY, the loader can compare priority directly against the slot — O(1) activation without full rebuild. Full rebuild (`needs_resolve`) is only needed for mount/unmount/set_priority.

## 18.8 Retry policy

1-2 retries with exponential backoff. After retries fail: PackState = FAILED, log error, game code decides response (show error, retry later).

## 18.9 Memory note

Peak memory during loading = 2x pack size (JS fetch buffer + WASM heap copy). For packs in the low megabytes range this is acceptable.

---

# 19. Pack Format (NTPACK)

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
│ NtPackHeader (32 bytes, packed)       │
│   magic: uint32     "NPAK"           │
│   meta_count: uint32                 │
│   version: uint16   NT_PACK_VERSION  │
│   asset_count: uint16                │
│   header_size: uint32  ← data start  │
│   total_size: uint32                  │
│   checksum: uint32     ← CRC32       │
│   meta_offset: uint32  ← meta start  │
│   _pad: uint32      (8-byte align)   │
├──────────────────────────────────────┤
│ NtAssetEntry[0] (24 bytes, packed)    │
│   resource_id: uint64                 │
│   offset: uint32  ← from file start  │
│   size: uint32                        │
│   format_version: uint16              │
│   asset_type: uint8                   │
│   _pad: uint8                         │
│   meta_offset: uint32  ← per-asset   │
├──────────────────────────────────────┤
│ NtAssetEntry[1..N-1]                  │
│   ...                                 │
╞══════════════════════════════════════╡
│ [padding to 8-byte alignment]         │
│ [asset 0 binary data]                 │
│ [asset 1 binary data]                 │
│ ...                                   │
│ [asset N-1 binary data]               │
╞══════════════════════════════════════╡
│ [meta section] (optional)             │
│   NtMetaEntryHeader + payload ...     │
│   grouped by resource_id              │
└──────────────────────────────────────┘
```

Assets aligned to 4 bytes (NT_PACK_ASSET_ALIGN). Header/entries region aligned to 8 bytes (NT_PACK_DATA_ALIGN) before data start. Meta section appended after asset data, covered by CRC32. Resident copy made at parse time (survives blob eviction).

## 19.2.1 Version policy

No backwards compatibility. Runtime asserts `version == NT_PACK_VERSION`. Old packs must be rebuilt when format changes. This is intentional: the engine is in active development, and maintaining backwards compat for a format that changes frequently adds complexity without benefit. Builder and runtime always agree on version.

## 19.2.2 Metadata section

Optional section after asset data. Contains variable-length entries (NtMetaEntryHeader + payload) grouped by resource_id. Header-level `meta_offset` points to section start; per-asset `meta_offset` points to first entry for that asset. Used for game-defined metadata (tags, material bindings, custom properties). AABB is not metadata — it lives in NtMeshAssetHeader as inherent mesh data.

```c
NtMetaEntryHeader (20 bytes, packed):
    uint64_t resource_id;  /* which asset */
    uint64_t kind;         /* hash64 of metadata type name */
    uint32_t size;         /* payload bytes (max 256) */
    /* uint8_t data[size] follows immediately */
```

Query: `nt_resource_get_meta(handle, nt_hash64_str("tag").value, &size)` — returns pointer to resident memory, NULL if absent.

## 19.3 Runtime parsing

```c
// Pseudocode — see nt_resource.c for actual implementation
void parse_pack(const uint8_t* blob, uint32_t blob_size) {
    const NtPackHeader* h = (const NtPackHeader*)blob;

    NT_ASSERT(h->magic == NT_PACK_MAGIC);
    NT_ASSERT(h->version == NT_PACK_VERSION);  /* no backwards compat */

    const NtAssetEntry* entries = (const NtAssetEntry*)(blob + sizeof(NtPackHeader));

    for (uint16_t i = 0; i < h->asset_count; i++) {
        NtAssetMeta* meta = asset_alloc();
        meta->resource_id = entries[i].resource_id;
        meta->offset = entries[i].offset;
        meta->size = entries[i].size;
        /* Convert per-asset meta_offset from absolute to meta_data-relative */
        meta->meta_offset = (entries[i].meta_offset != 0)
            ? entries[i].meta_offset - h->meta_offset
            : NT_NO_METADATA;
    }

    /* Copy meta section to resident memory (survives blob eviction) */
    if (h->meta_count > 0 && h->meta_offset != 0) {
        uint32_t meta_size = blob_size - h->meta_offset;
        pack->meta_data = malloc(meta_size);
        memcpy(pack->meta_data, blob + h->meta_offset, meta_size);
    }
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

Builder is a standalone native binary (C17, with vendored C++ for Basis Universal encoder behind extern "C"). Rules are written in code.

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
add_font(const char* path);

add_meshes(const char* pattern);
add_textures(const char* pattern);
add_shaders(const char* pattern);
add_materials(const char* pattern);
add_audios(const char* pattern);
add_fonts(const char* pattern);

/* Atlas: groups N source sprites into 1 metadata blob + M texture pages.
 * Per-sprite opts carry the name override and the pivot point (NULL = defaults). */
begin_atlas(const char* name, const nt_atlas_opts_t* opts);
atlas_add(const char* path, const nt_atlas_sprite_opts_t* opts);
atlas_add_raw(const uint8_t* rgba, uint32_t w, uint32_t h, const nt_atlas_sprite_opts_t* opts);
atlas_add_glob(const char* pattern, const nt_atlas_sprite_opts_t* opts);
end_atlas(void);
```

Prefer typed wildcard functions over one untyped `add_files()`. Atlas uses a `begin/add*/end` pattern because one atlas requires its full sprite set before packing can start — this is the only place in the API where multi-call grouping is required.

## 23.5 Builder stages

1. source assets
2. import
3. validation
4. conversion to runtime format
5. pack placement with alignment
6. manifest generation (embedded in pack header)
7. write NTPACK binary

## 23.6 Builder validation

Builder must check: references between assets, resource types, mesh/material/shader compatibility, required attributes, runtime format generation correctness, audio format validity.

## 23.7 Asset ID codegen

`finish_pack` generates a `.h` header alongside each `.ntpack` with typed compile-time constants for every asset:

```c
/* Auto-generated by nt_builder -- do not edit */
#define ASSET_MESH_MESHES_CUBE_GLB   ((nt_hash64_t){0x...ULL}) /* meshes/cube.glb */
#define ASSET_SHADER_SHADERS_MESH_VERT ((nt_hash64_t){0x...ULL}) /* shaders/mesh.vert */
```

Rules:
- Constants are typed compound literals `((nt_hash64_t){...})` -- work with `nt_resource_request` without casts.
- Hash values match `nt_hash64_str(normalized_path)` at runtime. The header is the single source of truth.
- Identifier format: `ASSET_{TYPE}_{PATH}` where path includes file extension (`.vert`, `.frag`, `.glb` etc.) to avoid collisions between same-stem files. Slashes, dots, dashes become underscores, uppercased.
- Entries sorted alphabetically within each type group (MESH, TEXTURE, SHADER, BLOB) for deterministic, diffable output.
- `register_labels()` function under `#if NT_HASH_LABELS` registers all paths for debug hash lookup.
- Identifier collisions (two assets producing the same `#define` name) are a fatal builder error.

## 23.8 Combined headers (multi-pack projects)

Projects with multiple packs merge per-pack headers into one combined header after all packs are built:

```c
/* Each finish_pack generates a per-pack .h (e.g. core.h, textures.h) */
nt_builder_set_header_dir(ctx, "examples/myproject/generated");
nt_builder_finish_pack(ctx);

/* After all packs: merge into one combined header */
const char *headers[] = { "generated/core.h", "generated/textures.h" };
nt_builder_merge_headers(headers, 2, "generated/assets.h");
```

Rules:
- `merge_headers` reads per-pack `.h` files, deduplicates by hash, sorts, writes one combined header.
- No runtime state needed during pack building — merge operates on already-generated files.
- Game code includes the single combined header, not per-pack headers.
- Per-pack headers are still generated for diagnostics and per-pack diffing.
- `set_header_dir` controls where headers are written. Convention: `examples/{project}/generated/` in source tree so headers are visible in IDE and version control.

## 23.9 Generated headers in version control

Generated asset headers are committed to git. This is intentional:

- Output is deterministic (sorted by name, stable hashes). File only changes when assets are added, removed, or renamed.
- Git diff on the header shows exactly which assets changed between commits -- acts as an asset changelog.
- New contributors can build and run without first running the builder.
- Header files are small (one line per asset) and compress well in git.

## 23.10 Builder cache

Content-addressed encode cache. Opt-in via `nt_builder_set_cache_dir(ctx, path)`. Skips re-encoding unchanged assets on repeat builds.

**Cache key:** `decoded_hash` (xxHash64 of decoded source bytes) × `opts_hash` (hash of encode-affecting options + `NT_BUILDER_VERSION`).

**Storage:** flat directory of `.bin` files named `{decoded_hash}_{opts_hash}.bin`. No index file, no subdirectories. Cached data is raw encoded asset bytes (post-encode, pre-pack-header).

**Pipeline order:** early dedup → cache lookup → encode → cache store. Dedup runs first so duplicates never hit cache. Cache stores only unique encoded results.

**Invalidation:**
- Source data changes → different `decoded_hash` → automatic miss.
- Encode options change (format, compression, quality) → different `opts_hash` → automatic miss.
- Encoder logic changes → bump `NT_BUILDER_VERSION` → all `opts_hash` values change → full cache miss.
- Manual: delete cache directory contents.

**Safety:** write-to-temp + atomic rename. Cache failures (read/write) fall through to normal encode — never break the build.

**Build summary** reports per-asset cache status (cached / miss-new / miss-opts) and aggregate hit/miss counts.

## 23.11 Atlas builder

The atlas builder packs a set of sprite images into one or more atlas pages and emits compact runtime metadata (`NT_ASSET_ATLAS`, see §17.8). It is the only "grouping" producer in the builder — every other importer is single-asset.

**API shape (begin/add*/end):**

```c
nt_atlas_opts_t opts = nt_atlas_opts_defaults();  /* atlas-level: packer, format, etc. */
opts.max_size = 2048;
opts.max_vertices = 8;
opts.shape = NT_ATLAS_SHAPE_CONCAVE_CONTOUR;

nt_builder_begin_atlas(ctx, "hero", &opts);

/* Idle frames use the default centre pivot (0.5, 0.5). */
nt_builder_atlas_add_glob(ctx, "assets/sprites/hero/idle_*.png", NULL);

/* Walk cycle: override the pivot to bottom-centre for every matched frame. */
nt_atlas_sprite_opts_t walk = nt_atlas_sprite_opts_defaults();
walk.origin_y = 1.0F;  /* feet at bottom edge */
nt_builder_atlas_add_glob(ctx, "assets/sprites/hero/walk_*.png", &walk);

/* Single sprite with a custom name override. */
nt_builder_atlas_add(ctx, "assets/sprites/hero/portrait.png",
                     &(nt_atlas_sprite_opts_t){
                         .name = "hero_portrait",
                         .origin_x = 0.5F, .origin_y = 0.5F,
                     });

nt_builder_end_atlas(ctx);
```

`begin_atlas` opens an atlas state on the context. Subsequent `atlas_add*` calls feed sprites in — each takes an optional `nt_atlas_sprite_opts_t*` carrying the per-sprite name override and pivot point (pass `NULL` for the centred-pivot default). `end_atlas` runs the full pipeline and registers entries. Nested atlases are not allowed (asserts).

### 23.11.1 Pipeline

`end_atlas` runs ten stages in order:

1. **alpha_trim** — extract alpha plane, find tight bbox per sprite (rejects fully transparent inputs).
2. **cache_check** — compute atlas-level cache key (sorted sprite hashes + serialized opts + version), try loading cached placement+pages.
3. **dedup** — hash + byte-level compare to find identical sprites; duplicates share `vertex_start`/`index_start` in the final blob.
4. **geometry** — for each unique sprite: build binary mask, optional morphological closing for disjoint components, contour trace, multi-strategy simplification (RDP / perpendicular distance / bbox / convex hull — pick lowest estimated final area), Clipper2 inflate by `extrude + padding/2`, post-verify pixel coverage with fallback to bbox.
5. **tile_pack** — call `vector_pack` (NFP packer, see below) to assign each unique sprite to a page and (x, y) position.
6. **compose** — blit trimmed pixels onto page buffers, run AABB edge-extrude only when packing uses rectangles; in polygon mode, require `extrude=0` and rely on `padding`.
7. **debug_png** — optional outline visualization (when `opts.debug_png`).
8. **cache_write** — persist placement+pages for next build.
9. **serialize** — pack `NtAtlasHeader + texture_resource_ids + regions + vertices + indices` into one blob, register as `NT_ASSET_ATLAS`.
10. **register** — add `NT_ASSET_TEXTURE` entries for each page texture, populate region codegen entries.

Stages 5–8 are skipped on cache hit; serialize/register always run.

### 23.11.2 Vector packer

The packer is **NFP/Minkowski-based** (`nt_builder_atlas_vpack.c`). For each candidate position the incoming polygon is tested against the union of No-Fit Polygons of all already-placed sprites. Properties:

- **Sub-pixel exact** — no quantization to a tile grid.
- **Concave-aware** — Clipper2 `MinkowskiSum + Union(NonZero)` produces multi-ring NFPs for concave inputs; rings are forbidden zones.
- **8 D4 orientations** — flipH, flipV, diagonal flip and combinations. Identity-equivalent orientations are deduplicated.
- **NFP cache** — 8-way set-associative seqlock cache keyed by `(placed_shape_hash, incoming_shape_hash)`. Lock-free reads via version counter, CAS writes. Same shape pair across different sprites reuses the cached NFP.
- **Parallel build** — when `nt_builder_set_threads(ctx, N)` is called, NFP construction and candidate scanning run on a thread pool. Per-thread stat accumulators merge into global stats deterministically.
- **Page growth** — sprites that don't fit allocate a new page (up to `ATLAS_MAX_PAGES = 64`); new pages start with the same dimensions as the first.

### 23.11.3 Atlas options

```c
/* Silhouette mode for atlas packing. Ordered by cost and density. */
typedef enum {
    NT_ATLAS_SHAPE_RECT = 0,             /* AABB trim rect — fastest, worst pack density */
    NT_ATLAS_SHAPE_CONVEX_HULL = 1,      /* convex hull of opaque pixels — no contour trace */
    NT_ATLAS_SHAPE_CONCAVE_CONTOUR = 2,  /* concave contour + multi-strategy — densest, slowest */
} nt_atlas_shape_t;

typedef struct {
    const nt_tex_compress_opts_t *compress;  /* NULL = raw RGBA */
    nt_texture_pixel_format_t format;        /* RGBA8 default */
    uint32_t max_size;       /* max atlas page dimension (default 2048) */
    uint32_t padding;        /* extra spacing between sprites after extrude (default 2) */
    uint32_t margin;         /* atlas edge margin (default 0) */
    uint32_t extrude;        /* AABB edge pixel duplication count (default 0; must be 0 unless shape == NT_ATLAS_SHAPE_RECT) */
    uint8_t alpha_threshold; /* alpha >= threshold = opaque (default 1) */
    uint8_t max_vertices;    /* max polygon vertices per region (default 8, hard cap 16) */
    nt_atlas_shape_t shape;  /* silhouette mode (default NT_ATLAS_SHAPE_CONCAVE_CONTOUR) */
    bool allow_transform;    /* try 8 D4 orientations (4 rotations × 2 flips; default true) */
    bool power_of_two;       /* round atlas dims to POT (default true) */
    bool debug_png;          /* write debug atlas page PNGs (default false) */
    bool premultiplied;      /* premultiply RGB by alpha during texture encode (default true) */
} nt_atlas_opts_t;
```

**Silhouette modes (`nt_atlas_shape_t`):**

- `NT_ATLAS_SHAPE_RECT` — 4-vertex AABB of the trim rect. No contour tracing, no hull, no RDP. Fastest geometry stage; lowest pack density because the packer cannot slot concave notches between sprites. The only mode where `extrude > 0` is legal.
- `NT_ATLAS_SHAPE_CONVEX_HULL` — convex hull of opaque pixels via `binary_build_convex_polygon`, simplified to `max_vertices`. Skips morphological closing, contour tracing, RDP, and the 4-strategy pipeline entirely. Good compromise when sprites are roughly convex: noticeably denser than `RECT` without paying the full concave cost.
- `NT_ATLAS_SHAPE_CONCAVE_CONTOUR` (default) — traces the concave alpha boundary, runs RDP plus a multi-strategy simplification (RDP / perpendicular distance / bbox / convex hull), Clipper2-inflates the chosen polygon, and post-verifies pixel coverage. Internally falls back to `binary_build_convex_polygon` for degenerate inputs (disjoint components that morphological closing cannot merge, degenerate contours, Clipper2 inflate failure). Densest packing, highest cost.

**Premultiplied alpha (default):** atlas pages are encoded through the regular texture pipeline with `premultiplied = true`, which writes `RGB' = (RGB * A + 127) / 255` into the page before `strip_channels` (RAW path) or `nt_basisu_encode` (BASIS path). The resulting texture sets `NT_TEXTURE_FLAG_PREMULTIPLIED` in `NtTextureAssetHeader.flags`, and the runtime must draw with `(ONE, ONE_MINUS_SRC_ALPHA)` blending. This is what keeps NFP-packed sprites free of dark fringes at sub-pixel clearance: `(0,0,0,0)` gap pixels are the identity for premultiplied blending, so bilinear filtering at sprite edges stays correct. Setting `premultiplied = false` logs a warning and is only valid for NEAREST-filtered or fully-opaque atlases; combining it with a non-RGBA8 `format` is a hard assert.

**Hard limits:**
- `max_vertices ≤ 16`. NFP buffers are stack-sized for `nA + nB ≤ 32`.
- Per-region `index_count` is `uint8_t` → ≤ 255 indices per region. With `max_vertices ≤ 16` an ear-clipped/fan triangulation produces at most `(16 - 2) * 3 = 42` indices, so one byte is sufficient.
- Per-atlas `vertex_start` / `index_start` are `uint32_t` (v3). v2 used `uint16_t` and asserted at ~65K cumulative entries — v3 lifts that limit.

### 23.11.4 Per-sprite options (`nt_atlas_sprite_opts_t`)

Each `atlas_add` / `atlas_add_raw` / `atlas_add_glob` call accepts an optional `nt_atlas_sprite_opts_t*`. `NULL` picks the defaults (centre pivot, name derived from path).

```c
typedef struct {
    const char *name;   /* NULL = derive from path (atlas_add/glob); required for atlas_add_raw */
    float origin_x;     /* pivot X, normalized over source_w (default 0.5) */
    float origin_y;     /* pivot Y, normalized over source_h (default 0.5) */
} nt_atlas_sprite_opts_t;
```

**Pivot semantics:**
- Normalized over the **source image** dimensions (not the trimmed rect). Default `(0.5, 0.5)` = image centre.
- **Values outside `[0, 1]` are allowed** — pivots may lie outside the frame for weapons, effects, or motion-stabilised sprites. Must be finite (`isfinite()` asserted; NaN/inf is caller bug).
- Source-space (not trim-space) is chosen so frame-by-frame animations with varying per-frame trim bounds have stable pivots across frames. A walk cycle with `origin_y = 0.9375` sits on the same source-image pixel row regardless of how much whitespace the alpha trim removed from each frame.

**Glob rule:** `atlas_add_glob` asserts `opts->name == NULL` — a single name cannot apply to N matched files without hash collisions. Each matched file derives its own name from its path, and the `origin_x/y` fields propagate to all of them. For per-file name overrides within a glob, fall back to calling `nt_builder_glob_iterate` directly with a custom callback that calls `nt_builder_atlas_add` per match.

**Dedup + different pivots:** adding the same pixel-identical sprite twice with different `origin_x/y` produces **two separate regions** that **share** `vertex_start` and `index_start` in the blob. The dedup pass matches on pixel hash + byte-level pixel compare (origin is not considered), so the geometry/pixel data is stored once; each logical region stores its own pivot. This is the cheap path for "same sprite, different anchor" (e.g. icon referenced with centre pivot in menu vs bottom-centre in HUD).

**Zero-init footgun:** C99 designated-initialiser compound literals (`&(nt_atlas_sprite_opts_t){.origin_y = 1.0F}`) zero-init unset fields — so `origin_x` becomes `0.0`, not the default `0.5`. Always start from `nt_atlas_sprite_opts_defaults()` for partial overrides, or set every field explicitly in the literal.

### 23.11.5 Atlas cache

Separate from the per-asset builder cache (§23.10) because atlas placement is a global decision over the whole sprite set.

**Cache key:** `xxh64(sorted(decoded_hashes) + serialized_opts + ATLAS_CACHE_KEY_VERSION + compress_settings)`. Sorted sprite hashes make the key independent of `atlas_add` order.

**Storage:** one `atlas_<key>.bin` file per cache hit, containing the placement table and the composed page pixels. On hit, the pipeline skips pack/compose/debug_png/cache_write entirely.

**Invalidation:** any change to source pixels, opts, or `ATLAS_CACHE_KEY_VERSION` produces a fresh key. The version constant is bumped when the packer's behavior changes in a way that would silently produce different output.

**Failure mode:** atomic temp+rename writes; read/write failures fall through to a fresh build, never break it.

---

# 24. Logging, Errors, Debugging

## 24.1 Logging levels

- INFO
- WARN
- ERROR
- ASSERT
- PANIC

## 24.2 Assert policy

Asserts are contracts, not error handling. A failed assert means the program is broken beyond recovery — continuing would mask bugs.

- **NT_ASSERT** — single macro, three compile-time modes via `NT_ASSERT_MODE`:
  - `0 (OFF)` — `((void)0)`, zero overhead. Available via CMake override (`-DNT_ASSERT_MODE=0`) for final production builds where binary size is critical.
  - `1 (TRAP)` — `__builtin_trap()`, no strings, minimal binary impact. **Release default.**
  - `2 (FULL)` — hookable handler with `expr/file/line` strings. **Debug default.** Tests use the handler to catch and verify assert failures via `setjmp`/`longjmp`.
- Release ships with TRAP (1): contract violations crash immediately instead of continuing with corrupted state. No string bloat, no handler overhead — just a single branch + trap instruction per assert.
- Never use asserts for conditions that can legitimately occur at runtime (missing files, user input, network errors) — those are error handling (see below).

## 24.3 Error policy

### Fatal

- backend init failure
- unsupported critical format version
- impossible startup state

### Recoverable

- missing texture → placeholder
- material mismatch → placeholder material
- resource load fail → asset state failed + log
- audio decode failure → clip state failed + log

## 24.4 Debug overlay

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
7. NTPACK format: pack parsing + asset access
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
7. Builder is standalone native binary (C17 + vendored C++ behind extern "C")
8. Builder rules are code
9. Runtime formats are custom binary
10. Custom pack format (NTPACK) — flat binary, no ZIP
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
- ~~exact sprite asset format~~ → resolved: `NT_ASSET_ATLAS` (§17.8) builder-side. Runtime sprite renderer + `SpriteComponent` consumer is still pending — atlas blob is produced and validated end-to-end but not yet consumed by a runtime sprite module.
- sprite animation system
- ~~exact text rendering strategy and string pool design~~ → resolved: Slug-based GPU vector rendering (§17.8 NT_ASSET_FONT), font module API (§9.5)
- camera component/structure definition
- whether some renderer-specific caches are worth adding later
- whether pack hot-reload becomes needed
- ~~whether asset build database is needed immediately or later~~ → resolved: content-addressed builder cache (§23.10)
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
    ├─ loads runtime assets from NTPACK packs (async)
    ├─ provides render backend (WebGL 2)
    ├─ provides input + platform services
    └─ provides audio playback

Builder
    ├─ imports source assets
    ├─ validates compatibility
    ├─ converts to runtime formats
    ├─ builds NTPACK packs
    └─ embeds manifest in pack header
```

This gives: explicit control, minimal runtime complexity, strong builder/runtime split, web-first practicality, platform-agnostic game code, future room for WebGPU and desktop platforms without redesigning the core.
