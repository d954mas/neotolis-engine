from pathlib import Path

from PIL import Image, ImageDraw


ROOT = Path(__file__).resolve().parents[2]
RAW = ROOT / "games" / "turkic-jam-2026" / "raw" / "equipment"
OUT = ROOT / "gamedesign" / "assets" / "concept" / "pass_10_slot_surface_correction"
SOURCE = Path(
    r"C:\Users\ROG\.codex\generated_images\019e9928-1183-71b0-8014-3e8cce9a7afb\ig_044e7e33590026fc016a23f1ec871c81919a3ec6cfdf41a821.png"
)

SLOT_FILES = [
    "equip_slot_weapon_01.png",
    "equip_slot_clothes_01.png",
    "equip_slot_tamga_01.png",
    "equip_slot_tool_01.png",
]


def remove_magenta(img: Image.Image) -> Image.Image:
    rgba = img.convert("RGBA")
    px = rgba.load()
    for y in range(rgba.height):
        for x in range(rgba.width):
            r, g, b, a = px[x, y]
            if r > 205 and b > 190 and g < 90:
                px[x, y] = (r, g, b, 0)
            elif r > 160 and b > 145 and g < 120 and abs(r - b) < 120:
                px[x, y] = (r, g, b, min(a, 40))
    return rgba


def crop_to_alpha(img: Image.Image) -> Image.Image:
    bbox = img.getchannel("A").getbbox()
    if bbox is None:
        raise RuntimeError("source has empty alpha")
    return img.crop(bbox)


def make_runtime_slot() -> Image.Image:
    source = Image.open(SOURCE).convert("RGBA")
    OUT.mkdir(parents=True, exist_ok=True)
    source.save(OUT / "equip_slot_generated_source.png")
    cleaned = remove_magenta(source)
    cleaned.save(OUT / "equip_slot_generated_source_chroma_removed.png")
    cropped = crop_to_alpha(cleaned)

    target = Image.new("RGBA", (64, 64), (0, 0, 0, 0))
    scale = min(60 / cropped.width, 60 / cropped.height)
    size = (max(1, int(cropped.width * scale)), max(1, int(cropped.height * scale)))
    small = cropped.resize(size, Image.Resampling.LANCZOS)
    target.alpha_composite(small, ((64 - size[0]) // 2, (64 - size[1]) // 2))
    return target


def contact_sheet(paths: list[Path], base: Image.Image) -> None:
    sheet = Image.new("RGBA", (660, 230), (238, 229, 207, 255))
    d = ImageDraw.Draw(sheet, "RGBA")
    d.text((16, 12), "Generated shared equipment slot: one base surface, four runtime aliases", fill=(30, 27, 23, 255))

    large = base.resize((128, 128), Image.Resampling.NEAREST)
    bg = Image.new("RGBA", (150, 150), (27, 29, 25, 255))
    bg.alpha_composite(large, (11, 11))
    sheet.alpha_composite(bg, (20, 42))
    d.text((20, 198), "base slot x2", fill=(30, 27, 23, 255))

    for i, path in enumerate(paths):
        img = Image.open(path).convert("RGBA")
        bg = Image.new("RGBA", (76, 76), (27, 29, 25, 255))
        bg.alpha_composite(img, (6, 6))
        x = 206 + i * 108
        y = 68
        sheet.alpha_composite(bg, (x, y))
        d.text((x, y + 84), path.name.replace("equip_slot_", "").replace("_01.png", ""), fill=(30, 27, 23, 255))
    sheet.save(OUT / "pass_10_generated_slot_surface_correction_contact_sheet.png")


def main() -> None:
    OUT.mkdir(parents=True, exist_ok=True)
    slot = make_runtime_slot()
    slot.save(OUT / "equip_slot_base_generated_64.png")

    written: list[Path] = []
    for name in SLOT_FILES:
        path = RAW / name
        slot.save(path)
        slot.save(OUT / name)
        written.append(path)
    contact_sheet(written, slot)

    for path in written:
        print(path.relative_to(ROOT).as_posix())
    print((OUT / "equip_slot_generated_source.png").relative_to(ROOT).as_posix())
    print((OUT / "equip_slot_base_generated_64.png").relative_to(ROOT).as_posix())
    print((OUT / "pass_10_generated_slot_surface_correction_contact_sheet.png").relative_to(ROOT).as_posix())


if __name__ == "__main__":
    main()
