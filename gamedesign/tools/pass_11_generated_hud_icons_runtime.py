from pathlib import Path

from PIL import Image, ImageDraw, ImageFont


ROOT = Path(__file__).resolve().parents[2]
CONCEPT = ROOT / "gamedesign/assets/concept/pass_11_generated_hud_icons"
RAW = ROOT / "games/turkic-jam-2026/raw/icons"

SOURCE = CONCEPT / "pass_11_hud_icons_chroma_source.png"
CONTACT = CONCEPT / "pass_11_generated_hud_icons_contact_sheet.png"

ICONS = [
    ("icon_stamina_32.png", "stamina"),
    ("icon_supplies_32.png", "supplies"),
    ("icon_wisdom_32.png", "wisdom"),
    ("icon_glory_32.png", "glory"),
    ("icon_circle_32.png", "circle"),
    ("icon_day_32.png", "day"),
    ("icon_body_32.png", "body"),
    ("icon_mind_32.png", "mind"),
    ("icon_spirit_32.png", "spirit"),
    ("icon_last_tamga_32.png", "last_tamga"),
    ("icon_settings_32.png", "settings"),
    ("icon_speed_32.png", "speed"),
    ("icon_aul_upgrade_32.png", "aul_upgrade"),
    ("icon_card_gain_32.png", "card_gain"),
    ("icon_deck_32.png", "deck"),
    ("icon_map_32.png", "map"),
    ("icon_memory_32.png", "memory"),
    ("icon_warning_32.png", "warning"),
]


def remove_green_key(img: Image.Image) -> Image.Image:
    out = img.convert("RGBA")
    px = out.load()
    for y in range(out.height):
        for x in range(out.width):
            r, g, b, a = px[x, y]
            key = g > 125 and g > r * 1.35 and g > b * 1.35
            near_key = g > 105 and g > r * 1.18 and g > b * 1.18
            if key:
                px[x, y] = (r, g, b, 0)
            elif near_key:
                alpha = max(0, min(a, int((max(r, b) / max(g, 1)) * 132)))
                px[x, y] = (r, min(g, max(r, b)), b, alpha)
    return out


def alpha_bbox(img: Image.Image) -> tuple[int, int, int, int]:
    bbox = img.getchannel("A").getbbox()
    if bbox is None:
        raise RuntimeError("empty icon after key removal")
    return bbox


def fit_icon(img: Image.Image) -> Image.Image:
    crop = img.crop(alpha_bbox(img))
    scale = min(30 / crop.width, 30 / crop.height)
    size = (max(1, int(crop.width * scale)), max(1, int(crop.height * scale)))
    small = crop.resize(size, Image.Resampling.LANCZOS)
    out = Image.new("RGBA", (32, 32), (0, 0, 0, 0))
    out.alpha_composite(small, ((32 - size[0]) // 2, (32 - size[1]) // 2))
    return out


def slice_icons() -> list[tuple[str, str, Path]]:
    source = remove_green_key(Image.open(SOURCE))
    cell_w = source.width // 6
    cell_h = source.height // 3
    results = []
    RAW.mkdir(parents=True, exist_ok=True)

    for i, (filename, label) in enumerate(ICONS):
        col = i % 6
        row = i // 6
        margin_x = int(cell_w * 0.08)
        margin_y = int(cell_h * 0.08)
        box = (
            col * cell_w + margin_x,
            row * cell_h + margin_y,
            (col + 1) * cell_w - margin_x,
            (row + 1) * cell_h - margin_y,
        )
        icon = fit_icon(source.crop(box))
        path = RAW / filename
        icon.save(path)
        results.append((label, filename, path))
        print(f"wrote {path}")

    return results


def validate(icons: list[tuple[str, str, Path]]) -> None:
    for _, filename, path in icons:
        img = Image.open(path)
        if img.size != (32, 32):
            raise RuntimeError(f"{filename}: expected 32x32, got {img.size}")
        if img.mode != "RGBA":
            raise RuntimeError(f"{filename}: expected RGBA, got {img.mode}")
        bbox = img.getchannel("A").getbbox()
        if bbox is None:
            raise RuntimeError(f"{filename}: empty alpha")
        print(f"ok {filename} bbox={bbox}")


def paste_preview(dst: Image.Image, sprite: Image.Image, xy: tuple[int, int], size: int) -> None:
    tile = Image.new("RGBA", (size, size), (48, 42, 34, 255))
    tile.alpha_composite(sprite.resize((size, size), Image.Resampling.LANCZOS))
    dst.alpha_composite(tile, xy)


def make_contact(icons: list[tuple[str, str, Path]]) -> None:
    sheet = Image.new("RGBA", (980, 520), (34, 30, 24, 255))
    draw = ImageDraw.Draw(sheet)
    font = ImageFont.load_default()
    title = (240, 224, 190, 255)
    dim = (190, 176, 148, 255)

    draw.text((18, 14), "Pass 11 generated HUD icons - native 32x32", fill=title, font=font)
    draw.text((18, 258), "Gameplay preview - 24x24", fill=title, font=font)

    for i, (label, _, path) in enumerate(icons):
        col = i % 6
        row = i // 6
        x = 18 + col * 158
        y = 44 + row * 66
        img = Image.open(path).convert("RGBA")
        paste_preview(sheet, img, (x, y), 32)
        draw.text((x + 40, y + 9), label, fill=dim, font=font)

        y2 = 288 + row * 52
        paste_preview(sheet, img, (x, y2), 24)
        draw.text((x + 32, y2 + 6), label, fill=dim, font=font)

    CONTACT.parent.mkdir(parents=True, exist_ok=True)
    sheet.save(CONTACT)
    print(f"contact {CONTACT}")


def main() -> None:
    icons = slice_icons()
    validate(icons)
    make_contact(icons)


if __name__ == "__main__":
    main()
