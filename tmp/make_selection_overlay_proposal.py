from pathlib import Path
import math

from PIL import Image, ImageDraw, ImageFilter, ImageFont


ROOT = Path("gamedesign/assets/concept/pass_selection_overlay")
OUT_UI = ROOT / "proposed_runtime" / "ui"
OUT_UI.mkdir(parents=True, exist_ok=True)

SOURCES = {
    "valid": ROOT / "selection_overlay_valid_source.png",
    "invalid": ROOT / "selection_overlay_invalid_source.png",
    "hover": ROOT / "selection_overlay_hover_source.png",
}
OUTPUTS = {
    "valid": OUT_UI / "ui_valid_cell_overlay_128.png",
    "invalid": OUT_UI / "ui_invalid_cell_overlay_128.png",
    "hover": OUT_UI / "ui_hover_cell_overlay_128.png",
}


def key_to_alpha(img):
    img = img.convert("RGBA")
    px = img.load()
    key = (255, 0, 255)
    for y in range(img.height):
        for x in range(img.width):
            r, g, b, a = px[x, y]
            d = math.sqrt((r - key[0]) ** 2 + (g - key[1]) ** 2 + (b - key[2]) ** 2)
            if d < 42:
                px[x, y] = (r, g, b, 0)
            elif d < 145:
                alpha = int(255 * (d - 42) / (145 - 42))
                rr = min(255, int(r * 0.65 + 196 * 0.35))
                gg = min(255, int(g * 0.65 + 176 * 0.35))
                bb = min(255, int(b * 0.65 + 134 * 0.35))
                px[x, y] = (rr, gg, bb, min(a, alpha))
    return img


def trim_transparent(img, pad=20):
    bbox = img.getchannel("A").getbbox()
    if bbox is None:
        return img
    l, t, r, b = bbox
    l = max(0, l - pad)
    t = max(0, t - pad)
    r = min(img.width, r + pad)
    b = min(img.height, b + pad)
    return img.crop((l, t, r, b))


def fit_square(img, size=128, margin=5):
    img = trim_transparent(img)
    scale = (size - margin * 2) / max(img.size)
    resized = img.resize(
        (max(1, round(img.width * scale)), max(1, round(img.height * scale))),
        Image.Resampling.LANCZOS,
    )
    out = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    out.alpha_composite(resized, ((size - resized.width) // 2, (size - resized.height) // 2))
    r, g, b, a = out.split()
    a = a.filter(ImageFilter.GaussianBlur(0.25))
    return Image.merge("RGBA", (r, g, b, a))


def palette_pass(img, target, strength):
    img = img.convert("RGBA")
    px = img.load()
    for y in range(img.height):
        for x in range(img.width):
            r, g, b, a = px[x, y]
            if a == 0:
                continue
            local = strength * min(1.0, a / 180.0)
            px[x, y] = (
                int(r * (1.0 - local) + target[0] * local),
                int(g * (1.0 - local) + target[1] * local),
                int(b * (1.0 - local) + target[2] * local),
                a,
            )
    return img


processed = {}
for name, path in SOURCES.items():
    overlay = fit_square(key_to_alpha(Image.open(path)))
    if name == "valid":
        overlay = palette_pass(overlay, (82, 190, 160), 0.46)
    elif name == "invalid":
        overlay = palette_pass(overlay, (190, 78, 62), 0.38)
    else:
        overlay = palette_pass(overlay, (225, 178, 92), 0.42)
    overlay.save(OUTPUTS[name])
    processed[name] = overlay

font = ImageFont.load_default()
panel_w = 420
panel_h = 470
source_sheet = Image.new("RGBA", (panel_w * 3, panel_h), (20, 23, 31, 255))
draw = ImageDraw.Draw(source_sheet)
for idx, name in enumerate(("valid", "invalid", "hover")):
    img = Image.open(SOURCES[name]).convert("RGBA")
    img.thumbnail((380, 380), Image.Resampling.LANCZOS)
    x = idx * panel_w + (panel_w - img.width) // 2
    source_sheet.alpha_composite(img, (x, 54))
    draw.text(
        (idx * panel_w + 22, 18),
        f"{name} source - generated proposal input",
        fill=(224, 213, 185),
        font=font,
    )
source_sheet.save(ROOT / "selection_overlay_source.png")

screenshot = Image.open("tmp/normal_gameplay_placement_state.png").convert("RGBA")
contact = Image.new("RGBA", (1280, 950), (15, 18, 28, 255))
contact.alpha_composite(screenshot, (0, 0))

for name, pos in {
    "valid": (331, 84),
    "invalid": (573, 86),
    "hover": (813, 407),
}.items():
    overlay = processed[name].resize((58, 58), Image.Resampling.LANCZOS)
    contact.alpha_composite(overlay, pos)

draw = ImageDraw.Draw(contact)
draw.rectangle((0, 720, 1280, 900), fill=(19, 23, 35, 255))
draw.text(
    (32, 740),
    "selection_overlay proposal contact - runtime PNGs composited on current gameplay scale",
    fill=(224, 213, 185),
    font=font,
)
preview_x = 56
for name in ("valid", "invalid", "hover"):
    draw.text((preview_x, 768), OUTPUTS[name].name, fill=(178, 185, 199), font=font)
    sand = Image.new("RGBA", (128, 128), (177, 145, 103, 255))
    sd = ImageDraw.Draw(sand)
    for i in range(0, 128, 8):
        sd.line((0, i, 128, i + 18), fill=(195, 171, 128, 55), width=2)
    contact.alpha_composite(sand, (preview_x, 790))
    contact.alpha_composite(processed[name], (preview_x, 790))
    preview_x += 360
contact.save(ROOT / "selection_overlay_contact.png")

for name, path in OUTPUTS.items():
    im = Image.open(path)
    alpha = im.getchannel("A")
    print(f"{path} size={im.size} mode={im.mode} alpha_bbox={alpha.getbbox()} alpha_extrema={alpha.getextrema()}")
print(ROOT / "selection_overlay_source.png")
print(ROOT / "selection_overlay_contact.png")
