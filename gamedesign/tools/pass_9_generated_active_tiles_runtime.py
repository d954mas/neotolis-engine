from pathlib import Path
import colorsys

from PIL import Image, ImageDraw, ImageFont


ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "gamedesign/assets/concept/pass_9_generated_active_tiles/pass_9_active_tile_source_sheet_v2.png"
OUT_DIR = ROOT / "games/turkic-jam-2026/raw/tiles"
CONTACT = ROOT / "gamedesign/assets/concept/pass_9_generated_active_tiles/pass_9_generated_active_tiles_runtime_contact_sheet.png"

ENTRIES = [
    ("tile_saxaul_01.png", "saxaul"),
    ("tile_yurt_01.png", "yurt"),
    ("tile_tamga_stone_01.png", "tamga"),
    ("tile_wolf_track_01.png", "trail"),
    ("tile_oasis_01.png", "oasis"),
    ("tile_mirage_01.png", "mirage"),
    ("tile_storm_01.png", "storm"),
    ("tile_last_tamga_01.png", "last_tamga"),
]


def chroma_to_alpha(img: Image.Image) -> Image.Image:
    src = img.convert("RGBA")
    px = src.load()
    width, height = src.size
    for y in range(height):
        for x in range(width):
            r, g, b, a = px[x, y]
            h, s, v = colorsys.rgb_to_hsv(r / 255.0, g / 255.0, b / 255.0)
            magenta_hue = 0.78 <= h <= 0.94
            magenta = magenta_hue and s > 0.24 and v > 0.28 and g < 185
            if magenta:
                px[x, y] = (r, g, b, 0)
    return src


def checker_to_alpha(img: Image.Image) -> Image.Image:
    src = img.convert("RGBA")
    px = src.load()
    width, height = src.size
    for y in range(height):
        for x in range(width):
            r, g, b, a = px[x, y]
            neutral = max(r, g, b) - min(r, g, b) <= 10
            if neutral and min(r, g, b) >= 214:
                px[x, y] = (r, g, b, 0)
    return src


def warm_remaining_magenta(img: Image.Image) -> Image.Image:
    src = img.convert("RGBA")
    px = src.load()
    width, height = src.size
    for y in range(height):
        for x in range(width):
            r, g, b, a = px[x, y]
            if a == 0:
                continue
            h, s, v = colorsys.rgb_to_hsv(r / 255.0, g / 255.0, b / 255.0)
            rose_or_magenta = (0.76 <= h <= 0.99 and s > 0.16) or (r > int(g * 1.14) and b > int(g * 1.04) and r > 115 and b > 75)
            if rose_or_magenta:
                value = int(v * 255.0)
                px[x, y] = (value, int(value * 0.72), int(value * 0.34), a)
    return src


def trim_and_fit(img: Image.Image) -> Image.Image:
    alpha = img.getchannel("A")
    bbox = alpha.getbbox()
    if bbox is None:
        return Image.new("RGBA", (128, 128), (0, 0, 0, 0))
    left, top, right, bottom = bbox
    pad = 10
    left = max(0, left - pad)
    top = max(0, top - pad)
    right = min(img.width, right + pad)
    bottom = min(img.height, bottom + pad)
    crop = img.crop((left, top, right, bottom))
    crop.thumbnail((116, 116), Image.Resampling.LANCZOS)
    out = Image.new("RGBA", (128, 128), (0, 0, 0, 0))
    x = (128 - crop.width) // 2
    y = (128 - crop.height) // 2
    out.alpha_composite(crop, (x, y))
    return out


def make_contact(entries: list[tuple[str, str]]) -> None:
    sheet = Image.new("RGBA", (4 * 150, 2 * 168), (32, 28, 22, 255))
    draw = ImageDraw.Draw(sheet)
    font = ImageFont.load_default()
    sand = Image.new("RGBA", (128, 128), (198, 144, 67, 255))
    for i, (filename, label) in enumerate(entries):
        col = i % 4
        row = i // 4
        x = col * 150 + 11
        y = row * 168 + 10
        tile = sand.copy()
        sprite = Image.open(OUT_DIR / filename).convert("RGBA")
        tile.alpha_composite(sprite, (0, 0))
        sheet.alpha_composite(tile, (x, y))
        draw.text((x, y + 132), label, fill=(236, 224, 198, 255), font=font)
    CONTACT.parent.mkdir(parents=True, exist_ok=True)
    sheet.save(CONTACT)


def main() -> None:
    source = Image.open(SOURCE).convert("RGBA")
    cell_w = source.width // 4
    cell_h = source.height // 2
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    for i, (filename, _) in enumerate(ENTRIES):
        col = i % 4
        row = i // 4
        crop = source.crop((col * cell_w, row * cell_h, (col + 1) * cell_w, (row + 1) * cell_h))
        sprite = trim_and_fit(warm_remaining_magenta(chroma_to_alpha(checker_to_alpha(crop))))
        sprite.save(OUT_DIR / filename)
        print(f"wrote {OUT_DIR / filename}")
    make_contact(ENTRIES)
    print(f"contact {CONTACT}")


if __name__ == "__main__":
    main()
