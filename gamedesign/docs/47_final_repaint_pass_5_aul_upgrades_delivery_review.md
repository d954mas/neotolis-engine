# 47. Final repaint pass 5 aul upgrades delivery review

Status: candidate final art delivered, not final accepted.

Date: 2026-06-06.

## Scope

Pass 5 replaces the future/progression aul visuals:

```text
games/turkic-jam-2026/raw/aul/aul_tamga_post_01.png
games/turkic-jam-2026/raw/aul/aul_stage_01_camp.png
games/turkic-jam-2026/raw/aul/aul_stage_02_settlement.png
games/turkic-jam-2026/raw/aul/aul_stage_03_village.png
games/turkic-jam-2026/raw/aul/aul_stage_04_fortified_aul.png
games/turkic-jam-2026/raw/aul/aul_stage_05_steppe_capital.png
```

Contact sheet:

```text
gamedesign/assets/concept/final_repaint_pass_5_aul_upgrades/final_repaint_pass_5_aul_upgrades_contact_sheet.png
```

## Technical check

Validator passed:

```text
py -3.12 tmp/validate_final_repaint_pass_5_aul_upgrades.py

aul_tamga_post_01.png 128x128 RGBA
aul_stage_01_camp.png 256x256 RGBA
aul_stage_02_settlement.png 256x256 RGBA
aul_stage_03_village.png 256x256 RGBA
aul_stage_04_fortified_aul.png 256x256 RGBA
aul_stage_05_steppe_capital.png 256x256 RGBA
OK final repaint pass 5 aul upgrades
```

Pack check after delivery:

```text
build_turkic_jam_packs: Batch A 44/0, Batch B 47/0, optional 91/0, atlas 95 sprites
cmake --build build/_cmake/native-debug --target turkic_jam: passed
```

Important: the Pass 5 files are valid raw PNGs, but they are not yet registered in the atlas/pack. Code has been asked to add a separate optional Batch C / aul progression registry group.

## Art review

Accepted as candidate final for future progression.

Strengths:

- progression reads clearly: camp -> settlement -> village -> fortified aul -> steppe capital;
- stage 05 stays rooted in aul/steppe language instead of switching to palace fantasy;
- Tamga post is simple and fictional, not a copied sacred sign;
- silhouettes stay readable and centered for 256x256 map-scale use;
- Pass 3 starting aul assets were not touched.

Risks for runtime QA:

- `aul_stage_05_steppe_capital.png` has a central blue/turquoise shape that may read as oasis/water at small scale;
- stage 04/05 may become too busy if drawn directly on top of dense map decor;
- Tamga post needs scale proof if used as a clickable/upgradable object;
- these sprites need atlas registration before any L2/L3/L4 claim is valid.

## Code request

Add a separate optional registry group for:

```text
aul_tamga_post_01
aul_stage_01_camp
aul_stage_02_settlement
aul_stage_03_village
aul_stage_04_fortified_aul
aul_stage_05_steppe_capital
```

Keep them future/inactive until aul upgrade mechanics or screens start. First acceptance target is:

```text
L2 builder found -> L3 generated ids/runtime bind readiness
```

Do not mark Pass 5 final accepted until runtime or editor screenshots prove scale/readability.
