#!/usr/bin/env python3
"""Hermetic self-test for pixel_health — no GL, no socket, no host.

pixel_health otherwise runs only inside the GL-requiring live UAT. This builds synthetic PNGs in memory
and exercises its positive and negative branches directly, so a regression that silently weakens a
check (drops not-blank, skips a dims assert) fails fast instead of only on the heavyweight live path.

Run from the repo root:
  python3 tools/devapi/scenarios/pixel_health_selftest.py   (exit 0 == all checks held).
"""
import base64
import io
import os
import sys

from PIL import Image  # capture-scoped dep
import numpy as np  # noqa: E402  capture-scoped dep

if __package__ in (None, ""):
    sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", "..")))
    from tools.devapi import pixel_health
else:
    from .. import pixel_health

ORANGE = (229, 140, 25)  # R-dominant fg (matches the host's foreground).
BLUE = (25, 51, 115)     # B-dominant bg.


def _png_b64(arr):
    """Encode an HxWx3 uint8 array as a base64 PNG string (the capture wire shape)."""
    buf = io.BytesIO()
    Image.fromarray(arr).save(buf, format="PNG")
    return base64.b64encode(buf.getvalue()).decode("ascii")


def _payload(arr):
    h, w = arr.shape[0], arr.shape[1]
    return {"width": w, "height": h, "format": "png", "data": _png_b64(arr)}


def _payload_mismatched(reported_w, reported_h, png_arr):
    """A payload whose reported dims are decoupled from the actual PNG pixel dims — a producer that
    encodes a wrong-sized image yet reports the expected size, which only check()'s decoded-dims guard catches."""
    return {"width": reported_w, "height": reported_h, "format": "png", "data": _png_b64(png_arr)}


def _two_tone(w, h):
    """Centered orange fg box on a blue field (the host's pattern), HxWx3 uint8."""
    arr = np.full((h, w, 3), BLUE, dtype=np.uint8)
    arr[h // 4:h * 3 // 4, w // 4:w * 3 // 4] = ORANGE
    return arr


def _expect_assert(fn, label):
    try:
        fn()
    except AssertionError:
        return
    raise SystemExit(f"FAIL: {label} — expected AssertionError, none raised")


def main():
    two = _two_tone(60, 40)

    # 1. not-blank: a uniform frame must FAIL check(), gray or colored (channels differ); a two-tone frame passes.
    _expect_assert(lambda: pixel_health.check(Image.fromarray(np.full((40, 60, 3), 120, np.uint8)), 60, 40),
                   "uniform frame not-blank")
    _expect_assert(lambda: pixel_health.check(Image.fromarray(np.full((40, 60, 3), BLUE, np.uint8)), 60, 40),
                   "uniform colored frame not-blank")
    pixel_health.check(Image.fromarray(two), 60, 40)

    # 2. dims: check_payload must FAIL on a wrong reported size, pass on the right one.
    _expect_assert(lambda: pixel_health.check_payload(_payload(two), 61, 40), "wrong-width dims")
    pixel_health.check_payload(_payload(two), 60, 40)

    # 2b. decoded-vs-reported mismatch: reported dims match expected (passing check_payload's reported-dims
    #     assert) but the PNG decodes to a different size -> check()'s decoded-dims assert must fire.
    _expect_assert(lambda: pixel_health.check_payload(_payload_mismatched(60, 40, _two_tone(30, 20)), 60, 40),
                   "decoded-vs-reported dims mismatch")

    print("pixel_health_selftest: all checks held")


if __name__ == "__main__":
    main()
