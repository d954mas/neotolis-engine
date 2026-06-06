from pathlib import Path

from PIL import Image


ROOT = Path(__file__).resolve().parents[1]

EXPECTED = {
    "games/turkic-jam-2026/raw/ui/ui_card_playable_96x128.png": (96, 128),
    "games/turkic-jam-2026/raw/ui/ui_card_selected_96x128.png": (96, 128),
    "games/turkic-jam-2026/raw/ui/ui_panel_felt_dark_96.png": (96, 96),
    "games/turkic-jam-2026/raw/cards/card_art_saxaul_64.png": (64, 64),
    "games/turkic-jam-2026/raw/icons/icon_stamina_32.png": (32, 32),
    "games/turkic-jam-2026/raw/icons/icon_supplies_32.png": (32, 32),
    "games/turkic-jam-2026/raw/tiles/tile_saxaul_01.png": (128, 128),
    "games/turkic-jam-2026/raw/hero/hero_wayfarer_idle_s.png": (128, 128),
}


def main() -> None:
    for rel, size in EXPECTED.items():
        path = ROOT / rel
        with Image.open(path) as img:
            print(f"{rel} {img.width}x{img.height} {img.mode}")
            if img.size != size:
                raise SystemExit(f"bad size {rel}: {img.size} != {size}")
            if img.mode != "RGBA":
                raise SystemExit(f"bad mode {rel}: {img.mode}")
    contact = ROOT / "gamedesign/assets/concept/final_repaint_pass_1/final_repaint_pass_1_contact_sheet.png"
    with Image.open(contact) as img:
        print(f"{contact.relative_to(ROOT).as_posix()} {img.width}x{img.height} {img.mode}")
    print("OK final repaint pass 1 minimum")


if __name__ == "__main__":
    main()
