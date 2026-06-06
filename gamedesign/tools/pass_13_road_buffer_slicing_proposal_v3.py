from pathlib import Path
from typing import Literal

import colorsys
from PIL import Image, ImageDraw, ImageEnhance, ImageFilter


ROOT = Path(__file__).resolve().parents[2]
RAW = ROOT / "games" / "turkic-jam-2026" / "raw"
OUT = ROOT / "gamedesign" / "assets" / "concept" / "pass_13_generated_world_foundation"
SOURCE = OUT / "pass_13_road_buffer_source_v3.png"
PROPOSED = OUT / "proposed_runtime_v3" / "road"

Mode = Literal["RGBA"]


ASSETS: list[dict] = [
    {
        "path": "road/road_straight_ns.png",
        "file": "road_straight_ns.png",
        "source": "V3 road source vertical road crop (64, 64, 300, 620)",
        "reuse": "generated vertical road component, fit to 128 tile with shared 50px road body target",
        "risk": "road-width mismatch / edge halo",
    },
    {
        "path": "road/road_straight_ew.png",
        "file": "road_straight_ew.png",
        "source": "V3 road source horizontal road crop (395, 190, 815, 378)",
        "reuse": "generated horizontal road component, fit to 128 tile with same road body target",
        "risk": "road-width mismatch / edge halo",
    },
    {
        "path": "road/road_corner_ne.png",
        "file": "road_corner_ne.png",
        "source": "V3 road source corner crop (870, 125, 1160, 392)",
        "reuse": "generated corner component, cleaned and fit to 128 tile",
        "risk": "corner connection / edge halo",
    },
    {
        "path": "road/road_corner_es.png",
        "file": "road_corner_es.png",
        "source": "V3 road source corner crop (1215, 125, 1500, 392)",
        "reuse": "generated corner component, cleaned and fit to 128 tile",
        "risk": "corner connection / edge halo",
    },
    {
        "path": "road/road_corner_sw.png",
        "file": "road_corner_sw.png",
        "source": "V3 road source corner crop (500, 445, 755, 710)",
        "reuse": "generated corner component, cleaned and fit to 128 tile",
        "risk": "corner connection / edge halo",
    },
    {
        "path": "road/road_corner_wn.png",
        "file": "road_corner_wn.png",
        "source": "V3 road source corner crop (855, 445, 1115, 705)",
        "reuse": "generated corner component, cleaned and fit to 128 tile",
        "risk": "corner connection / edge halo",
    },
    {
        "path": "road/buffer_edge_stones_01.png",
        "file": "buffer_edge_stones_01.png",
        "source": "V3 buffer source sparse stones crop (60, 805, 515, 942)",
        "reuse": "generated stone buffer strip, reduced contrast/alpha and centered as no-build edge overlay",
        "risk": "wall-read / repeated line",
    },
    {
        "path": "road/buffer_packed_sand_01.png",
        "file": "buffer_packed_sand_01.png",
        "source": "V3 buffer source packed sand crop (1110, 810, 1485, 945)",
        "reuse": "generated dusty packed edge, low alpha and shorter than road",
        "risk": "road-read / repeated strip",
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
            is_magenta = (h > 0.78 or h < 0.03) and s > 0.32 and r > 120 and b > 110 and g < 170
            strong_key = r > 200 and b > 180 and g < 145
            fringe_key = r > 150 and b > 125 and g < 120
            if strong_key or is_magenta or fringe_key:
                px[x, y] = (r, g, b, 0)
            elif r > b and b > g and b > 80:
                px[x, y] = (r, g, max(28, int(b * 0.35)), a)
    return rgba


def crop_alpha(img: Image.Image, padding: int = 4) -> Image.Image:
    bbox = img.getchannel("A").getbbox()
    if bbox is None:
        return img
    return img.crop(
        (
            max(0, bbox[0] - padding),
            max(0, bbox[1] - padding),
            min(img.width, bbox[2] + padding),
            min(img.height, bbox[3] + padding),
        )
    )


def fit_component(img: Image.Image, size: tuple[int, int], max_size: tuple[int, int], alpha=1.0) -> Image.Image:
    out = Image.new("RGBA", size, (0, 0, 0, 0))
    component = crop_alpha(img, 2)
    component.thumbnail(max_size, Image.Resampling.LANCZOS)
    component = ImageEnhance.Contrast(component).enhance(0.98)
    component = ImageEnhance.Color(component).enhance(0.96)
    if alpha != 1.0:
        component.putalpha(component.getchannel("A").point(lambda a: int(a * alpha)))
    out.alpha_composite(component, ((size[0] - component.width) // 2, (size[1] - component.height) // 2))
    return out


def road_from_crop(source: Image.Image, rect: tuple[int, int, int, int], max_size: tuple[int, int]) -> Image.Image:
    img = remove_magenta(crop(source, rect))
    out = fit_component(img, (128, 128), max_size)
    out = out.filter(ImageFilter.UnsharpMask(radius=0.6, percent=70, threshold=3))
    return out


def buffer_from_crop(
    source: Image.Image, rect: tuple[int, int, int, int], max_size: tuple[int, int], alpha: float
) -> Image.Image:
    img = remove_magenta(crop(source, rect))
    out = fit_component(img, (128, 128), max_size, alpha)
    return ImageEnhance.Contrast(out).enhance(0.88)


def make_candidate(source: Image.Image, file_name: str) -> Image.Image:
    makers = {
        "road_straight_ns.png": lambda: road_from_crop(source, (64, 64, 300, 620), (76, 128)),
        "road_straight_ew.png": lambda: road_from_crop(source, (395, 190, 815, 378), (128, 76)),
        "road_corner_ne.png": lambda: road_from_crop(source, (870, 125, 1160, 392), (112, 112)),
        "road_corner_es.png": lambda: road_from_crop(source, (1215, 125, 1500, 392), (112, 112)),
        "road_corner_sw.png": lambda: road_from_crop(source, (500, 445, 755, 710), (112, 112)),
        "road_corner_wn.png": lambda: road_from_crop(source, (855, 445, 1115, 705), (112, 112)),
        "buffer_edge_stones_01.png": lambda: buffer_from_crop(source, (60, 805, 515, 942), (112, 42), 0.72),
        "buffer_packed_sand_01.png": lambda: buffer_from_crop(source, (1110, 810, 1485, 945), (108, 38), 0.58),
    }
    return makers[file_name]()


def checker(size: tuple[int, int]) -> Image.Image:
    img = Image.new("RGBA", size, (35, 36, 33, 255))
    d = ImageDraw.Draw(img)
    for y in range(0, size[1], 8):
        for x in range(0, size[0], 8):
            if (x // 8 + y // 8) % 2:
                d.rectangle((x, y, x + 7, y + 7), fill=(45, 45, 41, 255))
    return img


def preview(path: Path, size=(104, 104)) -> Image.Image:
    img = Image.open(path).convert("RGBA")
    bg = checker(size)
    tmp = img.copy()
    tmp.thumbnail((size[0] - 8, size[1] - 8), Image.Resampling.LANCZOS)
    bg.alpha_composite(tmp, ((size[0] - tmp.width) // 2, (size[1] - tmp.height) // 2))
    return bg


def contact_sheet() -> None:
    row_h = 122
    sheet = Image.new("RGBA", (1160, 58 + len(ASSETS) * row_h), (238, 229, 207, 255))
    d = ImageDraw.Draw(sheet, "RGBA")
    d.text((16, 14), "Pass 13 road/buffer proposal V3: current raw vs generated bitmap candidate", fill=(30, 27, 23, 255))
    d.text((18, 40), "runtime path", fill=(30, 27, 23, 255))
    d.text((318, 40), "current raw", fill=(30, 27, 23, 255))
    d.text((484, 40), "proposed v3", fill=(30, 27, 23, 255))
    d.text((650, 40), "reuse / risk", fill=(30, 27, 23, 255))
    for i, asset in enumerate(ASSETS):
        y = 58 + i * row_h
        d.line((16, y, 1144, y), fill=(185, 171, 139, 255), width=1)
        d.text((20, y + 18), asset["path"], fill=(30, 27, 23, 255))
        d.text((20, y + 42), "128x128 RGBA", fill=(85, 76, 62, 255))
        sheet.alpha_composite(preview(RAW / asset["path"]), (316, y + 10))
        sheet.alpha_composite(preview(PROPOSED / asset["file"]), (482, y + 10))
        d.text((650, y + 16), asset["reuse"][:78], fill=(30, 27, 23, 255))
        d.text((650, y + 44), asset["source"][:78], fill=(85, 76, 62, 255))
        d.text((650, y + 72), f"risk: {asset['risk']}", fill=(125, 70, 44, 255))
    sheet.save(OUT / "pass_13_road_buffer_contact_v3.png")


def write_reuse_map() -> None:
    lines = [
        "# Pass 13 Road/Buffer Reuse Map V3",
        "",
        "Status: road/buffer slicing proposal only. Runtime raw overwrite is not approved.",
        "",
        "## Component Inventory",
        "",
        "- road material: straight NS/EW and four corners",
        "- road buffer: sparse stones edge and packed sand edge",
        "",
        "## Existing Generated Sources To Reuse",
        "",
        "- `pass_13_road_buffer_source_v3.png`: generated bitmap road/buffer source sheet for this rejected subfamily",
        "- accepted Pass 13 ground/decor/aul candidates are not touched",
        "",
        "## New Generated Source Needed",
        "",
        "Yes, one narrow road/buffer generated bitmap source sheet. No full world foundation regeneration.",
        "",
        "## Runtime Files Touched",
        "",
        "None. V3 writes only to `proposed_runtime_v3/road/` for GDD review.",
        "",
        "## Reuse Map",
        "",
    ]
    for asset in ASSETS:
        lines.extend(
            [
                f"### `{asset['path']}`",
                "",
                f"- source crop / component reference: {asset['source']}",
                f"- reuse operation: {asset['reuse']}",
                f"- proposed concept export: `proposed_runtime_v3/road/{asset['file']}`",
                "- size/mode: `128x128 RGBA`",
                f"- risk: {asset['risk']}",
                "- new generation: no per-asset one-off; all files reuse one V3 generated source sheet",
                "",
            ]
        )
    lines.extend(
        [
            "## V3 Notes",
            "",
            "- Road pieces come from visible generated bitmap road components, not procedural strips.",
            "- Crops are reduced into one 128x128 grid family; straight and corner files remain proposal-only.",
            "- Buffer files are intentionally lower-alpha and shorter than road pieces to avoid road/wall read.",
            "- No raw files are overwritten in this pass.",
        ]
    )
    (OUT / "pass_13_road_buffer_reuse_map_v3.md").write_text("\n".join(lines), encoding="utf-8")


def validate() -> None:
    for asset in ASSETS:
        path = PROPOSED / asset["file"]
        with Image.open(path) as img:
            if img.size != (128, 128):
                raise SystemExit(f"{asset['file']}: expected 128x128, got {img.size}")
            if img.mode != "RGBA":
                raise SystemExit(f"{asset['file']}: expected RGBA, got {img.mode}")
            if img.getchannel("A").getbbox() is None:
                raise SystemExit(f"{asset['file']}: empty alpha")


def main() -> None:
    PROPOSED.mkdir(parents=True, exist_ok=True)
    source = Image.open(SOURCE).convert("RGBA")
    for asset in ASSETS:
        make_candidate(source, asset["file"]).save(PROPOSED / asset["file"])
    validate()
    contact_sheet()
    write_reuse_map()
    print((OUT / "pass_13_road_buffer_contact_v3.png").relative_to(ROOT).as_posix())
    print((OUT / "pass_13_road_buffer_reuse_map_v3.md").relative_to(ROOT).as_posix())
    print(PROPOSED.relative_to(ROOT).as_posix())


if __name__ == "__main__":
    main()
