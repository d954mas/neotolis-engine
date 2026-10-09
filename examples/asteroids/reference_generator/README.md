# Offline Methane reference geometry

This standalone C++20 tool exports the pinned Methane Asteroids mesh-generation
algorithm without linking a renderer, graphics backend, Taskflow, or Neotolis's
runtime. The example build invokes it before packing assets; it can also run standalone.

## Source and fidelity

The reference is Methane Asteroids commit
`16a5751e835dd0776d976e51438604dc8de27d16`, specifically
`Asteroid::Mesh::Randomize` and `AsteroidsArray::UberMesh`. Icosahedron topology,
linear edge subdivision, spherical normalization and area-weighted normals come
from MethaneKit `04a95fb78334252594427c69737839518e53c4a0` (v0.8.2).
Unmodified geometry headers are included under `upstream/` for review.

`main.cpp` is an Apache-2.0 source adaptation. Its changes remove storage-wrapper,
renderer and instrumentation coupling, execute task traversal serially, and add
GLB packing, export metadata and validation. It preserves the original initial
icosahedron formula, face ordering, midpoint insertion ordering, both spherical
normalizations, floating-point operations, noise parameters and normal sums.
The original radius is 0.5. Four-dimensional Simplex FBM uses four octaves,
lacunarity 2, a normally distributed gain (0.95, 0.04), and a uniform fourth-axis
offset (0, 10000). Per-mesh extrema normalize noise to [0.5, 1.0], followed by the
original radial multiplier `noise * 1.5 + 0.3`. Actual radii span approximately
0.525 to 0.9. There are no invented crater, axis-deformation or silhouette fields.

Dependencies are source-only, pinned by commit and each file's Git blob SHA-1 in
`dependencies.json`: MethanePowered/FastNoise2 v1.1.1, its original FastSIMD pin,
and MethanePowered/HLSLpp 3.9. Full Apache-2.0 and dependency MIT licenses are in
`licenses/`. Pinned dependencies are vendored in `deps`; generated assets stay in build output.

Two explicit reproducibility choices are necessary:

- Upstream tasks access one shared `std::mt19937` without synchronization, and
  append meshes in task-completion order. The exporter executes the original
  subdivision-then-instance traversal serially. It preserves the algorithm but
  does not claim to reproduce one unspecified parallel run's random ordering.
- The original noise library automatically chooses SIMD and defaults to relaxed
  floating-point math. This exporter fixes the official SSE2 target and retains
  relaxed math. The validated toolchain is Clang 19.1.7 with GNU libstdc++.
  `std::normal_distribution<float>` is the original implementation; distributions
  and floating-point details can differ between C++ libraries/platforms. Repeat
  output is verified on this toolchain, not promised byte-identical everywhere.

The four source subdivision levels are 0, 1, 2, 3 (coarse to fine), with
12/42/162/642 shared vertices and 20/80/320/1280 triangles. **LODs are independently
randomized**, as in the source. They are not nested versions of one shape.
A fresh `mt19937(seed)` supplies seed number `subdivision * unique_count + variant`.
Therefore each source benchmark preset must be exported with its own unique
count; taking prefixes of a 1000-variant export changes lower-complexity scenes.
Source benchmark counts are 35, 50, 75, 100, 200, 300, 400, 500, 750, 1000; its seed
is 1123. The runtime preserves source subdivision ordering 0 (coarse) through 3 (fine).
Legacy export metadata also records the reversed quality ordering.

## Build and export

From the repository root:

```sh
python3 examples/asteroids/reference_generator/fetch_dependencies.py build/reference_dependencies
cmake -S examples/asteroids/reference_generator -B build/reference_generator \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=clang++ \
  -DAST_REFERENCE_DEPENDENCIES="$PWD/build/reference_dependencies"
cmake --build build/reference_generator --parallel 1
build/reference_generator/asteroids_reference_generator \
  --unique 750 --seed 1123 --output build/reference_geometry/750
python3 examples/asteroids/reference_generator/check_export.py build/reference_geometry/750
```

Use the fetch helper's `--check` mode to verify already-materialized dependencies
without network access. No download, generator or source parser runs in-game.
The example CMake target also integrates the exporter and its parity test.
The pinned SSE2 exporter requires an x86 host. ARM hosts can use packs generated
on the validated x86 toolchain; they do not silently select a different SIMD path.

## Storage contract

Each generated chunk is one single-mesh GLB. Vertices are POSITION/NORMAL plus
zero-filled TEXCOORD_0 for the example's common layout; rock shading is triplanar.
Vertices remain shared; no flat-normal or UV-seam duplication is introduced.
Indices are chunk-global unsigned 16-bit values, with each variant's vertices
and indices contiguous. The original renderer's per-instance draws are not
combined by this storage choice.

The existing builder limits are 65,536 vertices and 196,608 indices per mesh.
Chunks hold at most 1000 / 819 / 204 / 51 variants for subdivisions 0 / 1 / 2 / 3.
The exporter also stays under the 65,535-vertex unsigned-16-bit threshold.
A 1000-variant export has 28 chunks; 750 variants have 21; 50 variants have four.
No engine limit, asset API or runtime format changes are required.

`geometry.json` records source pins, compiler, SIMD, seed and unique count, then:

- `chunks`: filename/name, source subdivision, reversed runtime LOD, first variant,
  variant count, vertex count and index count.
- `subsets`: source subdivision, runtime LOD, variant, chunk index, first index,
  index count, first vertex, vertex count, actual depth minimum/maximum and seed.

`first_index` is in index elements, not bytes. Global chunk indices already
include `first_vertex`; do not add that offset again when drawing. Meshopt may
rotate the three vertices of a triangle but preserves triangle and subset order.

`geometry_meta.h` contains the same C-compatible metadata in source order:
`subdivision * AST_REFERENCE_UNIQUE_COUNT + variant`. Its structs are source
metadata, **not a serialized ABI**: the pack builder must serialize fields
explicitly and validate its own wire layout. Each output directory is independent;
identifiers in these generated headers are intended for one preset per compilation.

`check_export.py` rereads the stored GLB bytes, checks all accessors, counts and
per-variant index ranges, independently verifies MT19937 seed windows, uniqueness,
radii, outward winding, and recomputes area-weighted normals from stored positions.
To establish byte determinism, export the same arguments into a second directory
and compare every generated file.

## Binary metadata and environment meshes

`geometry.bin` is explicitly written little-endian, never by dumping C++ structs:

- Header, 20 bytes: uint32 magic `0x4F454741` (ASCII `AGEO`), version 1,
  unique count, chunk count, subset count.
- Each chunk, 20 bytes: uint32 subdivision, first variant, variant count,
  vertex count, index count.
- Each subset, 32 bytes: uint32 chunk index, first index, index count, first vertex,
  vertex count, random seed; float32 depth minimum, depth maximum.

The checker compares every wire byte against the independently parsed JSON
representation. Source order remains coarse to fine, then increasing variant.

Each export also writes identical `environment.glb`, 48,188 bytes, with two meshes:

- `planet`: original `SphereMesh<Vertex>(layout, 1.F, 32, 32)` from Planet.cpp,
  1,056 vertices, 5,952 indices (1,984 triangles), original normals and UVs.
  The source includes degenerate pole triangles and V coordinates ending at
  31/33. These are deliberately preserved.
- `skybox`: original unit `CubeMesh` used by SkyBox.cpp, 24 vertices and 36 indices.
  Positions, face order and winding are preserved; unused normal/UV lanes are
  zero-filled solely for the example's common input layout.

Environment topology comes from the unmodified SphereMesh/CubeMesh/QuadMesh
headers in `upstream/` and Mesh.cpp's original face constants. The GLB writer
only serializes the resulting data; it applies no coordinate or winding change.

## Original-source oracle

```sh
cmake --build build/reference_generator --target asteroids_reference_parity --parallel 1
build/reference_generator/asteroids_reference_parity
```

The oracle compiles unmodified upstream mesh method bodies and the original
Asteroid::Mesh::Randomize body (only its enclosing class is renamed). Test-only
storage/instrumentation adapters avoid a renderer dependency. It compares every
position, normal and depth float bit pattern and every index for all ten source presets, plus
original planet normals/UVs and cube geometry. The adapters do not substitute a
noise or geometry algorithm. All comparisons pass on Clang 19.1.7 with GNU
libstdc++ 14 (`__GLIBCXX__=20250315`) and the pinned SSE2 noise library.

The companion `asteroids_reference_scene_exporter` target
uses these same dependency targets. Its output contract and source-parity checks
are documented in `../reference_scene_data/`.

## Vendored source-only dependencies

This engine example includes the 149 verified dependency files in `deps/`
(about 2.3 MiB of source) so a normal native pack build does not access the
network. `dependencies.json` and `fetch_dependencies.py --check` verify those
unchanged upstream blobs. The explicit `AST_REFERENCE_DEPENDENCIES` override
remains available for a separately materialized copy. These libraries are
linked only to offline exporter executables, never the browser runtime.
