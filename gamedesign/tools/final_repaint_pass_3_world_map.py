from pathlib import Path
import math

from PIL import Image, ImageDraw


ROOT = Path(__file__).resolve().parents[2]
RAW = ROOT / "games" / "turkic-jam-2026" / "raw"
OUT = ROOT / "gamedesign" / "assets" / "concept" / "final_repaint_pass_3_world_map"
S = 4

INK = (18, 15, 12, 255)
SAND = (207, 151, 69, 255)
SAND_LIGHT = (232, 185, 96, 255)
SAND_DARK = (137, 91, 45, 255)
ROAD = (122, 78, 42, 245)
ROAD_LIGHT = (154, 101, 52, 210)
ROAD_DARK = (67, 45, 31, 180)
STONE = (139, 120, 86, 230)
WOOD = (94, 57, 32, 255)
FELT = (236, 217, 171, 255)
TEAL = (44, 169, 157, 255)
RED = (156, 52, 38, 255)
GOLD = (245, 188, 72, 255)
BONE = (226, 209, 166, 230)
DRY = (151, 111, 39, 220)
GREEN = (74, 139, 76, 230)
WATER = (49, 145, 160, 230)
SHADOW = (0, 0, 0, 80)


def sc(v: int) -> int:
    return v * S


def box(b: tuple[int, int, int, int]) -> tuple[int, int, int, int]:
    return tuple(sc(v) for v in b)


def pts(points: list[tuple[int, int]]) -> list[tuple[int, int]]:
    return [(sc(x), sc(y)) for x, y in points]


def rgba(size: tuple[int, int]) -> tuple[Image.Image, ImageDraw.ImageDraw]:
    img = Image.new("RGBA", (size[0] * S, size[1] * S), (0, 0, 0, 0))
    return img, ImageDraw.Draw(img, "RGBA")


def rgb(size: tuple[int, int], color) -> tuple[Image.Image, ImageDraw.ImageDraw]:
    img = Image.new("RGB", (size[0] * S, size[1] * S), color[:3])
    return img, ImageDraw.Draw(img, "RGBA")


def down(img: Image.Image, size: tuple[int, int]) -> Image.Image:
    return img.resize(size, Image.Resampling.LANCZOS)


def tamga(d: ImageDraw.ImageDraw, cx: int, cy: int, s: int, color=TEAL, width: int = 3) -> None:
    cx, cy, s, width = sc(cx), sc(cy), sc(s), sc(width)
    d.line((cx, cy - s, cx, cy + s), fill=color, width=width)
    d.line((cx - s, cy - s // 2, cx, cy - s, cx + s, cy - s // 2), fill=color, width=width)
    d.line((cx - s, cy + s // 2, cx, cy, cx + s, cy + s // 2), fill=color, width=width)


def sand_base(size=(128, 128), packed=False) -> Image.Image:
    img, d = rgb(size, (203, 146, 63, 255) if not packed else (176, 121, 59, 255))
    w, h = size
    for y in range(-24, h + 30, 13):
        d.arc(box((-18, y, w + 18, y + 58)), 190, 350, fill=(235, 188, 98, 74), width=sc(1))
    for i in range(54 if size[0] == 128 else 150):
        x = (i * 37 + 11) % w
        y = (i * 53 + 19) % h
        col = (111, 75, 39, 75) if i % 4 else (245, 200, 111, 65)
        d.ellipse(box((x, y, x + 2, y + 1)), fill=col)
    return down(img, size)


def decor(kind: str) -> Image.Image:
    img, d = rgba((128, 128))
    if kind == "dune":
        d.pieslice(box((11, 35, 117, 123)), 190, 350, fill=(222, 174, 83, 130))
        d.arc(box((16, 39, 112, 112)), 194, 348, fill=(133, 86, 42, 135), width=sc(2))
        d.arc(box((23, 51, 101, 103)), 196, 340, fill=(245, 200, 111, 70), width=sc(1))
    elif kind == "stones":
        for x, y, r in [(34, 62, 9), (52, 75, 5), (75, 58, 8), (93, 78, 5), (84, 43, 4)]:
            d.ellipse(box((x - r, y - r, x + r, y + r)), fill=STONE, outline=(65, 48, 34, 160), width=sc(1))
            d.arc(box((x - r + 2, y - r + 2, x + r - 1, y + r)), 205, 310, fill=(207, 185, 138, 110), width=sc(1))
    elif kind == "dry_grass":
        for x, y in [(36, 84), (62, 77), (89, 88)]:
            for a in [-42, -18, 12, 36]:
                dx = int(math.sin(math.radians(a)) * 14)
                dy = int(math.cos(math.radians(a)) * 14)
                d.line(pts([(x, y), (x + dx, y - dy)]), fill=DRY, width=sc(3))
    elif kind == "tracks":
        for i in range(10):
            x = 32 + i * 7
            y = 30 + i * 8
            d.ellipse(box((x, y, x + 6, y + 11)), fill=(85, 57, 34, 92))
            d.ellipse(box((x + 19, y + 3, x + 25, y + 14)), fill=(85, 57, 34, 72))
    elif kind == "bones":
        for x, y in [(38, 70), (68, 84), (86, 58)]:
            d.rounded_rectangle(box((x - 13, y - 3, x + 13, y + 3)), radius=sc(3), fill=BONE)
            d.ellipse(box((x - 17, y - 6, x - 8, y + 3)), fill=BONE)
            d.ellipse(box((x + 8, y - 3, x + 17, y + 6)), fill=BONE)
    elif kind == "cracks":
        for line in [[(22, 73), (55, 64), (91, 77)], [(54, 65), (47, 38), (62, 23)], [(78, 75), (97, 49), (113, 39)]]:
            d.line(pts(line), fill=(80, 51, 30, 145), width=sc(3))
    return down(img, (128, 128))


def road_sprite(kind: str) -> Image.Image:
    img, d = rgba((128, 128))
    paths = {
        "ew": [(-20, 64), (148, 64)],
        "ns": [(64, -20), (64, 148)],
        "ne": [(64, -20), (64, 64), (148, 64)],
        "es": [(148, 64), (64, 64), (64, 148)],
        "sw": [(64, 148), (64, 64), (-20, 64)],
        "wn": [(-20, 64), (64, 64), (64, -20)],
    }
    path = paths[kind]
    d.line(pts(path), fill=ROAD_DARK, width=sc(46), joint="curve")
    d.line(pts(path), fill=ROAD, width=sc(38), joint="curve")
    d.line(pts(path), fill=ROAD_LIGHT, width=sc(20), joint="curve")
    d.line(pts(path), fill=(74, 48, 29, 100), width=sc(3), joint="curve")
    for i in range(12):
        x = 13 + (i * 17) % 103
        y = 58 + ((i * 29) % 14)
        if kind == "ns":
            x, y = y, x
        d.ellipse(box((x, y, x + 3, y + 2)), fill=(49, 33, 23, 100))
    return down(img, (128, 128))


def road_entry() -> Image.Image:
    img, d = rgba((128, 128))
    d.line(pts([(64, 20), (64, 148)]), fill=ROAD_DARK, width=sc(48))
    d.line(pts([(64, 20), (64, 148)]), fill=ROAD, width=sc(38))
    d.polygon(pts([(27, 18), (101, 18), (84, 50), (44, 50)]), fill=(164, 111, 57, 240), outline=(65, 43, 28, 170))
    d.line(pts([(42, 29), (86, 29)]), fill=TEAL, width=sc(3))
    return down(img, (128, 128))


def current_highlight() -> Image.Image:
    img, d = rgba((128, 128))
    d.rounded_rectangle(box((22, 22, 106, 106)), radius=sc(12), outline=(255, 218, 92, 215), width=sc(4))
    d.rounded_rectangle(box((30, 30, 98, 98)), radius=sc(9), outline=(118, 218, 155, 120), width=sc(2))
    d.ellipse(box((49, 49, 79, 79)), fill=(255, 205, 75, 45))
    return down(img, (128, 128))


def buffer(kind: str) -> Image.Image:
    img, d = rgba((128, 128))
    if kind == "edge_stones":
        for x, y, r in [(18, 69, 5), (34, 61, 7), (51, 72, 4), (68, 62, 8), (88, 72, 5), (106, 61, 6)]:
            d.ellipse(box((x - r, y - r, x + r, y + r)), fill=STONE, outline=(67, 48, 34, 185), width=sc(1))
        d.line(pts([(15, 80), (113, 82)]), fill=(61, 42, 29, 95), width=sc(2))
    elif kind == "packed_sand":
        d.rounded_rectangle(box((9, 44, 119, 86)), radius=sc(13), fill=(160, 111, 56, 110))
        for y in [54, 64, 74]:
            d.line(pts([(18, y), (110, y - 4)]), fill=(78, 52, 32, 90), width=sc(2))
    elif kind == "stakes":
        for x, h in [(30, 35), (64, 42), (98, 32)]:
            d.rounded_rectangle(box((x - 4, 78 - h, x + 4, 82)), radius=sc(3), fill=(111, 69, 36, 220))
            d.polygon(pts([(x - 5, 78 - h), (x + 5, 78 - h), (x, 68 - h)]), fill=(148, 91, 45, 220))
            d.line(pts([(x - 2, 56), (x + 9, 63)]), fill=TEAL, width=sc(2))
    else:
        d.arc(box((7, 34, 121, 98)), 8, 172, fill=(75, 50, 32, 115), width=sc(3))
        d.arc(box((14, 43, 126, 106)), 8, 172, fill=(75, 50, 32, 78), width=sc(2))
        for x in [38, 58, 82]:
            d.ellipse(box((x, 72, x + 4, 77)), fill=(75, 50, 32, 80))
    return down(img, (128, 128))


def aul_ground() -> Image.Image:
    img = sand_base((256, 256), packed=True).convert("RGB").resize((1024, 1024), Image.Resampling.NEAREST)
    d = ImageDraw.Draw(img, "RGBA")
    d.rounded_rectangle(box((42, 54, 214, 206)), radius=sc(26), fill=(177, 122, 61, 220), outline=(111, 73, 37, 135), width=sc(4))
    d.ellipse(box((78, 78, 178, 178)), outline=(124, 78, 39, 120), width=sc(3))
    d.ellipse(box((110, 110, 146, 146)), fill=(71, 45, 27, 230))
    for x, y in [(64, 78), (194, 88), (78, 190), (180, 185)]:
        d.rounded_rectangle(box((x - 4, y - 12, x + 4, y + 16)), radius=sc(3), fill=(97, 59, 31, 190))
    return down(img, (256, 256)).convert("RGB")


def yurt(size=(128, 128), shift=0) -> Image.Image:
    img, d = rgba(size)
    d.ellipse(box((23 + shift, 78, 105 + shift, 101)), fill=SHADOW)
    d.ellipse(box((28 + shift, 48, 100 + shift, 96)), fill=FELT, outline=(74, 55, 38, 230), width=sc(3))
    d.arc(box((30 + shift, 22, 100 + shift, 88)), 200, 340, fill=(74, 55, 38, 230), width=sc(4))
    d.arc(box((38 + shift, 34, 92 + shift, 78)), 204, 336, fill=(183, 129, 62, 170), width=sc(2))
    d.rectangle(box((54 + shift, 72, 72 + shift, 96)), fill=RED)
    d.line(pts([(35 + shift, 63), (95 + shift, 63)]), fill=TEAL, width=sc(3))
    tamga(d, 66 + shift, 30, 7, RED, 2)
    return down(img, size)


def fire() -> Image.Image:
    img, d = rgba((128, 128))
    d.ellipse(box((27, 64, 101, 108)), fill=(239, 119, 35, 58))
    d.ellipse(box((35, 78, 94, 98)), fill=(60, 40, 25, 195))
    d.polygon(pts([(58, 80), (66, 38), (77, 80)]), fill=(240, 105, 35, 245))
    d.polygon(pts([(47, 82), (61, 55), (69, 84)]), fill=(255, 194, 71, 240))
    d.polygon(pts([(65, 82), (75, 58), (82, 84)]), fill=(255, 225, 115, 220))
    d.line(pts([(38, 90), (92, 76)]), fill=(91, 52, 31, 255), width=sc(5))
    d.line(pts([(42, 74), (88, 92)]), fill=(91, 52, 31, 255), width=sc(5))
    return down(img, (128, 128))


def tile(kind: str) -> Image.Image:
    img, d = rgba((128, 128))
    if kind == "yurt":
        return yurt()
    if kind == "tamga_stone":
        d.ellipse(box((39, 100, 95, 115)), fill=SHADOW)
        d.polygon(pts([(44, 104), (52, 35), (83, 27), (99, 105)]), fill=(145, 121, 86, 255), outline=INK)
        d.line(pts([(56, 48), (81, 44)]), fill=(199, 176, 125, 160), width=sc(2))
        tamga(d, 70, 67, 15, TEAL, 4)
    elif kind == "wolf_track":
        d.line(pts([(23, 98), (106, 29)]), fill=(134, 52, 42, 120), width=sc(4))
        for x, y in [(40, 58), (61, 77), (82, 95)]:
            d.ellipse(box((x - 7, y - 4, x + 7, y + 10)), fill=(60, 42, 31, 235))
            for ox, oy in [(-9, -10), (0, -13), (9, -10)]:
                d.ellipse(box((x + ox - 4, y + oy - 4, x + ox + 4, y + oy + 4)), fill=(60, 42, 31, 235))
    elif kind == "oasis":
        d.ellipse(box((27, 62, 101, 99)), fill=WATER, outline=(26, 81, 86, 240), width=sc(3))
        d.arc(box((32, 66, 96, 93)), 10, 170, fill=(122, 215, 219, 100), width=sc(2))
        for x, y in [(30, 80), (82, 71), (66, 96), (95, 88)]:
            d.line(pts([(x, y), (x + 10, y - 25)]), fill=GREEN, width=sc(4))
            d.polygon(pts([(x + 8, y - 18), (x + 22, y - 22), (x + 13, y - 11)]), fill=(93, 158, 77, 210))
    elif kind == "mirage":
        for y in [49, 61, 74, 86]:
            d.arc(box((18, y - 13, 110, y + 17)), 6, 174, fill=(143, 210, 220, 145), width=sc(4))
        d.line(pts([(34, 78), (96, 70)]), fill=(235, 215, 168, 150), width=sc(4))
    elif kind == "storm":
        for r, a in [(50, 210), (38, 220), (26, 230)]:
            d.arc(box((64 - r, 64 - r, 64 + r, 64 + r)), 210, 520, fill=(208, 169, 103, a), width=sc(7))
        d.ellipse(box((42, 92, 90, 108)), fill=(109, 79, 49, 110))
    else:
        d.ellipse(box((42, 88, 94, 106)), fill=SHADOW)
        d.rounded_rectangle(box((43, 59, 91, 91)), radius=sc(8), fill=(190, 171, 128, 255), outline=INK, width=sc(3))
        tamga(d, 67, 75, 13, TEAL, 4)
    return down(img, (128, 128))


def save(img: Image.Image, rel: str, written: list[Path]) -> None:
    path = RAW / rel
    path.parent.mkdir(parents=True, exist_ok=True)
    img.save(path)
    OUT.mkdir(parents=True, exist_ok=True)
    img.convert("RGBA").save(OUT / path.name)
    written.append(path)


def contact_sheet(paths: list[Path]) -> None:
    cells = []
    for path in paths:
        img = Image.open(path).convert("RGBA")
        thumb = Image.new("RGBA", (136, 136), (27, 29, 25, 255))
        img.thumbnail((118, 118), Image.Resampling.LANCZOS)
        thumb.alpha_composite(img, ((136 - img.width) // 2, (136 - img.height) // 2))
        cells.append((path.name, thumb))
    cols = 5
    rows = (len(cells) + cols - 1) // cols
    sheet = Image.new("RGBA", (cols * 188, rows * 170), (238, 229, 207, 255))
    d = ImageDraw.Draw(sheet, "RGBA")
    for i, (name, thumb) in enumerate(cells):
        x = (i % cols) * 188
        y = (i // cols) * 170
        sheet.alpha_composite(thumb, (x + 26, y + 8))
        d.text((x + 6, y + 148), name[:27], fill=(30, 27, 23, 255))
    OUT.mkdir(parents=True, exist_ok=True)
    sheet.save(OUT / "final_repaint_pass_3_world_map_contact_sheet.png")


def main() -> None:
    written: list[Path] = []
    save(sand_base(), "ground/ground_sand_base_01.png", written)
    for name in ["dune", "stones", "dry_grass", "tracks", "bones", "cracks"]:
        save(decor(name), f"decor/decor_{name}_01.png", written)
    for name in ["ns", "ew", "ne", "es", "sw", "wn"]:
        save(road_sprite(name), f"road/road_{'straight_' + name if name in ['ns', 'ew'] else 'corner_' + name}.png", written)
    save(road_entry(), "road/road_entry_aul.png", written)
    save(current_highlight(), "road/road_current_highlight.png", written)
    for name in ["edge_stones", "packed_sand", "stakes", "cart_marks"]:
        save(buffer(name), f"road/buffer_{name}_01.png", written)
    save(aul_ground(), "aul/aul_ground_2x2.png", written)
    save(yurt(shift=0), "aul/aul_yurt_small_01.png", written)
    save(yurt(shift=7), "aul/aul_yurt_small_02.png", written)
    save(fire(), "aul/aul_fire_01.png", written)
    for name in ["yurt", "tamga_stone", "wolf_track", "oasis", "mirage", "storm", "last_tamga"]:
        save(tile(name), f"tiles/tile_{name}_01.png", written)
    contact_sheet(written)
    for path in written:
        print(path.relative_to(ROOT).as_posix())
    print((OUT / "final_repaint_pass_3_world_map_contact_sheet.png").relative_to(ROOT).as_posix())


if __name__ == "__main__":
    main()
