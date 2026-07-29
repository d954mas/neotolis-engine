#!/usr/bin/env python3
# Generates deterministic decoded RGBA fixtures with no third-party dependencies.
# Compressed PNG bytes may vary across zlib implementations.

import os
import struct
import sys
import zlib

# #region png encoder
def _png_chunk(tag, data):
    return (
        struct.pack(">I", len(data))
        + tag
        + data
        + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)
    )


def write_png(path, width, height, rgba):
    # rgba: bytes/bytearray of length width*height*4 (8-bit RGBA, color type 6)
    assert len(rgba) == width * height * 4
    stride = width * 4
    raw = bytearray()
    for y in range(height):
        raw.append(0)  # filter type 0 (None) per scanline
        raw.extend(rgba[y * stride : (y + 1) * stride])
    ihdr = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(_png_chunk(b"IHDR", ihdr))
        f.write(_png_chunk(b"IDAT", zlib.compress(bytes(raw), 9)))
        f.write(_png_chunk(b"IEND", b""))
# #endregion

# #region raster primitives
def canvas(w, h):
    return bytearray(w * h * 4)  # fully transparent


def put(buf, w, x, y, r, g, b, a):
    i = (y * w + x) * 4
    buf[i] = r
    buf[i + 1] = g
    buf[i + 2] = b
    buf[i + 3] = a


def fill_rect(buf, w, x0, y0, x1, y1, col):
    for y in range(y0, y1):
        for x in range(x0, x1):
            put(buf, w, x, y, *col)


def fill_disc(buf, w, h, cx, cy, radius, col):
    r2 = radius * radius
    for y in range(max(0, cy - radius), min(h, cy + radius + 1)):
        for x in range(max(0, cx - radius), min(w, cx + radius + 1)):
            dx = x - cx
            dy = y - cy
            if dx * dx + dy * dy <= r2:
                put(buf, w, x, y, *col)


def blit(dst, dst_w, src, src_w, src_h, ox, oy):
    for y in range(src_h):
        di = (((oy + y) * dst_w) + ox) * 4
        si = (y * src_w) * 4
        dst[di : di + (src_w * 4)] = src[si : si + (src_w * 4)]


def mirror_h(src, w, h):
    out = canvas(w, h)
    for y in range(h):
        for x in range(w):
            si = ((y * w) + (w - 1 - x)) * 4
            di = ((y * w) + x) * 4
            out[di : di + 4] = src[si : si + 4]
    return out


def mirror_v(src, w, h):
    out = canvas(w, h)
    for y in range(h):
        si = ((h - 1 - y) * w) * 4
        di = (y * w) * 4
        out[di : di + (w * 4)] = src[si : si + (w * 4)]
    return out


def transpose(src, w, h):
    # Result is h x w: (x, y) -> (y, x).
    out = canvas(h, w)
    for y in range(h):
        for x in range(w):
            si = ((y * w) + x) * 4
            di = ((x * h) + y) * 4
            out[di : di + 4] = src[si : si + 4]
    return out


def rounded_panel(width, height, radius, border, fill, edge):
    # 9-slice panel: rounded transparent corners, `border`-px frame in `edge`,
    # interior filled with `fill`. Transparent corners force a non-rect hull.
    buf = canvas(width, height)
    r2 = radius * radius
    for y in range(height):
        for x in range(width):
            # nearest corner-center distance test for the rounded transparency
            cx = radius if x < radius else (width - 1 - radius if x > width - 1 - radius else x)
            cy = radius if y < radius else (height - 1 - radius if y > height - 1 - radius else y)
            dx = x - cx
            dy = y - cy
            if dx * dx + dy * dy > r2:
                continue  # transparent rounded corner
            on_edge = (
                x < border
                or y < border
                or x >= width - border
                or y >= height - border
            )
            put(buf, width, x, y, *(edge if on_edge else fill))
    return buf
# #endregion

# #region corpora
def gen_rect_only(out):
    # 32 solid opaque rectangles, varied aspect + size. Fully opaque -> hull is
    # the trimmed box (4 verts); the density stress is pure rectangle packing.
    os.makedirs(out, exist_ok=True)
    sizes = []
    w = 8
    for i in range(32):
        # deterministic spread of widths/heights in [8, 160]
        rw = 8 + ((i * 37) % 152)
        rh = 8 + ((i * 53 + 11) % 120)
        sizes.append((rw, rh))
    for i, (rw, rh) in enumerate(sizes):
        buf = canvas(rw, rh)
        col = (32 + (i * 60) % 220, 32 + (i * 90 + 40) % 220, 32 + (i * 150 + 80) % 220, 255)
        fill_rect(buf, rw, 0, 0, rw, rh, col)
        write_png(os.path.join(out, f"rect_{i:02d}.png"), rw, rh, buf)
    return 32


def gen_slice9(out):
    # 12 rounded-corner panels, varied size/border/radius.
    os.makedirs(out, exist_ok=True)
    specs = []
    for i in range(12):
        pw = 48 + (i * 17) % 96
        ph = 40 + (i * 23 + 8) % 88
        rad = 6 + (i * 3) % 14
        bor = 2 + i % 4
        specs.append((pw, ph, rad, bor))
    for i, (pw, ph, rad, bor) in enumerate(specs):
        fill = (40 + (i * 70) % 180, 60 + (i * 40) % 170, 90 + (i * 55) % 150, 255)
        edge = (230, 230, 240, 255)
        buf = rounded_panel(pw, ph, rad, bor, fill, edge)
        write_png(os.path.join(out, f"panel_{i:02d}.png"), pw, ph, buf)
    return 12


def _anim_frame(state):
    # Identical states deliberately produce dedup-able frame bytes.
    size = 64
    buf = canvas(size, size)
    radius = 8 + (state * 5) % 20
    cx = 20 + (state * 7) % 24
    cy = 20 + (state * 11) % 24
    col = (60 + (state * 40) % 190, 50 + (state * 80) % 200, 70 + (state * 25) % 180, 255)
    fill_disc(buf, size, size, cx, cy, radius, col)
    return buf


def gen_anim_heavy(out):
    # Sequences whose frame->state maps repeat, creating byte-identical frames.
    os.makedirs(out, exist_ok=True)
    sequences = {
        # 10 frames, 4 unique states.
        "orb": [0, 1, 2, 3, 3, 2, 1, 0, 1, 2],
        # 8 frames, all-unique states
        "spark": [10, 11, 12, 13, 14, 15, 16, 17],
        # 12 frames, ping-pong -> 6 unique states
        "ring": [20, 21, 22, 23, 24, 25, 25, 24, 23, 22, 21, 20],
    }
    count = 0
    cache = {}
    for name, states in sequences.items():
        for idx, state in enumerate(states):
            if state not in cache:
                cache[state] = _anim_frame(state)
            write_png(os.path.join(out, f"{name}_{idx:02d}.png"), 64, 64, cache[state])
            count += 1
    return count


def _trim_art(state):
    # F-shaped glyph: asymmetric under all eight D4 orientations, and it touches
    # every edge of its box so the alpha trim recovers exactly the art rect.
    aw = 14 + ((state * 2) % 6)
    ah = 10 + ((state * 4) % 8)
    buf = canvas(aw, ah)
    col = (70 + (state * 45) % 180, 90 + (state * 65) % 160, 50 + (state * 85) % 200, 255)
    fill_rect(buf, aw, 0, 0, 3, ah, col)  # stem — left, top and bottom edges
    fill_rect(buf, aw, 0, 0, aw, 3, col)  # top bar — right edge
    fill_rect(buf, aw, 0, ah // 2, aw - 4, (ah // 2) + 2, col)  # middle bar
    return aw, ah, buf


def gen_anim_trim(out):
    # Two groups so each dedup stage is measured on its own: trim_* is
    # byte-identical only AFTER the alpha trim (same art, different canvas and
    # offset), d4_* only through a D4 orientation (mirrored/transposed art).
    os.makedirs(out, exist_ok=True)
    count = 0
    offsets = ((0, 0), (3, 5), (7, 2), (11, 9))
    for state in range(4):
        aw, ah, art = _trim_art(state)
        for k, (ox, oy) in enumerate(offsets):
            cw = aw + ox + 2 + (k * 3)
            ch = ah + oy + 2 + (k * 2)
            buf = canvas(cw, ch)
            blit(buf, cw, art, aw, ah, ox, oy)
            write_png(os.path.join(out, f"trim_s{state}_{k}.png"), cw, ch, buf)
            count += 1
    for state in range(4, 7):
        aw, ah, art = _trim_art(state)
        variants = (
            ("id", aw, ah, art),
            ("mh", aw, ah, mirror_h(art, aw, ah)),
            ("mv", aw, ah, mirror_v(art, aw, ah)),
            ("tr", ah, aw, transpose(art, aw, ah)),
        )
        # A 1px transparent frame on every variant keeps the trim uniform, so a
        # fold here can only come from the orientation search.
        for tag, vw, vh, vbuf in variants:
            cw = vw + 2
            ch = vh + 2
            buf = canvas(cw, ch)
            blit(buf, cw, vbuf, vw, vh, 1, 1)
            write_png(os.path.join(out, f"d4_s{state}_{tag}.png"), cw, ch, buf)
            count += 1
    return count
# #endregion


def main():
    root = sys.argv[1] if len(sys.argv) > 1 else os.path.join("assets", "bench")
    n_rect = gen_rect_only(os.path.join(root, "rect_only"))
    n_slice = gen_slice9(os.path.join(root, "slice9"))
    n_anim = gen_anim_heavy(os.path.join(root, "anim_heavy"))
    n_trim = gen_anim_trim(os.path.join(root, "anim_trim"))
    print(f"rect_only:  {n_rect} pngs")
    print(f"slice9:     {n_slice} pngs")
    print(f"anim_heavy: {n_anim} pngs")
    print(f"anim_trim:  {n_trim} pngs")
    print(f"total:      {n_rect + n_slice + n_anim + n_trim} pngs under {root}/")


if __name__ == "__main__":
    main()
