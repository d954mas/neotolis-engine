# Radial widgets & the custom-attr image path

Design rationale for `nt_ui_radial_image` and the generic custom-attr
atlas-region emit it uses (`nt_ui_image_custom`): Clay IMAGE + per-element
material for batching, the walker-written aspect, and reveal modes with their
v1 limits. Flat radial shapes use `nt_ui_shape` instead.

Related: [Scope](../core/scope.md), [Rich Text](rich-text.md), [Material System](../render/material.md)

This section holds the design rationale behind the textured radial reveal
(`nt_ui_radial_image`) and its generic custom-attr atlas-region emit. The
analytic flat RADIAL is specified in [Analytic shapes](nt-ui.md#analytic-shapes).

## Route A (Clay CUSTOM) vs Route B (IMAGE + material)

A textured radial reveal could be drawn two ways:

- **Route A — Clay CUSTOM element.** The game gets a bbox and a raw draw
  callback and emits geometry itself.
- **Route B — Clay IMAGE element + a per-element material.** The widget rides
  the existing UI walker image path; the walker emits a textured/white-region
  quad through the sprite renderer and binds the widget's material.

Neotolis uses **Route B**. The reason is batching: Route A drops out of the
walker's image emit and cannot share draw state, so every CUSTOM widget is its
own draw. Route B keeps every reveal on the sprite renderer's emit path, so many
reveals that share one material batch into a single draw. The per-element
material override (`nt_ui_image_payload_t.material`) carries the SDF fragment
shader and extended vertex layout; the walker only re-binds it when the `.id`
differs from the currently bound material, so a screen full of identical-material
reveals still collapses to one `set_material` and one draw.

## Walker-written aspect

The custom block is a byte record copied verbatim to each vertex tail. The
material's full `vertex_layout` declares its storage. `custom_bytes` equals
`vertex_layout.stride - 20`; the UI custom record has capacity64 bytes. The
only walker-written value is the bbox aspect: when `aspect_offset` is nonzero,
the walker writes FLOAT bbox width / height (1 when height is zero) at that
full-vertex byte offset after Clay layout. The offset must lie inside the tail.
Other bytes, including any per-widget layout or UV data, come from the widget.
Custom images always use the region emit (`emit_region` / `emit_slice9`), so
origin, flip and slice9 are honored by generic custom images.

`nt_ui_radial_image` uses the exported `NT_UI_RADIAL_IMAGE_VERTEX_LAYOUT`, a
64-byte full vertex: `a_radial` FLOAT4 at offset20 (location4), `a_tint` FLOAT4
at36 (location5), `a_aspect` FLOAT at52 (location7) and `a_source_uv` FLOAT2 at56
(location6). Its material must use that layout and set `source_uv_offset =
NT_UI_RADIAL_IMAGE_SOURCE_UV_OFFSET` (56); the widget asserts both. No
`attr_map` is needed. The first two fields are uniform per emit. The widget sets
`aspect_offset` to 52, and the sprite renderer overwrites `a_source_uv` per
region vertex from the source-space position and original source dimensions.
The generic custom-image API still accepts other valid byte layouts.

`nt_ui_radial_image` uses the generic custom-emit branch keyed on
`payload.custom != NULL`. The separate [analytic shape path](nt-ui.md#analytic-shapes)
uses the private payload flag `NT_UI_IMAGE_ANALYTIC_SHAPE` for copied shape
styles and expanded paint bounds; it does not change this generic custom-image
contract or radial-image geometry.

## The four walls (what this path does NOT do)

The custom-attr block is **uniform across a widget's verts** — it behaves like
the per-emit color, passed with each emit and baked into every vertex. This gives four hard
boundaries:

1. **No per-vertex data.** A composite widget (segmented bar, sparkline, minimap
   blips) is N separate emit calls, not one call with a vertex stream.
2. **64-byte UI cap.** Typed fields and padding occupy the same byte record.
3. **Time / animation is not a walker injection.** A widget that needs a time-driven
   shader writes the current time into `custom_attrs` itself each frame — no shipped
   widget does this (the demo animates via `color_packed`); the walker writes only the optional aspect.
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

The sprite renderer writes source-image UV into `a_source_uv` from each region
vertex's source-space position, including the alpha-trim offset. The wedge uses
these coordinates; the fragment shader samples the original packed atlas UV.
Packing placement and all D4 orientations therefore leave the wedge fixed to
the source image. Explicit sprite flips mirror the art and wedge together.
The radial full vertex is 64 bytes; basic sprites remain 20 bytes.

**v1 limits:**

- **slice9 is rejected.** The region source-coordinate producer does not cover
  independently stretched slice9 patch vertices. The slice9 struct fields remain
  for ABI parity with `nt_ui_image_style_t`; the widget asserts both style
  overrides and baked atlas borders are unset.
- **Angular convention follows local UI coordinates:** Y points down,
  `0` points right, `+π/2` points down, `π` points left, and `3π/2` points
  up. Increasing angles sweep clockwise on an unflipped, untransformed image,
  independent of its atlas packing orientation.
  Two independent `angle_start` / `angle_end` drive the positive wrapped span;
  swapping them selects the complementary span, not a short reverse sweep.
  Explicit image flips mirror the wedge with the art. Atlas D4 packing is
  inverted before the angular test and does not alter the visible wedge.
- **`fill` 0..1** is a thin convenience mapping `angle_end = angle_start +
  clamp(fill,0,1) * sweep_total` for cooldown / hold_progress idioms. Equal
  start/end angles have zero swept coverage, including the start ray, so HIDE
  leaves no seam at `fill=0`.
- **`inner_radius_norm` `[0,1)`** carves a ring (0 = full disc); aspect from the
  bbox lets the same shape render as an oval.
