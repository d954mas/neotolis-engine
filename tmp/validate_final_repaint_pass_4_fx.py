from pathlib import Path

from PIL import Image


ROOT = Path(__file__).resolve().parents[1]

EXPECTED: dict[str, tuple[int, int]] = {}

for name, count in [
    ("dust_step", 4),
    ("tile_placed", 4),
    ("tile_trigger", 4),
]:
    for i in range(count):
        EXPECTED[f"games/turkic-jam-2026/raw/fx/fx_{name}_{i:02d}.png"] = (64, 64)

for i in range(3):
    EXPECTED[f"games/turkic-jam-2026/raw/fx/fx_gain_popup_{i:02d}.png"] = (64, 64)
for i in range(2):
    EXPECTED[f"games/turkic-jam-2026/raw/fx/fx_invalid_cell_{i:02d}.png"] = (64, 64)

for name, count in [
    ("intro_sand", 6),
    ("fire_glow", 4),
    ("last_tamga_spawn", 6),
    ("storm_veil", 6),
    ("card_reward", 4),
    ("near_death", 4),
]:
    for i in range(count):
        EXPECTED[f"games/turkic-jam-2026/raw/fx/fx_{name}_{i:02d}.png"] = (128, 128)


def visible_bbox(img: Image.Image):
    alpha = img.getchannel("A")
    return alpha.getbbox()


def main() -> None:
    for rel, size in sorted(EXPECTED.items()):
        path = ROOT / rel
        with Image.open(path) as img:
            print(f"{rel} {img.width}x{img.height} {img.mode} bbox={visible_bbox(img)}")
            if img.size != size:
                raise SystemExit(f"bad size {rel}: {img.size} != {size}")
            if img.mode != "RGBA":
                raise SystemExit(f"bad mode {rel}: {img.mode}")
            if visible_bbox(img) is None:
                raise SystemExit(f"fully transparent frame: {rel}")
    contact = ROOT / "gamedesign/assets/concept/final_repaint_pass_4_fx/final_repaint_pass_4_fx_contact_sheet.png"
    with Image.open(contact) as img:
        print(f"{contact.relative_to(ROOT).as_posix()} {img.width}x{img.height} {img.mode}")
    print(f"OK final repaint pass 4 FX: {len(EXPECTED)} frames")


if __name__ == "__main__":
    main()
