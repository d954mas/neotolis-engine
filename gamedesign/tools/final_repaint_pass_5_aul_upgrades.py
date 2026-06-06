from pathlib import Path
import math

from PIL import Image, ImageDraw


ROOT = Path(__file__).resolve().parents[2]
RAW = ROOT / "games" / "turkic-jam-2026" / "raw" / "aul"
OUT = ROOT / "gamedesign" / "assets" / "concept" / "final_repaint_pass_5_aul_upgrades"
S = 4

INK = (18, 15, 12, 255)
SAND = (177, 122, 61, 210)
SAND_EDGE = (112, 72, 36, 150)
FELT = (236, 217, 171, 255)
FELT_SHADE = (210, 189, 146, 255)
WOOD = (94, 57, 32, 255)
WOOD_DARK = (56, 35, 23, 230)
TEAL = (44, 169, 157, 255)
RED = (156, 52, 38, 255)
GOLD = (245, 188, 72, 230)
WATER = (49, 145, 160, 155)
GREEN = (78, 137, 76, 180)
SHADOW = (0, 0, 0, 76)


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


def yurt(d: ImageDraw.ImageDraw, x: int, y: int, scale: float = 1.0, accent=RED, door=True) -> None:
    w = int(25 * scale)
    h = int(23 * scale)
    d.ellipse(box((x - w - 6, y + h - 2, x + w + 6, y + h + 11)), fill=SHADOW)
    d.polygon(pts([(x - w, y), (x, y - int(30 * scale)), (x + w, y)]), fill=FELT, outline=INK)
    d.rounded_rectangle(box((x - w + 3, y - 1, x + w - 3, y + h)), radius=sc(6), fill=FELT_SHADE, outline=INK, width=sc(2))
    d.line(pts([(x - w + 7, y + 5), (x + w - 7, y + 5)]), fill=accent, width=sc(3))
    if door:
        d.rectangle(box((x - 7, y + 8, x + 7, y + h)), fill=accent, outline=INK)


def fire(d: ImageDraw.ImageDraw, x: int, y: int, scale: float = 1.0) -> None:
    r = int(13 * scale)
    d.ellipse(box((x - r - 8, y + 7, x + r + 8, y + 20)), fill=(56, 36, 22, 165))
    d.polygon(pts([(x - 6, y + 12), (x, y - int(20 * scale)), (x + 8, y + 12)]), fill=(238, 104, 36, 230))
    d.polygon(pts([(x - 13, y + 13), (x - 2, y - int(8 * scale)), (x + 4, y + 14)]), fill=(255, 205, 82, 220))


def fence(d: ImageDraw.ImageDraw, points: list[tuple[int, int]], posts=True, color=WOOD) -> None:
    d.line(pts(points), fill=color, width=sc(5), joint="curve")
    d.line(pts(points), fill=WOOD_DARK, width=sc(2), joint="curve")
    if posts:
        for x, y in points[::2]:
            d.rounded_rectangle(box((x - 4, y - 14, x + 4, y + 14)), radius=sc(3), fill=WOOD, outline=WOOD_DARK, width=sc(1))


def ground(d: ImageDraw.ImageDraw, stage: int) -> None:
    d.ellipse(box((34, 66, 222, 207)), fill=SAND, outline=SAND_EDGE, width=sc(4))
    d.ellipse(box((55, 84, 201, 191)), outline=(121, 77, 38, 90), width=sc(2))
    for i in range(8 + stage * 2):
        x = 58 + (i * 31) % 142
        y = 88 + (i * 47) % 90
        d.ellipse(box((x, y, x + 2, y + 1)), fill=(238, 190, 92, 70))


def storage(d: ImageDraw.ImageDraw, x: int, y: int) -> None:
    d.rounded_rectangle(box((x - 12, y - 8, x + 12, y + 8)), radius=sc(4), fill=(132, 84, 41, 230), outline=INK, width=sc(2))
    d.line(pts([(x - 10, y), (x + 10, y)]), fill=GOLD, width=sc(2))


def tamga_post_sprite() -> Image.Image:
    img, d = canvas((128, 128))
    d.ellipse(box((39, 100, 89, 114)), fill=SHADOW)
    d.line(pts([(64, 105), (64, 34)]), fill=INK, width=sc(9))
    d.line(pts([(64, 105), (64, 34)]), fill=WOOD, width=sc(6))
    d.rounded_rectangle(box((38, 20, 90, 56)), radius=sc(7), fill=(123, 86, 50, 255), outline=INK, width=sc(3))
    d.line(pts([(43, 50), (85, 50)]), fill=GOLD, width=sc(2))
    tamga(d, 64, 38, 13, TEAL, 4)
    d.line(pts([(48, 27), (80, 27)]), fill=RED, width=sc(2))
    return down(img, (128, 128))


def stage_sprite(stage: int) -> Image.Image:
    img, d = canvas((256, 256))
    ground(d, stage)

    if stage >= 2:
        fence(d, [(47, 198), (83, 205), (128, 209), (174, 205), (210, 198)], color=(106, 70, 36, 210))
    if stage >= 4:
        d.arc(box((24, 53, 232, 221)), 188, 346, fill=(98, 65, 34, 225), width=sc(9))
        for x, y in [(45, 165), (72, 194), (184, 194), (211, 164)]:
            d.rounded_rectangle(box((x - 5, y - 20, x + 5, y + 15)), radius=sc(3), fill=WOOD, outline=WOOD_DARK, width=sc(1))

    layouts = {
        1: [(96, 133, 0.82, RED), (139, 137, 0.72, TEAL), (161, 108, 0.55, RED)],
        2: [(78, 140, 0.76, RED), (124, 116, 0.95, TEAL), (171, 141, 0.76, RED), (104, 166, 0.62, TEAL)],
        3: [(74, 138, 0.74, RED), (118, 111, 1.0, TEAL), (166, 137, 0.75, RED), (100, 168, 0.64, TEAL), (188, 106, 0.62, RED)],
        4: [(68, 137, 0.68, RED), (112, 112, 0.88, TEAL), (158, 134, 0.68, RED), (96, 169, 0.6, TEAL), (188, 109, 0.62, RED), (180, 168, 0.56, TEAL)],
        5: [(64, 140, 0.64, RED), (113, 108, 1.08, TEAL), (160, 136, 0.68, RED), (95, 171, 0.58, TEAL), (190, 105, 0.58, RED), (151, 172, 0.58, TEAL), (80, 103, 0.52, GOLD)],
    }
    for x, y, scale, accent in layouts[stage]:
        yurt(d, x, y, scale, accent)

    fire(d, 128, 150, 1.0)

    if stage >= 1:
        storage(d, 66, 158)
    if stage >= 2:
        d.rounded_rectangle(box((110, 50, 146, 96)), radius=sc(5), fill=(119, 83, 48, 255), outline=INK, width=sc(3))
        tamga(d, 128, 73, 12, TEAL, 4)
        d.line(pts([(128, 96), (128, 116)]), fill=WOOD, width=sc(5))
    if stage >= 3:
        for x, y in [(60, 104), (204, 142), (149, 74)]:
            storage(d, x, y)
        d.line(pts([(75, 152), (124, 132), (181, 148)]), fill=(112, 72, 35, 80), width=sc(4))
    if stage >= 4:
        for x, y in [(54, 176), (202, 176)]:
            fire(d, x, y, 0.65)
        d.rectangle(box((105, 32, 151, 59)), fill=(136, 94, 48, 245), outline=INK, width=sc(3))
        d.line(pts([(109, 46), (147, 46)]), fill=TEAL, width=sc(4))
    if stage >= 5:
        d.arc(box((75, 78, 181, 184)), 210, 510, fill=(185, 139, 65, 150), width=sc(7))
        d.ellipse(box((104, 132, 152, 158)), fill=WATER, outline=(25, 78, 84, 150), width=sc(2))
        for x, y in [(96, 135), (158, 132)]:
            d.line(pts([(x, y), (x + 10, y - 24)]), fill=GREEN, width=sc(3))
        tamga(d, 128, 45, 11, GOLD, 3)
    return down(img, (256, 256))


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
        thumb = Image.new("RGBA", (160, 160), (27, 29, 25, 255))
        img.thumbnail((140, 140), Image.Resampling.LANCZOS)
        thumb.alpha_composite(img, ((160 - img.width) // 2, (160 - img.height) // 2))
        cells.append((path.name, thumb))
    sheet = Image.new("RGBA", (3 * 230, 2 * 198), (238, 229, 207, 255))
    d = ImageDraw.Draw(sheet, "RGBA")
    for i, (name, thumb) in enumerate(cells):
        x = (i % 3) * 230
        y = (i // 3) * 198
        sheet.alpha_composite(thumb, (x + 35, y + 8))
        d.text((x + 10, y + 171), name[:32], fill=(30, 27, 23, 255))
    OUT.mkdir(parents=True, exist_ok=True)
    sheet.save(OUT / "final_repaint_pass_5_aul_upgrades_contact_sheet.png")


def main() -> None:
    written: list[Path] = []
    save(tamga_post_sprite(), "aul_tamga_post_01.png", written)
    save(stage_sprite(1), "aul_stage_01_camp.png", written)
    save(stage_sprite(2), "aul_stage_02_settlement.png", written)
    save(stage_sprite(3), "aul_stage_03_village.png", written)
    save(stage_sprite(4), "aul_stage_04_fortified_aul.png", written)
    save(stage_sprite(5), "aul_stage_05_steppe_capital.png", written)
    contact_sheet(written)
    for path in written:
        print(path.relative_to(ROOT).as_posix())
    print((OUT / "final_repaint_pass_5_aul_upgrades_contact_sheet.png").relative_to(ROOT).as_posix())


if __name__ == "__main__":
    main()
