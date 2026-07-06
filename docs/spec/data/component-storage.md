# Component Storage Design

Canonical sparse/dense component storage: per-type dense arrays with
entity_to_index / index_to_entity mapping, preallocated capacity, uint16
component indices. Component modules expose typed APIs with per-field SoA
accessors, not one generic mega-API.

Related: [Entity System](entity.md), [Transform](transform.md), [Render Components](../render/render-components.md)

## Storage model

Each component type has: unique component per entity, sparse lookup, dense storage, preallocated capacity.

## Canonical storage layout

```c
typedef struct
{
    ComponentType data[CAPACITY];
    ComponentIndex entity_to_index[MAX_ENTITIES];
    EntityIndex index_to_entity[CAPACITY];
    uint32_t count;
} ComponentStorage;
```

Where:

- `entity_to_index` maps entity → dense index
- `index_to_entity` maps dense index → entity
- `data` stores actual dense components

Yes, sparse side is sized by max entities even if component capacity is smaller. This is correct and intentional.

## Component index type

Default component indices = `uint16_t`. Only use `uint32_t` if truly needed later. Do not use odd-width runtime types like 12-bit indices.

## Component API style

Typed APIs, not one generic mega-API. Each component module exposes its own
init descriptor, lifecycle (`init`, `shutdown`), per-entity ops (`add`, `has`,
`remove`), and per-field accessors. Components do not return a monolithic
struct — fields live in parallel SoA arrays and are read/written one at a time.

```c
/* Lifecycle */
nt_result_t nt_transform_comp_init(const nt_transform_comp_desc_t *desc);
void nt_transform_comp_shutdown(void);

/* Per-entity ops */
bool nt_transform_comp_add(nt_entity_t e);
bool nt_transform_comp_has(nt_entity_t e);
void nt_transform_comp_remove(nt_entity_t e);

/* Per-field accessors return pointers into the dense SoA — the caller
 * mutates fields directly through the returned pointer where appropriate. */
float *nt_transform_comp_position(nt_entity_t e); /* vec3 */
float *nt_transform_comp_rotation(nt_entity_t e); /* vec4 quaternion */
/* ... */
```

Modules with cross-field invariants (e.g. sprite_comp, where `atlas` and
`region_hash` drive a cached `region_index`) return `const` accessors and
provide dedicated setters instead. See per-component sections in [Transform System](transform.md) and [Render-Related Components](../render/render-components.md)
for the actual API surface.
