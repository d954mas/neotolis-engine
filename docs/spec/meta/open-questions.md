# Open but Non-Critical Future Questions

Open questions that do not block implementation — format details, sort-key bit
packing, WebGPU backend, sprite animation, camera structure, audio extensions.
Resolved items are struck through and point at their answers. All can be solved
incrementally without breaking the core architecture.

Related: [Locked Decisions](../decisions/index.md), [Architecture Snapshot](architecture-snapshot.md)

These do not block implementation:

- exact binary layout of each runtime format header
- precise bit packing of sort keys
- future WebGPU backend details
- ~~exact sprite asset format~~ → resolved: `NT_ASSET_ATLAS` ([Resource System — asset types](../assets/resource.md)) builder-side, runtime atlas consumer, `SpriteComponent`, and SpriteRenderer.
- sprite animation system
- ~~exact text rendering strategy and string pool design~~ → resolved: Slug-based GPU vector rendering ([Resource System — NT_ASSET_FONT](../assets/resource.md)), font module API ([Render-Related Components — text component](../render/render-components.md))
- camera component/structure definition
- whether some renderer-specific caches are worth adding later
- whether pack hot-reload becomes needed
- ~~whether asset build database is needed immediately or later~~ → resolved: content-addressed builder cache ([Builder Architecture — builder cache](../builder/builder.md))
- 3D audio / positional sound
- sound groups / mix buses
- desktop audio backend library choice (miniaudio recommended)
- entity destruction notification mechanism details
- generation overflow monitoring strategy

These can be solved incrementally without breaking the core architecture.
