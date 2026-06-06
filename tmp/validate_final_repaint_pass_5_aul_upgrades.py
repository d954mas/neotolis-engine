from pathlib import Path

from PIL import Image


ROOT = Path(__file__).resolve().parents[1]

EXPECTED = {
    "games/turkic-jam-2026/raw/aul/aul_tamga_post_01.png": (128, 128),
    "games/turkic-jam-2026/raw/aul/aul_stage_01_camp.png": (256, 256),
    "games/turkic-jam-2026/raw/aul/aul_stage_02_settlement.png": (256, 256),
    "games/turkic-jam-2026/raw/aul/aul_stage_03_village.png": (256, 256),
    "games/turkic-jam-2026/raw/aul/aul_stage_04_fortified_aul.png": (256, 256),
    "games/turkic-jam-2026/raw/aul/aul_stage_05_steppe_capital.png": (256, 256),
}


def main() -> None:
    for rel, size in EXPECTED.items():
        path = ROOT / rel
        with Image.open(path) as img:
            bbox = img.getchannel("A").getbbox()
            print(f"{rel} {img.width}x{img.height} {img.mode} bbox={bbox}")
            if img.size != size:
                raise SystemExit(f"bad size {rel}: {img.size} != {size}")
            if img.mode != "RGBA":
                raise SystemExit(f"bad mode {rel}: {img.mode}")
            if bbox is None:
                raise SystemExit(f"fully transparent sprite: {rel}")
    contact = ROOT / "gamedesign/assets/concept/final_repaint_pass_5_aul_upgrades/final_repaint_pass_5_aul_upgrades_contact_sheet.png"
    with Image.open(contact) as img:
        print(f"{contact.relative_to(ROOT).as_posix()} {img.width}x{img.height} {img.mode}")
    print("OK final repaint pass 5 aul upgrades")


if __name__ == "__main__":
    main()
