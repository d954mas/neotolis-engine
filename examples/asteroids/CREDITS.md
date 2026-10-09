# Asteroids demo: sources and credits

## Source-derived Neotolis port (in progress)

The baseline follows [Methane Asteroids](https://github.com/MethanePowered/MethaneAsteroids)
at [16a5751e835dd0776d976e51438604dc8de27d16](https://github.com/MethanePowered/MethaneAsteroids/tree/16a5751e835dd0776d976e51438604dc8de27d16).
The earlier independent ring prototype is superseded. This version ports the
original geometry, procedural textures, scene parameters, transforms, LOD and
shaders to Neotolis's C17 runtime and offline NTPACK pipeline. It does not import
Methane's RHI, DirectX, Metal or Vulkan backend.

- Original Methane Asteroids and MethaneKit author: Evgeny Gorodetskiy.
  Source-file notices cover 2019–2021; the project README carries 2019–2022.
- Modified source-derived exporter/runtime/shader files retain their Apache 2.0
  notices, add the Neotolis integration notice, and identify the changes.
- Complete [Apache License 2.0](licenses/MethaneAsteroids-Apache-2.0.txt) is included.
  The pinned Methane Asteroids tree has no NOTICE file.
- Neotolis Engine itself remains under its [MIT license](licenses/Neotolis-MIT.txt).
- No endorsement by the original project or asset creators is implied.

### Offline dependencies and source records

The original algorithms require the following source-only offline dependencies;
none is linked into the C17/WebAssembly runtime:

- MethaneKit v0.8.2, commit `04a95fb78334252594427c69737839518e53c4a0`:
  original `IcosahedronMesh`, `BaseMesh`, `SphereMesh` and `CubeMesh` methods,
  Apache 2.0.
- MethanePowered/FastNoise2 v1.1.1, commit
  `903c1f2d2f9d53ddce94cd223f32727d9ab3aeaa`, MIT.
- FastSIMD, commit `16450dae9528727e500e7254f635a671f9c7ee2d`, MIT.
- HLSL++ 3.9, commit `3a5b1cf0d807f945ec861201b316c425e9cf5061`, MIT.

Exact source URLs, per-file Git blob hashes, copied upstream method bodies,
modification notes and full dependency licenses are under
`reference_generator` and `reference_scene_data` in the source snapshot.

The exporter preserves the original random distributions, FastNoise settings,
independent LOD shapes, source topology/normal accumulation, level-specific RNG
windows, palettes and instance records. It runs those operations serially with
a recorded compiler/STL/SIMD target. Upstream parallel tasks share RNG state, so
the original does not supply a stable task-order-to-mesh mapping. This serial
ordering is an explicit compatibility choice, not a claim of matching any
particular multithreaded screenshot byte-for-byte.

## Mars texture

- Local file: `raw/Mars.jpg` (2048 × 1024, 750,547 bytes).
- Title: Mars, from Solar System Planets.
- Creator: [Solar System Scope](https://www.solarsystemscope.com/), developed by INOVE.
- Creator's source and terms: [Solar System Scope textures](https://www.solarsystemscope.com/textures/).
- License: [Creative Commons Attribution 4.0 International (CC BY 4.0)](https://creativecommons.org/licenses/by/4.0/).
  The full legal code is included in [licenses/CC-BY-4.0.txt](licenses/CC-BY-4.0.txt).
- Downloaded from the reference revision's [Resources/Textures/Planet/Mars.jpg](https://github.com/MethanePowered/MethaneAsteroids/blob/16a5751e835dd0776d976e51438604dc8de27d16/Resources/Textures/Planet/Mars.jpg).
- Source changes: none. The checked-in JPG is byte-for-byte identical to that
  upstream file. The offline builder decodes it at its original resolution into raw SRGBA8; runtime mipmaps are generated.
- Git blob SHA-1: `b3654a9b2d21c2910c38a0287423862650d54e1f`.
- SHA-256: `2d187f3e77a98eaa8cea5f4cc722f633c122ef170b9e94ace6b5fb6cbc3f8e01`.

Solar System Scope identifies its maps as based on NASA elevation and imagery
data, with adjusted colors and some fictional terrain in unmapped regions.
The image is credited to Solar System Scope and is not presented as an unmodified
NASA photograph.

## Galaxy sky

Galaxy panorama: ESO/S. Brunier, CC BY 4.0; adapted as a cubemap in Methane Asteroids.

- Panorama: [The Milky Way panorama, eso0932a](https://www.eso.org/public/images/eso0932a/).
- Creator credit: ESO/S. Brunier. See [ESO image-use terms](https://www.eso.org/public/outreach/copyright/).
- License: [Creative Commons Attribution 4.0](https://creativecommons.org/licenses/by/4.0/);
  the full legal code is included in [licenses/CC-BY-4.0.txt](licenses/CC-BY-4.0.txt).
- Changes in the upstream demo: panorama projection to six cube faces and resizing.
  This Neotolis port retains the upstream JPEGs byte-for-byte.

`raw/Galaxy/{Positive,Negative}{X,Y,Z}.jpg` are the six unchanged 2048×2048
files from the pinned Methane Asteroids revision. Source URLs, Git blob hashes
and SHA-256 values are in `raw/Galaxy/source-manifest.json`. Runtime uses all
six full-resolution faces; it does not substitute a different star map.

### Provenance evidence and distinction

The six faces were [introduced in MethaneKit on 2019-09-06](https://github.com/MethanePowered/MethaneKit/commit/2d8028d770d809ddea41efa29aa26898e8e53184),
whose [LICENSE at that revision](https://github.com/MethanePowered/MethaneKit/blob/2d8028d770d809ddea41efa29aa26898e8e53184/LICENSE)
was Apache 2.0. The [2019-09-15 resizing commit](https://github.com/MethanePowered/MethaneKit/commit/6893b4bccda970cdf78a01b5617a2d3a5a7990fe)
produced the current 2048×2048 faces, later carried unchanged into the
[2022 Methane Asteroids split](https://github.com/MethanePowered/MethaneAsteroids/commit/cf337225a26753ed921fc4f76f1a076f5d1209c4).
The pinned [sample README](https://github.com/MethanePowered/MethaneAsteroids/blob/16a5751e835dd0776d976e51438604dc8de27d16/README.md#license)
also declares Apache 2.0 for the sample. No Galaxy-specific exception was found.
The Apache notices are retained for the source-derived demo and its adaptations;
they do not replace the underlying photograph's supplemental attribution.

ESO/S. Brunier was identified by reproducible image comparison with ESO's
published panorama, not by an explicit upstream photo credit. Reprojection and
brightness normalization matched PositiveX with correlation 0.9856 and NegativeZ
with 0.9765; the fitted face orientations differ by approximately 90 degrees.
The [provenance receipt](licenses/Galaxy-provenance.json) records the sources,
file hashes and fitted parameters; correlations are similarities, not probabilities.
This evidence supports the supplemental panorama credit above. Evgeny
Gorodetskiy is credited as the demo author, not as the panorama photographer.

Representation change: six 2D samplers implement cubemap lookup through the
current Neotolis interface. Sampling/filtering differences are tracked in the
baseline parity record; the source JPEGs are unchanged. No endorsement by ESO,
S. Brunier or the original demo author is implied.

## HUD font

- Local file: `raw/DejaVuSansMono.ttf`.
- Family: [DejaVu Sans Mono](https://dejavu-fonts.github.io/).
- Copyright (c) 2003 by Bitstream, Inc. All Rights Reserved.
  Bitstream Vera is a trademark of Bitstream, Inc. DejaVu changes are in the public domain.
- License: Bitstream Vera font license, reproduced in
  [licenses/DejaVu-Fonts-copyright.txt](licenses/DejaVu-Fonts-copyright.txt).
- Source: the unmodified system package file
  `/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf`; its corresponding copyright
  document is copied from `/usr/share/doc/fonts-dejavu-core/copyright`.
  The document's separate Debian packaging section describes the packaging,
  not the font's license.
- Source changes: none. The builder extracts the glyphs needed by the HUD.
- SHA-256: `54bf827eb99404e8f430c330ad30f063334f637eba0109b6a18d4f566a8e9dd8`.

## Reproducible baseline data

The source snapshot includes `reference_generator/README.md` and
`reference_scene_data/README.md` with commands and validation. Original method-body oracles check positions, normals, indices,
depth ranges, all 145,000 preset instances, and procedural texture layers.
Generated GLBs and raw records live under the build directory and pass through
the existing Neotolis builder. No source parsers, FastNoise or C++ run in-game.

## Native control panel

The optional native panel uses existing Neotolis UI components and a small
procedural, tintable atlas generated by this example. It adds no third-party UI
art, font dependency or HTML overlay. Source-style presentation remains available
with `--ui 0`. See [PARITY.md](PARITY.md) for exact compatibility and QA limits.
