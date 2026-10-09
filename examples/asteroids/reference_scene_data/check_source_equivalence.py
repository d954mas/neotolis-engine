#!/usr/bin/env python3
"""Compile literal upstream scene/noise functions with serial storage shims and compare every exported byte."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import struct


HERE = Path(__file__).resolve().parent


def definition(text, start, *, structure=False):
    begin = text.index(start)
    opening = text.index('{', begin)
    depth = 1
    end = opening + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[begin:end] + (';' if structure else '')


def main(args):
    sources = json.loads((HERE / 'upstream/sources.json').read_text())
    for source in sources['sources']:
        data = (HERE / 'upstream' / source['file']).read_bytes()
        blob = hashlib.sha1(b'blob ' + str(len(data)).encode() + b'\0' + data).hexdigest()
        assert blob == source['git_blob_sha1'], source['file']
    array = (HERE / 'upstream/Modules/Simulation/AsteroidsArray.cpp').read_text()
    asteroid = (HERE / 'upstream/Modules/Simulation/Asteroid.cpp').read_text()
    header = (HERE / 'upstream/Modules/Simulation/Asteroid.h').read_text()
    app = (HERE / 'upstream/App/AsteroidsApp.cpp').read_text()
    code = r'''
#include "scene_format.h"
#include <FastNoise/FastNoise.h>
#include <hlsl++.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <limits>
#include <numbers>
#include <random>
#include <stdexcept>
#include <vector>
#define META_FUNCTION_TASK(...)
#define META_SCOPE_TIMER(...)
#define META_CHECK_LESS(...)
#define META_CHECK_NOT_ZERO(...)
#define META_CHECK_GREATER_OR_EQUAL(...)
namespace Data { using Bytes = std::vector<std::byte>; }
namespace gfx {
struct Color3F {
    static constexpr size_t Size = 3;
    std::array<float, 3> c{};
    Color3F() = default;
    Color3F(uint8_t r, uint8_t g, uint8_t b) : c{float(r) / 255.F, float(g) / 255.F, float(b) / 255.F} {}
    float operator[](size_t i) const { return c[i]; }
    void Set(size_t i, float x) { c[i] = x; }
};
struct Dimensions {
    uint32_t width = 256, height = 256;
    uint32_t GetWidth() const { return width; }
    uint32_t GetHeight() const { return height; }
    uint32_t GetPixelsCount() const { return width * height; }
};
enum class PixelFormat { RGBA8Unorm };
uint32_t GetPixelSize(PixelFormat) { return 4; }
}
namespace rhi {
struct SubResource {
    struct Index { uint32_t mip, layer; };
    Data::Bytes data;
    SubResource(Data::Bytes &&bytes, Index) : data(std::move(bytes)) {}
};
using SubResources = std::vector<SubResource>;
}
namespace tf {
struct Taskflow {
    template <class It, class Fn> void for_each(It begin, It end, Fn fn) {
        for (; begin != end; ++begin) fn(*begin);
    }
};
struct Executor {
    struct Future { void get() {} };
    Future run(Taskflow &) { return {}; }
};
}
struct Asteroid {
'''
    for name in ['Colors', 'Parameters', 'TextureNoiseParameters']:
        code += definition(header, 'struct ' + name, structure=True) + '\n'
    code += r'''
    static constexpr size_t color_schema_size = 6U;
    static Colors GetAsteroidIceColors(uint32_t, uint32_t);
    static Colors GetAsteroidRockColors(uint32_t, uint32_t);
    static rhi::SubResources GenerateTextureArraySubResources(const gfx::Dimensions &, uint32_t, const TextureNoiseParameters &);
    static void FillPerlinNoiseToTexture(Data::Bytes &, const gfx::Dimensions &, uint32_t, const TextureNoiseParameters &);
};
using AsteroidColorSchema = std::array<gfx::Color3F, Asteroid::color_schema_size>;
'''
    starts = ['static gfx::Color3F TransformSrgbToLinear(',
              'static AsteroidColorSchema TransformSrgbToLinear(',
              'Asteroid::Colors Asteroid::GetAsteroidRockColors(',
              'Asteroid::Colors Asteroid::GetAsteroidIceColors(',
              'rhi::SubResources Asteroid::GenerateTextureArraySubResources(',
              'void Asteroid::FillPerlinNoiseToTexture(']
    for start in starts:
        code += definition(asteroid, start) + '\n'
    code += definition(array, 'static hlslpp::float3 GetRandomDirection(') + '\n'
    code += definition(app, 'struct MutableParameters', structure=True) + '\n'
    code += 'constexpr uint32_t g_max_complexity = 9;\n'
    code += definition(app, 'static const std::array<MutableParameters', structure=True) + '\n'
    code += r'''
struct AsteroidsArray {
    struct Settings {
        float scale = 15.F;
        uint32_t instance_count, unique_mesh_count, subdivisions_count = 4, textures_count;
        gfx::Dimensions texture_dimensions;
        uint32_t random_seed = 1123;
        float orbit_radius_ratio = 13.F, disc_radius_ratio = 4.F;
        float min_asteroid_scale_ratio, max_asteroid_scale_ratio;
        bool textures_array_enabled = true;
    };
    struct ContentState {
        struct UberMesh { UberMesh(tf::Executor &, uint32_t, uint32_t, uint32_t) {} } uber_mesh;
        std::vector<rhi::SubResources> texture_array_subresources;
        std::vector<uint32_t> mesh_subset_texture_indices;
        std::vector<Asteroid::Parameters> parameters;
        ContentState(tf::Executor &, const Settings &);
    };
};
'''
    code += definition(array, 'AsteroidsArray::ContentState::ContentState(') + '\n'
    update = definition(array, 'void AsteroidsArray::UpdateAsteroidUniforms(')
    math_operations = update[update.index('    const float spin_angle_rad'):update.index('    const uint32_t mesh_subset_index')]
    code += r"""
void write_math_vectors(const std::filesystem::path &root, const std::filesystem::path &destination) {
    std::ofstream out(destination);
    out.exceptions(std::ios::badbit | std::ios::failbit);
    out << std::setprecision(std::numeric_limits<float>::max_digits10);
    out << "{\"samples\":[\n";
    const uint32_t levels[] = {1U, 8U};
    const uint32_t selected[2][8] = {{0U, 1U, 17U, 94U, 137U, 292U, 1733U, 1999U},
                                    {0U, 19U, 137U, 997U, 4093U, 20996U, 30535U, 34999U}};
    const double times[] = {0.0, 1.0, 10.0};
    const hlslpp::float3 eye_position(-110.F, 75.F, 210.F);
    struct { uint32_t subdivisions_count = 4U; } m_settings;
    const float m_min_mesh_lod_screen_size_log_2 = std::log2(0.06F);
    uint32_t sample_index = 0;
    for (uint32_t level_index = 0; level_index < 2U; ++level_index) {
        std::ifstream input(root / ("scene_level_" + std::to_string(levels[level_index]) + ".instances.bin"), std::ios::binary);
        input.exceptions(std::ios::badbit | std::ios::failbit);
        for (uint32_t selection = 0; selection < 8U; ++selection) {
            const uint32_t index = selected[level_index][selection];
            ast_reference_runtime_instance instance{};
            input.seekg(sizeof(ast_reference_runtime_header) + index * sizeof(instance));
            input.read(reinterpret_cast<char *>(&instance), sizeof(instance));
            hlslpp::float4x4 base;
            hlslpp::float3 axis;
            hlslpp::load(base, instance.scale_translate);
            hlslpp::load(axis, instance.spin_axis);
            const Asteroid::Parameters asteroid_parameters{
                .index = index, .mesh_instance_index = instance.mesh_index, .texture_index = instance.texture_index,
                .colors = {}, .scale_translate_matrix = base, .spin_axis = axis, .scale = instance.scale,
                .orbit_speed = instance.orbit_speed, .spin_speed = instance.spin_speed,
                .spin_angle_rad = instance.spin_angle, .orbit_angle_rad = instance.orbit_angle
            };
            for (double elapsed_seconds : times) {
                const float elapsed_radians = static_cast<float>(std::numbers::pi * elapsed_seconds);
"""
    code += math_operations
    code += r"""
                float model_row_major[16], uploaded_row_major[16];
                hlslpp::store(model_row_major, model_matrix);
                hlslpp::store(uploaded_row_major, hlslpp::transpose(model_matrix));
                if (sample_index++) out << ",\n";
                out << "{\"input_index\":" << level_index * 8U + selection
                    << ",\"level\":" << levels[level_index] << ",\"instance_index\":" << index
                    << ",\"elapsed_seconds\":" << elapsed_seconds << ",\"elapsed_radians\":" << elapsed_radians
                    << ",\"distance_to_eye\":" << distance_to_eye
                    << ",\"source_subdivision\":" << mesh_subdivision_index
                    << ",\"model_row_major\":[";
                for (uint32_t component = 0; component < 16; ++component) {
                    if (component) out << ',';
                    out << model_row_major[component];
                }
                out << "],\"uploaded_transpose_row_major\":[";
                for (uint32_t component = 0; component < 16; ++component) {
                    if (component) out << ',';
                    out << uploaded_row_major[component];
                }
                out << "]}";
            }
        }
    }
    out << "\n]}\n";
    out.close();
}
"""
    code += r'''
int main(int argc, char **argv) {
    if (argc != 2 && argc != 3) return 2;
    const std::filesystem::path root(argv[1]);
    if (argc == 3) { write_math_vectors(root, argv[2]); return 0; }
    size_t instances_checked = 0, layers_checked = 0;
    tf::Executor executor;
    for (uint32_t level = 0; level <= g_max_complexity; ++level) {
        const auto &preset = g_mutable_parameters[level];
        AsteroidsArray::Settings settings{};
        settings.instance_count = preset.instances_count;
        settings.unique_mesh_count = preset.unique_mesh_count;
        settings.textures_count = preset.textures_count;
        settings.min_asteroid_scale_ratio = preset.scale_ratio / 10.F;
        settings.max_asteroid_scale_ratio = preset.scale_ratio;
        AsteroidsArray::ContentState original(executor, settings);
        std::ifstream file(root / ("scene_level_" + std::to_string(level) + ".instances.bin"), std::ios::binary);
        file.exceptions(std::ios::failbit | std::ios::badbit);
        ast_reference_runtime_header h{};
        file.read(reinterpret_cast<char *>(&h), sizeof(h));
        for (const auto &p : original.parameters) {
            ast_reference_runtime_instance expected{}, actual{};
            hlslpp::store(expected.scale_translate, p.scale_translate_matrix);
            hlslpp::store(expected.spin_axis, p.spin_axis);
            expected.scale = p.scale;
            std::copy_n(p.colors.deep.c.data(), 3, expected.deep);
            expected.spin_speed = p.spin_speed;
            std::copy_n(p.colors.shallow.c.data(), 3, expected.shallow);
            expected.orbit_speed = p.orbit_speed;
            expected.spin_angle = p.spin_angle_rad;
            expected.orbit_angle = p.orbit_angle_rad;
            expected.mesh_index = p.mesh_instance_index;
            expected.texture_index = p.texture_index;
            file.read(reinterpret_cast<char *>(&actual), sizeof(actual));
            if (std::memcmp(&expected, &actual, sizeof(actual))) {
                std::cerr << "source mismatch: level=" << level << " instance=" << p.index << '\n';
                return 1;
            }
            ++instances_checked;
        }
        std::ifstream source_file(root / ("scene_level_" + std::to_string(level) + ".bin"), std::ios::binary);
        source_file.exceptions(std::ios::failbit | std::ios::badbit);
        source_file.seekg(64 + 132 * settings.instance_count);
        std::vector<uint32_t> indices(settings.unique_mesh_count * settings.subdivisions_count);
        source_file.read(reinterpret_cast<char *>(indices.data()), indices.size() * sizeof(uint32_t));
        if (indices != original.mesh_subset_texture_indices) throw std::runtime_error("source subset texture mismatch");
        for (uint32_t texture = 0; texture < settings.textures_count; ++texture) {
            for (uint32_t layer = 0; layer < 3; ++layer) {
                std::ifstream image(root / "textures" / ("texture_" + std::to_string(texture) + "_layer_" + std::to_string(layer) + ".rgba"), std::ios::binary);
                image.exceptions(std::ios::failbit | std::ios::badbit);
                const auto &expected = original.texture_array_subresources[texture][layer].data;
                Data::Bytes actual(expected.size());
                image.read(reinterpret_cast<char *>(actual.data()), actual.size());
                if (expected != actual) throw std::runtime_error("source texture mismatch");
                ++layers_checked;
            }
        }
    }
    std::cout << "PASS: literal upstream functions match " << instances_checked << " instances, all subset indices, and " << layers_checked << " per-level texture layers byte-for-byte\n";
}
'''
    args.work.mkdir(parents=True, exist_ok=True)
    cpp = args.work / 'source_equivalence.cpp'
    cpp.write_text(code)
    binary = args.work / 'source_equivalence'
    command = [args.compiler, '-std=c++20', '-O3', '-DNDEBUG',
               '-DHLSLPP_FEATURE_TRANSFORM', '-DHLSLPP_LOGICAL_LAYOUT=0', '-DHLSLPP_COORDINATES=0',
               '-DFASTNOISE_STATIC_LIB', '-DFASTSIMD_IS_RELAXED=1', '-DFASTSIMD_STATIC_LIB',
               '-I' + str(HERE), '-I' + str(args.dependencies / 'HLSLpp/include'),
               '-I' + str(args.dependencies / 'FastNoise2/include'),
               '-I' + str(args.dependencies / 'FastSIMD/include'),
               '-I' + str(args.generator_build / 'fastsimd/FastSIMD_FastNoise/include'),
               str(cpp), str(args.generator_build / 'libFastNoise.a'), '-o', str(binary)]
    subprocess.run(command, check=True)
    if args.math_output:
        raw_math = args.work / 'runtime_math_raw.json'
        subprocess.run([str(binary), str(args.output), str(raw_math)], check=True)
        write_math_outputs(args, json.loads(raw_math.read_text()))
    else:
        subprocess.run([str(binary), str(args.output)], check=True)


def write_math_outputs(args, data):
    def f(value):
        value = struct.unpack('<f', struct.pack('<f', value))[0]
        text = format(value, '.9g')
        return text + ('F' if '.' in text or 'e' in text else '.0F')

    def array(values):
        return '{' + ', '.join(f(value) for value in values) + '}'

    inputs = []
    input_hashes = {}
    for sample in data['samples']:
        if sample['input_index'] < len(inputs):
            continue
        assert sample['input_index'] == len(inputs)
        level, index = sample['level'], sample['instance_index']
        path = args.output / f'scene_level_{level}.instances.bin'
        raw = path.read_bytes()
        input_hashes[str(level)] = hashlib.sha256(raw).hexdigest()
        values = struct.unpack_from('<30f2I', raw, 16 + 128 * index)
        instance = dict(scale_translate=values[:16], spin_axis=values[16:19], scale=values[19],
                        deep=values[20:23], spin_speed=values[23], shallow=values[24:27],
                        orbit_speed=values[27], spin_angle=values[28], orbit_angle=values[29],
                        mesh_index=values[30], texture_index=values[31])
        inputs.append(dict(level=level, instance_index=index, instance=instance))
    provenance = dict(
        upstream_commit='16a5751e835dd0776d976e51438604dc8de27d16',
        source_function='AsteroidsArray::UpdateAsteroidUniforms',
        source_url='https://github.com/MethanePowered/MethaneAsteroids/blob/16a5751e835dd0776d976e51438604dc8de27d16/Modules/Simulation/AsteroidsArray.cpp',
        hlslpp_commit='3a5b1cf0d807f945ec861201b316c425e9cf5061',
        method='Literal source transform/LOD operations extracted from verified source snapshots; pinned HLSL++ SSE2; existing 128-byte binary inputs',
        input_sha256=input_hashes)
    data = dict(schema_version=1, provenance=provenance, camera_eye=[-110, 75, 210],
                min_lod_screen_size=0.06, subdivisions_count=4,
                matrix_convention='Both arrays are row-major: model_row_major is before the source uniform transpose; uploaded_transpose_row_major is after it',
                inputs=inputs, samples=data['samples'])
    args.math_output.mkdir(parents=True, exist_ok=True)
    (args.math_output / 'runtime_math_vectors.json').write_text(json.dumps(data, indent=2) + '\n')
    lines = [
        '/* Generated by check_source_equivalence.py --math-output using pinned Methane/HLSL++.',
        ' * Original algorithms: Copyright 2019-2020 Evgeny Gorodetskiy, Apache-2.0.',
        ' * See README.md and runtime_math_vectors.json for source and binary provenance. */',
        '#ifndef ASTEROIDS_REFERENCE_RUNTIME_MATH_VECTORS_H',
        '#define ASTEROIDS_REFERENCE_RUNTIME_MATH_VECTORS_H',
        '#include "scene_format.h"',
        'typedef struct ast_reference_math_input {',
        '    uint32_t complexity, instance_index;',
        '    ast_reference_runtime_instance instance;',
        '} ast_reference_math_input;',
        'typedef struct ast_reference_math_sample {',
        '    uint32_t input_index;',
        '    float elapsed_seconds, elapsed_radians;',
        '    float model_row_major[16];',
        '    float uploaded_transpose_row_major[16];',
        '    float distance_to_eye;',
        '    uint32_t source_subdivision;',
        '} ast_reference_math_sample;',
        'static const float ast_reference_math_camera_eye[3] = {-110.F, 75.F, 210.F};',
        'static const float ast_reference_math_min_screen_size = 0.06F;',
        'enum { AST_REFERENCE_MATH_SUBDIVISIONS = 4, AST_REFERENCE_MATH_INPUT_COUNT = 16, AST_REFERENCE_MATH_SAMPLE_COUNT = 48 };',
        'static const ast_reference_math_input ast_reference_math_inputs[AST_REFERENCE_MATH_INPUT_COUNT] = {'
    ]
    for item in inputs:
        instance = item['instance']
        fields = [array(instance['scale_translate']), array(instance['spin_axis']), f(instance['scale']),
                  array(instance['deep']), f(instance['spin_speed']), array(instance['shallow']),
                  f(instance['orbit_speed']), f(instance['spin_angle']), f(instance['orbit_angle']),
                  str(instance['mesh_index']) + 'U', str(instance['texture_index']) + 'U']
        lines.append('    {' + f"{item['level']}U, {item['instance_index']}U, {{" + ', '.join(fields) + '}},')
    lines += ['};', 'static const ast_reference_math_sample ast_reference_math_samples[AST_REFERENCE_MATH_SAMPLE_COUNT] = {']
    for sample in data['samples']:
        fields = [str(sample['input_index']) + 'U', f(sample['elapsed_seconds']), f(sample['elapsed_radians']),
                  array(sample['model_row_major']), array(sample['uploaded_transpose_row_major']),
                  f(sample['distance_to_eye']), str(sample['source_subdivision']) + 'U']
        lines.append('    {' + ', '.join(fields) + '},')
    lines += ['};', '#endif']
    (args.math_output / 'runtime_math_vectors.h').write_text('\n'.join(lines) + '\n')
    levels = sorted(set(s['source_subdivision'] for s in data['samples']))
    print(f'PASS: emitted {len(inputs)} asymmetric inputs and {len(data["samples"])} literal-source math samples; source subdivisions {levels}')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output', type=Path)
    parser.add_argument('--dependencies', type=Path, required=True)
    parser.add_argument('--generator-build', type=Path, required=True)
    parser.add_argument('--work', type=Path, required=True)
    parser.add_argument('--compiler', default=os.environ.get('CXX', 'clang++'))
    parser.add_argument('--math-output', type=Path, help='Write runtime_math_vectors.h/.json instead of rerunning scene equivalence')
    main(parser.parse_args())
