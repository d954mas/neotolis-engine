import math
from pathlib import Path
from PIL import Image, ImageDraw


ROOT = Path(__file__).resolve().parents[2]
PREVIEW_DIR = ROOT / "gamedesign" / "assets" / "concept" / "production_batch_b"
RAW = ROOT / "games" / "turkic-jam-2026" / "raw"

COLORS = {
    "panel": (20, 29, 34, 242),
    "panel2": (29, 39, 45, 242),
    "edge": (140, 98, 50, 255),
    "edge_dim": (64, 71, 77, 255),
    "card": (232, 211, 165, 255),
    "sand": (218, 176, 89, 255),
    "teal": (47, 170, 158, 255),
    "red": (197, 80, 52, 255),
    "green": (142, 211, 101, 255),
    "gold": (245, 188, 73, 255),
    "text": (232, 215, 181, 255),
    "dark": (13, 17, 18, 255),
    "bone": (225, 209, 168, 255),
    "wood": (94, 57, 34, 255),
    "ink": (19, 17, 15, 255),
    "shadow": (0, 0, 0, 80),
}


def ensure_dirs():
    PREVIEW_DIR.mkdir(parents=True, exist_ok=True)
    for name in ["ui", "cards", "equipment", "icons", "fx"]:
        (RAW / name).mkdir(parents=True, exist_ok=True)


def save(img, folder, name, files):
    target = RAW / folder / name
    img.save(target)
    files.append(target)

    concept_target = PREVIEW_DIR / name
    img.save(concept_target)


def draw_tamga(draw, cx, cy, s, color, width=2):
    draw.line((cx, cy - s, cx, cy + s), fill=color, width=width)
    draw.line((cx - s, cy - s // 2, cx, cy - s, cx + s, cy - s // 2), fill=color, width=width)
    draw.line((cx - s, cy + s // 2, cx, cy, cx + s, cy + s // 2), fill=color, width=width)


def draw_corner_tamga(draw, x, y, sx, sy, color):
    draw.line((x, y + sy * 4, x + sx * 4, y, x + sx * 8, y + sy * 4), fill=color, width=2)
    draw.line((x + sx * 4, y, x + sx * 4, y + sy * 8), fill=color, width=2)
    draw.line((x + sx * 2, y + sy * 7, x + sx * 6, y + sy * 7), fill=color, width=2)


def draw_stitches(draw, box, color, step=8):
    x0, y0, x1, y1 = box
    for x in range(x0 + 7, x1 - 7, step):
        draw.line((x, y0, x + 3, y0), fill=color, width=1)
        draw.line((x, y1, x + 3, y1), fill=color, width=1)
    for y in range(y0 + 7, y1 - 7, step):
        draw.line((x0, y, x0, y + 3), fill=color, width=1)
        draw.line((x1, y, x1, y + 3), fill=color, width=1)


def panel(size, name):
    w, h = size
    img = Image.new("RGBA", size, (0, 0, 0, 0))
    d = ImageDraw.Draw(img, "RGBA")
    d.rounded_rectangle((2, 2, w - 3, h - 3), 10, fill=COLORS["panel"], outline=(6, 8, 9, 180), width=2)
    d.rounded_rectangle((4, 4, w - 5, h - 5), 8, outline=COLORS["edge"], width=2)
    d.rounded_rectangle((9, 9, w - 10, h - 10), 6, outline=COLORS["edge_dim"], width=1)
    for y in range(13, h - 12, 14):
        d.line((12, y, w - 13, y + (y // 14) % 2), fill=(93, 129, 120, 28), width=1)
    draw_stitches(d, (9, 9, w - 10, h - 10), (196, 158, 89, 135), 8)
    draw_corner_tamga(d, 9, 9, 1, 1, COLORS["teal"])
    draw_corner_tamga(d, w - 9, 9, -1, 1, COLORS["teal"])
    draw_corner_tamga(d, 9, h - 9, 1, -1, COLORS["teal"])
    draw_corner_tamga(d, w - 9, h - 9, -1, -1, COLORS["teal"])
    if "card_back" in name:
        d.rounded_rectangle((19, 23, w - 20, h - 24), 8, outline=(79, 61, 43, 190), width=2)
        draw_tamga(d, w // 2, h // 2, 22, COLORS["teal"], 4)
        draw_tamga(d, w // 2, h // 2, 12, COLORS["gold"], 2)
        d.line((20, 23, w - 20, h - 24), fill=(84, 67, 49, 135), width=2)
        d.line((w - 20, 23, 20, h - 24), fill=(84, 67, 49, 135), width=2)
    if "button" in name:
        d.ellipse((18, 18, 46, 46), fill=(38, 49, 54, 230), outline=(185, 136, 66, 180), width=2)
        draw_tamga(d, w // 2, h // 2, 10, COLORS["gold"], 3)
    return img


def card_badge(kind):
    img = Image.new("RGBA", (32, 32), (0, 0, 0, 0))
    d = ImageDraw.Draw(img, "RGBA")
    d.rounded_rectangle((2, 2, 30, 30), 8, fill=COLORS["panel2"], outline=(11, 13, 14, 190), width=2)
    d.rounded_rectangle((5, 5, 27, 27), 6, outline=COLORS["edge"], width=2)
    if kind == "count":
        d.ellipse((10, 7, 22, 19), fill=COLORS["gold"], outline=COLORS["ink"])
        d.rectangle((14, 16, 18, 25), fill=COLORS["gold"], outline=COLORS["ink"])
    elif kind == "roadside":
        d.line((5, 20, 27, 20), fill=(91, 62, 36, 255), width=8)
        d.line((5, 20, 27, 20), fill=COLORS["sand"], width=5)
        d.line((6, 10, 26, 10), fill=(132, 116, 83, 210), width=3)
        for x in [8, 16, 24]:
            d.ellipse((x - 2, 8, x + 2, 12), fill=COLORS["bone"])
    elif kind == "field":
        d.rectangle((7, 7, 25, 25), outline=COLORS["green"], width=3)
        d.line((7, 16, 25, 16), fill=COLORS["green"], width=2)
        d.line((16, 7, 16, 25), fill=COLORS["green"], width=2)
    else:
        d.rectangle((7, 7, 25, 25), outline=(114, 91, 58, 190), width=2)
        draw_tamga(d, 16, 16, 10, COLORS["teal"], 3)
    return img


def card_art(kind):
    img = Image.new("RGBA", (64, 64), (0, 0, 0, 0))
    d = ImageDraw.Draw(img, "RGBA")
    if kind == "saxaul":
        d.ellipse((8, 42, 56, 53), fill=COLORS["shadow"])
        for x0, y0, x1, y1, w in [(13, 42, 33, 25, 4), (20, 45, 49, 31, 4), (28, 43, 18, 28, 3), (36, 44, 54, 25, 3)]:
            d.line((x0, y0, x1, y1), fill=COLORS["ink"], width=w + 2)
            d.line((x0, y0, x1, y1), fill=COLORS["wood"], width=w)
        for x, y in [(12, 42), (22, 38), (36, 39), (48, 42)]:
            d.polygon((x, y, x + 5, y - 12, x + 11, y), fill=(158, 123, 48, 230), outline=(93, 63, 35, 180))
    elif kind == "yurt":
        d.ellipse((10, 45, 54, 56), fill=COLORS["shadow"])
        d.polygon((13, 34, 32, 15, 51, 34), fill=(236, 215, 167, 255), outline=COLORS["ink"])
        d.rounded_rectangle((12, 31, 52, 53), 8, fill=(219, 199, 154, 255), outline=COLORS["edge"], width=3)
        d.rectangle((27, 39, 36, 53), fill=COLORS["red"], outline=COLORS["ink"])
        d.line((16, 36, 48, 36), fill=COLORS["teal"], width=3)
        draw_tamga(d, 32, 25, 6, COLORS["red"], 2)
    elif kind == "tamga_stone":
        d.ellipse((17, 49, 49, 57), fill=COLORS["shadow"])
        d.polygon((21, 53, 17, 20, 33, 10, 48, 22, 43, 54), fill=(142, 124, 91, 255), outline=COLORS["ink"])
        d.line((22, 23, 43, 23), fill=(184, 165, 121, 180), width=2)
        draw_tamga(d, 32, 32, 13, COLORS["teal"], 4)
    elif kind == "wolf_track":
        d.line((10, 55, 55, 11), fill=(134, 52, 42, 120), width=3)
        for x, y, s in [(20, 23, 1), (40, 36, 1), (25, 49, 0)]:
            d.ellipse((x - 5, y - 3, x + 5, y + 7), fill=(65, 45, 33, 230))
            for ox, oy in [(-7, -8), (0, -10), (7, -8)]:
                d.ellipse((x + ox - 3, y + oy - 3, x + ox + 3, y + oy + 3), fill=(65, 45, 33, 230))
    return img


def equipment(kind, slot=False):
    img = Image.new("RGBA", (64, 64), (0, 0, 0, 0))
    d = ImageDraw.Draw(img, "RGBA")
    if slot:
        d.rounded_rectangle((5, 5, 59, 59), 9, fill=(18, 25, 29, 210), outline=(8, 10, 11, 190), width=2)
        d.rounded_rectangle((9, 9, 55, 55), 7, outline=COLORS["edge"], width=2)
        color = (181, 164, 128, 118)
    else:
        color = COLORS["bone"]
    if kind == "weapon":
        d.line((18, 51, 48, 13), fill=COLORS["ink"], width=7)
        d.line((18, 51, 48, 13), fill=color, width=5)
        d.line((37, 16, 50, 22), fill=color, width=4)
        if not slot:
            d.ellipse((42, 9, 53, 20), fill=COLORS["gold"], outline=COLORS["ink"])
    elif kind == "clothes":
        d.polygon((22, 16, 42, 16, 53, 55, 11, 55), fill=color, outline=COLORS["ink"])
        d.line((22, 27, 42, 27), fill=COLORS["red"], width=3)
        d.line((31, 17, 31, 55), fill=(158, 125, 80, 160), width=2)
    elif kind == "tamga":
        d.ellipse((13, 12, 51, 52), fill=(0, 0, 0, 0), outline=COLORS["ink"], width=5)
        d.ellipse((15, 14, 49, 50), fill=(0, 0, 0, 0), outline=color, width=4)
        draw_tamga(d, 32, 32, 13, COLORS["teal"] if not slot else color, 4)
    else:
        d.rounded_rectangle((17, 24, 47, 54), 6, fill=color, outline=COLORS["ink"], width=2)
        d.arc((20, 9, 44, 33), 180, 360, fill=color, width=4)
        if not slot:
            d.polygon((21, 27, 43, 27, 39, 36, 25, 36), fill=(178, 119, 58, 180))
    return img


def icon(kind):
    img = Image.new("RGBA", (32, 32), (0, 0, 0, 0))
    d = ImageDraw.Draw(img, "RGBA")
    c = COLORS["text"]
    if kind == "stamina":
        d.polygon((17, 2, 7, 17, 15, 17, 12, 30, 25, 12, 17, 12), fill=COLORS["ink"])
        d.polygon((17, 3, 8, 17, 16, 17, 13, 29, 24, 13, 16, 13), fill=COLORS["gold"])
    elif kind == "supplies":
        d.rounded_rectangle((7, 11, 25, 29), 5, fill=COLORS["bone"], outline=COLORS["ink"], width=2)
        d.arc((10, 3, 22, 17), 180, 360, fill=COLORS["bone"], width=3)
        d.polygon((10, 15, 22, 15, 20, 21, 12, 21), fill=(181, 127, 65, 190))
    elif kind == "wisdom":
        d.ellipse((6, 5, 26, 25), outline=COLORS["ink"], width=4)
        d.ellipse((8, 7, 24, 23), outline=COLORS["teal"], width=4)
        d.line((16, 9, 16, 26), fill=COLORS["teal"], width=3)
    elif kind == "glory":
        pts = (16, 3, 21, 12, 30, 13, 23, 20, 25, 29, 16, 24, 7, 29, 9, 20, 2, 13, 11, 12)
        d.polygon(pts, fill=COLORS["ink"])
        d.polygon((16, 5, 20, 13, 28, 14, 22, 19, 24, 27, 16, 23, 8, 27, 10, 19, 4, 14, 12, 13), fill=COLORS["red"])
    elif kind == "circle":
        d.ellipse((5, 5, 27, 27), outline=COLORS["ink"], width=5)
        d.ellipse((7, 7, 25, 25), outline=COLORS["gold"], width=4)
    elif kind == "day":
        d.ellipse((9, 9, 23, 23), fill=COLORS["gold"], outline=COLORS["ink"], width=2)
        for x1, y1, x2, y2 in [(16, 2, 16, 7), (16, 25, 16, 30), (2, 16, 7, 16), (25, 16, 30, 16), (6, 6, 10, 10), (22, 22, 26, 26), (26, 6, 22, 10), (10, 22, 6, 26)]:
            d.line((x1, y1, x2, y2), fill=COLORS["gold"], width=3)
    elif kind == "body":
        d.ellipse((10, 3, 22, 15), fill=COLORS["ink"])
        d.rounded_rectangle((7, 13, 25, 29), 6, fill=COLORS["ink"])
        d.ellipse((11, 5, 21, 14), fill=c)
        d.rounded_rectangle((9, 14, 23, 28), 5, fill=c)
    elif kind == "mind":
        d.ellipse((5, 7, 27, 25), outline=COLORS["ink"], width=4)
        d.ellipse((7, 8, 25, 24), outline=COLORS["teal"], width=3)
        d.arc((10, 11, 22, 24), 180, 350, fill=COLORS["teal"], width=3)
    elif kind == "spirit":
        d.polygon((16, 3, 27, 16, 16, 29, 5, 16), fill=COLORS["ink"])
        d.polygon((16, 5, 25, 16, 16, 27, 7, 16), outline=COLORS["teal"], fill=(47, 170, 158, 105))
    elif kind == "last_tamga":
        draw_tamga(d, 16, 16, 12, COLORS["ink"], 5)
        draw_tamga(d, 16, 16, 11, COLORS["teal"], 3)
    elif kind == "settings":
        d.ellipse((8, 8, 24, 24), outline=COLORS["ink"], width=5)
        d.ellipse((10, 10, 22, 22), outline=c, width=3)
        for x1, y1, x2, y2 in [(16, 2, 16, 9), (16, 23, 16, 30), (2, 16, 9, 16), (23, 16, 30, 16)]:
            d.line((x1, y1, x2, y2), fill=c, width=3)
    elif kind == "speed":
        d.polygon((5, 5, 19, 16, 5, 27), fill=COLORS["ink"])
        d.polygon((16, 5, 30, 16, 16, 27), fill=COLORS["ink"])
        d.polygon((7, 7, 19, 16, 7, 25), fill=c)
        d.polygon((18, 7, 30, 16, 18, 25), fill=c)
    return img


def fx(kind, frame, total):
    img = Image.new("RGBA", (64, 64), (0, 0, 0, 0))
    d = ImageDraw.Draw(img, "RGBA")
    t = frame / max(1, total - 1)
    if kind == "dust_step":
        alpha = int(165 * (1 - t))
        for i in range(3):
            x = 20 + i * 10 + frame * 2
            y = 42 + int(math.sin(i + frame) * 4)
            r = 4 + frame * 3 + i
            d.ellipse((x - r, y - r // 2, x + r, y + r // 2), fill=(222, 186, 116, max(96, alpha - i * 28)))
    elif kind == "tile_placed":
        alpha = int(230 * (1 - t))
        inset = 7 + frame * 4
        d.rounded_rectangle((inset, inset, 64 - inset, 64 - inset), 8, outline=(142, 211, 101, max(112, alpha)), width=4)
        d.rounded_rectangle((inset + 5, inset + 5, 59 - inset, 59 - inset), 5, outline=(245, 188, 73, max(96, alpha // 2)), width=2)
    elif kind == "tile_trigger":
        alpha = max(112, int(220 * (1 - t) + 40))
        draw_tamga(d, 32, 32, 9 + frame * 4, (47, 170, 158, alpha), 4)
        d.ellipse((17 + frame * 3, 17 + frame * 3, 47 - frame * 3, 47 - frame * 3), outline=(245, 188, 73, max(96, alpha // 2)), width=2)
    elif kind == "gain_popup":
        a = max(112, int(235 * (1 - t) + 35))
        cy = 28 - frame * 5
        d.ellipse((20, cy - 10, 44, cy + 14), fill=(245, 188, 73, a), outline=(19, 17, 15, min(190, a)), width=2)
        d.line((32, cy - 6, 32, cy + 9), fill=COLORS["dark"], width=3)
        d.line((24, cy + 2, 40, cy + 2), fill=COLORS["dark"], width=3)
    else:
        a = 215 - frame * 55
        d.rounded_rectangle((9 + frame * 3, 9 + frame * 3, 55 - frame * 3, 55 - frame * 3), 7, outline=(197, 80, 52, max(80, a)), width=4)
        d.line((20, 20, 44, 44), fill=(197, 80, 52, max(110, a)), width=5)
        d.line((44, 20, 20, 44), fill=(197, 80, 52, max(110, a)), width=5)
    return img


def make_preview(files):
    thumbs = []
    for path in files:
        im = Image.open(path).convert("RGBA")
        canvas = Image.new("RGBA", (96, 96), (29, 30, 25, 255))
        im.thumbnail((80, 80), Image.Resampling.LANCZOS)
        canvas.alpha_composite(im, ((96 - im.width) // 2, (96 - im.height) // 2))
        thumbs.append((path.name, canvas))

    cols = 8
    rows = (len(thumbs) + cols - 1) // cols
    preview = Image.new("RGBA", (cols * 128, rows * 122), (239, 229, 207, 255))
    d = ImageDraw.Draw(preview, "RGBA")
    for i, (name, thumb) in enumerate(thumbs):
        x = (i % cols) * 128
        y = (i // cols) * 122
        preview.alpha_composite(thumb, (x + 16, y + 8))
        d.text((x + 8, y + 105), name[:18], fill=(35, 39, 50, 255))
    preview.save(PREVIEW_DIR / "batch_b_runtime_preview.png")


def write_manifest(files):
    lines = ["# Batch B Runtime Placeholder Manifest", ""]
    lines.append("Generated by `gamedesign/tools/generate_batch_b_assets.py`.")
    lines.append("These are runtime-ready placeholders, not final art.")
    lines.append("")
    lines.append("## Files")
    lines.append("")
    for path in sorted(files):
        with Image.open(path) as im:
            rel = path.relative_to(ROOT).as_posix()
            lines.append(f"- `{rel}` - {im.width}x{im.height}, {im.mode}")
    lines.append("")
    lines.append("## Preview")
    lines.append("")
    lines.append("`gamedesign/assets/concept/production_batch_b/batch_b_runtime_preview.png`")
    (PREVIEW_DIR / "batch_b_runtime_manifest.md").write_text("\n".join(lines), encoding="utf-8")


def main():
    ensure_dirs()
    files = []
    save(panel((96, 128), "card_back"), "ui", "ui_card_back_96x128.png", files)
    save(panel((64, 64), "button_dark"), "ui", "ui_button_dark_64.png", files)

    for name, kind in [
        ("card_badge_count_32.png", "count"),
        ("card_placement_roadside_32.png", "roadside"),
        ("card_placement_field_32.png", "field"),
        ("card_placement_special_32.png", "special"),
    ]:
        save(card_badge(kind), "cards", name, files)

    for kind in ["saxaul", "yurt", "tamga_stone", "wolf_track"]:
        save(card_art(kind), "cards", f"card_art_{kind}_64.png", files)

    for kind in ["weapon", "clothes", "tamga", "tool"]:
        save(equipment(kind, True), "equipment", f"equip_slot_{kind}_01.png", files)
    for kind, name in [
        ("weapon", "equip_weapon_staff_01.png"),
        ("clothes", "equip_clothes_cloak_01.png"),
        ("tamga", "equip_tamga_charm_01.png"),
        ("tool", "equip_tool_satchel_01.png"),
    ]:
        save(equipment(kind, False), "equipment", name, files)

    for kind in ["stamina", "supplies", "wisdom", "glory", "circle", "day", "body", "mind", "spirit", "last_tamga", "settings", "speed"]:
        save(icon(kind), "icons", f"icon_{kind}_32.png", files)

    for kind, count in [("dust_step", 4), ("tile_placed", 4), ("tile_trigger", 4), ("gain_popup", 3), ("invalid_cell", 2)]:
        for frame in range(count):
            save(fx(kind, frame, count), "fx", f"fx_{kind}_{frame:02d}.png", files)

    make_preview(files)
    write_manifest(files)
    print(f"Generated {len(files)} Batch B PNG files")
    print(PREVIEW_DIR / "batch_b_runtime_preview.png")


if __name__ == "__main__":
    main()
