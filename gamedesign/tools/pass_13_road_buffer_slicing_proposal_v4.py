from pathlib import Path

import colorsys
from PIL import Image, ImageDraw, ImageEnhance, ImageFilter


ROOT = Path(__file__).resolve().parents[2]
RAW = ROOT / "games" / "turkic-jam-2026" / "raw"
OUT = ROOT / "gamedesign" / "assets" / "concept" / "pass_13_generated_world_foundation"
SOURCE = OUT / "pass_13_road_buffer_source_v3.png"
PROPOSED = OUT / "proposed_runtime_v4" / "road"

TILE = 128
ROAD_W = 54
EDGE_W = 74


ASSETS = [
    ("road_straight_ns.png", "road/road_straight_ns.png", "top center + bottom center"),
    ("road_straight_ew.png", "road/road_straight_ew.png", "left center + right center"),
    ("road_corner_ne.png", "road/road_corner_ne.png", "top center + right center"),
    ("road_corner_es.png", "road/road_corner_es.png", "right center + bottom center"),
    ("road_corner_sw.png", "road/road_corner_sw.png", "bottom center + left center"),
    ("road_corner_wn.png", "road/road_corner_wn.png", "left center + top center"),
    ("buffer_edge_stones_01.png", "road/buffer_edge_stones_01.png", "sparse no-build stone edge"),
    ("buffer_packed_sand_01.png", "road/buffer_packed_sand_01.png", "quiet no-build packed sand edge"),
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
            key_hue = (h > 0.76 or h < 0.03) and s > 0.22 and r > 105 and b > 95 and g < 170
            strong_key = r > 185 and b > 150 and g < 150
            red_fringe = r > 130 and b > 75 and g < 95 and r > g + 38 and b > g + 25
            if key_hue or strong_key or red_fringe:
                px[x, y] = (r, g, b, 0)
            elif b > g + 24 and r > g + 34:
                px[x, y] = (r, g, max(24, int(b * 0.42)), a)
    return rgba


def crop_alpha(img: Image.Image, padding: int = 0) -> Image.Image:
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


def flatten_for_mask(img: Image.Image, fill: tuple[int, int, int]) -> Image.Image:
    base = Image.new("RGBA", img.size, (*fill, 255))
    base.alpha_composite(img)
    return base


def make_material(source: Image.Image) -> Image.Image:
    body = remove_magenta(crop(source, (138, 88, 250, 590)))
    body = crop_alpha(body, 0)
    body = flatten_for_mask(body, (116, 72, 42))
    tex = body.resize((TILE, TILE), Image.Resampling.BICUBIC)
    tex = ImageEnhance.Contrast(tex).enhance(1.08)
    tex = ImageEnhance.Color(tex).enhance(0.9)
    return tex


def make_edge_material(source: Image.Image) -> Image.Image:
    edge = remove_magenta(crop(source, (118, 86, 274, 590)))
    edge = crop_alpha(edge, 0)
    edge = flatten_for_mask(edge, (181, 125, 65))
    tex = edge.resize((TILE, TILE), Image.Resampling.BICUBIC)
    tex = ImageEnhance.Contrast(tex).enhance(0.95)
    tex = ImageEnhance.Color(tex).enhance(0.84)
    return tex


def draw_road_mask(kind: str, width: int) -> Image.Image:
    mask = Image.new("L", (TILE, TILE), 0)
    d = ImageDraw.Draw(mask)
    c = TILE // 2
    radius = width // 2
    if kind == "ns":
        d.rectangle((c - radius, -4, c + radius, TILE + 4), fill=255)
    elif kind == "ew":
        d.rectangle((-4, c - radius, TILE + 4, c + radius), fill=255)
    else:
        points = {
            "ne": [(c, -4), (c, c), (TILE + 4, c)],
            "es": [(TILE + 4, c), (c, c), (c, TILE + 4)],
            "sw": [(c, TILE + 4), (c, c), (-4, c)],
            "wn": [(-4, c), (c, c), (c, -4)],
        }
        d.line(points[kind], fill=255, width=width, joint="curve")
        d.ellipse((c - radius, c - radius, c + radius, c + radius), fill=255)
    return mask


def road_tile(source: Image.Image, kind: str) -> Image.Image:
    body = make_material(source)
    edge = make_edge_material(source)
    edge_mask = draw_road_mask(kind, EDGE_W).filter(ImageFilter.GaussianBlur(1.1))
    body_mask = draw_road_mask(kind, ROAD_W).filter(ImageFilter.GaussianBlur(0.25))
    out = Image.new("RGBA", (TILE, TILE), (0, 0, 0, 0))
    edge.putalpha(edge_mask.point(lambda a: int(a * 0.42)))
    body.putalpha(body_mask)
    out.alpha_composite(edge)
    out.alpha_composite(body)
    return out


def fit_buffer(img: Image.Image, max_size: tuple[int, int], alpha: float) -> Image.Image:
    out = Image.new("RGBA", (TILE, TILE), (0, 0, 0, 0))
    component = crop_alpha(img, 0)
    component.thumbnail(max_size, Image.Resampling.LANCZOS)
    component = ImageEnhance.Contrast(component).enhance(0.72)
    component = ImageEnhance.Color(component).enhance(0.78)
    component.putalpha(component.getchannel("A").point(lambda a: int(a * alpha)))
    out.alpha_composite(component, ((TILE - component.width) // 2, (TILE - component.height) // 2))
    return out


def buffer_tile(source: Image.Image, kind: str) -> Image.Image:
    if kind == "stones":
        img = remove_magenta(crop(source, (120, 820, 445, 930)))
        return fit_buffer(img, (96, 28), 0.55)
    img = remove_magenta(crop(source, (1140, 835, 1455, 940)))
    return fit_buffer(img, (94, 30), 0.42)


def make_candidate(source: Image.Image, file_name: str) -> Image.Image:
    makers = {
        "road_straight_ns.png": lambda: road_tile(source, "ns"),
        "road_straight_ew.png": lambda: road_tile(source, "ew"),
        "road_corner_ne.png": lambda: road_tile(source, "ne"),
        "road_corner_es.png": lambda: road_tile(source, "es"),
        "road_corner_sw.png": lambda: road_tile(source, "sw"),
        "road_corner_wn.png": lambda: road_tile(source, "wn"),
        "buffer_edge_stones_01.png": lambda: buffer_tile(source, "stones"),
        "buffer_packed_sand_01.png": lambda: buffer_tile(source, "sand"),
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
    d.text((16, 14), "Pass 13 road/buffer proposal V4: current raw vs generated bitmap + technical mask candidate", fill=(30, 27, 23, 255))
    d.text((18, 40), "runtime path", fill=(30, 27, 23, 255))
    d.text((318, 40), "current raw", fill=(30, 27, 23, 255))
    d.text((484, 40), "proposed v4", fill=(30, 27, 23, 255))
    d.text((650, 40), "source / label / risk", fill=(30, 27, 23, 255))
    for i, (file_name, raw_path, exits) in enumerate(ASSETS):
        y = 58 + i * row_h
        d.line((16, y, 1144, y), fill=(185, 171, 139, 255), width=1)
        d.text((20, y + 18), raw_path, fill=(30, 27, 23, 255))
        d.text((20, y + 42), "128x128 RGBA", fill=(85, 76, 62, 255))
        sheet.alpha_composite(preview(RAW / raw_path), (316, y + 10))
        sheet.alpha_composite(preview(PROPOSED / file_name), (482, y + 10))
        d.text((650, y + 16), exits, fill=(30, 27, 23, 255))
        d.text((650, y + 44), "generated_bitmap_slice + technical_mask_only geometry", fill=(85, 76, 62, 255))
        d.text((650, y + 72), "risk: needs runtime road-loop screenshot proof", fill=(125, 70, 44, 255))
    sheet.save(OUT / "pass_13_road_buffer_contact_v4.png")


def grid_preview() -> None:
    order = [
        ["road_corner_es.png", "road_straight_ew.png", "road_corner_sw.png"],
        ["road_straight_ns.png", None, "road_straight_ns.png"],
        ["road_corner_ne.png", "road_straight_ew.png", "road_corner_wn.png"],
    ]
    cell = 128
    pad = 32
    sheet = Image.new("RGBA", (cell * 3 + pad * 2, cell * 3 + pad * 2 + 36), (238, 229, 207, 255))
    d = ImageDraw.Draw(sheet, "RGBA")
    d.text((pad, 10), "Pass 13 V4 road closed-loop grid preview: centerlines align at 64px, body width 54px", fill=(30, 27, 23, 255))
    bg_tile = Image.new("RGBA", (cell, cell), (179, 126, 70, 255))
    for y, row in enumerate(order):
        for x, name in enumerate(row):
            px = pad + x * cell
            py = pad + 36 + y * cell
            sheet.alpha_composite(bg_tile, (px, py))
            d.rectangle((px, py, px + cell, py + cell), outline=(115, 87, 52, 180), width=1)
            d.line((px + 64, py, px + 64, py + cell), fill=(20, 20, 20, 35), width=1)
            d.line((px, py + 64, px + cell, py + 64), fill=(20, 20, 20, 35), width=1)
            if name:
                sheet.alpha_composite(Image.open(PROPOSED / name).convert("RGBA"), (px, py))
    sheet.save(OUT / "pass_13_road_buffer_grid_preview_v4.png")


def write_reuse_map() -> None:
    lines = [
        "# Pass 13 Road/Buffer Reuse Map V4",
        "",
        "Status: proposal-only. No runtime raw overwrite.",
        "",
        "## Production Inventory",
        "",
        "- visible production-facing asset: yes, road/buffer runtime candidates",
        "- visible material source: `pass_13_road_buffer_source_v3.png` labeled `generated_bitmap_source`",
        "- new source sheet needed: no new full generation for V4; V4 reuses the partially accepted V3 road/buffer source",
        "- runtime assets to slice later after GDD approval: six `road_*` files and two `buffer_*` files",
        "- guides/masks: shared road centerline/body masks labeled `technical_mask_only`",
        "- final-looking proposal files: `proposed_runtime_v4/road/*.png` labeled `generated_bitmap_slice` until GDD approves raw copy",
        "",
        "## Runtime Files Touched",
        "",
        "None. V4 writes only to `proposed_runtime_v4/road/` for review.",
        "",
        "## Reuse Map",
        "",
    ]
    for file_name, raw_path, exits in ASSETS:
        if file_name.startswith("road_"):
            source_ref = "generated road material crops from V3 source, visible fill; shared 54px body / 74px soft edge technical masks"
        elif file_name == "buffer_edge_stones_01.png":
            source_ref = "generated sparse stone buffer crop, reduced alpha/contrast"
        else:
            source_ref = "generated packed-sand buffer crop, reduced alpha/contrast"
        lines.extend(
            [
                f"### `{raw_path}`",
                "",
                f"- proposed concept export: `proposed_runtime_v4/road/{file_name}`",
                "- label: `generated_bitmap_slice`",
                f"- visible source: {source_ref}",
                f"- geometry/guide: {exits}",
                "- technical mask: `technical_mask_only`, used only for alignment/width",
                "- size/mode: `128x128 RGBA`",
                "- new per-asset generation: no",
                "- raw overwrite: no",
                "",
            ]
        )
    lines.extend(
        [
            "## V4 Grid Proof",
            "",
            "- `pass_13_road_buffer_grid_preview_v4.png` shows a 3x3 closed-loop segment.",
            "- All road exits use centerline `64px` and body width `54px`.",
            "- Straight and corner exits intentionally extend to tile edges.",
            "",
            "## Risks",
            "",
            "- Runtime zoom may still require lowering edge contrast.",
            "- Buffer strips may need alternate placement rules if repeated every road-buffer cell.",
            "- V4 remains proposal-only until GDD approves raw copy and Code screenshot QA validates the loop.",
        ]
    )
    (OUT / "pass_13_road_buffer_reuse_map_v4.md").write_text("\n".join(lines), encoding="utf-8")


def validate() -> None:
    for file_name, _raw_path, _exits in ASSETS:
        path = PROPOSED / file_name
        with Image.open(path) as img:
            if img.size != (128, 128):
                raise SystemExit(f"{file_name}: expected 128x128, got {img.size}")
            if img.mode != "RGBA":
                raise SystemExit(f"{file_name}: expected RGBA, got {img.mode}")
            if img.getchannel("A").getbbox() is None:
                raise SystemExit(f"{file_name}: empty alpha")


def main() -> None:
    PROPOSED.mkdir(parents=True, exist_ok=True)
    source = Image.open(SOURCE).convert("RGBA")
    for file_name, _raw_path, _exits in ASSETS:
        make_candidate(source, file_name).save(PROPOSED / file_name)
    validate()
    contact_sheet()
    grid_preview()
    write_reuse_map()
    print((OUT / "pass_13_road_buffer_contact_v4.png").relative_to(ROOT).as_posix())
    print((OUT / "pass_13_road_buffer_grid_preview_v4.png").relative_to(ROOT).as_posix())
    print((OUT / "pass_13_road_buffer_reuse_map_v4.md").relative_to(ROOT).as_posix())
    print(PROPOSED.relative_to(ROOT).as_posix())


if __name__ == "__main__":
    main()
