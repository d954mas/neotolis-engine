# 51. Final repaint pass 7 - hero archetype panels contract

Status: active art contract.

Owner: Art thread.

Purpose: prepare final candidate art for the heir/archetype selection panel without changing gameplay ids, balance, or normal runtime flow.

This pass is production asset preparation, not fake-shot work. The assets support the future heir-select screen and RPG-style right hero panel.

## Rules

1. Replace only exact files listed below.
2. Do not rename files.
3. Do not add new ids.
4. Do not touch Pass 1-6 candidate-final PNGs except the explicit icon fix targets below.
5. Keep exact dimensions and alpha rules.
6. Every RGBA sprite must have visible pixels and useful transparent bounds.
7. Create a contact sheet in `gamedesign/assets/concept/final_repaint_pass_7_hero_archetype_panels/`.
8. Report exact changed files, dimensions, what remains placeholder, ready-for-programmer status, and screenshot QA needs.

## Required hero archetype panels

All files are `128x192`, RGBA.

```text
raw/hero/hero_body_panel.png
raw/hero/hero_mind_panel.png
raw/hero/hero_spirit_panel.png
```

Player-facing meaning:

| File | Archetype | Role |
| --- | --- | --- |
| `hero_body_panel.png` | Путник тела | устойчивость, риск, ближнее выживание |
| `hero_mind_panel.png` | Путник разума | карты, память, выборы, знания |
| `hero_spirit_panel.png` | Путник духа | Тамга, вера рода, стойкость к страху/буре |

Style notes:

- These are panel dolls/portraits, not full gameplay walk sprites.
- Silhouette must read in the right-side RPG/Loop Hero-style hero panel.
- Keep the first hero language: practical traveler, cloak, satchel, staff, simple steppe/aul survival gear.
- Each archetype needs one clear silhouette cue, not a costume overload.
- Body: broader stance, staff/sling/pack, sturdier cloak.
- Mind: map strip, small tablet/knotted cord, calmer pose.
- Spirit: small fictional tamga charm, wind/felt trim, solemn pose.
- Do not make archetypes into wizards, knights, palace guards, or generic fantasy classes.
- Do not use real sacred signs; tamga marks must remain fictional.

## Required icon readability fixes

These files were already repainted, but GDD flagged scale risk. Repaint only if the current silhouette is not readable at `24px`.

All files are `32x32`, RGBA.

```text
raw/icons/icon_aul_upgrade_32.png
raw/icons/icon_settings_32.png
```

Rules:

- Keep same ids and filenames.
- Prefer bold central shapes over thin linework.
- `icon_aul_upgrade_32`: should read as upgrade/growth of the aul, not a generic house.
- `icon_settings_32`: should read at 24px; a simple tool/knot/toggle is acceptable if clearer than a gear.

## Contact sheet

The contact sheet must include:

```text
3 hero panel dolls at native scale
3 hero panel dolls scaled down to expected UI panel scale
2 icon fix targets at 32px and 24px
```

## Delivery checklist

```text
Exact listed files replaced:
Dimensions/mode verified:
Non-empty alpha verified:
Contact sheet created:
Pass 1-6 untouched except allowed icon fixes:
Ready for programmer:
Runtime QA needs:
```

## Cultural guardrails

- No Aladdin/palace/genie/flying carpet/bazaar/gold fantasy.
- No copied real sacred tamgas.
- No generic magic rune language.
- No RPG class cosplay that breaks the grounded traveler fantasy.
- Keep the feeling: Turkic-nomadic game-fairy-tale, road, aul, fire, trace, memory, survival in desert/steppe.
