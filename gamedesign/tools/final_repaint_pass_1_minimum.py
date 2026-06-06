from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter


ROOT = Path(__file__).resolve().parents[2]
RAW = ROOT / "games" / "turkic-jam-2026" / "raw"
OUT = ROOT / "gamedesign" / "assets" / "concept" / "final_repaint_pass_1"

INK = (18, 15, 12, 255)
FELT_DARK = (18, 28, 31, 246)
FELT_DARK_2 = (8, 14, 17, 246)
EDGE_WOOD = (127, 87, 44, 255)
EDGE_GOLD = (221, 159, 64, 255)
PARCHMENT = (236, 213, 164, 255)
PARCHMENT_LIT = (250, 227, 174, 255)
TEAL = (44, 169, 157, 255)
TEAL_DARK = (19, 88, 88, 255)
RED = (156, 52, 38, 255)
GREEN = (133, 207, 94, 255)
GOLD = (245, 188, 72, 255)
BONE = (226, 209, 166, 255)
SAND_SHADOW = (92, 61, 34, 95)
WOOD = (92, 57, 33, 255)
DRY_LEAF = (165, 124, 45, 230)


def canvas(size: tuple[int, int], color=(0, 0, 0, 0), scale: int = 4) -> tuple[Image.Image, ImageDraw.ImageDraw]:
    img = Image.new("RGBA", (size[0] * scale, size[1] * scale), color)
    return img, ImageDraw.Draw(img, "RGBA")


def down(img: Image.Image, size: tuple[int, int]) -> Image.Image:
    return img.resize(size, Image.Resampling.LANCZOS)


def sc(v: int, scale: int = 4) -> int:
    return v * scale


def box(b: tuple[int, int, int, int], scale: int = 4) -> tuple[int, int, int, int]:
    return tuple(sc(v, scale) for v in b)


def line_points(points: list[tuple[int, int]], scale: int = 4) -> list[tuple[int, int]]:
    return [(sc(x, scale), sc(y, scale)) for x, y in points]


def tamga(d: ImageDraw.ImageDraw, cx: int, cy: int, s: int, color, width: int = 2, scale: int = 4) -> None:
    cx, cy, s, width = sc(cx, scale), sc(cy, scale), sc(s, scale), sc(width, scale)
    d.line((cx, cy - s, cx, cy + s), fill=color, width=width)
    d.line((cx - s, cy - s // 2, cx, cy - s, cx + s, cy - s // 2), fill=color, width=width)
    d.line((cx - s, cy + s // 2, cx, cy, cx + s, cy + s // 2), fill=color, width=width)


def corner_mark(d: ImageDraw.ImageDraw, x: int, y: int, sx: int, sy: int, color, scale: int = 4) -> None:
    pts = [(x, y + sy * 5), (x + sx * 5, y), (x + sx * 10, y + sy * 5)]
    d.line(line_points(pts, scale), fill=color, width=sc(1, scale))
    d.line(line_points([(x + sx * 5, y), (x + sx * 5, y + sy * 10)], scale), fill=color, width=sc(1, scale))


def felt_noise(d: ImageDraw.ImageDraw, size: tuple[int, int], light: bool = False, scale: int = 4) -> None:
    w, h = size
    line = (91, 132, 123, 24) if not light else (109, 76, 39, 22)
    dot = (218, 176, 96, 18) if not light else (92, 62, 35, 26)
    for y in range(10, h - 8, 9):
        d.line((sc(9, scale), sc(y, scale), sc(w - 10, scale), sc(y + (y % 3), scale)), fill=line, width=sc(1, scale))
    for i in range(54):
        x = 7 + (i * 19) % max(1, w - 14)
        y = 7 + (i * 29) % max(1, h - 14)
        d.ellipse(box((x, y, x + 1, y + 1), scale), fill=dot)


def save_runtime(img: Image.Image, rel: str, written: list[Path]) -> None:
    path = RAW / rel
    path.parent.mkdir(parents=True, exist_ok=True)
    img.save(path)
    OUT.mkdir(parents=True, exist_ok=True)
    img.save(OUT / path.name)
    written.append(path)


def ui_panel_dark() -> Image.Image:
    size = (96, 96)
    img, d = canvas(size)
    d.rounded_rectangle(box((2, 2, 93, 93)), radius=sc(9), fill=FELT_DARK_2, outline=(0, 0, 0, 170), width=sc(2))
    d.rounded_rectangle(box((5, 5, 90, 90)), radius=sc(7), fill=FELT_DARK, outline=EDGE_WOOD, width=sc(2))
    felt_noise(d, size)
    d.rounded_rectangle(box((24, 24, 72, 72)), radius=sc(3), fill=(17, 25, 27, 180), outline=(76, 93, 88, 48), width=sc(1))
    for x, y, sx, sy in [(11, 11, 1, 1), (85, 11, -1, 1), (11, 85, 1, -1), (85, 85, -1, -1)]:
        corner_mark(d, x, y, sx, sy, TEAL)
    for x in [17, 79]:
        d.line(line_points([(x, 10), (x, 20)], 4), fill=(207, 145, 60, 150), width=sc(1))
    return down(img, size)


def card(play_selected: bool) -> Image.Image:
    size = (96, 128)
    img, d = canvas(size)
    face = PARCHMENT_LIT if play_selected else PARCHMENT
    edge = TEAL_DARK if play_selected else (82, 55, 34, 255)
    accent = TEAL if play_selected else RED
    d.rounded_rectangle(box((2, 3, 93, 124)), radius=sc(10), fill=(0, 0, 0, 110))
    d.rounded_rectangle(box((5, 2, 90, 121)), radius=sc(8), fill=face, outline=INK, width=sc(2))
    d.rounded_rectangle(box((8, 5, 87, 118)), radius=sc(6), outline=edge, width=sc(3))
    for y in [15, 101]:
        d.rounded_rectangle(box((18, y, 78, y + 7)), radius=sc(2), fill=(176, 117, 55, 65))
    for x, y, sx, sy in [(14, 15, 1, 1), (82, 15, -1, 1), (14, 112, 1, -1), (82, 112, -1, -1)]:
        corner_mark(d, x, y, sx, sy, accent)
    d.polygon(line_points([(2, 19), (18, 2), (29, 2), (2, 30)]), fill=(117, 198, 82, 210) if not play_selected else (64, 212, 205, 220))
    d.line(line_points([(23, 21), (73, 21)]), fill=(98, 66, 38, 130), width=sc(1))
    d.line(line_points([(23, 106), (73, 106)]), fill=(98, 66, 38, 120), width=sc(1))
    if play_selected:
        d.rounded_rectangle(box((1, 1, 94, 126)), radius=sc(11), outline=(255, 226, 93, 235), width=sc(3))
        d.rounded_rectangle(box((11, 8, 84, 115)), radius=sc(6), outline=(83, 224, 212, 135), width=sc(2))
    return down(img, size)


def card_art_saxaul() -> Image.Image:
    size = (64, 64)
    img, d = canvas(size)
    d.ellipse(box((7, 43, 57, 55)), fill=SAND_SHADOW)
    branches = [(12, 42, 33, 25, 4), (19, 45, 50, 31, 4), (29, 43, 17, 29, 3), (35, 44, 55, 24, 3), (26, 44, 38, 22, 3)]
    for x0, y0, x1, y1, w in branches:
        d.line(line_points([(x0, y0), (x1, y1)]), fill=INK, width=sc(w + 1))
        d.line(line_points([(x0, y0), (x1, y1)]), fill=WOOD, width=sc(w))
    for x, y in [(10, 43), (20, 39), (33, 38), (46, 41)]:
        d.polygon(line_points([(x, y), (x + 5, y - 13), (x + 12, y - 1)]), fill=DRY_LEAF)
    return down(img, size)


def icon_stamina() -> Image.Image:
    size = (32, 32)
    img, d = canvas(size)
    d.ellipse(box((6, 8, 26, 28)), fill=(118, 37, 31, 255), outline=INK, width=sc(2))
    d.polygon(line_points([(16, 4), (25, 15), (18, 15), (20, 28), (8, 18), (15, 18)]), fill=GOLD)
    d.line(line_points([(16, 6), (16, 23)]), fill=(255, 229, 121, 160), width=sc(1))
    return down(img, size)


def icon_supplies() -> Image.Image:
    size = (32, 32)
    img, d = canvas(size)
    d.rounded_rectangle(box((7, 11, 25, 29)), radius=sc(5), fill=BONE, outline=INK, width=sc(2))
    d.arc(box((10, 3, 22, 18)), 180, 360, fill=BONE, width=sc(3))
    d.polygon(line_points([(10, 16), (22, 16), (20, 23), (12, 23)]), fill=(175, 111, 55, 210))
    d.line(line_points([(9, 18), (24, 18)]), fill=EDGE_WOOD, width=sc(1))
    return down(img, size)


def tile_saxaul() -> Image.Image:
    size = (128, 128)
    img, d = canvas(size)
    d.ellipse(box((19, 88, 109, 110)), fill=SAND_SHADOW)
    branches = [
        (25, 90, 68, 59, 6),
        (39, 95, 100, 72, 6),
        (53, 91, 32, 61, 5),
        (70, 92, 96, 53, 5),
        (62, 94, 74, 48, 4),
        (47, 94, 17, 75, 4),
        (82, 92, 112, 78, 4),
    ]
    for x0, y0, x1, y1, w in branches:
        d.line(line_points([(x0, y0), (x1, y1)]), fill=INK, width=sc(w + 1))
        d.line(line_points([(x0, y0), (x1, y1)]), fill=WOOD, width=sc(w))
    for x, y in [(26, 90), (43, 82), (61, 79), (79, 82), (96, 89)]:
        d.polygon(line_points([(x, y), (x + 8, y - 24), (x + 18, y - 1)]), fill=DRY_LEAF)
    for x, y in [(37, 74), (72, 70), (89, 65)]:
        d.line(line_points([(x, y), (x + 9, y - 12)]), fill=(204, 162, 61, 160), width=sc(2))
    return down(img, size)


def hero_idle() -> Image.Image:
    size = (128, 128)
    img, d = canvas(size)
    d.ellipse(box((41, 104, 88, 119)), fill=(0, 0, 0, 95))
    d.ellipse(box((51, 22, 77, 48)), fill=(204, 133, 77, 255), outline=INK, width=sc(2))
    d.polygon(line_points([(45, 42), (83, 42), (75, 63), (53, 63)]), fill=(44, 31, 24, 255), outline=INK)
    d.rounded_rectangle(box((43, 50, 85, 94)), radius=sc(9), fill=(43, 101, 95, 255), outline=INK, width=sc(2))
    d.line(line_points([(51, 66), (80, 66)]), fill=RED, width=sc(3))
    d.line(line_points([(61, 51), (72, 94)]), fill=TEAL, width=sc(3))
    d.polygon(line_points([(43, 56), (27, 81), (46, 86)]), fill=(79, 62, 45, 255), outline=INK)
    d.polygon(line_points([(85, 56), (101, 81), (82, 86)]), fill=(79, 62, 45, 255), outline=INK)
    d.line(line_points([(55, 94), (47, 115)]), fill=INK, width=sc(7))
    d.line(line_points([(74, 94), (82, 115)]), fill=INK, width=sc(7))
    d.line(line_points([(55, 94), (48, 114)]), fill=(53, 42, 34, 255), width=sc(5))
    d.line(line_points([(74, 94), (81, 114)]), fill=(53, 42, 34, 255), width=sc(5))
    d.line(line_points([(96, 48), (96, 112)]), fill=INK, width=sc(4))
    d.line(line_points([(96, 48), (96, 112)]), fill=(108, 67, 34, 255), width=sc(2))
    return down(img, size)


def contact_sheet(paths: list[Path]) -> None:
    cells = []
    for path in paths:
        img = Image.open(path).convert("RGBA")
        thumb = Image.new("RGBA", (128, 128), (27, 29, 25, 255))
        img.thumbnail((108, 108), Image.Resampling.LANCZOS)
        thumb.alpha_composite(img, ((128 - img.width) // 2, (128 - img.height) // 2))
        cells.append((path.name, thumb))
    sheet = Image.new("RGBA", (4 * 180, 2 * 160), (238, 229, 207, 255))
    d = ImageDraw.Draw(sheet, "RGBA")
    for i, (name, thumb) in enumerate(cells):
        x = (i % 4) * 180
        y = (i // 4) * 160
        sheet.alpha_composite(thumb, (x + 26, y + 8))
        d.text((x + 8, y + 139), name[:26], fill=(30, 27, 23, 255))
    OUT.mkdir(parents=True, exist_ok=True)
    sheet.save(OUT / "final_repaint_pass_1_contact_sheet.png")


def main() -> None:
    written: list[Path] = []
    save_runtime(ui_panel_dark(), "ui/ui_panel_felt_dark_96.png", written)
    save_runtime(card(False), "ui/ui_card_playable_96x128.png", written)
    save_runtime(card(True), "ui/ui_card_selected_96x128.png", written)
    save_runtime(card_art_saxaul(), "cards/card_art_saxaul_64.png", written)
    save_runtime(icon_stamina(), "icons/icon_stamina_32.png", written)
    save_runtime(icon_supplies(), "icons/icon_supplies_32.png", written)
    save_runtime(tile_saxaul(), "tiles/tile_saxaul_01.png", written)
    save_runtime(hero_idle(), "hero/hero_wayfarer_idle_s.png", written)
    contact_sheet(written)
    for path in written:
        print(path.relative_to(ROOT).as_posix())
    print((OUT / "final_repaint_pass_1_contact_sheet.png").relative_to(ROOT).as_posix())


if __name__ == "__main__":
    main()
