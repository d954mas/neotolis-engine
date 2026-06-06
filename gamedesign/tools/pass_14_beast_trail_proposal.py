from pathlib import Path

from PIL import Image, ImageDraw, ImageEnhance, ImageFilter


ROOT = Path(__file__).resolve().parents[2]
RAW = ROOT / "games" / "turkic-jam-2026" / "raw"
OUT = ROOT / "gamedesign" / "assets" / "concept" / "pass_14_beast_trail"
SOURCE = OUT / "pass_14_beast_trail_source.png"
PROPOSED = OUT / "proposed_runtime"


def crop(source: Image.Image, rect: tuple[int, int, int, int]) -> Image.Image:
    return source.crop(rect).convert("RGBA")


def remove_magenta(img: Image.Image) -> Image.Image:
    rgba = img.convert("RGBA")
    px = rgba.load()
    for y in range(rgba.height):
        for x in range(rgba.width):
            r, g, b, a = px[x, y]
            key = r > 185 and b > 150 and g < 155 and r > g + 40 and b > g + 28
            fringe = r > 125 and b > 95 and g < 125 and r > g + 28 and b > g + 18
            if key or fringe:
                px[x, y] = (r, g, b, 0)
            elif b > g + 18 and r > g + 24:
                px[x, y] = (r, g, max(28, int(b * 0.45)), a)
    return rgba


def crop_alpha(img: Image.Image, padding: int = 0) -> Image.Image:
    bbox = img.getchannel("A").getbbox()
    if bbox is None:
        return img
    return img.crop(
        (
            max(0, bbox[0] - padding),
            max(0, bbox[1] - padding),
            min(img.width, bbox[2] + padding),
            min(img.height, bbox[3] + padding),
        )
    )


def fit_center(img: Image.Image, size: tuple[int, int], max_size: tuple[int, int], alpha=1.0) -> Image.Image:
    out = Image.new("RGBA", size, (0, 0, 0, 0))
    comp = crop_alpha(img, 0)
    comp.thumbnail(max_size, Image.Resampling.LANCZOS)
    comp = ImageEnhance.Contrast(comp).enhance(1.16)
    comp = ImageEnhance.Color(comp).enhance(0.92)
    comp = comp.filter(ImageFilter.UnsharpMask(radius=0.45, percent=45, threshold=3))
    if alpha != 1.0:
        comp.putalpha(comp.getchannel("A").point(lambda a: int(a * alpha)))
    out.alpha_composite(comp, ((size[0] - comp.width) // 2, (size[1] - comp.height) // 2))
    return out


def make_tile(source: Image.Image) -> Image.Image:
    # Diagonal lower-right variant gives fuller trail mass without becoming a baked square tile.
    img = remove_magenta(crop(source, (760, 492, 1395, 878)))
    return fit_center(img, (128, 128), (122, 88), 0.98)


def make_card_art(source: Image.Image) -> Image.Image:
    # Lower-left compact crop fills the 64px card art with a paw mark plus scratch lines.
    img = remove_magenta(crop(source, (210, 575, 665, 835)))
    return fit_center(img, (64, 64), (64, 58), 1.0)


def checker(size: tuple[int, int]) -> Image.Image:
    img = Image.new("RGBA", size, (35, 36, 33, 255))
    d = ImageDraw.Draw(img)
    for y in range(0, size[1], 8):
        for x in range(0, size[0], 8):
            if (x // 8 + y // 8) % 2:
                d.rectangle((x, y, x + 7, y + 7), fill=(45, 45, 41, 255))
    return img


def preview_alpha(path: Path, size: tuple[int, int]) -> Image.Image:
    img = Image.open(path).convert("RGBA")
    bg = checker(size)
    tmp = img.copy()
    tmp.thumbnail((size[0] - 8, size[1] - 8), Image.Resampling.LANCZOS)
    bg.alpha_composite(tmp, ((size[0] - tmp.width) // 2, (size[1] - tmp.height) // 2))
    return bg


def map_preview(tile_path: Path) -> Image.Image:
    ground_path = RAW / "ground" / "ground_sand_base_01.png"
    decor_path = RAW / "decor" / "decor_dry_grass_01.png"
    bg = Image.open(ground_path).convert("RGBA").resize((192, 192), Image.Resampling.LANCZOS)
    decor = Image.open(decor_path).convert("RGBA")
    bg.alpha_composite(decor.resize((96, 96), Image.Resampling.LANCZOS), (12, 82))
    tile = Image.open(tile_path).convert("RGBA")
    bg.alpha_composite(tile, (32, 32))
    return bg


def card_preview(card_art_path: Path) -> Image.Image:
    surface = Image.open(RAW / "ui" / "ui_card_playable_96x128.png").convert("RGBA")
    art = Image.open(card_art_path).convert("RGBA")
    out = surface.copy()
    out.alpha_composite(art, ((96 - art.width) // 2, 22))
    return out


def contact_sheet() -> None:
    tile_current = RAW / "tiles" / "tile_wolf_track_01.png"
    card_current = RAW / "cards" / "card_art_wolf_track_64.png"
    tile_new = PROPOSED / "tiles" / "tile_wolf_track_01.png"
    card_new = PROPOSED / "cards" / "card_art_wolf_track_64.png"

    sheet = Image.new("RGBA", (980, 520), (238, 229, 207, 255))
    d = ImageDraw.Draw(sheet)
    d.text((20, 16), "Pass 14 Beast Trail targeted proposal: current vs generated bitmap candidate", fill=(30, 27, 23))
    d.text((28, 58), "current tile 128", fill=(30, 27, 23))
    d.text((210, 58), "proposed tile 128", fill=(30, 27, 23))
    d.text((404, 58), "current card art 64", fill=(30, 27, 23))
    d.text((594, 58), "proposed card art 64", fill=(30, 27, 23))
    d.text((28, 260), "map-context preview over sand/decor", fill=(30, 27, 23))
    d.text((404, 260), "card preview on existing Pass 12 surface", fill=(30, 27, 23))

    sheet.alpha_composite(preview_alpha(tile_current, (148, 148)), (24, 82))
    sheet.alpha_composite(preview_alpha(tile_new, (148, 148)), (206, 82))
    sheet.alpha_composite(preview_alpha(card_current, (112, 112)), (418, 96))
    sheet.alpha_composite(preview_alpha(card_new, (112, 112)), (606, 96))
    sheet.alpha_composite(map_preview(tile_new), (34, 292))
    sheet.alpha_composite(card_preview(card_new).resize((144, 192), Image.Resampling.NEAREST), (430, 294))

    notes = [
        "source: generated_bitmap_source",
        "slices: generated_bitmap_slice",
        "technical_mask_only: chroma cleanup + fitting",
        "no wolf body / totem / rune / real tamga",
        "raw overwrite: no",
    ]
    for i, note in enumerate(notes):
        d.text((642, 308 + i * 28), note, fill=(85, 76, 62))
    sheet.save(OUT / "pass_14_beast_trail_contact.png")


def write_reuse_map() -> None:
    lines = [
        "# Pass 14 Beast Trail Reuse Map",
        "",
        "Status: targeted proposal only. No raw runtime overwrite.",
        "",
        "## Production Inventory",
        "",
        "- visible production-facing asset: yes, Beast Trail tile/card candidate",
        "- existing UI family reused: `ui_card_playable_96x128.png` for preview only",
        "- new generated source sheet needed: yes, current wolf_track source fails map/card readability",
        "- source sheet: `pass_14_beast_trail_source.png` labeled `generated_bitmap_source`",
        "- runtime files proposed: `tile_wolf_track_01.png`, `card_art_wolf_track_64.png`",
        "- runtime text/state overlays: none",
        "- technical masks/guides: chroma cleanup and fit/crop only, labeled `technical_mask_only`",
        "",
        "## Proposed Runtime Slices",
        "",
        "### `games/turkic-jam-2026/raw/tiles/tile_wolf_track_01.png`",
        "",
        "- proposed export: `proposed_runtime/tiles/tile_wolf_track_01.png`",
        "- label: `generated_bitmap_slice`",
        "- size/mode: `128x128 RGBA`",
        "- source crop: diagonal lower-right Beast Trail variant from generated source",
        "- intent: low flat disturbed sand trail, 2-3 paw marks, scratch/drag marks, muted danger accent",
        "- raw overwrite: no",
        "",
        "### `games/turkic-jam-2026/raw/cards/card_art_wolf_track_64.png`",
        "",
        "- proposed export: `proposed_runtime/cards/card_art_wolf_track_64.png`",
        "- label: `generated_bitmap_slice`",
        "- size/mode: `64x64 RGBA`",
        "- source crop: compact lower-left paw/scratch Beast Trail crop from generated source",
        "- intent: paw and scratch marks remain readable at card scale",
        "- raw overwrite: no",
        "",
        "## Screenshot Failure Fixed",
        "",
        "- Dense QA showed current `wolf_track` reads like generic sand/decor.",
        "- Proposal increases risk identity through darker disturbed path, paw marks and scratch marks.",
        "- No broad UI/art pass is started.",
        "",
        "## Avoided Motifs",
        "",
        "- no wolf body, monster, totem, sacred wolf symbol, magic rune, glowing spell mark, real tamga/clan sign, Arabic fantasy",
    ]
    (OUT / "pass_14_beast_trail_reuse_map.md").write_text("\n".join(lines), encoding="utf-8")


def validate() -> None:
    checks = [
        (PROPOSED / "tiles" / "tile_wolf_track_01.png", (128, 128)),
        (PROPOSED / "cards" / "card_art_wolf_track_64.png", (64, 64)),
    ]
    for path, size in checks:
        with Image.open(path) as img:
            if img.size != size:
                raise SystemExit(f"{path}: expected {size}, got {img.size}")
            if img.mode != "RGBA":
                raise SystemExit(f"{path}: expected RGBA, got {img.mode}")
            if img.getchannel("A").getbbox() is None:
                raise SystemExit(f"{path}: empty alpha")


def main() -> None:
    (PROPOSED / "tiles").mkdir(parents=True, exist_ok=True)
    (PROPOSED / "cards").mkdir(parents=True, exist_ok=True)
    source = Image.open(SOURCE).convert("RGBA")
    make_tile(source).save(PROPOSED / "tiles" / "tile_wolf_track_01.png")
    make_card_art(source).save(PROPOSED / "cards" / "card_art_wolf_track_64.png")
    validate()
    contact_sheet()
    write_reuse_map()
    print((OUT / "pass_14_beast_trail_contact.png").relative_to(ROOT).as_posix())
    print((OUT / "pass_14_beast_trail_reuse_map.md").relative_to(ROOT).as_posix())
    print((PROPOSED / "tiles" / "tile_wolf_track_01.png").relative_to(ROOT).as_posix())
    print((PROPOSED / "cards" / "card_art_wolf_track_64.png").relative_to(ROOT).as_posix())


if __name__ == "__main__":
    main()
