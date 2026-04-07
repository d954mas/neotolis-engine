#!/usr/bin/env python3
"""Verify alpha coverage of an atlas page against a recorded baseline.

Polygon-tightness iterations must guarantee that NO opaque sprite pixels
are ever clipped. Two checks are layered:

  (1) per-pixel alpha-monotonic invariant — for every pixel coordinate, the
      new page's alpha MUST be >= the recorded baseline alpha. Tighter
      polygons can only relocate sprites (different positions on the page),
      but at any given (x, y) where the baseline had opaque content, the
      new page must also be opaque. Catches per-sprite clipping that a
      sum-only check would miss (e.g. sprite A loses pixels but sprite B
      gains some, totals match by accident).

      Note: this check assumes packing is bit-exact w.r.t. positions. If
      polygon changes ALSO move sprites around (different packing), this
      check is too strict. In that case use --check-sum instead.

  (2) total opaque-pixel sum — coarse fallback when packing positions can
      legitimately change. New count must be >= baseline count.

Usage:
  # Record baseline (BEFORE any tightness experiments):
  check_atlas_coverage.py --record    <page.png> <baseline_dir>
      Saves <baseline_dir>/<page_basename>.alpha.png and .json (sum/dims).

  # Verify per-pixel (strictest, requires bit-exact packing):
  check_atlas_coverage.py --check-px  <page.png> <baseline_dir>

  # Verify sum only (loose, allows packing changes):
  check_atlas_coverage.py --check-sum <page.png> <baseline_dir>

  # Per-sprite verification (strongest, packing-agnostic):
  check_atlas_coverage.py --record-regions <blob.ntpack> <page.png> <baseline_dir>
  check_atlas_coverage.py --check-regions  <blob.ntpack> <page.png> <baseline_dir>
      Looks up each region by name_hash, counts opaque pixels in its UV bbox
      on the new page, and verifies count >= baseline. Catches per-sprite
      clipping even when packing positions move.

  # Stand-alone count (just print, exit 0):
  check_atlas_coverage.py --count     <page.png>

Exit codes:
  0  pass / record success
  1  coverage regression detected
  2  usage / IO error

Threshold: opaque = alpha >= 1 (any non-zero pixel is sprite content).
"""
import json
import os
import sys

try:
    from PIL import Image
except ImportError:
    print("ERROR: PIL/Pillow not installed (pip install Pillow)", file=sys.stderr)
    sys.exit(2)


def open_alpha(page_path):
    """Open page PNG and return (alpha_bytes, width, height, total_opaque)."""
    img = Image.open(page_path)
    if img.mode != "RGBA":
        img = img.convert("RGBA")
    w, h = img.size
    pixels = img.tobytes()
    # alpha is every 4th byte
    alpha = bytes(pixels[i] for i in range(3, len(pixels), 4))
    opaque = sum(1 for b in alpha if b >= 1)
    return alpha, w, h, opaque


def baseline_paths(baseline_dir, page_path):
    base = os.path.splitext(os.path.basename(page_path))[0]
    os.makedirs(os.path.abspath(baseline_dir), exist_ok=True)
    return (
        os.path.join(baseline_dir, f"{base}.alpha.png"),
        os.path.join(baseline_dir, f"{base}.json"),
    )


def save_alpha_png(alpha, w, h, out_path):
    img = Image.frombytes("L", (w, h), alpha)
    img.save(out_path, optimize=True)


def load_alpha_png(in_path, expected_w, expected_h):
    img = Image.open(in_path)
    if img.size != (expected_w, expected_h):
        return None
    if img.mode != "L":
        img = img.convert("L")
    return img.tobytes()


def cmd_record(page_path, baseline_dir):
    alpha, w, h, opaque = open_alpha(page_path)
    alpha_path, json_path = baseline_paths(baseline_dir, page_path)
    save_alpha_png(alpha, w, h, alpha_path)
    pct = 100.0 * opaque / (w * h) if w * h else 0.0
    data = {
        "page": os.path.basename(page_path),
        "width": w,
        "height": h,
        "opaque_pixels": opaque,
        "total_pixels": w * h,
        "fill_pct": round(pct, 4),
        "alpha_mask": os.path.basename(alpha_path),
    }
    with open(json_path, "w", encoding="utf-8") as f:
        json.dump(data, f, indent=2)
    print(f"RECORDED baseline: {opaque} opaque / {w*h} total ({pct:.2f}% fill)")
    print(f"  -> {json_path}")
    print(f"  -> {alpha_path}")
    return 0


def cmd_check_px(page_path, baseline_dir):
    alpha_path, json_path = baseline_paths(baseline_dir, page_path)
    if not os.path.exists(json_path) or not os.path.exists(alpha_path):
        print(
            f"ERROR: baseline {json_path} / {alpha_path} not found. "
            f"Run with --record first.",
            file=sys.stderr,
        )
        return 2
    with open(json_path, "r", encoding="utf-8") as f:
        meta = json.load(f)
    new_alpha, w, h, new_opaque = open_alpha(page_path)
    if w != meta["width"] or h != meta["height"]:
        print(
            f"FAIL: page dimensions changed "
            f"({meta['width']}x{meta['height']} -> {w}x{h})"
        )
        return 1
    base_alpha = load_alpha_png(alpha_path, w, h)
    if base_alpha is None:
        print(f"ERROR: baseline alpha mask {alpha_path} unreadable", file=sys.stderr)
        return 2
    base_opaque = int(meta["opaque_pixels"])

    # Per-pixel monotonic check: at every pixel where baseline was opaque,
    # the new page must also be opaque (alpha >= 1). We use '>= 1' rather
    # than '>= base_alpha' because compose may legitimately change the
    # exact alpha value (compression, format conversion).
    lost = 0
    extra = 0
    sample_lost = []
    for i, b in enumerate(base_alpha):
        n = new_alpha[i]
        if b >= 1 and n < 1:
            lost += 1
            if len(sample_lost) < 5:
                sample_lost.append((i % w, i // w))
        elif b < 1 and n >= 1:
            extra += 1

    delta = new_opaque - base_opaque
    print(
        f"baseline opaque = {base_opaque}, "
        f"current opaque = {new_opaque}, "
        f"delta = {delta:+d}"
    )
    print(
        f"per-pixel: {lost} pixels became transparent, "
        f"{extra} new opaque pixels"
    )
    if lost > 0:
        print(
            f"FAIL: per-pixel coverage regression — {lost} pixels lost "
            f"(polygon clipped sprite content)"
        )
        if sample_lost:
            print(f"  sample lost coords (x,y): {sample_lost}")
        return 1
    if extra > 0:
        print(
            f"WARN: {extra} new opaque pixels (sprites moved? compose changed?). "
            f"This is OK for tightness work but worth investigating."
        )
    print("PASS: per-pixel coverage preserved (no opaque pixels lost)")
    return 0


def parse_blob_polygons(blob_path, page_w, page_h):
    """Parse NtAtlas blob, return dict {vertex_start: polygon} for page 0.

    Uses the same field layout as scripts/check_atlas_overlap.py:
      region struct <QHHhhffHBBBBH>: f[0]=name_hash, f[7]=vertex_start,
                                     f[8]=vertex_count, f[9]=page
      vertex struct <hhHH>: v[2]=u (0..65535), v[3]=v (0..65535)

    Multiple regions can share the same vertex_start (the builder dedupes
    identical sprites — different name_hashes can alias to one shape at
    one position). We group by vertex_start so each unique placement is
    checked exactly once. The vertex_start is a stable identity for a
    placement that survives across iterations as long as the dedup output
    is deterministic (which it is in our pipeline).

    Also filters out OVERFLOW pile polygons: when more sprites are added
    than fit on any page, the builder places the overflow at the
    (extrude, extrude) corner — they all stack at the same position. We
    detect this by grouping polygons by their integer (min_x, min_y) and
    dropping any group with 50+ members.
    """
    import struct as st
    from collections import defaultdict

    with open(blob_path, "rb") as f:
        data = f.read()
    idx = data.find(b"ATLS")
    if idx < 0:
        raise RuntimeError("ATLS magic not found")
    hdr = data[idx : idx + 28]
    _, _, region_count, page_count, _, voff, vcount, _, _ = st.unpack(
        "<IHHHHIIII", hdr
    )
    regions_off = idx + 28 + page_count * 8
    verts_off = idx + voff

    verts = []
    for v in range(vcount):
        vb = data[verts_off + v * 8 : verts_off + (v + 1) * 8]
        verts.append(st.unpack("<hhHH", vb))

    polys_by_vstart = {}
    bbox_by_vstart = {}
    for i in range(region_count):
        r = data[regions_off + i * 32 : regions_off + (i + 1) * 32]
        f = st.unpack("<QHHhhffHBBBBH", r)
        vstart = f[7]
        vc = f[8]
        page = f[9]
        if page != 0 or vc == 0 or vstart + vc > vcount:
            continue
        if vstart in polys_by_vstart:
            continue  # already recorded for this vstart
        poly = [
            (
                verts[vstart + j][2] * page_w / 65535.0,
                verts[vstart + j][3] * page_h / 65535.0,
            )
            for j in range(vc)
        ]
        polys_by_vstart[vstart] = poly
        xs = [p[0] for p in poly]
        ys = [p[1] for p in poly]
        bbox_by_vstart[vstart] = (int(min(xs)), int(min(ys)))

    # Filter overflow pile.
    pile = defaultdict(list)
    for vs, mn in bbox_by_vstart.items():
        pile[mn].append(vs)
    overflow_threshold = 50
    overflow_set = set()
    for mn, vstarts in pile.items():
        if len(vstarts) >= overflow_threshold:
            overflow_set.update(vstarts)
    if overflow_set:
        for vs in overflow_set:
            polys_by_vstart.pop(vs, None)
    return polys_by_vstart, len(overflow_set)


def polygon_opaque_count(img_alpha, page_w, page_h, poly):
    """Count opaque pixels strictly inside the polygon (even-odd rule)."""
    if not poly:
        return 0
    xs = [p[0] for p in poly]
    ys = [p[1] for p in poly]
    minx = max(0, int(min(xs)))
    maxx = min(page_w - 1, int(max(xs)) + 1)
    miny = max(0, int(min(ys)))
    maxy = min(page_h - 1, int(max(ys)) + 1)
    n = len(xs)
    cnt = 0
    for y in range(miny, maxy + 1):
        py = y + 0.5
        row_off = y * page_w
        for x in range(minx, maxx + 1):
            px = x + 0.5
            inside = False
            j = n - 1
            for k in range(n):
                if ((ys[k] > py) != (ys[j] > py)) and (
                    px
                    < (xs[j] - xs[k]) * (py - ys[k]) / (ys[j] - ys[k]) + xs[k]
                ):
                    inside = not inside
                j = k
            if inside and img_alpha[row_off + x] >= 1:
                cnt += 1
    return cnt


def cmd_record_regions(blob_path, page_path, baseline_dir):
    alpha, w, h, total_opaque = open_alpha(page_path)
    polys, overflow_dropped = parse_blob_polygons(blob_path, w, h)
    per_region = {}
    for vstart, poly in polys.items():
        per_region[str(vstart)] = polygon_opaque_count(alpha, w, h, poly)
    base = os.path.splitext(os.path.basename(page_path))[0]
    out_path = os.path.join(baseline_dir, f"{base}.regions.json")
    os.makedirs(os.path.dirname(os.path.abspath(out_path)), exist_ok=True)
    data = {
        "page": os.path.basename(page_path),
        "page_width": w,
        "page_height": h,
        "total_opaque": total_opaque,
        "region_count": len(per_region),
        "overflow_dropped": overflow_dropped,
        "per_region_opaque": per_region,
    }
    with open(out_path, "w", encoding="utf-8") as f:
        json.dump(data, f, indent=2)
    sum_per_region = sum(per_region.values())
    print(
        f"RECORDED per-region baseline: {len(per_region)} unique regions "
        f"(dropped {overflow_dropped} overflow), sum opaque {sum_per_region} "
        f"(total page opaque {total_opaque})"
    )
    print(f"  -> {out_path}")
    return 0


def cmd_check_regions(blob_path, page_path, baseline_dir):
    base = os.path.splitext(os.path.basename(page_path))[0]
    in_path = os.path.join(baseline_dir, f"{base}.regions.json")
    if not os.path.exists(in_path):
        print(f"ERROR: per-region baseline {in_path} not found.", file=sys.stderr)
        return 2
    with open(in_path, "r", encoding="utf-8") as f:
        meta = json.load(f)
    base_per_region = meta["per_region_opaque"]

    alpha, w, h, _total = open_alpha(page_path)
    polys, overflow_dropped = parse_blob_polygons(blob_path, w, h)

    failed = []
    checked = 0
    missing = 0
    for vstart, poly in polys.items():
        key = str(vstart)
        if key not in base_per_region:
            missing += 1
            continue
        base_cnt = base_per_region[key]
        new_cnt = polygon_opaque_count(alpha, w, h, poly)
        checked += 1
        if new_cnt < base_cnt:
            failed.append((key, base_cnt, new_cnt))

    print(
        f"checked {checked} regions against baseline "
        f"({missing} new/skipped, {overflow_dropped} overflow excluded)"
    )
    if failed:
        print(f"FAIL: {len(failed)} regions lost opaque pixels:")
        for key, b, n in failed[:10]:
            print(f"  region {key}: baseline {b} -> current {n} ({n - b:+d})")
        if len(failed) > 10:
            print(f"  ... and {len(failed) - 10} more")
        return 1
    print(f"PASS: all {checked} regions preserved opaque pixel counts")
    return 0


def cmd_check_sum(page_path, baseline_dir):
    """Sum-only check, allows position changes from re-packing."""
    _alpha_path, json_path = baseline_paths(baseline_dir, page_path)
    if not os.path.exists(json_path):
        print(
            f"ERROR: baseline {json_path} not found. Run with --record first.",
            file=sys.stderr,
        )
        return 2
    with open(json_path, "r", encoding="utf-8") as f:
        meta = json.load(f)
    base_opaque = int(meta["opaque_pixels"])
    _alpha, _w, _h, new_opaque = open_alpha(page_path)
    delta = new_opaque - base_opaque
    pct_delta = 100.0 * delta / base_opaque if base_opaque else 0.0
    print(
        f"baseline opaque = {base_opaque}, "
        f"current opaque = {new_opaque}, "
        f"delta = {delta:+d} ({pct_delta:+.4f}%)"
    )
    if new_opaque < base_opaque:
        print(
            f"FAIL: sum coverage regression — lost {-delta} opaque pixels "
            f"(net loss across the page; some sprite content clipped)"
        )
        return 1
    if new_opaque > base_opaque:
        print(
            f"WARN: opaque count INCREASED by {delta} pixels "
            f"(may indicate compose path change). Investigate."
        )
        return 0
    print("PASS: sum coverage matches baseline")
    return 0


def cmd_count(page_path):
    _alpha, w, h, opaque = open_alpha(page_path)
    pct = 100.0 * opaque / (w * h) if w * h else 0.0
    print(f"{opaque} opaque / {w*h} total ({pct:.2f}% fill)")
    return 0


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    cmd = argv[1]
    if cmd == "--record" and len(argv) == 4:
        return cmd_record(argv[2], argv[3])
    if cmd == "--check-px" and len(argv) == 4:
        return cmd_check_px(argv[2], argv[3])
    if cmd == "--check-sum" and len(argv) == 4:
        return cmd_check_sum(argv[2], argv[3])
    if cmd == "--record-regions" and len(argv) == 5:
        return cmd_record_regions(argv[2], argv[3], argv[4])
    if cmd == "--check-regions" and len(argv) == 5:
        return cmd_check_regions(argv[2], argv[3], argv[4])
    if cmd == "--count" and len(argv) == 3:
        return cmd_count(argv[2])
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
