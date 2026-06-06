from pathlib import Path

from PIL import Image, ImageDraw


ROOT = Path(__file__).resolve().parents[2]
RAW = ROOT / "games" / "turkic-jam-2026" / "raw" / "equipment"
OUT = ROOT / "gamedesign" / "assets" / "concept" / "pass_10_slot_surface_correction"
S = 4

SLOT_FILES = [
    "equip_slot_weapon_01.png",
    "equip_slot_clothes_01.png",
    "equip_slot_tamga_01.png",
    "equip_slot_tool_01.png",
]


def sc(v: int) -> int:
    return v * S


def box(b: tuple[int, int, int, int]) -> tuple[int, int, int, int]:
    return tuple(sc(v) for v in b)


def make_base_slot() -> Image.Image:
    img = Image.new("RGBA", (64 * S, 64 * S), (0, 0, 0, 0))
    d = ImageDraw.Draw(img, "RGBA")

    ink = (18, 15, 12, 245)
    shadow = (0, 0, 0, 82)
    wood_dark = (54, 36, 24, 240)
    wood = (111, 72, 38, 245)
    felt = (187, 166, 126, 250)
    felt_light = (219, 199, 155, 235)
    teal = (44, 169, 157, 210)
    gold = (227, 171, 66, 225)

    d.rounded_rectangle(box((6, 8, 58, 61)), radius=sc(9), fill=shadow)
    d.rounded_rectangle(box((5, 5, 59, 59)), radius=sc(9), fill=wood_dark, outline=ink, width=sc(2))
    d.rounded_rectangle(box((9, 9, 55, 55)), radius=sc(7), fill=wood, outline=(34, 23, 17, 180), width=sc(2))
    d.rounded_rectangle(box((13, 13, 51, 51)), radius=sc(5), fill=felt, outline=(79, 51, 30, 190), width=sc(2))
    d.rounded_rectangle(box((17, 17, 47, 47)), radius=sc(4), fill=felt_light)

    for x, y in [(11, 11), (53, 11), (11, 53), (53, 53)]:
        d.ellipse(box((x - 2, y - 2, x + 2, y + 2)), fill=gold, outline=ink)

    d.line(box((19, 13, 45, 13)), fill=(245, 210, 119, 95), width=sc(1))
    d.line(box((13, 19, 13, 45)), fill=(245, 210, 119, 55), width=sc(1))
    d.arc(box((8, 8, 56, 56)), 218, 318, fill=teal, width=sc(2))
    return img.resize((64, 64), Image.Resampling.LANCZOS)


def contact_sheet(paths: list[Path]) -> None:
    sheet = Image.new("RGBA", (520, 180), (238, 229, 207, 255))
    d = ImageDraw.Draw(sheet, "RGBA")
    d.text((16, 12), "Equipment slot correction: one reusable base slot surface", fill=(30, 27, 23, 255))
    for i, path in enumerate(paths):
        img = Image.open(path).convert("RGBA")
        bg = Image.new("RGBA", (88, 88), (27, 29, 25, 255))
        bg.alpha_composite(img, (12, 12))
        x = 20 + i * 124
        y = 42
        sheet.alpha_composite(bg, (x, y))
        d.text((x, y + 96), path.name.replace("equip_slot_", "").replace("_01.png", ""), fill=(30, 27, 23, 255))
    OUT.mkdir(parents=True, exist_ok=True)
    sheet.save(OUT / "pass_10_slot_surface_correction_contact_sheet.png")


def main() -> None:
    OUT.mkdir(parents=True, exist_ok=True)
    base = make_base_slot()
    base.save(OUT / "equip_slot_base_64.png")
    written: list[Path] = []
    for name in SLOT_FILES:
        path = RAW / name
        base.save(path)
        base.save(OUT / name)
        written.append(path)
    contact_sheet(written)
    for path in written:
        print(path.relative_to(ROOT).as_posix())
    print((OUT / "equip_slot_base_64.png").relative_to(ROOT).as_posix())
    print((OUT / "pass_10_slot_surface_correction_contact_sheet.png").relative_to(ROOT).as_posix())


if __name__ == "__main__":
    main()
