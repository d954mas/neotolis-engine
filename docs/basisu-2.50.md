# Basis Universal 2.50 upgrade evidence

Date: 2026-09-10. Source baseline: `4414b2c48b29c66e4d2e54c335615cc9793b1f93`
(vendored version `02.10`, `BASISD_LIB_VERSION=210`).
The [new pin, archive digest, licenses, and subset](../deps/basisu/README.md)
identify version `02.50`, `BASISD_LIB_VERSION=250`.

## Configuration

No engine feature defines changed. Both consumers use the existing shared native
transcoder translation unit and matching interface compile definitions.
The encoder source list follows the new upstream files; the C API is unchanged.
LDR mip generation explicitly sets `m_mip_srgb = false` in the encoder wrapper.
Stored RGB and alpha values must use the same linear filter to preserve
premultiplied data. Basis 2.50 defaults this parameter to true; accepting that
default gamma-filters RGB while leaving alpha linear.

Values below are equal before and after the upgrade. Names have the
`BASISD_SUPPORT_` prefix. These are effective preprocessor values, including
defaults from upstream, rather than just CMake arguments.

| Macros | Native | WASM |
| --- | --- | --- |
| DXT1, DXT5A, PVRTC1, PVRTC2, FXT1, ATC, KTX2_ZSTD | 0 | 0 |
| BC7, ASTC, ETC2_EAC_A8 | 1 | 1 |
| UASTC, UASTC_HDR, XUASTC, ETC2_EAC_RG11, BC7_MODE5 (implicit defaults) | 1 | 1 |
| KTX2 | 1 | 0 |
| ASTC_HIGHER_OPAQUE_QUALITY (implicit platform default) | 1 | 0 |

Native KTX2 helpers are required by the encoder and do not expose KTX2 engine
input. The native encoder retains `BASISU_SUPPORT_SSE=1`. HDR/XUASTC defaults were
already enabled at the baseline; trimming them and testing OFF combinations is
separate work. No Zstd library is built or linked.

Full before/after snapshots are under [basisu evidence](evidence/basisu-2.50/README.md).
They were captured with the actual `compile_commands.json` command for each TU,
replacing compilation with `-E -dM` and retaining sorted `BASISD*`/`BASISU*`
defines. The encoder wrapper snapshot includes only macros visible through its
headers; implementation-only defaults appear in the upstream transcoder snapshot.

## Comparable size

Windows x64, clang 19.1.7, CMake 3.31.6, Ninja, pinned Emscripten 4.0.19
(`08e2de1031913e4ba7963b1c56f35f036a7d4d56`). Same `wasm-release` preset,
`bunnymark_demo` target, toolchain, build directory, and engine format defines.
Production assertions are TRAP. Gzip uses Python `gzip.compress(data, mtime=0)`
at its default level 9. Sizes are bytes.

| Artifact | 2.10 raw / gzip | 2.50 raw / gzip | Delta raw / gzip |
| --- | ---: | ---: | ---: |
| index.wasm | 782174 / 335698 | 786345 / 337095 | +4171 / +1397 |
| index.js | 19776 / 7994 | 19776 / 7994 | 0 / 0 |

Both size builds set `NT_SKIP_EXAMPLE_PACKS=sponza;bunnymark`. This isolates code
size; asset bytes are excluded and existing packs are not used as encoder
correctness evidence. This is the combined upstream upgrade delta, not a
per-codec attribution or a claim about every application.

## Conversion and cache evidence

`test_basisu_roundtrip` links encoder and real runtime in the same native
executable. Its four Unity cases cover both ETC1S and UASTC, RGB and varying
alpha, dimensions 16x8, 13x7, 1x1 and 96x64, and every generated mip through all
five existing outputs. Checks cover dimensions, successful conversion, output
canaries, and asymmetric base-level RGBA pixels with codec tolerance. Compressed
conversion is CPU evidence; GPU sampling is measured separately below.

Two additional Unity cases encode an 8x8 premultiplied opaque-white/transparent-black
checker and verify its decoded 1x1 mip against an independent RGBA `(128,128,128,128)`
reference with tolerance 8. Before the explicit linear-filter setting, ETC1S
failed with RGB 187 and UASTC with RGB 188. The check frees the encoded data and
closes the transcoder session before comparing pixels, including on failure.
Both cases pass with the explicit linear filter in native-debug and
native-release-test/NDEBUG.

For cache invalidation, the identical four-texture producer at
`cb107e8a` was run with the same cache directory: builder version 1 produced
0 hits / 4 misses; version 2 produced 0 hits / 4 misses; its next run produced
4 hits / 0 misses. The final `NT_BUILDER_VERSION` is 3, invalidating both the
2.10 cache and the intermediate version-2 cache encoded with sRGB mip filtering.
The existing cache key mechanism suffices. No cache registry or TTEX wire-version
change is added.
With the same final fixture source and its populated version-2 cache, version 3
produced 0 hits / 4 misses; the next run produced 4 hits / 0 misses.
The final fixture adds a blue checker pattern to make selecting the wrong mip
observable; that source change is independent of the version-isolation test.

## Real browser fixture

The small native producer creates `fixture.ntpack` independently of all example
packs. It contains ETC1S/UASTC RGB and varying-alpha 96x64 textures, seven mips,
asymmetric red/green gradients, and a blue checker pattern. The browser fixture
links the real transcoder, fetches that pack, and uses ordinary resource mounting,
resolution, and texture activation. Existing stub browser smoke stays separate.

Each run performs 140 CPU conversions (four textures, seven mips, five outputs)
and 28 offscreen GPU samples (four textures, seven explicit LODs). Nearest
sampling and top-down public readback are compared per channel against CPU RGBA
references, with mean absolute error below 24/255. Base-level references are
also checked against the source pixels. Failures remain active in Release,
independent of engine assertions. Browser exceptions, console errors, HTTP
failure, wrong preset/assert mode, and GL errors fail the Playwright test.

A negative witness replaced every sampled LOD with zero in the Release fixture.
With linear mip filtering, the pixel check rejected texture 0, mip 1, blue
channel (MAE 57 against the limit 24). The mutation was restored before rebuilding
the passing fixture.

| Browser configuration | Debug FULL / Release TRAP | Default GPU sampling |
| --- | --- | --- |
| Chromium ANGLE SwiftShader Vulkan | 140 CPU / 28 GPU each; GL error 0 | BC7, software GPU |
| Chromium ANGLE Intel UHD Graphics (0x0000A788), D3D11 | 140 CPU / 28 GPU each; GL error 0 | BC7, hardware GPU |
| Hardware ASTC / ETC2 | Unverified: not exposed by this Intel adapter | No hardware sampling claim |

SwiftShader exposes BC7, ASTC and ETC2, but normal selection chooses BC7. These
runs do not claim GPU coverage for the other families. All five families are
covered by actual CPU conversion in native and WASM. The existing actual-format
reporting discrepancy is unchanged and belongs to the subsequent upload task;
the fixture does not use that query as proof of physical GPU storage.

## Reproduction

Activate the pinned emsdk first. On Windows use Git Bash, not WSL. Install the
locked browser dependencies once with `cd tests/browser && npm ci && npx
playwright install chromium`, then return to the repository root.

```sh
cmake --preset native-debug
cmake --build --preset native-debug --target test_basisu_roundtrip
ctest --test-dir build/_cmake/native-debug -R '^test_basisu_roundtrip$' --output-on-failure
bash scripts/check_basisu_browser.sh
NT_BASIS_HARDWARE=1 NT_BASIS_PORT=8463 bash scripts/check_basisu_browser.sh
```

The runner rebuilds the native fixture and both WASM fixtures before each run,
serves each exact preset directory on a separate port, and forbids server reuse.
Hardware mode uses full Chromium with GPU enabled and rejects software renderer
names. CI runs the default software browser matrix in the existing browser job.
The producer prints its builder version and cache hit/miss summary.

Repository gates remain `bash scripts/format_and_check.sh`,
`bash scripts/check.sh --full`, and `bash scripts/check.sh --push`.
