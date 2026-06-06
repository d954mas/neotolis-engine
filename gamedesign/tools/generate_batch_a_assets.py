from __future__ import annotations

import math
from pathlib import Path

from PIL import Image, ImageDraw


ROOT = Path(__file__).resolve().parents[2]
RAW = ROOT / "games" / "turkic-jam-2026" / "raw"
CONCEPT = ROOT / "gamedesign" / "assets" / "concept" / "production_batch_a"


SAND = (203, 150, 70, 255)
SAND_LIGHT = (226, 181, 93, 255)
SAND_DARK = (137, 94, 48, 255)
ROAD = (126, 84, 47, 255)
ROAD_DARK = (76, 55, 38, 255)
STONE = (139, 119, 86, 255)
FELT_DARK = (24, 32, 35, 246)
FELT_DARK_2 = (14, 20, 24, 246)
FELT_LIGHT = (218, 187, 126, 246)
FIRE = (239, 112, 42, 255)
TEAL = (46, 160, 151, 255)
TEAL_DARK = (22, 87, 91, 255)
CREAM = (239, 217, 170, 255)
RED_TRIM = (158, 55, 38, 255)
GOLD = (238, 171, 73, 255)
INK = (28, 24, 20, 255)


def ensure_dirs() -> None:
    for name in [
        "ground",
        "decor",
        "road",
        "tiles",
        "aul",
        "hero",
        "ui",
        "cards",
        "equipment",
        "icons",
        "fx",
    ]:
        (RAW / name).mkdir(parents=True, exist_ok=True)
    CONCEPT.mkdir(parents=True, exist_ok=True)


def save(img: Image.Image, rel: str) -> Path:
    path = RAW / rel
    path.parent.mkdir(parents=True, exist_ok=True)
    img.save(path)
    return path


def rgba(size: tuple[int, int], color=(0, 0, 0, 0)) -> Image.Image:
    return Image.new("RGBA", size, color)


def draw_woven_border(draw: ImageDraw.ImageDraw, box: tuple[int, int, int, int], color, accent, step: int = 10) -> None:
    x0, y0, x1, y1 = box
    draw.rounded_rectangle(box, radius=8, outline=color, width=3)
    for x in range(x0 + 6, x1 - 6, step):
        draw.line((x, y0 + 2, x + 4, y0 + 6), fill=accent, width=2)
        draw.line((x, y1 - 2, x + 4, y1 - 6), fill=accent, width=2)
    for y in range(y0 + 6, y1 - 6, step):
        draw.line((x0 + 2, y, x0 + 6, y + 4), fill=accent, width=2)
        draw.line((x1 - 2, y, x1 - 6, y + 4), fill=accent, width=2)


def draw_tamga_corner(draw: ImageDraw.ImageDraw, x: int, y: int, sx: int, sy: int, color) -> None:
    draw.line((x, y + sy * 4, x + sx * 4, y, x + sx * 8, y + sy * 4), fill=color, width=2)
    draw.line((x + sx * 4, y, x + sx * 4, y + sy * 8), fill=color, width=2)
    draw.line((x + sx * 2, y + sy * 7, x + sx * 6, y + sy * 7), fill=color, width=2)


def draw_felt_texture(draw: ImageDraw.ImageDraw, size: tuple[int, int], light: bool) -> None:
    w, h = size
    line = (255, 230, 175, 18) if light else (111, 137, 126, 22)
    dot = (67, 55, 35, 34) if light else (205, 180, 112, 16)
    for y in range(11, h - 10, 14):
        draw.line((10, y, w - 10, y + ((y // 14) % 2) * 2), fill=line, width=1)
    for i in range(34):
        x = 9 + (i * 23) % max(1, w - 18)
        y = 9 + (i * 37) % max(1, h - 18)
        draw.point((x, y), fill=dot)


def ui_surface(size: tuple[int, int], bg, border, accent, radius: int = 10) -> Image.Image:
    img = rgba(size)
    d = ImageDraw.Draw(img)
    w, h = size
    d.rounded_rectangle((2, 2, w - 3, h - 3), radius=radius, fill=bg, outline=(0, 0, 0, 100), width=2)
    draw_felt_texture(d, size, bg[0] > 100)
    d.rounded_rectangle((8, 8, w - 9, h - 9), radius=max(2, radius - 5), outline=(255, 236, 178, 28), width=1)
    draw_woven_border(d, (3, 3, w - 4, h - 4), border, accent, 12)
    draw_tamga_corner(d, 9, 9, 1, 1, accent)
    draw_tamga_corner(d, w - 9, 9, -1, 1, accent)
    draw_tamga_corner(d, 9, h - 9, 1, -1, accent)
    draw_tamga_corner(d, w - 9, h - 9, -1, -1, accent)
    return img


def card_surface(selected: bool) -> Image.Image:
    img = rgba((96, 128))
    d = ImageDraw.Draw(img)
    face = (233, 211, 163, 255) if not selected else (242, 221, 170, 255)
    edge = (70, 54, 39, 255) if not selected else TEAL_DARK
    glow = (117, 179, 80, 210) if not selected else (88, 216, 207, 230)
    d.rounded_rectangle((3, 3, 92, 124), radius=8, fill=(15, 19, 20, 130))
    d.rounded_rectangle((5, 2, 90, 122), radius=8, fill=face, outline=edge, width=3)
    d.rounded_rectangle((9, 6, 86, 118), radius=5, outline=glow, width=2)
    d.rectangle((18, 16, 78, 24), fill=(198, 146, 76, 110))
    d.rectangle((18, 101, 78, 108), fill=(198, 146, 76, 100))
    for x in [9, 87]:
        sx = 1 if x == 9 else -1
        draw_tamga_corner(d, x, 10, sx, 1, RED_TRIM if not selected else TEAL)
        draw_tamga_corner(d, x, 118, sx, -1, RED_TRIM if not selected else TEAL)
    d.polygon((2, 18, 17, 2, 25, 2, 2, 25), fill=glow)
    if selected:
        d.rounded_rectangle((1, 1, 94, 126), radius=10, outline=(255, 232, 122, 210), width=3)
    return img


def make_ui() -> list[Path]:
    files = []
    files.append(save(ui_surface((96, 96), FELT_DARK, (88, 72, 49, 255), TEAL), "ui/ui_panel_felt_dark_96.png"))
    files.append(save(ui_surface((96, 96), FELT_LIGHT, (123, 86, 48, 255), RED_TRIM), "ui/ui_panel_felt_light_96.png"))
    files.append(save(card_surface(False), "ui/ui_card_playable_96x128.png"))
    files.append(save(card_surface(True), "ui/ui_card_selected_96x128.png"))
    files.append(save(ui_surface((64, 64), (31, 38, 39, 228), (93, 82, 62, 255), TEAL, 7), "ui/ui_slot_equipment_64.png"))
    files.append(save(ui_surface((64, 64), (58, 50, 39, 234), (150, 105, 55, 255), GOLD, 7), "ui/ui_chip_resource_64.png"))
    files.append(save(ui_surface((64, 64), (18, 23, 26, 240), (95, 78, 54, 255), TEAL, 7), "ui/ui_tooltip_dark_64.png"))
    return files


def sand_texture(size=(128, 128), alpha: int = 255) -> Image.Image:
    img = rgba(size, SAND)
    d = ImageDraw.Draw(img)
    w, h = size
    for y in range(0, h, 12):
        d.arc((-20, y - 24, w + 20, y + 42), 8, 174, fill=(215, 169, 88, alpha), width=1)
    for i in range(28):
        x = (i * 37) % w
        y = (i * 53) % h
        c = (162, 114, 58, int(alpha * 0.35))
        d.ellipse((x, y, x + 3, y + 2), fill=c)
    return img


def make_ground_decor() -> list[Path]:
    files = []
    files.append(save(sand_texture(), "ground/ground_sand_base_01.png"))
    for name in ["decor_dune_01", "decor_stones_01", "decor_dry_grass_01", "decor_tracks_01", "decor_cracks_01", "decor_bones_01"]:
        img = rgba((128, 128))
        d = ImageDraw.Draw(img)
        if name == "decor_dune_01":
            d.pieslice((8, 30, 120, 122), 190, 350, fill=(220, 175, 88, 120))
            d.arc((12, 34, 116, 116), 192, 350, fill=(164, 112, 54, 120), width=2)
        elif name == "decor_stones_01":
            for x, y, r in [(38, 58, 9), (56, 72, 5), (78, 54, 7), (88, 78, 4)]:
                d.ellipse((x - r, y - r, x + r, y + r), fill=STONE, outline=(83, 65, 46, 180), width=2)
        elif name == "decor_dry_grass_01":
            for x, y in [(40, 82), (65, 76), (86, 86)]:
                for a in [-35, -12, 15, 38]:
                    dx = int(math.sin(math.radians(a)) * 13)
                    dy = int(math.cos(math.radians(a)) * 13)
                    d.line((x, y, x + dx, y - dy), fill=(118, 91, 42, 190), width=3)
        elif name == "decor_tracks_01":
            for i in range(9):
                x = 34 + i * 7
                y = 32 + i * 8
                d.ellipse((x, y, x + 5, y + 10), fill=(104, 75, 44, 95))
                d.ellipse((x + 18, y + 2, x + 23, y + 12), fill=(104, 75, 44, 80))
        elif name == "decor_cracks_01":
            lines = [((22, 70), (58, 63), (90, 75)), ((54, 64), (48, 38), (62, 22)), ((78, 72), (96, 48), (113, 39))]
            for pts in lines:
                d.line(pts, fill=(105, 72, 40, 135), width=3)
        elif name == "decor_bones_01":
            for x, y, angle in [(42, 68, -20), (68, 82, 15), (84, 58, 42)]:
                d.rounded_rectangle((x - 12, y - 3, x + 12, y + 3), radius=3, fill=(226, 211, 174, 210))
                d.ellipse((x - 16, y - 5, x - 8, y + 3), fill=(226, 211, 174, 210))
                d.ellipse((x + 8, y - 3, x + 16, y + 5), fill=(226, 211, 174, 210))
        files.append(save(img, f"decor/{name}.png"))
    return files


def make_road() -> list[Path]:
    files = []
    specs = {
        "road_straight_ew": [(-18, 64), (146, 64)],
        "road_straight_ns": [(64, -18), (64, 146)],
        "road_corner_ne": [(64, -18), (64, 64), (146, 64)],
        "road_corner_es": [(146, 64), (64, 64), (64, 146)],
        "road_corner_sw": [(64, 146), (64, 64), (-18, 64)],
        "road_corner_wn": [(-18, 64), (64, 64), (64, -18)],
    }
    for name, points in specs.items():
        img = rgba((128, 128))
        d = ImageDraw.Draw(img)
        d.line(points, fill=(66, 46, 34, 230), width=44, joint="curve")
        d.line(points, fill=ROAD, width=36, joint="curve")
        d.line(points, fill=(153, 103, 56, 180), width=22, joint="curve")
        d.line(points, fill=(92, 63, 38, 120), width=3, joint="curve")
        for i in range(9):
            x = 14 + (i * 17) % 100
            y = 56 + ((i * 31) % 16)
            if name.endswith("_ns"):
                x, y = y, x
            d.ellipse((x, y, x + 3, y + 2), fill=(67, 46, 32, 90))
        files.append(save(img, f"road/{name}.png"))
    entry = rgba((128, 128))
    d = ImageDraw.Draw(entry)
    d.line([(64, 20), (64, 146)], fill=(66, 46, 34, 230), width=46)
    d.line([(64, 20), (64, 146)], fill=ROAD, width=36)
    d.polygon((28, 18, 100, 18, 84, 48, 44, 48), fill=(164, 112, 58, 245), outline=(78, 55, 36, 180))
    d.line((42, 28, 86, 28), fill=TEAL, width=3)
    files.append(save(entry, "road/road_entry_aul.png"))
    hl = rgba((128, 128))
    d = ImageDraw.Draw(hl)
    d.rounded_rectangle((22, 22, 106, 106), radius=12, outline=(255, 218, 92, 210), width=4)
    d.rounded_rectangle((30, 30, 98, 98), radius=9, outline=(112, 216, 147, 110), width=2)
    d.ellipse((48, 48, 80, 80), fill=(255, 205, 75, 45))
    files.append(save(hl, "road/road_current_highlight.png"))
    for name in ["buffer_edge_stones_01", "buffer_packed_sand_01", "buffer_stakes_01", "buffer_cart_marks_01"]:
        img = rgba((128, 128))
        d = ImageDraw.Draw(img)
        if name == "buffer_edge_stones_01":
            for x, y, r in [(18, 69, 5), (34, 61, 7), (51, 72, 4), (68, 62, 8), (88, 72, 5), (106, 61, 6)]:
                d.ellipse((x - r, y - r, x + r, y + r), fill=STONE, outline=(78, 58, 42, 185), width=1)
            d.line((15, 78, 113, 80), fill=(71, 51, 34, 90), width=2)
        elif name == "buffer_packed_sand_01":
            d.rounded_rectangle((10, 44, 118, 86), radius=13, fill=(171, 121, 62, 102))
            for y in [54, 64, 74]:
                d.line((18, y, 110, y - 4), fill=(91, 63, 37, 82), width=2)
        elif name == "buffer_stakes_01":
            for x, h in [(30, 35), (64, 42), (98, 32)]:
                d.rounded_rectangle((x - 4, 78 - h, x + 4, 82), radius=3, fill=(111, 69, 36, 210))
                d.polygon((x - 5, 78 - h, x + 5, 78 - h, x, 68 - h), fill=(148, 91, 45, 210))
                d.line((x - 2, 56, x + 9, 63), fill=TEAL_DARK, width=2)
        else:
            d.arc((7, 34, 121, 98), 8, 172, fill=(77, 55, 36, 112), width=3)
            d.arc((14, 43, 126, 106), 8, 172, fill=(77, 55, 36, 80), width=2)
            for x in [38, 58, 82]:
                d.ellipse((x, 72, x + 4, 77), fill=(77, 55, 36, 80))
        files.append(save(img, f"road/{name}.png"))
    return files


def make_aul_tiles_hero() -> list[Path]:
    files = []
    aul_ground = sand_texture((256, 256))
    d = ImageDraw.Draw(aul_ground)
    d.rounded_rectangle((42, 54, 214, 206), radius=26, fill=(176, 123, 62, 215), outline=(117, 77, 39, 130), width=4)
    d.ellipse((78, 78, 178, 178), outline=(129, 82, 42, 115), width=3)
    d.ellipse((110, 110, 146, 146), fill=(72, 48, 30, 220))
    for x, y in [(64, 78), (194, 88), (78, 190), (180, 185)]:
        d.rounded_rectangle((x - 4, y - 12, x + 4, y + 16), radius=3, fill=(97, 59, 31, 190))
    files.append(save(aul_ground, "aul/aul_ground_2x2.png"))
    for name, shift in [("aul_yurt_small_01", 0), ("aul_yurt_small_02", 7)]:
        img = rgba((128, 128))
        d = ImageDraw.Draw(img)
        d.ellipse((23 + shift, 78, 105 + shift, 101), fill=(42, 32, 22, 70))
        d.ellipse((28 + shift, 48, 100 + shift, 96), fill=CREAM, outline=(74, 55, 38, 230), width=3)
        d.arc((30 + shift, 22, 100 + shift, 88), 200, 340, fill=(74, 55, 38, 230), width=4)
        d.arc((38 + shift, 34, 92 + shift, 78), 204, 336, fill=(183, 129, 62, 170), width=2)
        d.rectangle((54 + shift, 72, 72 + shift, 96), fill=RED_TRIM)
        d.line((35 + shift, 63, 95 + shift, 63), fill=TEAL, width=3)
        draw_tamga_corner(d, 66 + shift, 30, 1, 1, RED_TRIM)
        files.append(save(img, f"aul/{name}.png"))
    fire = rgba((128, 128))
    d = ImageDraw.Draw(fire)
    d.ellipse((27, 64, 101, 108), fill=(239, 119, 35, 55))
    d.ellipse((35, 78, 94, 98), fill=(60, 40, 25, 190))
    d.polygon((58, 80, 66, 38, 77, 80), fill=FIRE)
    d.polygon((47, 82, 61, 55, 69, 84), fill=(255, 194, 71, 238))
    d.polygon((65, 82, 75, 58, 82, 84), fill=(255, 225, 115, 220))
    d.line((38, 90, 92, 76), fill=(91, 52, 31, 255), width=5)
    d.line((42, 74, 88, 92), fill=(91, 52, 31, 255), width=5)
    files.append(save(fire, "aul/aul_fire_01.png"))

    for name, dx, dy in [
        ("hero_wayfarer_idle_s", 0, 0),
        ("hero_wayfarer_walk_s", 0, 2),
        ("hero_wayfarer_walk_e", 4, 0),
        ("hero_wayfarer_walk_n", 0, -2),
        ("hero_wayfarer_walk_w", -4, 0),
    ]:
        img = rgba((128, 128))
        d = ImageDraw.Draw(img)
        d.ellipse((42 + dx, 101 + dy, 88 + dx, 116 + dy), fill=(18, 15, 12, 85))
        d.ellipse((52 + dx, 22 + dy, 76 + dx, 46 + dy), fill=(196, 129, 76, 255), outline=INK, width=2)
        d.polygon((47 + dx, 41 + dy, 82 + dx, 41 + dy, 74 + dx, 62 + dy, 54 + dx, 62 + dy), fill=(42, 31, 24, 255))
        d.rounded_rectangle((45 + dx, 49 + dy, 83 + dx, 93 + dy), radius=8, fill=(45, 91, 86, 255), outline=INK, width=2)
        d.polygon((45 + dx, 55 + dy, 29 + dx, 80 + dy, 47 + dx, 84 + dy), fill=(79, 62, 45, 255), outline=INK)
        d.polygon((83 + dx, 55 + dy, 99 + dx, 80 + dy, 81 + dx, 84 + dy), fill=(79, 62, 45, 255), outline=INK)
        d.line((55 + dx, 93 + dy, 47 + dx, 114 + dy), fill=(50, 41, 35, 255), width=6)
        d.line((74 + dx, 93 + dy, 82 + dx, 114 + dy), fill=(50, 41, 35, 255), width=6)
        d.line((50 + dx, 65 + dy, 78 + dx, 65 + dy), fill=RED_TRIM, width=3)
        d.line((61 + dx, 49 + dy, 72 + dx, 93 + dy), fill=TEAL, width=3)
        files.append(save(img, f"hero/{name}.png"))
    panel = rgba((128, 192))
    d = ImageDraw.Draw(panel)
    d.ellipse((35, 170, 93, 188), fill=(18, 15, 12, 90))
    d.ellipse((49, 18, 79, 48), fill=(196, 129, 76, 255), outline=INK, width=2)
    d.polygon((41, 43, 88, 43, 78, 72, 52, 72), fill=(42, 31, 24, 255))
    d.rounded_rectangle((39, 60, 89, 132), radius=11, fill=(45, 91, 86, 255), outline=INK, width=3)
    d.polygon((39, 70, 20, 122, 41, 131), fill=(79, 62, 45, 255), outline=INK)
    d.polygon((89, 70, 108, 122, 87, 131), fill=(79, 62, 45, 255), outline=INK)
    d.line((54, 132, 46, 176), fill=(50, 41, 35, 255), width=8)
    d.line((74, 132, 82, 176), fill=(50, 41, 35, 255), width=8)
    d.line((44, 83, 84, 83), fill=RED_TRIM, width=4)
    d.line((60, 62, 75, 132), fill=TEAL, width=4)
    files.append(save(panel, "hero/hero_wayfarer_panel.png"))
    return files


def make_active_tiles() -> list[Path]:
    files = []
    for name in [
        "tile_saxaul_01",
        "tile_yurt_01",
        "tile_tamga_stone_01",
        "tile_wolf_track_01",
        "tile_oasis_01",
        "tile_mirage_01",
        "tile_storm_01",
        "tile_last_tamga_01",
    ]:
        img = rgba((128, 128))
        d = ImageDraw.Draw(img)
        if name == "tile_saxaul_01":
            d.ellipse((22, 88, 106, 108), fill=(62, 45, 30, 90))
            for x0, y0, x1, y1 in [(30, 88, 78, 58), (42, 92, 96, 70), (52, 88, 34, 60), (70, 90, 92, 55)]:
                d.line((x0, y0, x1, y1), fill=(91, 59, 35, 255), width=5)
            for x, y in [(28, 88), (48, 82), (76, 84), (96, 90)]:
                d.polygon((x, y, x + 8, y - 18, x + 16, y), fill=(160, 128, 47, 220))
        elif name == "tile_yurt_01":
            d.ellipse((28, 52, 100, 98), fill=CREAM, outline=(91, 67, 45, 220), width=3)
            d.arc((31, 27, 99, 88), 200, 340, fill=(91, 67, 45, 220), width=3)
            d.rectangle((55, 74, 72, 98), fill=RED_TRIM)
        elif name == "tile_tamga_stone_01":
            d.polygon((46, 102, 54, 34, 83, 26, 98, 104), fill=(146, 124, 91, 255), outline=(82, 65, 48, 255))
            d.line((60, 64, 80, 48), fill=TEAL, width=5)
            d.line((68, 52, 78, 82), fill=TEAL, width=5)
        elif name == "tile_wolf_track_01":
            for x, y in [(42, 62), (62, 78)]:
                d.ellipse((x, y, x + 18, y + 24), fill=(76, 56, 42, 230))
                for ox, oy in [(-6, -8), (5, -12), (16, -7)]:
                    d.ellipse((x + ox, y + oy, x + ox + 8, y + oy + 11), fill=(76, 56, 42, 230))
            d.line((78, 86, 108, 98), fill=(95, 40, 37, 180), width=4)
        elif name == "tile_oasis_01":
            d.ellipse((28, 58, 100, 96), fill=(57, 145, 159, 220), outline=(33, 91, 96, 255), width=3)
            for x, y in [(28, 76), (82, 68), (68, 96)]:
                d.line((x, y, x + 8, y - 18), fill=(67, 123, 60, 230), width=4)
        elif name == "tile_mirage_01":
            for y in [50, 60, 71, 83]:
                d.arc((20, y - 12, 108, y + 16), 6, 174, fill=(160, 206, 214, 130), width=4)
            d.line((34, 78, 96, 70), fill=(226, 210, 167, 150), width=4)
        elif name == "tile_storm_01":
            for r in [50, 38, 26]:
                d.arc((64 - r, 64 - r, 64 + r, 64 + r), 210, 520, fill=(205, 172, 117, 220), width=7)
            d.ellipse((42, 92, 90, 108), fill=(109, 79, 49, 100))
        else:
            d.rounded_rectangle((42, 58, 92, 92), radius=8, fill=(190, 171, 128, 255), outline=(82, 65, 48, 255), width=3)
            d.line((55, 72, 78, 86), fill=TEAL, width=5)
            d.line((72, 68, 58, 90), fill=TEAL, width=5)
        files.append(save(img, f"tiles/{name}.png"))
    return files


def make_preview(paths: list[Path]) -> None:
    thumbs: list[tuple[str, Image.Image]] = []
    for path in paths:
        img = Image.open(path).convert("RGBA")
        thumb = Image.new("RGBA", (128, 128), (42, 38, 31, 255))
        img.thumbnail((112, 112), Image.Resampling.LANCZOS)
        thumb.alpha_composite(img, ((128 - img.width) // 2, (128 - img.height) // 2))
        thumbs.append((path.stem, thumb))
    cols = 6
    rows = math.ceil(len(thumbs) / cols)
    sheet = Image.new("RGBA", (cols * 160, rows * 156), (232, 224, 207, 255))
    d = ImageDraw.Draw(sheet)
    for i, (name, thumb) in enumerate(thumbs):
        x = (i % cols) * 160
        y = (i // cols) * 156
        sheet.alpha_composite(thumb, (x + 16, y + 8))
        d.text((x + 8, y + 134), name[:22], fill=(36, 31, 26, 255))
    sheet.save(CONCEPT / "batch_a_runtime_preview.png")


def write_manifest(paths: list[Path]) -> None:
    lines = [
        "# Batch A Runtime Placeholder Manifest",
        "",
        "These files are runtime-ready placeholder PNGs generated from `gamedesign/tools/generate_batch_a_assets.py`.",
        "They validate the atlas/runtime pipeline and can be replaced by higher-fidelity art with the same filenames.",
        "",
        "## Files",
        "",
    ]
    for path in sorted(paths):
        rel = path.relative_to(ROOT).as_posix()
        with Image.open(path) as img:
            mode = img.mode
            size = f"{img.width}x{img.height}"
        lines.append(f"- `{rel}` — {size}, {mode}")
    lines.extend(
        [
            "",
            "## Preview",
            "",
            "`gamedesign/assets/concept/production_batch_a/batch_a_runtime_preview.png`",
            "",
        ]
    )
    (CONCEPT / "batch_a_runtime_manifest.md").write_text("\n".join(lines), encoding="utf-8")


def main() -> None:
    ensure_dirs()
    paths: list[Path] = []
    paths.extend(make_ui())
    paths.extend(make_ground_decor())
    paths.extend(make_road())
    paths.extend(make_aul_tiles_hero())
    paths.extend(make_active_tiles())
    make_preview(paths)
    write_manifest(paths)
    print(f"Generated {len(paths)} Batch A PNG files")
    print(f"Preview: {CONCEPT / 'batch_a_runtime_preview.png'}")


if __name__ == "__main__":
    main()
