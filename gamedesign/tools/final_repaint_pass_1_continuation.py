from pathlib import Path

from PIL import Image, ImageDraw


ROOT = Path(__file__).resolve().parents[2]
RAW = ROOT / "games" / "turkic-jam-2026" / "raw"
OUT = ROOT / "gamedesign" / "assets" / "concept" / "final_repaint_pass_1"

S = 4
INK = (18, 15, 12, 255)
FELT_DARK = (18, 28, 31, 246)
FELT_DARK_2 = (8, 14, 17, 246)
PARCHMENT = (237, 215, 166, 255)
PARCHMENT_2 = (246, 226, 178, 255)
EDGE_WOOD = (127, 87, 44, 255)
EDGE_GOLD = (221, 159, 64, 255)
TEAL = (44, 169, 157, 255)
TEAL_DARK = (19, 88, 88, 255)
RED = (156, 52, 38, 255)
GREEN = (133, 207, 94, 255)
GOLD = (245, 188, 72, 255)
BONE = (226, 209, 166, 255)
WOOD = (92, 57, 33, 255)
SHADOW = (0, 0, 0, 88)


def sc(v: int) -> int:
    return v * S


def box(b: tuple[int, int, int, int]) -> tuple[int, int, int, int]:
    return tuple(sc(v) for v in b)


def pts(points: list[tuple[int, int]]) -> list[tuple[int, int]]:
    return [(sc(x), sc(y)) for x, y in points]


def canvas(size: tuple[int, int], fill=(0, 0, 0, 0)) -> tuple[Image.Image, ImageDraw.ImageDraw]:
    img = Image.new("RGBA", (size[0] * S, size[1] * S), fill)
    return img, ImageDraw.Draw(img, "RGBA")


def down(img: Image.Image, size: tuple[int, int]) -> Image.Image:
    return img.resize(size, Image.Resampling.LANCZOS)


def tamga(d: ImageDraw.ImageDraw, cx: int, cy: int, s: int, color=TEAL, width: int = 2) -> None:
    cx, cy, s, width = sc(cx), sc(cy), sc(s), sc(width)
    d.line((cx, cy - s, cx, cy + s), fill=color, width=width)
    d.line((cx - s, cy - s // 2, cx, cy - s, cx + s, cy - s // 2), fill=color, width=width)
    d.line((cx - s, cy + s // 2, cx, cy, cx + s, cy + s // 2), fill=color, width=width)


def corner_mark(d: ImageDraw.ImageDraw, x: int, y: int, sx: int, sy: int, color) -> None:
    d.line(pts([(x, y + sy * 5), (x + sx * 5, y), (x + sx * 10, y + sy * 5)]), fill=color, width=sc(1))
    d.line(pts([(x + sx * 5, y), (x + sx * 5, y + sy * 10)]), fill=color, width=sc(1))


def cloth_noise(d: ImageDraw.ImageDraw, size: tuple[int, int], light: bool) -> None:
    w, h = size
    line = (116, 79, 42, 22) if light else (91, 132, 123, 24)
    dot = (86, 56, 31, 28) if light else (218, 176, 96, 18)
    for y in range(9, h - 8, 9):
        d.line((sc(8), sc(y), sc(w - 9), sc(y + y % 3)), fill=line, width=sc(1))
    for i in range(46):
        x = 6 + (i * 17) % max(1, w - 12)
        y = 6 + (i * 31) % max(1, h - 12)
        d.ellipse(box((x, y, x + 1, y + 1)), fill=dot)


def ui_panel_light() -> Image.Image:
    size = (96, 96)
    img, d = canvas(size)
    d.rounded_rectangle(box((2, 2, 93, 93)), radius=sc(9), fill=(151, 98, 47, 180), outline=INK, width=sc(2))
    d.rounded_rectangle(box((5, 5, 90, 90)), radius=sc(7), fill=PARCHMENT, outline=EDGE_WOOD, width=sc(2))
    cloth_noise(d, size, True)
    d.rounded_rectangle(box((24, 24, 72, 72)), radius=sc(3), fill=(244, 223, 176, 170), outline=(143, 94, 48, 55), width=sc(1))
    for x, y, sx, sy in [(10, 10, 1, 1), (86, 10, -1, 1), (10, 86, 1, -1), (86, 86, -1, -1)]:
        corner_mark(d, x, y, sx, sy, RED)
    return down(img, size)


def ui_square(size: tuple[int, int], fill, accent, inner=True) -> Image.Image:
    img, d = canvas(size)
    w, h = size
    d.rounded_rectangle(box((2, 2, w - 3, h - 3)), radius=sc(8), fill=(0, 0, 0, 100))
    d.rounded_rectangle(box((5, 5, w - 6, h - 6)), radius=sc(7), fill=fill, outline=INK, width=sc(2))
    d.rounded_rectangle(box((9, 9, w - 10, h - 10)), radius=sc(5), outline=EDGE_WOOD, width=sc(2))
    for x, y, sx, sy in [(12, 12, 1, 1), (w - 12, 12, -1, 1), (12, h - 12, 1, -1), (w - 12, h - 12, -1, -1)]:
        corner_mark(d, x, y, sx, sy, accent)
    if inner:
        d.rounded_rectangle(box((19, 19, w - 20, h - 20)), radius=sc(5), fill=(12, 17, 18, 95), outline=(118, 91, 54, 80), width=sc(1))
    return down(img, size)


def card_back() -> Image.Image:
    size = (96, 128)
    img, d = canvas(size)
    d.rounded_rectangle(box((3, 3, 92, 124)), radius=sc(10), fill=FELT_DARK_2, outline=INK, width=sc(2))
    d.rounded_rectangle(box((7, 7, 88, 120)), radius=sc(7), fill=FELT_DARK, outline=EDGE_WOOD, width=sc(2))
    cloth_noise(d, size, False)
    d.rounded_rectangle(box((22, 26, 74, 102)), radius=sc(8), outline=(102, 74, 43, 190), width=sc(2))
    tamga(d, 48, 64, 22, TEAL, 4)
    tamga(d, 48, 64, 12, GOLD, 2)
    for x, y, sx, sy in [(13, 14, 1, 1), (83, 14, -1, 1), (13, 114, 1, -1), (83, 114, -1, -1)]:
        corner_mark(d, x, y, sx, sy, TEAL)
    return down(img, size)


def art_yurt() -> Image.Image:
    size = (64, 64)
    img, d = canvas(size)
    d.ellipse(box((8, 45, 56, 56)), fill=SHADOW)
    d.polygon(pts([(12, 35), (32, 14), (52, 35)]), fill=PARCHMENT_2, outline=INK)
    d.rounded_rectangle(box((11, 32, 53, 54)), radius=sc(8), fill=PARCHMENT, outline=EDGE_WOOD, width=sc(3))
    d.rectangle(box((27, 39, 37, 54)), fill=RED, outline=INK)
    d.line(pts([(16, 37), (48, 37)]), fill=TEAL, width=sc(3))
    tamga(d, 32, 25, 6, RED, 2)
    return down(img, size)


def art_tamga_stone() -> Image.Image:
    size = (64, 64)
    img, d = canvas(size)
    d.ellipse(box((16, 50, 50, 58)), fill=SHADOW)
    d.polygon(pts([(21, 53), (17, 20), (33, 10), (48, 22), (43, 54)]), fill=(142, 124, 91, 255), outline=INK)
    d.line(pts([(23, 24), (42, 23)]), fill=(195, 173, 123, 170), width=sc(2))
    tamga(d, 32, 32, 13, TEAL, 4)
    return down(img, size)


def art_wolf_track() -> Image.Image:
    size = (64, 64)
    img, d = canvas(size)
    d.line(pts([(10, 55), (55, 11)]), fill=(134, 52, 42, 120), width=sc(3))
    for x, y in [(20, 23), (40, 36), (25, 49)]:
        d.ellipse(box((x - 5, y - 3, x + 5, y + 7)), fill=(65, 45, 33, 235))
        for ox, oy in [(-7, -8), (0, -10), (7, -8)]:
            d.ellipse(box((x + ox - 3, y + oy - 3, x + ox + 3, y + oy + 3)), fill=(65, 45, 33, 235))
    return down(img, size)


def icon_wisdom() -> Image.Image:
    size = (32, 32)
    img, d = canvas(size)
    d.ellipse(box((6, 5, 26, 25)), outline=INK, width=sc(4))
    d.ellipse(box((8, 7, 24, 23)), outline=TEAL, width=sc(4))
    d.line(pts([(16, 8), (16, 26)]), fill=TEAL, width=sc(3))
    d.line(pts([(12, 14), (20, 14)]), fill=(133, 220, 210, 180), width=sc(2))
    return down(img, size)


def icon_glory() -> Image.Image:
    size = (32, 32)
    img, d = canvas(size)
    star = [(16, 3), (21, 12), (30, 13), (23, 20), (25, 29), (16, 24), (7, 29), (9, 20), (2, 13), (11, 12)]
    d.polygon(pts(star), fill=INK)
    inner = [(16, 5), (20, 13), (28, 14), (22, 19), (24, 27), (16, 23), (8, 27), (10, 19), (4, 14), (12, 13)]
    d.polygon(pts(inner), fill=RED)
    d.line(pts([(16, 7), (16, 22)]), fill=GOLD, width=sc(2))
    return down(img, size)


def save(img: Image.Image, rel: str, written: list[Path]) -> None:
    path = RAW / rel
    path.parent.mkdir(parents=True, exist_ok=True)
    img.save(path)
    OUT.mkdir(parents=True, exist_ok=True)
    img.save(OUT / path.name)
    written.append(path)


def contact_sheet(paths: list[Path]) -> None:
    cells = []
    for path in paths:
        img = Image.open(path).convert("RGBA")
        thumb = Image.new("RGBA", (128, 128), (27, 29, 25, 255))
        img.thumbnail((108, 108), Image.Resampling.LANCZOS)
        thumb.alpha_composite(img, ((128 - img.width) // 2, (128 - img.height) // 2))
        cells.append((path.name, thumb))
    cols = 4
    rows = (len(cells) + cols - 1) // cols
    sheet = Image.new("RGBA", (cols * 180, rows * 160), (238, 229, 207, 255))
    d = ImageDraw.Draw(sheet, "RGBA")
    for i, (name, thumb) in enumerate(cells):
        x = (i % cols) * 180
        y = (i // cols) * 160
        sheet.alpha_composite(thumb, (x + 26, y + 8))
        d.text((x + 8, y + 139), name[:26], fill=(30, 27, 23, 255))
    OUT.mkdir(parents=True, exist_ok=True)
    sheet.save(OUT / "final_repaint_pass_1_continuation_contact_sheet.png")


def main() -> None:
    written: list[Path] = []
    save(ui_panel_light(), "ui/ui_panel_felt_light_96.png", written)
    save(ui_square((64, 64), (18, 28, 31, 238), TEAL), "ui/ui_slot_equipment_64.png", written)
    save(ui_square((64, 64), (64, 49, 33, 238), GOLD, inner=False), "ui/ui_chip_resource_64.png", written)
    save(ui_square((64, 64), FELT_DARK, TEAL), "ui/ui_tooltip_dark_64.png", written)
    save(card_back(), "ui/ui_card_back_96x128.png", written)
    save(ui_square((64, 64), FELT_DARK, GOLD, inner=False), "ui/ui_button_dark_64.png", written)
    save(art_yurt(), "cards/card_art_yurt_64.png", written)
    save(art_tamga_stone(), "cards/card_art_tamga_stone_64.png", written)
    save(art_wolf_track(), "cards/card_art_wolf_track_64.png", written)
    save(icon_wisdom(), "icons/icon_wisdom_32.png", written)
    save(icon_glory(), "icons/icon_glory_32.png", written)
    contact_sheet(written)
    for path in written:
        print(path.relative_to(ROOT).as_posix())
    print((OUT / "final_repaint_pass_1_continuation_contact_sheet.png").relative_to(ROOT).as_posix())


if __name__ == "__main__":
    main()
