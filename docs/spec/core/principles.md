# Core Principles

Design philosophy of the engine: code-first, explicit over implicit, runtime
simplicity, data-oriented where useful, minimal abstraction. Also holds the strict
engine/game ownership boundary — the engine owns platform, memory, entities,
resources, the GPU backend and render primitives; the game owns gameplay, system
order, render passes, sort/batch policy, and content organization.

Related: [Scope](scope.md), [Module Layout](module-layout.md), [Architecture Snapshot](../meta/architecture-snapshot.md), [Locked Decisions](../decisions/index.md)

## Design Philosophy

The engine follows these principles:

1. **Code-first architecture**
   - render loop is game code
   - gameplay system order is game code
   - builder rules are code
   - no heavy declarative system

2. **Explicit over implicit**
   - no hidden system scheduler
   - no hidden render graph
   - no automatic ECS magic
   - no hidden asset conversion at runtime

3. **Runtime simplicity**
   - builder does heavy work
   - runtime only loads, validates, resolves, and renders
   - no source-format import in runtime

4. **Data-oriented where useful**
   - sparse component storages
   - dense iteration
   - typed handles/refs
   - sorted draw items

5. **Minimal abstraction**
   - enough abstraction to survive WebGL 2 → WebGPU later
   - not so much abstraction that the engine becomes hard to understand

## Code-First Approach

Throughout the entire architecture:

- render pipeline is defined by **game code**
- system order is defined by **game code**
- builder rules are defined by **code**
- engine provides primitives and infrastructure, does not impose a ready-made pipeline

Engine gives low-level and mid-level infrastructure. Game defines concrete logic, passes, sorting, batching policy, and content build pipeline.

## Simplicity Over Universality

Key constraints:

- no premature universal abstractions
- no complex runtime reflection system when layouts and indices suffice
- no complex plugin system
- no material graph, editor framework, or scripting at start
- no multi-platform/multi-backend core design upfront

If a decision can be deferred without loss of base architecture — it is deferred.

## Engine/Game Boundary

This is one of the most important decisions.

### Engine owns

- platform
- memory
- entities
- component storage pattern
- hierarchy storage
- resources
- packs
- runtime format loading
- GPU backend
- render primitives
- input system
- audio system

### Game owns

- gameplay systems
- system order
- render passes
- render tags
- sort policy
- batching choice per pass
- level/scene logic
- capture ownership semantics
- high-level content organization

This boundary is strict.
