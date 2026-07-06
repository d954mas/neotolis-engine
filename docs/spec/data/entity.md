# Entity System

Entities are 16-bit index + generation handles; generation catches stale
references after slot reuse. The entity table owns alive/enabled flags and the
parent/child/sibling hierarchy — hierarchy lives in the entity system, not in
Transform. Covers root detection and deferred destruction.

Related: [Component Storage](component-storage.md), [Transform](transform.md), [Memory Policy](../runtime/memory.md)

## Entity identity

Entities are lightweight IDs with generation validation.

```c
typedef uint16_t EntityIndex;
typedef uint16_t EntityGeneration;

typedef struct EntityHandle {
    EntityIndex index;
    EntityGeneration generation;
} EntityHandle;
```

If larger scale is needed later, compile-time switch can allow 32-bit indices.

**Generation overflow note:** `uint16_t` generation overflows after 65535 create/destroy cycles on a single slot. For long-running web sessions with hot slots, monitor usage. If needed, upgrade to `uint32_t` generation (minimal cost increase).

## Why generation exists

Without generation, stale entity handles can silently become valid again after slot reuse. Generation solves stale references and destroyed-then-reused slot ambiguity.

## Entity table

Engine-level entity data lives in entity system, not in Transform.

Per entity slot:

```c
generation[index]
alive[index]
enabled[index]

parent[index]
first_child[index]
next_sibling[index]
prev_sibling[index]
```

This gives: valid/alive checks, hierarchy, enable/disable tree support, transform inheritance source tree, general logical tree use.

## Hierarchy policy

Hierarchy belongs to **entity system**, not Transform.

Reasons: hierarchy useful beyond transform, enable/disable subtree, ownership/tree traversal, tree-based logic, render grouping if desired later.

## Root detection

No separate root component needed. An entity is a root if:

```c
parent == INVALID_ENTITY_INDEX
```

## Entity destruction and cleanup

When an entity is destroyed, systems that hold component data for that entity must be cleaned up. Two strategies are available:

1. **Deferred destruction queue:** mark entities for destruction, process queue at a defined point in frame (e.g., after game_update, before transform_update).
2. **Immediate destruction with per-storage cleanup:** entity_destroy iterates registered storages and removes components.

For v0.1, deferred destruction is recommended to avoid mid-frame structural changes to storages.
