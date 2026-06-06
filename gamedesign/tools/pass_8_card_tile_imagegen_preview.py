from pathlib import Path

from PIL import Image, ImageChops, ImageDraw


ROOT = Path(__file__).resolve().parents[2]
BASE = ROOT / "gamedesign" / "assets" / "concept" / "pass_8_generated_bitmap_repaint"
SRC = BASE / "pass_8_card_tile_source_sheet.png"
OUT = BASE / "preview"

NAMES = [
    "card_art_saxaul_64",
    "card_art_yurt_64",
    "card_art_tamga_stone_64",
    "card_art_wolf_track_64",
    "card_art_oasis_64",
    "card_art_mirage_64",
    "card_art_storm_64",
    "card_art_last_tamga_64",
    "card_art_well_64",
    "card_art_watchtower_64",
]


def crop_cell(img: Image.Image, col: int, row: int) -> Image.Image:
    w, h = img.size
    cell_w = w / 5.0
    cell_h = h / 2.0
    pad_x = cell_w * 0.07
    pad_y = cell_h * 0.07
    left = int(col * cell_w + pad_x)
    top = int(row * cell_h + pad_y)
    right = int((col + 1) * cell_w - pad_x)
    bottom = int((row + 1) * cell_h - pad_y)
    return img.crop((left, top, right, bottom))


def remove_chroma(img: Image.Image) -> Image.Image:
    rgba = img.convert("RGBA")
    pixels = rgba.load()
    w, h = rgba.size
    for y in range(h):
        for x in range(w):
            r, g, b, a = pixels[x, y]
            magenta_score = (int(r) + int(b)) - (2 * int(g))
            if r > 170 and b > 145 and g < 150 and magenta_score > 190:
                pixels[x, y] = (0, 0, 0, 0)
            elif r > 145 and b > 125 and g < 165 and magenta_score > 135:
                pixels[x, y] = (0, 0, 0, 0)
            elif a < 28:
                pixels[x, y] = (0, 0, 0, 0)
    bbox = rgba.getbbox()
    if bbox is None:
        return rgba
    subject = rgba.crop(bbox)
    canvas = Image.new("RGBA", rgba.size, (0, 0, 0, 0))
    canvas.alpha_composite(subject, ((rgba.size[0] - subject.size[0]) // 2, (rgba.size[1] - subject.size[1]) // 2))
    return canvas


def fit_square(img: Image.Image, size: int) -> Image.Image:
    bbox = img.getbbox()
    if bbox is None:
        return Image.new("RGBA", (size, size), (0, 0, 0, 0))
    subject = img.crop(bbox)
    scale = min((size - 8) / subject.size[0], (size - 8) / subject.size[1])
    new_size = (max(1, int(subject.size[0] * scale)), max(1, int(subject.size[1] * scale)))
    subject = subject.resize(new_size, Image.Resampling.LANCZOS)
    out = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    out.alpha_composite(subject, ((size - new_size[0]) // 2, (size - new_size[1]) // 2))
    return out


def make_contact_sheet(previews: list[tuple[str, Image.Image]]) -> None:
    cell = 128
    label_h = 24
    sheet = Image.new("RGBA", (5 * cell, 2 * (cell + label_h)), (35, 31, 24, 255))
    draw = ImageDraw.Draw(sheet)
    for idx, (name, img) in enumerate(previews):
        col = idx % 5
        row = idx // 5
        x = col * cell
        y = row * (cell + label_h)
        bg = Image.new("RGBA", (cell, cell), (231, 198, 128, 255))
        bg.alpha_composite(img.resize((96, 96), Image.Resampling.NEAREST), (16, 16))
        sheet.alpha_composite(bg, (x, y))
        draw.text((x + 6, y + cell + 4), name.replace("card_art_", "").replace("_64", "")[:18], fill=(245, 238, 220, 255))
    sheet.save(BASE / "pass_8_card_tile_preview_contact_sheet.png")


def main() -> None:
    OUT.mkdir(parents=True, exist_ok=True)
    img = Image.open(SRC).convert("RGBA")
    previews = []
    for idx, name in enumerate(NAMES):
        cell = crop_cell(img, idx % 5, idx // 5)
        alpha = remove_chroma(cell)
        preview = fit_square(alpha, 64)
        preview.save(OUT / f"{name}.png")
        previews.append((name, preview))
    make_contact_sheet(previews)
    print(f"source: {img.size[0]}x{img.size[1]}")
    print(f"previews: {len(previews)}")
    print(f"contact: {BASE / 'pass_8_card_tile_preview_contact_sheet.png'}")


if __name__ == "__main__":
    main()
