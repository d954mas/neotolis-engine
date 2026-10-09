#!/usr/bin/env python3
"""Check all exported instance bytes, JSON round trips and original texture layers."""
import argparse
import hashlib
import json
import math
from pathlib import Path
import struct
import zlib


PRESETS = [(1000, 35, 10, .6), (2000, 50, 10, .5), (3000, 75, 20, .45),
           (4000, 100, 20, .4), (5000, 200, 30, .33), (10000, 300, 30, .3),
           (15000, 400, 40, .27), (20000, 500, 40, .23),
           (35000, 750, 50, .2), (50000, 1000, 50, .17)]


def f32(value):
    return struct.unpack('<f', struct.pack('<f', value))[0]


def decode_png(path):
    data = path.read_bytes()
    assert data[:8] == b'\x89PNG\r\n\x1a\n'
    offset, packed = 8, bytearray()
    while offset < len(data):
        length, = struct.unpack_from('>I', data, offset)
        kind = data[offset + 4:offset + 8]
        chunk = data[offset + 8:offset + 8 + length]
        crc, = struct.unpack_from('>I', data, offset + 8 + length)
        assert zlib.crc32(kind + chunk) & 0xffffffff == crc
        if kind == b'IHDR':
            assert struct.unpack('>IIBBBBB', chunk) == (256, 256, 8, 6, 0, 0, 0)
        elif kind == b'IDAT':
            packed.extend(chunk)
        offset += 12 + length
    filtered = zlib.decompress(packed)
    assert len(filtered) == 256 * 1025
    image = bytearray(256 * 1024)
    for row in range(256):
        mode = filtered[row * 1025]
        assert 0 <= mode <= 4
        for col in range(1024):
            index = row * 1024 + col
            a = image[index - 4] if col >= 4 else 0
            b = image[index - 1024] if row else 0
            c = image[index - 1028] if row and col >= 4 else 0
            if mode == 0:
                prediction = 0
            elif mode == 1:
                prediction = a
            elif mode == 2:
                prediction = b
            elif mode == 3:
                prediction = (a + b) // 2
            else:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                prediction = a if pa <= pb and pa <= pc else b if pb <= pc else c
            image[index] = (filtered[row * 1025 + 1 + col] + prediction) & 255
    return image


def check(root):
    aggregate = hashlib.sha256()
    total_instances = 0
    for level, (count, meshes, textures, ratio) in enumerate(PRESETS):
        stem = root / f'scene_level_{level}'
        binary = stem.with_suffix('.bin').read_bytes()
        data = json.loads(stem.with_suffix('.json').read_text())
        header = struct.unpack_from('<8s10I3fI', binary)
        assert header[:11] == (b'ASTSCN01', 1, 64, 132, level, 1123, count, meshes, 4, textures, meshes * 4)
        assert header[11:] == (15.0, f32(f32(ratio) / 10.0), f32(ratio), 0)
        assert len(binary) == 64 + count * 132 + meshes * 4 * 4
        assert len(data['instances']) == count
        indices = struct.unpack_from(f'<{meshes * 4}I', binary, 64 + count * 132)
        assert tuple(data['mesh_subset_texture_indices']) == indices
        assert all(0 <= index < textures for index in indices)
        runtime = (root / f'scene_level_{level}.instances.bin').read_bytes()
        assert struct.unpack_from('<4I', runtime) == (0x49545341, 1, level, count)
        assert len(runtime) == 16 + 128 * count
        for index, instance in enumerate(data['instances']):
            values = [instance['index'], instance['mesh_instance_index'], instance['texture_index'],
                      *instance['deep_color'], *instance['shallow_color'], *instance['scale_translate_matrix'],
                      *instance['spin_axis'], instance['scale'], instance['orbit_speed'], instance['spin_speed'],
                      instance['spin_angle_rad'], instance['orbit_angle_rad']]
            assert struct.pack('<3I30f', *values) == binary[64 + index * 132:64 + (index + 1) * 132]
            runtime_values = [*instance['scale_translate_matrix'], *instance['spin_axis'], instance['scale'],
                              *instance['deep_color'], instance['spin_speed'], *instance['shallow_color'],
                              instance['orbit_speed'], instance['spin_angle_rad'], instance['orbit_angle_rad'],
                              instance['mesh_instance_index'], instance['texture_index']]
            assert struct.pack('<30f2I', *runtime_values) == runtime[16 + index * 128:16 + (index + 1) * 128]
            assert instance['index'] == index and 0 <= instance['mesh_instance_index'] < meshes
            assert 0 <= instance['texture_index'] < textures
            assert all(math.isfinite(value) for value in values)
            assert all(0 <= color <= 1 for color in instance['deep_color'] + instance['shallow_color'])
            assert abs(sum(c * c for c in instance['spin_axis']) - 1) < .00001
            scale = instance['scale']
            assert ratio * 1.5 - .0001 <= scale <= ratio * 15 + .0001
            matrix = instance['scale_translate_matrix']
            for offset in [0, 5, 10]:
                assert .8 * scale - .0001 <= matrix[offset] <= 1.2 * scale + .0001
            assert all(matrix[offset] == 0 for offset in [1, 2, 3, 4, 6, 7, 8, 9, 11, 14])
            assert matrix[15] == 1
            assert abs(instance['spin_speed'] * scale) <= 1.70001
            assert 1.4999 <= instance['orbit_speed'] * scale * matrix[12] <= 5.0001
        aggregate.update(binary)
        aggregate.update(runtime)
        total_instances += count
    manifest = json.loads((root / 'textures/textures.json').read_text())
    assert manifest['dimensions'] == [256, 256] and manifest['layers'] == 3
    assert len(manifest['textures']) == 50
    unique = set()
    for texture in manifest['textures']:
        layers = []
        for layer in texture['layers']:
            raw = (root / 'textures' / layer['raw']).read_bytes()
            assert len(raw) == 256 * 256 * 4
            assert raw == decode_png(root / 'textures' / layer['png'])
            assert raw[0::4] == raw[1::4] == raw[2::4]
            assert raw[3::4] == b'\xff' * (256 * 256)
            assert min(raw[0::4]) == 0 and max(raw[0::4]) >= 254
            layers.append(raw)
            aggregate.update(raw)
        assert layers[0] == layers[1] == layers[2]
        unique.add(hashlib.sha256(layers[0]).digest())
    assert len(unique) == 50
    for level in range(10):
        data = json.loads((root / f'scene_level_{level}.json').read_text())
        assert data['texture_parameters'] == [t['parameters'] for t in manifest['textures'][:data['textures_count']]]
    print(f'PASS: {total_instances:,} exact binary32 JSON records, both binary ABIs, 150 raw/PNG layer pairs, 50 distinct textures; aggregate SHA-256 {aggregate.hexdigest()}')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output', type=Path)
    check(parser.parse_args().output)
