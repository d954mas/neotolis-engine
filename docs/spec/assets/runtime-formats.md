# Runtime Formats

The runtime reads only prebuilt runtime formats; the builder converts source
formats (glTF, PNG, WAV/OGG) offline. Runtime validation is a safety net
(magic/version/type/sizes) — builder validation is primary. Includes the mesh
format strategy: compact, near GPU-ready attribute packing.

Related: [Pack Format](ntpack.md), [Builder Architecture](../builder/builder.md), [Shader System](../render/shader.md)

## General rule

Runtime reads only runtime formats. Builder converts from source formats to runtime formats.

Examples:

- source `.glb` → runtime mesh binary
- source `.png` → runtime texture binary
- source material description → runtime material binary
- source `.wav`/`.ogg` → runtime audio binary (OGG Vorbis)

## Runtime format validation

Runtime must validate: magic, version, type, sizes/offsets. Required attributes (e.g. POSITION) are a builder guarantee — the runtime sees only name hashes and cannot identify them. Builder validation is primary. Runtime validation is safety net.

## Mesh format strategy

Runtime mesh format should be: compact, near GPU-ready, not authoring-friendly.

Attributes: POSITION (required), NORMAL (optional), UV0 (optional), COLOR0 (optional), TANGENT (optional, from glTF or MikkTSpace in the builder).

Preferred data types: position float16 or float32, normals snorm8 packed, uv unorm16, colors uint8 normalized.

Avoid runtime parsing/conversion of asset content — with one deliberate
exception: the **wire → GPU transform** at mesh activation. The pack stores a
wire form chosen for HTTP gzip/brotli compressibility; `nt_gfx_activate_mesh`
decodes it into the byte-exact GPU form before upload (precedent: basis
texture transcoding). The transform is a fixed byte permutation plus an index
codec — no parsers, no format negotiation, no heap in the hot path (decode
runs at load into transient memory).

### Mesh wire layout (format v3)

`NtMeshAssetHeader` v3 (52 bytes) carries two wire tags next to the GPU-form
fields (`index_type` keeps its width-only meaning; `index_data_size` is the
WIRE size of the index block):

- `vertex_wire`: `RAW` (interleaved, upload as-is) or `SOA` — one plane per
  stream in `NtStreamDesc` order. Plane element size =
  `nt_stream_type_size(type) * count` (the whole attribute, never split per
  component); plane *i* starts at `vertex_count * sum(previous element sizes)`.
  A pure permutation: `vertex_data_size` equals the interleaved size. Decode =
  re-interleave (`nt_meshwire_reinterleave`).
- `index_wire`: `RAW` (plain u16/u32 array) or `MESHOPT` — a meshopt index
  codec stream (version 1, vendored in `deps/meshoptimizer`). Decoded size is
  `index_count * element size`; validation requires triangles
  (`index_count % 3 == 0`), a non-zero wire no larger than the decoded size,
  and a decoded size that fits `uint32_t`.

**Canonicalization.** The meshopt codec preserves triangles up to rotation of
each triple (same winding, same rasterization). The builder therefore encodes,
decodes back, and stores the DECODED order as the pack's ground truth — the
runtime decode is byte-exact against the pack. The only observable of a
rotation is the `flat`-qualified provoking vertex; content relying on
provoking-vertex identity should use runtime-generated (RAW) meshes.

Decoding lives in the swappable `engine/meshwire` module
([Module Layout](../core/module-layout.md)); executables without
builder-packed meshes link its loud-fail stub. RAW/RAW stays first-class for
runtime-generated meshes.

A stream is `(type, count 1-4, normalized)` — the `vertexAttribPointer` space
over float32/float16/byte/short types (no int32 or 2_10_10_2 packed types),
including 3-component byte/short streams (snorm8×3 normals, unorm8×3 colors). The builder can narrow a wider source
accessor to the leading components when the layout declares it
(`source_components`, see [Builder](../builder/builder.md)). Constraint carried
by the format: WebGL2 requires every attribute's offset and the vertex stride
to be multiples of that attribute's type size — the builder validates this at
pack build. Note for a future WebGPU backend: WebGPU has no 3-component 8/16-bit
vertex formats, so a game targeting it chooses 2- or 4-component packings;
layouts are a per-game choice, not an engine constraint.
