# Offline Methane scene and procedural texture export

This is an offline C++20 source adaptation. The C17 game consumes prebuilt packs;
no C++ RNG, FastNoise, PNG encoder, source parser, or JSON parser is required at
runtime. It shares the pinned dependencies and standalone CMake build with
[`../reference_generator`](../reference_generator/README.md).

## Original source

The six files in `upstream/` are unmodified snapshots. Their Git blob SHA-1 hashes,
commit pins and direct source URLs are in [`upstream/sources.json`](upstream/sources.json).
The MethaneAsteroids source is pinned to `16a5751e835dd0776d976e51438604dc8de27d16`:

- [AsteroidsApp.cpp](https://github.com/MethanePowered/MethaneAsteroids/blob/16a5751e835dd0776d976e51438604dc8de27d16/App/AsteroidsApp.cpp):
  all ten `g_mutable_parameters` presets and constructor settings.
- [AsteroidsArray.cpp](https://github.com/MethanePowered/MethaneAsteroids/blob/16a5751e835dd0776d976e51438604dc8de27d16/Modules/Simulation/AsteroidsArray.cpp):
  `ContentState::ContentState` contains texture parameter generation, subset
  texture assignment, and instance generation; `GetRandomDirection` supplies
  spin axes. `UpdateAsteroidUniforms` defines the time and matrix conventions.
- [Asteroid.cpp](https://github.com/MethanePowered/MethaneAsteroids/blob/16a5751e835dd0776d976e51438604dc8de27d16/Modules/Simulation/Asteroid.cpp):
  `TransformSrgbToLinear`, `GetAsteroidRockColors`, `GetAsteroidIceColors`,
  `GenerateTextureArraySubResources`, and `FillPerlinNoiseToTexture`.
- [Asteroid.h](https://github.com/MethanePowered/MethaneAsteroids/blob/16a5751e835dd0776d976e51438604dc8de27d16/Modules/Simulation/Asteroid.h):
  `Parameters` and `TextureNoiseParameters` defaults.
- [AsteroidsArray.h](https://github.com/MethanePowered/MethaneAsteroids/blob/16a5751e835dd0776d976e51438604dc8de27d16/Modules/Simulation/AsteroidsArray.h):
  array settings defaults.
- [MethaneKit Color.hpp](https://github.com/MethanePowered/MethaneKit/blob/04a95fb78334252594427c69737839518e53c4a0/Modules/Graphics/Types/Include/Methane/Graphics/Color.hpp):
  byte-to-float channel conversion divides by 255 before the source exponent
  `2.233333333F` is applied.

The adapted code and source snapshots retain the Evgeny Gorodetskiy copyright and
Apache-2.0 notices. [`LICENSE-Apache-2.0.txt`](LICENSE-Apache-2.0.txt) contains the
full license. The shared generator's `licenses/` contains pinned FastNoise2,
FastSIMD and HLSL++ licenses. PNG export uses the engine's existing stb_image_write.

## Fidelity and explicit reproducibility choices

The exporter uses the original `std::mt19937`, `std::normal_distribution<float>`,
`std::uniform_real_distribution<float>`, and `std::uniform_int_distribution<uint32_t>`.
The verified toolchain is Clang 19.1.7, GNU libstdc++ 14 (`__GLIBCXX__=20250315`),
x86-64 little-endian, HLSL++ row-major/left-handed SSE2, and the official FastNoise2
SSE2 implementation with upstream relaxed floating-point math.

The original tasks share one RNG without synchronization. Their parallel
execution does not define a portable, repeatable seed-to-scene mapping. This
exporter executes source texture traversal serially, then preserves the exact
subsequent RNG consumption and operation order. It reproduces a serial execution
of the pinned algorithm, not one unspecified parallel run. C++ distribution
implementations, argument evaluation order, SIMD and floating-point behavior can
differ across compilers/platforms. The committed validation receipt applies to
the verified toolchain, not all C++ implementations.

Preserved source behavior includes:

- Ten exact presets: 1k/2k/3k/4k/5k/10k/15k/20k/35k/50k instances;
  35/50/75/100/200/300/400/500/750/1000 unique meshes;
  10/10/20/20/30/30/40/40/50/50 textures;
  maximum scale ratios .6/.5/.45/.4/.33/.3/.27/.23/.2/.17.
- Seed 1123, four mesh subdivisions, scene scale 15, orbit radius 195,
  radial standard deviation 36, and vertical standard deviation 24.
- Minimum scale ratio is the maximum divided by ten. Per-axis proportions are
  independently uniform on [.8, 1.2]. The scalar source scale is retained
  separately from the scale-translation matrix.
- Texture parameter draws occur before subset texture assignment and instance
  draws. Each level is generated independently; taking a prefix of the largest
  level would produce different instances. All texture parameter sets are
  prefixes of the maximum 50-texture sequence.
- Source ice selection is `normal_distribution(rng) <= 1.F`, approximately 84%.
  Deep and shallow palette indices are drawn separately. Both source six-color
  ice/rock palettes and their original linear conversion are retained.
- The texture function is named Perlin but constructs `FastNoise::Simplex` and
  `FastNoise::FractalFBm`. It uses four octaves, randomized gain, weighted strength,
  lacunarity and scale, and exact min/max normalization and uint8 truncation.
  The source `strength` parameter is drawn and exported but never used by noise.
- Each texture has three byte-identical source layers. The local RNG and seed
  distribution in `GenerateTextureArraySubResources` do not modify the supplied
  parameters. Exporting distinct noise into those layers would change the source.
- Texture pixels are 256×256 RGBA8Unorm, with grayscale RGB and alpha 255. Raw
  pixel rows and PNG rows preserve the original generator order without flipping.
  Mipmap creation and sampling belong to the downstream renderer/pack builder.

## Build and export

After configuring the shared standalone generator as documented in its README:

```sh
cmake --build build/reference_generator --target asteroids_reference_scene_exporter --parallel 1
build/reference_generator/asteroids_reference_scene_exporter build/reference_scene_data
python3 examples/asteroids/reference_scene_data/check_export.py build/reference_scene_data
```

Optional CLI modes `--scenes-only` and `--textures-only` limit the export. The
main-independent C++ functions in `scene_export.hpp` export an individual source
complexity or a texture sequence. Generated data should stay in untracked build
output, not in the runtime or this source directory.

## Binary and JSON contract

All binary fields are little-endian; floats are IEEE-754 binary32. The generator
requires a matching host. [`scene_format.h`](scene_format.h) is C17/C++ compatible
and compile-time checks all structure sizes. The files are offline builder inputs;
the game builder embeds selected data into its checked/CRC-protected runtime pack.

For every level N:

- `scene_level_N.instances.bin`: a 16-byte header of four uint32 values:
  magic `0x49545341` (bytes `ASTI`), version 1, level N, instance count. Each
  following record is exactly 128 bytes:
  - float scale_translate[16]
  - float spin_axis[3], scale
  - float deep[3], spin_speed
  - float shallow[3], orbit_speed
  - float spin_angle, orbit_angle; uint32 mesh_index, texture_index
- `scene_level_N.bin`: an audit export retaining every source field, including
  the explicit instance index. A 64-byte `ASTSCN01` header describes the preset,
  followed by 132-byte `ast_reference_instance` records, followed by uint32
  mesh-subset texture indices in subdivision-major order. The latter source
  table is retained even though source array-enabled rendering samples each
  instance's independently selected texture index.
- `scene_level_N.json`: all settings, texture parameters, subset texture indices,
  and complete source instance records. Nine significant digits guarantee exact
  binary32 round trips.

`textures/textures.json` describes every noise parameter and source layer.
`texture_T_layer_L.rgba` is exactly 262,144 headerless RGBA bytes; the corresponding
`.png` is a lossless representation of those bytes. All 150 source layers are
exported explicitly for audit even though each group of three is identical.

### Source transform convention

The base matrix is HLSL++ row-major, left-handed, using row vectors. Its translation
occupies float indices 12, 13, 14. Original animation uses:

```text
elapsed_radians = float(pi * elapsed_seconds)
spin_angle      = initial_spin_angle  + spin_speed  * elapsed_radians
orbit_angle     = initial_orbit_angle - orbit_speed * elapsed_radians
model           = rotation_axis(spin_axis, spin_angle)
                  * scale_translate
                  * rotation_y(orbit_angle)
```

Speeds must not be interpreted directly as radians per second. A column-vector
renderer must transpose the source transform and reverse multiplication order.
The source LOD selection uses the scalar `scale`, not any single matrix diagonal.

## Verification

`check_export.py` checks all 145,000 source records, binary32 JSON round trips,
both binary ABIs, indices, finite values, unit spin axes, original scale/speed
ranges and matrix layout. It independently decodes each PNG and compares every
RGBA byte with its raw layer. It verifies all three layers are identical while
all 50 texture arrays are distinct, and that every level uses the correct texture
parameter prefix.

`check_source_equivalence.py` first verifies the original source snapshots against
the pinned Git blobs. It then extracts and compiles the literal upstream functions
with small serial Taskflow and graphics-storage shims. It compares every exported
instance field and subset texture index, and reruns the original texture functions
for all 900 per-level face layers. No adapted generator function is used as the
oracle. Run it with the same compiler and pinned libraries:

```sh
python3 examples/asteroids/reference_scene_data/check_source_equivalence.py \
  build/reference_scene_data \
  --dependencies build/reference_dependencies \
  --generator-build build/reference_generator \
  --work build/reference_scene_oracle \
  --compiler clang++
```

A complete second export was byte-identical across all 331 generated files.
[`validation.json`](validation.json) records the checked toolchain, scope, and
repeat-export aggregate SHA-256. This is source/data validation; it does not
establish that a renderer has integrated or displayed those data correctly.

### Independent runtime transform and LOD vectors

`runtime_math_vectors.h` is a small C17 fixture; the equivalent
`runtime_math_vectors.json` includes source links, dependency pins and SHA-256
hashes of the two original binary input files. Sixteen asymmetric source
instances from levels 1 and 8 are evaluated at 0, 1 and 10 seconds, for 48 cases.
The selection includes both presets' minimum/maximum scalar scales, first/last
instances and mixed-sign spin axes. The cases cover source subdivisions 0–3.

The existing source checker generates these fixtures using the literal transform
and LOD operations extracted from `UpdateAsteroidUniforms`, with the original
HLSL++ rotation/multiply/length functions. Inputs are read from the 128-byte
`.instances.bin` records. The eye is the source initial camera (-110, 75, 210),
minimum screen size is .06, and subdivision count is four.

Each sample contains both the original model matrix and its source uniform
transpose, each explicitly serialized row-major. `model_row_major` has translation
at indices 12–14; `uploaded_transpose_row_major` has it at 3, 7, 11. A conventional
column-major, column-vector runtime's world-matrix byte layout should be compared
with `model_row_major`. The fixture also contains the exact source distance and
selected subdivision, so the C17 LOD function can be checked independently of
its world-matrix calculation. Matrix comparisons should use an explicitly chosen
numeric tolerance if runtime trigonometric operations differ from HLSL++;
subdivision results should match exactly.

Regenerate using the same command as the source-equivalence check with
`--math-output examples/asteroids/reference_scene_data` appended. This mode
compiles the same oracle but emits math vectors instead of regenerating all
scene/noise checks. Then format the generated C header:

```sh
clang-format -i examples/asteroids/reference_scene_data/runtime_math_vectors.h
```
