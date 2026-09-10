# Basis Universal vendor pin

Upstream: [BinomialLLC/basis_universal, v2_50](https://github.com/BinomialLLC/basis_universal/tree/v2_50).
Commit: `9bebe16726b3a61c8c213eeee3b7cffb462ef34e`.
[Source archive](https://github.com/BinomialLLC/basis_universal/archive/refs/tags/v2_50.tar.gz)
SHA-256: `216e49e1f4213d4bfa4afaa07527e16bac28533dddd444197d3aa19230ac130c`.

The source subset is copied byte-for-byte, with no local vendor patches:

- All top-level `transcoder/` files.
- Top-level `encoder/` files except `basisu_wasm_*` (JavaScript bindings),
  `basisu_bc15_spmd*` (standalone BC1/BC4 encoder), `basisu_dds_export.*`, and
  `basisu_bc7e_scalar.cpp`. The BC7E implementation has no consumer with
  `BASISD_SUPPORT_KTX2_ZSTD=0`; its header is required by XBC7.
- Regular files directly inside `encoder/3rdparty/`; the separate `astcenc/` implementation is omitted.
- The upstream [Apache-2.0 license](LICENSE). Third-party notices remain in their source files.
- `zstd/zstd.h`, `zstd/zstd_errors.h`, and the upstream [BSD license](zstd/LICENSE).
  `basisu_xbc7_encode.cpp` includes `zstd.h` unconditionally, even with
  `BASISD_SUPPORT_KTX2_ZSTD=0`. Only the declarations are vendored; no Zstd
  implementation is compiled or linked.

The builder's encoder source list is explicit in
[tools/builder/CMakeLists.txt](../../tools/builder/CMakeLists.txt).
Native encoder and runtime share the single upstream transcoder translation unit
and `nt_basisu_transcoder_config` from
[engine/basisu/CMakeLists.txt](../../engine/basisu/CMakeLists.txt).

Engine input is ETC1S/UASTC LDR `.basis`; outputs are ETC1 RGB, ETC2 RGBA,
BC7 RGBA, ASTC 4x4 RGBA, and RGBA32.
