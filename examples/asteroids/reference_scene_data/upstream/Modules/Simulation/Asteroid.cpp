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

*******************************************************************************

FILE: Asteroid.cpp
Random generated asteroid model with mesh and texture ready for rendering

******************************************************************************/

#include "Asteroid.h"

#include <Methane/Graphics/RHI/RenderContext.h>
#include <Methane/Checks.hpp>
#include <Methane/Instrumentation.h>

#include <FastNoise/FastNoise.h>

#include <algorithm>
#include <cmath>
#include <random>

namespace Methane::Samples
{

using AsteroidColorSchema = std::array<gfx::Color3F, Asteroid::color_schema_size>;

static gfx::Color3F TransformSrgbToLinear(const gfx::Color3F& srgb_color)
{
    META_FUNCTION_TASK();
    gfx::Color3F linear_color{};
    for (size_t c = 0U; c < gfx::Color3F::Size; ++c)
    {
        linear_color.Set(c, std::pow(srgb_color[c], 2.233333333F));
    }
    return linear_color;
}

static AsteroidColorSchema TransformSrgbToLinear(const AsteroidColorSchema& srgb_color_schema)
{
    META_FUNCTION_TASK();
    AsteroidColorSchema linear_color_schema{};
    for (size_t i = 0; i < srgb_color_schema.size(); ++i)
    {
        linear_color_schema[i] = TransformSrgbToLinear(srgb_color_schema[i]);
    }
    return linear_color_schema;
}

Asteroid::Mesh::Mesh(uint32_t subdivisions_count, bool randomize)
    : gfx::IcosahedronMesh<Vertex>(Mesh::VertexLayout(Vertex::layout), 0.5F, subdivisions_count, true)
{
    META_FUNCTION_TASK();
    if (randomize)
    {
        Randomize();
    }
}

void Asteroid::Mesh::Randomize(uint32_t random_seed)
{
    META_FUNCTION_TASK();
    // Simplex feature scale (reciprocal of frequency) in mesh coordinates: the base mesh is a
    // sphere of 0.5 radius, so the first octave has about 5 noise features across its diameter.
    constexpr float noise_feature_scale = 1.0F;
    constexpr int   noise_octave_count  = 4;
    constexpr float noise_lacunarity    = 2.F;

    // Normalized noise range applied to the vertex radius: the lower bound keeps the deepest
    // craters from collapsing the mesh towards its center.
    constexpr float noise_range_min = 0.5F;
    constexpr float noise_range_max = 1.0F;

    constexpr float radius_scale = 1.5F;
    constexpr float radius_bias = 0.3F;

    std::mt19937 rng(random_seed); // NOSONAR - using pseudorandom generator is safe here

    auto random_persistence = std::normal_distribution<float>(0.95F, 0.04F);
    const float noise_gain = random_persistence(rng);

    auto  random_noise = std::uniform_real_distribution<float>(0.0F, 10000.0F);
    const float noise_w_offset = random_noise(rng);

    // Fractal Brownian Motion of the Simplex noise:
    // gain is randomized per mesh, so unlike the texture noise generator the node tree can not be
    // shared and is created for each mesh; node creation and generation are both thread-safe,
    // which is required because asteroid meshes are randomized in parallel tasks.
    auto simplex_noise_ptr = FastNoise::New<FastNoise::Simplex>();
    simplex_noise_ptr->SetScale(noise_feature_scale);

    auto fbm_noise_ptr = FastNoise::New<FastNoise::FractalFBm>();
    fbm_noise_ptr->SetSource(simplex_noise_ptr);
    fbm_noise_ptr->SetOctaveCount(noise_octave_count);
    fbm_noise_ptr->SetGain(noise_gain);
    fbm_noise_ptr->SetLacunarity(noise_lacunarity);

    // Batched SIMD-accelerated generation takes vertex coordinates as separate per-axis arrays,
    // so vertex positions are transposed before generating noise for all vertices at once.
    const auto vertex_count = static_cast<size_t>(GetVertexCount());
    std::vector<float> pos_x(vertex_count);
    std::vector<float> pos_y(vertex_count);
    std::vector<float> pos_z(vertex_count);
    const std::vector<float> pos_w(vertex_count, 0.F);

    const Vertices& vertices = GetVertices();
    for (size_t vertex_index = 0; vertex_index < vertex_count; ++vertex_index)
    {
        const Mesh::Position& position = vertices[vertex_index].position;
        pos_x[vertex_index] = position.GetX();
        pos_y[vertex_index] = position.GetY();
        pos_z[vertex_index] = position.GetZ();
    }

    // The 4-th noise dimension is fixed to a random offset which decorrelates asteroid meshes
    // generated with the same vertex positions.
    std::vector<float> noise_values(vertex_count);
    const FastNoise::OutputMinMax noise_min_max = fbm_noise_ptr->GenPositionArray4D(
        noise_values.data(), static_cast<int>(vertex_count),
        pos_x.data(), pos_y.data(), pos_z.data(), pos_w.data(),
        0.F, 0.F, 0.F, noise_w_offset,
        static_cast<int>(random_seed));

    // FBM output range depends on the octaves count and gain, so generated values are normalized
    // to the [noise_range_min, noise_range_max] range with the actually generated range.
    const float noise_range      = noise_min_max.max - noise_min_max.min;
    const float noise_multiplier = noise_range > 0.F ? (noise_range_max - noise_range_min) / noise_range : 0.F;

    m_depth_range.first = std::numeric_limits<float>::max();
    m_depth_range.second = std::numeric_limits<float>::min();

    for (size_t vertex_index = 0; vertex_index < vertex_count; ++vertex_index)
    {
        Vertex& vertex = GetMutableVertex(vertex_index);
        const float noise = noise_range_min + (noise_values[vertex_index] - noise_min_max.min) * noise_multiplier;
        vertex.position *= noise * radius_scale + radius_bias;

        const float vertex_depth = vertex.position.GetLength();
        m_depth_range.first = std::min(m_depth_range.first, vertex_depth);
        m_depth_range.second = std::max(m_depth_range.second, vertex_depth);
    }

    ComputeAverageNormals();
}

Asteroid::Asteroid(const rhi::RenderContext& render_context, const rhi::CommandQueue& render_cmd_queue)
    : BaseBuffers(render_cmd_queue, Mesh(3, true), "Asteroid")
{
    META_FUNCTION_TASK();
    SetTexture(GenerateTextureArray(render_context, render_cmd_queue,
                                    gfx::Dimensions(256, 256), 1, true,
                                    TextureNoiseParameters()));
}

rhi::Texture Asteroid::GenerateTextureArray(const rhi::RenderContext& render_context,
                                            const rhi::CommandQueue& render_cmd_queue,
                                            const gfx::Dimensions& dimensions,
                                            uint32_t array_size, bool mipmapped,
                                            const TextureNoiseParameters& noise_parameters)
{
    META_FUNCTION_TASK();
    const rhi::SubResources sub_resources = GenerateTextureArraySubResources(dimensions, array_size, noise_parameters);
    rhi::Texture texture_array = render_context.CreateTexture(
        rhi::TextureSettings::ForImage(dimensions, array_size, gfx::PixelFormat::RGBA8Unorm, mipmapped));
    texture_array.SetData(render_cmd_queue, sub_resources);
    return texture_array;
}

rhi::SubResources Asteroid::GenerateTextureArraySubResources(const gfx::Dimensions& dimensions, uint32_t array_size,
                                                             const TextureNoiseParameters& noise_parameters)
{
    META_FUNCTION_TASK();
    const gfx::PixelFormat pixel_format = gfx::PixelFormat::RGBA8Unorm;
    const uint32_t         pixel_size   = gfx::GetPixelSize(pixel_format);
    const uint32_t         pixels_count = dimensions.GetPixelsCount();
    const uint32_t         row_stride   = pixel_size * dimensions.GetWidth();

    rhi::SubResources sub_resources;
    sub_resources.reserve(array_size);

    std::mt19937 rng(noise_parameters.random_seed); // NOSONAR - using pseudorandom generator is safe here
    std::uniform_int_distribution<int> noise_seed_distribution(0, 10000);

    for (uint32_t array_index = 0; array_index < array_size; ++array_index)
    {
        Data::Bytes sub_resource_data(static_cast<size_t>(pixels_count) * pixel_size, std::byte(255));
        FillPerlinNoiseToTexture(sub_resource_data, dimensions, row_stride, noise_parameters);

        sub_resources.emplace_back(std::move(sub_resource_data), rhi::SubResource::Index{ 0, array_index });
    }

    return sub_resources;
}

Asteroid::Colors Asteroid::GetAsteroidRockColors(uint32_t deep_color_index, uint32_t shallow_color_index)
{
    META_FUNCTION_TASK();

    static const AsteroidColorSchema s_srgb_deep_rock_colors{ {
        { uint8_t( 55), uint8_t( 49), uint8_t( 40) },
        { uint8_t( 58), uint8_t( 38), uint8_t( 14) },
        { uint8_t( 98), uint8_t(101), uint8_t(104) },
        { uint8_t(172), uint8_t(158), uint8_t(122) },
        { uint8_t( 88), uint8_t( 88), uint8_t( 88) },
        { uint8_t(148), uint8_t(108), uint8_t(102) },
    } };
    static const AsteroidColorSchema s_linear_deep_rock_colors = TransformSrgbToLinear(s_srgb_deep_rock_colors);

    static const AsteroidColorSchema s_srgb_shallow_rock_colors{ {
        { uint8_t(140), uint8_t(109), uint8_t( 61) },
        { uint8_t(172), uint8_t(154), uint8_t( 58) },
        { uint8_t(204), uint8_t(177), uint8_t(119) },
        { uint8_t(204), uint8_t(164), uint8_t(136) },
        { uint8_t(130), uint8_t(117), uint8_t( 98) },
        { uint8_t(160), uint8_t(145), uint8_t(114) },
    } };
    static const AsteroidColorSchema s_linear_shallow_rock_colors = TransformSrgbToLinear(s_srgb_shallow_rock_colors);

    META_CHECK_LESS(deep_color_index, s_linear_deep_rock_colors.size());
    META_CHECK_LESS(shallow_color_index, s_linear_shallow_rock_colors.size());
    return Asteroid::Colors{ s_linear_deep_rock_colors[deep_color_index], s_linear_shallow_rock_colors[shallow_color_index] };
}

Asteroid::Colors Asteroid::GetAsteroidIceColors(uint32_t deep_color_index, uint32_t shallow_color_index)
{
    META_FUNCTION_TASK();

    static const AsteroidColorSchema s_srgb_deep_ice_colors{ {
        { uint8_t(22), uint8_t( 51), uint8_t( 59) },
        { uint8_t(45), uint8_t( 72), uint8_t( 93) },
        { uint8_t(14), uint8_t( 25), uint8_t( 27) },
        { uint8_t(68), uint8_t(103), uint8_t(129) },
        { uint8_t(29), uint8_t( 59), uint8_t( 59) },
        { uint8_t(59), uint8_t( 92), uint8_t(118) }
    } };
    static const AsteroidColorSchema s_linear_deep_ice_colors = TransformSrgbToLinear(s_srgb_deep_ice_colors);

    static const AsteroidColorSchema s_srgb_shallow_ice_colors{ {
        { uint8_t(144), uint8_t(163), uint8_t(188) },
        { uint8_t(133), uint8_t(179), uint8_t(189) },
        { uint8_t( 74), uint8_t(135), uint8_t(178) },
        { uint8_t( 69), uint8_t(143), uint8_t(177) },
        { uint8_t(104), uint8_t(168), uint8_t(185) },
        { uint8_t(140), uint8_t(170), uint8_t(186) }
    } };
    static const AsteroidColorSchema s_linear_shallow_ice_colors = TransformSrgbToLinear(s_srgb_shallow_ice_colors);

    META_CHECK_LESS(deep_color_index, s_linear_deep_ice_colors.size());
    META_CHECK_LESS(shallow_color_index, s_linear_shallow_ice_colors.size());
    return Asteroid::Colors{ s_linear_deep_ice_colors[deep_color_index], s_linear_shallow_ice_colors[shallow_color_index] };
}

Asteroid::Colors Asteroid::GetAsteroidLodColors(uint32_t lod_index)
{
    META_FUNCTION_TASK();
    static const AsteroidColorSchema s_srgb_lod_deep_colors{ {
        {  uint8_t(  0), uint8_t(128), uint8_t(  0) }, // LOD-0: green
        {  uint8_t(  0), uint8_t( 64), uint8_t(128) }, // LOD-1: blue
        {  uint8_t( 96), uint8_t(  0), uint8_t(128) }, // LOD-2: purple
        {  uint8_t(128), uint8_t(  0), uint8_t(  0) }, // LOD-3: red
        {  uint8_t(128), uint8_t(128), uint8_t(  0) }, // LOD-4: yellow
        {  uint8_t(128), uint8_t( 64), uint8_t(  0) }, // LOD-5: orange
    } };
    static const AsteroidColorSchema s_linear_lod_deep_colors = TransformSrgbToLinear(s_srgb_lod_deep_colors);

    static const AsteroidColorSchema s_srgb_lod_shallow_colors{ {
        {  uint8_t(  0), uint8_t(255), uint8_t(  0) }, // LOD-0: green
        {  uint8_t(  0), uint8_t(128), uint8_t(255) }, // LOD-1: blue
        {  uint8_t(196), uint8_t(  0), uint8_t(255) }, // LOD-2: purple
        {  uint8_t(255), uint8_t(  0), uint8_t(  0) }, // LOD-3: red
        {  uint8_t(255), uint8_t(255), uint8_t(  0) }, // LOD-4: yellow
        {  uint8_t(255), uint8_t(128), uint8_t(  0) }, // LOD-5: orange
    } };
    static const AsteroidColorSchema s_linear_lod_shallow_colors = TransformSrgbToLinear(s_srgb_lod_shallow_colors);

    META_CHECK_LESS(lod_index, s_linear_lod_deep_colors.size());
    META_CHECK_LESS(lod_index, s_linear_lod_shallow_colors.size());
    return Asteroid::Colors{ s_linear_lod_deep_colors[lod_index], s_linear_lod_shallow_colors[lod_index] };
}

void Asteroid::FillPerlinNoiseToTexture(Data::Bytes& texture_data, const gfx::Dimensions& dimensions, uint32_t row_stride,
                                        const TextureNoiseParameters& noise_parameters)
{
    META_FUNCTION_TASK();
    META_CHECK_NOT_ZERO(dimensions.GetWidth());
    META_CHECK_NOT_ZERO(dimensions.GetHeight());
    META_CHECK_GREATER_OR_EQUAL(row_stride, dimensions.GetWidth() * 3U);
    META_CHECK_GREATER_OR_EQUAL(texture_data.size(), static_cast<size_t>(row_stride) * dimensions.GetHeight());

    // Fractal Brownian Motion of the Perlin (Simplex) noise:
    // fractal parameters are randomized per texture, so the node tree can not be shared and is created
    // for each texture; node creation and generation are both thread-safe, which is required because
    // asteroid textures are generated in parallel tasks.
    auto simplex_noise_ptr = FastNoise::New<FastNoise::Simplex>();
    simplex_noise_ptr->SetScale(1.F); // noise feature size in generation coordinates
    simplex_noise_ptr->SetOutputMin(0.F);
    simplex_noise_ptr->SetOutputMax(1.F);

    auto fbm_noise_ptr = FastNoise::New<FastNoise::FractalFBm>();
    fbm_noise_ptr->SetSource(simplex_noise_ptr);
    fbm_noise_ptr->SetOctaveCount(noise_parameters.octave_count);
    fbm_noise_ptr->SetGain(noise_parameters.gain);
    fbm_noise_ptr->SetLacunarity(noise_parameters.lacunarity);
    fbm_noise_ptr->SetWeightedStrength(noise_parameters.fractal_weight);

    const uint32_t width  = dimensions.GetWidth();
    const uint32_t height = dimensions.GetHeight();

    // Generate noise for all texels at once with a batched SIMD-accelerated call;
    // step sizes keep the number of base octave features independent of the texture resolution.
    std::vector<float> noise_values(static_cast<size_t>(width) * height);
    const FastNoise::OutputMinMax noise_min_max = fbm_noise_ptr->GenUniformGrid2D(
        noise_values.data(), 0.F, 0.F,
        static_cast<int>(width), static_cast<int>(height),
        1.f / noise_parameters.scale,
        1.f / noise_parameters.scale,
        noise_parameters.random_seed);

    // FBM output range depends on the octaves count and gain,
    // so generated values are normalized to the [0, 255] color channel range with the actually generated range.
    const float noise_range      = noise_min_max.max - noise_min_max.min;
    const float noise_multiplier = noise_range > 0.F ? 255.F / noise_range : 0.F;
    const uint32_t pixel_stride  = row_stride / width;

    for (uint32_t row = 0; row < height; ++row)
    {
        std::byte*   row_data       = texture_data.data() + static_cast<size_t>(row) * row_stride;
        const float* row_noise_data = noise_values.data() + static_cast<size_t>(row) * width;

        for (uint32_t col = 0; col < width; ++col)
        {
            const float noise_intensity = (row_noise_data[col] - noise_min_max.min) * noise_multiplier;
            const auto  channel_value   = static_cast<std::byte>(static_cast<uint8_t>(std::min(255.F, noise_intensity)));

            std::byte* texel_data = row_data + static_cast<size_t>(col) * pixel_stride;
            texel_data[0] = channel_value; // Red
            texel_data[1] = channel_value; // Green
            texel_data[2] = channel_value; // Blue
        }
    }
}

} // namespace Methane::Samples
