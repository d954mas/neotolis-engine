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

Attributes: POSITION (required), NORMAL (optional), UV0 (optional), COLOR0 (optional).

Preferred data types: position float16 or float32, normals snorm8 packed, uv unorm16, colors uint8 normalized. Avoid runtime unpacking.
