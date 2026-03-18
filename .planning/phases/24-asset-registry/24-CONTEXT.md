# Phase 24: Asset Registry - Context

**Gathered:** 2026-03-17
**Status:** Ready for planning

<domain>
## Phase Boundary

Runtime module (nt_resource) that parses NEOPAK packs, registers asset metadata by ResourceId, and provides typed handle-based access to assets with pack stacking and priority override. Phase does NOT include async loading (JS fetch bridge), GPU resource activation (mesh/texture/shader creation), or material system. Those are Phase 25+.

</domain>

<decisions>
## Implementation Decisions

### Pack stacking & priority
- Explicit numeric priority at mount: `nt_resource_mount(path, priority)` — higher number wins
- store ALL AssetMeta entries for the same ResourceId from different packs
- Resolve returns the highest-priority READY entry; falls back to lower-priority READY
- If no READY entry exists — placeholder (texture) or skip (mesh/shader)
- Equal priority tiebreaker: last mounted wins
- `set_priority(pack_id, new_priority)` to change priority at runtime
- MAX_PACKS 16 (overridable #define), MAX_ASSETS 4096 (overridable #define)

### Mount / unmount semantics
- **Mount** — register pack metadata + all AssetMeta entries in registry. Blob not loaded yet.
- **Unmount** — full removal: destroy GPU resources (file packs only), remove AssetMeta entries, fallback to lower-priority pack or placeholder. Free blob if loaded.
- **Load/unload blob** — Phase 25 concern. Pack stays mounted even if blob is freed from memory. Assets can be re-loaded later by re-fetching the blob.
- On-demand loading: game requests resource → if not loaded, triggers load in Phase 25

### Architecture: AssetMeta + ResourceSlot
- **AssetMeta[MAX_ASSETS]** — internal array of all registered assets from all packs. One struct for all types:
  ```
  { resource_id, asset_type, state, format_version, pack_index, offset, size, runtime_handle (uint32) }
  ```
- **ResourceSlot[MAX_SLOTS]** — one slot per unique ResourceId requested by the game. Holds current best GFX handle.
  ```
  { resource_id, gfx_handle (uint32), generation, asset_type, state }
  ```
- Game interacts with ResourceSlots via `nt_resource_t` handles. AssetMeta is internal.

### Resource handle (nt_resource_t)
- `nt_resource_t` = {slot_index + generation} — same pattern as entity/gfx handles
- `nt_resource_request(resource_id, type)` — one-time: creates slot, returns handle, triggers loading
- `nt_resource_get(nt_resource_t)` — per-frame: reads slot[index].gfx_handle, O(1) array access
- Generation for stale detection: if slot freed/reused, old handle is stale
- `nt_resource_is_ready(nt_resource_t)` — check if resource is loaded and available
- `nt_resource_get_state(nt_resource_t)` — get current AssetState (REGISTERED/LOADING/READY/FAILED)

### Per-frame step
- `nt_resource_step()` called once per frame BEFORE update/render
- Processes change queues (not full array scan): pending loads, new requests, state transitions
- Updates all affected ResourceSlots — after step(), all slots are current for the rest of the frame
- Invariant: step() is the ONLY place that modifies resolved resource slots
- Rate-limited activation: #define NT_RESOURCE_STEP_BUDGET (assets per frame), overridable
- Typical frame with no changes: step() checks queues, empty, returns — O(1)

### API style
- Core API accepts uint32_t hashes only — no string parameters
- `nt_resource_hash(const char *name)` — FNV-1a utility for runtime hashing (same algorithm as builder)
- Builder prints resource_id hashes; game uses them as #define constants
- Typed wrappers (get_texture returning nt_texture_t) live outside nt_resource (Phase 25 or game code)

### Two-level registration (virtual packs)
- Runtime-created resources live in virtual packs: `nt_resource_create_pack(pack_id_hash, priority)`
- `nt_resource_register(pack_id, resource_id, asset_type, gfx_handle)` — adds to virtual pack, state=READY immediately
- Virtual packs participate in stacking like file packs — priority determines override
- pack_id = FNV-1a hash of pack name (same as resource_id pattern)
- Pack type enum: NT_PACK_FILE / NT_PACK_VIRTUAL — distinguishes ownership behavior
- File pack ownership: registry creates GPU resources → registry destroys on unmount
- Virtual pack ownership: game creates GPU resources → game destroys. Unmount only removes from registry.
- Explicit cleanup: game must unregister before destroying GPU handle (or unmount virtual pack)
- Context loss: registry marks virtual pack resources as FAILED. Game responsible for recreation.

### Placeholder policy
- Placeholder texture (1x1): magenta (debug, #ifndef NDEBUG) / white (release, NDEBUG)
- Created at nt_resource_init()
- Mesh/shader not ready → skip entity (render pipeline checks is_ready)
- FAILED state: log error once, state is permanent (no retry), game checks get_state() to react

### Module structure
- New module: engine/resource/ (nt_resource)
- Type-agnostic: stores uint32_t handles, no dependency on nt_gfx
- Dependencies: nt_core (types, assert), nt_shared (format headers, CRC32)
- NEOPAK parser lives inside nt_resource (~30 lines, too small for separate module)
- Parser validates: magic, version, header_size, total_size, CRC32

### Claude's Discretion
- Internal data structure for AssetMeta lookup (linear scan, sorted, hash table)
- ResourceSlot pool sizing and free list implementation
- Exact PackMeta struct fields and layout
- Internal queue implementation for step() processing
- Memory allocation strategy for pack metadata
- FNV-1a implementation placement (inline in header vs .c file)
- Whether to sort AssetMeta for faster "find best READY" queries

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### Resource system architecture
- `docs/neotolis_engine_spec_1.md` S17 — Resource system: ResourceId, AssetRef, AssetMeta, typed handles, placeholder policy
- `docs/neotolis_engine_spec_1.md` S18 — Async loading: pack state machine, asset state machine, loading flow, activation strategy
- `docs/neotolis_engine_spec_1.md` S19 — NEOPAK format: binary layout, runtime parsing pseudocode, asset data access

### Shared format headers (Phase 20 output)
- `shared/include/nt_pack_format.h` — NtPackHeader (24 bytes), NtAssetEntry (16 bytes), alignment constants, asset type enum
- `shared/include/nt_mesh_format.h` — NtMeshAssetHeader, stream types
- `shared/include/nt_texture_format.h` — NtTextureAssetHeader, pixel format
- `shared/include/nt_shader_format.h` — NtShaderCodeHeader, shader stage
- `shared/include/nt_crc32.h` — CRC32 for pack checksum validation

### Existing patterns to follow
- `engine/graphics/nt_gfx_internal.h` — Pool pattern: nt_gfx_pool_t with slot/generation, free queue stack
- `engine/entity/nt_entity.h` — Entity handle pattern: index + generation in uint32_t
- `engine/graphics/nt_gfx.h` — GFX handle types (nt_texture_t, nt_buffer_t etc.) for typed wrapper reference

### Builder output (Phase 23)
- `tools/builder/nt_builder.h` — nt_builder_hash() FNV-1a function (runtime needs same algorithm)

### Requirements
- `.planning/REQUIREMENTS.md` — REG-01 through REG-06, LOAD-04

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets
- `engine/graphics/nt_gfx_internal.h`: Pool pattern (nt_gfx_pool_t) — slot/generation handles, free queue stack. Registry's ResourceSlot pool can follow same pattern.
- `shared/include/nt_pack_format.h`: NtPackHeader + NtAssetEntry structs — parser reads these directly from blob.
- `shared/include/nt_crc32.h`: CRC32 utility — parser validates pack checksum.
- `tools/builder/nt_builder.h`: nt_builder_hash() FNV-1a — runtime needs identical function.

### Established Patterns
- Handle = {uint32_t id} with index in lower bits, generation in upper bits (entity, gfx handles)
- `#pragma pack(push, 1)` for binary format structs (Phase 20)
- `#define` compile-time limits overridable by game (MAX_ENTITIES, NT_BUILD_MAX_ASSETS etc.)
- Swappable backends via CMake: stub backend for testing, real backend for runtime
- `_Static_assert` for struct size verification

### Integration Points
- `engine/CMakeLists.txt` — add_subdirectory(resource) for new module
- `engine/resource/CMakeLists.txt` — new target nt_resource, links nt_core + nt_shared
- Phase 25 will add loading backends (web fetch, native fread) and typed activation wrappers
- Phase 27 render pipeline will call is_ready() + get() per entity

</code_context>

<specifics>
## Specific Ideas

- "Как при context loss — стали невалидные, пойдут в пак за новыми данными" — analogy user drew between unmount and context loss GPU resource handling
- Pack blob can be unloaded from memory independently of unmount — pack metadata stays, assets can be re-loaded later (important for memory management)
- "Resolve = наивысший READY" with graceful fallback — old texture shows while new one loads, no visual pop
- "10,000 мешей" concern drove step() + O(1) get() architecture — no per-entity checks in render hot path
- Virtual packs as "named namespace for convenient access" — not lifecycle management for runtime resources
- API on hashes only (no string wrappers) — builder prints hashes, game uses #define constants

</specifics>

<deferred>
## Deferred Ideas

Migrated to GitHub issues — see label `deferred`.

</deferred>

---

*Phase: 24-asset-registry*
*Context gathered: 2026-03-17*
