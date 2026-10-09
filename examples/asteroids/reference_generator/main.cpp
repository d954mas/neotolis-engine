/******************************************************************************
Copyright 2019-2020 Evgeny Gorodetskiy
Copyright 2026 Neotolis Contributors

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at
    https://www.apache.org/licenses/LICENSE-2.0
Unless required by applicable law or agreed to in writing, software distributed
under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
CONDITIONS OF ANY KIND, either express or implied. See the License for the
specific language governing permissions and limitations under the License.

Modified from Methane Asteroids Asteroid::Mesh::Randomize and MethaneKit's
IcosahedronMesh/BaseMesh/SphereMesh/CubeMesh/QuadMesh: removed renderer,
storage-wrapper and instrumentation
coupling; kept arithmetic, topology, subdivision and normal accumulation order.
Added serial generation, bounded GLB packing, metadata and validation.
See README.md and dependencies.json for exact original source revisions.
******************************************************************************/

#include <FastNoise/FastNoise.h>
#include <hlsl++/vector_float.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <numbers>
#include <random>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;

struct Vec3 {
    float x, y, z;
    hlslpp::float3 hlsl() const { return {x, y, z}; }
    static Vec3 from(const hlslpp::float3 &v) { return {v.x, v.y, v.z}; }
    float length() const {
        float square_sum = 0.F;
        square_sum += x * x;
        square_sum += y * y;
        square_sum += z * z;
        return std::sqrt(square_sum);
    }
    void scale(float multiplier) {
        x *= multiplier;
        y *= multiplier;
        z *= multiplier;
    }
};
struct Vertex {
    Vec3 position;
    Vec3 normal;
};
struct Mesh {
    std::vector<Vertex> vertices;
    std::vector<uint16_t> indices;
    float depth_min = 0.F, depth_max = 0.F;
    std::vector<std::array<float, 2>> texcoords;
};
struct Subset {
    uint32_t subdivision, variant, chunk_index, first_index, index_count, first_vertex, vertex_count, random_seed;
    float depth_min, depth_max;
};
struct Chunk {
    std::string name;
    uint32_t subdivision, first_variant, variant_count;
    Mesh mesh;
};

static void require(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}

static void spherify(Mesh &mesh) {
    for (Vertex &vertex : mesh.vertices) {
        const hlslpp::float3 normalized = hlslpp::normalize(vertex.position.hlsl());
        vertex.position = Vec3::from(normalized * 0.5F);
        vertex.normal = Vec3::from(normalized);
    }
}

static Mesh base_mesh(uint32_t subdivisions) {
    constexpr float radius = 0.5F;
    const float a = (radius + std::sqrt(radius * 5.F)) / 2.F;
    const float b = radius;
    const std::array<Vec3, 12> positions{{{-b, a, 0}, {b, a, 0}, {-b, -a, 0}, {b, -a, 0}, {0, -b, a}, {0, b, a}, {0, -b, -a}, {0, b, -a}, {a, 0, -b}, {a, 0, b}, {-a, 0, -b}, {-a, 0, b}}};
    Mesh mesh;
    for (const Vec3 &p : positions)
        mesh.vertices.push_back({p, Vec3::from(hlslpp::normalize(p.hlsl()))});
    mesh.indices = {5, 0, 11, 1, 0, 5, 7, 0, 1, 10, 0, 7, 11, 0, 10, 9, 1, 5, 4,  5, 11, 2,  11, 10, 6, 10, 7, 8, 7, 1,
                    4, 3, 9,  2, 3, 4, 6, 3, 2, 8,  3, 6, 9,  3, 8,  5, 4, 9, 11, 2, 4,  10, 6,  2,  7, 8,  6, 1, 9, 8};
    for (uint32_t subdivision = 0; subdivision < subdivisions; ++subdivision) {
        std::vector<uint16_t> indices;
        std::map<std::pair<uint16_t, uint16_t>, uint16_t> midpoints;
        auto midpoint = [&](uint16_t v1, uint16_t v2) {
            const auto key = std::make_pair(std::min(v1, v2), std::max(v1, v2));
            if (const auto found = midpoints.find(key); found != midpoints.end())
                return found->second;
            const Vertex &first = mesh.vertices[key.first];
            const Vertex &second = mesh.vertices[key.second];
            const Vertex vertex{Vec3::from((first.position.hlsl() + second.position.hlsl()) / 2.F), Vec3::from(hlslpp::normalize(first.normal.hlsl() + second.normal.hlsl()))};
            const auto index = static_cast<uint16_t>(mesh.vertices.size());
            midpoints.emplace(key, index);
            mesh.vertices.push_back(vertex);
            return index;
        };
        for (size_t i = 0; i < mesh.indices.size(); i += 3) {
            const uint16_t vi1 = mesh.indices[i], vi2 = mesh.indices[i + 1], vi3 = mesh.indices[i + 2];
            const uint16_t vm1 = midpoint(vi1, vi2), vm2 = midpoint(vi2, vi3), vm3 = midpoint(vi3, vi1);
            const std::array<uint16_t, 12> divided{vi1, vm1, vm3, vm1, vi2, vm2, vm1, vm2, vm3, vm3, vm2, vi3};
            indices.insert(indices.end(), divided.begin(), divided.end());
        }
        mesh.indices.swap(indices);
    }
    spherify(mesh);
    spherify(mesh); // Constructor and UberMesh each call the original operation.
    return mesh;
}

static void average_normals(Mesh &mesh) {
    for (Vertex &vertex : mesh.vertices)
        vertex.normal = {0.F, 0.F, 0.F};
    for (size_t i = 0; i < mesh.indices.size(); i += 3) {
        Vertex &v1 = mesh.vertices[mesh.indices[i]];
        Vertex &v2 = mesh.vertices[mesh.indices[i + 1]];
        Vertex &v3 = mesh.vertices[mesh.indices[i + 2]];
        const hlslpp::float3 u = v2.position.hlsl() - v1.position.hlsl();
        const hlslpp::float3 v = v3.position.hlsl() - v1.position.hlsl();
        const hlslpp::float3 normal = hlslpp::cross(u, v);
        v1.normal = Vec3::from(v1.normal.hlsl() + normal);
        v2.normal = Vec3::from(v2.normal.hlsl() + normal);
        v3.normal = Vec3::from(v3.normal.hlsl() + normal);
    }
    for (Vertex &vertex : mesh.vertices)
        vertex.normal = Vec3::from(hlslpp::normalize(vertex.normal.hlsl()));
}

static void randomize(Mesh &mesh, uint32_t random_seed) {
    constexpr float noise_feature_scale = 1.0F;
    constexpr int noise_octave_count = 4;
    constexpr float noise_lacunarity = 2.F;
    constexpr float noise_range_min = 0.5F;
    constexpr float noise_range_max = 1.0F;
    constexpr float radius_scale = 1.5F;
    constexpr float radius_bias = 0.3F;
    std::mt19937 rng(random_seed);
    auto random_persistence = std::normal_distribution<float>(0.95F, 0.04F);
    const float noise_gain = random_persistence(rng);
    auto random_noise = std::uniform_real_distribution<float>(0.0F, 10000.0F);
    const float noise_w_offset = random_noise(rng);
    auto simplex = FastNoise::New<FastNoise::Simplex>(FastSIMD::FeatureSet::SSE2);
    require(static_cast<bool>(simplex), "FastNoise SSE2 is unavailable");
    simplex->SetScale(noise_feature_scale);
    auto fbm = FastNoise::New<FastNoise::FractalFBm>(FastSIMD::FeatureSet::SSE2);
    fbm->SetSource(simplex);
    fbm->SetOctaveCount(noise_octave_count);
    fbm->SetGain(noise_gain);
    fbm->SetLacunarity(noise_lacunarity);
    const size_t count = mesh.vertices.size();
    std::vector<float> x(count), y(count), z(count), w(count, 0.F), values(count);
    for (size_t i = 0; i < count; ++i) {
        x[i] = mesh.vertices[i].position.x;
        y[i] = mesh.vertices[i].position.y;
        z[i] = mesh.vertices[i].position.z;
    }
    const auto range = fbm->GenPositionArray4D(values.data(), static_cast<int>(count), x.data(), y.data(), z.data(), w.data(), 0.F, 0.F, 0.F, noise_w_offset, static_cast<int>(random_seed));
    const float noise_range = range.max - range.min;
    const float multiplier = noise_range > 0.F ? (noise_range_max - noise_range_min) / noise_range : 0.F;
    mesh.depth_min = std::numeric_limits<float>::max();
    mesh.depth_max = std::numeric_limits<float>::min();
    for (size_t i = 0; i < count; ++i) {
        Vertex &vertex = mesh.vertices[i];
        const float noise = noise_range_min + (values[i] - range.min) * multiplier;
        vertex.position.scale(noise * radius_scale + radius_bias);
        const float depth = vertex.position.length();
        mesh.depth_min = std::min(mesh.depth_min, depth);
        mesh.depth_max = std::max(mesh.depth_max, depth);
    }
    average_normals(mesh);
}

static void validate(const Mesh &mesh, uint32_t subdivision) {
    const uint32_t triangles = 20U << (2U * subdivision);
    require(mesh.indices.size() == triangles * 3U, "wrong triangle count");
    require(mesh.vertices.size() == 10U * (1U << (2U * subdivision)) + 2U, "wrong indexed vertex count");
    require(std::abs(mesh.depth_min - 0.525F) < 0.0001F && std::abs(mesh.depth_max - 0.9F) < 0.0001F, "source depth range mismatch");
    for (const Vertex &vertex : mesh.vertices) {
        require(std::isfinite(vertex.position.length()) && vertex.position.length() <= 0.9001F, "invalid radius");
        require(std::isfinite(vertex.normal.length()) && std::abs(vertex.normal.length() - 1.F) < 0.0001F, "invalid averaged normal");
        require(static_cast<float>(hlslpp::dot(vertex.normal.hlsl(), vertex.position.hlsl())) > 0.F, "inward averaged normal");
    }
    for (size_t i = 0; i < mesh.indices.size(); i += 3) {
        for (size_t j = 0; j < 3; ++j)
            require(mesh.indices[i + j] < mesh.vertices.size(), "index out of bounds");
        const auto p = mesh.vertices[mesh.indices[i]].position.hlsl();
        const auto u = mesh.vertices[mesh.indices[i + 1]].position.hlsl() - p;
        const auto v = mesh.vertices[mesh.indices[i + 2]].position.hlsl() - p;
        require(static_cast<float>(hlslpp::dot(hlslpp::cross(u, v), p)) > 0.F, "degenerate or inward triangle");
    }
}

static void u32(std::vector<uint8_t> &out, uint32_t value) {
    for (uint32_t i = 0; i < 4; ++i)
        out.push_back(static_cast<uint8_t>(value >> (8U * i)));
}
static void f32(std::vector<uint8_t> &out, float value) { u32(out, std::bit_cast<uint32_t>(value)); }

static Mesh planet_mesh() {
    Mesh mesh;
    constexpr uint32_t lat_lines = 32, long_lines = 32, actual_long_lines = 33;
    const float texcoord_long_spacing = 1.F / (actual_long_lines - 1);
    const float texcoord_lat_spacing = 1.F / (lat_lines + 1);
    for (uint32_t latitude = 0; latitude < lat_lines; ++latitude) {
        const float lat_ratio = static_cast<float>(latitude) / static_cast<float>(lat_lines - 1);
        for (uint32_t longitude = 0; longitude < actual_long_lines; ++longitude) {
            const float long_ratio = static_cast<float>(longitude) / static_cast<float>(actual_long_lines - 1);
            const auto pi = std::numbers::pi_v<float>;
            const Vec3 position{std::sin(pi * lat_ratio) * std::cos(2.F * pi * long_ratio), std::cos(pi * lat_ratio), std::sin(pi * lat_ratio) * std::sin(2.F * pi * long_ratio)};
            mesh.vertices.push_back({position, position});
            mesh.texcoords.push_back({texcoord_long_spacing * static_cast<float>(longitude), texcoord_lat_spacing * static_cast<float>(latitude)});
        }
    }
    for (uint32_t latitude = 0; latitude < lat_lines - 1; ++latitude) {
        for (uint32_t longitude = 0; longitude < long_lines; ++longitude) {
            const uint16_t a = static_cast<uint16_t>(latitude * actual_long_lines + longitude);
            const uint16_t b = static_cast<uint16_t>((latitude + 1) * actual_long_lines + longitude);
            for (uint16_t index : {a, static_cast<uint16_t>(a + 1), b, b, static_cast<uint16_t>(a + 1), static_cast<uint16_t>(b + 1)})
                mesh.indices.push_back(index);
        }
    }
    return mesh;
}

static Mesh skybox_mesh() {
    Mesh mesh;
    const std::array<std::array<float, 2>, 4> positions{{{-0.5F, -0.5F}, {-0.5F, 0.5F}, {0.5F, 0.5F}, {0.5F, -0.5F}}};
    const std::array<uint16_t, 6> face_indices{0, 1, 2, 0, 2, 3};
    for (uint32_t face = 0; face < 6; ++face) {
        const uint32_t axis = face / 2;
        const float depth = face % 2 == 0 ? 0.5F : -0.5F;
        for (const auto &p : positions) {
            const Vec3 position = axis == 0 ? Vec3{p[0], p[1], depth} : axis == 1 ? Vec3{p[0], depth, p[1]} : Vec3{depth, p[1], p[0]};
            mesh.vertices.push_back({position, {0.F, 0.F, 0.F}});
        }
        const bool reverse = (axis == 0 && depth >= 0.F) || (axis != 0 && depth < 0.F);
        for (uint32_t i = 0; i < face_indices.size(); ++i)
            mesh.indices.push_back(static_cast<uint16_t>(face * 4 + face_indices[reverse ? 5 - i : i]));
    }
    return mesh;
}

static void write_glb(const fs::path &path, std::span<const Chunk> chunks) {
    std::vector<uint8_t> binary;
    std::ostringstream views, accessors, meshes, nodes, scene_nodes;
    accessors << std::setprecision(std::numeric_limits<float>::max_digits10);
    for (size_t mesh_index = 0; mesh_index < chunks.size(); ++mesh_index) {
        const Chunk &chunk = chunks[mesh_index];
        const Mesh &mesh = chunk.mesh;
        const uint32_t count = static_cast<uint32_t>(mesh.vertices.size());
        const uint32_t binary_base = static_cast<uint32_t>(binary.size());
        const uint32_t accessor_base = static_cast<uint32_t>(mesh_index * 4U);
        std::array<float, 3> minimum{INFINITY, INFINITY, INFINITY}, maximum{-INFINITY, -INFINITY, -INFINITY};
        for (const Vertex &vertex : mesh.vertices) {
            const std::array<float, 3> p{vertex.position.x, vertex.position.y, vertex.position.z};
            for (size_t i = 0; i < 3; ++i) {
                f32(binary, p[i]);
                minimum[i] = std::min(minimum[i], p[i]);
                maximum[i] = std::max(maximum[i], p[i]);
            }
        }
        for (const Vertex &vertex : mesh.vertices) {
            f32(binary, vertex.normal.x);
            f32(binary, vertex.normal.y);
            f32(binary, vertex.normal.z);
        }
        require(mesh.texcoords.empty() || mesh.texcoords.size() == count, "UV count mismatch");
        for (uint32_t i = 0; i < count; ++i) {
            f32(binary, mesh.texcoords.empty() ? 0.F : mesh.texcoords[i][0]);
            f32(binary, mesh.texcoords.empty() ? 0.F : mesh.texcoords[i][1]);
        }
        for (uint16_t index : mesh.indices) {
            binary.push_back(static_cast<uint8_t>(index));
            binary.push_back(static_cast<uint8_t>(index >> 8U));
        }
        while (binary.size() % 4U)
            binary.push_back(0);
        if (mesh_index) {
            views << ',';
            accessors << ',';
            meshes << ',';
            nodes << ',';
            scene_nodes << ',';
        }
        const std::array<uint32_t, 4> offsets{binary_base, binary_base + count * 12U, binary_base + count * 24U, binary_base + count * 32U};
        const std::array<uint32_t, 4> lengths{count * 12U, count * 12U, count * 8U, static_cast<uint32_t>(mesh.indices.size() * 2U)};
        for (size_t i = 0; i < 4; ++i) {
            if (i)
                views << ',';
            views << "{\"buffer\":0,\"byteOffset\":" << offsets[i] << ",\"byteLength\":" << lengths[i] << ",\"target\":" << (i == 3 ? 34963 : 34962) << '}';
        }
        accessors << "{\"bufferView\":" << accessor_base << ",\"componentType\":5126,\"count\":" << count << ",\"type\":\"VEC3\",\"min\":[" << minimum[0] << ',' << minimum[1] << ',' << minimum[2]
                  << "],\"max\":[" << maximum[0] << ',' << maximum[1] << ',' << maximum[2] << "]},{\"bufferView\":" << accessor_base + 1 << ",\"componentType\":5126,\"count\":" << count
                  << ",\"type\":\"VEC3\"},{\"bufferView\":" << accessor_base + 2 << ",\"componentType\":5126,\"count\":" << count << ",\"type\":\"VEC2\"},{\"bufferView\":" << accessor_base + 3
                  << ",\"componentType\":5123,\"count\":" << mesh.indices.size() << ",\"type\":\"SCALAR\"}";
        meshes << "{\"name\":\"" << chunk.name << "\",\"primitives\":[{\"attributes\":{\"POSITION\":" << accessor_base << ",\"NORMAL\":" << accessor_base + 1 << ",\"TEXCOORD_0\":" << accessor_base + 2
               << "},\"indices\":" << accessor_base + 3 << ",\"mode\":4}]}";
        nodes << "{\"name\":\"" << chunk.name << "\",\"mesh\":" << mesh_index << '}';
        scene_nodes << mesh_index;
    }
    std::ostringstream json;
    json << "{\"asset\":{\"version\":\"2.0\",\"generator\":\"Neotolis offline Methane reference exporter\"},\"scene\":0,\"scenes\":[{\"nodes\":[" << scene_nodes.str() << "]}],\"nodes\":["
         << nodes.str() << "],\"meshes\":[" << meshes.str() << "],\"buffers\":[{\"byteLength\":" << binary.size() << "}],\"bufferViews\":[" << views.str() << "],\"accessors\":[" << accessors.str()
         << "]}";
    std::string document = json.str();
    while (document.size() % 4U)
        document.push_back(' ');
    std::vector<uint8_t> output;
    u32(output, 0x46546c67);
    u32(output, 2);
    u32(output, static_cast<uint32_t>(28U + document.size() + binary.size()));
    u32(output, static_cast<uint32_t>(document.size()));
    u32(output, 0x4e4f534a);
    output.insert(output.end(), document.begin(), document.end());
    u32(output, static_cast<uint32_t>(binary.size()));
    u32(output, 0x004e4942);
    output.insert(output.end(), binary.begin(), binary.end());
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char *>(output.data()), static_cast<std::streamsize>(output.size()));
    require(static_cast<bool>(file), "could not write GLB");
}

static void write_metadata(const fs::path &output, uint32_t unique, uint32_t seed, const std::vector<Chunk> &chunks, const std::vector<Subset> &subsets) {
    std::vector<uint8_t> wire;
    u32(wire, 0x4F454741U);
    u32(wire, 1U);
    u32(wire, unique);
    u32(wire, static_cast<uint32_t>(chunks.size()));
    u32(wire, static_cast<uint32_t>(subsets.size()));
    for (const Chunk &c : chunks) {
        u32(wire, c.subdivision);
        u32(wire, c.first_variant);
        u32(wire, c.variant_count);
        u32(wire, static_cast<uint32_t>(c.mesh.vertices.size()));
        u32(wire, static_cast<uint32_t>(c.mesh.indices.size()));
    }
    for (const Subset &s : subsets) {
        u32(wire, s.chunk_index);
        u32(wire, s.first_index);
        u32(wire, s.index_count);
        u32(wire, s.first_vertex);
        u32(wire, s.vertex_count);
        u32(wire, s.random_seed);
        f32(wire, s.depth_min);
        f32(wire, s.depth_max);
    }
    require(wire.size() == 20U + chunks.size() * 20U + subsets.size() * 32U, "geometry wire size mismatch");
    std::ofstream binary(output / "geometry.bin", std::ios::binary);
    binary.write(reinterpret_cast<const char *>(wire.data()), static_cast<std::streamsize>(wire.size()));
    require(static_cast<bool>(binary), "could not write binary metadata");
    std::ofstream json(output / "geometry.json");
    json << std::setprecision(std::numeric_limits<float>::max_digits10) << "{\n\"schema_version\":1,\"unique_count\":" << unique << ",\"seed\":" << seed
         << ",\"ordering\":\"serial_subdivision_then_variant\",\"simd\":\"SSE2\",\"fastnoise_math\":\"relaxed\",\"compiler\":\"" << __VERSION__ << "\",\"libstdcxx_date\":" << __GLIBCXX__
         << ",\"libstdcxx_release\":" << _GLIBCXX_RELEASE
         << ",\"fastsimd\":\"16450dae9528727e500e7254f635a671f9c7ee2d\",\"methane_asteroids\":\"16a5751e835dd0776d976e51438604dc8de27d16\",\"methane_kit\":"
            "\"04a95fb78334252594427c69737839518e53c4a0\",\"fastnoise2\":"
            "\"903c1f2d2f9d53ddce94cd223f32727d9ab3aeaa\",\"hlslpp\":\"3a5b1cf0d807f945ec861201b316c425e9cf5061\",\"chunks\":[\n";
    for (size_t i = 0; i < chunks.size(); ++i) {
        const Chunk &c = chunks[i];
        if (i)
            json << ",\n";
        json << "{\"file\":\"" << c.name << ".glb\",\"name\":\"" << c.name << "\",\"subdivision\":" << c.subdivision << ",\"lod\":" << 3U - c.subdivision << ",\"first_variant\":" << c.first_variant
             << ",\"variant_count\":" << c.variant_count << ",\"vertex_count\":" << c.mesh.vertices.size() << ",\"index_count\":" << c.mesh.indices.size() << '}';
    }
    json << "\n],\"subsets\":[\n";
    for (size_t i = 0; i < subsets.size(); ++i) {
        const Subset &s = subsets[i];
        if (i)
            json << ",\n";
        json << "{\"subdivision\":" << s.subdivision << ",\"lod\":" << 3U - s.subdivision << ",\"variant\":" << s.variant << ",\"chunk_index\":" << s.chunk_index
             << ",\"first_index\":" << s.first_index << ",\"index_count\":" << s.index_count << ",\"first_vertex\":" << s.first_vertex << ",\"vertex_count\":" << s.vertex_count
             << ",\"depth_min\":" << s.depth_min << ",\"depth_max\":" << s.depth_max << ",\"random_seed\":" << s.random_seed << '}';
    }
    json << "\n]}\n";
    require(static_cast<bool>(json), "could not write manifest");
    std::ofstream header(output / "geometry_meta.h");
    header << "/* Generated offline reference geometry. Source subdivision order is coarse to fine. */\n#pragma once\n#include <stdint.h>\n#define AST_REFERENCE_UNIQUE_COUNT " << unique
           << "U\n#define AST_REFERENCE_CHUNK_COUNT " << chunks.size() << "U\n#define AST_REFERENCE_SUBSET_COUNT " << subsets.size()
           << "U\ntypedef struct { uint32_t subdivision, first_variant, variant_count, vertex_count, index_count; } AstReferenceChunk;\n"
           << "typedef struct { uint32_t chunk_index, first_index, index_count, first_vertex, vertex_count, random_seed; float depth_min, depth_max; } AstReferenceSubset;\n"
           << "static const char *const ast_reference_chunk_names[AST_REFERENCE_CHUNK_COUNT] = {\n";
    for (const Chunk &c : chunks)
        header << "    \"" << c.name << "\",\n";
    header << "};\nstatic const AstReferenceChunk ast_reference_chunks[AST_REFERENCE_CHUNK_COUNT] = {\n";
    for (const Chunk &c : chunks)
        header << "    {" << c.subdivision << "U," << c.first_variant << "U," << c.variant_count << "U," << c.mesh.vertices.size() << "U," << c.mesh.indices.size() << "U},\n";
    header << "};\nstatic const AstReferenceSubset ast_reference_subsets[AST_REFERENCE_SUBSET_COUNT] = {\n" << std::scientific << std::setprecision(std::numeric_limits<float>::max_digits10);
    for (const Subset &s : subsets)
        header << "    {" << s.chunk_index << "U," << s.first_index << "U," << s.index_count << "U," << s.first_vertex << "U," << s.vertex_count << "U," << s.random_seed << "U," << s.depth_min << "F,"
               << s.depth_max << "F},\n";
    header << "};\n";
    require(static_cast<bool>(header), "could not write C metadata");
}

static uint32_t parse_u32(const char *text) {
    size_t consumed = 0;
    const uint64_t value = std::stoull(text, &consumed);
    require(consumed == std::string(text).size() && value <= UINT32_MAX, "integer argument is not a uint32 value");
    return static_cast<uint32_t>(value);
}

int main(int argc, char **argv) {
    try {
        uint32_t unique = 750, seed = 1123;
        fs::path output;
        for (int i = 1; i < argc; ++i) {
            require(i + 1 < argc, "expected --unique N --seed N --output DIR");
            const std::string option = argv[i++];
            if (option == "--unique")
                unique = parse_u32(argv[i]);
            else if (option == "--seed")
                seed = parse_u32(argv[i]);
            else if (option == "--output")
                output = argv[i];
            else
                throw std::runtime_error("unknown argument: " + option);
        }
        require(unique > 0 && unique <= 1000 && !output.empty(), "unique must be 1..1000; output is required");
        fs::create_directories(output);
        std::mt19937 rng(seed);
        std::vector<Chunk> chunks;
        std::vector<Subset> subsets;
        uint64_t total_vertices = 0, total_indices = 0;
        for (uint32_t subdivision = 0; subdivision < 4; ++subdivision) {
            const Mesh base = base_mesh(subdivision);
            const auto vertex_count = static_cast<uint32_t>(base.vertices.size());
            const auto index_count = static_cast<uint32_t>(base.indices.size());
            const uint32_t variants_per_chunk = std::min(65535U / vertex_count, 196608U / index_count);
            for (uint32_t variant = 0; variant < unique; ++variant) {
                if (variant % variants_per_chunk == 0) {
                    const std::string name = "rocks_s" + std::to_string(subdivision) + "_c" + std::to_string(variant / variants_per_chunk);
                    chunks.push_back({name, subdivision, variant, 0, {}});
                }
                Chunk &chunk = chunks.back();
                Mesh mesh = base;
                const uint32_t random_seed = rng();
                randomize(mesh, random_seed);
                validate(mesh, subdivision);
                const auto first_vertex = static_cast<uint32_t>(chunk.mesh.vertices.size());
                subsets.push_back({subdivision, variant, static_cast<uint32_t>(chunks.size() - 1), static_cast<uint32_t>(chunk.mesh.indices.size()), index_count, first_vertex, vertex_count,
                                   random_seed, mesh.depth_min, mesh.depth_max});
                chunk.mesh.vertices.insert(chunk.mesh.vertices.end(), mesh.vertices.begin(), mesh.vertices.end());
                for (uint16_t index : mesh.indices)
                    chunk.mesh.indices.push_back(static_cast<uint16_t>(first_vertex + index));
                chunk.variant_count++;
                require(chunk.mesh.vertices.size() <= 65535U && chunk.mesh.indices.size() <= 196608U, "builder mesh limits exceeded");
                total_vertices += vertex_count;
                total_indices += index_count;
            }
        }
        uint64_t bytes = 0;
        for (const Chunk &chunk : chunks) {
            const fs::path path = output / (chunk.name + ".glb");
            write_glb(path, std::span<const Chunk>(&chunk, 1));
            bytes += fs::file_size(path);
        }
        write_metadata(output, unique, seed, chunks, subsets);
        const std::array<Chunk, 2> environment{{{"planet", 0, 0, 0, planet_mesh()}, {"skybox", 0, 0, 0, skybox_mesh()}}};
        write_glb(output / "environment.glb", environment);
        std::cout << "PASS: " << unique << " variants x 4 independent source LODs; " << chunks.size() << " chunks; " << total_vertices << " vertices; " << total_indices << " indices; " << bytes
                  << " GLB bytes\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
