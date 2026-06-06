from pathlib import Path

from PIL import Image, ImageChops, ImageDraw, ImageEnhance, ImageFilter, ImageFont


ROOT = Path(__file__).resolve().parents[2]
CONCEPT = ROOT / "gamedesign/assets/concept/pass_12_generated_ui_surfaces"
RAW = ROOT / "games/turkic-jam-2026/raw/ui"

SOURCE = CONCEPT / "pass_12_ui_material_source.png"
CONTACT = CONCEPT / "pass_12_generated_ui_surfaces_contact_sheet.png"


def source_cell(src: Image.Image, col: int, row: int) -> Image.Image:
    cw = src.width // 4
    # Generated sheet has large top/bottom gutters; use the actual swatch bands.
    x0 = col * cw + int(cw * 0.06)
    x1 = (col + 1) * cw - int(cw * 0.06)
    if row == 0:
        y0 = int(src.height * 0.18)
        y1 = int(src.height * 0.50)
    else:
        y0 = int(src.height * 0.52)
        y1 = int(src.height * 0.83)
    return src.crop((x0, y0, x1, y1)).convert("RGBA")


def fit_texture(tex: Image.Image, size: tuple[int, int]) -> Image.Image:
    scale = max(size[0] / tex.width, size[1] / tex.height)
    resized = tex.resize((max(1, int(tex.width * scale)), max(1, int(tex.height * scale))), Image.Resampling.LANCZOS)
    x = (resized.width - size[0]) // 2
    y = (resized.height - size[1]) // 2
    return resized.crop((x, y, x + size[0], y + size[1]))


def rounded_mask(size: tuple[int, int], radius: int, inset: int = 0) -> Image.Image:
    mask = Image.new("L", size, 0)
    draw = ImageDraw.Draw(mask)
    box = (inset, inset, size[0] - 1 - inset, size[1] - 1 - inset)
    draw.rounded_rectangle(box, radius=radius, fill=255)
    return mask


def apply_alpha(img: Image.Image, mask: Image.Image) -> Image.Image:
    out = img.convert("RGBA")
    out.putalpha(mask)
    return out


def border_overlay(size: tuple[int, int], radius: int, color: tuple[int, int, int, int], width: int) -> Image.Image:
    out = Image.new("RGBA", size, (0, 0, 0, 0))
    draw = ImageDraw.Draw(out)
    for i in range(width):
        draw.rounded_rectangle(
            (i, i, size[0] - 1 - i, size[1] - 1 - i),
            radius=max(1, radius - i),
            outline=color,
            width=1,
        )
    return out


def panel(tex: Image.Image, size: tuple[int, int], radius: int, edge: tuple[int, int, int, int]) -> Image.Image:
    base = fit_texture(tex, size)
    base = ImageEnhance.Contrast(base).enhance(1.08)
    mask = rounded_mask(size, radius)
    out = apply_alpha(base, mask)
    out.alpha_composite(border_overlay(size, radius, (0, 0, 0, 95), 2))
    out.alpha_composite(border_overlay(size, radius, edge, 2))
    return out


def card(tex: Image.Image, trim: Image.Image, size: tuple[int, int], selected: bool, back: bool = False) -> Image.Image:
    base = fit_texture(trim if back else tex, size)
    if not back:
        base = ImageEnhance.Brightness(base).enhance(1.05)
    mask = rounded_mask(size, 7)
    out = apply_alpha(base, mask)
    edge = (37, 93, 84, 230) if selected else (111, 84, 45, 210)
    accent = (219, 177, 78, 235) if selected else (229, 202, 139, 210)
    out.alpha_composite(border_overlay(size, 7, (0, 0, 0, 100), 2))
    out.alpha_composite(border_overlay(size, 7, edge, 2))
    out.alpha_composite(border_overlay((size[0] - 8, size[1] - 8), 5, accent, 1), (4, 4))
    if selected:
        glow = border_overlay(size, 7, (49, 189, 169, 150), 3).filter(ImageFilter.GaussianBlur(0.4))
        out = Image.alpha_composite(glow, out)
    return out


def slot(tex: Image.Image, size: tuple[int, int], accent: tuple[int, int, int, int]) -> Image.Image:
    base = fit_texture(tex, size)
    base = ImageEnhance.Brightness(base).enhance(0.88)
    mask = rounded_mask(size, 6)
    out = apply_alpha(base, mask)
    out.alpha_composite(border_overlay(size, 6, (0, 0, 0, 145), 2))
    out.alpha_composite(border_overlay((size[0] - 8, size[1] - 8), 4, accent, 1), (4, 4))
    return out


def chip(tex: Image.Image, size: tuple[int, int]) -> Image.Image:
    base = fit_texture(tex, size)
    base = ImageEnhance.Brightness(base).enhance(1.02)
    mask = rounded_mask(size, 8)
    out = apply_alpha(base, mask)
    out.alpha_composite(border_overlay(size, 8, (0, 0, 0, 125), 2))
    out.alpha_composite(border_overlay((size[0] - 8, size[1] - 8), 5, (207, 164, 74, 210), 1), (4, 4))
    return out


def write_outputs() -> list[tuple[str, Path]]:
    src = Image.open(SOURCE).convert("RGBA")
    dark_felt = source_cell(src, 0, 0)
    leather = source_cell(src, 1, 0)
    card_body = source_cell(src, 2, 0)
    light_felt = source_cell(src, 3, 0)
    woven = source_cell(src, 0, 1)
    gold = source_cell(src, 1, 1)
    tooltip = source_cell(src, 2, 1)
    selected_trim = source_cell(src, 3, 1)

    outputs = [
        ("ui_panel_felt_dark_96.png", panel(dark_felt, (96, 96), 7, (94, 83, 60, 190))),
        ("ui_panel_felt_light_96.png", panel(light_felt, (96, 96), 7, (190, 159, 93, 205))),
        ("ui_card_playable_96x128.png", card(card_body, woven, (96, 128), False)),
        ("ui_card_selected_96x128.png", card(card_body, selected_trim, (96, 128), True)),
        ("ui_card_back_96x128.png", card(card_body, woven, (96, 128), False, True)),
        ("ui_slot_equipment_64.png", slot(leather, (64, 64), (67, 141, 127, 210))),
        ("ui_chip_resource_64.png", chip(gold, (64, 64))),
        ("ui_tooltip_dark_64.png", panel(tooltip, (64, 64), 6, (92, 78, 55, 180))),
        ("ui_button_dark_64.png", slot(dark_felt, (64, 64), (217, 175, 77, 190))),
    ]

    RAW.mkdir(parents=True, exist_ok=True)
    written = []
    for filename, img in outputs:
        path = RAW / filename
        img.save(path)
        written.append((filename, path))
        print(f"wrote {path}")
    return written


def validate(files: list[tuple[str, Path]]) -> None:
    expected = {
        "ui_panel_felt_dark_96.png": (96, 96),
        "ui_panel_felt_light_96.png": (96, 96),
        "ui_card_playable_96x128.png": (96, 128),
        "ui_card_selected_96x128.png": (96, 128),
        "ui_card_back_96x128.png": (96, 128),
        "ui_slot_equipment_64.png": (64, 64),
        "ui_chip_resource_64.png": (64, 64),
        "ui_tooltip_dark_64.png": (64, 64),
        "ui_button_dark_64.png": (64, 64),
    }
    for filename, path in files:
        img = Image.open(path)
        if img.size != expected[filename]:
            raise RuntimeError(f"{filename}: expected {expected[filename]}, got {img.size}")
        if img.mode != "RGBA":
            raise RuntimeError(f"{filename}: expected RGBA, got {img.mode}")
        bbox = img.getchannel("A").getbbox()
        if bbox is None:
            raise RuntimeError(f"{filename}: empty alpha")
        print(f"ok {filename} {img.size} bbox={bbox}")


def make_contact(files: list[tuple[str, Path]]) -> None:
    sheet = Image.new("RGBA", (1040, 620), (34, 30, 24, 255))
    draw = ImageDraw.Draw(sheet)
    font = ImageFont.load_default()
    title = (240, 224, 190, 255)
    dim = (190, 176, 148, 255)
    draw.text((18, 14), "Pass 12 generated UI surfaces - native runtime sizes", fill=title, font=font)
    x = 18
    y = 44
    for i, (filename, path) in enumerate(files):
        img = Image.open(path).convert("RGBA")
        if i == 5:
            x = 18
            y = 300
        bg = Image.new("RGBA", img.size, (22, 22, 24, 255))
        bg.alpha_composite(img)
        sheet.alpha_composite(bg, (x, y))
        draw.text((x, y + img.height + 8), filename, fill=dim, font=font)
        preview = img.resize((max(1, img.width // 2), max(1, img.height // 2)), Image.Resampling.LANCZOS)
        bg2 = Image.new("RGBA", preview.size, (22, 22, 24, 255))
        bg2.alpha_composite(preview)
        sheet.alpha_composite(bg2, (x, y + img.height + 28))
        x += max(160, img.width + 54)
    CONTACT.parent.mkdir(parents=True, exist_ok=True)
    sheet.save(CONTACT)
    print(f"contact {CONTACT}")


def main() -> None:
    files = write_outputs()
    validate(files)
    make_contact(files)


if __name__ == "__main__":
    main()
