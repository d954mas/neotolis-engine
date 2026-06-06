from PIL import Image, ImageDraw
from pathlib import Path
import math
import random

out = Path("gamedesign/assets/concept/production_batch_a")
out.mkdir(parents=True, exist_ok=True)

UI_PANEL = (21, 28, 42, 232)
EDGE_WARM = (123, 90, 50, 255)
EDGE_DIM = (52, 59, 74, 255)
FIRE_GOLD = (244, 201, 93, 255)
TAMGA = (53, 184, 166, 255)
CLAN_RED = (201, 90, 58, 255)
VALID = (154, 214, 111, 255)
CARD_LIGHT = (231, 215, 184, 255)
SAND = (216, 181, 107, 255)
SAND_SHADOW = (169, 120, 60, 255)
ROAD = (154, 109, 58, 220)
ROAD_DARK = (122, 83, 45, 210)
STONE = (151, 126, 88, 255)
STONE_L = (193, 164, 111, 255)
WOOD = (95, 58, 36, 255)
GRASS = (148, 111, 43, 255)

random.seed(7)


def save(im, name):
    im.save(out / name)


def rounded_rect(draw, box, r, fill, outline=None, width=1):
    draw.rounded_rectangle(box, radius=r, fill=fill, outline=outline, width=width)


def add_noise(im, alpha=10, count=250):
    d = ImageDraw.Draw(im, "RGBA")
    w, h = im.size
    for _ in range(count):
        x = random.randrange(w)
        y = random.randrange(h)
        c = random.choice([(255, 255, 255, alpha), (0, 0, 0, alpha), (190, 140, 70, alpha)])
        d.point((x, y), fill=c)


def draw_stitches(d, box, color, step=8, size=3):
    x0, y0, x1, y1 = box
    for x in range(x0 + 8, x1 - 8, step):
        d.line((x, y0, x + size, y0), fill=color, width=1)
        d.line((x, y1, x + size, y1), fill=color, width=1)
    for y in range(y0 + 8, y1 - 8, step):
        d.line((x0, y, x0, y + size), fill=color, width=1)
        d.line((x1, y, x1, y + size), fill=color, width=1)


def draw_corner_marks(d, w, h, color=TAMGA):
    for cx, cy in [(12, 12), (w - 12, 12), (12, h - 12), (w - 12, h - 12)]:
        d.line((cx - 4, cy, cx, cy - 4, cx + 4, cy), fill=color, width=2)
        d.line((cx, cy - 4, cx, cy + 4), fill=color, width=1)


def draw_tamga(d, cx, cy, scale, color=TAMGA, width=2):
    s = scale
    d.line((cx, cy - s, cx, cy + s), fill=color, width=width)
    d.line((cx, cy - s // 2, cx - s // 2, cy - s), fill=color, width=width)
    d.line((cx, cy - s // 2, cx + s // 2, cy - s), fill=color, width=width)
    d.line((cx, cy, cx - s // 2, cy + s // 3), fill=color, width=width)
    d.line((cx, cy, cx + s // 2, cy + s // 3), fill=color, width=width)
    d.line((cx, cy + s, cx - s // 3, cy + s // 2), fill=color, width=width)
    d.line((cx, cy + s, cx + s // 3, cy + s // 2), fill=color, width=width)


def sand_bg(size):
    im = Image.new("RGBA", size, SAND)
    d = ImageDraw.Draw(im, "RGBA")
    w, h = size
    for _ in range(160):
        x = random.randrange(w)
        y = random.randrange(h)
        r = random.choice([1, 1, 2])
        col = random.choice([(232, 204, 134, 25), (169, 120, 60, 24), (120, 80, 40, 18)])
        d.ellipse((x - r, y - r, x + r, y + r), fill=col)
    return im


# UI 9-slice pieces.
im = Image.new("RGBA", (96, 96), (0, 0, 0, 0))
d = ImageDraw.Draw(im, "RGBA")
rounded_rect(d, (3, 3, 92, 92), 8, UI_PANEL, EDGE_WARM, 2)
rounded_rect(d, (8, 8, 87, 87), 5, (14, 20, 31, 210), EDGE_DIM, 1)
draw_stitches(d, (7, 7, 88, 88), (182, 113, 57, 160))
draw_corner_marks(d, 96, 96, (41, 136, 126, 170))
add_noise(im, 5, 180)
save(im, "ui_panel_felt_dark_96.png")

im = Image.new("RGBA", (96, 96), (0, 0, 0, 0))
d = ImageDraw.Draw(im, "RGBA")
rounded_rect(d, (3, 3, 92, 92), 8, (45, 36, 28, 235), FIRE_GOLD, 2)
rounded_rect(d, (9, 9, 86, 86), 5, (55, 43, 31, 218), EDGE_WARM, 1)
draw_stitches(d, (8, 8, 87, 87), (238, 180, 79, 150))
draw_corner_marks(d, 96, 96, TAMGA)
add_noise(im, 6, 150)
save(im, "ui_panel_felt_light_96.png")

for selected, name in [(False, "ui_card_playable_96x128.png"), (True, "ui_card_selected_96x128.png")]:
    im = Image.new("RGBA", (96, 128), (0, 0, 0, 0))
    d = ImageDraw.Draw(im, "RGBA")
    edge = VALID if selected else EDGE_WARM
    rounded_rect(d, (3, 3, 92, 124), 8, CARD_LIGHT, edge, 3 if selected else 2)
    rounded_rect(d, (9, 9, 86, 118), 4, (221, 198, 155, 245), (146, 100, 55, 180), 1)
    rounded_rect(d, (15, 16, 80, 70), 4, (196, 158, 92, 80), (143, 100, 55, 130), 1)
    d.line((14, 82, 81, 82), fill=(126, 80, 44, 130), width=1)
    d.line((18, 104, 77, 104), fill=(126, 80, 44, 100), width=1)
    draw_corner_marks(d, 96, 128, TAMGA if selected else CLAN_RED)
    d.polygon([(10, 10), (25, 10), (25, 25), (10, 25)], fill=(72, 120, 60, 230), outline=(235, 215, 155, 255))
    add_noise(im, 5, 100)
    save(im, name)

im = Image.new("RGBA", (64, 64), (0, 0, 0, 0))
d = ImageDraw.Draw(im, "RGBA")
rounded_rect(d, (4, 4, 59, 59), 7, (17, 22, 31, 230), EDGE_WARM, 2)
rounded_rect(d, (10, 10, 53, 53), 4, (8, 12, 20, 180), EDGE_DIM, 1)
draw_tamga(d, 32, 32, 14, (73, 92, 101, 160), 2)
add_noise(im, 4, 80)
save(im, "ui_slot_equipment_64.png")

im = Image.new("RGBA", (64, 64), (0, 0, 0, 0))
d = ImageDraw.Draw(im, "RGBA")
rounded_rect(d, (4, 8, 59, 56), 7, (20, 27, 39, 235), EDGE_WARM, 2)
rounded_rect(d, (9, 13, 54, 51), 5, (24, 31, 45, 210), EDGE_DIM, 1)
draw_stitches(d, (8, 12, 55, 52), (182, 113, 57, 140), step=7, size=2)
save(im, "ui_chip_resource_64.png")

im = Image.new("RGBA", (64, 64), (0, 0, 0, 0))
d = ImageDraw.Draw(im, "RGBA")
rounded_rect(d, (4, 8, 59, 56), 7, (13, 17, 26, 238), EDGE_WARM, 2)
d.line((14, 14, 50, 14), fill=TAMGA, width=1)
draw_corner_marks(d, 64, 64, (43, 147, 135, 180))
add_noise(im, 4, 50)
save(im, "ui_tooltip_dark_64.png")

# Ground and decor.
save(sand_bg((128, 128)).convert("RGB"), "ground_sand_base_01.png")

for name in ["decor_dune_01.png", "decor_stones_01.png", "decor_dry_grass_01.png", "decor_tracks_01.png", "decor_bones_01.png", "decor_cracks_01.png"]:
    im = Image.new("RGBA", (128, 128), (0, 0, 0, 0))
    d = ImageDraw.Draw(im, "RGBA")
    if name.startswith("decor_dune"):
        d.polygon([(15, 80), (46, 52), (92, 47), (115, 72), (103, 87), (54, 89)], fill=(229, 192, 100, 88))
        d.arc((24, 42, 110, 112), 200, 330, fill=(160, 105, 50, 70), width=4)
    elif name.startswith("decor_stones"):
        for cx, cy, rx, ry in [(46, 66, 11, 7), (66, 58, 7, 5), (79, 73, 9, 6), (55, 83, 5, 4)]:
            d.ellipse((cx - rx, cy - ry, cx + rx, cy + ry), fill=STONE, outline=(89, 70, 50, 180), width=1)
            d.arc((cx - rx + 2, cy - ry + 1, cx + rx - 1, cy + ry), 200, 330, fill=STONE_L, width=2)
    elif name.startswith("decor_dry"):
        for bx, by in [(44, 79), (61, 74), (78, 83)]:
            for a in [-35, -18, 0, 18, 35]:
                length = random.randint(14, 23)
                rad = math.radians(270 + a)
                d.line((bx, by, bx + math.cos(rad) * length, by + math.sin(rad) * length), fill=GRASS, width=3)
    elif name.startswith("decor_tracks"):
        for i in range(9):
            x = 34 + i * 7
            y = 42 + i * 5
            d.ellipse((x, y, x + 7, y + 3), fill=(99, 67, 39, 75))
            d.ellipse((x + 18, y - 4, x + 25, y - 1), fill=(99, 67, 39, 65))
    elif name.startswith("decor_bones"):
        for x, y, a in [(45, 68, -20), (66, 76, 18), (82, 61, 35), (56, 91, -12)]:
            d.line((x - 9, y, x + 9, y), fill=(218, 202, 158, 210), width=4)
            d.ellipse((x - 13, y - 4, x - 6, y + 3), fill=(218, 202, 158, 210))
            d.ellipse((x + 6, y - 4, x + 13, y + 3), fill=(218, 202, 158, 210))
            d.line((x - 9, y + 1, x + 9, y + 1), fill=(126, 98, 66, 90), width=1)
    elif name.startswith("decor_cracks"):
        for pts in [[(25, 72), (47, 64), (60, 70), (77, 58), (101, 62)], [(50, 64), (48, 42)], [(77, 58), (86, 38)], [(60, 70), (62, 92), (75, 103)], [(47, 64), (33, 51)]]:
            d.line(pts, fill=(101, 68, 39, 115), width=3)
    save(im, name)


def draw_road_straight(horizontal=True):
    im = Image.new("RGBA", (128, 128), (0, 0, 0, 0))
    d = ImageDraw.Draw(im, "RGBA")
    if horizontal:
        d.rounded_rectangle((0, 42, 128, 86), radius=16, fill=ROAD)
        d.line((0, 54, 128, 54), fill=(207, 160, 89, 60), width=2)
        d.line((0, 75, 128, 75), fill=ROAD_DARK, width=2)
    else:
        d.rounded_rectangle((42, 0, 86, 128), radius=16, fill=ROAD)
        d.line((54, 0, 54, 128), fill=(207, 160, 89, 60), width=2)
        d.line((75, 0, 75, 128), fill=ROAD_DARK, width=2)
    return im


save(draw_road_straight(True), "road_straight_ew.png")
save(draw_road_straight(False), "road_straight_ns.png")


def draw_corner(kind):
    im = Image.new("RGBA", (128, 128), (0, 0, 0, 0))
    d = ImageDraw.Draw(im, "RGBA")
    if kind == "ne":
        box = (42, -42, 170, 86)
        start, end = 90, 180
    elif kind == "es":
        box = (42, 42, 170, 170)
        start, end = 180, 270
    elif kind == "sw":
        box = (-42, 42, 86, 170)
        start, end = 270, 360
    else:
        box = (-42, -42, 86, 86)
        start, end = 0, 90
    for width, col in [(46, ROAD), (30, (143, 99, 54, 210)), (6, (210, 160, 90, 65))]:
        d.arc(box, start, end, fill=col, width=width)
    return im


for key, name in [("ne", "road_corner_ne.png"), ("es", "road_corner_es.png"), ("sw", "road_corner_sw.png"), ("wn", "road_corner_wn.png")]:
    save(draw_corner(key), name)

im = Image.new("RGBA", (128, 128), (0, 0, 0, 0))
d = ImageDraw.Draw(im, "RGBA")
for x in range(14, 116, 17):
    y = 68 + random.randint(-5, 5)
    r = random.randint(4, 8)
    d.ellipse((x - r, y - r, x + r, y + r), fill=STONE, outline=(80, 60, 45, 160), width=1)
    if random.random() < 0.35:
        d.rounded_rectangle((x - 2, y - 24, x + 3, y - 4), radius=2, fill=(117, 74, 38, 230), outline=(70, 45, 28, 180))
save(im, "buffer_edge_stones_01.png")

im = Image.new("RGBA", (128, 128), (0, 0, 0, 0))
d = ImageDraw.Draw(im, "RGBA")
d.rounded_rectangle((0, 44, 128, 84), radius=14, fill=(184, 139, 76, 90))
for i in range(7):
    d.arc((5 + i * 18, 45, 45 + i * 18, 88), 190, 335, fill=(125, 85, 46, 45), width=2)
save(im, "buffer_packed_sand_01.png")

im = sand_bg((256, 256)).convert("RGBA")
d = ImageDraw.Draw(im, "RGBA")
d.ellipse((36, 42, 220, 214), fill=(159, 125, 75, 125), outline=(118, 85, 48, 90), width=3)
for r in [38, 74, 110]:
    d.ellipse((128 - r, 128 - r, 128 + r, 128 + r), outline=(125, 88, 48, 45), width=2)
save(im.convert("RGB"), "aul_ground_2x2.png")

im = Image.new("RGBA", (128, 128), (0, 0, 0, 0))
d = ImageDraw.Draw(im, "RGBA")
d.ellipse((28, 42, 100, 101), fill=(216, 204, 181, 255), outline=(99, 70, 45, 255), width=3)
d.pieslice((28, 20, 100, 88), 180, 360, fill=(232, 222, 199, 255), outline=(99, 70, 45, 255), width=3)
d.rectangle((55, 74, 73, 102), fill=(121, 63, 39, 255))
d.line((32, 70, 96, 70), fill=CLAN_RED, width=4)
d.line((64, 24, 64, 90), fill=(98, 79, 57, 180), width=1)
save(im, "aul_yurt_small_01.png")

im = Image.new("RGBA", (128, 128), (0, 0, 0, 0))
d = ImageDraw.Draw(im, "RGBA")
d.ellipse((39, 91, 89, 105), fill=(52, 35, 20, 140))
for x in [50, 60, 70, 80]:
    d.line((x, 92, 64, 58), fill=WOOD, width=5)
for poly, col in [
    ([(56, 88), (65, 42), (72, 88)], (231, 80, 35, 235)),
    ([(61, 88), (68, 51), (78, 88)], (248, 163, 50, 235)),
    ([(50, 88), (59, 62), (66, 88)], (255, 217, 83, 220)),
]:
    d.polygon(poly, fill=col)
save(im, "aul_fire_01.png")

im = Image.new("RGBA", (128, 128), (0, 0, 0, 0))
d = ImageDraw.Draw(im, "RGBA")
d.ellipse((32, 82, 99, 101), fill=(70, 42, 25, 75))
for x0, y0, x1, y1 in [(38, 78, 69, 70), (45, 83, 88, 73), (36, 88, 77, 82), (58, 91, 99, 84), (45, 75, 29, 66), (75, 72, 92, 62)]:
    d.line((x0, y0, x1, y1), fill=WOOD, width=6)
    d.line((x0, y0, x1, y1), fill=(136, 86, 50, 210), width=2)
for bx, by in [(42, 91), (61, 89), (84, 92)]:
    for a in [-35, -15, 15, 35]:
        rad = math.radians(270 + a)
        length = 17
        d.line((bx, by, bx + math.cos(rad) * length, by + math.sin(rad) * length), fill=GRASS, width=3)
save(im, "tile_saxaul_01.png")

im = Image.new("RGBA", (128, 128), (0, 0, 0, 0))
d = ImageDraw.Draw(im, "RGBA")
d.ellipse((48, 104, 82, 113), fill=(0, 0, 0, 75))
d.polygon([(58, 42), (76, 52), (83, 101), (50, 101), (53, 57)], fill=(33, 38, 42, 255), outline=(12, 15, 19, 220))
d.ellipse((57, 30, 73, 49), fill=(74, 50, 34, 255))
d.polygon([(53, 47), (76, 48), (71, 70), (56, 68)], fill=(42, 76, 82, 255))
d.line((78, 52, 90, 102), fill=(82, 54, 34, 255), width=4)
d.line((56, 99, 50, 115), fill=(57, 35, 25, 255), width=5)
d.line((75, 99, 80, 115), fill=(57, 35, 25, 255), width=5)
save(im, "hero_wayfarer_idle_s.png")

(out / "MANIFEST.md").write_text(
    """# Production Batch A Asset Manifest

All files are individual PNGs. No crop coordinates needed.

UI slice9:
- ui_panel_felt_dark_96.png: 96x96, slice 24
- ui_panel_felt_light_96.png: 96x96, slice 24
- ui_card_playable_96x128.png: 96x128, slice 18
- ui_card_selected_96x128.png: 96x128, slice 18
- ui_slot_equipment_64.png: 64x64, slice 14
- ui_chip_resource_64.png: 64x64, slice 16
- ui_tooltip_dark_64.png: 64x64, slice 16

World/map P0:
- ground_sand_base_01.png: 128x128 opaque
- decor_*.png: 128x128 transparent
- road_*.png: 128x128 transparent
- buffer_*.png: 128x128 transparent
- aul_ground_2x2.png: 256x256 opaque
- aul_yurt_small_01.png: 128x128 transparent
- aul_fire_01.png: 128x128 transparent
- tile_saxaul_01.png: 128x128 transparent
- hero_wayfarer_idle_s.png: 128x128 transparent
""",
    encoding="utf-8",
)

print("wrote", len(list(out.glob("*.png"))), "png files to", out)
