# Radial widgets & the custom-attr image path

Design rationale for `nt_ui_radial` / `nt_ui_radial_image` and the generic
custom-attr atlas-region emit they ride (`nt_ui_image_custom`): Route B (Clay
IMAGE + per-element material) chosen for batching, name-bound attr injection,
REGION vs GEOMETRY modes, the four hard boundaries of the path, and reveal
modes with their v1 limits.

Related: [Scope](../core/scope.md), [Rich Text](rich-text.md), [Material System](../render/material.md)

This section holds the design rationale behind the radial widgets
(`nt_ui_radial`, `nt_ui_radial_image`) and the generic custom-attr atlas-region
emit they ride (`nt_ui_image_custom`). The headers carry only the short
caller-facing contract; the reasoning lives here.

## Route A (Clay CUSTOM) vs Route B (IMAGE + material)

A radial could be drawn two ways:

- **Route A — Clay CUSTOM element.** The game gets a bbox and a raw draw
  callback and emits geometry itself.
- **Route B — Clay IMAGE element + a per-element material.** The widget rides
  the existing UI walker image path; the walker emits a textured/white-region
  quad through the sprite renderer and binds the widget's material.

Neotolis uses **Route B**. The reason is batching: Route A drops out of the
walker's image emit and cannot share draw state, so every CUSTOM widget is its
own draw. Route B keeps every radial on the sprite renderer's emit path, so many
radials that share one material batch into a single draw. The per-element
material override (`nt_ui_image_payload_t.material`) carries the SDF fragment
shader and extended vertex layout; the walker only re-binds it when the `.id`
differs from the currently bound material, so a screen full of identical-material
radials still collapses to one `set_material` and one draw.

## Name-bound injection vocabulary

The per-vertex custom block is **untyped**. The bound material's `attr_map` is
the single source of truth for what the floats mean. The widget supplies its
data block with zero placeholders where walker-derived attrs sit; the walker
scans the `attr_map` and fills any attr it recognizes **by name**:

- `a_layout` vec4 = `{aspect = bbox w/h, bbox_width_px, bbox_height_px, 0}`
- `a_uvrect` vec4 = `{u0, v0, u1, v1}` = the region's min/max atlas UV

The block float-offset of attr *i* is `i*4` (attr_map declaration order; each
attr is one FLOAT4). Everything the walker does not recognize by name is baked
verbatim from the widget's block.

**To add a new injected value:** pick a new attr name, fill it in the walker,
and name it in a material's `attr_map`. No payload struct change and no public
API change. There are deliberately no per-widget flags or branches in the
walker — it has one generic custom-emit branch keyed on `payload.custom != NULL`.

## geom_mode: REGION vs GEOMETRY

`geom_mode` selects how the walker rasterizes the element's bbox when the block
is present:

- **`NT_UI_IMAGE_GEOM_REGION`** — the textured `emit_region` / `emit_slice9`
  path. Real atlas art; origin, flip, and slice9 are honored. Used by
  `nt_ui_radial_image`, which reveals a real texture.
- **`NT_UI_IMAGE_GEOM_GEOMETRY`** — a clean 4-corner bbox quad (TL/TR/BR/BL)
  against the white region via `emit_geometry`. Required by SDF shaders that
  derive a local `[-1,1]` coordinate from `gl_VertexID & 3`; a packed region's
  own winding would break that derivation. Used by `nt_ui_radial` (flat SDF
  shape on the white pixel).

## The four walls (what this path does NOT do)

The custom-attr block is **uniform across a widget's verts** — it behaves like
the per-emit color, set once and baked into every vertex. This gives four hard
boundaries:

1. **No per-vertex data.** A composite widget (segmented bar, sparkline, minimap
   blips) is N separate emit calls, not one call with a vertex stream.
2. **16-float cap.** Four FLOAT4 attrs at `NT_SPRITE_CUSTOM_STRIDE_MAX` (64 B).
3. **Time / animation is not a walker injection.** A widget that needs a time-driven
   shader writes the current time into `custom_attrs` itself each frame — no shipped
   widget does this (the demo animates via `color_packed`); the walker injects only layout.
4. **A second texture rides the material** (`textures[]`), not the custom block.

## Reveal modes and v1 limits (`nt_ui_radial_image`)

`nt_ui_radial_image` textures a real atlas region and applies a reveal effect to
the **un-swept** (remaining) sector; the swept sector always renders at full
color. The catalogue:

| Mode | Effect on un-swept sector |
| --- | --- |
| `DESATURATE` | grayscale (luma), alpha preserved |
| `DIM` | multiplied by `dim_factor` |
| `HIDE` | discarded (fully hidden) |
| `TINT` | mixed toward a tint color |

`mode` and `dim_factor` are baked on the **material** at creation
(`u_reveal_mode = {mode, dim_factor, 0, 0}`), so N widgets sharing a material
reveal in the same mode. The **tint is per-widget** (`tint_color_packed` +
`tint_strength` → baked into `a_tint`), so many differently-tinted radials share
one TINT-mode material and still batch to a single draw.

The reveal fragment shader normalizes `v_texcoord` into region-local `[-1,1]`,
so the wedge centers on the region wherever it sits in the atlas page; this works
with any rectangular region (full-bleed `[0,1]` texture or a packed sub-region).

**v1 limits:**

- **slice9 is rejected.** The UV is non-linear across slice9 patches, so the
  ring/reveal would deform. The slice9 struct fields remain for ABI parity with
  `nt_ui_image_style_t` but the widget asserts they are unset. A real
  geometry-local coordinate is the future path that would lift this.
- **Angular convention is mathematical:** `0 = +X` axis, CCW positive. Two
  independent `angle_start` / `angle_end` drive the sweep; there is no CW/CCW
  flag — direction is implicit in the start/end order, and **swapping the two
  angles reverses the sweep** (flip is API-layer, no shader branch).
- **`fill` 0..1** is a thin convenience mapping `angle_end = angle_start +
  clamp(fill,0,1) * sweep_total` for cooldown / hold_progress idioms.
- **`inner_radius_norm` `[0,1)`** carves a ring (0 = full disc); aspect from the
  bbox lets the same shape render as an oval.
