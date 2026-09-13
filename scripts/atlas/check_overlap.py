#!/usr/bin/env python3
"""Verify polygon overlap in atlas blob.

Reads NtAtlas blob + a page PNG, rasterizes each unique polygon hull
(deduped by page UV ring) into a grid, counts overlapping cells.

Usage: python scripts/atlas/check_overlap.py <blob.ntpack> <page0.png>
Returns exit 0 if no overlap, exit 1 if overlap detected.
"""
import struct
import sys
from PIL import Image


def main(blob_path, page_path):
    with open(blob_path, "rb") as f:
        data = f.read()
    idx = data.find(b"ATLS")
    if idx < 0:
        print("ERROR: ATLS magic not found in blob")
        sys.exit(2)
    hdr = data[idx : idx + 32]
    _, version, region_count, page_count, _, voff, vcount, _, _, _ = struct.unpack(
        "<IHHHHIIIIf", hdr
    )
    if version != 8:
        print(f"ERROR: atlas version {version}, expected 8; rebuild the pack")
        sys.exit(2)
    regions_off = idx + 32 + page_count * 8
    uvs_off = idx + voff + vcount * 8

    img = Image.open(page_path)
    page_w, page_h = img.size

    uvs = []
    for v in range(vcount):
        uv = data[uvs_off + v * 4 : uvs_off + (v + 1) * 4]
        uvs.append(struct.unpack("<HH", uv))

    # Read the geometry fields and skip flags, slice9 borders and reserved bytes.
    REGION_FMT = "<QHHhhffIIBBBB12x"
    REGION_SIZE = struct.calcsize(REGION_FMT)
    assert REGION_SIZE == 48, f"NtAtlasRegion must be 48 bytes, got {REGION_SIZE}"

    # Aliases can share a placement without sharing serialized geometry.
    seen_rings = set()
    unique_polys = []
    for i in range(region_count):
        r = data[regions_off + i * REGION_SIZE : regions_off + (i + 1) * REGION_SIZE]
        fields = struct.unpack(REGION_FMT, r)
        vstart, vertex_count, page_index = fields[7], fields[9], fields[10]
        if page_index != 0 or vertex_count == 0:
            continue
        ring = tuple(uvs[vstart : vstart + vertex_count])
        canonical = min(
            ordered[j:] + ordered[:j]
            for ordered in (ring, ring[::-1])
            for j in range(len(ring))
        )
        if canonical in seen_rings:
            continue
        seen_rings.add(canonical)
        poly = [(u * page_w / 65535.0, v * page_h / 65535.0) for u, v in ring]
        unique_polys.append((vstart, poly))

    print(f"unique polygons on page 0: {len(unique_polys)}")

    grid = 2048
    sx, sy = grid / page_w, grid / page_h
    owner = [-1] * (grid * grid)
    overlap_pairs = {}

    for vs, poly in unique_polys:
        if not poly:
            continue
        xs = [p[0] * sx for p in poly]
        ys = [p[1] * sy for p in poly]
        minx = max(0, int(min(xs)))
        maxx = min(grid - 1, int(max(xs)) + 1)
        miny = max(0, int(min(ys)))
        maxy = min(grid - 1, int(max(ys)) + 1)
        n = len(xs)
        for y in range(miny, maxy + 1):
            py = y + 0.5
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
                if inside:
                    cell = y * grid + x
                    if owner[cell] >= 0 and owner[cell] != vs:
                        pair = (min(owner[cell], vs), max(owner[cell], vs))
                        overlap_pairs[pair] = overlap_pairs.get(pair, 0) + 1
                    else:
                        owner[cell] = vs

    pairs = len(overlap_pairs)
    cells = sum(overlap_pairs.values())
    print(f"overlap pairs: {pairs}, cells: {cells}")
    if pairs > 0:
        print("FAIL: polygon overlap detected (NFP packing bug)")
        for (a, b), cnt in sorted(overlap_pairs.items(), key=lambda x: -x[1])[:5]:
            print(f"  vstart {a} <-> vstart {b}: {cnt} cells")
        sys.exit(1)
    print("PASS: no polygon overlap")
    sys.exit(0)


if __name__ == "__main__":
    if len(sys.argv) != 3:
        print(__doc__)
        sys.exit(2)
    main(sys.argv[1], sys.argv[2])
