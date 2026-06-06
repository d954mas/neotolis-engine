from pathlib import Path

from PIL import Image, ImageDraw


ROOT = Path(__file__).resolve().parents[2]
BASE = ROOT / "gamedesign" / "assets" / "concept" / "pass_8_generated_bitmap_repaint"
SRC = BASE / "pass_8_card_tile_source_sheet.png"
RAW = ROOT / "games" / "turkic-jam-2026" / "raw" / "cards"

ASSETS = [
    ("card_art_saxaul_64", "card_art_saxaul_64.png"),
    ("card_art_yurt_64", "card_art_yurt_64.png"),
    ("card_art_tamga_stone_64", "card_art_tamga_stone_64.png"),
    ("card_art_wolf_track_64", "card_art_wolf_track_64.png"),
    ("card_art_oasis_64", "card_art_oasis_64.png"),
    ("card_art_mirage_64", "card_art_mirage_64.png"),
    ("card_art_storm_64", "card_art_storm_64.png"),
    ("card_art_last_tamga_64", "card_art_last_tamga_64.png"),
    ("card_art_well_64", "card_art_well_64.png"),
    ("card_art_watchtower_64", "card_art_watchtower_64.png"),
]

SAND = (229, 197, 127, 255)
SAND_DARK = (181, 137, 76, 255)


def crop_cell(img: Image.Image, col: int, row: int) -> Image.Image:
    w, h = img.size
    cell_w = w / 5.0
    cell_h = h / 2.0
    pad_x = cell_w * 0.06
    pad_y = cell_h * 0.06
    return img.crop(
        (
            int(col * cell_w + pad_x),
            int(row * cell_h + pad_y),
            int((col + 1) * cell_w - pad_x),
            int((row + 1) * cell_h - pad_y),
        )
    )


def replace_chroma_with_sand(img: Image.Image) -> Image.Image:
    rgba = img.convert("RGBA")
    pixels = rgba.load()
    w, h = rgba.size
    for y in range(h):
        for x in range(w):
            r, g, b, a = pixels[x, y]
            magenta_score = (int(r) + int(b)) - (2 * int(g))
            if r > 145 and b > 125 and g < 170 and magenta_score > 125:
                shade = int(10 * (y / max(1, h - 1)))
                pixels[x, y] = (SAND[0] - shade, SAND[1] - shade, SAND[2] - shade, 255)
            elif a < 255:
                pixels[x, y] = (r, g, b, 255)
    return rgba


def make_card_art(img: Image.Image) -> Image.Image:
    card = replace_chroma_with_sand(img)
    card = card.resize((64, 64), Image.Resampling.LANCZOS)
    draw = ImageDraw.Draw(card)
    draw.rectangle((0, 0, 63, 63), outline=SAND_DARK, width=1)
    return card


def make_contact_sheet(cards: list[tuple[str, Image.Image]]) -> None:
    cell = 96
    label_h = 22
    sheet = Image.new("RGBA", (5 * cell, 2 * (cell + label_h)), (35, 31, 24, 255))
    draw = ImageDraw.Draw(sheet)
    for idx, (name, card) in enumerate(cards):
        col = idx % 5
        row = idx // 5
        x = col * cell
        y = row * (cell + label_h)
        enlarged = card.resize((64, 64), Image.Resampling.NEAREST)
        sheet.alpha_composite(enlarged, (x + 16, y + 8))
        draw.text((x + 4, y + 76), name.replace("card_art_", "").replace("_64", "")[:17], fill=(245, 238, 220, 255))
    sheet.save(BASE / "pass_8_generated_card_art_runtime_contact_sheet.png")


def main() -> None:
    img = Image.open(SRC).convert("RGBA")
    RAW.mkdir(parents=True, exist_ok=True)
    cards: list[tuple[str, Image.Image]] = []
    for idx, (asset_id, filename) in enumerate(ASSETS):
        source = crop_cell(img, idx % 5, idx // 5)
        card = make_card_art(source)
        card.save(RAW / filename)
        cards.append((asset_id, card))
    make_contact_sheet(cards)
    print("updated runtime card art:")
    for _, filename in ASSETS:
        print(f"  raw/cards/{filename}")
    print(f"contact: {BASE / 'pass_8_generated_card_art_runtime_contact_sheet.png'}")


if __name__ == "__main__":
    main()
