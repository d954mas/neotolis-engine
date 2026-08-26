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

Runtime must validate: magic, version, type, sizes/offsets, required vertex/material compatibility. Builder validation is primary. Runtime validation is safety net.

## Mesh format strategy

Runtime mesh format should be: compact, near GPU-ready, not authoring-friendly.

Attributes: POSITION (required), NORMAL (optional), UV0 (optional), COLOR0 (optional), TANGENT (optional, from glTF or MikkTSpace in the builder).

Preferred data types: position float16 or float32, normals snorm8 packed, uv unorm16, colors uint8 normalized. Avoid runtime unpacking.

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
