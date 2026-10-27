"""Pixel-health checks for captured frames.

The single decode path for capture payloads plus a "something rendered" smoke: decode the base64 PNG,
assert decoded dims match the reported dims, and assert the frame is not a single uniform color
(black / frozen / empty screen).

Dependency boundary: Pillow + numpy are imported HERE only — the harness core (transport / client /
other scenarios) stays stdlib-only. Scenarios import this lazily inside their capture branch so a
non-capture run never pulls these deps.
"""
import base64
import io

from PIL import Image  # capture-scoped dep — never imported by the harness core.
import numpy as np  # noqa: E402  capture-scoped dep.


def decode(b64: str) -> Image.Image:
    """Decode a base64 PNG string into a PIL Image (the single capture decode path)."""
    if not isinstance(b64, str) or b64 == "":
        raise ValueError("pixel_health.decode: expected a non-empty base64 string")
    raw = base64.b64decode(b64, validate=True)
    return Image.open(io.BytesIO(raw))


def check(img: Image.Image, expected_w: int, expected_h: int) -> None:
    """Assert decoded dims match expected and the frame is not blank. Raises AssertionError on violation.

    not-blank = the pixels are not a single uniform color (ptp == 0 on every channel means every pixel
    is identical — a black / frozen / empty screen, the exact failure this catches). devapi_host
    clears a two-tone pattern, so a healthy frame has >= 2 distinct colors.
    """
    assert img.width == expected_w, f"pixel-health: decoded width {img.width} != expected {expected_w}"
    assert img.height == expected_h, f"pixel-health: decoded height {img.height} != expected {expected_h}"
    arr = np.asarray(img)
    assert arr.size > 0, "pixel-health: decoded image is empty"
    assert float(np.ptp(arr)) > 0.0, "pixel-health: frame is a single uniform color (blank/frozen/empty screen)"


def check_payload(payload: dict, expected_w: int, expected_h: int) -> Image.Image:
    """Validate a {width,height,format:"png",data:<base64>} capture result end-to-end, return the image.

    Asserts format/reported-dims, decodes the payload, then runs check() over the pixels. Returns the
    decoded image so a caller can optionally save it for human viewing.
    """
    assert payload.get("format") == "png", f"capture: format is {payload.get('format')!r}, expected 'png'"
    rw = payload.get("width")
    rh = payload.get("height")
    assert rw == expected_w, f"capture: reported width {rw} != expected {expected_w}"
    assert rh == expected_h, f"capture: reported height {rh} != expected {expected_h}"
    data = payload.get("data")
    assert isinstance(data, str) and data != "", f"capture: result.data is {data!r}, expected a base64 string"
    img = decode(data)
    check(img, expected_w, expected_h)
    return img
