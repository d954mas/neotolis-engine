from pathlib import Path

from PIL import Image


ROOT = Path(__file__).resolve().parents[1]


EXPECTED_B = {
    "ui/ui_card_back_96x128.png": (96, 128),
    "ui/ui_button_dark_64.png": (64, 64),
    "cards/card_badge_count_32.png": (32, 32),
    "cards/card_placement_roadside_32.png": (32, 32),
    "cards/card_placement_field_32.png": (32, 32),
    "cards/card_placement_special_32.png": (32, 32),
    "cards/card_art_saxaul_64.png": (64, 64),
    "cards/card_art_yurt_64.png": (64, 64),
    "cards/card_art_tamga_stone_64.png": (64, 64),
    "cards/card_art_wolf_track_64.png": (64, 64),
    "equipment/equip_slot_weapon_01.png": (64, 64),
    "equipment/equip_slot_clothes_01.png": (64, 64),
    "equipment/equip_slot_tamga_01.png": (64, 64),
    "equipment/equip_slot_tool_01.png": (64, 64),
    "equipment/equip_weapon_staff_01.png": (64, 64),
    "equipment/equip_clothes_cloak_01.png": (64, 64),
    "equipment/equip_tamga_charm_01.png": (64, 64),
    "equipment/equip_tool_satchel_01.png": (64, 64),
    "icons/icon_stamina_32.png": (32, 32),
    "icons/icon_supplies_32.png": (32, 32),
    "icons/icon_wisdom_32.png": (32, 32),
    "icons/icon_glory_32.png": (32, 32),
    "icons/icon_circle_32.png": (32, 32),
    "icons/icon_day_32.png": (32, 32),
    "icons/icon_body_32.png": (32, 32),
    "icons/icon_mind_32.png": (32, 32),
    "icons/icon_spirit_32.png": (32, 32),
    "icons/icon_last_tamga_32.png": (32, 32),
    "icons/icon_settings_32.png": (32, 32),
    "icons/icon_speed_32.png": (32, 32),
}

for name in ["dust_step", "tile_placed", "tile_trigger"]:
    for frame in range(4):
        EXPECTED_B[f"fx/fx_{name}_{frame:02d}.png"] = (64, 64)
for frame in range(3):
    EXPECTED_B[f"fx/fx_gain_popup_{frame:02d}.png"] = (64, 64)
for frame in range(2):
    EXPECTED_B[f"fx/fx_invalid_cell_{frame:02d}.png"] = (64, 64)


def main() -> None:
    raw = ROOT / "games" / "turkic-jam-2026" / "raw"
    problems: list[str] = []
    for rel, expected_size in sorted(EXPECTED_B.items()):
        path = raw / rel
        if not path.exists():
            problems.append(f"missing {rel}")
            continue
        with Image.open(path) as img:
            if img.size != expected_size:
                problems.append(f"bad size {rel}: {img.size}, expected {expected_size}")
            if img.mode != "RGBA":
                problems.append(f"bad mode {rel}: {img.mode}, expected RGBA")

    extra_count = len(EXPECTED_B)
    if problems:
        print("FAILED")
        for problem in problems:
            print(problem)
        raise SystemExit(1)
    print(f"OK Batch B: {extra_count} files, exact sizes, RGBA")


if __name__ == "__main__":
    main()
