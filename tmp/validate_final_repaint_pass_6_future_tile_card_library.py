from pathlib import Path

from PIL import Image


ROOT = Path(__file__).resolve().parents[1]
RAW = ROOT / "games" / "turkic-jam-2026" / "raw"
CONTACT = (
    ROOT
    / "gamedesign"
    / "assets"
    / "concept"
    / "final_repaint_pass_6_future_tile_card_library"
    / "final_repaint_pass_6_future_tile_card_library_contact_sheet.png"
)

EXPECTED = {
    "cards/card_art_oasis_64.png": (64, 64),
    "cards/card_art_mirage_64.png": (64, 64),
    "cards/card_art_storm_64.png": (64, 64),
    "cards/card_art_last_tamga_64.png": (64, 64),
    "cards/card_art_well_64.png": (64, 64),
    "cards/card_art_watchtower_64.png": (64, 64),
    "tiles/tile_well_01.png": (128, 128),
    "tiles/tile_watchtower_01.png": (128, 128),
    "tiles/tile_pack_01.png": (128, 128),
    "tiles/tile_small_camp_01.png": (128, 128),
    "tiles/tile_clan_camp_01.png": (128, 128),
    "tiles/tile_hunting_trail_01.png": (128, 128),
    "tiles/tile_vision_01.png": (128, 128),
    "tiles/tile_false_path_01.png": (128, 128),
    "tiles/tile_buried_spring_01.png": (128, 128),
    "icons/icon_aul_upgrade_32.png": (32, 32),
    "icons/icon_card_gain_32.png": (32, 32),
    "icons/icon_deck_32.png": (32, 32),
    "icons/icon_map_32.png": (32, 32),
    "icons/icon_memory_32.png": (32, 32),
    "icons/icon_settings_32.png": (32, 32),
    "icons/icon_warning_32.png": (32, 32),
}


def main() -> None:
    for rel, size in EXPECTED.items():
        path = RAW / rel
        with Image.open(path) as img:
            if img.size != size:
                raise SystemExit(f"{rel}: expected {size}, got {img.size}")
            if img.mode != "RGBA":
                raise SystemExit(f"{rel}: expected RGBA, got {img.mode}")
            bbox = img.getchannel("A").getbbox()
            if bbox is None:
                raise SystemExit(f"{rel}: alpha is empty")
            print(f"{rel}: {img.size[0]}x{img.size[1]} {img.mode} bbox={bbox}")

    with Image.open(CONTACT) as img:
        if img.mode != "RGBA":
            raise SystemExit(f"contact sheet: expected RGBA, got {img.mode}")
        print(f"{CONTACT.relative_to(ROOT).as_posix()}: {img.size[0]}x{img.size[1]} {img.mode}")
    print("OK final repaint pass 6 future tile/card library")


if __name__ == "__main__":
    main()
