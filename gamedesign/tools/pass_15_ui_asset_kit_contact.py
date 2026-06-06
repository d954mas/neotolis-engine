from pathlib import Path

from PIL import Image, ImageDraw, ImageFont


ROOT = Path(__file__).resolve().parents[2]
RAW = ROOT / "games" / "turkic-jam-2026" / "raw"
UI = RAW / "ui"
OUT = ROOT / "gamedesign" / "assets" / "concept" / "pass_15_ui_asset_kit"


def load(path: Path) -> Image.Image:
    return Image.open(path).convert("RGBA")


def paste_fit(dst: Image.Image, src: Image.Image, box: tuple[int, int, int, int]) -> None:
    x0, y0, x1, y1 = box
    tmp = src.copy()
    tmp.resize((x1 - x0, y1 - y0), Image.Resampling.LANCZOS)
    dst.alpha_composite(tmp, (x0, y0))


def nine_patch(src: Image.Image, size: tuple[int, int], border: int) -> Image.Image:
    w, h = src.size
    out_w, out_h = size
    out = Image.new("RGBA", size, (0, 0, 0, 0))
    regions = [
        ((0, 0, border, border), (0, 0, border, border)),
        ((border, 0, w - border, border), (border, 0, out_w - border, border)),
        ((w - border, 0, w, border), (out_w - border, 0, out_w, border)),
        ((0, border, border, h - border), (0, border, border, out_h - border)),
        ((border, border, w - border, h - border), (border, border, out_w - border, out_h - border)),
        ((w - border, border, w, h - border), (out_w - border, border, out_w, out_h - border)),
        ((0, h - border, border, h), (0, out_h - border, border, out_h)),
        ((border, h - border, w - border, h), (border, out_h - border, out_w - border, out_h)),
        ((w - border, h - border, w, h), (out_w - border, out_h - border, out_w, out_h)),
    ]
    for src_box, dst_box in regions:
        piece = src.crop(src_box)
        piece = piece.resize((dst_box[2] - dst_box[0], dst_box[3] - dst_box[1]), Image.Resampling.LANCZOS)
        out.alpha_composite(piece, (dst_box[0], dst_box[1]))
    return out


def text(draw: ImageDraw.ImageDraw, xy: tuple[int, int], value: str, fill=(236, 224, 193, 255)) -> None:
    draw.text(xy, value, fill=fill)


def chip(base: Image.Image, icon: Image.Image, label: str, value: str) -> Image.Image:
    out = nine_patch(base, (128, 44), 16)
    d = ImageDraw.Draw(out)
    icon = icon.resize((24, 24), Image.Resampling.LANCZOS)
    out.alpha_composite(icon, (12, 10))
    text(d, (42, 7), label, (226, 213, 181, 255))
    text(d, (88, 24), value, (255, 219, 112, 255))
    return out


def card(surface: Image.Image, art: Image.Image, title: str, count: str | None = None) -> Image.Image:
    out = surface.copy()
    d = ImageDraw.Draw(out)
    art = art.resize((58, 58), Image.Resampling.LANCZOS)
    out.alpha_composite(art, (19, 27))
    text(d, (12, 8), title, (57, 43, 31, 255))
    if count:
        d.ellipse((66, 7, 88, 29), fill=(82, 45, 34, 230))
        text(d, (72, 10), count, (255, 225, 165, 255))
    return out


def main() -> None:
    OUT.mkdir(parents=True, exist_ok=True)
    sheet = Image.new("RGBA", (1600, 980), (18, 20, 28, 255))
    d = ImageDraw.Draw(sheet)

    panel_dark = load(UI / "ui_panel_felt_dark_96.png")
    panel_light = load(UI / "ui_panel_felt_light_96.png")
    chip_base = load(UI / "ui_chip_resource_64.png")
    card_playable = load(UI / "ui_card_playable_96x128.png")
    card_selected = load(UI / "ui_card_selected_96x128.png")
    card_back = load(UI / "ui_card_back_96x128.png")
    slot_base = load(UI / "ui_slot_equipment_64.png")
    tooltip_base = load(UI / "ui_tooltip_dark_64.png")

    # Gameplay context layout uses existing runtime UI surfaces; labels/arrows are contact-sheet notes.
    sheet.alpha_composite(nine_patch(panel_dark, (1500, 900), 24), (50, 40))
    text(d, (72, 58), "Pass 15 UI kit contact: existing Pass 12 runtime UI families reused in gameplay context", (245, 223, 167, 255))

    # Top HUD chips.
    chip_items = [
        ("icon_supplies_32.png", "Supplies", "10"),
        ("icon_wisdom_32.png", "Wisdom", "6"),
        ("icon_glory_32.png", "Glory", "5"),
        ("icon_day_32.png", "Day", "70"),
        ("icon_speed_32.png", "Speed", "x1"),
    ]
    x = 85
    for icon_name, label, value in chip_items:
        c = chip(chip_base, load(RAW / "icons" / icon_name), label, value)
        sheet.alpha_composite(c, (x, 92))
        x += 148
    text(d, (88, 142), "ui_chip_resource_64 -> all top HUD resources", (152, 219, 198, 255))

    # Left log panel.
    log_panel = nine_patch(panel_dark, (300, 510), 24)
    sheet.alpha_composite(log_panel, (82, 176))
    text(d, (108, 198), "Chronicle / Log", (247, 204, 108, 255))
    rows = [
        ("gain", "Saxaul feeds the clan [+3]"),
        ("risk", "Beast Trail stirs near road"),
        ("memory", "Tamga stone remembers a name"),
        ("info", "Empty cell"),
        ("info", "Empty cell"),
    ]
    colors = {
        "gain": (155, 218, 145, 255),
        "risk": (207, 112, 82, 255),
        "memory": (83, 195, 181, 255),
        "info": (170, 170, 178, 255),
    }
    y = 232
    for kind, row in rows:
        d.polygon([(108, y + 6), (116, y), (124, y + 6), (116, y + 12)], fill=colors[kind])
        text(d, (136, y - 2), row, colors[kind])
        y += 34
    text(d, (102, 646), "ui_panel_felt_dark_96 -> log", (152, 219, 198, 255))

    # Map context placeholder.
    map_box = (420, 178, 1030, 652)
    d.rectangle(map_box, fill=(172, 119, 61, 255), outline=(102, 75, 49, 255), width=2)
    for gx in range(map_box[0] + 50, map_box[2], 72):
        d.line((gx, map_box[1], gx, map_box[3]), fill=(117, 84, 52, 85), width=1)
    for gy in range(map_box[1] + 50, map_box[3], 72):
        d.line((map_box[0], gy, map_box[2], gy), fill=(117, 84, 52, 85), width=1)
    d.rounded_rectangle((625, 312, 825, 510), radius=18, outline=(117, 85, 53, 180), width=26)
    d.rectangle((835, 382, 906, 453), outline=(154, 224, 113, 210), width=5)
    d.rectangle((840, 387, 901, 448), fill=(154, 224, 113, 42))
    text(d, (650, 402), "map", (62, 42, 25, 255))
    text(d, (838, 462), "valid-cell overlay: runtime-only state", (191, 232, 157, 255))

    # Right hero/equipment panel.
    right_panel = nine_patch(panel_dark, (360, 612), 24)
    sheet.alpha_composite(right_panel, (1068, 176))
    text(d, (1110, 198), "Wayfarer / Hero", (247, 204, 108, 255))
    hero = load(RAW / "hero" / "hero_wayfarer_panel.png")
    hero.thumbnail((150, 206), Image.Resampling.LANCZOS)
    sheet.alpha_composite(hero, (1174, 222))
    slot_positions = [(1100, 450), (1180, 450), (1260, 450), (1340, 450)]
    item_names = ["equip_weapon_staff_01.png", "equip_clothes_cloak_01.png", "equip_tool_satchel_01.png", "equip_tamga_charm_01.png"]
    for (sx, sy), item in zip(slot_positions, item_names):
        sheet.alpha_composite(slot_base, (sx, sy))
        sprite = load(RAW / "equipment" / item)
        sprite.thumbnail((46, 46), Image.Resampling.LANCZOS)
        sheet.alpha_composite(sprite, (sx + (64 - sprite.width) // 2, sy + (64 - sprite.height) // 2))
    stat_y = 546
    for icon, label in [("icon_body_32.png", "Body 1"), ("icon_mind_32.png", "Mind 3"), ("icon_spirit_32.png", "Spirit 1")]:
        sheet.alpha_composite(load(RAW / "icons" / icon).resize((24, 24), Image.Resampling.LANCZOS), (1110, stat_y))
        text(d, (1144, stat_y + 3), label)
        stat_y += 34
    text(d, (1098, 746), "ui_panel_felt_dark_96 + ui_slot_equipment_64", (152, 219, 198, 255))

    # Bottom card hand.
    hand_panel = nine_patch(panel_dark, (1085, 188), 24)
    sheet.alpha_composite(hand_panel, (300, 732))
    card_specs = [
        (card_playable, "card_art_saxaul_64.png", "Saxaul", "3"),
        (card_selected, "card_art_wolf_track_64.png", "Trail", "1"),
        (card_playable, "card_art_yurt_64.png", "Yurt", "1"),
    ]
    x = 342
    for surface, art_name, title, count in card_specs:
        c = card(surface, load(RAW / "cards" / art_name), title, count)
        sheet.alpha_composite(c, (x, 765 if surface is not card_selected else 748))
        x += 122
    sheet.alpha_composite(card_back, (x, 765))
    text(d, (x + 26, 816), "empty", (196, 190, 184, 255))
    text(d, (342, 904), "card_face -> playable cards | card_selected -> selected | card_back -> empty/back", (152, 219, 198, 255))

    # Tooltip / selected hint.
    tooltip = nine_patch(tooltip_base, (330, 66), 16)
    sheet.alpha_composite(tooltip, (620, 682))
    text(d, (644, 704), "Selected card hint: choose a valid cell", (236, 224, 193, 255))
    text(d, (620, 752), "ui_tooltip_dark_64 -> hover/selected hint", (152, 219, 198, 255))

    # Reuse legend.
    legend = nine_patch(panel_light, (500, 230), 24)
    sheet.alpha_composite(legend, (1010, 700))
    legend_lines = [
        "Reuse legend",
        "ui_panel_felt_dark_96 -> log + right panel + large tooltip",
        "ui_chip_resource_64 -> all HUD resources",
        "ui_card_playable_96x128 -> all playable cards",
        "ui_card_selected_96x128 -> selected card",
        "ui_card_back_96x128 -> empty/back card",
        "ui_slot_equipment_64 -> all equipment slots",
        "ui_tooltip_dark_64 -> hover/selected hint",
    ]
    ly = 722
    for i, line in enumerate(legend_lines):
        text(d, (1034, ly), line, (57, 43, 31, 255) if i == 0 else (78, 62, 43, 255))
        ly += 24

    sheet.save(OUT / "pass_15_ui_asset_kit_contact.png")

    notes = """# Pass 15 UI Asset Kit Contact Notes

Status: visual proof/contact only. No new UI generation. No raw overwrite.

## Source Policy

- Visible UI surfaces in the contact sheet come from existing runtime PNGs in `games/turkic-jam-2026/raw/ui/`.
- The contact-sheet map rectangle, labels, arrows/legend and grid lines are `technical_contact_sheet_only`.
- This file is not a new runtime UI mockup and does not authorize new UI ids.

## Reuse Shown

- `ui_panel_felt_dark_96.png` -> left log, right hero/equipment panel, dark hand panel context.
- `ui_chip_resource_64.png` -> all top HUD resource chips.
- `ui_card_playable_96x128.png` -> playable hand cards.
- `ui_card_selected_96x128.png` -> selected hand card.
- `ui_card_back_96x128.png` -> empty/back card.
- `ui_slot_equipment_64.png` -> all equipment slots.
- `ui_tooltip_dark_64.png` -> selected-card hint / hover tooltip.

## Runtime-Only States

- Text labels, numbers, selected-card lift, card dimming, hover/selected hint position, valid-cell overlay and log event colors are runtime states.
- The valid-cell overlay in the contact sheet is a placeholder proof of state ownership, not a generated UI asset.

## UI Generation Gate

Do not generate a new button, card, panel, slot, chip or tooltip from this contact sheet. If GDD finds a gap, the next art task must authorize one reusable generated bitmap family source.
"""
    (OUT / "pass_15_ui_asset_kit_contact_notes.md").write_text(notes, encoding="utf-8")
    print((OUT / "pass_15_ui_asset_kit_contact.png").relative_to(ROOT).as_posix())
    print((OUT / "pass_15_ui_asset_kit_contact_notes.md").relative_to(ROOT).as_posix())


if __name__ == "__main__":
    main()
