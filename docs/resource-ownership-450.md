# Resource ownership migration (#450)

NTPACK v3 moves canonical ownership into the builder. Runtime registration now
uses a free-index stack and translates owner ordinals without sorting or temporary
allocation. Either asset name works independently; one canonical record activates
and destroys the shared object. Metadata and published BLOB identity remain per name.

The implementation also fixes delayed FAILED publication and same-index/same-handle
reuse retaining stale AUX data. Virtual IDs must be nonzero; unregister is virtual-only
and parse requires a file mount. The public game-facing function signatures are unchanged;
the binary format and internal builder encoder signatures deliberately break compatibility.
Old packs must be rebuilt; encoded payload caches remain valid.

## Measurement method

Measured on Windows, 2026-09-10, Emscripten 4.0.19 Release (-O3, NDEBUG, default
TRAP assertions), tests OFF, resource timing ON, other diagnostic producers OFF,
Node v24.15.0 with --no-liftoff. Three versions use one identical local harness:

- Original: 0c26ded1b0e710bae0ced0147307966496324e38; source snapshot verified byte-identical after newline normalization.
- qsort: 161b876899687205737bffd29671513ab5c0fc1f; previous branch implementation.
- Owner stack: the implementation accompanying this report.

Each process warms every case eight times, then measures 31 repetitions. Three
rounds rotate version order: 93 samples per case/version. All binaries were rebuilt
before measurement, and no builds or tests ran concurrently with timing.
Each repetition initializes a registry, optionally creates fragmented holes, mounts,
parses and steps. Initialization is measured separately. Dirty-step averages cover
40 priority changes and steps; clean-step averages cover 100 steps. Synthetic
activation uses counting/no-op mesh activators, unlimited budget and requests up to
the configured slot limit. Each run verifies matching activation/destruction counts.
Real UI and builder BLOB cases measure registration without GPU activation or requests.

The real UI pack contains 16 entries and 1,222,552 bytes. Builder fixtures actually
use add_blob/finish_pack for 1024 unique payloads or 1024 names sharing 512 payloads.
Equivalent v2 fixtures change only manifest/header version fields. Synthetic cases
cover 1/8/16/64/1024/2048 entries, unique and 50%-alias layouts, both ordered and
permuted ranges. Fragmentation fills the registry with virtual assets, frees even
indices, then parses aliases into those holes. Source and raw CSV/JSON artifacts
remain local under build/450-owner-bench; no benchmark subsystem was added to runtime.

## Registration excluding CRC

Median microseconds; each sample subtracts its own nested CRC duration before
aggregation. Tiny sub-microsecond differences should not be interpreted as stable
speedups. CRC itself is unchanged.

| Case | Original us | qsort us | Owner stack us |
| --- | ---: | ---: | ---: |
| UI showcase | 1.100 | 2.600 | 1.200 |
| Builder: 1024 unique BLOBs | 358.400 | 17.500 | 3.800 |
| Builder: 1024 names / 512 BLOBs | 281.300 | 144.200 | 3.600 |
| Synthetic: 16 unique, ordered | 0.300 | 0.600 | 0.200 |
| Synthetic: 2048 unique, permuted | 1415.800 | 367.900 | 7.700 |
| Synthetic: 2048 names / 1024 owners, permuted | 1111.400 | 370.900 | 7.300 |
| Fragmented: 1024 names / 512 owners | 463.700 | 152.400 | 4.200 |

For permuted full-capacity packs, registration excluding CRC is
47.8x faster than qsort for unique entries and
50.8x for aliases. These ratios describe synthetic registry work, not network or GPU loading.

## Total parse and variability

Milliseconds, median [p10, p90]. These are observed sample quantiles, not confidence intervals.

| Case | Original | qsort | Owner stack |
| --- | ---: | ---: | ---: |
| UI showcase | 2.181300 [2.135600, 2.287200] | 2.188300 [2.075200, 2.404200] | 2.192600 [2.067200, 2.311500] |
| Builder: 1024 unique BLOBs | 0.404500 [0.380000, 0.485100] | 0.061200 [0.056000, 0.073700] | 0.047500 [0.044000, 0.050900] |
| Builder: 1024 names / 512 BLOBs | 0.305000 [0.283300, 0.321200] | 0.166200 [0.156800, 0.201100] | 0.025900 [0.024400, 0.028200] |
| Synthetic: 16 unique, ordered | 0.000800 [0.000700, 0.001300] | 0.001100 [0.001000, 0.001400] | 0.000700 [0.000700, 0.000800] |
| Synthetic: 2048 unique, permuted | 1.477800 [1.382200, 1.567100] | 0.428900 [0.395500, 0.465900] | 0.068700 [0.065400, 0.072300] |
| Synthetic: 2048 names / 1024 owners, permuted | 1.174400 [1.083700, 1.322000] | 0.432000 [0.411800, 0.515400] | 0.068200 [0.063800, 0.071100] |
| Fragmented: 1024 names / 512 owners | 0.494700 [0.458700, 0.602600] | 0.182800 [0.170800, 0.200500] | 0.033900 [0.032000, 0.036500] |

UI loading remains dominated by CRC: the total medians are about 2.19 ms for both
qsort and owner stack. The distributions overlap; this is not evidence of a real
UI loading speedup or slowdown. The previous 2.1544 -> 2.2288 ms sample is preserved
in [the original qsort report](pack-registration-450.md).

## Activation, resolve and initialization

Median microseconds. Activation is CPU work with fake objects, not GPU completion.
Dirty step includes the existing resolve pass; clean step still scans resident
owners as required by the existing activation loop.

| Case / metric | Original us | qsort us | Owner stack us |
| --- | ---: | ---: | ---: |
| Synthetic: 2048 unique, permuted / activate_ms | 101.800 | 102.100 | 99.400 |
| Synthetic: 2048 unique, permuted / dirty_step_ms | 21.192 | 20.950 | 21.270 |
| Synthetic: 2048 unique, permuted / clean_step_ms | 1.845 | 1.956 | 2.004 |
| Synthetic: 2048 names / 1024 owners, permuted / activate_ms | 469.200 | 453.800 | 49.900 |
| Synthetic: 2048 names / 1024 owners, permuted / dirty_step_ms | 21.750 | 20.922 | 21.095 |
| Synthetic: 2048 names / 1024 owners, permuted / clean_step_ms | 1.890 | 1.980 | 1.822 |
| Fragmented: 1024 names / 512 owners / activate_ms | 214.900 | 212.900 | 25.000 |
| Fragmented: 1024 names / 512 owners / dirty_step_ms | 15.060 | 15.370 | 14.342 |
| Fragmented: 1024 names / 512 owners / clean_step_ms | 1.985 | 2.570 | 1.688 |
| Registry init (UI case) | 2.300 | 2.400 | 2.800 |

Initialization increases by approximately 0.4 us versus qsort in this run. Dirty
resolve and clean-step medians are close; this migration does not claim to optimize
those loops. The large alias activation improvement comes from removing repeated
searches for a READY copy and from skipping aliases before activation-budget work.

## Size and storage

| Measurement | Original | qsort | Owner stack |
| --- | ---: | ---: | ---: |
| Standalone harness WASM, bytes | 36198 | 37764 | 37228 |
| UI showcase WASM, bytes | not measured | 409810 | 408611 |
| sizeof(NtAssetMeta), bytes | 32 | 32 | 32 |
| Temporary registration indices at 2048 entries, bytes | 0 | 4096 | 0 |
| New persistent free-index fields, bytes | 0 | 0 | 4100 |

The compiled WASM s_resource symbol grows from 165288 to 169392 bytes (+4104,
including alignment), measured with llvm-nm. The free-index array itself is 4096
bytes plus a 4-byte count. Metadata copies, input blobs and resolve scratch are
unchanged and excluded from registration-index storage above.

The standalone comparison exercises more resource APIs than the earlier parse-only
harness, so its absolute sizes must not be compared with that report's 25–26 KiB.
Relative to qsort, this harness shrinks 536 bytes and UI showcase shrinks 1199 bytes.
Relative to the original scan implementation, the harness is 1030 bytes larger.
The tradeoff buys explicit ownership, O(1) slot pop/push and safe reuse; it is not
presented as a universal binary-size reduction.

## Verification

- Three baseline regression tests failed before the fix: delayed FAILED publication (expected FAILED, got REGISTERED), alias retry (expected one activation, got two), and virtual same-handle AUX reuse (expected two resolve callbacks, got one).
- Full format_and_check.sh --push: PASS, 143/143 native Debug CTest targets, native/WASM Debug/Release builds, full clang-tidy, submodule consumption and diagnostics configuration/runtime matrix.
- native-release-test: 13/13 affected CTest targets pass, including resource, builder, font, atlas and sprite paths.
- Release/WASM with a standard 64 KiB stack: capacities 1, 7 and 65535 pass zero-entry, one-entry and full-capacity parse/unmount smoke tests. NtAssetMeta remains 32 bytes.
- Tests cover alias-only requests with reversed registry order; explicit recovery; invalid links rejected atomically; mixed file/virtual hole reuse; separate per-name BLOB metadata; same-index getters before publication; independently owned equal ranges; alias-specific AUX/PIN fallback, eviction/redownload and synchronous provider severing.
- Builder tests prove cross-type byte equality does not share ownership, early duplicates of late aliases flatten to the canonical owner, and metadata remains per name.
- A local real dump probe distinguishes self-owned equal ranges from an explicit alias and rejects an invalid owner. No production test hook was added.
- Four golden packs changed only in header/manifest fields: restoring v2 version fields reproduces the old SHA-256 exactly. Structural dumps and payloads remain unchanged. Regenerated example headers produced no content changes.

One intermediate gate found an invalid test callback replacement and expected
format/golden updates; these were corrected. An intermediate graphics test process
also failed, then passed in the fresh full gate. Final gate results above are authoritative.

Sponza's expensive pack encode remains skipped by the configured build option;
its program-assignment test passes. Browser visual QA, network transport,
JS-to-WASM copying and GPU completion were not measured. Existing general resolve
allocation and BLOB payload-version/invalidate behavior remain outside this change.
