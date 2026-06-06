from pathlib import Path

from PIL import Image, ImageDraw


ROOT = Path(__file__).resolve().parents[2]
RAW = ROOT / "games" / "turkic-jam-2026" / "raw"
OUT = ROOT / "gamedesign" / "assets" / "concept" / "final_repaint_pass_2"
S = 4

INK = (18, 15, 12, 255)
FELT = (43, 101, 95, 255)
FELT_DARK = (18, 28, 31, 246)
CLOAK = (79, 62, 45, 255)
WOOD = (95, 58, 32, 255)
SKIN = (204, 133, 77, 255)
TEAL = (44, 169, 157, 255)
RED = (156, 52, 38, 255)
GOLD = (245, 188, 72, 255)
GREEN = (133, 207, 94, 255)
BONE = (226, 209, 166, 255)
EDGE = (127, 87, 44, 255)
SHADOW = (0, 0, 0, 88)


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


def tamga(d: ImageDraw.ImageDraw, cx: int, cy: int, s: int, color=TEAL, width: int = 2) -> None:
    cx, cy, s, width = sc(cx), sc(cy), sc(s), sc(width)
    d.line((cx, cy - s, cx, cy + s), fill=color, width=width)
    d.line((cx - s, cy - s // 2, cx, cy - s, cx + s, cy - s // 2), fill=color, width=width)
    d.line((cx - s, cy + s // 2, cx, cy, cx + s, cy + s // 2), fill=color, width=width)


def hero_128(direction: str) -> Image.Image:
    size = (128, 128)
    img, d = canvas(size)
    offset = {"s": (0, 0), "e": (4, 0), "n": (0, -3), "w": (-4, 0)}[direction]
    dx, dy = offset
    d.ellipse(box((41 + dx, 104 + dy, 88 + dx, 119 + dy)), fill=SHADOW)
    head_y = 22 + dy
    d.ellipse(box((51 + dx, head_y, 77 + dx, head_y + 26)), fill=SKIN, outline=INK, width=sc(2))
    d.polygon(pts([(45 + dx, 42 + dy), (83 + dx, 42 + dy), (75 + dx, 63 + dy), (53 + dx, 63 + dy)]), fill=(44, 31, 24, 255), outline=INK)
    d.rounded_rectangle(box((43 + dx, 50 + dy, 85 + dx, 94 + dy)), radius=sc(9), fill=FELT, outline=INK, width=sc(2))
    d.line(pts([(51 + dx, 66 + dy), (80 + dx, 66 + dy)]), fill=RED, width=sc(3))
    d.line(pts([(61 + dx, 51 + dy), (72 + dx, 94 + dy)]), fill=TEAL, width=sc(3))
    arm_shift = 5 if direction == "e" else -5 if direction == "w" else 0
    d.polygon(pts([(43 + dx, 56 + dy), (27 + dx + arm_shift, 81 + dy), (46 + dx, 86 + dy)]), fill=CLOAK, outline=INK)
    d.polygon(pts([(85 + dx, 56 + dy), (101 + dx + arm_shift, 81 + dy), (82 + dx, 86 + dy)]), fill=CLOAK, outline=INK)
    leg_swing = 5 if direction in ["s", "e"] else -5
    d.line(pts([(55 + dx, 94 + dy), (47 + dx - leg_swing, 115)]), fill=INK, width=sc(7))
    d.line(pts([(74 + dx, 94 + dy), (82 + dx + leg_swing, 115)]), fill=INK, width=sc(7))
    d.line(pts([(55 + dx, 94 + dy), (48 + dx - leg_swing, 114)]), fill=(53, 42, 34, 255), width=sc(5))
    d.line(pts([(74 + dx, 94 + dy), (81 + dx + leg_swing, 114)]), fill=(53, 42, 34, 255), width=sc(5))
    staff_x = 96 + dx if direction != "w" else 30 + dx
    d.line(pts([(staff_x, 48 + dy), (staff_x, 112)]), fill=INK, width=sc(4))
    d.line(pts([(staff_x, 48 + dy), (staff_x, 112)]), fill=WOOD, width=sc(2))
    return down(img, size)


def hero_panel() -> Image.Image:
    size = (160, 220)
    img, d = canvas(size)
    d.ellipse(box((45, 193, 115, 214)), fill=SHADOW)
    d.ellipse(box((64, 22, 96, 54)), fill=SKIN, outline=INK, width=sc(2))
    d.polygon(pts([(54, 49), (106, 49), (94, 80), (66, 80)]), fill=(44, 31, 24, 255), outline=INK)
    d.rounded_rectangle(box((49, 72, 111, 154)), radius=sc(13), fill=FELT, outline=INK, width=sc(3))
    d.line(pts([(56, 98), (104, 98)]), fill=RED, width=sc(5))
    d.line(pts([(72, 74), (91, 154)]), fill=TEAL, width=sc(5))
    d.polygon(pts([(49, 84), (25, 143), (51, 154)]), fill=CLOAK, outline=INK)
    d.polygon(pts([(111, 84), (135, 143), (109, 154)]), fill=CLOAK, outline=INK)
    d.line(pts([(66, 154), (54, 204)]), fill=INK, width=sc(9))
    d.line(pts([(94, 154), (106, 204)]), fill=INK, width=sc(9))
    d.line(pts([(126, 55), (126, 207)]), fill=INK, width=sc(5))
    d.line(pts([(126, 55), (126, 207)]), fill=WOOD, width=sc(3))
    tamga(d, 80, 115, 12, GOLD, 3)
    return down(img, size)


def slot(kind: str) -> Image.Image:
    size = (64, 64)
    img, d = canvas(size)
    d.rounded_rectangle(box((5, 5, 59, 59)), radius=sc(9), fill=FELT_DARK, outline=INK, width=sc(2))
    d.rounded_rectangle(box((10, 10, 54, 54)), radius=sc(7), outline=EDGE, width=sc(2))
    color = (190, 174, 136, 120)
    draw_equipment_symbol(d, kind, color, slot=True)
    return down(img, size)


def draw_equipment_symbol(d: ImageDraw.ImageDraw, kind: str, color, slot=False) -> None:
    if kind == "weapon":
        d.line(pts([(18, 51), (48, 13)]), fill=INK if not slot else color, width=sc(6))
        d.line(pts([(18, 51), (48, 13)]), fill=color, width=sc(4))
        d.line(pts([(38, 16), (51, 23)]), fill=color, width=sc(3))
    elif kind == "clothes":
        d.polygon(pts([(22, 16), (42, 16), (53, 55), (11, 55)]), fill=color, outline=INK if not slot else color)
        d.line(pts([(22, 27), (42, 27)]), fill=RED if not slot else color, width=sc(3))
    elif kind == "tool":
        d.rounded_rectangle(box((17, 24, 47, 54)), radius=sc(6), fill=color, outline=INK if not slot else color, width=sc(2))
        d.arc(box((20, 9, 44, 33)), 180, 360, fill=color, width=sc(4))
        if not slot:
            d.polygon(pts([(21, 28), (43, 28), (39, 37), (25, 37)]), fill=(178, 119, 58, 200))
    else:
        d.ellipse(box((14, 13, 50, 51)), outline=color, width=sc(4))
        tamga(d, 32, 32, 13, TEAL if not slot else color, 4)


def equipment_item(kind: str) -> Image.Image:
    size = (64, 64)
    img, d = canvas(size)
    d.ellipse(box((13, 50, 51, 58)), fill=SHADOW)
    draw_equipment_symbol(d, kind, BONE, slot=False)
    if kind == "weapon":
        d.ellipse(box((42, 8, 54, 20)), fill=GOLD, outline=INK, width=sc(2))
    return down(img, size)


def icon(kind: str) -> Image.Image:
    size = (32, 32)
    img, d = canvas(size)
    if kind == "body":
        d.ellipse(box((10, 3, 22, 15)), fill=INK)
        d.rounded_rectangle(box((7, 13, 25, 29)), radius=sc(6), fill=INK)
        d.ellipse(box((11, 5, 21, 14)), fill=BONE)
        d.rounded_rectangle(box((9, 14, 23, 28)), radius=sc(5), fill=BONE)
    elif kind == "mind":
        d.ellipse(box((5, 7, 27, 25)), outline=INK, width=sc(4))
        d.ellipse(box((7, 8, 25, 24)), outline=TEAL, width=sc(3))
        d.arc(box((10, 11, 22, 24)), 180, 350, fill=TEAL, width=sc(3))
    elif kind == "spirit":
        d.polygon(pts([(16, 3), (27, 16), (16, 29), (5, 16)]), fill=INK)
        d.polygon(pts([(16, 5), (25, 16), (16, 27), (7, 16)]), fill=(44, 169, 157, 105), outline=TEAL)
    elif kind == "circle":
        d.ellipse(box((5, 5, 27, 27)), outline=INK, width=sc(5))
        d.ellipse(box((7, 7, 25, 25)), outline=GOLD, width=sc(4))
    elif kind == "day":
        d.ellipse(box((9, 9, 23, 23)), fill=GOLD, outline=INK, width=sc(2))
        for a, b in [((16, 2), (16, 7)), ((16, 25), (16, 30)), ((2, 16), (7, 16)), ((25, 16), (30, 16)), ((6, 6), (10, 10)), ((22, 22), (26, 26)), ((26, 6), (22, 10)), ((10, 22), (6, 26))]:
            d.line(pts([a, b]), fill=GOLD, width=sc(3))
    elif kind == "speed":
        d.polygon(pts([(5, 5), (19, 16), (5, 27)]), fill=INK)
        d.polygon(pts([(16, 5), (30, 16), (16, 27)]), fill=INK)
        d.polygon(pts([(7, 7), (19, 16), (7, 25)]), fill=BONE)
        d.polygon(pts([(18, 7), (30, 16), (18, 25)]), fill=BONE)
    else:
        tamga(d, 16, 16, 12, INK, 5)
        tamga(d, 16, 16, 11, TEAL, 3)
    return down(img, size)


def card_utility(kind: str) -> Image.Image:
    size = (32, 32)
    img, d = canvas(size)
    d.rounded_rectangle(box((2, 2, 30, 30)), radius=sc(8), fill=FELT_DARK, outline=INK, width=sc(2))
    d.rounded_rectangle(box((5, 5, 27, 27)), radius=sc(6), outline=EDGE, width=sc(2))
    if kind == "count":
        d.ellipse(box((10, 7, 22, 19)), fill=GOLD, outline=INK, width=sc(1))
        d.rectangle(box((14, 16, 18, 25)), fill=GOLD, outline=INK)
    elif kind == "roadside":
        d.line(pts([(5, 20), (27, 20)]), fill=(91, 62, 36, 255), width=sc(8))
        d.line(pts([(5, 20), (27, 20)]), fill=(218, 176, 89, 255), width=sc(5))
        for x in [8, 16, 24]:
            d.ellipse(box((x - 2, 8, x + 2, 12)), fill=BONE)
    elif kind == "field":
        d.rectangle(box((7, 7, 25, 25)), outline=GREEN, width=sc(3))
        d.line(pts([(7, 16), (25, 16)]), fill=GREEN, width=sc(2))
        d.line(pts([(16, 7), (16, 25)]), fill=GREEN, width=sc(2))
    else:
        d.rectangle(box((7, 7, 25, 25)), outline=(114, 91, 58, 190), width=sc(2))
        tamga(d, 16, 16, 10, TEAL, 3)
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
        img.thumbnail((112, 112), Image.Resampling.LANCZOS)
        thumb.alpha_composite(img, ((128 - img.width) // 2, (128 - img.height) // 2))
        cells.append((path.name, thumb))
    cols = 5
    rows = (len(cells) + cols - 1) // cols
    sheet = Image.new("RGBA", (cols * 176, rows * 160), (238, 229, 207, 255))
    d = ImageDraw.Draw(sheet, "RGBA")
    for i, (name, thumb) in enumerate(cells):
        x = (i % cols) * 176
        y = (i // cols) * 160
        sheet.alpha_composite(thumb, (x + 24, y + 8))
        d.text((x + 6, y + 139), name[:25], fill=(30, 27, 23, 255))
    OUT.mkdir(parents=True, exist_ok=True)
    sheet.save(OUT / "final_repaint_pass_2_contact_sheet.png")


def main() -> None:
    written: list[Path] = []
    for direction in ["s", "e", "n", "w"]:
        save(hero_128(direction), f"hero/hero_wayfarer_walk_{direction}.png", written)
    save(hero_panel(), "hero/hero_wayfarer_panel.png", written)

    for kind in ["weapon", "clothes", "tool", "tamga"]:
        save(slot(kind), f"equipment/equip_slot_{kind}_01.png", written)
    for kind, filename in [
        ("weapon", "equip_weapon_staff_01.png"),
        ("clothes", "equip_clothes_cloak_01.png"),
        ("tool", "equip_tool_satchel_01.png"),
        ("tamga", "equip_tamga_charm_01.png"),
    ]:
        save(equipment_item(kind), f"equipment/{filename}", written)

    for kind in ["body", "mind", "spirit", "circle", "day", "speed", "last_tamga"]:
        save(icon(kind), f"icons/icon_{kind}_32.png", written)
    for kind, filename in [
        ("count", "card_badge_count_32.png"),
        ("roadside", "card_placement_roadside_32.png"),
        ("field", "card_placement_field_32.png"),
        ("special", "card_placement_special_32.png"),
    ]:
        save(card_utility(kind), f"cards/{filename}", written)

    contact_sheet(written)
    for path in written:
        print(path.relative_to(ROOT).as_posix())
    print((OUT / "final_repaint_pass_2_contact_sheet.png").relative_to(ROOT).as_posix())


if __name__ == "__main__":
    main()
