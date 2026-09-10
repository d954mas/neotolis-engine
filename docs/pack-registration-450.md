# Pack registration comparison (#450)

Historical qsort experiment at `161b8768`. The subsequent owner/free-stack
migration, current behavior and three-way measurements are recorded in
[Resource ownership migration](resource-ownership-450.md).

Baseline: `0c26ded1b0e710bae0ced0147307966496324e38`, including #449 and #459.
Measured 2026-09-10 on Windows, Emscripten 4.0.19 Release (`-O3 -DNDEBUG`,
default TRAP asserts), `NT_RESOURCE_TIMING_ENABLED=ON`, `NT_BUILD_TESTS=OFF`,
`NT_LOG_MIN_LEVEL=1`; other diagnostic producers OFF. Both versions use the
same CMake wrapper, compiler options, input bytes and Node v24.15.0.

The initial baseline was recorded before changing the resource implementation.
The final comparison runs Node with `--no-liftoff` to use the optimizing WASM
compiler from the start, avoiding tier changes during short measurements.
Each dataset has one warm-up and seven measured parses. Each repetition starts
with a fresh registry, mounts, parses, reads the existing parse/CRC getters,
unmounts and shuts down. Input creation, I/O, init and teardown are outside the
reported parse time; no resource requests or activation are measured.

## Inputs and medians

| Dataset | Entries / distinct ranges | Bytes | Parse ms, before → after | CRC ms, before → after | Parse minus CRC ms, before → after |
| --- | --- | --- | --- | --- | --- |
| UI showcase | 16 / 16 | 1,222,552 | 2.1544 → 2.2288 | 2.1538 → 2.2278 | 0.0016 → 0.0019 |
| Tiny unique | 2048 / 2048 | 81,952 | 1.4633 → 0.4024 | 0.0583 → 0.0569 | 1.4068 → 0.3462 |
| Tiny aliases | 2048 / 1024 | 81,952 | 1.1092 → 0.4104 | 0.0568 → 0.0583 | 1.0468 → 0.3521 |

Each column is an independently calculated median; the median difference need
not equal the difference of medians. Registration and other work outside CRC
improved **4.06×** for unique ranges and **2.97×** for aliases. Overall parse
improved **3.64×** and **2.70×**, respectively.

The real pack's 0.0744 ms total difference tracks CRC's 0.0740 ms difference;
outside CRC the median changed by 0.0003 ms. This small sample shows no material
registration regression, not a claim of faster real-pack loading.

The real input is `build/examples/ui_showcase/ui_showcase.ntpack`, containing
textures, shader code, fonts and an atlas. The copy from the #449 worktree and
the freshly rebuilt pack had the same SHA-256:
`cc5ff57cef24ed0661c9463af7a626cc2fa9014050ed07e4ff729e0f13e1441c`.

Synthetic entries have IDs `i + 1`, texture type, format version 1, 16-byte
payloads, no metadata and offsets `header_size + ((i * 127) % 2048) * 16`.
This permutes the range order. The alias variant changes the second half's
offsets to those of the corresponding first-half entries. Both variants keep
the same zero-filled payload region and CRC cost.

## Raw parse / CRC samples (milliseconds)

| Dataset / version | Seven measured pairs, in order |
| --- | --- |
| Real before | 2.1705/2.1677, 2.2947/2.2775, 2.1273/2.1261, 2.1544/2.1538, 2.0713/2.0707, 2.2376/2.2354, 2.1139/2.1123 |
| Real after | 2.1994/2.1954, 2.3270/2.3096, 2.2254/2.2229, 2.5553/2.5537, 2.3437/2.3418, 2.1564/2.1555, 2.2288/2.2278 |
| Unique before | 1.5298/0.0551, 1.5572/0.0583, 1.6768/0.0655, 1.4236/0.0584, 1.4633/0.0565, 1.3931/0.0584, 1.3831/0.0557 |
| Unique after | 0.3908/0.0582, 0.4029/0.0567, 0.4024/0.0559, 0.3973/0.0569, 0.3936/0.0567, 0.4519/0.0576, 0.4162/0.0585 |
| Aliases before | 1.1148/0.0585, 1.0851/0.0583, 1.0989/0.0566, 1.1092/0.0559, 1.0835/0.0551, 1.1105/0.0568, 1.1155/0.0687 |
| Aliases after | 0.4104/0.0583, 0.4222/0.0583, 0.4064/0.0581, 0.4069/0.0583, 0.4040/0.0576, 0.4312/0.0566, 0.4492/0.0584 |

## Implementation and limits

- One local allocation cursor scans holes once per pack. Virtual registration
  retains its existing duplicate lookup and starts hole allocation at zero.
- Sorting record indices by offset, size and index replaces all-prior-entry
  comparisons. The pinned Emscripten libc uses smoothsort: worst-case O(N log N).
  Registry order and first-entry ownership remain unchanged.
- Temporary load storage uses two bytes per entry, 4096 bytes at the default
  capacity, and is freed before resident metadata is allocated. There is no new
  persistent state. A fixed stack array was rejected because the supported
  65535-asset override would exceed Emscripten's default 64 KiB stack.
- The standalone measured WASM grew from 25,037 to 26,603 bytes (+1566 bytes).
  This includes pulling in the libc sort routine; it is not a whole-game size delta.
- Native benchmark processes stalled before main, so no native timing claim is
  made. The one-off harness and complete CSVs remain locally under
  `build/450-bench/`; they are not a new engine benchmark subsystem.
- Activation's repeated alias lookup remains #368. Browser transport, JS-to-WASM
  copying, GPU completion and end-to-end loading are outside these measurements.

Behavioral coverage in `test_resource` checks nonadjacent aliases, equal offsets
with differing sizes, zero-size ranges, activation order, invalidation/reactivation,
single deactivation per owner, repeated full-capacity mixed file/virtual hole reuse,
and the existing invalid-later-entry rejection followed by corrected loading.
The NTPACK format and public API/lifecycle contracts are unchanged.

A separate Release/WASM smoke with `NT_RESOURCE_MAX_ASSETS=65535` and the
standard 64 KiB stack parsed and unmounted packs with 0, 1 and 65535 entries;
asset counts matched before and after each unmount.

Repository validation: `bash scripts/format_and_check.sh --push` passed, including
143/143 CTest targets, native Debug/Release, WASM Debug/Release, format/tidy,
submodule consumption, diagnostics configuration and all eight runtime matrix
configurations. Example builds used `NT_SKIP_EXAMPLE_PACKS=sponza`; the expensive
Sponza encode and browser runtime/visual checks were not performed.
