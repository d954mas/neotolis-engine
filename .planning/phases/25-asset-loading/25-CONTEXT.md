# Phase 25: Asset Loading - Context

**Gathered:** 2026-03-18
**Status:** Ready for planning

<domain>
## Phase Boundary

Packs load asynchronously on WASM via JS fetch bridge (and synchronously on native), and resource_step() activates loaded assets into GPU resources each frame. Phase introduces two new I/O modules (nt_fs, nt_http), extends nt_resource with pack loading state machine, adds activator callbacks for type-agnostic asset activation, and refactors nt_gfx context loss recovery to remove CPU-side copies.

Phase does NOT include material system (Phase 26), mesh rendering pipeline (Phase 27), or demo integration (Phase 28).

</domain>

<decisions>
## Implementation Decisions

### New I/O modules
- Two new engine modules: **nt_fs** (file system) and **nt_http** (HTTP requests)
- Each has three backends: web, native, stub (same swappable backend pattern as nt_window, nt_input)
- Minimum API for Phase 25, but modules are general-purpose and reusable (save files, web requests from desktop, etc.)
- **nt_fs**: native = fread/fwrite, web = stub (FAILED silently), stub = FAILED silently
- **nt_http**: web = JS fetch via EM_JS (consistent with all other web backends), native = stub (FAILED silently), stub = FAILED silently
- Generational handles for requests (same pattern as entity, gfx, resource handles)
- Request lifecycle: request → poll state → take_data or free

### Request memory ownership (take semantics)
- nt_http/nt_fs allocates memory internally (malloc on WASM heap, fread on native)
- `nt_http_take_data(req, &size)` — transfers ownership: nt_http sets internal pointer to NULL, caller owns the buffer
- `nt_http_free(req)` after take — frees slot only, data pointer already NULL (no-op on data)
- `nt_http_free(req)` without take — frees both slot AND data buffer
- One owner at all times, no double-free risk

### Pack loading API
- **Mount and load are separate** (explicit over implicit): `nt_resource_mount(pack_id, priority)` then `nt_resource_load_file/url(pack_id, path)`
- Assert if load called on unmounted pack
- Three load functions:
  - `nt_resource_load_file(pack_id, path)` — through nt_fs
  - `nt_resource_load_url(pack_id, url)` — through nt_http
  - `nt_resource_load_auto(pack_id, path)` — convenience helper, routes by platform (#ifdef NT_PLATFORM_WEB → nt_http, else → nt_fs)
- Poll API for status (no callbacks, code-first):
  - `nt_resource_pack_state(pack_id)` → PackState enum
  - `nt_resource_pack_progress(pack_id, &received, &total)` → download progress

### Pack state machine
- NONE → REQUESTED → DOWNLOADING → LOADED → READY / FAILED
- DOWNLOADING state included with bytes_received / bytes_total progress (JS fetch ReadableStream)
- resource_step() sees LOADED pack → calls nt_resource_parse_pack() → registers assets → PackState = READY → begins asset activation

### Retry policy
- Auto-retry inside engine, transparent to game
- `nt_resource_set_retry_policy(max_attempts, base_delay_ms, max_delay_ms)` — global setter
- Defaults: max_attempts = 0 (infinite), base_delay_ms = 500, max_delay_ms = 10000
- Backoff multiplier: x1.5
  - Sequence: 500 → 750 → 1125 → 1687 → 2531 → 3797 → 5695 → 8543 → 10000 → 10000 → ...
- max_attempts = 1 → one attempt, no retry
- max_attempts = 0 → infinite retry, game stops via unmount
- Logging:
  - First request: `nt_log_info("nt_resource: loading pack 0x%08X from %s")`
  - Retry: `nt_log_warn("nt_resource: pack 0x%08X attempt %d/%d, retry in %dms")`
  - Final FAILED: `nt_log_error("nt_resource: pack 0x%08X FAILED after %d attempts")`
  - Success: `nt_log_info("nt_resource: pack 0x%08X loaded (%u bytes)")`

### Blob lifecycle (per-pack policy)
- Per-pack blob retention policy via `nt_resource_set_blob_policy(pack_id, policy, ttl_ms)`:
  - `NT_BLOB_KEEP` — blob lives as long as pack is mounted (for small base packs)
  - `NT_BLOB_AUTO` — auto-evict after TTL since last access (for large level packs)
- TTL is per-pack (different packs can have different TTLs)
- resource_step() checks timers, frees blob when TTL expires
- After blob eviction: metadata stays, assets on GPU stay. If re-activation needed (context loss) → triggers re-download

### Asset activation (callback system)
- nt_resource stays type-agnostic — no dependency on nt_gfx
- Game registers activator callbacks per asset type: `nt_resource_set_activator(asset_type, activate_fn, deactivate_fn)`
- Activator signature: `uint32_t (*activate_fn)(const uint8_t *data, uint32_t size)` → returns runtime_handle
- Deactivator signature: `void (*deactivate_fn)(uint32_t runtime_handle)`
- If no activator registered for a type → assets stay REGISTERED (not activated)
- resource_step() calls activators during per-frame processing

### Activators live in owning modules
- **nt_gfx** exports activators for GPU assets:
  - `nt_gfx_activate_texture(data, size)` — parses NtTextureAssetHeader → nt_gfx_make_texture()
  - `nt_gfx_activate_mesh(data, size)` — parses NtMeshAssetHeader → nt_gfx_make_buffer() x2 (VBO + IBO)
  - `nt_gfx_activate_shader(data, size)` — parses NtShaderCodeHeader → nt_gfx_make_shader()
  - Corresponding deactivate functions call nt_gfx_destroy_*
- Future modules (nt_audio etc.) export their own activators
- Game registers: `nt_resource_set_activator(NT_ASSET_TEXTURE, nt_gfx_activate_texture, nt_gfx_deactivate_texture)`

### Shader activation = compile only
- Shader activator creates nt_shader_t only (glCompileShader). No pipeline.
- Pipeline (shader + vertex layout + render state) is created in Phase 26/27 Material System
- **Deviation from SHDR-02**: requirement says "creates nt_gfx shader + pipeline." Updated: pipeline deferred to material system where vertex layout and render state are known.

### Cost-based activation budget
- Each asset type has a fixed cost (#define, overridable by game):
  - `NT_ACTIVATE_COST_MESH 1`
  - `NT_ACTIVATE_COST_SHADER 2`
  - `NT_ACTIVATE_COST_TEXTURE 4`
- Per-frame budget: `#define NT_RESOURCE_ACTIVATE_BUDGET 16` (default)
- Runtime setter: `nt_resource_set_activate_budget(N)`
- resource_step() activates assets until budget spent: budget -= cost per activation
- Game adjusts: loading screen = 32+, gameplay = 4

### Context loss refactor (part of Phase 25)
- **Remove CPU-side copies from nt_gfx**: no more shader_descs, texture_descs, buffer_descs stored for recovery
- **nt_gfx on context loss**: cleans up pool slots, sets context_lost = true. restore_context() removed.
- **context_restored flag**: lives one frame (set in begin_frame, cleared in end_frame)
- **Restoration is game's responsibility**:
  - Pack assets: `nt_resource_invalidate(NT_ASSET_TEXTURE)`, `nt_resource_invalidate(NT_ASSET_MESH)`, `nt_resource_invalidate(NT_ASSET_SHADER_CODE)` — called per type, not invalidate_all (audio not affected)
  - Modules with internal GPU state: `nt_shape_restore_gpu()` — new function, NOT init() (init does too much)
  - Pattern: init() = full init once, restore_gpu() = recreate GPU resources, shutdown() = full cleanup
- **invalidate(type)**: marks all file pack assets of that type as REGISTERED, runtime_handle = 0
- resource_step() re-activates from blob if available, or triggers re-download if blob evicted

### Claude's Discretion
- nt_http internal request pool sizing and state machine
- nt_fs internal implementation details
- Exact NtPackMeta extensions (PackState field, url, progress fields)
- How resource_step() discovers completed nt_http/nt_fs requests (poll in step vs internal linkage)
- Blob TTL timer implementation (wall clock vs frame count)
- Order of activation within budget (by type, by pack, FIFO)
- CMake wiring for new modules and backend selection

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### Async loading architecture
- `docs/neotolis_engine_spec_1.md` S18 — Async loading: pack state machine, asset state machine, loading flow, JS bridge contract, activation strategy, retry policy, memory note
- `docs/neotolis_engine_spec_1.md` S18.6 — JS bridge fetch contract: platform_request_fetch, platform_on_fetch_complete, platform_on_fetch_progress
- `docs/neotolis_engine_spec_1.md` S18.7 — Asset activation strategy: eager with rate-limit, O(1) activation via resolve_prio/resolve_pack

### Resource system (Phase 24 output)
- `engine/resource/nt_resource.h` — Current public API: mount/unmount, parse_pack, request/get/is_ready/get_state, virtual packs, hash utility
- `engine/resource/nt_resource.c` — Full implementation: pack management, NEOPAK parser, slot map, resource_step() resolve logic
- `engine/resource/nt_resource_internal.h` — Internal types: NtAssetMeta, NtPackMeta (has blob/blob_size ready), NtResourceSlot, asset/pack state enums
- `.planning/phases/24-asset-registry/24-CONTEXT.md` — Phase 24 decisions: pack stacking, mount/unmount, resource handles, step() design, virtual packs, placeholder

### GFX API (activation target)
- `engine/graphics/nt_gfx.h` — make_shader/make_pipeline/make_buffer/make_texture APIs, descriptor structs, handle types
- `engine/graphics/nt_gfx.c` lines 231-289 — Current restore_context() implementation (to be refactored)

### Asset format headers (activators parse these)
- `shared/include/nt_mesh_format.h` — NtMeshAssetHeader, stream descriptors, attribute mask
- `shared/include/nt_texture_format.h` — NtTextureAssetHeader, pixel format
- `shared/include/nt_shader_format.h` — NtShaderCodeHeader, shader stage enum

### Existing web platform
- `engine/platform/web/nt_platform_web.c` — Current web platform (EM_JS pattern reference)
- `engine/platform/web/library.js` — Currently empty (not used for fetch)

### Requirements
- `.planning/REQUIREMENTS.md` — LOAD-01, LOAD-02, LOAD-03, LOAD-05, SHDR-01, SHDR-02 (modified: no pipeline), MESH-02

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets
- `engine/resource/nt_resource.c`: Full registry implementation — Phase 25 extends with pack loading state machine and activation loop in resource_step()
- `engine/resource/nt_resource_internal.h`: NtPackMeta already has blob/blob_size fields, NtAssetMeta has state/runtime_handle — ready for activation
- `shared/include/nt_pack_format.h`: Pack header and asset entry structs — activators parse asset-specific headers from blob data
- `engine/graphics/nt_gfx.h`: make_shader/buffer/texture descriptors — activators fill these from format headers

### Established Patterns
- EM_JS for all web backends (nt_window_web.c, nt_input_web.c) — nt_http web backend follows same
- Swappable backends via CMake: INTERFACE API + multiple STATIC implementations (web/native/stub)
- Generational handles: index + generation in uint32_t (entity, gfx, resource)
- `#define` compile-time limits with game override (MAX_ENTITIES, MAX_ASSETS, etc.)
- `_Static_assert` for struct size verification

### Integration Points
- `engine/CMakeLists.txt` — add_subdirectory for new nt_fs and nt_http modules
- `engine/resource/nt_resource.h` — extend with load_file/load_url/load_auto, pack_state, pack_progress, set_activator, set_activate_budget, set_blob_policy, set_retry_policy, invalidate
- `engine/graphics/nt_gfx.h` — add activate/deactivate functions for texture/mesh/shader
- `engine/graphics/nt_gfx.c` — remove restore_context(), remove CPU-side desc storage
- `engine/renderers/nt_shape.h` — add nt_shape_restore_gpu() function

</code_context>

<specifics>
## Specific Ideas

- "Это вообще разные модули для игры. Их потом можно использовать для других фич. Например для веб запросов из десктопа. Или для чтения и записи сохранений в файл" — nt_fs and nt_http as general-purpose modules, not just asset loading
- "Давай явно и ассерт если грузим пак который не mount" — explicit mount → load separation, strict validation
- Load_auto as convenience helper — "просто небольшой хелпер чтобы не городить много кода на загрузке и не думать"
- "base.neopak маленький и пусть всегда будет в памяти. А для других автовыгрузка" — per-pack blob policy driven by game's memory strategy
- "Мы убираем состояние ресурсов из CPU. Шейдер не хранит текст, текстура не хранит пиксели. GFX не восстанавливает, он только освобождает. Восстановить это задача игры" — context loss ownership shift from nt_gfx to game/nt_resource
- "Init может ещё много чего делать" — restore_gpu() as separate function from init() for post-context-loss recovery
- Cost-based budget was chosen over simple count because "это очень просто сделать его сейчас" and gives fairer distribution across asset types

</specifics>

<deferred>
## Deferred Ideas

Migrated to GitHub issues — see label `deferred`.

</deferred>

---

*Phase: 25-asset-loading*
*Context gathered: 2026-03-18*
