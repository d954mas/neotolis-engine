/******************************************************************************

Copyright 2019-2020 Evgeny Gorodetskiy

Licensed under the Apache License, Version 2.0 (the "License"),
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.

Modified for offline export by Neotolis contributors, 2026.
Source functions and deterministic traversal choices are documented in README.md.
******************************************************************************/

#include "scene_export.hpp"
#include "scene_format.h"

#include <FastNoise/FastNoise.h>
#include <hlsl++.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <limits>
#include <numbers>
#include <random>
#include <stdexcept>
#include <vector>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../../../deps/stb/stb_image_write.h"

namespace methane_reference {
namespace {

static_assert(std::endian::native == std::endian::little, "Reference export requires a little-endian host");
static_assert(std::numeric_limits<float>::is_iec559 && sizeof(float) == 4);

struct MutableParameters {
    uint32_t instances_count;
    uint32_t unique_mesh_count;
    uint32_t textures_count;
    float scale_ratio;
};

constexpr std::array<MutableParameters, 10> mutable_parameters{{
    {1000U, 35U, 10U, 0.6F},
    {2000U, 50U, 10U, 0.5F},
    {3000U, 75U, 20U, 0.45F},
    {4000U, 100U, 20U, 0.4F},
    {5000U, 200U, 30U, 0.33F},
    {10000U, 300U, 30U, 0.3F},
    {15000U, 400U, 40U, 0.27F},
    {20000U, 500U, 40U, 0.23F},
    {35000U, 750U, 50U, 0.2F},
    {50000U, 1000U, 50U, 0.17F},
}};

struct TextureNoiseParameters {
    int random_seed = 0;
    int octave_count = 4;
    float gain = 0.5F;
    float fractal_weight = 0.5F;
    float lacunarity = 2.0F;
    float scale = 0.5F;
    float strength = 0.8F;
};

struct Colors {
    std::array<float, 3> deep;
    std::array<float, 3> shallow;
};

using ColorSchema = std::array<std::array<uint8_t, 3>, 6>;
constexpr ColorSchema srgb_deep_rock{{{55, 49, 40}, {58, 38, 14}, {98, 101, 104}, {172, 158, 122}, {88, 88, 88}, {148, 108, 102}}};
constexpr ColorSchema srgb_shallow_rock{{{140, 109, 61}, {172, 154, 58}, {204, 177, 119}, {204, 164, 136}, {130, 117, 98}, {160, 145, 114}}};
constexpr ColorSchema srgb_deep_ice{{{22, 51, 59}, {45, 72, 93}, {14, 25, 27}, {68, 103, 129}, {29, 59, 59}, {59, 92, 118}}};
constexpr ColorSchema srgb_shallow_ice{{{144, 163, 188}, {133, 179, 189}, {74, 135, 178}, {69, 143, 177}, {104, 168, 185}, {140, 170, 186}}};

Colors get_colors(const ColorSchema &deep, const ColorSchema &shallow, uint32_t deep_index, uint32_t shallow_index) {
    Colors result{};
    for (size_t channel = 0; channel < 3; ++channel) {
        result.deep[channel] = std::pow(static_cast<float>(deep[deep_index][channel]) / 255.F, 2.233333333F);
        result.shallow[channel] = std::pow(static_cast<float>(shallow[shallow_index][channel]) / 255.F, 2.233333333F);
    }
    return result;
}

hlslpp::float3 get_random_direction(std::mt19937 &rng) {
    std::normal_distribution<float> distribution;
    hlslpp::float3 direction;
    do {
        direction = {distribution(rng), distribution(rng), distribution(rng)};
    } while (static_cast<float>(hlslpp::length(direction)) <= std::numeric_limits<float>::min());
    return hlslpp::normalize(direction);
}

std::vector<TextureNoiseParameters> generate_texture_parameters(std::mt19937 &rng, uint32_t count) {
    std::uniform_real_distribution<float> noise_gain_distribution(0.2F, 0.8F);
    std::uniform_real_distribution<float> noise_fractal_distribution(0.3F, 0.7F);
    std::uniform_real_distribution<float> noise_lacunarity_distribution(1.5F, 2.5F);
    std::uniform_real_distribution<float> noise_scale_distribution(5.0F, 20.0F);
    std::uniform_real_distribution<float> noise_strength_distribution(0.8F, 1.0F);
    std::vector<TextureNoiseParameters> parameters;
    parameters.reserve(count);
    for (uint32_t index = 0; index < count; ++index) {
        parameters.push_back(TextureNoiseParameters{
            .random_seed = static_cast<int>(rng()),
            .gain = noise_gain_distribution(rng),
            .fractal_weight = noise_fractal_distribution(rng),
            .lacunarity = noise_lacunarity_distribution(rng),
            .scale = noise_scale_distribution(rng),
            .strength = noise_strength_distribution(rng),
        });
    }
    return parameters;
}

std::vector<uint8_t> fill_perlin_noise_to_texture(const TextureNoiseParameters &parameters) {
    constexpr uint32_t width = 256U;
    constexpr uint32_t height = 256U;
    auto simplex_noise = FastNoise::New<FastNoise::Simplex>();
    simplex_noise->SetScale(1.F);
    simplex_noise->SetOutputMin(0.F);
    simplex_noise->SetOutputMax(1.F);
    auto fbm_noise = FastNoise::New<FastNoise::FractalFBm>();
    fbm_noise->SetSource(simplex_noise);
    fbm_noise->SetOctaveCount(parameters.octave_count);
    fbm_noise->SetGain(parameters.gain);
    fbm_noise->SetLacunarity(parameters.lacunarity);
    fbm_noise->SetWeightedStrength(parameters.fractal_weight);
    std::vector<float> noise_values(static_cast<size_t>(width) * height);
    const FastNoise::OutputMinMax noise_min_max =
        fbm_noise->GenUniformGrid2D(noise_values.data(), 0.F, 0.F, static_cast<int>(width), static_cast<int>(height), 1.F / parameters.scale, 1.F / parameters.scale, parameters.random_seed);
    const float noise_range = noise_min_max.max - noise_min_max.min;
    const float noise_multiplier = noise_range > 0.F ? 255.F / noise_range : 0.F;
    std::vector<uint8_t> rgba(static_cast<size_t>(width) * height * 4, 255U);
    for (size_t index = 0; index < noise_values.size(); ++index) {
        const float noise_intensity = (noise_values[index] - noise_min_max.min) * noise_multiplier;
        const auto channel_value = static_cast<uint8_t>(std::min(255.F, noise_intensity));
        rgba[index * 4 + 0] = channel_value;
        rgba[index * 4 + 1] = channel_value;
        rgba[index * 4 + 2] = channel_value;
    }
    return rgba;
}

std::ofstream open_output(const std::filesystem::path &path, bool binary = false) {
    std::ofstream file;
    file.exceptions(std::ios::badbit | std::ios::failbit);
    file.open(path, std::ios::out | std::ios::trunc | (binary ? std::ios::binary : std::ios::openmode{}));
    file << std::setprecision(std::numeric_limits<float>::max_digits10);
    return file;
}

template <typename T> void write_array(std::ostream &out, const T *values, size_t count) {
    out << '[';
    for (size_t index = 0; index < count; ++index) {
        if (index)
            out << ',';
        out << values[index];
    }
    out << ']';
}

void write_texture_parameters(std::ostream &out, const TextureNoiseParameters &parameters) {
    out << "{\"random_seed\":" << parameters.random_seed << ",\"octave_count\":" << parameters.octave_count << ",\"gain\":" << parameters.gain << ",\"fractal_weight\":" << parameters.fractal_weight
        << ",\"lacunarity\":" << parameters.lacunarity << ",\"scale\":" << parameters.scale << ",\"strength\":" << parameters.strength << '}';
}

} // namespace

void export_scene_data(const std::filesystem::path &output_dir, uint32_t complexity) {
    if (complexity >= mutable_parameters.size())
        throw std::out_of_range("complexity must be between 0 and 9");
    const MutableParameters &settings = mutable_parameters[complexity];
    constexpr float scene_scale = 15.F;
    constexpr uint32_t seed = 1123U;
    constexpr uint32_t subdivisions = 4U;
    constexpr float orbit_radius = 13.F * scene_scale;
    constexpr float disc_radius = 4.F * scene_scale;
    std::mt19937 rng(seed);
    const auto texture_parameters = generate_texture_parameters(rng, settings.textures_count);
    std::uniform_int_distribution<uint32_t> textures_distribution(0U, settings.textures_count - 1);
    std::vector<uint32_t> mesh_subset_texture_indices(static_cast<size_t>(settings.unique_mesh_count) * subdivisions);
    for (uint32_t &texture_index : mesh_subset_texture_indices)
        texture_index = textures_distribution(rng);

    std::normal_distribution<float> normal_distribution;
    std::uniform_int_distribution<uint32_t> mesh_distribution(0U, settings.unique_mesh_count - 1);
    std::uniform_int_distribution<uint32_t> colors_distribution(0U, 5U);
    std::uniform_real_distribution<float> scale_distribution(settings.scale_ratio / 10.F, settings.scale_ratio);
    std::uniform_real_distribution<float> scale_proportion_distribution(0.8F, 1.2F);
    std::uniform_real_distribution<float> spin_velocity_distribution(-1.7F, 1.7F);
    std::uniform_real_distribution<float> orbit_velocity_distribution(1.5F, 5.F);
    std::normal_distribution<float> orbit_radius_distribution(orbit_radius, 0.6F * disc_radius);
    std::normal_distribution<float> orbit_height_distribution(0.F, 0.4F * disc_radius);
    std::vector<ast_reference_instance> instances;
    instances.reserve(settings.instances_count);
    for (uint32_t index = 0; index < settings.instances_count; ++index) {
        const uint32_t asteroid_mesh_index = mesh_distribution(rng);
        const float asteroid_orbit_radius = orbit_radius_distribution(rng);
        const float asteroid_orbit_height = orbit_height_distribution(rng);
        const float asteroid_scale_ratio = scale_distribution(rng);
        const float asteroid_scale = asteroid_scale_ratio * scene_scale;
        const hlslpp::float3 asteroid_scale_ratios = hlslpp::float3(scale_proportion_distribution(rng), scale_proportion_distribution(rng), scale_proportion_distribution(rng)) * asteroid_scale_ratio;
        const hlslpp::float4x4 scale_translate_matrix =
            hlslpp::mul(hlslpp::float4x4::scale(asteroid_scale_ratios * scene_scale), hlslpp::float4x4::translation(asteroid_orbit_radius, asteroid_orbit_height, 0.F));
        const Colors colors = normal_distribution(rng) <= 1.F ? get_colors(srgb_deep_ice, srgb_shallow_ice, colors_distribution(rng), colors_distribution(rng))
                                                              : get_colors(srgb_deep_rock, srgb_shallow_rock, colors_distribution(rng), colors_distribution(rng));
        ast_reference_instance instance{};
        instance.index = index;
        instance.mesh_instance_index = asteroid_mesh_index;
        instance.texture_index = textures_distribution(rng);
        std::copy(colors.deep.begin(), colors.deep.end(), instance.deep_color);
        std::copy(colors.shallow.begin(), colors.shallow.end(), instance.shallow_color);
        hlslpp::store(instance.scale_translate_matrix, scale_translate_matrix);
        hlslpp::store(instance.spin_axis, get_random_direction(rng));
        instance.scale = asteroid_scale;
        instance.orbit_speed = orbit_velocity_distribution(rng) / (asteroid_scale * asteroid_orbit_radius);
        instance.spin_speed = spin_velocity_distribution(rng) / asteroid_scale;
        instance.spin_angle_rad = static_cast<float>(std::numbers::pi) * normal_distribution(rng);
        instance.orbit_angle_rad = static_cast<float>(std::numbers::pi) * normal_distribution(rng) * 2.F;
        instances.push_back(instance);
    }
    std::filesystem::create_directories(output_dir);
    const std::string stem = "scene_level_" + std::to_string(complexity);
    const ast_reference_scene_header header{
        {'A', 'S', 'T', 'S', 'C', 'N', '0', '1'},
        1U,
        sizeof(ast_reference_scene_header),
        sizeof(ast_reference_instance),
        complexity,
        seed,
        settings.instances_count,
        settings.unique_mesh_count,
        subdivisions,
        settings.textures_count,
        static_cast<uint32_t>(mesh_subset_texture_indices.size()),
        scene_scale,
        settings.scale_ratio / 10.F,
        settings.scale_ratio,
        0U,
    };
    auto binary = open_output(output_dir / (stem + ".bin"), true);
    binary.write(reinterpret_cast<const char *>(&header), sizeof(header));
    binary.write(reinterpret_cast<const char *>(instances.data()), static_cast<std::streamsize>(instances.size() * sizeof(ast_reference_instance)));
    binary.write(reinterpret_cast<const char *>(mesh_subset_texture_indices.data()), static_cast<std::streamsize>(mesh_subset_texture_indices.size() * sizeof(uint32_t)));
    binary.close();

    auto runtime = open_output(output_dir / (stem + ".instances.bin"), true);
    const ast_reference_runtime_header runtime_header{AST_REFERENCE_RUNTIME_MAGIC, 1U, complexity, settings.instances_count};
    runtime.write(reinterpret_cast<const char *>(&runtime_header), sizeof(runtime_header));
    for (const ast_reference_instance &instance : instances) {
        ast_reference_runtime_instance record{};
        std::copy_n(instance.scale_translate_matrix, 16, record.scale_translate);
        std::copy_n(instance.spin_axis, 3, record.spin_axis);
        record.scale = instance.scale;
        std::copy_n(instance.deep_color, 3, record.deep);
        record.spin_speed = instance.spin_speed;
        std::copy_n(instance.shallow_color, 3, record.shallow);
        record.orbit_speed = instance.orbit_speed;
        record.spin_angle = instance.spin_angle_rad;
        record.orbit_angle = instance.orbit_angle_rad;
        record.mesh_index = instance.mesh_instance_index;
        record.texture_index = instance.texture_index;
        runtime.write(reinterpret_cast<const char *>(&record), sizeof(record));
    }
    runtime.close();

    auto json = open_output(output_dir / (stem + ".json"));
    json << "{\n\"schema\":\"methane-asteroids-reference-scene-v1\",\n"
            "\"upstream_commit\":\"16a5751e835dd0776d976e51438604dc8de27d16\",\n"
            "\"rng_policy\":\"serial source traversal; compiler/library provenance in README\",\n"
            "\"matrix_convention\":\"left-handed row-vector, row-major storage\",\n"
            "\"complexity\":"
         << complexity << ",\n\"random_seed\":" << seed << ",\n\"instance_count\":" << settings.instances_count << ",\n\"unique_mesh_count\":" << settings.unique_mesh_count
         << ",\n\"subdivisions_count\":" << subdivisions << ",\n\"textures_count\":" << settings.textures_count << ",\n\"scene_scale\":" << scene_scale
         << ",\n\"min_scale_ratio\":" << settings.scale_ratio / 10.F << ",\n\"max_scale_ratio\":" << settings.scale_ratio
         << ",\n\"orbit_radius\":195,\n\"orbit_radius_standard_deviation\":36,\n\"orbit_height_standard_deviation\":24,\n\"texture_dimensions\":[256,256],\n\"texture_parameters\":[";
    for (size_t index = 0; index < texture_parameters.size(); ++index) {
        if (index)
            json << ',';
        write_texture_parameters(json, texture_parameters[index]);
    }
    json << "],\n\"mesh_subset_texture_indices\":";
    write_array(json, mesh_subset_texture_indices.data(), mesh_subset_texture_indices.size());
    json << ",\n\"instances\":[\n";
    for (const ast_reference_instance &instance : instances) {
        if (instance.index)
            json << ",\n";
        json << "{\"index\":" << instance.index << ",\"mesh_instance_index\":" << instance.mesh_instance_index << ",\"texture_index\":" << instance.texture_index << ",\"deep_color\":";
        write_array(json, instance.deep_color, 3);
        json << ",\"shallow_color\":";
        write_array(json, instance.shallow_color, 3);
        json << ",\"scale_translate_matrix\":";
        write_array(json, instance.scale_translate_matrix, 16);
        json << ",\"spin_axis\":";
        write_array(json, instance.spin_axis, 3);
        json << ",\"scale\":" << instance.scale << ",\"orbit_speed\":" << instance.orbit_speed << ",\"spin_speed\":" << instance.spin_speed << ",\"spin_angle_rad\":" << instance.spin_angle_rad
             << ",\"orbit_angle_rad\":" << instance.orbit_angle_rad << '}';
    }
    json << "\n]}\n";
    json.close();
}

void export_texture_data(const std::filesystem::path &output_dir, uint32_t random_seed, uint32_t textures_count) {
    std::filesystem::create_directories(output_dir);
    std::mt19937 rng(random_seed);
    const auto parameters = generate_texture_parameters(rng, textures_count);
    auto manifest = open_output(output_dir / "textures.json");
    manifest << "{\n\"schema\":\"methane-asteroids-reference-textures-v1\",\n\"random_seed\":" << random_seed
             << ",\n\"dimensions\":[256,256],\n\"channels\":\"RGBA8Unorm\",\n\"layers\":3,\n"
                "\"row_order\":\"original GenUniformGrid2D row order; no vertical flip\",\n"
                "\"fastnoise_dispatch\":\"SSE2 relaxed\",\n\"textures\":[\n";
    for (uint32_t texture_index = 0; texture_index < textures_count; ++texture_index) {
        if (texture_index)
            manifest << ",\n";
        manifest << "{\"index\":" << texture_index << ",\"parameters\":";
        write_texture_parameters(manifest, parameters[texture_index]);
        manifest << ",\"layers\":[";
        // The source passes unchanged parameters to all three layers, so their bytes are identical.
        const std::vector<uint8_t> rgba = fill_perlin_noise_to_texture(parameters[texture_index]);
        for (uint32_t layer = 0; layer < 3U; ++layer) {
            const std::string stem = "texture_" + std::to_string(texture_index) + "_layer_" + std::to_string(layer);
            auto raw = open_output(output_dir / (stem + ".rgba"), true);
            raw.write(reinterpret_cast<const char *>(rgba.data()), static_cast<std::streamsize>(rgba.size()));
            raw.close();
            const std::string png_path = (output_dir / (stem + ".png")).string();
            if (!stbi_write_png(png_path.c_str(), 256, 256, 4, rgba.data(), 256 * 4))
                throw std::runtime_error("PNG write failed: " + png_path);
            if (layer)
                manifest << ',';
            manifest << "{\"layer\":" << layer << ",\"raw\":\"" << stem << ".rgba\",\"png\":\"" << stem << ".png\"}";
        }
        manifest << "]}";
    }
    manifest << "\n]}\n";
    manifest.close();
}

} // namespace methane_reference
