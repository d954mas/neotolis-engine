# Phase 29: nt_hash module — unified CRC32 + string hash, resource label hashing - Context

**Gathered:** 2026-03-19
**Status:** Ready for planning

<domain>
## Phase Boundary

New `nt_hash` engine module that unifies string/binary hashing across builder and runtime. Includes hash function benchmark (FNV-1a vs xxHash vs MurmurHash3 vs CRC32), typed hash structs (`nt_hash32_t`, `nt_hash64_t`), debug label reverse-lookup system, and migration of all existing hash callers. CRC32 stays in `shared/` (checksum, not identity hashing). resource_id migrates from uint32 to nt_hash64_t.

Phase does NOT include: resource ID header codegen (#62), path normalization changes (stays in builder), or CRC32 relocation.

</domain>

<decisions>
## Implementation Decisions

### Module placement
- New module: `engine/hash/` (nt_hash), STATIC library
- CRC32 stays in `shared/` as-is — different purpose (checksum vs identity hash)
- Builder links nt_hash directly (pragmatic exception to engine/tools separation — nt_hash is pure utility, no platform deps)
- .h + .c layout, consistent with all other engine modules

### Hash function selection (benchmark-driven)
- 4 candidates: FNV-1a (current), xxHash (xxh32/xxh64), MurmurHash3, CRC32-as-string-hash
- Both 32-bit and 64-bit variants benchmarked for each
- Metrics: speed on short strings (~20-50 chars), speed on medium strings (~100 chars), avalanche/distribution quality, WASM release gzipped size impact
- Benchmark code lives in `tools/research/hash_benchmark/` — native executable, not linked to engine
- One algorithm wins, both widths shipped
- All candidates are endian-independent on WASM (LE) + x86 (LE). Byte-by-byte algorithms (FNV-1a) are endian-safe on any platform; block-reading algorithms (xxHash, MurmurHash3) produce same results on LE only

### Type safety — struct wrappers
- `typedef struct { uint32_t value; } nt_hash32_t;`
- `typedef struct { uint64_t value; } nt_hash64_t;`
- Compiler catches mixing hash with plain integer (unlike typedef uint32_t)
- Comparison via `.value ==` (same pattern as entity/gfx handles)
- No NT_HASH32_INVALID / NT_HASH64_INVALID — any value can be valid hash result, reserving 0 would lose one value

### API surface
- Base API on binary data (pointer + size):
  - `nt_hash32_t nt_hash32(const void *data, uint32_t size);`
  - `nt_hash64_t nt_hash64(const void *data, uint32_t size);`
- String helpers as inline in header:
  - `static inline nt_hash32_t nt_hash32_str(const char *s)`
  - `static inline nt_hash64_t nt_hash64_str(const char *s)`
- If byte-by-byte algorithm wins benchmark, `_str` variants can be optimized to check \0 in-process (no strlen). Binary version still needed for future data hashing
- No combine function — no current use case for composite keys
- init/shutdown following standard module pattern (needed for label table)

### Debug label system (NT_HASH_LABELS)
- Controlled by `#define NT_HASH_LABELS 0/1` — independent of NDEBUG
  - Can enable in release for QA/logging, disable in debug for profiling
  - Default: off (0). Game enables via `-DNT_HASH_LABELS=1`
- Auto-registration: when NT_HASH_LABELS=1, every nt_hash32/64 call stores source string → hash mapping
- Explicit registration: `nt_hash_register_label64(hash, "name")` / `nt_hash_register_label32(hash, "name")` — for cases where hashes come pre-computed (e.g. from pack files)
- Reverse lookup: `const char *nt_hash64_label(nt_hash64_t hash)` / `nt_hash32_label(...)` — returns string or NULL
- Internal storage structure: Claude's discretion (hash table for O(1) lookup)

### resource_id migration (32 → 64-bit)
- resource_id becomes `nt_hash64_t` everywhere:
  - NtAssetEntry.resource_id
  - NtAssetMeta.resource_id
  - NtResourceSlot.resource_id
  - Builder API outputs
  - All nt_resource public API that takes resource_id
- pack_id stays 32-bit but becomes `nt_hash32_t` (MAX_PACKS=16, collision impossible)
- Pack format compatibility: not a concern — project in development, no production .neopak files
- Spec update: `docs/neotolis_engine_spec_1.md` updated to reflect new hash module and 64-bit resource_id

### Caller migration
- `nt_resource_hash()` — deleted, replaced with `nt_hash64_str()` at all call sites
- `nt_builder_fnv1a()` — deleted
- `nt_builder_hash()` — deleted, replaced with `nt_builder_normalize_and_hash()` (normalize_path + `nt_hash64_str()` inside, lives in builder)
- FNV-1a constants removed from `nt_resource.c` and `nt_builder_hash.c`
- New test_hash covers nt_hash32/64 + label system
- Existing test_builder and test_resource migrated to new API

### Claude's Discretion
- Internal label table data structure and sizing
- Benchmark harness implementation details
- Exact benchmark iteration counts and statistical reporting
- Whether _str variants use strlen+hash or single-pass (depends on winning algorithm)
- nt_hash.c internal structure and helper functions
- CMake wiring details for builder → nt_hash linkage

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### Existing hash implementations (to be replaced)
- `tools/builder/nt_builder_hash.c` — Current FNV-1a + path normalization + file I/O
- `tools/builder/nt_builder_internal.h` — nt_builder_fnv1a() and nt_builder_hash() declarations
- `engine/resource/nt_resource.c` lines 18-21, 478-486 — Duplicate FNV-1a in runtime
- `engine/resource/nt_resource.h` line 134 — nt_resource_hash() declaration

### Structures to migrate (resource_id uint32 → nt_hash64_t)
- `shared/include/nt_pack_format.h` — NtAssetEntry.resource_id (pack binary format)
- `engine/resource/nt_resource_internal.h` — NtAssetMeta.resource_id, NtPackMeta.pack_id, NtResourceSlot.resource_id
- `engine/resource/nt_resource.h` — Public API functions accepting resource_id / pack_id

### CRC32 (stays in shared/)
- `shared/include/nt_crc32.h` — CRC32 API (unchanged)
- `shared/include/nt_crc32.c` — CRC32 implementation with lookup table (unchanged)

### Module patterns to follow
- `engine/graphics/nt_gfx.h` — Handle type pattern: `typedef struct { uint32_t id; } nt_TYPE_t;`
- `engine/entity/nt_entity.h` — Module init/shutdown pattern
- `engine/CMakeLists.txt` — add_subdirectory for new modules

### Spec (to be updated)
- `docs/neotolis_engine_spec_1.md` — Sections mentioning FNV-1a, ResourceId, uint32 resource identifiers

### Requirements
- `.planning/REQUIREMENTS.md` — No specific hash requirements; phase is infrastructure improvement

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets
- `engine/graphics/nt_gfx.h`: Handle type pattern (`typedef struct { uint32_t id; }`) — hash types follow same
- `engine/entity/nt_entity.h`: Module init/shutdown pattern — nt_hash follows same
- `shared/include/nt_crc32.c`: CRC32 lookup table — stays as-is, not moved

### Established Patterns
- `#define` compile-time limits overridable by game (NT_HASH_LABELS, NT_HASH_MAX_LABELS)
- .h + .c with CMake STATIC library per module
- `_Static_assert` for struct size verification (NtAssetEntry size will change)
- Generational handle pattern: `typedef struct { uint32_t id; } nt_TYPE_t;` — hash types are simpler (just value, no generation)

### Integration Points
- `engine/CMakeLists.txt` — add_subdirectory(hash) for new module
- `tools/builder/CMakeLists.txt` — target_link_libraries(nt_builder PRIVATE nt_hash)
- `engine/resource/CMakeLists.txt` — target_link_libraries(nt_resource PRIVATE nt_hash)
- `tests/unit/CMakeLists.txt` — new test_hash target
- `tools/research/` — new directory for benchmark executable

</code_context>

<specifics>
## Specific Ideas

- Performance и минимальный шанс коллизий — главные критерии выбора алгоритма
- Defold использует 64-bit хеши для resource ID — этот подход принят для Neotolis
- WASM gzipped size — важная метрика бенчмарка (каждый байт на счету)
- Label system не привязана к debug/release — контролируется отдельным define для гибкости (QA может включить в release)
- В будущем builder будет генерировать .h с #define ASSET_* константами (issue #62) — nt_hash готовит фундамент для этого
- "Что больше по духу движка" — CRC32 остаётся отдельно, потому что "set of modules — use only what you need"

</specifics>

<deferred>
## Deferred Ideas

Deferred ideas tracked as GitHub issues — see label `deferred`.

</deferred>

---

*Phase: 29-nt-hash-module-unified-crc32-string-hash-resource-label-hashing*
*Context gathered: 2026-03-19*
