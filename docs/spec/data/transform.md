# Transform System

SoA transform component (local position/rotation/scale, world matrix, dirty
flag) with its API in `engine/transform_comp`. Inheritance reads the entity
hierarchy top-down; dirty propagation marks subtrees; nodes without a transform
pass the inherited basis through.

Related: [Entity System](entity.md), [Component Storage](component-storage.md), [Render Components](../render/render-components.md)

## Transform component data

The transform component is SoA — there is no monolithic `TransformComponent`
struct in the code. Each entity contributes one row across these dense fields:

| Field          | Type     | Notes                                |
|----------------|----------|--------------------------------------|
| local position | `vec3`   | mutable via `nt_transform_comp_position(e)`  |
| local rotation | `vec4`   | quaternion, via `nt_transform_comp_rotation(e)`  |
| local scale    | `vec3`   | via `nt_transform_comp_scale(e)`     |
| world matrix   | `mat4`   | read-only via `nt_transform_comp_world_matrix(e)`; recomputed by `nt_transform_comp_update()` |
| dirty          | `bool`   | mutable; set when locals change      |

API surface lives in `engine/transform_comp/nt_transform_comp.h`.

Optional future additions: previous_world_matrix, decomposed world data, bounds dirty flag.

## Hierarchy source

Transform inheritance reads from entity hierarchy. Transform does not own parent/child links.

## Update model

Update top-down. Roots are entities with no parent. Traversal uses entity hierarchy.

## Dirty propagation

When local transform changes: mark this transform dirty, mark descendant transforms dirty. When parent changes: mark subtree dirty. Dirty only means world transform must be recomputed.

## Non-transform nodes in hierarchy

Entities without Transform may still exist in hierarchy.

Traversal rule: walk entity tree; if node has transform, update world basis; if node has no transform, continue traversal with last valid inherited transform basis.
