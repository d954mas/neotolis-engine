from pathlib import Path
import math

from PIL import Image, ImageDraw


ROOT = Path(__file__).resolve().parents[2]
RAW = ROOT / "games" / "turkic-jam-2026" / "raw" / "fx"
OUT = ROOT / "gamedesign" / "assets" / "concept" / "final_repaint_pass_4_fx"
S = 4

INK = (18, 15, 12, 255)
SAND = (218, 176, 89, 180)
SAND_LIGHT = (245, 202, 112, 170)
SAND_DARK = (104, 66, 35, 130)
TEAL = (44, 169, 157, 210)
TEAL_SOFT = (80, 210, 198, 120)
GOLD = (245, 188, 72, 210)
FIRE = (238, 104, 36, 220)
FIRE_LIGHT = (255, 205, 82, 210)
RED = (198, 72, 50, 215)
AMBER = (236, 142, 56, 200)
BONE = (226, 209, 166, 190)


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


def dust_step(frame: int) -> Image.Image:
    size = (64, 64)
    img, d = canvas(size)
    alpha = max(52, 170 - frame * 34)
    for i in range(3):
        x = 19 + i * 10 + frame * 3
        y = 42 + int(math.sin(i + frame) * 3)
        r = 4 + frame * 3 + i
        d.ellipse(box((x - r, y - r // 2, x + r, y + r // 2)), fill=(218, 176, 89, max(30, alpha - i * 26)))
    d.line(pts([(19 + frame * 3, 47), (44 + frame * 3, 45)]), fill=(104, 66, 35, max(28, alpha // 2)), width=sc(2))
    return down(img, size)


def tile_placed(frame: int) -> Image.Image:
    size = (64, 64)
    img, d = canvas(size)
    alpha = max(58, 230 - frame * 45)
    inset = 7 + frame * 4
    d.rounded_rectangle(box((inset, inset, 64 - inset, 64 - inset)), radius=sc(8), outline=(133, 207, 94, alpha), width=sc(4))
    d.rounded_rectangle(box((inset + 5, inset + 5, 59 - inset, 59 - inset)), radius=sc(5), outline=(245, 188, 72, max(38, alpha // 2)), width=sc(2))
    if frame < 3:
        tamga(d, 32, 32, 6 + frame, TEAL, 2)
    return down(img, size)


def tile_trigger(frame: int) -> Image.Image:
    size = (64, 64)
    img, d = canvas(size)
    alpha = max(70, 220 - frame * 42)
    tamga(d, 32, 32, 9 + frame * 4, (44, 169, 157, alpha), 4)
    d.ellipse(box((17 + frame * 3, 17 + frame * 3, 47 - frame * 3, 47 - frame * 3)), outline=(245, 188, 72, max(38, alpha // 2)), width=sc(2))
    return down(img, size)


def gain_popup(frame: int) -> Image.Image:
    size = (64, 64)
    img, d = canvas(size)
    alpha = max(72, 235 - frame * 58)
    cy = 29 - frame * 5
    d.ellipse(box((20, cy - 10, 44, cy + 14)), fill=(245, 188, 72, alpha), outline=(18, 15, 12, min(190, alpha)), width=sc(2))
    d.line(pts([(32, cy - 6), (32, cy + 9)]), fill=INK, width=sc(3))
    d.line(pts([(24, cy + 2), (40, cy + 2)]), fill=INK, width=sc(3))
    return down(img, size)


def invalid_cell(frame: int) -> Image.Image:
    size = (64, 64)
    img, d = canvas(size)
    alpha = 220 - frame * 45
    inset = 8 + frame * 3
    d.rounded_rectangle(box((inset, inset, 64 - inset, 64 - inset)), radius=sc(7), outline=(198, 72, 50, alpha), width=sc(4))
    d.line(pts([(20, 20), (44, 44)]), fill=(198, 72, 50, alpha), width=sc(5))
    d.line(pts([(44, 20), (20, 44)]), fill=(236, 142, 56, max(90, alpha - 30)), width=sc(3))
    return down(img, size)


def intro_sand(frame: int) -> Image.Image:
    size = (128, 128)
    img, d = canvas(size)
    base = 175 - frame * 14
    for i, y in enumerate(range(13 + frame * 5, 142, 18)):
        a = max(48, base - i * 12)
        d.arc(box((-26, y - 36, 154, y + 34)), 190, 350, fill=(218, 176, 89, a), width=sc(4))
    for i in range(11):
        x = (i * 23 + frame * 11) % 128
        y = 26 + ((i * 31 + frame * 7) % 82)
        d.ellipse(box((x, y, x + 3, y + 2)), fill=(245, 202, 112, max(42, base // 2)))
    return down(img, size)


def fire_glow(frame: int) -> Image.Image:
    size = (128, 128)
    img, d = canvas(size)
    r = 22 + frame * 9
    d.ellipse(box((64 - r, 64 - r, 64 + r, 64 + r)), fill=(245, 188, 72, 78 + frame * 12))
    d.ellipse(box((45, 75, 83, 96)), fill=(55, 36, 22, 150))
    d.polygon(pts([(56, 77), (64, 36 + frame * 2), (74, 77)]), fill=FIRE)
    d.polygon(pts([(50, 80), (61, 55), (68, 82)]), fill=FIRE_LIGHT)
    return down(img, size)


def last_tamga_spawn(frame: int) -> Image.Image:
    size = (128, 128)
    img, d = canvas(size)
    alpha = max(72, 220 - frame * 20)
    d.rounded_rectangle(box((48, 55, 80, 86)), radius=sc(5), fill=(151, 125, 82, max(82, alpha // 2)), outline=(127, 87, 44, alpha), width=sc(2))
    tamga(d, 64, 70, 9 + frame * 3, (44, 169, 157, alpha), 4)
    d.ellipse(box((23 + frame * 3, 23 + frame * 3, 105 - frame * 3, 105 - frame * 3)), outline=(44, 169, 157, max(70, alpha)), width=sc(3))
    for a in [0, 90, 180, 270]:
        x = 64 + int(math.cos(math.radians(a + frame * 14)) * (35 - frame * 2))
        y = 64 + int(math.sin(math.radians(a + frame * 14)) * (35 - frame * 2))
        d.ellipse(box((x - 3, y - 3, x + 3, y + 3)), fill=(245, 188, 72, max(74, alpha // 2)))
    return down(img, size)


def storm_veil(frame: int) -> Image.Image:
    size = (128, 128)
    img, d = canvas(size)
    for y in range(-8, 148, 18):
        yy = y + frame * 7
        d.line(pts([(0, yy), (128, yy - 26)]), fill=(218, 176, 89, 112), width=sc(8))
        d.line(pts([(0, yy + 6), (128, yy - 20)]), fill=(104, 66, 35, 42), width=sc(2))
    return down(img, size)


def card_reward(frame: int) -> Image.Image:
    size = (128, 128)
    img, d = canvas(size)
    alpha = max(72, 225 - frame * 36)
    grow = frame * 4
    d.rounded_rectangle(box((40 - grow, 25 - frame * 2, 88 + grow, 103 + frame * 2)), radius=sc(8), outline=(245, 188, 72, alpha), width=sc(5))
    d.rounded_rectangle(box((48 - frame * 2, 36, 80 + frame * 2, 92)), radius=sc(5), outline=(44, 169, 157, max(62, alpha // 2)), width=sc(3))
    tamga(d, 64, 64, 13, TEAL, 3)
    return down(img, size)


def near_death(frame: int) -> Image.Image:
    size = (128, 128)
    img, d = canvas(size)
    alpha = max(86, 220 - frame * 34)
    inset = 18 + frame * 5
    d.ellipse(box((inset, inset, 128 - inset, 128 - inset)), outline=(198, 72, 50, alpha), width=sc(6))
    d.polygon(pts([(64, 31), (95, 88), (33, 88)]), outline=(198, 72, 50, alpha), fill=(198, 72, 50, max(30, alpha // 5)))
    d.line(pts([(64, 47), (64, 74)]), fill=(198, 72, 50, alpha), width=sc(5))
    d.ellipse(box((61, 81, 67, 87)), fill=(198, 72, 50, alpha))
    return down(img, size)


def save(img: Image.Image, name: str, written: list[Path]) -> None:
    path = RAW / name
    path.parent.mkdir(parents=True, exist_ok=True)
    img.save(path)
    OUT.mkdir(parents=True, exist_ok=True)
    img.save(OUT / name)
    written.append(path)


def contact_sheet(paths: list[Path]) -> None:
    cells = []
    for path in paths:
        img = Image.open(path).convert("RGBA")
        thumb = Image.new("RGBA", (136, 136), (27, 29, 25, 255))
        img.thumbnail((118, 118), Image.Resampling.LANCZOS)
        thumb.alpha_composite(img, ((136 - img.width) // 2, (136 - img.height) // 2))
        cells.append((path.name, thumb))
    cols = 8
    rows = (len(cells) + cols - 1) // cols
    sheet = Image.new("RGBA", (cols * 168, rows * 170), (238, 229, 207, 255))
    d = ImageDraw.Draw(sheet, "RGBA")
    for i, (name, thumb) in enumerate(cells):
        x = (i % cols) * 168
        y = (i // cols) * 170
        sheet.alpha_composite(thumb, (x + 16, y + 8))
        d.text((x + 6, y + 148), name[:24], fill=(30, 27, 23, 255))
    OUT.mkdir(parents=True, exist_ok=True)
    sheet.save(OUT / "final_repaint_pass_4_fx_contact_sheet.png")


def main() -> None:
    written: list[Path] = []
    for i in range(4):
        save(dust_step(i), f"fx_dust_step_{i:02d}.png", written)
    for i in range(4):
        save(tile_placed(i), f"fx_tile_placed_{i:02d}.png", written)
    for i in range(4):
        save(tile_trigger(i), f"fx_tile_trigger_{i:02d}.png", written)
    for i in range(3):
        save(gain_popup(i), f"fx_gain_popup_{i:02d}.png", written)
    for i in range(2):
        save(invalid_cell(i), f"fx_invalid_cell_{i:02d}.png", written)
    for i in range(6):
        save(intro_sand(i), f"fx_intro_sand_{i:02d}.png", written)
    for i in range(4):
        save(fire_glow(i), f"fx_fire_glow_{i:02d}.png", written)
    for i in range(6):
        save(last_tamga_spawn(i), f"fx_last_tamga_spawn_{i:02d}.png", written)
    for i in range(6):
        save(storm_veil(i), f"fx_storm_veil_{i:02d}.png", written)
    for i in range(4):
        save(card_reward(i), f"fx_card_reward_{i:02d}.png", written)
    for i in range(4):
        save(near_death(i), f"fx_near_death_{i:02d}.png", written)
    contact_sheet(written)
    for path in written:
        print(path.relative_to(ROOT).as_posix())
    print((OUT / "final_repaint_pass_4_fx_contact_sheet.png").relative_to(ROOT).as_posix())


if __name__ == "__main__":
    main()
