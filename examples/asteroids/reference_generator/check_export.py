#!/usr/bin/env python3
"""Independently validate stored GLB bytes, subset ranges and source seed windows."""
import argparse
import hashlib
import json
import math
from pathlib import Path
import struct


class Mt19937:
    def __init__(self, seed):
        self.state = [seed]
        for i in range(1, 624):
            self.state.append((1812433253 * (self.state[-1] ^ (self.state[-1] >> 30)) + i) & 0xffffffff)
        self.index = 624

    def next(self):
        if self.index == 624:
            for i in range(624):
                y = (self.state[i] & 0x80000000) | (self.state[(i + 1) % 624] & 0x7fffffff)
                self.state[i] = self.state[(i + 397) % 624] ^ (y >> 1) ^ (0x9908b0df if y & 1 else 0)
            self.index = 0
        y = self.state[self.index]
        self.index += 1
        y ^= y >> 11
        y ^= (y << 7) & 0x9d2c5680
        y ^= (y << 15) & 0xefc60000
        return (y ^ (y >> 18)) & 0xffffffff


def dot(a, b):
    return sum(x * y for x, y in zip(a, b))


def check(output):
    manifest = json.loads((output / 'geometry.json').read_text())
    n = manifest['unique_count']
    wire = (output / 'geometry.bin').read_bytes()
    assert struct.unpack_from('<5I', wire) == (0x4f454741, 1, manifest['unique_count'], len(manifest['chunks']), len(manifest['subsets']))
    expected = bytearray(wire[:20])
    for c in manifest['chunks']:
        expected.extend(struct.pack('<5I', *(c[k] for k in ['subdivision', 'first_variant', 'variant_count', 'vertex_count', 'index_count'])))
    for s in manifest['subsets']:
        expected.extend(struct.pack('<6I2f', *(s[k] for k in ['chunk_index', 'first_index', 'index_count', 'first_vertex', 'vertex_count', 'random_seed', 'depth_min', 'depth_max'])))
    assert bytes(expected) == wire, 'AGEO metadata differs from JSON'
    assert manifest['schema_version'] == 1 and 0 < n <= 1000
    assert len(manifest['subsets']) == n * 4
    rng = Mt19937(manifest['seed'])
    for i, subset in enumerate(manifest['subsets']):
        assert subset['subdivision'] == i // n
        assert subset['variant'] == i % n
        assert subset['lod'] == 3 - i // n
        assert subset['random_seed'] == rng.next(), ('seed window mismatch', i)
    seen = set()
    signatures = [set() for _ in range(4)]
    aggregate = hashlib.sha256()
    triangles = 0
    for chunk_index, chunk in enumerate(manifest['chunks']):
        data = (output / chunk['file']).read_bytes()
        aggregate.update(data)
        magic, version, length, json_length, json_type = struct.unpack_from('<5I', data)
        assert (magic, version, length, json_type) == (0x46546c67, 2, len(data), 0x4e4f534a)
        document = json.loads(data[20:20 + json_length])
        binary_length, binary_type = struct.unpack_from('<2I', data, 20 + json_length)
        binary = memoryview(data)[28 + json_length:]
        assert binary_type == 0x004e4942 and binary_length == len(binary)
        assert len(document['meshes']) == 1 and document['meshes'][0]['name'] == chunk['name']
        primitive = document['meshes'][0]['primitives'][0]
        assert primitive['mode'] == 4

        def accessor(index, component_type, components):
            a = document['accessors'][index]
            v = document['bufferViews'][a['bufferView']]
            assert a['componentType'] == component_type
            assert a['type'] == {1: 'SCALAR', 2: 'VEC2', 3: 'VEC3'}[components]
            fmt = '<' + ('f' if component_type == 5126 else 'H') * components
            start = v.get('byteOffset', 0) + a.get('byteOffset', 0)
            end = start + a['count'] * struct.calcsize(fmt)
            assert end <= v.get('byteOffset', 0) + v['byteLength'] <= len(binary)
            return list(struct.iter_unpack(fmt, binary[start:end]))

        positions = accessor(primitive['attributes']['POSITION'], 5126, 3)
        normals = accessor(primitive['attributes']['NORMAL'], 5126, 3)
        uvs = accessor(primitive['attributes']['TEXCOORD_0'], 5126, 2)
        indices = [i[0] for i in accessor(primitive['indices'], 5123, 1)]
        assert len(positions) == len(normals) == len(uvs) == chunk['vertex_count'] <= 65535
        assert len(indices) == chunk['index_count'] <= 196608
        assert all(math.isfinite(c) for stream in [positions, normals, uvs] for vertex in stream for c in vertex)
        assert all(abs(dot(normal, normal) - 1.0) < 0.0002 for normal in normals)
        assert all(dot(position, position) <= 0.9001 ** 2 for position in positions)
        assert all(uv == (0.0, 0.0) for uv in uvs)
        bounds = document['accessors'][primitive['attributes']['POSITION']]
        for axis in range(3):
            assert abs(bounds['min'][axis] - min(p[axis] for p in positions)) < 1e-8
            assert abs(bounds['max'][axis] - max(p[axis] for p in positions)) < 1e-8
        members = [s for s in manifest['subsets'] if s['chunk_index'] == chunk_index]
        assert len(members) == chunk['variant_count']
        first_vertex = first_index = 0
        subdivision = chunk['subdivision']
        for s in members:
            assert s['first_vertex'] == first_vertex and s['first_index'] == first_index
            assert s['vertex_count'] == 10 * 4 ** subdivision + 2
            assert s['index_count'] == 60 * 4 ** subdivision
            assert s['subdivision'] == subdivision
            key = (subdivision, s['variant'])
            assert key not in seen
            seen.add(key)
            end_vertex = first_vertex + s['vertex_count']
            end_index = first_index + s['index_count']
            assert all(first_vertex <= index < end_vertex for index in indices[first_index:end_index])
            radii = [math.sqrt(dot(p, p)) for p in positions[first_vertex:end_vertex]]
            assert abs(min(radii) - s['depth_min']) < 1e-6
            assert abs(max(radii) - s['depth_max']) < 1e-6
            assert abs(min(radii) - .525) < 1e-4 and abs(max(radii) - .9) < 1e-4
            signature = hashlib.sha256(b''.join(struct.pack('<3f', *p) for p in positions[first_vertex:end_vertex])).digest()
            assert signature not in signatures[subdivision], ('duplicate shape', key)
            signatures[subdivision].add(signature)
            first_vertex, first_index = end_vertex, end_index
        assert first_vertex == len(positions) and first_index == len(indices)
        accumulated = [[0.0, 0.0, 0.0] for _ in positions]
        for i in range(0, len(indices), 3):
            a, b, c = indices[i:i + 3]
            p = positions[a]
            u = tuple(x - y for x, y in zip(positions[b], p))
            v = tuple(x - y for x, y in zip(positions[c], p))
            cross = (u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0])
            assert dot(cross, p) > 0.0
            for index in (a, b, c):
                for axis in range(3):
                    accumulated[index][axis] += cross[axis]
            triangles += 1
        for got, expected in zip(normals, accumulated):
            length = math.sqrt(dot(expected, expected))
            assert all(abs(g - e / length) < 1e-5 for g, e in zip(got, expected)), 'normal is not area-weighted source normal'
    assert len(seen) == 4 * n
    environment = (output / 'environment.glb').read_bytes()
    magic, version, length, json_length, json_type = struct.unpack_from('<5I', environment)
    assert (magic, version, length, json_type) == (0x46546c67, 2, len(environment), 0x4e4f534a)
    document = json.loads(environment[20:20 + json_length])
    binary = memoryview(environment)[28 + json_length:]
    assert [mesh['name'] for mesh in document['meshes']] == ['planet', 'skybox']
    for mesh_index, counts in enumerate([(1056, 5952), (24, 36)]):
        primitive = document['meshes'][mesh_index]['primitives'][0]
        positions = accessor(primitive['attributes']['POSITION'], 5126, 3)
        normals = accessor(primitive['attributes']['NORMAL'], 5126, 3)
        uvs = accessor(primitive['attributes']['TEXCOORD_0'], 5126, 2)
        indices = [i[0] for i in accessor(primitive['indices'], 5123, 1)]
        assert (len(positions), len(indices)) == counts
        assert len(positions) == len(normals) == len(uvs)
        assert all(0 <= index < counts[0] for index in indices)
        if mesh_index == 0:
            assert normals == positions
            assert all(abs(dot(position, position) - 1.0) < 1e-6 for position in positions)
            f32 = lambda f: struct.unpack('<f', struct.pack('<f', f))[0]
            assert uvs == [(f32(column / 32), f32(f32(1 / 33) * row)) for row in range(32) for column in range(33)]
        else:
            assert all(abs(value) == 0.5 for position in positions for value in position)
            assert all(normal == (0.0, 0.0, 0.0) for normal in normals)
            assert all(uv == (0.0, 0.0) for uv in uvs)
    print(f'PASS: stored GLBs, ranges, source seeds, unique shapes, bounds, winding and area-weighted normals; {triangles:,} triangles; aggregate SHA-256 {aggregate.hexdigest()}')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    check(args.output)
