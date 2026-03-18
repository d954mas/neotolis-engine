# Phase 21: Texture Support - Context

**Gathered:** 2026-03-16
**Status:** Ready for planning

<domain>
## Phase Boundary

nt_gfx provides GPU texture lifecycle — create from raw RGBA8 pixel data, bind to sampler slot, destroy — following the existing slot/generation pool pattern. This extends nt_gfx with a texture pool parallel to existing shader, pipeline, and buffer pools. No asset loading, no material system — just the low-level GPU texture API.

</domain>

<decisions>
## Implementation Decisions

### Filter & wrap mode API
- Full 6-mode filter enum: NEAREST, LINEAR, NEAREST_MIPMAP_NEAREST, LINEAR_MIPMAP_NEAREST, NEAREST_MIPMAP_LINEAR, LINEAR_MIPMAP_LINEAR
- Separate min_filter and mag_filter in descriptor (matches GL model)
- 3 wrap modes: CLAMP_TO_EDGE, REPEAT, MIRRORED_REPEAT
- Separate wrap_u and wrap_v per axis (zero-cost, avoids future API break)

### Context-loss recovery
- Texture pool does NOT store CPU copy of pixel data — too much memory
- Pool stores only descriptor metadata (filter, wrap, size) for parameter recreation
- Full context-loss recovery (pixel re-upload) deferred to asset system (Phase 25) which has pack blob already in memory

### Mipmap control
- Single `bool gen_mipmaps` in descriptor — no mip_count field
- false = no mipmaps (single base level)
- true = runtime calls glGenerateMipmap after base level upload
- Pre-baked mip support (builder-generated levels) deferred to Phase 25 asset loading
- Mip level count computed automatically from width/height when needed

### Texture size
- Power-of-2 only (256, 512, 1024, etc.) — enforced by make_texture validation
- Data size computed automatically from width * height * 4 (RGBA8) — no data_size field in descriptor

### Pool defaults
- Default max_textures = 64 (configurable via nt_gfx_desc_t)
- Follows same pool infrastructure as shaders (32), pipelines (16), buffers (128)

### Visual verification
- Phase 21 includes a simple textured quad demo with hardcoded pixel data (checkerboard or color pattern)
- Proves textures render correctly end-to-end
- Will be replaced by asset-loaded textures in Phase 28

### Claude's Discretion
- Exact descriptor struct layout and field ordering
- GL backend implementation details (texture unit state cache, etc.)
- Unit test structure and coverage strategy
- Demo quad shader and geometry setup

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### Texture format
- `shared/include/nt_texture_format.h` — NtTextureAssetHeader struct (magic, version, format, width, height, mip_count, padding)

### GFX module (existing pool pattern to follow)
- `engine/graphics/nt_gfx.h` — Public API, handle types, descriptor structs, vertex layout enums
- `engine/graphics/nt_gfx.c` — Pool infrastructure, backend arrays, context-loss recovery loop
- `engine/graphics/nt_gfx_internal.h` — nt_gfx_pool_t, nt_gfx_slot_t, handle encoding (generation << 16 | slot_index)
- `engine/graphics/gl/nt_gfx_gl.c` — GL backend: resource create/destroy, state cache, context loss

### Spec
- `docs/neotolis_engine_spec_1.md` — Section 17 (Resource System), Section 20 (Runtime Formats)

### Existing tests
- `tests/unit/test_gfx.c` — Pool and resource tests to extend for textures

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets
- `nt_gfx_pool_t` (nt_gfx_internal.h): Pool with slots, free_queue, generation counter — reuse directly for texture pool
- `nt_gfx_pool_alloc/free/valid` (nt_gfx.c): Pool operations — texture pool uses identical functions
- GL state cache pattern (nt_gfx_gl.c): s_gl_cache struct avoids redundant GL calls — extend for texture unit binding
- Backend resource arrays (nt_gfx.c): shader_backends[], buffer_backends[] etc. — add texture_backends[] in same pattern

### Established Patterns
- Handle type: `typedef struct { uint32_t id; } nt_TYPE_t;` — texture follows same
- Descriptor storage: parallel array indexed by slot for context-loss recovery
- 1-based slot indexing: slot 0 reserved for invalid handle
- Backend create/destroy functions called from nt_gfx.c, implementation in gl/ backend

### Integration Points
- `nt_gfx_desc_t`: Add max_textures field
- `nt_gfx_init/shutdown`: Initialize/cleanup texture pool and backend arrays
- `nt_gfx_backend_recreate_all_resources`: Add texture recreation (descriptor-only, no pixel data)
- Demo example: New textured quad example or extend existing spinning cube demo

</code_context>

<specifics>
## Specific Ideas

- Demo should show a visible textured quad with hardcoded pixels to prove the pipeline works visually — not just unit tests
- Later phases replace hardcoded data with asset-loaded textures

</specifics>

<deferred>
## Deferred Ideas

Migrated to GitHub issues — see label `deferred`.

</deferred>

---

*Phase: 21-texture-support*
*Context gathered: 2026-03-16*
