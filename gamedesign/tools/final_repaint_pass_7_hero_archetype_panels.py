from pathlib import Path

from PIL import Image, ImageDraw


ROOT = Path(__file__).resolve().parents[2]
RAW = ROOT / "games" / "turkic-jam-2026" / "raw"
OUT = ROOT / "gamedesign" / "assets" / "concept" / "final_repaint_pass_7_hero_archetype_panels"
S = 4

INK = (18, 15, 12, 255)
FELT = (43, 101, 95, 255)
FELT_DARK = (24, 52, 50, 255)
CLOAK = (79, 62, 45, 255)
CLOAK_HEAVY = (64, 51, 40, 255)
SKIN = (204, 133, 77, 255)
WOOD = (95, 58, 32, 255)
TEAL = (44, 169, 157, 255)
RED = (156, 52, 38, 255)
GOLD = (245, 188, 72, 255)
BONE = (226, 209, 166, 255)
SAND = (199, 139, 63, 230)
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


def tamga(d: ImageDraw.ImageDraw, cx: int, cy: int, s: int, color=TEAL, width: int = 3) -> None:
    cx, cy, s, width = sc(cx), sc(cy), sc(s), sc(width)
    d.line((cx, cy - s, cx, cy + s), fill=color, width=width)
    d.line((cx - s, cy - s // 2, cx, cy - s, cx + s, cy - s // 2), fill=color, width=width)
    d.line((cx - s, cy + s // 2, cx, cy, cx + s, cy + s // 2), fill=color, width=width)


def head_and_hat(d: ImageDraw.ImageDraw, cx: int, y: int, accent=TEAL) -> None:
    d.ellipse(box((cx - 13, y, cx + 13, y + 27)), fill=SKIN, outline=INK, width=sc(2))
    d.polygon(pts([(cx - 20, y + 20), (cx + 20, y + 20), (cx + 13, y + 41), (cx - 13, y + 41)]), fill=(45, 31, 23, 255), outline=INK)
    d.line(pts([(cx - 15, y + 29), (cx + 15, y + 29)]), fill=accent, width=sc(3))


def boots(d: ImageDraw.ImageDraw, cx: int, y: int, spread: int) -> None:
    d.line(pts([(cx - 10, y), (cx - spread, y + 44)]), fill=INK, width=sc(8))
    d.line(pts([(cx + 10, y), (cx + spread, y + 44)]), fill=INK, width=sc(8))
    d.line(pts([(cx - 10, y), (cx - spread, y + 42)]), fill=(52, 40, 32, 255), width=sc(5))
    d.line(pts([(cx + 10, y), (cx + spread, y + 42)]), fill=(52, 40, 32, 255), width=sc(5))
    d.ellipse(box((cx - spread - 9, y + 37, cx - spread + 10, y + 47)), fill=INK)
    d.ellipse(box((cx + spread - 10, y + 37, cx + spread + 9, y + 47)), fill=INK)


def panel_body() -> Image.Image:
    img, d = canvas((128, 192))
    d.ellipse(box((29, 169, 99, 188)), fill=SHADOW)
    head_and_hat(d, 64, 16, RED)
    d.rounded_rectangle(box((40, 67, 88, 132)), radius=sc(12), fill=FELT_DARK, outline=INK, width=sc(3))
    d.line(pts([(46, 94), (82, 94)]), fill=RED, width=sc(5))
    d.polygon(pts([(39, 78), (18, 133), (43, 147), (51, 94)]), fill=CLOAK_HEAVY, outline=INK)
    d.polygon(pts([(89, 78), (111, 133), (85, 147), (77, 94)]), fill=CLOAK_HEAVY, outline=INK)
    d.line(pts([(25, 126), (103, 86)]), fill=INK, width=sc(7))
    d.line(pts([(25, 126), (103, 86)]), fill=WOOD, width=sc(4))
    d.rounded_rectangle(box((28, 91, 48, 124)), radius=sc(6), fill=(122, 78, 43, 255), outline=INK, width=sc(2))
    d.line(pts([(38, 91), (38, 124)]), fill=GOLD, width=sc(2))
    boots(d, 64, 132, 23)
    tamga(d, 64, 111, 10, GOLD, 3)
    return down(img, (128, 192))


def panel_mind() -> Image.Image:
    img, d = canvas((128, 192))
    d.ellipse(box((32, 170, 96, 187)), fill=SHADOW)
    head_and_hat(d, 64, 17, TEAL)
    d.rounded_rectangle(box((43, 68, 85, 132)), radius=sc(11), fill=FELT, outline=INK, width=sc(3))
    d.line(pts([(49, 89), (80, 89)]), fill=TEAL, width=sc(5))
    d.polygon(pts([(43, 77), (24, 132), (45, 143), (52, 94)]), fill=CLOAK, outline=INK)
    d.polygon(pts([(85, 77), (105, 132), (83, 143), (76, 94)]), fill=CLOAK, outline=INK)
    d.polygon(pts([(24, 105), (54, 96), (74, 105), (101, 96), (101, 128), (73, 137), (52, 128), (24, 137)]), fill=BONE, outline=INK)
    d.line(pts([(54, 96), (54, 128), (74, 105), (74, 137)]), fill=SAND, width=sc(2))
    d.line(pts([(33, 126), (52, 116), (68, 121), (92, 111)]), fill=TEAL, width=sc(3))
    d.line(pts([(88, 75), (100, 124)]), fill=INK, width=sc(4))
    d.line(pts([(88, 75), (100, 124)]), fill=WOOD, width=sc(2))
    for x, y in [(97, 129), (101, 135), (92, 138)]:
        d.ellipse(box((x - 3, y - 3, x + 3, y + 3)), fill=GOLD, outline=INK)
    boots(d, 64, 132, 16)
    return down(img, (128, 192))


def panel_spirit() -> Image.Image:
    img, d = canvas((128, 192))
    d.ellipse(box((30, 170, 98, 188)), fill=SHADOW)
    d.arc(box((19, 45, 109, 149)), 205, 335, fill=(44, 169, 157, 105), width=sc(5))
    d.arc(box((28, 36, 100, 134)), 210, 330, fill=(245, 188, 72, 95), width=sc(3))
    head_and_hat(d, 64, 16, GOLD)
    d.rounded_rectangle(box((42, 68, 86, 135)), radius=sc(12), fill=(52, 80, 76, 255), outline=INK, width=sc(3))
    d.line(pts([(48, 93), (80, 93)]), fill=GOLD, width=sc(4))
    d.polygon(pts([(42, 78), (20, 137), (46, 150), (54, 95)]), fill=CLOAK, outline=INK)
    d.polygon(pts([(86, 78), (108, 137), (82, 150), (74, 95)]), fill=CLOAK, outline=INK)
    d.line(pts([(64, 96), (64, 128)]), fill=INK, width=sc(4))
    d.ellipse(box((52, 125, 76, 149)), outline=INK, width=sc(4))
    d.ellipse(box((55, 128, 73, 146)), outline=TEAL, width=sc(3))
    tamga(d, 64, 137, 7, GOLD, 2)
    d.line(pts([(97, 75), (97, 166)]), fill=INK, width=sc(5))
    d.line(pts([(97, 75), (97, 166)]), fill=WOOD, width=sc(3))
    d.polygon(pts([(99, 76), (119, 88), (99, 99)]), fill=TEAL, outline=INK)
    boots(d, 64, 135, 18)
    return down(img, (128, 192))


def icon_aul_upgrade() -> Image.Image:
    img, d = canvas((32, 32))
    d.ellipse(box((5, 20, 27, 29)), fill=SHADOW)
    d.polygon(pts([(7, 20), (16, 9), (25, 20)]), fill=FELT, outline=INK)
    d.rounded_rectangle(box((9, 19, 23, 28)), radius=sc(3), fill=(205, 184, 139, 255), outline=INK, width=sc(2))
    d.rectangle(box((14, 22, 18, 28)), fill=TEAL, outline=INK)
    d.polygon(pts([(16, 1), (27, 12), (20, 12), (20, 18), (12, 18), (12, 12), (5, 12)]), fill=GOLD, outline=INK)
    return down(img, (32, 32))


def icon_settings() -> Image.Image:
    img, d = canvas((32, 32))
    d.rounded_rectangle(box((5, 8, 27, 24)), radius=sc(8), fill=INK)
    d.rounded_rectangle(box((7, 10, 25, 22)), radius=sc(6), fill=(67, 96, 89, 255))
    d.line(pts([(10, 16), (22, 16)]), fill=GOLD, width=sc(4))
    d.ellipse(box((13, 11, 21, 21)), fill=TEAL, outline=INK, width=sc(2))
    return down(img, (32, 32))


HEROES = {
    "hero_body_panel.png": panel_body,
    "hero_mind_panel.png": panel_mind,
    "hero_spirit_panel.png": panel_spirit,
}

ICONS = {
    "icon_aul_upgrade_32.png": icon_aul_upgrade,
    "icon_settings_32.png": icon_settings,
}


def save(img: Image.Image, folder: str, name: str, written: list[Path]) -> None:
    path = RAW / folder / name
    path.parent.mkdir(parents=True, exist_ok=True)
    img.save(path)
    OUT.mkdir(parents=True, exist_ok=True)
    img.save(OUT / name)
    written.append(path)


def contact_sheet(hero_paths: list[Path], icon_paths: list[Path]) -> None:
    width = 880
    height = 610
    sheet = Image.new("RGBA", (width, height), (238, 229, 207, 255))
    d = ImageDraw.Draw(sheet, "RGBA")
    d.text((20, 14), "Hero archetype panels - native 128x192", fill=(30, 27, 23, 255))
    for i, path in enumerate(hero_paths):
        img = Image.open(path).convert("RGBA")
        x = 38 + i * 190
        y = 42
        bg = Image.new("RGBA", (148, 212), (27, 29, 25, 255))
        bg.alpha_composite(img, (10, 10))
        sheet.alpha_composite(bg, (x, y))
        d.text((x, y + 218), path.name, fill=(30, 27, 23, 255))

    d.text((20, 296), "Reduced UI scale preview", fill=(30, 27, 23, 255))
    for i, path in enumerate(hero_paths):
        img = Image.open(path).convert("RGBA").resize((64, 96), Image.Resampling.LANCZOS)
        x = 70 + i * 150
        y = 325
        bg = Image.new("RGBA", (88, 120), (27, 29, 25, 255))
        bg.alpha_composite(img, (12, 12))
        sheet.alpha_composite(bg, (x, y))

    d.text((560, 296), "Icon fixes - 32px and 24px", fill=(30, 27, 23, 255))
    for i, path in enumerate(icon_paths):
        img = Image.open(path).convert("RGBA")
        small = img.resize((24, 24), Image.Resampling.LANCZOS)
        x = 575 + i * 135
        y = 333
        bg = Image.new("RGBA", (98, 78), (27, 29, 25, 255))
        bg.alpha_composite(img, (11, 10))
        bg.alpha_composite(small, (56, 14))
        sheet.alpha_composite(bg, (x, y))
        d.text((x, y + 84), path.name[:22], fill=(30, 27, 23, 255))

    OUT.mkdir(parents=True, exist_ok=True)
    sheet.save(OUT / "final_repaint_pass_7_hero_archetype_panels_contact_sheet.png")


def main() -> None:
    hero_paths: list[Path] = []
    icon_paths: list[Path] = []
    for name, make in HEROES.items():
        save(make(), "hero", name, hero_paths)
    for name, make in ICONS.items():
        save(make(), "icons", name, icon_paths)
    contact_sheet(hero_paths, icon_paths)
    for path in hero_paths + icon_paths:
        print(path.relative_to(ROOT).as_posix())
    print((OUT / "final_repaint_pass_7_hero_archetype_panels_contact_sheet.png").relative_to(ROOT).as_posix())


if __name__ == "__main__":
    main()
