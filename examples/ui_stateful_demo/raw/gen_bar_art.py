#!/usr/bin/env python3
"""Procedural CC0 progress-bar + scrollbar art for ui_stateful_demo (issue 4).

Generates the 192x32 progress-bar sprites:
  bar_track.png         - rounded recessed track (slice9-friendly: uniform 8px end-caps (== BAR_BORDER slice9)).
  bar_fill_smooth.png   - rounded vertical-gradient pill (stretches cleanly; STRETCH slice9 + slider fill).
  bar_fill_shaped.png   - diagonal candy stripes + gradient + glossy band (CROP reveal is obvious; NO slice9).

And the 32x32 scrollbar pills (8px slice9 == BAR_BORDER, so the rounded caps stay fixed at any
bar length/orientation — the bar is used both vertical and horizontal):
  scroll_track.png      - subtle recessed slot (dark, soft inner shadow, faint rim).
  bar_thumb.png         - light capsule thumb (vertical gloss gradient + bright top edge).

Pure-Pillow, deterministic. Run from anywhere:
    python examples/ui_stateful_demo/raw/gen_bar_art.py
"""
import math
import os

from PIL import Image, ImageDraw

W, H = 192, 32
# Corner radius == the 8px slice9 border (build_packs.c BAR_BORDER) so the rounded ends sit
# entirely inside the fixed slice9 corner and never stretch — clean at any bar width/height.
RADIUS = 8
# Square scrollbar pieces: 8px slice9 corners are symmetric so the SAME art reads correctly whether
# the bar is stretched tall (vertical) or wide (horizontal).
SB = 32
HERE = os.path.dirname(os.path.abspath(__file__))


def rounded_mask(w, h, r):
    """1-channel alpha mask of a rounded-rect pill."""
    m = Image.new("L", (w, h), 0)
    d = ImageDraw.Draw(m)
    d.rounded_rectangle([0, 0, w - 1, h - 1], radius=r, fill=255)
    return m


def lerp(a, b, t):
    return tuple(int(round(a[i] + (b[i] - a[i]) * t)) for i in range(len(a)))


def make_track():
    """Dark recessed track: vertical gradient (darker center = inner bevel) + lighter rim."""
    img = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    px = img.load()
    top = (58, 64, 82)     # rim highlight
    mid = (28, 31, 42)     # recessed center
    bot = (44, 49, 64)     # lower rim
    for y in range(H):
        t = y / (H - 1)
        if t < 0.5:
            col = lerp(top, mid, t / 0.5)
        else:
            col = lerp(mid, bot, (t - 0.5) / 0.5)
        for x in range(W):
            px[x, y] = (col[0], col[1], col[2], 255)
    img.putalpha(rounded_mask(W, H, RADIUS))
    img.save(os.path.join(HERE, "bar_track.png"))


def make_fill_smooth():
    """Bright green vertical-gradient pill — glossy top, deeper bottom. Stretches cleanly."""
    img = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    px = img.load()
    top = (150, 240, 160)   # glossy highlight
    mid = (78, 200, 110)    # body green
    bot = (40, 150, 78)     # shaded base
    for y in range(H):
        t = y / (H - 1)
        if t < 0.42:
            col = lerp(top, mid, t / 0.42)
        else:
            col = lerp(mid, bot, (t - 0.42) / 0.58)
        for x in range(W):
            px[x, y] = (col[0], col[1], col[2], 255)
    img.putalpha(rounded_mask(W, H, RADIUS))
    img.save(os.path.join(HERE, "bar_fill_smooth.png"))


def make_fill_shaped():
    """Diagonal candy stripes + left->right warm gradient + a glossy highlight band.
    Asymmetric & directional so CROP (clean reveal) vs STRETCH (squash) is obvious."""
    img = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    px = img.load()
    g_left = (255, 168, 64)    # amber (start)
    g_right = (255, 92, 150)   # pink (end) -> horizontal gradient makes reveal-progress legible
    stripe_a = (255, 255, 255)
    period = 22.0              # diagonal stripe period in px
    duty = 0.42                # fraction of the period that is the light stripe
    for y in range(H):
        for x in range(W):
            t = x / (W - 1)
            base = lerp(g_left, g_right, t)
            # Diagonal phase: slope 1 so stripes lean; asymmetric vs a vertical/horizontal bar.
            phase = ((x + y) % period) / period
            if phase < duty:
                # blend toward white stripe, strongest mid-stripe
                k = 1.0 - abs((phase / duty) - 0.5) * 2.0
                col = lerp(base, stripe_a, 0.55 * k)
            else:
                col = base
            # Glossy highlight band across the top third.
            gy = y / (H - 1)
            if gy < 0.34:
                col = lerp(col, (255, 255, 255), 0.22 * (1.0 - gy / 0.34))
            px[x, y] = (col[0], col[1], col[2], 255)
    img.putalpha(rounded_mask(W, H, RADIUS))
    img.save(os.path.join(HERE, "bar_fill_shaped.png"))


def make_scroll_track():
    """Recessed scrollbar slot: dark body, soft inner top shadow, faint lower rim. Square so the
    8px slice9 corners read identically when stretched vertical or horizontal."""
    img = Image.new("RGBA", (SB, SB), (0, 0, 0, 0))
    px = img.load()
    top = (20, 22, 30)     # inner shadow (recessed)
    mid = (32, 35, 46)     # slot body
    bot = (48, 52, 68)     # lower rim catch-light
    for y in range(SB):
        t = y / (SB - 1)
        if t < 0.4:
            col = lerp(top, mid, t / 0.4)
        else:
            col = lerp(mid, bot, (t - 0.4) / 0.6)
        for x in range(SB):
            px[x, y] = (col[0], col[1], col[2], 255)
    img.putalpha(rounded_mask(SB, SB, RADIUS))
    img.save(os.path.join(HERE, "scroll_track.png"))


def make_bar_thumb():
    """Light capsule scrollbar thumb: vertical gloss gradient + a bright top edge, slightly inset
    rounded body so the recessed track shows around it. Square -> symmetric 8px slice9 both ways."""
    img = Image.new("RGBA", (SB, SB), (0, 0, 0, 0))
    px = img.load()
    edge = (238, 243, 252)   # bright top edge
    top = (208, 216, 232)    # gloss highlight
    mid = (176, 186, 206)    # body
    bot = (140, 150, 172)    # shaded base
    for y in range(SB):
        t = y / (SB - 1)
        if t < 0.12:
            col = lerp(edge, top, t / 0.12)
        elif t < 0.5:
            col = lerp(top, mid, (t - 0.12) / 0.38)
        else:
            col = lerp(mid, bot, (t - 0.5) / 0.5)
        for x in range(SB):
            px[x, y] = (col[0], col[1], col[2], 255)
    img.putalpha(rounded_mask(SB, SB, RADIUS))
    img.save(os.path.join(HERE, "bar_thumb.png"))


if __name__ == "__main__":
    make_track()
    make_fill_smooth()
    make_fill_shaped()
    make_scroll_track()
    make_bar_thumb()
    print("wrote bar_track.png, bar_fill_smooth.png, bar_fill_shaped.png,", "scroll_track.png, bar_thumb.png to", HERE)
