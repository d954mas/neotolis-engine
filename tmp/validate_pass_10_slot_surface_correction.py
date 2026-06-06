from pathlib import Path

from PIL import Image


ROOT = Path(__file__).resolve().parents[1]
RAW = ROOT / "games" / "turkic-jam-2026" / "raw" / "equipment"
FILES = [
    "equip_slot_weapon_01.png",
    "equip_slot_clothes_01.png",
    "equip_slot_tamga_01.png",
    "equip_slot_tool_01.png",
]


def main() -> None:
    first_bytes = None
    for name in FILES:
        path = RAW / name
        data = path.read_bytes()
        if first_bytes is None:
            first_bytes = data
        elif data != first_bytes:
            raise SystemExit(f"{name}: slot file is not identical to base slot")
        with Image.open(path) as img:
            if img.size != (64, 64):
                raise SystemExit(f"{name}: expected 64x64, got {img.size}")
            if img.mode != "RGBA":
                raise SystemExit(f"{name}: expected RGBA, got {img.mode}")
            bbox = img.getchannel("A").getbbox()
            if bbox is None:
                raise SystemExit(f"{name}: alpha is empty")
            print(f"{name}: {img.size[0]}x{img.size[1]} {img.mode} bbox={bbox}")
    print("OK pass 10 slot surface correction: four filenames are identical base-slot aliases")


if __name__ == "__main__":
    main()
