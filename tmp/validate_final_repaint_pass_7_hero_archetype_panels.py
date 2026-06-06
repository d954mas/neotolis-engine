from pathlib import Path

from PIL import Image


ROOT = Path(__file__).resolve().parents[1]
RAW = ROOT / "games" / "turkic-jam-2026" / "raw"
CONTACT = (
    ROOT
    / "gamedesign"
    / "assets"
    / "concept"
    / "final_repaint_pass_7_hero_archetype_panels"
    / "final_repaint_pass_7_hero_archetype_panels_contact_sheet.png"
)

EXPECTED = {
    "hero/hero_body_panel.png": (128, 192),
    "hero/hero_mind_panel.png": (128, 192),
    "hero/hero_spirit_panel.png": (128, 192),
    "icons/icon_aul_upgrade_32.png": (32, 32),
    "icons/icon_settings_32.png": (32, 32),
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
    print("OK final repaint pass 7 hero archetype panels")


if __name__ == "__main__":
    main()
