from pathlib import Path
from typing import Literal

import colorsys
from PIL import Image, ImageDraw, ImageEnhance, ImageFilter


ROOT = Path(__file__).resolve().parents[2]
RAW = ROOT / "games" / "turkic-jam-2026" / "raw"
OUT = ROOT / "gamedesign" / "assets" / "concept" / "pass_13_generated_world_foundation"
SOURCE = OUT / "pass_13_world_foundation_source.png"
PROPOSED = OUT / "proposed_runtime_v2"

Mode = Literal["RGB", "RGBA"]


ASSETS: list[dict] = [
    {
        "path": "ground/ground_sand_base_01.png",
        "size": (128, 128),
        "mode": "RGB",
        "role": "quiet sand base",
        "source": "sand material crop (183, 15, 349, 170)",
        "risk": "noise",
    },
    {
        "path": "decor/decor_dune_01.png",
        "size": (128, 128),
        "mode": "RGBA",
        "role": "single quiet low dune overlay",
        "source": "single dune crop (548, 30, 725, 150)",
        "risk": "active-object-read / halo",
    },
    {
        "path": "decor/decor_stones_01.png",
        "size": (128, 128),
        "mode": "RGBA",
        "role": "small stones overlay",
        "source": "small stone crop (595, 174, 720, 252)",
        "risk": "active-object-read / halo",
    },
    {
        "path": "decor/decor_dry_grass_01.png",
        "size": (128, 128),
        "mode": "RGBA",
        "role": "small dry grass overlay",
        "source": "small grass crop (760, 215, 915, 302)",
        "risk": "active-object-read / halo",
    },
    {
        "path": "decor/decor_tracks_01.png",
        "size": (128, 128),
        "mode": "RGBA",
        "role": "old quiet cart/foot tracks, not beast trail",
        "source": "quiet tracks crop (236, 352, 420, 450)",
        "risk": "active-object-read / halo",
    },
    {
        "path": "decor/decor_cracks_01.png",
        "size": (128, 128),
        "mode": "RGBA",
        "role": "dry cracks overlay",
        "source": "cracks crop (835, 365, 958, 455)",
        "risk": "active-object-read / halo",
    },
    {
        "path": "road/road_straight_ns.png",
        "size": (128, 128),
        "mode": "RGBA",
        "role": "consistent packed road north-south",
        "source": "clean inner road material crop (1100, 25, 1162, 168) + 46px road mask",
        "risk": "road-width mismatch",
    },
    {
        "path": "road/road_straight_ew.png",
        "size": (128, 128),
        "mode": "RGBA",
        "role": "consistent packed road east-west",
        "source": "same clean road material crop, rotated + 46px road mask",
        "risk": "road-width mismatch",
    },
    {
        "path": "road/road_corner_ne.png",
        "size": (128, 128),
        "mode": "RGBA",
        "role": "consistent packed road corner NE",
        "source": "same clean road material crop + 46px L mask",
        "risk": "road-width mismatch",
    },
    {
        "path": "road/road_corner_es.png",
        "size": (128, 128),
        "mode": "RGBA",
        "role": "consistent packed road corner ES",
        "source": "same clean road material crop + 46px L mask",
        "risk": "road-width mismatch",
    },
    {
        "path": "road/road_corner_sw.png",
        "size": (128, 128),
        "mode": "RGBA",
        "role": "consistent packed road corner SW",
        "source": "same clean road material crop + 46px L mask",
        "risk": "road-width mismatch",
    },
    {
        "path": "road/road_corner_wn.png",
        "size": (128, 128),
        "mode": "RGBA",
        "role": "consistent packed road corner WN",
        "source": "same clean road material crop + 46px L mask",
        "risk": "road-width mismatch",
    },
    {
        "path": "road/buffer_edge_stones_01.png",
        "size": (128, 128),
        "mode": "RGBA",
        "role": "stone no-build edge, not wall",
        "source": "low stone strip crop (8, 505, 245, 555)",
        "risk": "wall-read",
    },
    {
        "path": "road/buffer_packed_sand_01.png",
        "size": (128, 128),
        "mode": "RGBA",
        "role": "packed no-build sand edge, not road",
        "source": "packed edge crop (245, 505, 520, 558)",
        "risk": "wall-read / road-read",
    },
    {
        "path": "aul/aul_ground_2x2.png",
        "size": (256, 256),
        "mode": "RGB",
        "role": "small packed-earth aul ground",
        "source": "large aul ground crop (5, 705, 370, 1018), softened",
        "risk": "noise / active-object-read",
    },
    {
        "path": "aul/aul_fire_01.png",
        "size": (128, 128),
        "mode": "RGBA",
        "role": "small first-camp fire only, no rack structure",
        "source": "campfire-only crop (1058, 890, 1188, 1015)",
        "risk": "halo / active-object-read",
    },
]


def crop(source: Image.Image, rect: tuple[int, int, int, int]) -> Image.Image:
    return source.crop(rect)


def remove_magenta(img: Image.Image) -> Image.Image:
    rgba = img.convert("RGBA")
    px = rgba.load()
    for y in range(rgba.height):
        for x in range(rgba.width):
            r, g, b, a = px[x, y]
            h, s, _ = colorsys.rgb_to_hsv(r / 255, g / 255, b / 255)
            is_magenta = (h > 0.79 or h < 0.03) and s > 0.28 and r > 110 and b > 105 and g < 150
            if r > 200 and b > 180 and g < 120:
                px[x, y] = (r, g, b, 0)
            elif is_magenta:
                px[x, y] = (r, g, b, 0)
    return rgba


def crop_alpha(img: Image.Image, padding: int = 4) -> Image.Image:
    bbox = img.getchannel("A").getbbox()
    if bbox is None:
        return img
    x0 = max(0, bbox[0] - padding)
    y0 = max(0, bbox[1] - padding)
    x1 = min(img.width, bbox[2] + padding)
    y1 = min(img.height, bbox[3] + padding)
    return img.crop((x0, y0, x1, y1))


def fit_center(img: Image.Image, size: tuple[int, int], scale_max=0.82, alpha=1.0) -> Image.Image:
    out = Image.new("RGBA", size, (0, 0, 0, 0))
    tmp = img.copy().convert("RGBA")
    tmp.thumbnail((int(size[0] * scale_max), int(size[1] * scale_max)), Image.Resampling.LANCZOS)
    if alpha != 1.0:
        tmp.putalpha(tmp.getchannel("A").point(lambda a: int(a * alpha)))
    out.alpha_composite(tmp, ((size[0] - tmp.width) // 2, (size[1] - tmp.height) // 2))
    return out


def quiet_overlay(source: Image.Image, rect, size=(128, 128), scale=0.65, alpha=0.72) -> Image.Image:
    img = crop_alpha(remove_magenta(crop(source, rect)), 2)
    img = ImageEnhance.Contrast(img).enhance(0.82)
    return fit_center(img, size, scale, alpha)


def ground(source: Image.Image) -> Image.Image:
    img = crop(source, (183, 15, 349, 170)).convert("RGB").resize((128, 128), Image.Resampling.LANCZOS)
    img = ImageEnhance.Contrast(img).enhance(0.58)
    img = ImageEnhance.Color(img).enhance(0.82)
    return img.filter(ImageFilter.GaussianBlur(0.6))


def aul_ground(source: Image.Image) -> Image.Image:
    cleaned = crop_alpha(remove_magenta(crop(source, (5, 705, 370, 1018))), 0)
    tile = fit_center(cleaned, (256, 256), 0.92, 1.0)
    bg = Image.new("RGBA", (256, 256), (181, 124, 63, 255))
    bg.alpha_composite(tile)
    img = bg.convert("RGB")
    img = ImageEnhance.Contrast(img).enhance(0.74)
    img = ImageEnhance.Color(img).enhance(0.84)
    return img


def road_texture(source: Image.Image, rect: tuple[int, int, int, int]) -> Image.Image:
    cleaned = remove_magenta(crop(source, rect))
    component = crop_alpha(cleaned, 0)
    if component.getchannel("A").getbbox() is None or component.width < 8 or component.height < 8:
        component = cleaned
    # Fill only the accepted road component bbox; the alpha mask defines the reusable runtime shape.
    base = Image.new("RGBA", component.size, (103, 70, 47, 255))
    base.alpha_composite(component)
    tex = base.resize((128, 128), Image.Resampling.BICUBIC)
    tex = ImageEnhance.Contrast(tex).enhance(1.18)
    tex = ImageEnhance.Color(tex).enhance(1.04)
    return tex


def road_mask(kind: str) -> Image.Image:
    mask = Image.new("L", (128, 128), 0)
    d = ImageDraw.Draw(mask)
    w = 46
    if kind == "ns":
        d.rounded_rectangle((64 - w // 2, -8, 64 + w // 2, 136), radius=18, fill=255)
    elif kind == "ew":
        d.rounded_rectangle((-8, 64 - w // 2, 136, 64 + w // 2), radius=18, fill=255)
    else:
        paths = {
            "ne": [(64, -10), (64, 64), (138, 64)],
            "es": [(138, 64), (64, 64), (64, 138)],
            "sw": [(64, 138), (64, 64), (-10, 64)],
            "wn": [(-10, 64), (64, 64), (64, -10)],
        }
        d.line(paths[kind], fill=255, width=w, joint="curve")
        d.ellipse((64 - w // 2, 64 - w // 2, 64 + w // 2, 64 + w // 2), fill=255)
    return mask.filter(ImageFilter.GaussianBlur(0.7))


def road(source: Image.Image, kind: str) -> Image.Image:
    tex = road_texture(source, (1100, 25, 1162, 168))
    if kind == "ew":
        tex = tex.rotate(90)
    elif kind in {"ne", "es", "sw", "wn"}:
        tex = ImageEnhance.Contrast(tex).enhance(0.98)
    tex.putalpha(road_mask(kind))
    return tex


def buffer_edge(source: Image.Image, rect: tuple[int, int, int, int], alpha=0.75) -> Image.Image:
    img = crop_alpha(remove_magenta(crop(source, rect)), 0)
    out = Image.new("RGBA", (128, 128), (0, 0, 0, 0))
    img.thumbnail((116, 34), Image.Resampling.LANCZOS)
    img.putalpha(img.getchannel("A").point(lambda a: int(a * alpha)))
    out.alpha_composite(img, ((128 - img.width) // 2, (128 - img.height) // 2 + 8))
    return out


def fire(source: Image.Image) -> Image.Image:
    img = crop_alpha(remove_magenta(crop(source, (1058, 890, 1188, 1015))), 0)
    return fit_center(img, (128, 128), 0.62, 1.0)


def make_candidate(source: Image.Image, path: str) -> Image.Image | None:
    makers = {
        "ground/ground_sand_base_01.png": lambda: ground(source),
        "decor/decor_dune_01.png": lambda: quiet_overlay(source, (548, 30, 725, 150), scale=0.58, alpha=0.68),
        "decor/decor_stones_01.png": lambda: quiet_overlay(source, (595, 174, 720, 252), scale=0.5, alpha=0.62),
        "decor/decor_dry_grass_01.png": lambda: quiet_overlay(source, (760, 215, 915, 302), scale=0.55, alpha=0.58),
        "decor/decor_tracks_01.png": lambda: quiet_overlay(source, (236, 352, 420, 450), scale=0.62, alpha=0.55),
        "decor/decor_cracks_01.png": lambda: quiet_overlay(source, (835, 365, 958, 455), scale=0.66, alpha=0.58),
        "road/road_straight_ns.png": lambda: road(source, "ns"),
        "road/road_straight_ew.png": lambda: road(source, "ew"),
        "road/road_corner_ne.png": lambda: road(source, "ne"),
        "road/road_corner_es.png": lambda: road(source, "es"),
        "road/road_corner_sw.png": lambda: road(source, "sw"),
        "road/road_corner_wn.png": lambda: road(source, "wn"),
        "road/buffer_edge_stones_01.png": lambda: buffer_edge(source, (8, 505, 245, 555), 0.7),
        "road/buffer_packed_sand_01.png": lambda: buffer_edge(source, (245, 505, 520, 558), 0.62),
        "aul/aul_ground_2x2.png": lambda: aul_ground(source),
        "aul/aul_fire_01.png": lambda: fire(source),
    }
    return makers[path]()


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
    sheet = Image.new("RGBA", (1180, 54 + len(ASSETS) * row_h), (238, 229, 207, 255))
    d = ImageDraw.Draw(sheet, "RGBA")
    d.text((16, 14), "Pass 13 slicing proposal V2: current raw vs generated candidate (concept only)", fill=(30, 27, 23, 255))
    d.text((20, 38), "runtime path", fill=(30, 27, 23, 255))
    d.text((318, 38), "current raw", fill=(30, 27, 23, 255))
    d.text((476, 38), "proposed v2", fill=(30, 27, 23, 255))
    d.text((644, 38), "source/reuse/risk", fill=(30, 27, 23, 255))
    for i, asset in enumerate(ASSETS):
        y = 54 + i * row_h
        d.line((16, y, 1162, y), fill=(185, 171, 139, 255), width=1)
        d.text((20, y + 18), asset["path"], fill=(30, 27, 23, 255))
        d.text((20, y + 42), f"{asset['size'][0]}x{asset['size'][1]} {asset['mode']}", fill=(85, 76, 62, 255))
        sheet.alpha_composite(preview(RAW / asset["path"]), (316, y + 14))
        sheet.alpha_composite(preview(PROPOSED / asset["path"]), (482, y + 14))
        d.text((644, y + 16), asset["role"], fill=(30, 27, 23, 255))
        d.text((644, y + 42), asset["source"][:72], fill=(85, 76, 62, 255))
        d.text((644, y + 68), f"risk: {asset['risk']}", fill=(125, 70, 44, 255))
    sheet.save(OUT / "pass_13_world_foundation_slicing_proposal_contact_v2.png")


def write_docs() -> None:
    lines = [
        "# Pass 13 World Foundation Reuse Map V2",
        "",
        "Status: slicing proposal only. Runtime raw overwrite is not approved.",
        "",
        "## Component Inventory",
        "",
        "- ground sand base",
        "- decor overlays: dune, stones, dry_grass, tracks, cracks",
        "- road material: straight/corner pieces with consistent mask width",
        "- road buffer: stones and packed edge",
        "- aul core: packed earth and small fire",
        "",
        "## Existing Generated Sources To Reuse",
        "",
        "- `pass_13_world_foundation_source.png`: accepted generated bitmap world material family source",
        "",
        "## New Generated Source Needed",
        "",
        "No. V2 reuses the accepted Pass 13 source sheet only.",
        "",
        "## Runtime Files Touched",
        "",
        "None. V2 writes only to `proposed_runtime_v2/` for GDD review.",
        "",
        "## Reuse Map",
        "",
    ]
    for asset in ASSETS:
        if asset["path"].startswith("road/road_"):
            reuse_operation = "accepted road material crop reused with rotation where needed plus shared 46px technical alpha mask for width/alignment proof"
        else:
            reuse_operation = "accepted source crop plus cleanup/downscale; no new generation"
        lines.extend(
            [
                f"### `{asset['path']}`",
                "",
                f"- source crop / component reference: {asset['source']}",
                f"- reuse operation: {reuse_operation}",
                f"- proposed concept export: `proposed_runtime_v2/{asset['path']}`",
                f"- size/mode: `{asset['size'][0]}x{asset['size'][1]} {asset['mode']}`",
                f"- role: {asset['role']}",
                f"- risk: {asset['risk']}",
                "- new generation: no",
                "",
            ]
        )
    lines.extend(
        [
            "## V2 Notes",
            "",
            "- Road family uses generated road material as visible texture and technical masks only for consistent width/alignment.",
            "- Decor candidates were reduced in scale/alpha to avoid active-object read.",
            "- Fire candidate avoids the rack/structure and keeps only the small campfire area.",
            "- Deferred assets remain deferred: bones, stakes, cart marks, yurts, current highlight.",
        ]
    )
    (OUT / "pass_13_world_foundation_reuse_map_v2.md").write_text("\n".join(lines), encoding="utf-8")


def validate() -> None:
    for asset in ASSETS:
        path = PROPOSED / asset["path"]
        with Image.open(path) as img:
            if img.size != asset["size"]:
                raise SystemExit(f"{asset['path']}: expected {asset['size']}, got {img.size}")
            if img.mode != asset["mode"]:
                raise SystemExit(f"{asset['path']}: expected {asset['mode']}, got {img.mode}")
            if img.mode == "RGBA" and img.getchannel("A").getbbox() is None:
                raise SystemExit(f"{asset['path']}: empty alpha")


def main() -> None:
    PROPOSED.mkdir(parents=True, exist_ok=True)
    source = Image.open(SOURCE).convert("RGBA")
    for asset in ASSETS:
        out_path = PROPOSED / asset["path"]
        out_path.parent.mkdir(parents=True, exist_ok=True)
        make_candidate(source, asset["path"]).save(out_path)
    validate()
    contact_sheet()
    write_docs()
    print((OUT / "pass_13_world_foundation_slicing_proposal_contact_v2.png").relative_to(ROOT).as_posix())
    print((OUT / "pass_13_world_foundation_reuse_map_v2.md").relative_to(ROOT).as_posix())
    print(PROPOSED.relative_to(ROOT).as_posix())


if __name__ == "__main__":
    main()
