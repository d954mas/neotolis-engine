#!/usr/bin/env python3
"""Generate the ui_stateful_demo sprite art (CC0, hand-authored).

Visual QA of these widgets is the user's eyes only (feedback_visual_check_user_only);
the demo permits hand-authored minimal box/check/ring/dot/track/thumb art per the
phase RESEARCH Environment fallback. Run from this directory:  python gen_art.py
"""

import math
from PIL import Image, ImageDraw

SS = 4  # supersample factor for crisp anti-aliased edges


def _canvas(w, h):
    return Image.new("RGBA", (w * SS, h * SS), (0, 0, 0, 0))


def _save(img, w, h, name):
    img = img.resize((w, h), Image.LANCZOS)
    img.save(name)
    print(f"  wrote {name}  ({w}x{h})")


def rounded_rect(draw, box, radius, **kw):
    draw.rounded_rectangle(box, radius=radius, **kw)


# ---- checkbox box (off): rounded square, soft fill + visible border ----
def gen_box_off():
    W = H = 64
    img = _canvas(W, H)
    d = ImageDraw.Draw(img)
    m = 3 * SS
    rounded_rect(
        d,
        [m, m, W * SS - m, H * SS - m],
        radius=10 * SS,
        fill=(58, 64, 78, 255),
        outline=(150, 158, 172, 255),
        width=3 * SS,
    )
    _save(img, W, H, "box_off.png")


# ---- checkmark overlay: bold tick, white so check_tint can recolor it ----
def gen_checkmark():
    W = H = 64
    img = _canvas(W, H)
    d = ImageDraw.Draw(img)
    pts = [(14 * SS, 34 * SS), (27 * SS, 47 * SS), (52 * SS, 16 * SS)]
    d.line(pts, fill=(255, 255, 255, 255), width=9 * SS, joint="curve")
    # round the stroke ends
    for p in (pts[0], pts[-1]):
        r = 4 * SS
        d.ellipse([p[0] - r, p[1] - r, p[0] + r, p[1] + r], fill=(255, 255, 255, 255))
    _save(img, W, H, "checkmark.png")


# ---- radio ring: circular outline ----
def gen_radio_ring():
    W = H = 64
    img = _canvas(W, H)
    d = ImageDraw.Draw(img)
    m = 3 * SS
    d.ellipse(
        [m, m, W * SS - m, H * SS - m],
        fill=(58, 64, 78, 255),
        outline=(150, 158, 172, 255),
        width=3 * SS,
    )
    _save(img, W, H, "radio_ring.png")


# ---- radio dot: filled circle (white -> recolor via check_tint) ----
def gen_radio_dot():
    W = H = 64
    img = _canvas(W, H)
    d = ImageDraw.Draw(img)
    c = (W * SS) // 2
    r = 18 * SS
    d.ellipse([c - r, c - r, c + r, c + r], fill=(255, 255, 255, 255))
    _save(img, W, H, "radio_dot.png")


# ---- toggle track: rounded pill, slice9-able. Wider (128x48) so the 24px
# horizontal slice9 keeps the full rounded end-caps while top+bottom (32) stays
# under the 48px height (builder asserts top+bottom < height, left+right < width).
def gen_track(name, fill):
    W, H = 128, 48
    img = _canvas(W, H)
    d = ImageDraw.Draw(img)
    rounded_rect(
        d,
        [0, 0, W * SS - 1, H * SS - 1],
        radius=(H * SS) // 2,
        fill=fill,
        outline=(20, 24, 32, 255),
        width=2 * SS,
    )
    _save(img, W, H, name)


# ---- toggle thumb: filled circle with a subtle ring ----
def gen_thumb():
    W = H = 48
    img = _canvas(W, H)
    d = ImageDraw.Draw(img)
    m = 2 * SS
    d.ellipse(
        [m, m, W * SS - m, H * SS - m],
        fill=(245, 247, 250, 255),
        outline=(180, 186, 196, 255),
        width=2 * SS,
    )
    _save(img, W, H, "thumb.png")


if __name__ == "__main__":
    print("=== ui_stateful_demo art ===")
    gen_box_off()
    gen_checkmark()
    gen_radio_ring()
    gen_radio_dot()
    gen_track("track_off.png", (70, 78, 92, 255))   # neutral grey  (OFF)
    gen_track("track_on.png", (76, 168, 96, 255))    # green         (ON)
    gen_thumb()
    print("=== done ===")
