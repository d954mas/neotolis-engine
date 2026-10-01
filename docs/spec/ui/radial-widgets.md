# Radial image widget

Design rationale for `nt_ui_radial_image`: a textured reveal drawn through the
walker's IMAGE path with a per-element material, its walker-written vertex tail,
and its reveal modes and limits. Flat radial shapes use `nt_ui_shape` instead.

Related: [Scope](../core/scope.md), [Rich Text](rich-text.md), [Material System](../render/material.md)

The analytic flat RADIAL is specified in [Analytic shapes](nt-ui.md#analytic-shapes).

## Route A (Clay CUSTOM) vs Route B (IMAGE + material)

A textured radial reveal could be drawn two ways:

- **Route A — Clay CUSTOM element.** The game gets a bbox and a raw draw
  callback and emits geometry itself.
- **Route B — Clay IMAGE element + a per-element material.** The widget rides
  the existing UI walker image path; the walker emits the atlas region through
  the sprite renderer and binds the widget's material.

Neotolis uses **Route B**. Route A drops out of the walker's image emit and
cannot share draw state, so every CUSTOM widget is its own draw. Route B keeps
every reveal on the sprite renderer's emit path, so reveals that share one
material batch into a single draw. The walker re-binds the per-element material
(`nt_ui_image_payload_t.material`) only when its `.id` differs from the bound
material.

## Vertex tail

The widget stores its parameters in a typed payload (engine-owned flag
`NT_UI_IMAGE_RADIAL_REVEAL`). At walk time the walker writes one
`nt_ui_radial_image_tail_t` per emit, after the 20-byte sprite prefix:

| Offset | Location | Field |
|---:|---:|---|
| 20 | 4 | FLOAT4 `a_radial`: start, end, inner radius, bbox width/height |
| 36 | 5 | FLOAT4 `a_tint`: RGB 0..1, TINT strength |
| 52 | 6 | FLOAT3 `a_source_u` |
| 64 | 7 | FLOAT3 `a_source_v` |

The radial-image material uses the exported `NT_UI_RADIAL_IMAGE_VERTEX_LAYOUT`
(76-byte vertex); basic sprites remain 20 bytes. `a_source_u/v` map the vertex's
atlas UV to source-image coordinates (x right, y down, before alpha trim):
`source = (dot(a_source_u, (uv, 1)), dot(a_source_v, (uv, 1)))`. One region's
atlas placement is rigid — trim, packing and D4 orientation — so the map is
affine; the walker solves it once per emit from the region's largest triangle.
The bbox aspect needs final layout, so the walker writes it too. The sprite
renderer copies the tail verbatim and knows nothing about radial images.

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

The wedge uses source-image coordinates; the fragment shader samples the
original packed atlas UV. Packing placement and all D4 orientations therefore
leave the wedge fixed to the source image. Explicit sprite flips mirror the art
and wedge together.

**v1 limits:**

- **No slice9.** Slice9 stretches patches independently, which would distort
  angles measured in source-image space. The radial-image style has no slice9
  fields and rejects `NT_UI_IMAGE_SLICE9_OVERRIDE`; a region with baked borders
  draws as a plain quad.
- **Angular convention follows local UI coordinates:** Y points down,
  `0` points right, `+π/2` points down, `π` points left, and `3π/2` points
  up. Increasing angles sweep clockwise on an unflipped, untransformed image,
  independent of its atlas packing orientation.
  The span is `(angle_end - angle_start)` wrapped into `[0, 2π)`; swapping the
  angles selects the complementary span, and a difference of at least `2π` in
  magnitude reveals the full turn.
  Explicit image flips mirror the wedge with the art. Atlas D4 packing is
  inverted before the angular test and does not alter the visible wedge.
- **`fill` 0..1** is a thin convenience mapping `angle_end = angle_start +
  clamp(fill,0,1) * sweep_total` for cooldown / hold_progress idioms. Equal
  start/end angles have zero swept coverage, including the start ray, so HIDE
  leaves no seam at `fill=0`.
- **`inner_radius_norm` `[0,1)`** carves a ring (0 = full disc); aspect from the
  bbox lets the same shape render as an oval.
