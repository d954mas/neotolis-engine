# Resource ownership and free asset slots

Status: revision 2 implemented and verified. The plan was approved by three independent subagents and Claude Fable; implementation passed four independent review lenses and Fable review. See [review findings and dispositions](450-resource-ownership-review.md) and [implementation evidence and measurements](../resource-ownership-450.md).

Branch: `450-pack-registration`; PR #461; implementation baseline `161b876899687205737bffd29671513ab5c0fc1f`.
Original comparison baseline: `0c26ded1b0e710bae0ced0147307966496324e38`.

## Objective and accepted decisions

Replace runtime reconstruction of pack aliases with builder-authored canonical ownership, and replace free AssetMeta scans with a stack of free indices. Keep the runtime small, explicit, and free of redundant ownership state. The sole engine consumer permits breaking the pack format and relevant APIs.

- Two names with identical compatible payloads in one pack share one runtime object. Either name works without requesting the other name.
- Pack activation stays eager and budgeted. Game requests choose published names, not whether the canonical owner is activated.
- Only the canonical owner activates and deactivates the shared object. Owner failure makes all its aliases unavailable; aliases never make independent activation attempts. Existing explicit invalidation/reload can cause a later attempt; no new automatic retry policy.
- Ownership never crosses packs. Identical assets in independently loaded packs remain independent.
- Alias identity, metadata, priority selection, blob views, and per-slot auxiliary data remain distinct from shared runtime-object ownership.
- No backward compatibility reader, general allocator, refcount, persistent manifest-to-registry map, new production test hook, or additional caller obligation.

## Current evidence

- `engine/resource/nt_resource.c`: `asset_alloc`, `asset_range_compare`, `nt_resource_parse_pack` reconstruct aliases with a temporary index array and qsort. `nt_resource_step` then searches the registry again for a READY alias.
- `tools/builder/nt_builder.c`: `nt_builder_register_asset` already identifies encoded-byte duplicates; `nt_builder_finish_pack` also appends early duplicates by copying an existing entry. Both paths must produce canonical ownership.
- `shared/include/nt_pack_format.h`: NtAssetEntry is 24 bytes. Its 16-bit manifest `format_version` is copied into NtAssetMeta but not read by runtime consumers; payload headers retain their own actual format versions.
- `engine/resource/nt_resource_internal.h`: NtAssetMeta is currently 32 bytes. `is_dedup` confuses byte aliasing with responsibility for destroying a runtime object.
- Existing failure: owner activation returns zero, later alias independently succeeds, then unmount/invalidate skips freeing that alias's object because `is_dedup` remains set.
- Existing final-byte dedup does not compare asset types; equal bytes alone are insufficient for sharing a runtime handle.
- Existing tests cover aliases and fragmented slot reuse separately; the new alias fixture assumes eight requested-resource slots.

## Data model

```text
Builder final entries                  Resident resource registry
entry i: resource_id, metadata          record r(i): resource_id, metadata
         offset, size, asset_type  -->              offset, size, asset_type
         owner_entry (self/earlier)                 owner_asset = r(owner_entry)

Game requests alias name --> its own ResourceSlot --> alias AssetMeta identity
                                                   --> owner state / GPU handle
```

### Wire contract

Replace NtAssetEntry's 16-bit `format_version` with `owner_entry`; increment `NT_PACK_VERSION` from 2 to 3. Retain the 24-byte layout and all existing offset/size/metadata fields. Keep payload-header versions and encoder/cache invalidation mechanisms; removing the redundant manifest version must not remove actual payload validation.

For entry `i`:

1. `owner_entry == i` means canonical owner.
2. Otherwise `owner_entry < i` and the referenced entry must reference itself. Chains, forward references, and cycles are forbidden.
3. An alias has exactly the owner's asset_type, offset, and size. Per-name resource_id and metadata may differ.
4. Owners and aliases are in this same manifest. Zero resource_id is invalid because it is the runtime's free-record sentinel.

Runtime preflight checks these relations and all existing entry ranges before reserving slots or modifying any asset/pack state. Reject malformed owner relations with the existing recoverable parse error, preserving corrected-load-into-same-mount behavior from issue #459 (delivered in PR #458 / baseline 0c26ded1). Capacity exhaustion and OOM remain developer assertions. Exact pack-version mismatch retains the existing assertion policy. Existing per-type payload checks stay in their owning activators; this is not a claim that BLOB currently checks its payload version.

Self-owned entries may use equal ranges without sharing runtime objects. The reader must not rediscover aliases or demand a global uniqueness scan. The builder emits sharing only for compatible assets; different types are independently owned.

### Resident contract

Replace NtAssetMeta's `format_version` with a 16-bit `owner_asset`; remove `is_dedup`. Keep the record at 32 bytes with ordinary explicit padding if needed; verify layout rather than assuming savings from field removal.

- Every live record points directly to a live owner in the same pack; owners reference themselves. Virtual records also reference themselves, but their handles remain caller-owned.
- Only owner records have authoritative `state` and `runtime_handle`. Alias copies of these fields are unused/zero; no synchronization pass mirrors owner state into aliases.
- Resolve/publication and diagnostic access read effective state/handle from the owner. They retain the alias record as winner identity for metadata, callbacks, priority, and user_data tracking.
- Preserve the BLOB-specific effective-handle behavior: its published asset index identifies the named entry, including index zero. Do not replace it with the owner's index or a GPU-handle validity test.
- At most a small private owner accessor is justified by repeated real consumers; no new public ownership API is needed.

## Allocation and translation

Add `uint16_t free_assets[NT_RESOURCE_MAX_ASSETS]` and `uint32_t free_asset_count` to module state. Use uint32_t for capacity loops/count arithmetic; the stored indices remain uint16_t. Retain `asset_hwm` for bounded existing iteration; it is the largest issued index plus one, not the live count. Initialize the stack once at resource init, with low indices returned first; shutdown resets it.

- Single virtual allocation pops one index; virtual unregister and whole-pack unmount push each live freed index exactly once.
- Re-registering an existing virtual name updates its current record without touching the free stack. Repeated unregister/unmount does not push twice.
- Assert nonzero resource IDs in virtual register/unregister before matching or mutation; unregister only matches live records. Zero is a programmer error for these APIs and a recoverable malformed-input error in the wire parser. This prevents a zero-ID unregister from matching a tombstone and pushing it twice.
- Keep `resource_id == 0` as the sole live/free marker; do not add a duplicate live bit or active count.
- File assets are removed only by whole-pack unmount. Assert the existing virtual-only meaning of `nt_resource_unregister` before release so it cannot free a canonical file owner while aliases remain. Missing packs/names retain the existing no-op behavior.
- Fully initialize every reused record, including virtual `owner_asset = self`, cleared unused fields and `meta_offset = NT_NO_METADATA`.

O(1) applies to slot pop/push. The existing virtual-register scan to find an already-registered name remains; this plan does not claim the entire virtual register/unregister operation is O(1) and does not add a second hash table.

For a file manifest, reserve N indices at once after complete validation. The just-reserved suffix of `free_assets` doubles as the temporary ordinal-to-slot mapping; it requires no allocation or second array:

```text
old_top = free_count; free_count -= N
r(i) = free_assets[old_top - 1 - i]
owner_asset(r(i)) = r(entries[i].owner_entry)
```

The reserved values remain untouched until parsing ends. The parser invokes no activator, resolve callback, or reentrant registry operation while using this mapping. No pointer into that suffix is retained. Later free operations may overwrite it because each live record already contains its translated owner index. Empty packs perform no stack access. Update asset_hwm with the actual issued indices.

## Activation, publication, and teardown

1. Activation traverses ready packs as today but activates only self-owned file records in REGISTERED state. Aliases are skipped. BLOB owners use the existing immediate-ready parse path.
2. The current time budget and minimum-one-attempt progress rule apply to actual owner activation attempts. Aliases do not consume attempts or require an extra frame to copy results. Activation order follows owner records, not a promised manifest order; there is no new scheduling layer.
3. Resolve reads the canonical state and handle even if an alias sits before its owner in the registry. It publishes the alias's own identity and metadata. Its original name does not need a requested slot.
4. FAILED owners remain failed until the existing explicit recovery path resets them. Aliases expose that state rather than falling back to their own activator call. Every completed activation attempt, successful or failed, sets `needs_resolve`; a failure on a later otherwise-clean step must reach the published slots.
5. Invalidation deactivates and resets each matching file owner once. Aliases reflect the owner through read-through access; virtual handles remain external. Same-type ownership prevents partial type invalidation leaving cross-type aliases stale.
6. Blob eviction retains owner indices just as it retains AssetMeta. Redownload continues to reuse registered records rather than reparse/reallocate them.
7. Unmount destroys each READY nonzero file-owner handle once and frees every named record once. Do not dereference a freed owner while clearing a later alias; self-ownership checks use record index equality. Preserve provider severing before blob free and existing metadata/IO cleanup. Finish the existing PIN_BLOB severing while old asset attribution is still available, before invalidating slot identities or returning indices to the free stack.
8. Audit all state/handle readers, diagnostic enumeration and existing test setters. A helper that targets an alias must manipulate its canonical activation state, not create an independent alias state.

### Reuse invalidates identity, not just occupancy

Before a live asset index becomes reusable, use its resource_id to find the existing requested-resource slot. If `resolve_asset_idx` or `user_data_asset_idx` equals the released index, invalidate that existing field with UINT16_MAX. Keep unrelated winners unchanged. Retain copy-out user_data for the normal resolve cleanup/rebuild path; PIN_BLOB providers have already been severed synchronously on unmount. Do not add asset generations, a new dirty field, or a full-slot scan per freed record.

This makes same-index/same-handle replacement trigger on_resolve and AUX synchronization instead of accepting the old snapshot. Keep published handle/state and epoch updates in the normal resolve pass; clarify the existing API comment that currently claims only slot_alloc writes slot fields outside resolve. The narrow release-time invalidation is an explicit exception, like existing synchronous provider severing.

`nt_resource_get_blob` must follow the published `resolve_asset_idx` for the named record and effective owner readiness, rather than trusting an old runtime_handle index after release. `nt_resource_get_meta` already uses resolve_asset_idx. Between release and the next resolve, both views return NULL instead of exposing a replacement's bytes. The BLOB runtime-handle value itself retains its named-entry-index convention.

### Required consumer map

All anchors refer to HEAD 161b8768; re-anchor during implementation.

| Site in engine/resource/nt_resource.c | Required behavior |
| --- | --- |
| asset_effective_runtime_handle, line 198 | Owner handle for non-BLOB; named-entry index for BLOB |
| resource_resolve_pass D.2, lines 274-320 | Read owner state/handle; retain alias identity, ranking and byte view |
| asset_is_publishable / asset_data_ptr / callbacks, lines 200-225 and 394-453 | Keep alias and per-slot identity; never substitute owner metadata |
| Phase B activation, lines 705-758 | Skip aliases before state/budget checks; update dirty flag on success and failure |
| unmount / unregister, lines 925-981 and 1471-1487 | Destroy owners only; invalidate released identities; push each live index once |
| get_blob, lines 1311-1340 | Published named index plus effective readiness; NULL after release |
| get_meta, lines 1363 onward | Preserve per-name metadata; invalidated index prevents stale reuse |
| virtual register, lines 1442-1464 | Nonzero ID, self-owner, complete initialization; in-place existing-name update |
| asset_info, lines 1601-1623 | Per-name record enumeration with effective owner state |
| invalidate, lines 1723-1750 | File owners only; existing reload scheduling and dirty marking |
| test_set_asset_state, lines 1844-1852 | Redirect existing test mutation to the canonical owner |

nt_resource_source_of already reads published slots. DevAPI reads asset_info rather than internal AssetMeta, so it requires no new ownership field or lookup mechanism.

## Implementation sequence

### 1. Freeze evidence and contract

Read AGENTS.md, docs/build.md, spec/core/{principles,api-contracts}.md and full relevant assets/builder chapters. Inventory all NtAssetEntry constructors and version-field consumers before editing. Preserve the current qsort benchmark artifact and results alongside the original baseline for a three-way comparison.

Update the owner/alias, failure, free-slot, and activation-order contracts with the implementation. Relevant specs: assets/ntpack.md, assets/resource.md, assets/async-loading.md and builder/builder.md; public header comments only where their meaning changes.

### 2. Make the format, writer, and runtime transition coherent

Treat the format bump, both builder ownership paths, all manifest constructors, runtime parse/allocation/read-through/activation/teardown, and affected tests as one coherent migration. Do not commit a writer/reader mismatch or a compiler-broken intermediate tree.

Primary files:

- `shared/include/nt_pack_format.h`
- `tools/builder/nt_builder.c`, internal declarations and encoder result/call sites that carry only the removed manifest version
- `engine/resource/nt_resource.c`, `nt_resource_internal.h`, and relevant API comments in `nt_resource.h`
- All direct pack constructors, including the NT_TEST_ACCESS font fixture and test helpers; all pack dumps/readers that interpret the replaced field
- Relevant specs and deterministic generated example headers if regeneration changes them

Builder late dedup requires matching compatible type as well as encoded bytes/size. Set a new unique entry's owner to itself. For late or early duplicates, copy the canonical owner's index, not a possibly aliased intermediate index. Assert the canonical link invariant locally with NT_BUILD_ASSERT before copy/serialization. Preserve the duplicated name's own metadata. Do not add a second dedup framework or change source-content dedup/cache semantics unrelated to this contract.

Remove runtime qsort/comparator/range scratch and allocation cursor code. Replace the redundant manifest-version plumbing only after tracing consumers; retain all meaningful encoder/cache and payload-header versions.

Migrate `tools/builder/nt_builder_dump.c` explicitly: its find_duplicate_original currently rediscovers sharing by range, even though it does not read format_version. Validate and report owner_entry instead, including duplicate statistics; equal-range self-owned entries must not be relabelled aliases by the dump.

### Audited constructor and version inventory

- Wire/layout: shared/include/nt_pack_format.h; tests/unit/test_pack_format.c.
- Writer: tools/builder/nt_builder.c late registration and early-copy path, then metadata assignment and final serialization.
- Manifest-only plumbing to remove: NtEncodeResult.format_version and nt_builder_register_asset's version argument in nt_builder_internal.h / nt_builder.c; derive_asset_type's version output; texture/shader encoder out_version arguments in nt_builder_texture.c / nt_builder_shader.c and all corresponding declarations/call sites; cache-hit synthetic versions in nt_builder.c.
- Preserve raw encoded-payload caches: nt_builder_cache.c stores payload bytes, not NtAssetEntry. Keep NT_BUILDER_VERSION / ATLAS_CACHE_KEY_VERSION and payload-header versions. No blanket payload-cache invalidation is required just for the manifest migration.
- Direct fixtures: engine/font/nt_font.c (NT_TEST_ACCESS); tests/unit/test_resource.c; test_resource_timing.c; test_atlas.c; test_atlas_bench_parse.c; test_helpers/ui_atlas.c; test_sprite_comp.c; test_sprite_renderer.c; test_nt_sprite_renderer_emit_region.c; test_sponza_program_assignment.c. Initialize every independent entry to its own ordinal, including fixtures that formerly omitted format_version. Remove the redundant version argument from the Sponza fixture helper.
- Test readers/assertions: test_builder.c manifest-version assertions become owner assertions while retaining payload-header checks; test_atlas_pack_determinism.c; test_helpers/atlas_dedup_fixture.h; test_helpers/atlas_transform_fixture.h.
- Other reader: tools/research/atlas_bench/ntpack_parse.c uses shared format/version constants; ensure it accepts the new layout and never infers handle ownership from range equality.
- No direct example constructor or Python NTPACK entry parser was found. scripts/atlas/check_overlap.py reads atlas payloads, not the replaced manifest field. Re-run the inventory search before implementation to catch concurrent edits.

### 3. Prove ownership and slot lifecycle

Extend existing test fixtures and fake activators; add no new production hooks or generic test framework.

- Request only the alias name of two equal meshes; one owner activation and a READY alias, without requesting the original name.
- Make alias registry index lower than owner using fragmented free slots; no missing readiness, wrong handle or extra activation caused by index order.
- Multiple aliases, nonadjacent manifest entries, independent metadata per name, same offset/different size and zero-length ranges.
- Owner activator fails: one attempt, all names FAILED, no alias retry/leak; explicit invalidate followed by successful owner activation makes all names READY and eventually destroys one object. Cover failure on a later otherwise-clean step, after initial resolve, so the test catches missing dirty marking rather than inheriting parse's dirty flag.
- Equal payload bytes with different asset types: builder does not share handles. Malformed manifest declaring a cross-type alias is rejected before registration.
- Canonicalization when an early duplicate references a late duplicate; final owner links are flat.
- Unmount, invalidate, shutdown and repeated cycles destroy owners once; duplicate unregister/unmount does not duplicate free indices; virtual updates/unregisters never destroy caller-owned objects.
- Fill full capacity, free interleaved file/virtual entries, reuse repeatedly, and remount without leaked slots. Test zero-, one- and three-entry packs at a supported fixed capacity. Separate small smoke configurations cover capacities 1, an odd value and 65535; zero capacity remains a compile-time error. Keep these smokes independent of MAX_SLOTS and do not run an eight-entry fixture in a capacity-one build.
- Reject zero IDs in virtual register/unregister as programmer errors, and reject unregister on a file pack before releasing an owner. Catch these assertions before any fixture allocation that an assert-handler longjmp could leak.
- Unmount/unregister then reuse before step, with the same resource ID and same runtime-handle value: on_resolve/AUX data rebuilds, metadata and BLOB getters do not expose replacement bytes early, PIN_BLOB cleanup still precedes blob free. Cover multiple slots aliasing one owner and a lower-priority fallback in another pack.
- Invalid owner >= count, forward owner, owner chain, range/type mismatch, zero ID, and invalid later byte range: no partial registry change; corrected pack loads into the same mount.
- Blob aliases, PIN_BLOB/AUX_BACKED publication, distinct metadata, priority overlays in different packs, eviction/redownload and type invalidation retain named-entry identity and existing lifecycle behavior.
- Existing eight-entry fixture explicitly handles capacities smaller than its requirements; maximum-capacity smoke is not claimed as comprehensive alias coverage.

### 4. Measure and complete verification

Use the existing #449 timing getters and local one-off harness. Production Release, tests OFF, relevant timing ON, identical compiler flags and Node runtime. Build fresh before running; separate init, parse, CRC and activation costs. The new free-stack initialization belongs in the report rather than being hidden outside parse timing.

Compare original baseline, current qsort commit and new owner/stack implementation. Rebuild equivalent inputs for the format versions; identify the same logical content instead of requiring identical file bytes across a format bump.

Cases: real UI pack; small 1/8/16/64-entry packs; builder-produced unique and 50%-alias packs up to its standard 1024-entry limit; explicitly synthetic 2048-entry full-registry packs with authored owner indices in builder-like and permuted range order; fragmented reuse; maximum-capacity WASM smoke under default 64 KiB stack. Permutations characterize qsort sensitivity in the old implementation, not a sorting mechanism in the new reader. Alternate comparison runs, use enough warm-up/repetitions to expose variance, and report distributions rather than attributing a sub-microsecond delta to a specific mechanism. Measure alias-heavy activation using no-op/counting activators separately from GPU work, plus dirty resolve and clean-step costs so owner read-through is not hidden.

Report parse minus CRC, activation, init cost, standalone WASM size, representative example WASM size, sizeof(NtAssetMeta), persistent free-index bytes, and peak registration scratch. Expect removal of search/sort machinery; do not promise a numeric speedup or exact binary saving before measurement. Metrics already also uses qsort, so whole-game savings depend on linked modules.

Follow docs/build.md: format_and_check after edit bursts/before commits; push gate before any later push; no competing check/ctest processes. Exercise native Debug/Release, WASM Debug/Release and the diagnostic matrix. Direct tidy is required if new C test/tool files are added. Rebuild affected packs and commit deterministic changed generated headers. Sponza may remain explicitly skipped by the existing configured option.

Relevant existing targets: test_pack_format, test_resource, test_resource_timing, test_builder, test_atlas, test_atlas_bench_parse, test_atlas_pack_determinism, affected sprite/font/UI suites and test_sponza_program_assignment. Also compile/run the affected tests in native-release-test: production native-release omits test translation units. Build native packs before WASM copying.

Implementation stays on issue #450's branch and PR #461; no merge. The developer authorized implementation after plan approval. The final report and PR describe the ownership migration and its measured tradeoffs.

## Review and acceptance

Independent reviewers must challenge runtime simplicity, alias-before-owner order, all ownership reads, free-stack suffix lifetime, malformed input atomicity, type safety, metadata identity, and test evidence. At least two independent subagent critiques and a source-grounded Claude Fable critique are required; revise and re-review material changes until no actionable blocker remains, or state the unresolved decision explicitly.

Acceptance: no runtime range discovery; no free-slot scan; one activation/destruction per owned object; requesting alias alone works; metadata and pack independence preserved; no new transient registration allocation; no extra alias-state mirror; hard validation precedes mutation; tests and required gates pass. Timing/size outcomes and any regression remain visible in the report.

## Explicitly outside this migration

Do not expand this into general resource resolve optimization, CRC replacement, transport changes, cache rewrites or public ownership APIs. Existing BLOB payload-version validation is incomplete, and invalidate(BLOB) can strand a blob with no activator; those pre-existing behaviors are not silently claimed fixed by preserving payload validation. The scoped reuse-identity and failed-publication fixes above are included because the new ownership/free-stack guarantees depend on them.
