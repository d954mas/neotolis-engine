from pathlib import Path
from typing import Literal

import colorsys

from PIL import Image, ImageDraw, ImageEnhance, ImageFilter


ROOT = Path(__file__).resolve().parents[2]
RAW = ROOT / "games" / "turkic-jam-2026" / "raw"
OUT = ROOT / "gamedesign" / "assets" / "concept" / "pass_13_generated_world_foundation"
SOURCE = OUT / "pass_13_world_foundation_source.png"
PROPOSED = OUT / "proposed_runtime"

Mode = Literal["RGB", "RGBA"]


ASSETS: list[dict] = [
    {
        "path": "ground/ground_sand_base_01.png",
        "size": (128, 128),
        "mode": "RGB",
        "crop": (183, 15, 349, 170),
        "role": "quiet sand base",
        "adjust": "quiet_ground",
    },
    {
        "path": "decor/decor_dune_01.png",
        "size": (128, 128),
        "mode": "RGBA",
        "crop": (535, 18, 760, 160),
        "role": "quiet low dune overlay",
        "adjust": "quiet_overlay",
    },
    {
        "path": "decor/decor_stones_01.png",
        "size": (128, 128),
        "mode": "RGBA",
        "crop": (520, 165, 760, 286),
        "role": "small stones overlay",
        "adjust": "quiet_overlay",
    },
    {
        "path": "decor/decor_dry_grass_01.png",
        "size": (128, 128),
        "mode": "RGBA",
        "crop": (758, 168, 938, 305),
        "role": "small dry grass overlay",
        "adjust": "quiet_overlay",
    },
    {
        "path": "decor/decor_tracks_01.png",
        "size": (128, 128),
        "mode": "RGBA",
        "crop": (16, 350, 220, 458),
        "role": "old tracks, not wolf trail",
        "adjust": "quiet_overlay",
    },
    {
        "path": "decor/decor_cracks_01.png",
        "size": (128, 128),
        "mode": "RGBA",
        "crop": (775, 350, 960, 466),
        "role": "dry cracks overlay",
        "adjust": "quiet_overlay",
    },
    {
        "path": "road/road_straight_ns.png",
        "size": (128, 128),
        "mode": "RGBA",
        "crop": (981, 20, 1118, 274),
        "role": "packed road north-south",
        "adjust": "road",
    },
    {
        "path": "road/road_straight_ew.png",
        "size": (128, 128),
        "mode": "RGBA",
        "crop": (975, 224, 1220, 358),
        "role": "packed road east-west",
        "adjust": "road",
    },
    {
        "path": "road/road_corner_ne.png",
        "size": (128, 128),
        "mode": "RGBA",
        "crop": (1222, 42, 1390, 206),
        "role": "packed road corner NE",
        "adjust": "road",
    },
    {
        "path": "road/road_corner_es.png",
        "size": (128, 128),
        "mode": "RGBA",
        "crop": (1368, 42, 1532, 206),
        "role": "packed road corner ES",
        "adjust": "road",
    },
    {
        "path": "road/road_corner_sw.png",
        "size": (128, 128),
        "mode": "RGBA",
        "crop": (1222, 222, 1390, 386),
        "role": "packed road corner SW",
        "adjust": "road",
    },
    {
        "path": "road/road_corner_wn.png",
        "size": (128, 128),
        "mode": "RGBA",
        "crop": (1368, 222, 1532, 386),
        "role": "packed road corner WN",
        "adjust": "road",
    },
    {
        "path": "road/buffer_edge_stones_01.png",
        "size": (128, 128),
        "mode": "RGBA",
        "crop": (14, 505, 245, 565),
        "role": "stone no-build edge, not wall",
        "adjust": "buffer",
    },
    {
        "path": "road/buffer_packed_sand_01.png",
        "size": (128, 128),
        "mode": "RGBA",
        "crop": (248, 505, 520, 565),
        "role": "packed no-build sand edge",
        "adjust": "buffer",
    },
    {
        "path": "aul/aul_ground_2x2.png",
        "size": (256, 256),
        "mode": "RGB",
        "crop": (5, 705, 370, 1018),
        "role": "small packed-earth aul ground",
        "adjust": "aul_ground",
    },
    {
        "path": "aul/aul_fire_01.png",
        "size": (128, 128),
        "mode": "RGBA",
        "crop": (1050, 805, 1185, 950),
        "role": "small campfire",
        "adjust": "aul_object",
    },
]


def remove_magenta(img: Image.Image) -> Image.Image:
    rgba = img.convert("RGBA")
    px = rgba.load()
    for y in range(rgba.height):
        for x in range(rgba.width):
            r, g, b, a = px[x, y]
            h, s, v = colorsys.rgb_to_hsv(r / 255, g / 255, b / 255)
            is_magenta_hue = (h > 0.79 or h < 0.03) and s > 0.32 and r > 115 and b > 105 and g < 150
            if r > 200 and b > 180 and g < 120:
                px[x, y] = (r, g, b, 0)
            elif is_magenta_hue:
                px[x, y] = (r, g, b, 0)
    return rgba


def crop_alpha(img: Image.Image, padding: int = 8) -> Image.Image:
    bbox = img.getchannel("A").getbbox()
    if bbox is None:
        return img
    x0 = max(0, bbox[0] - padding)
    y0 = max(0, bbox[1] - padding)
    x1 = min(img.width, bbox[2] + padding)
    y1 = min(img.height, bbox[3] + padding)
    return img.crop((x0, y0, x1, y1))


def fit_center(img: Image.Image, size: tuple[int, int], fill=(0, 0, 0, 0)) -> Image.Image:
    out = Image.new("RGBA", size, fill)
    tmp = img.copy()
    tmp.thumbnail((size[0] - 4, size[1] - 4), Image.Resampling.LANCZOS)
    out.alpha_composite(tmp.convert("RGBA"), ((size[0] - tmp.width) // 2, (size[1] - tmp.height) // 2))
    return out


def make_candidate(source: Image.Image, asset: dict) -> Image.Image:
    crop = source.crop(asset["crop"])
    if asset["mode"] == "RGB":
        if asset["adjust"] == "aul_ground":
            cleaned = crop_alpha(remove_magenta(crop), 2)
            rgba = fit_center(cleaned, asset["size"])
            bg = Image.new("RGBA", asset["size"], (181, 124, 63, 255))
            bg.alpha_composite(rgba)
            img = bg.convert("RGB")
        else:
            img = crop.convert("RGB").resize(asset["size"], Image.Resampling.LANCZOS)
    else:
        cleaned = crop_alpha(remove_magenta(crop), 6)
        img = fit_center(cleaned, asset["size"])

    adjust = asset["adjust"]
    if adjust == "quiet_ground":
        img = ImageEnhance.Contrast(img).enhance(0.72)
        img = ImageEnhance.Color(img).enhance(0.9)
        img = img.filter(ImageFilter.GaussianBlur(0.25))
    elif adjust == "quiet_overlay":
        img = ImageEnhance.Contrast(img).enhance(0.86)
        if img.mode == "RGBA":
            alpha = img.getchannel("A").point(lambda a: int(a * 0.82))
            img.putalpha(alpha)
    elif adjust == "buffer":
        if img.mode == "RGBA":
            alpha = img.getchannel("A").point(lambda a: int(a * 0.9))
            img.putalpha(alpha)
    elif adjust == "aul_ground":
        img = ImageEnhance.Contrast(img).enhance(0.82)
        img = ImageEnhance.Color(img).enhance(0.88)
    return img


def checker(size: tuple[int, int]) -> Image.Image:
    img = Image.new("RGB", size, (35, 36, 33))
    d = ImageDraw.Draw(img)
    step = 8
    for y in range(0, size[1], step):
        for x in range(0, size[0], step):
            if (x // step + y // step) % 2:
                d.rectangle((x, y, x + step - 1, y + step - 1), fill=(45, 45, 41))
    return img.convert("RGBA")


def preview(path: Path, size=(96, 96)) -> Image.Image:
    img = Image.open(path).convert("RGBA")
    bg = checker(size)
    tmp = img.copy()
    tmp.thumbnail((size[0] - 8, size[1] - 8), Image.Resampling.LANCZOS)
    bg.alpha_composite(tmp, ((size[0] - tmp.width) // 2, (size[1] - tmp.height) // 2))
    return bg


def contact_sheet() -> None:
    row_h = 128
    sheet = Image.new("RGBA", (1120, 54 + len(ASSETS) * row_h), (238, 229, 207, 255))
    d = ImageDraw.Draw(sheet, "RGBA")
    d.text((16, 14), "Pass 13 slicing proposal: current technical raw vs generated candidate (concept only)", fill=(30, 27, 23, 255))
    d.text((20, 38), "asset", fill=(30, 27, 23, 255))
    d.text((300, 38), "current raw", fill=(30, 27, 23, 255))
    d.text((450, 38), "proposed generated", fill=(30, 27, 23, 255))
    d.text((620, 38), "role / review risk", fill=(30, 27, 23, 255))

    for i, asset in enumerate(ASSETS):
        y = 54 + i * row_h
        d.line((16, y, 1100, y), fill=(185, 171, 139, 255), width=1)
        d.text((20, y + 18), asset["path"], fill=(30, 27, 23, 255))
        d.text((20, y + 42), f"{asset['size'][0]}x{asset['size'][1]} {asset['mode']}", fill=(85, 76, 62, 255))
        current = preview(RAW / asset["path"])
        proposed = preview(PROPOSED / asset["path"])
        sheet.alpha_composite(current, (298, y + 14))
        sheet.alpha_composite(proposed, (455, y + 14))
        d.text((620, y + 18), asset["role"], fill=(30, 27, 23, 255))
        d.text((620, y + 44), f"crop {asset['crop']} / adjust {asset['adjust']}", fill=(85, 76, 62, 255))

    sheet.save(OUT / "pass_13_world_foundation_contact_sheet.png")
    sheet.save(OUT / "pass_13_world_foundation_slicing_proposal_contact.png")


def write_docs() -> None:
    reuse = [
        "# Pass 13 World Foundation Reuse Map",
        "",
        "Status: slicing proposal only. Runtime raw overwrite is not approved.",
        "",
        "## Component Inventory",
        "",
        "- ground sand base",
        "- decor overlays: dune, stones, dry_grass, tracks, cracks",
        "- road material: straight/corner pieces",
        "- road buffer: stones and packed edge",
        "- aul core: packed earth and fire",
        "",
        "## Existing Sources Reused",
        "",
        "- `pass_13_world_foundation_source.png`: generated bitmap world material family source",
        "- Current `games/turkic-jam-2026/raw/*`: comparison only, not overwritten",
        "",
        "## New Generated Source Needed",
        "",
        "No additional generation for this slicing proposal. Use one coherent Pass 13 source sheet.",
        "",
        "## Runtime Files Touched",
        "",
        "None. Proposed exports are written only under `gamedesign/assets/concept/pass_13_generated_world_foundation/proposed_runtime/`.",
        "",
        "## Reuse Map",
        "",
    ]
    for asset in ASSETS:
        reuse.append(f"- source crop `{asset['crop']}` -> proposed `{asset['path']}` -> role: {asset['role']}")
    reuse.extend(["", "## Per-Asset Proposal", ""])
    for asset in ASSETS:
        reuse.extend(
            [
                f"### `{asset['path']}`",
                "",
                f"- source crop / component reference: `{asset['crop']}` from `pass_13_world_foundation_source.png`",
                "- reuse operation: reuse accepted Pass 13 source crop; cleanup/crop/downscale only; no new generation",
                f"- export proposal: `{asset['size'][0]}x{asset['size'][1]} {asset['mode']}` under `proposed_runtime/`",
                f"- role: {asset['role']}",
                f"- adjustment: `{asset['adjust']}`",
                f"- risk: {risk_for(asset)}",
                "",
            ]
        )
    reuse.extend(
        [
            "## Global Risks",
            "",
            "- Sand base may still be too noisy at the increased map scale.",
            "- Decor overlays must stay quieter than Pass 9 active tile objects.",
            "- Road straight/corner widths need GDD review before runtime slicing.",
            "- Buffer stones/packed edge should read no-build, not road or wall.",
            "- Aul ground and fire are proposed first; yurts stay deferred because source yurts are large/detailed.",
            "- Magenta cleanup around thin decor must be validated again before raw export.",
        ]
    )
    (OUT / "pass_13_world_foundation_reuse_map.md").write_text("\n".join(reuse), encoding="utf-8")

    crop_lines = [
        "# Pass 13 World Foundation Crop Map",
        "",
        "Status: proposal only. Coordinates refer to `pass_13_world_foundation_source.png`.",
        "",
        "| Runtime target | Size | Mode | Source crop px | Role | Adjustment |",
        "| --- | --- | --- | --- | --- | --- |",
    ]
    for asset in ASSETS:
        crop_lines.append(
            f"| `{asset['path']}` | `{asset['size'][0]}x{asset['size'][1]}` | `{asset['mode']}` | `{asset['crop']}` | {asset['role']} | `{asset['adjust']}` |"
        )
    (OUT / "pass_13_world_foundation_crop_map.md").write_text("\n".join(crop_lines), encoding="utf-8")


def main() -> None:
    PROPOSED.mkdir(parents=True, exist_ok=True)
    source = Image.open(SOURCE).convert("RGBA")
    for asset in ASSETS:
        out_path = PROPOSED / asset["path"]
        out_path.parent.mkdir(parents=True, exist_ok=True)
        make_candidate(source, asset).save(out_path)
    contact_sheet()
    write_docs()
    print((OUT / "pass_13_world_foundation_contact_sheet.png").relative_to(ROOT).as_posix())
    print((OUT / "pass_13_world_foundation_slicing_proposal_contact.png").relative_to(ROOT).as_posix())
    print((OUT / "pass_13_world_foundation_reuse_map.md").relative_to(ROOT).as_posix())
    print((OUT / "pass_13_world_foundation_crop_map.md").relative_to(ROOT).as_posix())
    print(PROPOSED.relative_to(ROOT).as_posix())


def risk_for(asset: dict) -> str:
    path = asset["path"]
    if "ground_sand" in path:
        return "noise"
    if "road_corner" in path or "road_straight" in path:
        return "road-width mismatch"
    if "buffer" in path:
        return "wall-read"
    if "decor" in path:
        return "active-object-read / halo"
    if "aul" in path:
        return "noise / active-object-read"
    return "halo"


if __name__ == "__main__":
    main()
