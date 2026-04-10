#!/usr/bin/env python3
"""Verify polygon overlap in atlas blob.

Reads NtAtlas blob + a page PNG, rasterizes each unique polygon hull
(deduped by vertex_start) into a grid, counts overlapping cells.

Usage: python scripts/atlas/check_overlap.py <blob.ntpack> <page0.png>
Returns exit 0 if no overlap, exit 1 if overlap detected.
"""
import struct
import sys
from collections import defaultdict
from PIL import Image


def main(blob_path, page_path):
    with open(blob_path, "rb") as f:
        data = f.read()
    idx = data.find(b"ATLS")
    if idx < 0:
        print("ERROR: ATLS magic not found in blob")
        sys.exit(2)
    hdr = data[idx : idx + 28]
    _, _, region_count, page_count, _, voff, vcount, _, _ = struct.unpack(
        "<IHHHHIIII", hdr
    )
    regions_off = idx + 28 + page_count * 8
    verts_off = idx + voff

    img = Image.open(page_path)
    page_w, page_h = img.size

    verts = []
    for v in range(vcount):
        vb = data[verts_off + v * 8 : verts_off + (v + 1) * 8]
        verts.append(struct.unpack("<hhHH", vb))

    # NtAtlasRegion v3 layout (36 bytes, shared/include/nt_atlas_format.h):
    #   name_hash     Q   @0
    #   source_w      H   @8
    #   source_h      H   @10
    #   trim_offset_x h   @12
    #   trim_offset_y h   @14
    #   origin_x      f   @16
    #   origin_y      f   @20
    #   vertex_start  I   @24   (u32 in v3, was u16 in v2)
    #   index_start   I   @28   (u32 in v3, was u16 in v2)
    #   vertex_count  B   @32
    #   page_index    B   @33
    #   transform     B   @34
    #   index_count   B   @35
    REGION_FMT = "<QHHhhffIIBBBB"
    REGION_SIZE = struct.calcsize(REGION_FMT)
    assert REGION_SIZE == 36, f"NtAtlasRegion v3 must be 36 bytes, got {REGION_SIZE}"

    # Group regions by vertex_start (duplicates share storage).
    vstart_groups = defaultdict(list)
    for i in range(region_count):
        r = data[regions_off + i * REGION_SIZE : regions_off + (i + 1) * REGION_SIZE]
        f = struct.unpack(REGION_FMT, r)
        vstart = f[7]
        vstart_groups[vstart].append(f)

    # One representative polygon per unique vertex_start.
    unique_polys = []
    for vstart, items in vstart_groups.items():
        f = items[0]
        vertex_count = f[9]
        page_index = f[10]
        if page_index != 0:  # only page 0 supported for overlap test
            continue
        poly = [
            (
                verts[vstart + j][2] * page_w / 65535.0,
                verts[vstart + j][3] * page_h / 65535.0,
            )
            for j in range(vertex_count)
        ]
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
