# Requirements: Neotolis Engine v1.3 Asset Pipeline

**Defined:** 2026-03-16
**Core Value:** Simple, fast, predictable -- composable features wired through code, zero hidden magic.

## v1.3 Requirements

### Builder

- [x] **BUILD-01**: Native C builder binary reads .glb mesh files via cgltf and outputs runtime mesh binary
- [x] **BUILD-02**: Builder reads .png textures via stb_image and outputs raw RGBA8 pixel data
- [x] **BUILD-03**: Builder reads .glsl vertex/fragment shader source files as text blobs
- [x] **BUILD-04**: Builder writes NEOPAK binary pack with header, asset entries, and aligned data
- [x] **BUILD-05**: Builder supports code-first API (start_pack/add_mesh/add_texture/add_shader/finish_pack)
- [x] **BUILD-06**: Builder supports glob patterns for batch asset addition (add_meshes("*.glb"))
- [x] **BUILD-07**: Builder validates mesh vertex attributes against minimum requirements (POSITION required)

### NEOPAK Format

- [x] **PACK-01**: NEOPAK binary has PackHeader with magic, version, asset_count, header_size, total_size, CRC32 checksum
- [x] **PACK-02**: Asset entries contain resource_id, asset_type, format_version, offset, size
- [x] **PACK-03**: Asset data is aligned to 4-byte boundaries within pack
- [x] **PACK-04**: Shared format structs (PackHeader, AssetEntry) are bit-identical between builder and runtime (_Static_assert verified)

### Asset Registry

- [x] **REG-01**: Asset registry tracks resources by ResourceId with AssetMeta (type, pack_index, offset, size, state)
- [x] **REG-02**: Asset state machine transitions: REGISTERED -> LOADING -> READY / FAILED
- [x] **REG-03**: Pack stacking with priority -- higher pack_index overrides lower for same ResourceId
- [x] **REG-04**: Game can mount/unmount packs and change priority order at runtime
- [x] **REG-05**: Runtime-created resources can be registered in the registry by name (two-level system)
- [x] **REG-06**: Typed handles: MeshHandle, TextureHandle, ShaderHandle for type-safe access

### Asset Loading

- [x] **LOAD-01**: Pack loading on WASM via JS fetch bridge (platform_request_fetch / on_fetch_complete)
- [x] **LOAD-02**: Pack loading on native via synchronous fread()
- [x] **LOAD-03**: Pack state machine: NONE -> REQUESTED -> LOADED -> READY / FAILED
- [x] **LOAD-04**: NEOPAK parser validates magic, version, sizes, CRC32 before registering assets
- [x] **LOAD-05**: resource_step() activates assets each frame (mesh -> buffers, texture -> GPU, shader -> program)

### Texture Support (nt_gfx extension)

- [x] **TEX-01**: nt_gfx_make_texture() creates GPU texture from raw RGBA8 data with configurable filter/wrap
- [x] **TEX-02**: nt_gfx_bind_texture() binds texture to sampler slot for rendering
- [x] **TEX-03**: nt_gfx_destroy_texture() releases GPU texture
- [x] **TEX-04**: Texture pool follows existing slot/generation pattern (like shaders, buffers, pipelines)
- [x] **TEX-05**: glGenerateMipmap() support when requested in texture descriptor

### Shader Assets

- [x] **SHDR-01**: Shader asset contains vertex + fragment source text loaded from pack
- [x] **SHDR-02**: Shader asset activation creates nt_gfx shader + pipeline with vertex layout from mesh format

### Mesh Assets

- [x] **MESH-01**: Mesh runtime format contains: vertex data (POSITION + optional NORMAL, UV, COLOR), index data, attribute mask, counts
- [x] **MESH-02**: Mesh asset activation creates VBO + IBO via nt_gfx_make_buffer()
- [x] **MESH-03**: MeshAssetHeader has magic, version, attribute_mask, vertex_count, index_count

### Material System

- [ ] **MAT-01**: Material is a runtime object: shader handle + texture handles[] + vec4 params[] + render state
- [ ] **MAT-02**: Game creates materials in code from loaded shader and textures
- [ ] **MAT-03**: Material render state includes blend mode, depth test/write, cull mode
- [ ] **MAT-04**: All numeric material params stored as vec4[] (per spec section 16.2)

### Entity System

- [x] **ENT-01**: EntityHandle with uint16 index + uint16 generation for stale handle detection
- [x] **ENT-02**: Fixed entity pool with compile-time MAX_ENTITIES limit
- [x] **ENT-03**: Entity create/destroy/is_alive API with generation validation
- [x] **ENT-04**: Per-entity alive and enabled flags

### Component Storage

- [x] **COMP-01**: Canonical sparse+dense storage pattern per component type
- [x] **COMP-02**: Typed API per component: add/get/has/remove (not generic void*)
- [x] **COMP-03**: Swap-and-pop removal keeps dense array packed
- [x] **COMP-04**: Sparse array initialized to INVALID_INDEX at storage init

### Transform Component

- [x] **XFORM-01**: TransformComponent with local_position (vec3), local_rotation (quat), local_scale (vec3), world_matrix (mat4), dirty flag
- [x] **XFORM-02**: transform_update() recomputes world_matrix for dirty transforms and clears dirty flag
- [x] **XFORM-03**: All entities are roots (no hierarchy in v1.3) -- world = local transform

### Render Components

- [x] **RCOMP-01**: MeshComponent stores mesh asset reference
- [x] **RCOMP-02**: MaterialComponent stores material reference
- [x] **RCOMP-03**: RenderStateComponent with visible (bool), color (vec4), params0 (vec4)

### Mesh Rendering Pipeline

- [ ] **REND-01**: Iterate entities with Transform + Mesh + Material + RenderState components
- [ ] **REND-02**: Bind material pipeline, textures, and set uniforms (MVP matrix, color, params) per entity
- [ ] **REND-03**: Draw indexed mesh geometry via nt_gfx_draw_indexed()

### Demo

- [ ] **DEMO-01**: Demo loads a .neopak pack containing mesh + texture + shaders
- [ ] **DEMO-02**: Demo creates material from loaded shader and texture at runtime
- [ ] **DEMO-03**: Demo creates entity with Transform + Mesh + Material + RenderState components
- [ ] **DEMO-04**: Demo renders textured 3D model from asset pack
- [ ] **DEMO-05**: Demo has camera control (reuse/adapt trackball from v1.2)

## v1.4 Requirements (Deferred)

### Entity Hierarchy

- **HIER-01**: Parent/child/sibling linked list in entity slots
- **HIER-02**: Top-down transform update through hierarchy
- **HIER-03**: Enable/disable propagation through subtree

### Advanced Destruction

- **DEST-01**: Deferred entity destruction queue
- **DEST-02**: Process destruction at defined frame point

### Material Pack Asset

- **MATASSET-01**: Material as NEOPAK pack asset
- **MATASSET-02**: Builder validates material/shader/texture compatibility

### Advanced Assets

- **ADV-01**: Compressed textures (ETC2/ASTC)
- **ADV-02**: Multi-primitive meshes
- **ADV-03**: Incremental builder
- **ADV-04**: Asset hot-reload

## Out of Scope

| Feature | Reason |
|---------|--------|
| Entity hierarchy (parent/child) | Demo has flat scene; defer to v1.4 |
| Deferred entity destruction | Demo has static entities; immediate destroy sufficient |
| Material as pack asset | Runtime creation covers demo needs |
| Render item sort/batch keys | One model = one draw call |
| Compressed textures | RGBA8 sufficient for demo |
| sRGB / gamma-correct pipeline | Linear rendering sufficient |
| Audio system | Not needed for visual demo |
| Sprite/text/shadow components | Beyond demo scope |
| Builder watch mode / hot-reload | Manual rebuild acceptable |
| Generic ECS query/iteration | Spec mandates typed APIs |

## Traceability

| Requirement | Phase | Status |
|-------------|-------|--------|
| PACK-01 | Phase 20 | Complete |
| PACK-02 | Phase 20 | Complete |
| PACK-03 | Phase 20 | Complete |
| PACK-04 | Phase 20 | Complete |
| MESH-01 | Phase 20 | Complete |
| MESH-03 | Phase 20 | Complete |
| TEX-01 | Phase 21 | Complete |
| TEX-02 | Phase 21 | Complete |
| TEX-03 | Phase 21 | Complete |
| TEX-04 | Phase 21 | Complete |
| TEX-05 | Phase 21 | Complete |
| ENT-01 | Phase 22 | Complete |
| ENT-02 | Phase 22 | Complete |
| ENT-03 | Phase 22 | Complete |
| ENT-04 | Phase 22 | Complete |
| COMP-01 | Phase 22 | Complete |
| COMP-02 | Phase 22 | Complete |
| COMP-03 | Phase 22 | Complete |
| COMP-04 | Phase 22 | Complete |
| XFORM-01 | Phase 22 | Complete |
| XFORM-02 | Phase 22 | Complete |
| XFORM-03 | Phase 22 | Complete |
| RCOMP-01 | Phase 22 | Complete |
| RCOMP-02 | Phase 22 | Complete |
| RCOMP-03 | Phase 22 | Complete |
| BUILD-01 | Phase 23 | Complete |
| BUILD-02 | Phase 23 | Complete |
| BUILD-03 | Phase 23 | Complete |
| BUILD-04 | Phase 23 | Complete |
| BUILD-05 | Phase 23 | Complete |
| BUILD-06 | Phase 23 | Complete |
| BUILD-07 | Phase 23 | Complete |
| REG-01 | Phase 24 | Complete |
| REG-02 | Phase 24 | Complete |
| REG-03 | Phase 24 | Complete |
| REG-04 | Phase 24 | Complete |
| REG-05 | Phase 24 | Complete |
| REG-06 | Phase 24 | Complete |
| LOAD-04 | Phase 24 | Complete |
| LOAD-01 | Phase 25 | Complete |
| LOAD-02 | Phase 25 | Complete |
| LOAD-03 | Phase 25 | Complete |
| LOAD-05 | Phase 25 | Complete |
| SHDR-01 | Phase 25 | Complete |
| SHDR-02 | Phase 25 | Complete |
| MESH-02 | Phase 25 | Complete |
| MAT-01 | Phase 26 | Pending |
| MAT-02 | Phase 26 | Pending |
| MAT-03 | Phase 26 | Pending |
| MAT-04 | Phase 26 | Pending |
| REND-01 | Phase 27 | Pending |
| REND-02 | Phase 27 | Pending |
| REND-03 | Phase 27 | Pending |
| DEMO-01 | Phase 28 | Pending |
| DEMO-02 | Phase 28 | Pending |
| DEMO-03 | Phase 28 | Pending |
| DEMO-04 | Phase 28 | Pending |
| DEMO-05 | Phase 28 | Pending |

**Coverage:**
- v1.3 requirements: 58 total (corrected from initial estimate of 52)
- Mapped to phases: 58
- Unmapped: 0

---
*Requirements defined: 2026-03-16*
*Last updated: 2026-03-16 after roadmap creation*
