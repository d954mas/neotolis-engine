from pathlib import Path

from PIL import Image, ImageDraw, ImageFont


ROOT = Path(__file__).resolve().parents[2]
CONCEPT = ROOT / "gamedesign/assets/concept/pass_10_generated_wayfarer_equipment"
RAW = ROOT / "games/turkic-jam-2026/raw"

WAYFARER_SOURCE = CONCEPT / "pass_10_wayfarer_chroma_source.png"
EQUIPMENT_SOURCE = CONCEPT / "pass_10_equipment_chroma_source.png"
CONTACT = CONCEPT / "pass_10_generated_wayfarer_equipment_contact_sheet.png"

HERO_CROPS = [
    ("hero/hero_wayfarer_idle_s.png", "idle_s", (20, 30, 280, 360), (128, 128), 112, "bottom"),
    ("hero/hero_wayfarer_walk_s.png", "walk_s", (310, 30, 585, 360), (128, 128), 112, "bottom"),
    ("hero/hero_wayfarer_walk_e.png", "walk_e", (615, 35, 875, 360), (128, 128), 112, "bottom"),
    ("hero/hero_wayfarer_walk_n.png", "walk_n", (895, 25, 1115, 360), (128, 128), 112, "bottom"),
    ("hero/hero_wayfarer_walk_w.png", "walk_w", (1160, 35, 1460, 365), (128, 128), 112, "bottom"),
    ("hero/hero_wayfarer_panel.png", "panel", (900, 340, 1440, 1020), (160, 220), 212, "bottom"),
]

EQUIPMENT = [
    ("equipment/equip_weapon_staff_01.png", "staff"),
    ("equipment/equip_clothes_cloak_01.png", "cloak"),
    ("equipment/equip_tamga_charm_01.png", "tamga"),
    ("equipment/equip_tool_satchel_01.png", "satchel"),
    ("equipment/equip_slot_weapon_01.png", "slot_weapon"),
    ("equipment/equip_slot_clothes_01.png", "slot_clothes"),
    ("equipment/equip_slot_tamga_01.png", "slot_tamga"),
    ("equipment/equip_slot_tool_01.png", "slot_tool"),
]


def remove_green_key(img: Image.Image) -> Image.Image:
    out = img.convert("RGBA")
    px = out.load()
    for y in range(out.height):
        for x in range(out.width):
            r, g, b, a = px[x, y]
            key = g > 125 and g > r * 1.35 and g > b * 1.35
            near_key = g > 105 and g > r * 1.18 and g > b * 1.18
            if key:
                px[x, y] = (r, g, b, 0)
            elif near_key:
                alpha = max(0, min(a, int((max(r, b) / max(g, 1)) * 140)))
                px[x, y] = (r, min(g, max(r, b)), b, alpha)
    return out


def alpha_bbox(img: Image.Image) -> tuple[int, int, int, int]:
    bbox = img.getchannel("A").getbbox()
    if bbox is None:
        raise RuntimeError("empty sprite after key removal")
    return bbox


def fit_sprite(img: Image.Image, target_size: tuple[int, int], max_main_axis: int, anchor: str) -> Image.Image:
    crop = img.crop(alpha_bbox(img))
    tw, th = target_size
    scale = min(max_main_axis / max(crop.width, crop.height), (tw - 8) / crop.width, (th - 8) / crop.height)
    new_size = (max(1, int(crop.width * scale)), max(1, int(crop.height * scale)))
    small = crop.resize(new_size, Image.Resampling.LANCZOS)
    out = Image.new("RGBA", target_size, (0, 0, 0, 0))
    x = (tw - new_size[0]) // 2
    y = th - new_size[1] - 4 if anchor == "bottom" else (th - new_size[1]) // 2
    out.alpha_composite(small, (x, y))
    return out


def keep_largest_alpha_component(img: Image.Image) -> Image.Image:
    alpha = img.getchannel("A")
    pixels = alpha.load()
    width, height = alpha.size
    seen: set[tuple[int, int]] = set()
    components: list[list[tuple[int, int]]] = []
    for yy in range(height):
        for xx in range(width):
            if (xx, yy) in seen or pixels[xx, yy] == 0:
                continue
            stack = [(xx, yy)]
            seen.add((xx, yy))
            comp: list[tuple[int, int]] = []
            while stack:
                x, y = stack.pop()
                comp.append((x, y))
                for nx, ny in ((x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)):
                    if nx < 0 or ny < 0 or nx >= width or ny >= height:
                        continue
                    if (nx, ny) in seen or pixels[nx, ny] == 0:
                        continue
                    seen.add((nx, ny))
                    stack.append((nx, ny))
            components.append(comp)
    if not components:
        return img
    keep = set(max(components, key=len))
    out = img.copy()
    out_px = out.load()
    for yy in range(height):
        for xx in range(width):
            if pixels[xx, yy] > 0 and (xx, yy) not in keep:
                r, g, b, _ = out_px[xx, yy]
                out_px[xx, yy] = (r, g, b, 0)
    return out


def cut_hero() -> list[tuple[str, str, Path]]:
    source = remove_green_key(Image.open(WAYFARER_SOURCE))
    results = []
    for rel, label, box, size, max_axis, anchor in HERO_CROPS:
        sprite = keep_largest_alpha_component(fit_sprite(source.crop(box), size, max_axis, anchor))
        path = RAW / rel
        path.parent.mkdir(parents=True, exist_ok=True)
        sprite.save(path)
        results.append((label, rel, path))
        print(f"wrote {path}")
    return results


def cut_equipment() -> list[tuple[str, str, Path]]:
    source = remove_green_key(Image.open(EQUIPMENT_SOURCE))
    cell_w = source.width // 4
    cell_h = source.height // 2
    results = []
    for i, (rel, label) in enumerate(EQUIPMENT):
        col = i % 4
        row = i // 4
        crop = source.crop((col * cell_w, row * cell_h, (col + 1) * cell_w, (row + 1) * cell_h))
        sprite = fit_sprite(crop, (64, 64), 58, "center")
        path = RAW / rel
        path.parent.mkdir(parents=True, exist_ok=True)
        sprite.save(path)
        results.append((label, rel, path))
        print(f"wrote {path}")
    return results


def validate(paths: list[tuple[str, str, Path]]) -> None:
    for _, rel, path in paths:
        img = Image.open(path)
        if img.mode != "RGBA":
            raise RuntimeError(f"{rel}: expected RGBA, got {img.mode}")
        if img.getchannel("A").getbbox() is None:
            raise RuntimeError(f"{rel}: empty alpha")


def paste_checker(dst: Image.Image, sprite: Image.Image, xy: tuple[int, int], bg: tuple[int, int, int, int]) -> None:
    tile = Image.new("RGBA", sprite.size, bg)
    tile.alpha_composite(sprite)
    dst.alpha_composite(tile, xy)


def make_contact(hero: list[tuple[str, str, Path]], equipment: list[tuple[str, str, Path]]) -> None:
    sheet = Image.new("RGBA", (1180, 760), (34, 30, 24, 255))
    draw = ImageDraw.Draw(sheet)
    font = ImageFont.load_default()
    title = (240, 224, 190, 255)
    dim = (190, 176, 148, 255)
    sand = (196, 142, 66, 255)
    panel = (52, 45, 36, 255)

    draw.text((18, 14), "Pass 10 generated wayfarer map sprites - native 128x128", fill=title, font=font)
    x = 18
    for label, _, path in hero[:5]:
        img = Image.open(path).convert("RGBA")
        paste_checker(sheet, img, (x, 42), sand)
        draw.text((x, 174), label, fill=dim, font=font)
        small = img.resize((48, 58), Image.Resampling.LANCZOS)
        paste_checker(sheet, small, (x + 36, 198), panel)
        x += 152

    draw.text((18, 292), "Hero panel - native 160x220 and runtime 96x132", fill=title, font=font)
    panel_img = Image.open(RAW / "hero/hero_wayfarer_panel.png").convert("RGBA")
    paste_checker(sheet, panel_img, (18, 324), panel)
    paste_checker(sheet, panel_img.resize((96, 132), Image.Resampling.LANCZOS), (196, 390), panel)

    draw.text((360, 292), "Equipment items and slots - native 64x64 / runtime preview 34x34", fill=title, font=font)
    x0 = 360
    y0 = 324
    for i, (label, _, path) in enumerate(equipment):
        col = i % 4
        row = i // 4
        x = x0 + col * 152
        y = y0 + row * 150
        img = Image.open(path).convert("RGBA")
        paste_checker(sheet, img, (x, y), panel)
        paste_checker(sheet, img.resize((34, 34), Image.Resampling.LANCZOS), (x + 82, y + 15), panel)
        draw.text((x, y + 72), label, fill=dim, font=font)

    CONTACT.parent.mkdir(parents=True, exist_ok=True)
    sheet.save(CONTACT)
    print(f"contact {CONTACT}")


def main() -> None:
    hero = cut_hero()
    equipment = cut_equipment()
    validate(hero + equipment)
    make_contact(hero, equipment)


if __name__ == "__main__":
    main()
