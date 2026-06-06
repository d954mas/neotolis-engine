# 54. Art source policy - generated bitmap production art

Status: active art direction note.

Owner: Art thread.

Audience: GDD / Art lead / Code.

## Decision

Candidate-final production art should use generated bitmap / painted source art as the visual baseline.

Deterministic script-drawn vector-like assets are acceptable only as temporary pipeline placeholders, validation fixtures, or small technical UI symbols. They should not be presented as final candidate art when the user expects polished visual production.

## Reason

The user explicitly rejected the earlier Pass 7 shape-based hero archetype panels as "scary SVG, not beautiful art." The corrected Pass 7 imagegen repaint is the approved direction for future character/panel-style production:

- generated bitmap source sheet;
- local cleanup / chroma removal;
- exact runtime slicing;
- exact filenames, dimensions, and alpha rules;
- contact sheet with native and runtime-scale preview.

## Production Rule

For future art passes, prefer this ladder:

```text
generated bitmap source -> cleanup/crop/slice -> exact raw PNG -> validator -> contact sheet -> runtime QA
```

Do not replace this with pure `ImageDraw` / vector-shape construction unless the deliverable is explicitly a placeholder, a 9-slice UI surface, a tiny utility icon, or a technical validator asset.

## Applies To

Use generated bitmap source for:

- hero/archetype panels;
- card art where visual appeal matters;
- active tile objects with character/landmark identity;
- aul progression polish;
- future narrative/memory art.

Script/deterministic drawing may still be used for:

- temporary placeholder kits;
- atlas/pack validation;
- simple HUD icons where readability is the main requirement;
- 9-slice panels/chips/buttons where procedural consistency is useful.

## Reference

Corrected Pass 7 source and contact sheet:

```text
gamedesign/assets/concept/final_repaint_pass_7_hero_archetype_panels/final_repaint_pass_7_imagegen_source.png
gamedesign/assets/concept/final_repaint_pass_7_hero_archetype_panels/final_repaint_pass_7_hero_archetype_panels_contact_sheet.png
```

Runtime files replaced in the corrected Pass 7:

```text
games/turkic-jam-2026/raw/hero/hero_body_panel.png
games/turkic-jam-2026/raw/hero/hero_mind_panel.png
games/turkic-jam-2026/raw/hero/hero_spirit_panel.png
```

## Note To GDD

Please treat generated bitmap art as the production baseline for future candidate-final visual passes. If a pass must use procedural/scripted output for speed, mark it explicitly as placeholder or technical art, not final candidate art.
