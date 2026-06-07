from __future__ import annotations

from pathlib import Path

from PIL import Image, ImageDraw, ImageStat


ROOT = Path(__file__).resolve().parents[2]
RAW = ROOT / "games" / "turkic-jam-2026" / "raw"
SRC = ROOT / "tmp" / "art_brief_ai_sources"


MAGENTA = (255, 0, 255)


def key_to_alpha(img: Image.Image) -> Image.Image:
    img = img.convert("RGBA")
    pixels = img.load()
    w, h = img.size
    for y in range(h):
        for x in range(w):
            r, g, b, a = pixels[x, y]
            magenta_key = r > 175 and b > 155 and g < 95
            dr = abs(r - MAGENTA[0])
            dg = abs(g - MAGENTA[1])
            db = abs(b - MAGENTA[2])
            magenta_edge = r > 150 and b > 130 and g < 125 and r > g + 45 and b > g + 45
            if magenta_key or magenta_edge or (dr < 76 and dg < 76 and db < 76):
                pixels[x, y] = (r, g, b, 0)
            elif r > 150 and b > 130 and g < 120:
                pixels[x, y] = (min(r, 180), max(g, 118), min(b, 112), min(a, 150))
    return img


def keep_main_components(img: Image.Image) -> Image.Image:
    alpha = img.getchannel("A")
    w, h = img.size
    data = alpha.load()
    seen = bytearray(w * h)
    comps: list[tuple[int, int, int, int, int, list[int]]] = []
    for y in range(h):
        for x in range(w):
            idx = y * w + x
            if seen[idx] or data[x, y] == 0:
                continue
            stack = [idx]
            seen[idx] = 1
            comp: list[int] = []
            while stack:
                cur = stack.pop()
                comp.append(cur)
                cx = cur % w
                cy = cur // w
                for nx, ny in ((cx - 1, cy), (cx + 1, cy), (cx, cy - 1), (cx, cy + 1)):
                    if nx < 0 or ny < 0 or nx >= w or ny >= h:
                        continue
                    ni = ny * w + nx
                    if not seen[ni] and data[nx, ny] > 0:
                        seen[ni] = 1
                        stack.append(ni)
            xs = [idx % w for idx in comp]
            ys = [idx // w for idx in comp]
            comps.append((len(comp), min(xs), min(ys), max(xs), max(ys), comp))
    if not comps:
        return img
    comps.sort(key=lambda item: item[0], reverse=True)
    keep = set()
    main = comps[0][0]
    for area, x0, _y0, x1, _y1, comp in comps:
        cx = (x0 + x1) * 0.5 / w
        central = cx > 0.08 and cx < 0.92
        if area == main or (central and area >= main * 0.04):
            keep.update(comp)
    out = img.copy()
    px = out.load()
    for y in range(h):
        for x in range(w):
            idx = y * w + x
            if data[x, y] > 0 and idx not in keep:
                r, g, b, _ = px[x, y]
                px[x, y] = (r, g, b, 0)
    return out


def fit_sprite(cell: Image.Image, size: int, fill: float) -> Image.Image:
    keyed = keep_main_components(key_to_alpha(cell))
    bbox = keyed.getchannel("A").getbbox()
    if bbox is None:
        raise RuntimeError("empty keyed sprite")
    cropped = keyed.crop(bbox)
    scale = min((size * fill) / cropped.width, (size * fill) / cropped.height)
    nw = max(1, int(round(cropped.width * scale)))
    nh = max(1, int(round(cropped.height * scale)))
    resized = cropped.resize((nw, nh), Image.Resampling.LANCZOS)
    out = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    out.alpha_composite(resized, ((size - nw) // 2, (size - nh) // 2))
    return out


def fit_sprite_rect(cell: Image.Image, width: int, height: int, fill: float) -> Image.Image:
    keyed = keep_main_components(key_to_alpha(cell))
    bbox = keyed.getchannel("A").getbbox()
    if bbox is None:
        raise RuntimeError("empty keyed sprite")
    cropped = keyed.crop(bbox)
    scale = min((width * fill) / cropped.width, (height * fill) / cropped.height)
    nw = max(1, int(round(cropped.width * scale)))
    nh = max(1, int(round(cropped.height * scale)))
    resized = cropped.resize((nw, nh), Image.Resampling.LANCZOS)
    out = Image.new("RGBA", (width, height), (0, 0, 0, 0))
    out.alpha_composite(resized, ((width - nw) // 2, (height - nh) // 2))
    return out


def slice_row(sheet_name: str, out_names: list[str], target: int, fill: float) -> list[tuple[str, Image.Image]]:
    sheet = Image.open(SRC / sheet_name).convert("RGBA")
    count = len(out_names)
    cell_w = sheet.width // count
    inset = max(4, sheet.width // 320)
    items = []
    for i, out_name in enumerate(out_names):
        left = (i * cell_w) + inset
        right = ((i + 1) * cell_w) - inset
        cell = sheet.crop((left, 0, right, sheet.height))
        sprite = fit_sprite(cell, target, fill)
        folder = "enemies" if out_name.startswith("boss_") else "tiles"
        path = RAW / folder / f"{out_name}.png"
        path.parent.mkdir(parents=True, exist_ok=True)
        sprite.save(path)
        items.append((out_name, sprite))
    return items


def slice_row_to(sheet_name: str, cell_count: int, specs: list[tuple[int, str, str, int, float]]) -> list[tuple[str, Image.Image]]:
    sheet = Image.open(SRC / sheet_name).convert("RGBA")
    cell_w = sheet.width // cell_count
    inset = max(4, sheet.width // 320)
    items = []
    for cell_idx, out_name, folder, target, fill in specs:
        left = (cell_idx * cell_w) + inset
        right = ((cell_idx + 1) * cell_w) - inset
        cell = sheet.crop((left, 0, right, sheet.height))
        sprite = fit_sprite(cell, target, fill)
        path = RAW / folder / f"{out_name}.png"
        path.parent.mkdir(parents=True, exist_ok=True)
        sprite.save(path)
        items.append((out_name, sprite))
    return items


def slice_overlap_row_to(sheet_name: str, cell_count: int, specs: list[tuple[int, str, str, int, float]], overlap: float) -> list[tuple[str, Image.Image]]:
    sheet = Image.open(SRC / sheet_name).convert("RGBA")
    cell_w = sheet.width / cell_count
    win_w = cell_w * overlap
    items = []
    for cell_idx, out_name, folder, target, fill in specs:
        cx = (cell_idx + 0.5) * cell_w
        left = max(0, int(round(cx - win_w * 0.5)))
        right = min(sheet.width, int(round(cx + win_w * 0.5)))
        cell = sheet.crop((left, 0, right, sheet.height))
        sprite = fit_sprite(cell, target, fill)
        path = RAW / folder / f"{out_name}.png"
        path.parent.mkdir(parents=True, exist_ok=True)
        sprite.save(path)
        items.append((out_name, sprite))
    return items


def slice_grid_to(sheet_name: str, cols: int, specs: list[tuple[int, str, str, int, float]]) -> list[tuple[str, Image.Image]]:
    sheet = Image.open(SRC / sheet_name).convert("RGBA")
    rows = (max(idx for idx, _name, _folder, _target, _fill in specs) // cols) + 1
    return slice_grid_to_rows(sheet_name, cols, rows, specs)


def slice_grid_to_rows(sheet_name: str, cols: int, rows: int, specs: list[tuple[int, str, str, int, float]]) -> list[tuple[str, Image.Image]]:
    sheet = Image.open(SRC / sheet_name).convert("RGBA")
    cell_w = sheet.width / cols
    cell_h = sheet.height / rows
    inset_x = max(4, int(round(cell_w * 0.08)))
    inset_y = max(4, int(round(cell_h * 0.08)))
    items = []
    for cell_idx, out_name, folder, target, fill in specs:
        col = cell_idx % cols
        row = cell_idx // cols
        left = int(round(col * cell_w)) + inset_x
        top = int(round(row * cell_h)) + inset_y
        right = int(round((col + 1) * cell_w)) - inset_x
        bottom = int(round((row + 1) * cell_h)) - inset_y
        sprite = fit_sprite(sheet.crop((left, top, right, bottom)), target, fill)
        path = RAW / folder / f"{out_name}.png"
        path.parent.mkdir(parents=True, exist_ok=True)
        sprite.save(path)
        items.append((out_name, sprite))
    return items


def slice_grid_rect_to(sheet_name: str, cols: int, specs: list[tuple[int, str, str, int, int, float]]) -> list[tuple[str, Image.Image]]:
    sheet = Image.open(SRC / sheet_name).convert("RGBA")
    rows = (max(idx for idx, _name, _folder, _w, _h, _fill in specs) // cols) + 1
    return slice_grid_rect_to_rows(sheet_name, cols, rows, specs)


def slice_grid_rect_to_rows(sheet_name: str, cols: int, rows: int, specs: list[tuple[int, str, str, int, int, float]]) -> list[tuple[str, Image.Image]]:
    sheet = Image.open(SRC / sheet_name).convert("RGBA")
    cell_w = sheet.width / cols
    cell_h = sheet.height / rows
    inset_x = max(4, int(round(cell_w * 0.08)))
    inset_y = max(4, int(round(cell_h * 0.08)))
    items = []
    for cell_idx, out_name, folder, width, height, fill in specs:
        col = cell_idx % cols
        row = cell_idx // cols
        left = int(round(col * cell_w)) + inset_x
        top = int(round(row * cell_h)) + inset_y
        right = int(round((col + 1) * cell_w)) - inset_x
        bottom = int(round((row + 1) * cell_h)) - inset_y
        sprite = fit_sprite_rect(sheet.crop((left, top, right, bottom)), width, height, fill)
        path = RAW / folder / f"{out_name}.png"
        path.parent.mkdir(parents=True, exist_ok=True)
        sprite.save(path)
        items.append((out_name, sprite))
    return items


def write_full_bleed_ground(sheet_name: str, cols: int, cell_idx: int) -> tuple[str, Image.Image]:
    sheet = Image.open(SRC / sheet_name).convert("RGBA")
    rows = (cell_idx // cols) + 1
    cell_w = sheet.width / cols
    cell_h = sheet.height / rows
    col = cell_idx % cols
    row = cell_idx // cols
    cell = sheet.crop((int(round(col * cell_w)), int(round(row * cell_h)), int(round((col + 1) * cell_w)), int(round((row + 1) * cell_h))))
    keyed = key_to_alpha(cell)
    bbox = keyed.getchannel("A").getbbox()
    if bbox is None:
        raise RuntimeError("empty ground sprite")
    cropped = keyed.crop(bbox)
    trim = max(4, min(cropped.width, cropped.height) // 5)
    if cropped.width > trim * 2 and cropped.height > trim * 2:
        cropped = cropped.crop((trim, trim, cropped.width - trim, cropped.height - trim))
    sand = cropped.resize((128, 128), Image.Resampling.LANCZOS)
    stat = ImageStat.Stat(sand.convert("RGB"), sand.getchannel("A"))
    avg = tuple(int(v) for v in stat.mean[:3])
    out = Image.new("RGBA", (128, 128), (*avg, 255))
    out.alpha_composite(sand)
    out.putalpha(255)
    path = RAW / "ground" / "ground_sand_base_01.png"
    path.parent.mkdir(parents=True, exist_ok=True)
    out.save(path)
    return ("ground_sand_base_01", out)


def detected_row_cells(sheet_name: str) -> list[Image.Image]:
    keyed = key_to_alpha(Image.open(SRC / sheet_name).convert("RGBA"))
    alpha = keyed.getchannel("A")
    bbox = alpha.getbbox()
    if bbox is None:
        raise RuntimeError(f"empty sheet {sheet_name}")
    cols = []
    for x in range(bbox[0], bbox[2]):
        seen = False
        for y in range(bbox[1], bbox[3]):
            if alpha.getpixel((x, y)) > 0:
                seen = True
                break
        if seen:
            cols.append(x)
    groups: list[list[int]] = []
    gap = max(18, keyed.width // 80)
    for x in cols:
        if not groups or x - groups[-1][-1] > gap:
            groups.append([x])
        else:
            groups[-1].append(x)
    cells = []
    for group in groups:
        x0 = max(0, group[0] - gap)
        x1 = min(keyed.width, group[-1] + gap + 1)
        col_bbox = alpha.crop((x0, bbox[1], x1, bbox[3])).getbbox()
        if col_bbox is None:
            continue
        y0 = bbox[1] + col_bbox[1]
        y1 = bbox[1] + col_bbox[3]
        cells.append(keyed.crop((x0, max(0, y0 - gap), x1, min(keyed.height, y1 + gap))))
    return cells


def slice_detected_row_to(sheet_name: str, specs: list[tuple[int, str, str, int, float]]) -> list[tuple[str, Image.Image]]:
    cells = detected_row_cells(sheet_name)
    max_cell = max(idx for idx, _name, _folder, _target, _fill in specs)
    if len(cells) <= max_cell:
        raise RuntimeError(f"{sheet_name}: detected {len(cells)} cells, need {max_cell + 1}")
    items = []
    for cell_idx, out_name, folder, target, fill in specs:
        sprite = fit_sprite(cells[cell_idx], target, fill)
        path = RAW / folder / f"{out_name}.png"
        path.parent.mkdir(parents=True, exist_ok=True)
        sprite.save(path)
        items.append((out_name, sprite))
    return items


def sheet(items: list[tuple[str, Image.Image]], out: Path, cell: int, cols: int) -> None:
    pad = 12
    label_h = 18
    rows = (len(items) + cols - 1) // cols
    img = Image.new("RGBA", (cols * (cell + pad) + pad, rows * (cell + label_h + pad) + pad), (36, 29, 22, 255))
    d = ImageDraw.Draw(img)
    for idx, (name, sprite) in enumerate(items):
        x = pad + (idx % cols) * (cell + pad)
        y = pad + (idx // cols) * (cell + label_h + pad)
        d.rounded_rectangle([x, y, x + cell, y + cell], radius=4, fill=(70, 55, 38, 255))
        img.alpha_composite(sprite, (x, y))
        d.text((x + 2, y + cell + 2), name, fill=(232, 211, 169, 255))
    out.parent.mkdir(parents=True, exist_ok=True)
    img.save(out)


def scale_sheet(items: list[tuple[str, Image.Image]], out: Path, cell: int, cols: int) -> None:
    pad = 8
    rows = (len(items) + cols - 1) // cols
    img = Image.new("RGBA", (cols * (cell + pad) + pad, rows * (cell + pad) + pad), (36, 29, 22, 255))
    for idx, (_name, sprite) in enumerate(items):
        x = pad + (idx % cols) * (cell + pad)
        y = pad + (idx // cols) * (cell + pad)
        small = sprite.resize((cell, cell), Image.Resampling.LANCZOS)
        img.alpha_composite(small, (x, y))
    out.parent.mkdir(parents=True, exist_ok=True)
    img.save(out)


def warm_magenta_noise(img: Image.Image) -> Image.Image:
    out = img.convert("RGBA")
    pixels = out.load()
    w, h = out.size
    for y in range(h):
        for x in range(w):
            r, g, b, a = pixels[x, y]
            if a == 0:
                continue
            if r > 125 and b > 85 and r > g + 28 and b > g + 18:
                lum = (r * 30 + g * 59 + b * 11) // 100
                pixels[x, y] = (min(235, lum + 58), min(178, max(82, (lum * 3) // 4 + 35)), min(124, max(48, lum // 2 + 18)), a)
    return out


def warm_saved(paths: list[Path]) -> list[tuple[str, Image.Image]]:
    items = []
    for path in paths:
        sprite = warm_magenta_noise(Image.open(path).convert("RGBA"))
        sprite.save(path)
        items.append((path.stem, sprite))
    return items


def validate(paths: list[Path], size: int) -> None:
    bad = []
    for path in paths:
        im = Image.open(path)
        if im.mode != "RGBA" or im.size != (size, size) or im.getchannel("A").getbbox() is None:
            bad.append((str(path), im.mode, im.size))
    if bad:
        raise SystemExit(f"invalid sliced assets: {bad}")


def validate_rect(specs: list[tuple[Path, int, int]]) -> None:
    bad = []
    for path, width, height in specs:
        im = Image.open(path)
        if im.mode != "RGBA" or im.size != (width, height) or im.getchannel("A").getbbox() is None:
            bad.append((str(path), im.mode, im.size, (width, height)))
    if bad:
        raise SystemExit(f"invalid sliced assets: {bad}")


def main() -> None:
    hero_specs = [
        (0, "hero_wayfarer_idle_s", "hero", 128, 128, 0.9),
        (1, "hero_wayfarer_walk_s", "hero", 128, 128, 0.9),
        (2, "hero_wayfarer_walk_e", "hero", 128, 128, 0.9),
        (3, "hero_wayfarer_walk_n", "hero", 128, 128, 0.9),
        (4, "hero_wayfarer_walk_w", "hero", 128, 128, 0.9),
    ]
    hero_items = slice_grid_rect_to_rows("hero_aul_sticker_sheet_ai.png", 4, 3, hero_specs)
    hero_panel_specs = [
        (0, "hero_wayfarer_panel", "hero", 160, 220, 0.95),
        (1, "hero_body_panel", "hero", 128, 192, 0.94),
        (2, "hero_mind_panel", "hero", 128, 192, 0.94),
        (3, "hero_spirit_panel", "hero", 128, 192, 0.94),
    ]
    hero_items.extend(slice_grid_rect_to("hero_portraits_sticker_sheet_ai.png", 4, hero_panel_specs))
    hero_specs.extend(hero_panel_specs)
    hero_items = warm_saved([RAW / folder / f"{name}.png" for _idx, name, folder, _w, _h, _fill in hero_specs])
    scale_sheet(hero_items, RAW / "_review" / "art_brief_hero_ingame_scale.png", 72, 5)
    validate_rect([(RAW / folder / f"{name}.png", w, h) for _idx, name, folder, w, h, _fill in hero_specs])

    aul_specs = [
        (0, "aul_ground_2x2", "aul", 256, 256, 0.98),
        (1, "aul_yurt_small_01", "aul", 128, 128, 0.88),
        (2, "aul_yurt_small_02", "aul", 128, 128, 0.88),
        (3, "aul_fire_01", "aul", 128, 128, 0.82),
        (4, "aul_tamga_post_01", "aul", 128, 128, 0.88),
        (5, "aul_stage_01_camp", "aul", 256, 256, 0.92),
        (6, "aul_stage_02_settlement", "aul", 256, 256, 0.92),
        (7, "aul_stage_03_village", "aul", 256, 256, 0.92),
        (8, "aul_stage_04_fortified_aul", "aul", 256, 256, 0.92),
        (9, "aul_stage_05_steppe_capital", "aul", 256, 256, 0.92),
    ]
    aul_component_specs = [
        (0, "aul_ground_2x2", "aul", 256, 256, 0.98),
        (1, "aul_yurt_small_01", "aul", 128, 128, 0.88),
        (2, "aul_yurt_small_02", "aul", 128, 128, 0.88),
        (3, "aul_fire_01", "aul", 128, 128, 0.82),
        (4, "aul_tamga_post_01", "aul", 128, 128, 0.88),
    ]
    aul_items = slice_grid_rect_to("aul_components_sticker_sheet_ai.png", 5, aul_component_specs)
    aul_progression_specs = [
        (0, "aul_stage_01_camp", "aul", 256, 256, 0.92),
        (1, "aul_stage_02_settlement", "aul", 256, 256, 0.92),
        (2, "aul_stage_03_village", "aul", 256, 256, 0.92),
        (3, "aul_stage_04_fortified_aul", "aul", 256, 256, 0.92),
        (4, "aul_stage_05_steppe_capital", "aul", 256, 256, 0.92),
    ]
    aul_items.extend(slice_grid_rect_to("aul_progression_sticker_sheet_ai.png", 5, aul_progression_specs))
    aul_items = warm_saved([RAW / folder / f"{name}.png" for _idx, name, folder, _w, _h, _fill in aul_specs])
    scale_sheet(aul_items, RAW / "_review" / "art_brief_aul_ingame_scale.png", 72, 5)
    validate_rect([(RAW / folder / f"{name}.png", w, h) for _idx, name, folder, w, h, _fill in aul_specs])

    world_specs = [
        (0, "decor_dune_01", "decor", 128, 0.9),
        (1, "decor_stones_01", "decor", 128, 0.82),
        (2, "decor_dry_grass_01", "decor", 128, 0.86),
        (3, "decor_tracks_01", "decor", 128, 0.9),
        (4, "decor_bones_01", "decor", 128, 0.82),
        (5, "decor_cracks_01", "decor", 128, 0.9),
        (6, "road_straight_ns", "road", 128, 0.98),
        (7, "road_straight_ew", "road", 128, 0.98),
        (8, "road_corner_ne", "road", 128, 0.98),
        (9, "road_corner_es", "road", 128, 0.98),
        (10, "road_corner_sw", "road", 128, 0.98),
        (11, "road_corner_wn", "road", 128, 0.98),
        (12, "road_entry_aul", "road", 128, 0.92),
        (13, "road_current_highlight", "road", 128, 0.92),
        (14, "buffer_edge_stones_01", "road", 128, 0.86),
        (15, "buffer_packed_sand_01", "road", 128, 0.9),
        (16, "buffer_stakes_01", "road", 128, 0.86),
        (17, "buffer_cart_marks_01", "road", 128, 0.9),
        (18, "ground_sand_base_01", "ground", 128, 0.98),
    ]
    world_items = slice_grid_to("world_sticker_sheet_ai.png", 5, world_specs)
    ground_item = write_full_bleed_ground("world_sticker_sheet_ai.png", 5, 18)
    world_items = [item if item[0] != "ground_sand_base_01" else ground_item for item in world_items]
    world_items = warm_saved([RAW / folder / f"{name}.png" for _idx, name, folder, _size, _fill in world_specs])
    scale_sheet(world_items, RAW / "_review" / "art_brief_world_ingame_scale.png", 56, 5)
    validate([RAW / folder / f"{name}.png" for _idx, name, folder, _size, _fill in world_specs], 128)

    tile_names = [
        "tile_war_1",
        "tile_war_2",
        "tile_war_3",
        "tile_horse_1",
        "tile_horse_2",
        "tile_horse_3",
        "tile_steppe_1",
        "tile_steppe_2",
        "tile_steppe_3",
        "tile_home_1",
        "tile_home_2",
        "tile_home_3",
        "tile_water_1",
        "tile_water_2",
        "tile_water_3",
    ]
    tile_specs = [(i, name, "tiles", 128, 0.84) for i, name in enumerate(tile_names)]
    tile_items = slice_grid_to("tiles_sticker_sheet_ai.png", 5, tile_specs)
    tile_paths = [RAW / "tiles" / f"{name}.png" for name in tile_names]
    tile_items = warm_saved(tile_paths)

    base_tile_names = ["tile_saxaul_01", "tile_yurt_01", "tile_tamga_stone_01", "tile_oasis_01", "tile_last_tamga_01"]
    base_tile_items = slice_row("base_tiles_sticker_sheet_ai.png", base_tile_names, 128, 0.86)
    base_tile_items = warm_saved([RAW / "tiles" / f"{name}.png" for name in base_tile_names])
    tile_items.extend(base_tile_items)
    tile_paths.extend(RAW / "tiles" / f"{name}.png" for name in base_tile_names)

    enemy_names = ["tile_wolf_track_01", "tile_mirage_01", "tile_storm_01"]
    enemy_specs = [(0, "tile_wolf_track_01", "tiles", 128, 0.82), (1, "tile_mirage_01", "tiles", 128, 0.82), (2, "tile_storm_01", "tiles", 128, 0.82)]
    enemy_items = slice_grid_to_rows("enemies_bosses_sticker_sheet_ai.png", 4, 2, enemy_specs)
    enemy_items = warm_saved([RAW / "tiles" / f"{name}.png" for name in enemy_names])
    tile_paths.extend(RAW / "tiles" / f"{name}.png" for name in enemy_names)
    sheet(tile_items, RAW / "_review" / "art_brief_tiles_sheet.png", 128, 5)
    scale_sheet(tile_items, RAW / "_review" / "art_brief_tiles_ingame_scale.png", 56, 5)
    sheet(enemy_items, RAW / "_review" / "art_brief_enemies_sheet.png", 128, 3)
    scale_sheet(enemy_items, RAW / "_review" / "art_brief_enemies_ingame_scale.png", 56, 3)
    validate(tile_paths, 128)

    future_tile_names = [
        "tile_buried_spring_01",
        "tile_clan_camp_01",
        "tile_false_path_01",
        "tile_hunting_trail_01",
        "tile_pack_01",
        "tile_small_camp_01",
        "tile_vision_01",
        "tile_watchtower_01",
        "tile_well_01",
    ]
    future_tile_specs = [(i, name, "tiles", 128, 0.86) for i, name in enumerate(future_tile_names)]
    future_tile_items = slice_grid_to_rows("future_tiles_sticker_sheet_ai.png", 5, 2, future_tile_specs)
    future_tile_items = warm_saved([RAW / "tiles" / f"{name}.png" for name in future_tile_names])
    sheet(future_tile_items, RAW / "_review" / "art_brief_future_tiles_sheet.png", 128, 3)
    scale_sheet(future_tile_items, RAW / "_review" / "art_brief_future_tiles_ingame_scale.png", 56, 3)
    validate([RAW / "tiles" / f"{name}.png" for name in future_tile_names], 128)

    boss_names = ["boss_fat", "boss_swift", "boss_fierce", "boss_ring_keeper"]
    boss_items = slice_row("bosses_sticker_sheet_ai.png", boss_names, 160, 0.86)
    boss_items = warm_saved([RAW / "enemies" / f"{name}.png" for name in boss_names])
    sheet(boss_items, RAW / "_review" / "art_brief_bosses_sheet.png", 160, 4)
    scale_sheet(boss_items, RAW / "_review" / "art_brief_bosses_ingame_scale.png", 72, 4)
    validate([RAW / "enemies" / f"{name}.png" for name in boss_names], 160)

    ui_specs = [
        (0, "die_d4", "ui", 64, 0.88),
        (1, "die_d6", "ui", 64, 0.88),
        (2, "die_d8", "ui", 64, 0.88),
        (3, "die_d10", "ui", 64, 0.88),
        (4, "die_d12", "ui", 64, 0.88),
        (5, "die_d20", "ui", 64, 0.88),
        (6, "pouch_closed", "ui", 96, 0.9),
        (6, "pouch_open_0", "ui", 96, 0.9),
        (7, "pouch_open_1", "ui", 96, 0.9),
        (8, "pouch_open_2", "ui", 96, 0.9),
        (9, "pouch_open_3", "ui", 96, 0.9),
    ]
    ui_items = slice_grid_to("ui_sheet_ai.png", 4, ui_specs)
    sheet(ui_items, RAW / "_review" / "art_brief_ui_sheet.png", 96, 6)
    validate([RAW / "ui" / f"{name}.png" for _idx, name, _folder, size, _fill in ui_specs if size == 64], 64)
    validate([RAW / "ui" / f"{name}.png" for _idx, name, _folder, size, _fill in ui_specs if size == 96], 96)

    icon_names = ["ev_storm_32", "ev_rockfall_32", "ev_chase_32", "aul_force_32", "aul_speed_32", "aul_vigor_32", "aul_keep_32"]
    icon_specs = [(i, name, "icons", 32, 0.92) for i, name in enumerate(icon_names)]
    icon_items = slice_overlap_row_to("icons_sheet_ai.png", 7, icon_specs, 1.35)
    sheet(icon_items, RAW / "_review" / "art_brief_icons_sheet.png", 48, 7)
    validate([RAW / "icons" / f"{name}.png" for name in icon_names], 32)

    card_art_names = [
        "card_art_saxaul_64",
        "card_art_yurt_64",
        "card_art_tamga_stone_64",
        "card_art_wolf_track_64",
        "card_art_oasis_64",
        "card_art_mirage_64",
        "card_art_storm_64",
        "card_art_last_tamga_64",
        "card_art_well_64",
        "card_art_watchtower_64",
    ]
    card_art_specs = [(i, name, "cards", 64, 0.9) for i, name in enumerate(card_art_names)]
    card_art_items = slice_grid_to("card_art_sticker_sheet_ai.png", 5, card_art_specs)
    card_art_items = warm_saved([RAW / "cards" / f"{name}.png" for name in card_art_names])
    sheet(card_art_items, RAW / "_review" / "art_brief_card_art_sheet.png", 64, 5)
    scale_sheet(card_art_items, RAW / "_review" / "art_brief_card_art_ingame_scale.png", 32, 5)
    validate([RAW / "cards" / f"{name}.png" for name in card_art_names], 64)

    equipment_names = [
        "equip_slot_weapon_01",
        "equip_slot_clothes_01",
        "equip_slot_tamga_01",
        "equip_slot_tool_01",
        "equip_weapon_staff_01",
        "equip_clothes_cloak_01",
        "equip_tamga_charm_01",
        "equip_tool_satchel_01",
    ]
    equipment_specs = [(i, name, "equipment", 64, 0.9) for i, name in enumerate(equipment_names)]
    equipment_items = slice_grid_to("equipment_sticker_sheet_ai.png", 4, equipment_specs)
    equipment_items = warm_saved([RAW / "equipment" / f"{name}.png" for name in equipment_names])
    sheet(equipment_items, RAW / "_review" / "art_brief_equipment_sheet.png", 64, 4)
    scale_sheet(equipment_items, RAW / "_review" / "art_brief_equipment_ingame_scale.png", 32, 4)
    validate([RAW / "equipment" / f"{name}.png" for name in equipment_names], 64)

    meta_icon_names = [
        "icon_aul_upgrade_32",
        "icon_card_gain_32",
        "icon_deck_32",
        "icon_map_32",
        "icon_memory_32",
        "icon_warning_32",
    ]
    meta_icon_specs = [(i, name, "icons", 32, 0.94) for i, name in enumerate(meta_icon_names)]
    meta_icon_items = slice_grid_to("meta_icons_sheet_ai.png", 3, meta_icon_specs)
    meta_icon_items = warm_saved([RAW / "icons" / f"{name}.png" for name in meta_icon_names])
    sheet(meta_icon_items, RAW / "_review" / "art_brief_meta_icons_sheet.png", 48, 6)
    validate([RAW / "icons" / f"{name}.png" for name in meta_icon_names], 32)

    fx_names = ["fx_merge_0", "fx_merge_1", "fx_merge_2", "fx_merge_3", "fx_hit_0", "fx_hit_1", "fx_hit_2", "fx_hit_3"]
    fx_specs = [(i, name, "fx", 64, 0.94) for i, name in enumerate(fx_names)]
    fx_items = slice_grid_to("fx_sheet_ai.png", 4, fx_specs)
    sheet(fx_items, RAW / "_review" / "art_brief_fx_sheet.png", 64, 8)
    validate([RAW / "fx" / f"{name}.png" for name in fx_names], 64)
    print("sliced AI art assets ok")


if __name__ == "__main__":
    main()
