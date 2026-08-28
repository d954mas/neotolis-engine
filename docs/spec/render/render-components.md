# Render-Related Components

Separate components per renderable kind, not one universal component: drawable
(tag/visible/color), mesh and material handle components, the SoA sprite
component (atlas + region_hash identity, cached resolve, origin override, flip,
explicit resource sync), and the planned text and shadow components.

Related: [Items, Sorting, Batching](items-sorting-batching.md), [Material System](material.md), [Resource System](../assets/resource.md)

The architecture supports different renderable kinds via separate components, not one universal component.

## Common render state

The drawable component is SoA. Fields:

| Field   | Type          | Default      | Notes                                |
|---------|---------------|--------------|--------------------------------------|
| tag     | `nt_hash32_t` | `{0}`        | pass/group filter; set via `nt_hash32_str("world")` etc. |
| visible | `bool`        | `true`       | render visibility only               |
| color   | `vec4`        | `(1,1,1,1)`  | object tint / alpha multiplier       |

Accessors for `tag` and `visible` return mutable pointers. Color is mutated
through `nt_drawable_comp_set_color()` / `nt_drawable_comp_set_alpha()` so the
module can keep its float SoA and packed RGBA8 mirror in sync for renderers.
API lives in `engine/drawable_comp/nt_drawable_comp.h`.

Per-entity shader params (`params0`) deferred to ShaderParamsComponent — add when per-entity shader effects are needed (#98).

## Mesh component

Per-entity mesh handle. The component stores a single `nt_mesh_t` per entity
(typed handle from the gfx module); accessor returns a mutable pointer:

```c
nt_mesh_t *nt_mesh_comp_handle(nt_entity_t e);
```

API in `engine/mesh_comp/nt_mesh_comp.h`. There is no `MeshComponent` struct
or `MeshAssetRef` wrapper — just one handle per entity.

## Material component

Per-entity material handle. The component stores a single `nt_material_t`
per entity (handle from the `nt_material` module); accessor returns a
mutable pointer:

```c
nt_material_t *nt_material_comp_handle(nt_entity_t e);
```

API in `engine/material_comp/nt_material_comp.h`. There is no
`MaterialComponent` struct — just one handle per entity. The `nt_material`
module behind the handle is described in [Material System](material.md).

## Sprite component

The sprite component is a SoA module — there is no monolithic `SpriteComponent`
struct. Each sprite-bearing entity contributes one row across parallel dense
arrays (atlas handle, region hash, cached resolved region data, cached atlas
revision, effective origin, flag bits). The module owns those arrays directly and
exposes them via per-entity accessors and a bulk view (see header
`engine/sprite_comp/nt_sprite_comp.h` for the full API).

### Identity

Sprite identity is the pair `(nt_resource_t atlas, uint64_t region_hash)`.
That pair survives atlas republish, hot reload, and region renumbering.

The runtime additionally caches:

- **Resolved region index** — `uint16_t` index into the atlas region table.
- **Resolved region data** — atlas geometry pointers and the actual page
  resource used by the renderer and its batch key.
- **Atlas revision snapshot** — `uint32_t`, used to detect republish.
- **Effective origin** — `float[2]`, either authored from the region or
  overridden by the game.
- **Flag byte** — `FLIP_X`, `FLIP_Y`, `ORIGIN_OV`, `RESOLVED`.

These are implementation details. Game code reads them through the public
accessors; layout (SoA, packed flags, sentinel choices) is not part of the
contract.

### Binding modes

Two ways to bind a sprite to a region, picked by what the game knows:

- **By hash (slow path, async-friendly).** Game knows `region_hash` only.
  The atlas may not be ready yet. Sync resolves the hash to a region index
  on the next `nt_sprite_comp_sync_resources()` once the atlas is published.
  Cost: one hash-table lookup per sprite per resolve.
- **By index (fast path, requires ready atlas).** Game already knows the
  numeric region index — typically because it cycled an animation frame,
  or read it back from a previously resolved sprite. Skips the hash lookup
  on bind and on stable frames (the atlas-revision gate inside sync resolves
  to a no-op when nothing changed). After atlas republish, sync re-resolves
  via hash to pick up a removed region going dead (`vertex_count==0`). The
  atlas merge contract guarantees every region ever present keeps the same
  index for the atlas lifetime — removed regions are marked dead in place but
  keep their index and revive there if re-added — so the cached index never
  moves; a dead region simply zero-draws.

Game code is free to read back the cached region index for animation logic
(e.g. cycle to the next frame). It is stable across atlas republish. A removed
region remains `RESOLVED` at its tombstoned index so it can revive in place;
`nt_sprite_resolved_region_has_geometry()` distinguishes it from a drawable
resolved region.

### Origin override and flip

Each sprite carries an effective origin (`float[2]`). By default it tracks
the region's authored origin from the atlas. The game may override it with
`set_origin(x, y)`, which sets the `ORIGIN_OV` flag and pins the value across
subsequent atlas republishes. `reset_origin()` clears the override and
restores the authored value on the next sync.

Flip is a pair of flag bits (`FLIP_X`, `FLIP_Y`) toggled via `set_flip`.
They are pure state — no resolve, no atlas interaction.

### Lifecycle

```c
nt_sprite_comp_init(&(nt_sprite_comp_desc_t){.capacity = 4096});
/* per-frame: */
nt_resource_step();
nt_sprite_comp_sync_resources(); /* explicit, after publication */
/* on shutdown: */
nt_sprite_comp_shutdown();
```

Capacity is set once at init (see [Memory Policy — capacity policy](../runtime/memory.md)). All SoA arrays are allocated up
front and never grow.

### Resolution flow

Resolution is explicit, not renderer-driven magic:

- game code requests / mounts resources
- `resource_step()` publishes winners
- game code calls `nt_sprite_comp_sync_resources()` (or equivalent system)
- sync iterates dense sprite rows when the resource publication epoch
  advanced or any sprite was bound by hash since the last call; per-row
  early-out via cached atlas revision keeps stable frames cheap
- sprite render-item build skips unresolved sprites and resolved tombstones

### Bulk iteration

`nt_sprite_comp_view()` returns base pointers into the dense SoA arrays plus
the live count and an `entity_indices` array (dense → entity index) for
joining sprites with other components. Pointers are stable for the lifetime
of the module; values shift on add/remove (swap-and-pop) so views must not
be cached across mutations.

> **Status:** sprite_comp, atlas runtime data, explicit resource sync, and the
> dedicated SpriteRenderer are implemented. Sprite render-item construction is
> still game-side code: the engine consumes caller-provided render-item arrays
> and does not introduce a hidden sprite scheduler.

Sprite is a separate render kind, not a special mode of mesh.

## Text component

> **Status:** the text component module does not exist yet — only the
> `nt_font` module backing it is implemented (`engine/font/nt_font.h`).
> The shape below describes the planned component.

```text
text_comp fields (planned):
  font   nt_font_t   /* handle from nt_font_create / nt_font_add */
  text   StringId    /* intern-table reference, design TBD */
```

`nt_font_t` is a pool-backed handle to a font instance. A font instance owns GPU textures (curve + band) and a glyph cache. Font data comes from one or more `nt_resource_t` assets attached via `nt_font_add()`, allowing fallback chains (base font + CJK extension pack, etc.). Glyphs are decoded and uploaded to GPU on first lookup, not on asset load.

StringId references a string in a string pool/intern table (detail deferred to implementation phase).

## Shadow component

> **Status:** the shadow component module does not exist yet. The shape
> below describes the planned component once a shadow pass lands.

```text
shadow_comp fields (planned):
  enabled            bool
  mesh_override      nt_mesh_t       /* optional override; INVALID = use primary mesh */
  material_override  nt_material_t   /* optional override; INVALID = use primary material */
```

If missing: object does not participate in shadow pass. If present and enabled: use override mesh/material if valid, otherwise use primary mesh/material or default shadow path.
