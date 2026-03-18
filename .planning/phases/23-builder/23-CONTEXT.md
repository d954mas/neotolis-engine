# Phase 23: Builder - Context

**Gathered:** 2026-03-17
**Status:** Ready for planning

<domain>
## Phase Boundary

Native C builder binary reads .glb mesh files, .png textures, and .glsl shader source, validates inputs, and writes a NEOPAK binary pack. Builder provides a code-first API (static library) that games link and use in their own builder main.c. Phase does NOT include runtime loading, material creation, or GL-based shader compilation.

</domain>

<decisions>
## Implementation Decisions

### Asset naming & resource IDs
- FNV-1a hash of the relative path string as passed to the API (e.g. "meshes/cube.glb" -> uint32)
- Interim hash function until nt_hash module (Phase 29); placed as inline utility in builder or shared
- Same FNV-1a function used for stream name_hash (vertex attribute names like "position", "uv0")
- Optional explicit resource_id override: add_mesh_with_id(path, layout, count, id) variant alongside add_mesh(path, layout, count)
- Duplicate path detection: hard error, abort pack build
- No hash collision check (astronomically unlikely with FNV-1a on short strings)
- Builder prints all resource_id values on successful build (path -> 0xHEXID)

### glTF mesh extraction
- One mesh per .glb file; error if file contains multiple meshes/primitives
- Explicit stream layout: game declares array of {engine_name, gltf_name, type, component_count} per stream
  - e.g. {"position", "POSITION", NT_STREAM_FLOAT32, 3}, {"uv0", "TEXCOORD_0", NT_STREAM_FLOAT32, 2}
  - No hidden mapping table — game sees exactly what gets extracted (explicit over implicit)
  - Same layout reused across add_mesh / add_meshes calls for batch addition
- Auto type conversion: builder converts between types when layout declares different type than glTF source (e.g. float32 -> float16, float32 -> int8 normalized) for mesh size optimization
- Auto-select index type: uint16 when vertex_count <= 65535, uint32 otherwise
- Non-indexed meshes supported with a warning (index_type=0)
- Max limits via #define (same pattern as engine): NT_BUILD_MAX_VERTICES, NT_BUILD_MAX_INDICES — error if exceeded, game can override

### Texture processing
- stb_image loads .png forced to 4-channel RGBA8
- No resize, no power-of-two enforcement
- Max texture size check via #define (e.g. NT_BUILD_MAX_TEXTURE_SIZE) — error if exceeded

### Shader processing
- Strip C-style comments (// and /* */) and collapse excess whitespace before packing
- Basic sanity checks: non-empty, has #version, has void main()
- Store as null-terminated text blob in pack
- No GL compile validation in this phase (deferred as separate phase)

### Error handling & validation
- Fail entire build on any error — no .neopak written
- Verbose error messages: file path + specific reason + context (e.g. "meshes/cube.glb: missing required attribute POSITION in primitive 0")
- No cross-asset validation (shader/mesh compatibility is a runtime concern)
- Always print build summary on success: asset count by type, total pack size, CRC32

### Pack output & builder architecture
- Builder API is a static library (nt_builder) in tools/builder/
- Code-first: game writes its own builder main.c, links nt_builder
- Game specifies full output path in code: start_pack("build/assets/base.neopak")
- Demo builder main.c lives in examples/ alongside demo game code
- Manual builder run — no CMake auto-build (spec: "manual rebuild acceptable")
- pack_dump utility included: builder can read a .neopak and print contents to console
- cgltf and stb_image vendored in deps/cgltf and deps/stb (consistent with cglm, glad, glfw pattern)
- Glob patterns for batch addition (add_meshes("*.glb")) — implementation details left to Claude

### Claude's Discretion
- Glob pattern implementation approach (portable C wildcard matching)
- pack_id generation strategy for NtPackHeader
- Memory management in builder (heap is fine — not runtime hot path)
- Exact FNV-1a implementation variant (32-bit, standard constants)
- Builder internal error reporting mechanism (printf vs nt_log)
- Intermediate directory creation behavior for output path

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### Builder architecture
- `docs/neotolis_engine_spec_1.md` S23 — Builder model, module layers, core API, stages, validation rules
- `docs/neotolis_engine_spec_1.md` S19 — NEOPAK pack format binary layout, runtime parsing, asset data access, debugging (pack_dump)
- `docs/neotolis_engine_spec_1.md` S20 — Runtime formats: general rule, mesh format strategy, validation policy

### Shared format headers (Phase 20 output)
- `shared/include/nt_pack_format.h` — NtPackHeader (24 bytes), NtAssetEntry (16 bytes), alignment constants, asset type enum
- `shared/include/nt_mesh_format.h` — NtMeshAssetHeader (24 bytes), NtStreamDesc (8 bytes), stream types, nt_stream_type_size()
- `shared/include/nt_texture_format.h` — NtTextureAssetHeader (20 bytes), pixel format enum
- `shared/include/nt_shader_format.h` — NtShaderCodeHeader (12 bytes), shader stage enum
- `shared/include/nt_crc32.h` — CRC32 computation for pack checksum
- `shared/include/nt_formats.h` — Umbrella include for all format headers

### Existing builder skeleton
- `tools/builder/CMakeLists.txt` — Current builder target (links nt_core, outputs to build/tools/builder/)
- `tools/builder/main.c` — Placeholder main (prints version)

### Requirements
- `.planning/REQUIREMENTS.md` — BUILD-01 through BUILD-07

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets
- `shared/include/nt_pack_format.h`: Pack header, asset entry structs — builder writes these directly
- `shared/include/nt_mesh_format.h`: Mesh header, stream descriptors — builder populates from glTF data
- `shared/include/nt_texture_format.h`: Texture header — builder populates from stb_image output
- `shared/include/nt_shader_format.h`: Shader header — builder populates from .glsl source
- `shared/include/nt_crc32.h/c`: CRC32 utility — builder uses for pack checksum
- `engine/core/`: nt_core module — builder already links it (version string, assert macros)

### Established Patterns
- `#pragma pack(push, 1)` for all binary format structs (Phase 20 decision)
- `_Static_assert` for struct size verification
- `#define` compile-time limits pattern (MAX_ENTITIES etc.)
- Vendored deps in deps/ with CMake include paths (cglm, glad, glfw, unity)
- Warning flags + sanitizers via nt_set_warning_flags() / nt_set_sanitizer_flags()
- Per-project output directory: build/tools/builder/${NT_PRESET_NAME}/

### Integration Points
- `tools/builder/CMakeLists.txt` needs expansion: nt_builder static library + link cgltf/stb
- `CMakeLists.txt` top-level: builder added via `add_subdirectory(tools/builder)` only for non-Emscripten builds
- `examples/` directory: demo builder main.c linking nt_builder
- `deps/`: new cgltf and stb subdirectories

</code_context>

<specifics>
## Specific Ideas

- Explicit stream layout was chosen over hidden mapping table to follow "explicit over implicit" philosophy — game code must declare both engine name AND glTF source attribute name
- Auto type conversion (float32 -> float16 etc.) is important: "the idea is to compress meshes and make them smaller for the game"
- Resource ID print was chosen over header codegen for now — user wants codegen as a future todo
- GL-based shader compile validation (create headless GLFW window, swap #version, try compile) was discussed in depth — catches 90% of errors but deferred as its own phase due to GLSL ES vs desktop dialect mismatch

</specifics>

<deferred>
## Deferred Ideas

Migrated to GitHub issues — see label `deferred`.

</deferred>

---

*Phase: 23-builder*
*Context gathered: 2026-03-17*
