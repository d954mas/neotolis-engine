from pathlib import Path

from PIL import Image


ROOT = Path(__file__).resolve().parents[1]

EXPECTED = {
    "games/turkic-jam-2026/raw/hero/hero_wayfarer_walk_s.png": (128, 128),
    "games/turkic-jam-2026/raw/hero/hero_wayfarer_walk_e.png": (128, 128),
    "games/turkic-jam-2026/raw/hero/hero_wayfarer_walk_n.png": (128, 128),
    "games/turkic-jam-2026/raw/hero/hero_wayfarer_walk_w.png": (128, 128),
    "games/turkic-jam-2026/raw/hero/hero_wayfarer_panel.png": (160, 220),
    "games/turkic-jam-2026/raw/equipment/equip_slot_weapon_01.png": (64, 64),
    "games/turkic-jam-2026/raw/equipment/equip_slot_clothes_01.png": (64, 64),
    "games/turkic-jam-2026/raw/equipment/equip_slot_tool_01.png": (64, 64),
    "games/turkic-jam-2026/raw/equipment/equip_slot_tamga_01.png": (64, 64),
    "games/turkic-jam-2026/raw/equipment/equip_weapon_staff_01.png": (64, 64),
    "games/turkic-jam-2026/raw/equipment/equip_clothes_cloak_01.png": (64, 64),
    "games/turkic-jam-2026/raw/equipment/equip_tool_satchel_01.png": (64, 64),
    "games/turkic-jam-2026/raw/equipment/equip_tamga_charm_01.png": (64, 64),
    "games/turkic-jam-2026/raw/icons/icon_body_32.png": (32, 32),
    "games/turkic-jam-2026/raw/icons/icon_mind_32.png": (32, 32),
    "games/turkic-jam-2026/raw/icons/icon_spirit_32.png": (32, 32),
    "games/turkic-jam-2026/raw/icons/icon_circle_32.png": (32, 32),
    "games/turkic-jam-2026/raw/icons/icon_day_32.png": (32, 32),
    "games/turkic-jam-2026/raw/icons/icon_speed_32.png": (32, 32),
    "games/turkic-jam-2026/raw/icons/icon_last_tamga_32.png": (32, 32),
    "games/turkic-jam-2026/raw/cards/card_badge_count_32.png": (32, 32),
    "games/turkic-jam-2026/raw/cards/card_placement_roadside_32.png": (32, 32),
    "games/turkic-jam-2026/raw/cards/card_placement_field_32.png": (32, 32),
    "games/turkic-jam-2026/raw/cards/card_placement_special_32.png": (32, 32),
}

FORBIDDEN = [
    "games/turkic-jam-2026/raw/equipment/equip_slot_weapon_64.png",
    "games/turkic-jam-2026/raw/equipment/equip_slot_clothes_64.png",
    "games/turkic-jam-2026/raw/equipment/equip_slot_tool_64.png",
    "games/turkic-jam-2026/raw/equipment/equip_slot_tamga_64.png",
]


def main() -> None:
    for rel, size in EXPECTED.items():
        path = ROOT / rel
        with Image.open(path) as img:
            print(f"{rel} {img.width}x{img.height} {img.mode}")
            if img.size != size:
                raise SystemExit(f"bad size {rel}: {img.size} != {size}")
            if img.mode != "RGBA":
                raise SystemExit(f"bad mode {rel}: {img.mode}")
    for rel in FORBIDDEN:
        if (ROOT / rel).exists():
            raise SystemExit(f"forbidden file exists: {rel}")
    contact = ROOT / "gamedesign/assets/concept/final_repaint_pass_2/final_repaint_pass_2_contact_sheet.png"
    with Image.open(contact) as img:
        print(f"{contact.relative_to(ROOT).as_posix()} {img.width}x{img.height} {img.mode}")
    print("OK final repaint pass 2")


if __name__ == "__main__":
    main()
