# Final Architecture Snapshot

The one-screen architecture snapshot: what game code, the engine, and the
builder each do, and the properties this split buys — explicit control, minimal
runtime complexity, a strong builder/runtime split, web-first practicality, and
room for WebGPU and desktop platforms without redesigning the core.

Related: [Core Principles](../core/principles.md), [Implementation Order](implementation-order.md)

```text
Game code
    ├─ defines gameplay system order
    ├─ defines render passes
    ├─ defines tags
    ├─ calls engine subsystems
    └─ uses engine primitives

Engine
    ├─ runs frame lifecycle
    ├─ stores entities/components/resources
    ├─ updates transforms
    ├─ loads runtime assets from NTPACK packs (async)
    ├─ provides render backend (WebGL 2)
    ├─ provides input + platform services
    └─ provides audio playback (planned)

Builder
    ├─ imports source assets
    ├─ validates compatibility
    ├─ converts to runtime formats
    ├─ builds NTPACK packs
    └─ embeds manifest in pack header
```

This gives: explicit control, minimal runtime complexity, strong builder/runtime split, web-first practicality, platform-agnostic game code, future room for WebGPU and desktop platforms without redesigning the core.
