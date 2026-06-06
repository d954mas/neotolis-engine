import math
from pathlib import Path
from PIL import Image, ImageDraw


ROOT = Path(__file__).resolve().parents[2]
PREVIEW_DIR = ROOT / "gamedesign" / "assets" / "concept" / "production_batch_c"
RAW = ROOT / "games" / "turkic-jam-2026" / "raw"

SAND = (216, 181, 107, 255)
EDGE = (123, 90, 50, 255)
WOOD = (94, 57, 34, 255)
FELT = (224, 205, 161, 255)
DARK = (21, 28, 42, 235)
TEAL = (53, 184, 166, 255)
RED = (201, 90, 58, 255)
GOLD = (244, 201, 93, 255)
GREEN = (154, 214, 111, 255)
BONE = (225, 209, 168, 255)
STONE = (134, 119, 91, 255)


def ensure_dirs():
    PREVIEW_DIR.mkdir(parents=True, exist_ok=True)
    for name in ["tiles", "cards", "aul", "hero", "fx", "icons"]:
        (RAW / name).mkdir(parents=True, exist_ok=True)


def save(img, folder, name, files):
    path = RAW / folder / name
    img.save(path)
    img.save(PREVIEW_DIR / name)
    files.append(path)


def tamga(d, cx, cy, s, color=TEAL, width=3):
    d.line((cx, cy - s, cx, cy + s), fill=color, width=width)
    d.line((cx - s, cy - s // 2, cx, cy - s, cx + s, cy - s // 2), fill=color, width=width)
    d.line((cx - s, cy + s // 2, cx, cy, cx + s, cy + s // 2), fill=color, width=width)


def tile(kind, size=128):
    img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    d = ImageDraw.Draw(img, "RGBA")
    cx = size // 2
    if kind == "well":
        d.ellipse((38, 76, 90, 104), fill=(87, 59, 39, 165))
        d.rounded_rectangle((42, 54, 86, 90), 9, fill=STONE, outline=EDGE, width=3)
        d.ellipse((48, 61, 80, 82), fill=(36, 92, 103, 220))
        d.line((49, 50, 49, 27), fill=WOOD, width=4)
        d.line((79, 50, 79, 27), fill=WOOD, width=4)
        d.line((44, 28, 84, 28), fill=WOOD, width=4)
    elif kind in ["small_camp", "clan_camp"]:
        tents = [(37, 73), (64, 60)] if kind == "small_camp" else [(32, 76), (64, 60), (88, 77)]
        for x, y in tents:
            d.polygon((x - 19, y, x, y - 22, x + 19, y), fill=FELT, outline=EDGE)
            d.rectangle((x - 15, y, x + 15, y + 18), fill=(211, 193, 150, 255), outline=EDGE)
        d.ellipse((54, 91, 74, 102), fill=(255, 143, 68, 220))
    elif kind == "watchtower":
        d.line((43, 100, 55, 38), fill=WOOD, width=5)
        d.line((85, 100, 73, 38), fill=WOOD, width=5)
        d.rectangle((45, 34, 83, 55), fill=(127, 83, 48, 255), outline=EDGE)
        d.line((42, 67, 86, 67), fill=WOOD, width=4)
    elif kind == "hunting_trail":
        d.line((23, 96, 105, 30), fill=(95, 58, 36, 160), width=6)
        for x, y in [(38, 78), (59, 61), (79, 44)]:
            d.ellipse((x - 4, y - 4, x + 4, y + 4), fill=(55, 39, 30, 230))
            d.line((x + 7, y - 8, x + 22, y - 16), fill=WOOD, width=3)
    elif kind == "pack":
        for x, y in [(37, 72), (61, 60), (82, 76)]:
            d.ellipse((x - 10, y - 7, x + 10, y + 7), fill=(60, 48, 39, 230))
            d.ellipse((x - 3, y - 16, x + 8, y - 5), fill=(60, 48, 39, 230))
        d.arc((20, 33, 108, 113), 190, 340, fill=RED, width=3)
    elif kind == "vision":
        d.ellipse((32, 43, 96, 85), outline=TEAL, width=4)
        d.ellipse((52, 52, 76, 76), fill=(53, 184, 166, 80), outline=TEAL, width=3)
        d.line((64, 25, 64, 105), fill=(53, 184, 166, 80), width=2)
    elif kind == "false_path":
        d.line((21, 93, 57, 66, 38, 39, 94, 23), fill=(154, 109, 58, 210), width=8)
        d.line((42, 38, 96, 97), fill=RED, width=4)
        d.line((96, 38, 42, 97), fill=RED, width=4)
    elif kind == "buried_spring":
        d.arc((35, 57, 93, 111), 200, 340, fill=TEAL, width=5)
        d.polygon((36, 80, 72, 50, 103, 80), fill=(214, 178, 102, 160))
        d.line((43, 82, 93, 82), fill=(88, 124, 120, 170), width=3)
    return img


def card_art(kind):
    img = Image.new("RGBA", (64, 64), (0, 0, 0, 0))
    d = ImageDraw.Draw(img, "RGBA")
    if kind == "well":
        d.ellipse((14, 43, 50, 55), fill=(0, 0, 0, 70))
        d.rounded_rectangle((18, 26, 46, 48), 6, fill=STONE, outline=EDGE, width=2)
        d.ellipse((22, 31, 42, 43), fill=(36, 92, 103, 230), outline=(18, 52, 58, 220), width=2)
        d.line((22, 25, 22, 12), fill=WOOD, width=3)
        d.line((42, 25, 42, 12), fill=WOOD, width=3)
        d.line((19, 13, 45, 13), fill=WOOD, width=3)
    elif kind == "vision":
        d.ellipse((10, 18, 54, 45), outline=TEAL, width=4)
        d.ellipse((24, 23, 40, 39), fill=(53, 184, 166, 80), outline=TEAL, width=3)
        d.line((32, 8, 32, 55), fill=(53, 184, 166, 120), width=2)
    elif kind == "false_path":
        d.line((10, 50, 30, 33, 22, 16, 52, 9), fill=(154, 109, 58, 220), width=6)
        d.line((20, 16, 52, 50), fill=RED, width=4)
        d.line((52, 16, 20, 50), fill=RED, width=4)
    elif kind == "last_tamga":
        d.rounded_rectangle((19, 17, 45, 48), 6, fill=(151, 125, 82, 230), outline=EDGE, width=2)
        tamga(d, 32, 32, 11, TEAL, 3)
        d.ellipse((13, 12, 51, 53), outline=(53, 184, 166, 95), width=3)
    elif kind == "watchtower":
        d.line((22, 53, 28, 18), fill=WOOD, width=4)
        d.line((43, 53, 37, 18), fill=WOOD, width=4)
        d.rectangle((21, 15, 43, 29), fill=(127, 83, 48, 255), outline=EDGE, width=2)
        d.line((20, 37, 44, 37), fill=WOOD, width=3)
    else:
        d.rounded_rectangle((20, 20, 44, 44), 8, fill=(53, 184, 166, 160), outline=TEAL, width=3)
    return img


def aul_stage(stage):
    img = Image.new("RGBA", (256, 256), (0, 0, 0, 0))
    d = ImageDraw.Draw(img, "RGBA")
    d.ellipse((37, 66, 219, 204), fill=(173, 126, 70, 150))
    d.ellipse((56, 82, 200, 190), outline=(111, 76, 39, 95), width=3)

    def yurt(x, y, scale=1.0, accent=RED):
        w = int(25 * scale)
        h = int(23 * scale)
        d.ellipse((x - w - 3, y + h - 3, x + w + 3, y + h + 9), fill=(0, 0, 0, 50))
        d.polygon((x - w, y, x, y - int(30 * scale), x + w, y), fill=FELT, outline=EDGE)
        d.rounded_rectangle((x - w + 3, y - 1, x + w - 3, y + h), 6, fill=(211, 193, 150, 255), outline=EDGE, width=2)
        d.rectangle((x - 7, y + 8, x + 7, y + h), fill=accent)
        d.line((x - w + 7, y + 5, x + w - 7, y + 5), fill=accent, width=3)

    stage_positions = {
        1: [(108, 132, 0.85, RED), (148, 137, 0.7, TEAL)],
        2: [(86, 139, 0.8, RED), (128, 117, 0.95, TEAL), (169, 141, 0.78, RED)],
        3: [(78, 138, 0.78, RED), (122, 113, 1.0, TEAL), (168, 137, 0.78, RED), (127, 164, 0.68, TEAL)],
        4: [(72, 136, 0.74, RED), (116, 111, 0.95, TEAL), (162, 133, 0.74, RED), (105, 165, 0.68, TEAL), (183, 104, 0.65, RED)],
        5: [(70, 139, 0.7, RED), (119, 107, 1.15, TEAL), (166, 136, 0.72, RED), (99, 170, 0.66, TEAL), (188, 106, 0.66, RED), (151, 169, 0.62, TEAL)],
    }[stage]
    for x, y, scale, accent in stage_positions:
        yurt(x, y, scale, accent)

    d.ellipse((117, 146, 145, 165), fill=(71, 45, 27, 220))
    d.polygon((124, 151, 132, 124, 140, 151), fill=(255, 139, 55, 240))
    d.polygon((120, 153, 127, 135, 132, 154), fill=(255, 207, 85, 220))

    if stage >= 2:
        d.line((47, 198, 209, 198), fill=WOOD, width=5)
        for x in range(54, 205, 20):
            d.line((x, 187, x, 207), fill=WOOD, width=3)
    if stage >= 3:
        d.rounded_rectangle((111, 50, 145, 95), 5, fill=(111, 83, 57, 255), outline=EDGE, width=3)
        tamga(d, 128, 72, 11, TEAL, 4)
        d.line((128, 95, 128, 116), fill=WOOD, width=5)
    if stage >= 4:
        d.arc((24, 54, 232, 220), 188, 346, fill=EDGE, width=8)
        for x, y in [(46, 165), (71, 194), (185, 194), (211, 164)]:
            d.rounded_rectangle((x - 5, y - 18, x + 5, y + 14), 3, fill=WOOD)
    if stage >= 5:
        d.rounded_rectangle((100, 29, 156, 58), 5, fill=(138, 99, 57, 255), outline=EDGE, width=3)
        d.line((105, 44, 151, 44), fill=TEAL, width=4)
        tamga(d, 128, 43, 10, GOLD, 3)
    return img


def tamga_post():
    img = Image.new("RGBA", (128, 128), (0, 0, 0, 0))
    d = ImageDraw.Draw(img, "RGBA")
    d.line((64, 105, 64, 34), fill=WOOD, width=8)
    d.rounded_rectangle((40, 20, 88, 55), 6, fill=(122, 89, 54, 255), outline=EDGE, width=3)
    tamga(d, 64, 38, 12, TEAL, 3)
    return img


def hero_panel(kind):
    img = Image.new("RGBA", (128, 192), (0, 0, 0, 0))
    d = ImageDraw.Draw(img, "RGBA")
    accent = {"body": RED, "mind": TEAL, "spirit": GOLD}[kind]
    d.ellipse((36, 171, 92, 188), fill=(0, 0, 0, 80))
    d.ellipse((49, 23, 79, 53), fill=(239, 157, 91, 255), outline=(20, 17, 15, 255), width=2)
    d.polygon((42, 48, 86, 48, 77, 70, 52, 70), fill=(46, 32, 25, 255))
    d.rounded_rectangle((39, 60, 89, 130), 10, fill=(45, 120, 113, 255), outline=(20, 17, 15, 255), width=3)
    d.line((44, 82, 84, 82), fill=accent, width=4)
    d.polygon((40, 126, 64, 177, 88, 126), fill=(88, 61, 42, 255), outline=(20, 17, 15, 255))
    if kind == "body":
        d.line((31, 76, 97, 76), fill=(20, 17, 15, 255), width=10)
        d.line((31, 76, 97, 76), fill=accent, width=6)
        d.line((37, 112, 21, 145), fill=(88, 61, 42, 255), width=7)
        d.line((91, 112, 107, 145), fill=(88, 61, 42, 255), width=7)
    elif kind == "mind":
        tamga(d, 64, 92, 16, accent, 4)
        d.line((39, 106, 21, 89), fill=(88, 61, 42, 255), width=6)
        d.line((89, 106, 107, 89), fill=(88, 61, 42, 255), width=6)
        d.ellipse((18, 82, 35, 99), outline=accent, width=3)
        d.ellipse((93, 82, 110, 99), outline=accent, width=3)
    else:
        d.ellipse((45, 71, 83, 109), outline=accent, width=5)
        d.line((64, 61, 64, 121), fill=accent, width=3)
        d.line((39, 109, 27, 140), fill=(88, 61, 42, 255), width=6)
        d.line((89, 109, 101, 140), fill=(88, 61, 42, 255), width=6)
        d.polygon((64, 55, 70, 65, 58, 65), fill=accent)
    return img


def fx(kind, frame, total):
    img = Image.new("RGBA", (128, 128), (0, 0, 0, 0))
    d = ImageDraw.Draw(img, "RGBA")
    t = frame / max(1, total - 1)
    alpha = max(112, int(220 * (1.0 - t) + 30))
    if kind == "intro_sand":
        for i, y in enumerate(range(16 + frame * 6, 136, 18)):
            a = max(45, alpha - i * 14)
            d.arc((-24, y - 36, 152, y + 34), 190, 350, fill=(216, 181, 107, a), width=4)
        for i in range(9):
            x = (i * 23 + frame * 11) % 128
            y = 30 + ((i * 31 + frame * 7) % 78)
            d.ellipse((x, y, x + 3, y + 2), fill=(240, 206, 128, max(40, alpha // 2)))
    elif kind == "fire_glow":
        r = 18 + frame * 8
        d.ellipse((64 - r, 64 - r, 64 + r, 64 + r), fill=(244, 201, 93, max(96, alpha // 3)))
        d.polygon((55, 73, 64, 35 + frame * 2, 73, 73), fill=(255, 122, 47, 230))
    elif kind == "last_tamga_spawn":
        d.rounded_rectangle((49, 55, 79, 85), 5, fill=(151, 125, 82, max(80, alpha // 2)), outline=(123, 90, 50, alpha), width=2)
        tamga(d, 64, 70, 9 + frame * 3, (53, 184, 166, alpha), 4)
        d.ellipse((23 + frame * 3, 23 + frame * 3, 105 - frame * 3, 105 - frame * 3), outline=(53, 184, 166, max(72, alpha)), width=3)
        for a in [0, 90, 180, 270]:
            x = 64 + int(math.cos(math.radians(a + frame * 12)) * (35 - frame * 2))
            y = 64 + int(math.sin(math.radians(a + frame * 12)) * (35 - frame * 2))
            d.ellipse((x - 3, y - 3, x + 3, y + 3), fill=(244, 201, 93, max(80, alpha // 2)))
    elif kind == "storm_veil":
        for y in range(8, 128, 18):
            d.line((0, y + frame * 6, 128, y - 22 + frame * 6), fill=(216, 181, 107, 120), width=8)
    elif kind == "card_reward":
        d.rounded_rectangle((38 - frame * 3, 25 - frame * 2, 90 + frame * 3, 103 + frame * 2), 8, outline=(244, 201, 93, alpha), width=5)
        tamga(d, 64, 64, 13, TEAL, 3)
    elif kind == "near_death":
        d.ellipse((18 + frame * 4, 18 + frame * 4, 110 - frame * 4, 110 - frame * 4), outline=(201, 90, 58, alpha), width=6)
        d.polygon((64, 31, 95, 88, 33, 88), outline=(201, 90, 58, alpha), fill=(201, 90, 58, max(28, alpha // 5)))
        d.line((64, 47, 64, 74), fill=(201, 90, 58, alpha), width=5)
        d.ellipse((61, 81, 67, 87), fill=(201, 90, 58, alpha))
    return img


def icon(kind):
    img = Image.new("RGBA", (32, 32), (0, 0, 0, 0))
    d = ImageDraw.Draw(img, "RGBA")
    if kind == "aul_upgrade":
        d.polygon((6, 20, 16, 8, 26, 20), fill=FELT, outline=EDGE)
        d.rectangle((8, 20, 24, 28), fill=FELT, outline=EDGE)
    elif kind == "deck":
        for off in [0, 3, 6]:
            d.rounded_rectangle((8 + off, 6 + off, 22 + off, 24 + off), 3, outline=GOLD, width=2)
    elif kind == "map":
        d.rectangle((5, 7, 27, 25), outline=BONE, width=2)
        d.line((12, 7, 12, 25), fill=BONE, width=1)
        d.line((20, 7, 20, 25), fill=BONE, width=1)
        d.line((7, 19, 25, 11), fill=TEAL, width=2)
    elif kind == "memory":
        tamga(d, 16, 16, 10, TEAL, 3)
        d.ellipse((6, 6, 26, 26), outline=TEAL, width=2)
    elif kind == "warning":
        d.polygon((16, 4, 29, 27, 3, 27), fill=RED)
        d.line((16, 11, 16, 21), fill=DARK, width=3)
        d.ellipse((14, 23, 18, 27), fill=DARK)
    else:
        d.rounded_rectangle((8, 7, 24, 25), 4, outline=GOLD, width=3)
        d.line((16, 11, 16, 21), fill=GOLD, width=2)
        d.line((11, 16, 21, 16), fill=GOLD, width=2)
    return img


def make_preview(files):
    thumbs = []
    for path in files:
        im = Image.open(path).convert("RGBA")
        canvas = Image.new("RGBA", (96, 96), (29, 30, 25, 255))
        im.thumbnail((84, 84), Image.Resampling.LANCZOS)
        canvas.alpha_composite(im, ((96 - im.width) // 2, (96 - im.height) // 2))
        thumbs.append((path.name, canvas))
    cols = 8
    rows = (len(thumbs) + cols - 1) // cols
    preview = Image.new("RGBA", (cols * 134, rows * 122), (239, 229, 207, 255))
    d = ImageDraw.Draw(preview, "RGBA")
    for i, (name, thumb) in enumerate(thumbs):
        x = (i % cols) * 134
        y = (i // cols) * 122
        preview.alpha_composite(thumb, (x + 17, y + 8))
        d.text((x + 8, y + 105), name[:18], fill=(35, 39, 50, 255))
    preview.save(PREVIEW_DIR / "batch_c_runtime_preview.png")


def write_manifest(files):
    lines = ["# Batch C Runtime Placeholder Manifest", ""]
    lines.append("Generated by `gamedesign/tools/generate_batch_c_assets.py`.")
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
    lines.append("`gamedesign/assets/concept/production_batch_c/batch_c_runtime_preview.png`")
    (PREVIEW_DIR / "batch_c_runtime_manifest.md").write_text("\n".join(lines), encoding="utf-8")


def main():
    ensure_dirs()
    files = []

    for kind in ["well", "small_camp", "clan_camp", "watchtower", "hunting_trail", "pack", "vision", "false_path", "buried_spring"]:
        save(tile(kind), "tiles", f"tile_{kind}_01.png", files)

    for kind in ["oasis", "mirage", "storm", "last_tamga", "well", "watchtower"]:
        mapped = {
            "oasis": "well",
            "mirage": "vision",
            "storm": "false_path",
            "last_tamga": "vision",
            "well": "well",
            "watchtower": "watchtower",
        }[kind]
        save(card_art(mapped), "cards", f"card_art_{kind}_64.png", files)

    save(tamga_post(), "aul", "aul_tamga_post_01.png", files)
    for stage, name in [
        (1, "aul_stage_01_camp.png"),
        (2, "aul_stage_02_settlement.png"),
        (3, "aul_stage_03_village.png"),
        (4, "aul_stage_04_fortified_aul.png"),
        (5, "aul_stage_05_steppe_capital.png"),
    ]:
        save(aul_stage(stage), "aul", name, files)

    for kind in ["body", "mind", "spirit"]:
        save(hero_panel(kind), "hero", f"hero_{kind}_panel.png", files)

    for kind, count in [
        ("intro_sand", 6),
        ("fire_glow", 4),
        ("last_tamga_spawn", 6),
        ("storm_veil", 6),
        ("card_reward", 4),
        ("near_death", 4),
    ]:
        for frame in range(count):
            save(fx(kind, frame, count), "fx", f"fx_{kind}_{frame:02d}.png", files)

    for kind in ["aul_upgrade", "deck", "map", "memory", "warning", "card_gain"]:
        save(icon(kind), "icons", f"icon_{kind}_32.png", files)

    make_preview(files)
    write_manifest(files)
    print(f"Generated {len(files)} Batch C PNG files")
    print(PREVIEW_DIR / "batch_c_runtime_preview.png")


if __name__ == "__main__":
    main()
