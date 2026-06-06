from pathlib import Path
import math

from PIL import Image, ImageDraw


ROOT = Path(__file__).resolve().parents[2]
RAW = ROOT / "games" / "turkic-jam-2026" / "raw"
OUT = ROOT / "gamedesign" / "assets" / "concept" / "final_repaint_pass_6_future_tile_card_library"
S = 4

INK = (18, 15, 12, 255)
SAND = (207, 151, 69, 230)
SAND_DARK = (115, 74, 38, 210)
FELT = (236, 217, 171, 255)
FELT_SHADE = (210, 189, 146, 255)
WOOD = (94, 57, 32, 255)
WOOD_DARK = (56, 35, 23, 230)
TEAL = (44, 169, 157, 255)
TEAL_SOFT = (44, 169, 157, 132)
RED = (156, 52, 38, 255)
GOLD = (245, 188, 72, 255)
GREEN = (78, 137, 76, 230)
BONE = (226, 209, 166, 255)
WATER = (49, 145, 160, 220)
STORM = (92, 86, 74, 205)
SHADOW = (0, 0, 0, 78)


def sc(v: int) -> int:
    return v * S


def box(b: tuple[int, int, int, int]) -> tuple[int, int, int, int]:
    return tuple(sc(v) for v in b)


def pts(points: list[tuple[int, int]]) -> list[tuple[int, int]]:
    return [(sc(x), sc(y)) for x, y in points]


def canvas(size: tuple[int, int]) -> tuple[Image.Image, ImageDraw.ImageDraw]:
    img = Image.new("RGBA", (size[0] * S, size[1] * S), (0, 0, 0, 0))
    return img, ImageDraw.Draw(img, "RGBA")


def down(img: Image.Image, size: tuple[int, int]) -> Image.Image:
    return img.resize(size, Image.Resampling.LANCZOS)


def tamga(d: ImageDraw.ImageDraw, cx: int, cy: int, s: int, color=TEAL, width: int = 3) -> None:
    cx, cy, s, width = sc(cx), sc(cy), sc(s), sc(width)
    d.line((cx, cy - s, cx, cy + s), fill=color, width=width)
    d.line((cx - s, cy - s // 2, cx, cy - s, cx + s, cy - s // 2), fill=color, width=width)
    d.line((cx - s, cy + s // 2, cx, cy, cx + s, cy + s // 2), fill=color, width=width)


def yurt(d: ImageDraw.ImageDraw, x: int, y: int, scale: float = 1.0, accent=RED) -> None:
    w = int(19 * scale)
    h = int(17 * scale)
    d.ellipse(box((x - w - 6, y + h - 1, x + w + 6, y + h + 9)), fill=SHADOW)
    d.polygon(pts([(x - w, y), (x, y - int(23 * scale)), (x + w, y)]), fill=FELT, outline=INK)
    d.rounded_rectangle(box((x - w + 2, y, x + w - 2, y + h)), radius=sc(5), fill=FELT_SHADE, outline=INK, width=sc(2))
    d.line(pts([(x - w + 6, y + 5), (x + w - 6, y + 5)]), fill=accent, width=sc(2))
    door_top = min(y + 7, y + h - 1)
    d.rectangle(box((x - 5, door_top, x + 5, y + h)), fill=accent, outline=INK)


def small_fire(d: ImageDraw.ImageDraw, x: int, y: int, scale: float = 1.0) -> None:
    d.ellipse(box((x - int(13 * scale), y + 6, x + int(13 * scale), y + 15)), fill=(54, 35, 23, 145))
    d.polygon(pts([(x - 4, y + 10), (x, y - int(14 * scale)), (x + 6, y + 10)]), fill=(232, 91, 38, 235))
    d.polygon(pts([(x - 8, y + 11), (x - 1, y - int(5 * scale)), (x + 3, y + 11)]), fill=(255, 203, 75, 225))


def well(d: ImageDraw.ImageDraw, x: int, y: int, scale: float = 1.0) -> None:
    w = int(31 * scale)
    h = int(18 * scale)
    d.ellipse(box((x - w, y + h, x + w, y + h + 14)), fill=SHADOW)
    d.ellipse(box((x - w, y, x + w, y + h + 16)), fill=(116, 75, 40, 245), outline=INK, width=sc(2))
    d.ellipse(box((x - w + 6, y + 5, x + w - 6, y + h + 9)), fill=WATER, outline=(23, 75, 85, 190), width=sc(2))
    d.line(pts([(x - w - 4, y + h + 4), (x + w + 4, y + h + 4)]), fill=BONE, width=sc(3))


def watchtower(d: ImageDraw.ImageDraw, x: int, y: int, scale: float = 1.0) -> None:
    h = int(58 * scale)
    d.ellipse(box((x - 27, y + 47, x + 27, y + 60)), fill=SHADOW)
    d.line(pts([(x - 17, y + 52), (x - 7, y + 6)]), fill=WOOD_DARK, width=sc(5))
    d.line(pts([(x + 17, y + 52), (x + 7, y + 6)]), fill=WOOD_DARK, width=sc(5))
    d.line(pts([(x - 20, y + 35), (x + 20, y + 35)]), fill=WOOD, width=sc(5))
    d.rounded_rectangle(box((x - 23, y - 4, x + 23, y + 17)), radius=sc(4), fill=(132, 84, 41, 255), outline=INK, width=sc(2))
    d.line(pts([(x - 17, y + 8), (x + 17, y + 8)]), fill=TEAL, width=sc(3))
    d.line(pts([(x + 24, y - h // 4), (x + 24, y + 5)]), fill=WOOD_DARK, width=sc(3))
    d.polygon(pts([(x + 25, y - h // 4), (x + 47, y - h // 8), (x + 25, y)]), fill=RED, outline=INK)


def pack(d: ImageDraw.ImageDraw, x: int, y: int, scale: float = 1.0) -> None:
    d.ellipse(box((x - 27, y + 24, x + 27, y + 35)), fill=SHADOW)
    d.rounded_rectangle(box((x - 27, y - 15, x + 27, y + 26)), radius=sc(8), fill=(122, 78, 43, 255), outline=INK, width=sc(2))
    d.line(pts([(x - 22, y - 1), (x + 22, y - 1)]), fill=GOLD, width=sc(3))
    d.rounded_rectangle(box((x - 13, y - 25, x + 13, y - 9)), radius=sc(5), fill=FELT_SHADE, outline=INK, width=sc(2))
    tamga(d, x, y + 12, int(8 * scale), TEAL, 3)


def trail_marks(d: ImageDraw.ImageDraw, points: list[tuple[int, int]], color=(81, 51, 31, 155), width: int = 5) -> None:
    d.line(pts(points), fill=color, width=sc(width), joint="curve")
    for i, (x, y) in enumerate(points):
        if i % 2 == 0:
            d.ellipse(box((x - 5, y - 3, x + 5, y + 3)), fill=(50, 33, 24, 135))


def card_frame(draw_fn) -> Image.Image:
    img, d = canvas((64, 64))
    d.rounded_rectangle(box((4, 4, 60, 60)), radius=sc(8), fill=(32, 38, 34, 255), outline=INK, width=sc(2))
    d.rounded_rectangle(box((8, 8, 56, 56)), radius=sc(6), fill=(188, 130, 64, 210))
    draw_fn(d, 32, 32, 0.55)
    return down(img, (64, 64))


def card_oasis() -> Image.Image:
    def draw(d, x, y, scale):
        well(d, x, y + 4, scale)
        for px in [18, 46]:
            d.line(pts([(px, 43), (px + (6 if px < x else -6), 25)]), fill=GREEN, width=sc(3))
        d.ellipse(box((16, 35, 48, 48)), fill=(49, 145, 160, 85))

    return card_frame(draw)


def card_mirage() -> Image.Image:
    def draw(d, x, y, scale):
        for off, alpha in [(-9, 82), (0, 140), (9, 82)]:
            d.arc(box((12 + off, 21, 52 + off, 45)), 190, 350, fill=(54, 194, 180, alpha), width=sc(3))
        d.polygon(pts([(18, 43), (32, 22), (46, 43)]), fill=(236, 217, 171, 88), outline=(44, 169, 157, 120))

    return card_frame(draw)


def card_storm() -> Image.Image:
    def draw(d, x, y, scale):
        for i, y0 in enumerate([20, 29, 38]):
            d.arc(box((7 + i * 4, y0 - 8, 58 - i * 2, y0 + 13)), 190, 355, fill=STORM, width=sc(5 - i))
        d.polygon(pts([(18, 46), (50, 38), (58, 51), (22, 55)]), fill=(207, 151, 69, 150))

    return card_frame(draw)


def card_last_tamga() -> Image.Image:
    def draw(d, x, y, scale):
        d.ellipse(box((17, 46, 47, 55)), fill=SHADOW)
        d.rounded_rectangle(box((22, 17, 42, 47)), radius=sc(5), fill=(111, 77, 50, 255), outline=INK, width=sc(2))
        tamga(d, x, y, 10, TEAL, 3)
        d.arc(box((12, 14, 52, 54)), 220, 320, fill=GOLD, width=sc(2))

    return card_frame(draw)


def card_well() -> Image.Image:
    return card_frame(lambda d, x, y, scale: well(d, x, y, scale))


def card_watchtower() -> Image.Image:
    return card_frame(lambda d, x, y, scale: watchtower(d, x, y + 6, scale))


def tile_well() -> Image.Image:
    img, d = canvas((128, 128))
    well(d, 64, 62, 1.0)
    d.line(pts([(36, 68), (51, 52), (66, 64), (90, 49)]), fill=(76, 48, 28, 92), width=sc(4))
    return down(img, (128, 128))


def tile_watchtower() -> Image.Image:
    img, d = canvas((128, 128))
    watchtower(d, 62, 50, 1.0)
    d.line(pts([(33, 91), (94, 91)]), fill=(98, 63, 35, 120), width=sc(3))
    return down(img, (128, 128))


def tile_pack() -> Image.Image:
    img, d = canvas((128, 128))
    pack(d, 64, 66, 1.0)
    d.line(pts([(35, 96), (94, 96)]), fill=(82, 53, 32, 105), width=sc(3))
    return down(img, (128, 128))


def tile_small_camp() -> Image.Image:
    img, d = canvas((128, 128))
    d.ellipse(box((28, 81, 100, 108)), fill=(169, 111, 53, 135), outline=(91, 59, 35, 120), width=sc(2))
    yurt(d, 50, 65, 0.72, RED)
    small_fire(d, 77, 76, 0.8)
    d.rounded_rectangle(box((81, 61, 104, 75)), radius=sc(4), fill=(126, 78, 40, 230), outline=INK, width=sc(2))
    return down(img, (128, 128))


def tile_clan_camp() -> Image.Image:
    img, d = canvas((128, 128))
    d.ellipse(box((18, 69, 110, 111)), fill=(169, 111, 53, 145), outline=(91, 59, 35, 130), width=sc(3))
    yurt(d, 42, 63, 0.64, RED)
    yurt(d, 78, 59, 0.78, TEAL)
    yurt(d, 88, 83, 0.55, RED)
    small_fire(d, 62, 84, 0.8)
    d.line(pts([(23, 99), (63, 106), (104, 98)]), fill=WOOD, width=sc(4))
    return down(img, (128, 128))


def tile_hunting_trail() -> Image.Image:
    img, d = canvas((128, 128))
    trail_marks(d, [(25, 101), (45, 83), (64, 72), (84, 55), (103, 31)], width=5)
    for x, y in [(37, 74), (76, 50)]:
        d.line(pts([(x, y), (x, y - 22)]), fill=WOOD, width=sc(4))
        d.polygon(pts([(x + 2, y - 22), (x + 22, y - 15), (x + 2, y - 7)]), fill=RED, outline=INK)
    for x, y in [(55, 86), (71, 68), (89, 49)]:
        d.ellipse(box((x - 3, y - 5, x + 5, y + 6)), fill=(42, 29, 22, 155))
    return down(img, (128, 128))


def tile_vision() -> Image.Image:
    img, d = canvas((128, 128))
    d.ellipse(box((30, 82, 98, 104)), fill=SHADOW)
    for r, alpha in [(46, 70), (34, 115), (22, 170)]:
        d.ellipse(box((64 - r, 52 - r // 2, 64 + r, 52 + r // 2)), outline=(44, 169, 157, alpha), width=sc(3))
    tamga(d, 64, 52, 16, GOLD, 4)
    d.arc(box((25, 54, 103, 116)), 205, 335, fill=(207, 151, 69, 132), width=sc(5))
    return down(img, (128, 128))


def tile_false_path() -> Image.Image:
    img, d = canvas((128, 128))
    trail_marks(d, [(22, 97), (45, 83), (68, 76), (95, 61)], color=(82, 53, 34, 135), width=5)
    d.arc(box((41, 42, 107, 94)), 180, 335, fill=(44, 169, 157, 130), width=sc(4))
    d.arc(box((28, 27, 97, 79)), 195, 350, fill=(236, 217, 171, 92), width=sc(3))
    d.line(pts([(87, 57), (104, 47)]), fill=(156, 52, 38, 190), width=sc(4))
    d.line(pts([(87, 47), (104, 57)]), fill=(156, 52, 38, 190), width=sc(4))
    return down(img, (128, 128))


def tile_buried_spring() -> Image.Image:
    img, d = canvas((128, 128))
    d.arc(box((22, 44, 106, 106)), 190, 350, fill=SAND, width=sc(16))
    d.ellipse(box((44, 70, 84, 88)), fill=WATER, outline=(23, 75, 85, 170), width=sc(2))
    for x, y in [(37, 75), (92, 69), (58, 58)]:
        d.line(pts([(x, y), (x + 7, y - 16)]), fill=GREEN, width=sc(4))
    d.arc(box((27, 38, 101, 101)), 195, 345, fill=(245, 188, 72, 110), width=sc(3))
    return down(img, (128, 128))


def icon_canvas() -> tuple[Image.Image, ImageDraw.ImageDraw]:
    return canvas((32, 32))


def icon(kind: str) -> Image.Image:
    img, d = icon_canvas()
    if kind == "aul_upgrade":
        yurt(d, 16, 19, 0.35, TEAL)
        d.polygon(pts([(16, 3), (24, 12), (18, 12), (18, 25), (14, 25), (14, 12), (8, 12)]), fill=GOLD, outline=INK)
    elif kind == "card_gain":
        d.rounded_rectangle(box((8, 8, 24, 28)), radius=sc(3), fill=FELT, outline=INK, width=sc(2))
        d.line(pts([(11, 14), (21, 14)]), fill=TEAL, width=sc(2))
        d.polygon(pts([(16, 2), (25, 11), (18, 11), (18, 20), (14, 20), (14, 11), (7, 11)]), fill=GOLD, outline=INK)
    elif kind == "deck":
        for off in [0, 3, 6]:
            d.rounded_rectangle(box((7 + off, 5 + off, 22 + off, 25 + off)), radius=sc(3), fill=(43, 101, 95, 255), outline=INK, width=sc(1))
        d.line(pts([(15, 17), (25, 17)]), fill=GOLD, width=sc(2))
    elif kind == "map":
        d.polygon(pts([(4, 8), (13, 5), (22, 8), (28, 5), (28, 25), (20, 28), (11, 25), (4, 28)]), fill=FELT, outline=INK)
        d.line(pts([(13, 5), (13, 25), (22, 8), (22, 28)]), fill=SAND_DARK, width=sc(2))
        d.line(pts([(8, 21), (15, 16), (25, 18)]), fill=TEAL, width=sc(2))
    elif kind == "memory":
        d.ellipse(box((6, 7, 26, 27)), fill=(111, 77, 50, 255), outline=INK, width=sc(2))
        tamga(d, 16, 17, 8, TEAL, 3)
        d.arc(box((3, 4, 29, 30)), 215, 320, fill=GOLD, width=sc(2))
    elif kind == "settings":
        d.rounded_rectangle(box((7, 8, 25, 24)), radius=sc(5), fill=WOOD, outline=INK, width=sc(2))
        d.line(pts([(8, 16), (24, 16)]), fill=GOLD, width=sc(3))
        d.ellipse(box((11, 12, 19, 20)), fill=TEAL, outline=INK, width=sc(1))
    elif kind == "warning":
        d.polygon(pts([(16, 3), (29, 28), (3, 28)]), fill=GOLD, outline=INK)
        d.line(pts([(16, 10), (16, 20)]), fill=INK, width=sc(3))
        d.ellipse(box((14, 23, 18, 27)), fill=INK)
    return down(img, (32, 32))


CARD_ART = {
    "card_art_oasis_64.png": card_oasis,
    "card_art_mirage_64.png": card_mirage,
    "card_art_storm_64.png": card_storm,
    "card_art_last_tamga_64.png": card_last_tamga,
    "card_art_well_64.png": card_well,
    "card_art_watchtower_64.png": card_watchtower,
}

TILES = {
    "tile_well_01.png": tile_well,
    "tile_watchtower_01.png": tile_watchtower,
    "tile_pack_01.png": tile_pack,
    "tile_small_camp_01.png": tile_small_camp,
    "tile_clan_camp_01.png": tile_clan_camp,
    "tile_hunting_trail_01.png": tile_hunting_trail,
    "tile_vision_01.png": tile_vision,
    "tile_false_path_01.png": tile_false_path,
    "tile_buried_spring_01.png": tile_buried_spring,
}

ICONS = {
    "icon_aul_upgrade_32.png": lambda: icon("aul_upgrade"),
    "icon_card_gain_32.png": lambda: icon("card_gain"),
    "icon_deck_32.png": lambda: icon("deck"),
    "icon_map_32.png": lambda: icon("map"),
    "icon_memory_32.png": lambda: icon("memory"),
    "icon_settings_32.png": lambda: icon("settings"),
    "icon_warning_32.png": lambda: icon("warning"),
}


def save(img: Image.Image, folder: str, name: str, written: list[Path]) -> None:
    path = RAW / folder / name
    path.parent.mkdir(parents=True, exist_ok=True)
    img.save(path)
    OUT.mkdir(parents=True, exist_ok=True)
    img.save(OUT / name)
    written.append(path)


def contact_sheet(paths: list[Path]) -> None:
    cells = []
    for path in paths:
        img = Image.open(path).convert("RGBA")
        thumb = Image.new("RGBA", (120, 120), (27, 29, 25, 255))
        img.thumbnail((104, 104), Image.Resampling.LANCZOS)
        thumb.alpha_composite(img, ((120 - img.width) // 2, (120 - img.height) // 2))
        cells.append((path.name, thumb))

    cols = 6
    rows = math.ceil(len(cells) / cols)
    sheet = Image.new("RGBA", (cols * 166, rows * 158), (238, 229, 207, 255))
    d = ImageDraw.Draw(sheet, "RGBA")
    for i, (name, thumb) in enumerate(cells):
        x = (i % cols) * 166
        y = (i // cols) * 158
        sheet.alpha_composite(thumb, (x + 23, y + 8))
        d.text((x + 8, y + 132), name[:25], fill=(30, 27, 23, 255))
    OUT.mkdir(parents=True, exist_ok=True)
    sheet.save(OUT / "final_repaint_pass_6_future_tile_card_library_contact_sheet.png")


def main() -> None:
    written: list[Path] = []
    for name, make in CARD_ART.items():
        save(make(), "cards", name, written)
    for name, make in TILES.items():
        save(make(), "tiles", name, written)
    for name, make in ICONS.items():
        save(make(), "icons", name, written)
    contact_sheet(written)
    for path in written:
        print(path.relative_to(ROOT).as_posix())
    print((OUT / "final_repaint_pass_6_future_tile_card_library_contact_sheet.png").relative_to(ROOT).as_posix())


if __name__ == "__main__":
    main()
