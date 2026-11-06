# Resource, Runtime Object and Pack Refactor

**Status:** Proposed target contract; not implemented. Revised 2026-11-06.

This chapter states the target of epic #501. Until a task lands, [Resource System](resource.md),
[Async Loading](async-loading.md) and [Pack Format](ntpack.md) describe shipped behavior.
Names below are provisional. The task sequence and acceptance live in the epic and its issues;
each task moves its implemented contract into the current chapters. Breaking APIs is allowed.

Related: [Principles](../core/principles.md), [API contracts](../core/api-contracts.md),
[Runtime formats](runtime-formats.md), [Frame lifecycle](../runtime/frame-lifecycle.md).

## 1. Decisions

1. Loaded objects keep no pointers into pack bytes.
2. There are two registries. A **pack registry** holds immutable sources and the objects built
   from their entries. A **resource registry** maps stable names to what is currently under them.
3. Priorities, winners, automatic fallback and virtual packs are removed completely. Opening a
   pack never changes a name; only an explicit call does.
4. A consumer holds a resource and never learns that its content changed. Creating a sprite and
   giving it a resource is all a game does; one call site elsewhere chooses the source.
5. Replacement keeps the current object usable until the new one is ready, under the same handle.
6. Creating a resource handle loads nothing. Bulk helpers, consumer counting, automatic dependency
   loading and creation recipes are not provided.
7. A slot may borrow an object the game created. The game keeps ownership and recreates it.
8. An atlas is one immutable object that owns its regions, geometry and page textures.
9. A font is a direct object filled from bytes. It is not installed from a pack.
10. The engine restores pack-backed GPU objects after context loss when the host says so; the game
    recreates what it created.

Out of scope: a new pack container, HTTP range streaming, worker decoding, scene lifetimes, a mod
stack, a dependency graph and new animation or audio features.

## 2. Data model

```c
/* Sketch of the contract, not a required layout. */
typedef struct {            /* one per canonical pack entry */
    uint32_t handle;        /* the runtime object, 0 = none */
    uint8_t state;          /* NONE, ACTIVATING, READY, FAILED */
} pack_entry_object_t;

typedef struct {            /* tagged source */
    uint8_t kind;           /* NONE, PACK, RUNTIME */
    /* PACK: pack instance handle + entry name. RUNTIME: borrowed object handle. */
} source_t;

typedef struct {            /* one per resource name */
    uint64_t name;
    uint8_t type;
    source_t current;
    source_t pending;       /* NONE or PACK */
} resource_slot_t;
```

### 2.1 Pack registry

A pack instance holds an immutable location, a validated catalog, canonical links, byte residency,
I/O state and the objects of its canonical entries. Its handle carries a generation; passing a stale
handle asserts. The catalog stays resident until the pack closes, so queries need no I/O.

Parsing a catalog creates no objects and requires no activator for unused types; a type must be
registered only when it is actually loaded. Some entry types are input bytes and never activate:
FONT and ATLAS_PAGE.

Canonical `owner_entry` sharing stays within one pack. Aliases share one object and keep their own
names. Different packs do not share objects even when their bytes match.

Per-entry metadata is removed: it has no caller outside tests. This covers `nt_resource_get_meta`,
the builder's `nt_builder_add_meta` and the pack's metadata section.

### 2.2 Resource registry

`request(name, type)` returns the slot, creating an empty one; it sets no source and loads nothing.
`find(name)` is a pure lookup. A different type under an existing name asserts. Slots do not move
or recycle before shutdown. The handle is the existing 32-bit index; the name is the existing
64-bit hash.

A slot only refers to things: it never owns an object. Runtime handles are typed pool values, never
pointers truncated to 32 bits; zero means no object.

## 3. Lifecycle

### 3.1 Operations

| Operation | Effect |
|---|---|
| `install(res, pack, entry)` | If `current` is READY, write `pending`, overwriting any previous one. Otherwise write `current` directly and clear `pending`. May precede catalog readiness. |
| `bind(res, handle)` | `current` becomes `RUNTIME(handle)` and `pending` clears. The game keeps ownership, destroys the object itself, and calls `bind` again after recreating it. |
| `unload(res)` | `current` and `pending` become `NONE`. The slot stops exposing an object before the call returns. Reloading means installing again. |
| `evict(pack)` | Frees source bytes. A later read frees them again once needed entries have activated. Memory-backed bytes stay with the caller. |
| `close(pack)` | Unloads every slot naming the pack, destroys its objects and invalidates the handle. Like any GPU destruction, it is illegal from the first pass until the frame ends. |

Installing the source that `current` already names clears `pending` and changes nothing else.
Installing a source whose entry or pack failed retries it.

### 3.2 Everything else is derived

No slot stores progress, an outcome or a failure reason, and no operation record exists.

| Fact | Derivation |
|---|---|
| Availability of `current` | `NONE`: UNLOADED. `RUNTIME`: READY. `PACK`: READY if its entry is READY, which outranks any pack state; FAILED if the entry or pack failed or the ready catalog lacks the name; otherwise LOADING |
| Pending outcome | The same derivation applied to `pending`, so READY plus a failed HD differs from a failed first load |
| Failure reason | I/O and catalog failures live in the pack, activation failures in the entry |
| Join and alias sharing | Two slots name one canonical entry |
| Supersession and cancellation | Overwriting or clearing `pending` |
| Demand | An entry is needed when any slot's `current` or `pending` names it |
| Object release | An entry no slot needs loses its object |
| Retry | Resets that entry; a failed pack restarts its read, and every slot naming it follows. Unrelated failures stay put |
| Late results | Activation is synchronous; pack generations reject results for closed records |

### 3.3 Step order

`nt_resource_step` runs at the game's lifecycle boundary, before consumer synchronization, layout
and draw preparation:

1. Poll pack I/O and parse catalogs.
2. Reconcile: release objects no slot needs, then schedule reads for needed entries.
3. Activate needed entries within the time budget. One activation is not preempted.
4. Publish: every `pending` whose entry is READY becomes `current`. A failed `pending` stays visible
   until the game replaces or clears it.
5. Reconcile again if publication ended a claim, releasing what no slot still needs.

Reconciling runs only when dirty. API mutations, I/O completion and publication set dirty, and it
persists while a needed entry is unfinished, so budget carryover continues. Idle steps do no scan and
no heap work.

Network callbacks only deliver results. A seamless replacement needs storage for both objects at
once; configure pool capacity for it and measure the overlap.

### 3.4 Publication epoch

`publication_epoch` stays a coarse gate: equality lets a module skip reconciliation. It advances
whenever an observable fact may have changed: any slot mutation, any entry's object or state change,
a pack read failure and loss recovery. Slots sharing an entry need no separate signal. It is not a
lifetime barrier.

### 3.5 Shutdown

Destroy consumers first, then the registries while type modules and gfx are alive. Each owned object
is destroyed once. A borrowed object stays with the game.

## 4. Runtime objects

Type modules own construction and destruction. An activator reads an entry's bytes only during
construction, then copies, decodes or uploads what it keeps. No permanent CPU copy is added for GPU
uploads. A module with no GPU data works headless with no registry.

### 4.1 Textures, shaders, meshes

Texture and shader stages keep their direct constructors. A direct mesh constructor takes runtime
vertex and index data with stream descriptors, so procedural meshes need no wire header. Mesh
readiness includes its owned buffers.

The optional texture placeholder stays an explicit render fallback: a getter may return it for an
unready texture slot, so `get != 0` never substitutes for the readiness check.

### 4.2 BLOB

An ordinary owned byte object with create, read and destroy. Its activator uses the same code; the
resource core has no BLOB case and BLOB entries are not ready at parse. A read returns a view valid
until unload, replacement or shutdown, independent of pack eviction.

### 4.3 Font

`nt_font_t` owns retained compact payloads, glyph indexing and its caches. Glyph decode stays lazy.

```c
if (!s_cjk_filled) {
    const uint8_t *bytes = nt_pack_entry_bytes(pack, entry, &size); /* NULL until resident */
    if (bytes != NULL) {
        nt_font_fill(font, bytes, size);
        nt_pack_evict(pack);
        s_cjk_filled = true;
    }
}
```

- `fill` copies the payload and publishes before returning, so old glyphs stay valid and no partial
  addition is visible. It clears the font's measure cache and advances the content revision. It is
  legal outside a pass, before layout.
- An incoming codepoint that already exists asserts. The builder selects ranges and emits disjoint
  subsets; there is no runtime range selector, source order or fallback stack.
- Overlapping replacement is deferred until a caller needs it.
- Font-wide metrics compatibility keeps its existing assert. The first fill owns decoration metrics.
- A font keeps a list of payloads bounded by an explicit per-font capacity, independent of the
  registry. Glyph access allocates nothing. Payloads are not merged: kerning indices and glyph
  offsets are per payload, and merging would hold both copies at once.
- A font works with no registry. CPU data survives context loss; the font rebuilds its glyph
  textures locally without reading a pack.

FONT entries are input bytes. Registering a font by name is deferred until a caller needs it.

### 4.4 Atlas

`nt_atlas_t` is immutable. Regions, the hash table, geometry and scale share one allocation; it owns
up to `NT_ATLAS_MAX_PAGES` page textures. The pool capacity comes from the game, and the object's
pool handle is a real runtime handle.

- **Format:** page payloads stay separate pack entries, in the same pack, with the new type
  ATLAS_PAGE. It uses the texture payload format and the common header check. It has no activator and
  no resource slot, and cannot be installed as a texture. Pages stay visible in the catalog. The
  builder's texture encode, cache and dedupe stay, and it generates no per-page asset IDs.
- **Activation:** the activator reads its page entries from its own pack and creates everything in
  one call, destroying what it made if a later page fails. The cost is measured on the HD `bunnymark`
  atlas on a phone; resumable activation is added only if the hitch is visible.
- **Replacement:** a new object through the ordinary install. Geometry and pages switch together,
  so new UVs can never meet old pages. There is no merge, no tombstone and no name directory.
- **Readiness:** READY means geometry and pages are usable. An atlas without its pages does not exist.
- **Sampling a page:** a game passes `nt_atlas_page_texture(atlas, page)` as a draw texture at draw
  time. Material bindings store only resources, and a page has none.
- **Context loss:** the whole object is retired and rebuilt from the pack, so no partial state exists.
- **Direct constructor:** deferred until a caller needs one.

`nt_atlas` links gfx; a headless composition uses its stub. The atlas uses no `on_resolve`,
`on_cleanup` or `on_post_resolve`. It is the only engine user of the first two and of `AUX_BACKED`, so
that path leaves the resource core with it. `on_post_resolve` leaves too; `examples/skeletal_showcase`
compares handles instead.

## 5. Consumers

### 5.1 References

Consumers retain `nt_resource_t`; a runtime handle or pointer read from it is a cached observation.
Assigning a different resource uses the consumer's setter. Direct typed borrowers keep their type's
explicit lifetime contract and reacquire after replacement or loss.

### 5.2 Region references

A persistent region reference is `(atlas resource, region name)` plus a cached index and the atlas
handle it was resolved against. The pool handle changes with every new object, so it serves as the
revision and no separate counter exists.

`nt_resource_get(atlas) == 0` means unresolved: read nothing and draw nothing. When the handle differs
from the cached one, resolve the name again before reading any old pointer, then refresh geometry,
UVs, sizes, scale, authored origin and page texture. A missing name becomes unresolved and may recover
later. Explicit game overrides keep their setter contracts.

An index passed to a one-call API belongs to the current object only, and such an API requires a READY
atlas. Persistent engine state therefore holds references, not indices, including the UI white region
and rich text. While an atlas is rebuilt after loss, its images draw nothing and layout that measures
them collapses until it returns; this is accepted.

This costs one call and one comparison per widget lookup and grows `nt_atlas_region_ref_t` from 16 to
24 bytes; report both measured numbers per the hot-path rule.

### 5.3 Frame order

The frame order is: resource changes, consumer synchronization, UI layout and render items, drawing.
A later mutation requires synchronization again. Pointer-bearing views stay valid until the next
invalidating mutation, and their access paths enforce the sync precondition. An object released in a
step is gone before consumers run, which is legal because the step precedes the first pass.

### 5.4 Font and UI caches

A font carries a content revision that advances on a successful fill, including a font used without
a registry. GPU-only glyph cache rebuild or eviction does not change it, and keeps the existing
glyph generation and preflush contract.

Before Clay layout, each UI context compares its bound `(font, revision)` pairs and clears Clay's
measurement cache on a change, including an inactive context on next use and a different font under
the same UI font ID. If metrics change after a layout was prepared, rebuild it before drawing.

## 6. Pack bytes and failures

- **Open:** the game gives a location; the engine reads and parses through `nt_resource_step`. Bytes
  stay resident until the game evicts them.
- **Retention:** one explicit `evict`. There are no KEEP or AUTO policies and no TTL. Nothing ready
  pins a pack.
- **Reread:** an activation that needs evicted bytes reads the same location, sharing one read per
  pack, and the bytes are freed again afterward. A missing payload loads the whole pack.
- **Identity:** a pack instance is immutable. A re-fetch is compared against the retained size,
  catalog and checksum before retained offsets are used. Replacing content means a new pack instance
  and an explicit install. CRC detects corruption; it is not authentication.
- **Memory-backed packs:** caller bytes stay valid until close; eviction cannot free them or invent a
  reread location.
- **Failures:** source, I/O and outer-container failures are recoverable and stay with the pack. The
  existing retry configuration remains, including zero meaning unlimited. A terminal activation
  failure does not retry every frame.
- **Trusted content:** asset header and structural violations assert, per the locked pack rule. Value
  validation stays in the builder.

## 7. Context loss

The stable identity is `nt_resource_t`, not a GPU pool handle. Loss cannot be derived from frame state:
a loss and restore between frames leaves `context_lost` false, and an activator's zero means a device
limit as well as a loss. Recovery therefore has one explicit trigger.

**Engine:** when `g_nt_gfx.context_restored` is set after `nt_gfx_begin_frame`, the host calls
`nt_resource_restore_gpu()`, in the default app runner or in its own loop. Every entry of a type
registered as GPU-backed then retires its object and returns to `NONE`, including FAILED entries and
those a `pending` names. Demand rebuilds them through ordinary activation, from resident or reread
bytes. Nothing unneeded is rebuilt and no pack is read for it. Recovery commits nothing by itself: a
`pending` publishes only through its normal rule. This replaces `nt_resource_invalidate`. A headless
registry has no GPU-backed types and no gfx dependency. CPU-only objects survive.

**Game:** in the same `context_restored` branch the game recreates what it created and calls `bind`
again. The registry retains no generator data. Between loss and that call a borrowed handle stays
bound, and its generation and readiness guard its use, as today.

Discard draw state prepared against the lost context, then step and render normally behind readiness
gates. Recovery of programs and custom owners is specified in #509.

## 8. Enumeration and diagnostics

Production iteration is a count plus an index-based info accessor, with no allocation. Slots do not
move, so mutating between calls is legal, and new slots appear on the next pass. A record gives the
handle, name, type, `current`, `pending` and derived availability and pending outcome. Pack catalogs
enumerate separately; they answer a different question.

Keep source bytes, catalog, runtime CPU and GPU memory distinct, using type-owned counters or on-demand
aggregation rather than a new ledger.

## 9. Boundaries

Type modules own construction and destruction. The registries own demand, source lookup, pack object
ownership and publication. I/O stays behind `nt_fs` and `nt_http`. No generic loader, factory, observer,
dependency graph, transaction framework or operation pool is added, and no CMake target per concept.
Allocation belongs at explicit construction boundaries; idle steps and hot getters do no heap work.
