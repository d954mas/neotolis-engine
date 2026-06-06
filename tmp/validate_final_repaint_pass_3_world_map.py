from pathlib import Path

from PIL import Image


ROOT = Path(__file__).resolve().parents[1]

EXPECTED = {
    "games/turkic-jam-2026/raw/ground/ground_sand_base_01.png": ((128, 128), "RGB"),
    "games/turkic-jam-2026/raw/decor/decor_dune_01.png": ((128, 128), "RGBA"),
    "games/turkic-jam-2026/raw/decor/decor_stones_01.png": ((128, 128), "RGBA"),
    "games/turkic-jam-2026/raw/decor/decor_dry_grass_01.png": ((128, 128), "RGBA"),
    "games/turkic-jam-2026/raw/decor/decor_tracks_01.png": ((128, 128), "RGBA"),
    "games/turkic-jam-2026/raw/decor/decor_bones_01.png": ((128, 128), "RGBA"),
    "games/turkic-jam-2026/raw/decor/decor_cracks_01.png": ((128, 128), "RGBA"),
    "games/turkic-jam-2026/raw/road/road_straight_ns.png": ((128, 128), "RGBA"),
    "games/turkic-jam-2026/raw/road/road_straight_ew.png": ((128, 128), "RGBA"),
    "games/turkic-jam-2026/raw/road/road_corner_ne.png": ((128, 128), "RGBA"),
    "games/turkic-jam-2026/raw/road/road_corner_es.png": ((128, 128), "RGBA"),
    "games/turkic-jam-2026/raw/road/road_corner_sw.png": ((128, 128), "RGBA"),
    "games/turkic-jam-2026/raw/road/road_corner_wn.png": ((128, 128), "RGBA"),
    "games/turkic-jam-2026/raw/road/road_entry_aul.png": ((128, 128), "RGBA"),
    "games/turkic-jam-2026/raw/road/road_current_highlight.png": ((128, 128), "RGBA"),
    "games/turkic-jam-2026/raw/road/buffer_edge_stones_01.png": ((128, 128), "RGBA"),
    "games/turkic-jam-2026/raw/road/buffer_packed_sand_01.png": ((128, 128), "RGBA"),
    "games/turkic-jam-2026/raw/road/buffer_stakes_01.png": ((128, 128), "RGBA"),
    "games/turkic-jam-2026/raw/road/buffer_cart_marks_01.png": ((128, 128), "RGBA"),
    "games/turkic-jam-2026/raw/aul/aul_ground_2x2.png": ((256, 256), "RGB"),
    "games/turkic-jam-2026/raw/aul/aul_yurt_small_01.png": ((128, 128), "RGBA"),
    "games/turkic-jam-2026/raw/aul/aul_yurt_small_02.png": ((128, 128), "RGBA"),
    "games/turkic-jam-2026/raw/aul/aul_fire_01.png": ((128, 128), "RGBA"),
    "games/turkic-jam-2026/raw/tiles/tile_yurt_01.png": ((128, 128), "RGBA"),
    "games/turkic-jam-2026/raw/tiles/tile_tamga_stone_01.png": ((128, 128), "RGBA"),
    "games/turkic-jam-2026/raw/tiles/tile_wolf_track_01.png": ((128, 128), "RGBA"),
    "games/turkic-jam-2026/raw/tiles/tile_oasis_01.png": ((128, 128), "RGBA"),
    "games/turkic-jam-2026/raw/tiles/tile_mirage_01.png": ((128, 128), "RGBA"),
    "games/turkic-jam-2026/raw/tiles/tile_storm_01.png": ((128, 128), "RGBA"),
    "games/turkic-jam-2026/raw/tiles/tile_last_tamga_01.png": ((128, 128), "RGBA"),
}


def main() -> None:
    for rel, (size, mode) in EXPECTED.items():
        path = ROOT / rel
        with Image.open(path) as img:
            print(f"{rel} {img.width}x{img.height} {img.mode}")
            if img.size != size:
                raise SystemExit(f"bad size {rel}: {img.size} != {size}")
            if img.mode != mode:
                raise SystemExit(f"bad mode {rel}: {img.mode} != {mode}")
    contact = ROOT / "gamedesign/assets/concept/final_repaint_pass_3_world_map/final_repaint_pass_3_world_map_contact_sheet.png"
    with Image.open(contact) as img:
        print(f"{contact.relative_to(ROOT).as_posix()} {img.width}x{img.height} {img.mode}")
    print("OK final repaint pass 3 world map")


if __name__ == "__main__":
    main()
