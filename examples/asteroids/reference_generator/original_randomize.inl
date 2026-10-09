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

// Test adapter change: enclosing class renamed; function body unchanged.
void ReferenceMesh::Randomize(uint32_t random_seed)
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
