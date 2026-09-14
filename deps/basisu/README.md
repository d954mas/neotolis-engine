# Basis Universal vendor pin

Upstream: [BinomialLLC/basis_universal, v2_50](https://github.com/BinomialLLC/basis_universal/tree/v2_50).
Commit: `9bebe16726b3a61c8c213eeee3b7cffb462ef34e`.
[Source archive](https://github.com/BinomialLLC/basis_universal/archive/refs/tags/v2_50.tar.gz)
SHA-256: `216e49e1f4213d4bfa4afaa07527e16bac28533dddd444197d3aa19230ac130c`.

The source subset is copied byte-for-byte, except the transcoder patch
described below:

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

## Local transcoder patch

`transcoder/basisu_transcoder.cpp` and `transcoder/basisu_transcoder.h` carry a
guards-only patch (`#if`/`#else`/`#endif` and one moved definition, no logic
changes) so the WASM runtime can drop decoder paths the engine never reaches.
Upstream cannot build with those flags off (BinomialLLC/basis_universal#196).
Reapply the patch on every vendor update; an upstream pull request is pending.

- `BASISD_SUPPORT_XUASTC=0` / `BASISD_SUPPORT_UASTC_HDR=0` / `BASISD_SUPPORT_UASTC=0`
  compile: the XBC7 decoder include, `arith_fastbits_f32` globals,
  `blocks_same_single_subset_endpoints` and the XBC7 `.inl` are guarded;
  `g_bc7_weights2` moves out of the UASTC region because the ETC1S→BC7 path
  reads it too.
- New `BASISD_SUPPORT_ETC1S` (default 1): guards the
  `basisu_lowlevel_etc1s_transcoder` implementation, the ETC1S branches of
  `start_transcoding`, `stop_transcoding`, `transcode_slice`,
  `transcode_image_level`, the decoder member and its accessors.
  `basis_validate_output_buffer_size` and
  `basis_compute_transcoded_image_size_in_bytes` stay shared.
- New `BASISD_SUPPORT_16BPP_FORMATS` (default 1): guards the RGB565/BGR565/
  RGBA4444 output cases. UASTC→BC1/BC3/BC4/BC5 cases now honor the existing
  `BASISD_SUPPORT_DXT1`/`BASISD_SUPPORT_DXT5A`. `basis_is_format_supported`
  answers false for every removed path.
- The BC1 single-colour init tables stay under `DXT1 || UASTC`: the native
  UASTC encoder reads them through `encode_bc1` for its hints.

With the native defines in `engine/basisu/CMakeLists.txt` the encoder
translation units preprocess identically to the pristine v2_50 sources; the
transcoder loses only the UASTC→BC1/BC3/BC4/BC5 cases (`DXT1=0`, `DXT5A=0`).

The builder's encoder source list is explicit in
[tools/builder/CMakeLists.txt](../../tools/builder/CMakeLists.txt).
Native encoder and runtime share the single upstream transcoder translation unit
and `nt_basisu_transcoder_config` from
[engine/basisu/CMakeLists.txt](../../engine/basisu/CMakeLists.txt).

Engine input is ETC1S/UASTC LDR `.basis`; outputs are ETC1 RGB, ETC2 RGBA,
BC7 RGBA, ASTC 4x4 RGBA, and RGBA32.
