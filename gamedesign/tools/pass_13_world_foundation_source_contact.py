from pathlib import Path

from PIL import Image, ImageDraw


ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "gamedesign" / "assets" / "concept" / "pass_13_generated_world_foundation"
SOURCE = Path(
    r"C:\Users\ROG\.codex\generated_images\019e9928-1183-71b0-8014-3e8cce9a7afb\ig_0fbf54c1d2c2c804016a23f561121c8191a695ea545a0aec4d.png"
)


def remove_magenta(img: Image.Image) -> Image.Image:
    rgba = img.convert("RGBA")
    px = rgba.load()
    for y in range(rgba.height):
        for x in range(rgba.width):
            r, g, b, a = px[x, y]
            if r > 210 and b > 190 and g < 95:
                px[x, y] = (r, g, b, 0)
            elif r > 165 and b > 150 and g < 125 and abs(r - b) < 125:
                px[x, y] = (r, g, b, min(a, 45))
    return rgba


def fit(img: Image.Image, size: tuple[int, int]) -> Image.Image:
    out = Image.new("RGBA", size, (27, 29, 25, 255))
    tmp = img.copy()
    tmp.thumbnail((size[0] - 16, size[1] - 16), Image.Resampling.LANCZOS)
    out.alpha_composite(tmp.convert("RGBA"), ((size[0] - tmp.width) // 2, (size[1] - tmp.height) // 2))
    return out


def contact_sheet(source: Image.Image) -> Image.Image:
    sheet = Image.new("RGBA", (1280, 900), (238, 229, 207, 255))
    d = ImageDraw.Draw(sheet, "RGBA")
    d.text((20, 16), "Pass 13 world foundation source - one generated bitmap family sheet", fill=(30, 27, 23, 255))

    overview = fit(source, (760, 500))
    sheet.alpha_composite(overview, (20, 44))
    d.text((20, 552), "Full source overview", fill=(30, 27, 23, 255))

    crops = [
        ("ground sand base", (0, 0, 520, 330)),
        ("quiet decor overlays", (500, 0, 910, 455)),
        ("road material", (980, 0, 1536, 735)),
        ("road buffer/no-build", (0, 430, 940, 705)),
        ("aul core", (0, 700, 1536, 1024)),
    ]
    for i, (label, rect) in enumerate(crops):
        crop = source.crop(rect)
        thumb = fit(crop, (220, 150))
        x = 820 + (i % 2) * 230
        y = 44 + (i // 2) * 200
        sheet.alpha_composite(thumb, (x, y))
        d.text((x, y + 158), label, fill=(30, 27, 23, 255))

    d.text((20, 620), "Review focus:", fill=(30, 27, 23, 255))
    notes = [
        "ground: quiet enough at larger map scale, avoid flat square feeling",
        "decor: lower contrast than active tile objects, no green placeholder cells",
        "road: reusable without obvious stamped repetition",
        "buffer: visible no-build edge, not fence/wall",
        "aul: small camp/standing place, not town/palace",
    ]
    for i, note in enumerate(notes):
        d.text((20, 650 + i * 28), f"- {note}", fill=(30, 27, 23, 255))
    return sheet


def main() -> None:
    OUT.mkdir(parents=True, exist_ok=True)
    source = Image.open(SOURCE).convert("RGBA")
    source.save(OUT / "pass_13_world_foundation_source.png")
    remove_magenta(source).save(OUT / "pass_13_world_foundation_chroma_removed.png")
    contact_sheet(source).save(OUT / "pass_13_world_foundation_contact_sheet.png")
    print((OUT / "pass_13_world_foundation_source.png").relative_to(ROOT).as_posix())
    print((OUT / "pass_13_world_foundation_chroma_removed.png").relative_to(ROOT).as_posix())
    print((OUT / "pass_13_world_foundation_contact_sheet.png").relative_to(ROOT).as_posix())


if __name__ == "__main__":
    main()
