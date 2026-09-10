# Basis Universal vendor pin

Upstream: [BinomialLLC/basis_universal, v2_50](https://github.com/BinomialLLC/basis_universal/tree/v2_50).
Commit: `9bebe16726b3a61c8c213eeee3b7cffb462ef34e`.
[Source archive](https://github.com/BinomialLLC/basis_universal/archive/refs/tags/v2_50.tar.gz)
SHA-256: `216e49e1f4213d4bfa4afaa07527e16bac28533dddd444197d3aa19230ac130c`.

The source subset is copied byte-for-byte, with no local vendor patches:

- All top-level `transcoder/` files.
- All top-level `encoder/` files except `basisu_wasm_*` (upstream JavaScript bindings).
- Regular files directly inside `encoder/3rdparty/`; the separate `astcenc/` implementation is omitted.
- The upstream [Apache-2.0 license](LICENSE). Third-party notices remain in their source files.
- `zstd/zstd.h`, `zstd/zstd_errors.h`, and the upstream [BSD license](zstd/LICENSE).
  The new `basisu_xbc7_encode.cpp` includes `zstd.h` unconditionally, even with
  `BASISD_SUPPORT_KTX2_ZSTD=0`. Only the declarations are vendored; no Zstd
  implementation is compiled or linked.

The builder's encoder source list is explicit in
[tools/builder/CMakeLists.txt](../../tools/builder/CMakeLists.txt).
Native encoder and runtime share the single upstream transcoder translation unit
and `nt_basisu_transcoder_config` from
[engine/basisu/CMakeLists.txt](../../engine/basisu/CMakeLists.txt).
Existing wrapper calls need no compatibility shims for this version.

Engine input remains ETC1S/UASTC LDR `.basis`; outputs remain ETC1 RGB, ETC2 RGBA,
BC7 RGBA, ASTC 4x4 RGBA, and RGBA32. Compiled upstream HDR/XUASTC helpers do not
constitute new engine APIs. KTX2/Zstd input, HDR/XUASTC features, new output formats,
and a format configuration redesign are outside this update.

See the [upgrade evidence](../../docs/basisu-2.50.md) for effective flags, size,
cache invalidation, and native/browser verification.
