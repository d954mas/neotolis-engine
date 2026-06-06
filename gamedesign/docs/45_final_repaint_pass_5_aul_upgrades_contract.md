# 45. Final Repaint Pass 5 Aul Upgrades Contract

Status: queued, do not start until Pass 4 FX is delivered or GDD explicitly promotes this pass.

Date: 2026-06-06.

## Purpose

Replace the aul upgrade placeholder PNGs with candidate final art.

This pass supports the long meta-progression fantasy:

```text
clan camp -> clan aul -> fortified aul -> trade settlement -> steppe town/capital
```

For current runtime filenames this is represented by five staged aul sprites plus a Tamga post:

```text
aul_stage_01_camp
aul_stage_02_settlement
aul_stage_03_village
aul_stage_04_fortified_aul
aul_stage_05_steppe_capital
aul_tamga_post_01
```

This is a future/progression art pass. It should not block first playable map/UI QA.

## Rules

1. Replace only the listed files in `games/turkic-jam-2026/raw/aul/`.
2. Do not rename files.
3. Do not add new ids.
4. Do not touch current P0 aul files from Pass 3:
   - `aul_ground_2x2.png`
   - `aul_yurt_small_01.png`
   - `aul_yurt_small_02.png`
   - `aul_fire_01.png`
5. Keep exact dimensions and RGBA alpha.
6. Every sprite must contain visible pixels.
7. Create a contact sheet in `gamedesign/assets/concept/final_repaint_pass_5_aul_upgrades/`.
8. Report exact changed files, dimensions, what remains placeholder, ready-for-programmer status, and screenshot QA needs.

## Required Files

```text
raw/aul/aul_tamga_post_01.png             128x128 RGBA
raw/aul/aul_stage_01_camp.png             256x256 RGBA
raw/aul/aul_stage_02_settlement.png       256x256 RGBA
raw/aul/aul_stage_03_village.png          256x256 RGBA
raw/aul/aul_stage_04_fortified_aul.png    256x256 RGBA
raw/aul/aul_stage_05_steppe_capital.png   256x256 RGBA
```

## Stage Direction

### `aul_stage_01_camp`

Meaning:

```text
starting clan camp / стойбище рода
```

Visual:

- 2-3 yurts;
- central fire;
- trampled sand/felt mats;
- simple storage bundle;
- small family/clan scale.

Must not look like a city.

### `aul_stage_02_settlement`

Meaning:

```text
clan aul / родовой аул
```

Visual:

- more yurts;
- small enclosure/corral;
- storyteller or gathering place implied by layout;
- stronger central Tamga presence;
- still nomadic and practical.

### `aul_stage_03_village`

Meaning:

```text
stable village / growing aul
```

Visual:

- compact permanent-feeling camp;
- workshops/storage;
- more paths between yurts;
- visible order and growth;
- no stone palace language.

### `aul_stage_04_fortified_aul`

Meaning:

```text
fortified aul
```

Visual:

- low palisade/earthwork/stakes;
- watch fires;
- guarded entrances;
- protected but still steppe/aul, not medieval castle.

### `aul_stage_05_steppe_capital`

Meaning:

```text
steppe capital / political-spiritual center
```

Visual:

- large organized center;
- banners, caravan signs, gathering ring;
- water or garden hint is allowed if grounded and restrained;
- power comes from clans, routes, memory and Tamga, not golden palace fantasy.

Must avoid:

- Arabic palace silhouette;
- golden domes;
- Aladdin/bazaar/exotic market cliche;
- giant fantasy temple;
- copied real sacred signs.

### `aul_tamga_post_01`

Meaning:

```text
fictional clan sign / memory marker for aul progression
```

Visual:

- wooden/felt/stone post;
- fictional Tamga-like mark;
- readable at 128x128;
- compatible with stage 1 and later stages.

Do not copy real clan/sacred Tamga marks.

## Layering

These staged sprites are future large center visuals, not individual map tiles.

They may later be used as:

- center-map stage replacement;
- death/inter-run screen visual;
- aul upgrade screen icon;
- progression preview.

For first playable, `aul_ground_2x2` + yurts + fire remain the current map composition until Code implements stage switching.

## Acceptance Criteria

1. All six PNGs exist at exact dimensions and RGBA mode.
2. Every sprite has visible alpha.
3. Stage silhouettes clearly grow from small camp to steppe capital.
4. No Arabic palace/genie/lamp/bazaar/flying-carpet/gold-fantasy drift.
5. No copied real sacred/tribal Tamga.
6. Each stage reads at map-center scale and in a smaller upgrade UI preview.
7. Contact sheet shows all stages in order so progression is obvious.

## Code Request After Delivery

After Art delivery:

1. Keep these as future/library assets unless aul upgrade feature starts.
2. Do not replace current P0 aul map composition automatically.
3. If feature starts, use them first in an upgrade/progression preview screen or death/inter-run screen.
4. Runtime QA needs screenshots at:
   - map-center scale;
   - upgrade UI preview scale.
5. Do not call these final accepted until L5 screenshot/readability evidence exists.
