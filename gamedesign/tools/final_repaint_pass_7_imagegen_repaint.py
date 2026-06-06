from pathlib import Path

from PIL import Image, ImageDraw


ROOT = Path(__file__).resolve().parents[2]
RAW = ROOT / "games" / "turkic-jam-2026" / "raw"
OUT = ROOT / "gamedesign" / "assets" / "concept" / "final_repaint_pass_7_hero_archetype_panels"
SOURCE = Path(
    r"C:\Users\ROG\.codex\generated_images\019e9928-1183-71b0-8014-3e8cce9a7afb\ig_0fbf54c1d2c2c804016a23cfedc0348191b18f863b4aa84f72.png"
)


def remove_magenta(img: Image.Image) -> Image.Image:
    rgba = img.convert("RGBA")
    px = rgba.load()
    for y in range(rgba.height):
        for x in range(rgba.width):
            r, g, b, a = px[x, y]
            if r > 210 and b > 200 and g < 85:
                px[x, y] = (r, g, b, 0)
            elif r > 120 and b > 120 and g < 105 and abs(r - b) < 95:
                px[x, y] = (r, g, b, 0)
            elif r > 170 and b > 160 and g < 120:
                px[x, y] = (r, g, b, min(a, 90))
    return rgba


def alpha_bbox(img: Image.Image) -> tuple[int, int, int, int]:
    bbox = img.getchannel("A").getbbox()
    if bbox is None:
        raise RuntimeError("empty alpha after chroma removal")
    return bbox


def extract_panel(img: Image.Image, x0: int, x1: int) -> Image.Image:
    part = img.crop((x0, 0, x1, img.height))
    bbox = alpha_bbox(part)
    crop = part.crop(bbox)
    target = Image.new("RGBA", (128, 192), (0, 0, 0, 0))
    scale = min(116 / crop.width, 180 / crop.height)
    new_size = (max(1, int(crop.width * scale)), max(1, int(crop.height * scale)))
    small = crop.resize(new_size, Image.Resampling.LANCZOS)
    x = (128 - new_size[0]) // 2
    y = 188 - new_size[1]
    target.alpha_composite(small, (x, y))
    return keep_largest_alpha_component(target)


def keep_largest_alpha_component(img: Image.Image) -> Image.Image:
    alpha = img.getchannel("A")
    pixels = alpha.load()
    width, height = alpha.size
    seen = set()
    components: list[list[tuple[int, int]]] = []

    for yy in range(height):
        for xx in range(width):
            if (xx, yy) in seen or pixels[xx, yy] == 0:
                continue
            stack = [(xx, yy)]
            seen.add((xx, yy))
            comp = []
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


def icon_aul_upgrade() -> Image.Image:
    img = Image.new("RGBA", (128, 128), (0, 0, 0, 0))
    d = ImageDraw.Draw(img, "RGBA")
    ink = (18, 15, 12, 255)
    felt = (236, 217, 171, 255)
    teal = (44, 169, 157, 255)
    gold = (245, 188, 72, 255)
    shadow = (0, 0, 0, 80)
    d.ellipse((24, 84, 104, 116), fill=shadow)
    d.polygon([(28, 78), (64, 34), (100, 78)], fill=felt, outline=ink)
    d.rounded_rectangle((36, 75, 92, 108), radius=10, fill=(210, 189, 146, 255), outline=ink, width=5)
    d.rectangle((55, 88, 73, 108), fill=teal, outline=ink)
    d.polygon([(64, 6), (109, 49), (82, 49), (82, 69), (46, 69), (46, 49), (19, 49)], fill=gold, outline=ink)
    return img.resize((32, 32), Image.Resampling.LANCZOS)


def icon_settings() -> Image.Image:
    img = Image.new("RGBA", (128, 128), (0, 0, 0, 0))
    d = ImageDraw.Draw(img, "RGBA")
    ink = (18, 15, 12, 255)
    teal = (44, 169, 157, 255)
    gold = (245, 188, 72, 255)
    d.rounded_rectangle((18, 36, 110, 92), radius=28, fill=ink)
    d.rounded_rectangle((27, 45, 101, 83), radius=19, fill=(68, 96, 89, 255))
    d.line((38, 64, 90, 64), fill=gold, width=15)
    d.ellipse((50, 45, 82, 77), fill=teal, outline=ink, width=7)
    return img.resize((32, 32), Image.Resampling.LANCZOS)


def contact_sheet(hero_paths: list[Path], icon_paths: list[Path]) -> None:
    sheet = Image.new("RGBA", (880, 610), (238, 229, 207, 255))
    d = ImageDraw.Draw(sheet, "RGBA")
    d.text((20, 14), "Hero archetype panels - imagegen repaint native 128x192", fill=(30, 27, 23, 255))
    for i, path in enumerate(hero_paths):
        img = Image.open(path).convert("RGBA")
        x = 38 + i * 190
        y = 42
        bg = Image.new("RGBA", (148, 212), (27, 29, 25, 255))
        bg.alpha_composite(img, (10, 10))
        sheet.alpha_composite(bg, (x, y))
        d.text((x, y + 218), path.name, fill=(30, 27, 23, 255))

    d.text((20, 296), "Reduced UI scale preview", fill=(30, 27, 23, 255))
    for i, path in enumerate(hero_paths):
        img = Image.open(path).convert("RGBA").resize((64, 96), Image.Resampling.LANCZOS)
        x = 70 + i * 150
        y = 325
        bg = Image.new("RGBA", (88, 120), (27, 29, 25, 255))
        bg.alpha_composite(img, (12, 12))
        sheet.alpha_composite(bg, (x, y))

    d.text((560, 296), "Icon fixes - 32px and 24px", fill=(30, 27, 23, 255))
    for i, path in enumerate(icon_paths):
        img = Image.open(path).convert("RGBA")
        small = img.resize((24, 24), Image.Resampling.LANCZOS)
        x = 575 + i * 135
        y = 333
        bg = Image.new("RGBA", (98, 78), (27, 29, 25, 255))
        bg.alpha_composite(img, (11, 10))
        bg.alpha_composite(small, (56, 14))
        sheet.alpha_composite(bg, (x, y))
        d.text((x, y + 84), path.name[:22], fill=(30, 27, 23, 255))
    sheet.save(OUT / "final_repaint_pass_7_hero_archetype_panels_contact_sheet.png")


def main() -> None:
    OUT.mkdir(parents=True, exist_ok=True)
    source = Image.open(SOURCE).convert("RGBA")
    source.save(OUT / "final_repaint_pass_7_imagegen_source.png")
    cleaned = remove_magenta(source)
    cleaned.save(OUT / "final_repaint_pass_7_imagegen_source_chroma_removed.png")

    width = cleaned.width
    panels = [
        ("hero_body_panel.png", 0, width // 3),
        ("hero_mind_panel.png", width // 3, 2 * width // 3),
        ("hero_spirit_panel.png", 2 * width // 3, width),
    ]
    hero_paths: list[Path] = []
    for name, x0, x1 in panels:
        panel = extract_panel(cleaned, x0, x1)
        path = RAW / "hero" / name
        panel.save(path)
        panel.save(OUT / name)
        hero_paths.append(path)

    icon_paths: list[Path] = []
    for name, img in {
        "icon_aul_upgrade_32.png": icon_aul_upgrade(),
        "icon_settings_32.png": icon_settings(),
    }.items():
        path = RAW / "icons" / name
        img.save(path)
        img.save(OUT / name)
        icon_paths.append(path)

    contact_sheet(hero_paths, icon_paths)
    for path in hero_paths + icon_paths:
        print(path.relative_to(ROOT).as_posix())
    print((OUT / "final_repaint_pass_7_imagegen_source.png").relative_to(ROOT).as_posix())
    print((OUT / "final_repaint_pass_7_hero_archetype_panels_contact_sheet.png").relative_to(ROOT).as_posix())


if __name__ == "__main__":
    main()
