/* Source-derived offline verification; upstream methods retain their notices. */
#define main asteroids_reference_export_main
#include "main.cpp"
#undef main
#include "upstream/CubeMesh.hpp"
#include "upstream/IcosahedronMesh.hpp"
#include "upstream/SphereMesh.hpp"

struct ReferenceVertex {
    Methane::Graphics::Mesh::Position position;
    Methane::Graphics::Mesh::Normal normal;
};
class ReferenceMesh : public Methane::Graphics::IcosahedronMesh<ReferenceVertex> {
  public:
    using Vertex = ReferenceVertex;
    using Mesh = ReferenceMesh;
    explicit ReferenceMesh(uint32_t subdivision)
        : Methane::Graphics::IcosahedronMesh<ReferenceVertex>({Methane::Graphics::Mesh::VertexField::Position, Methane::Graphics::Mesh::VertexField::Normal}, 0.5F, subdivision, true) {
        Spherify();
    }
    void Randomize(uint32_t random_seed);
    std::pair<float, float> m_depth_range;
};
#include "original_randomize.inl"

static bool same_bits(float a, float b) { return std::bit_cast<uint32_t>(a) == std::bit_cast<uint32_t>(b); }

int main() {
    using GfxMesh = Methane::Graphics::Mesh;
    struct PlanetVertex {
        GfxMesh::Position position;
        GfxMesh::Normal normal;
        GfxMesh::TexCoord texcoord;
    };
    const Methane::Graphics::SphereMesh<PlanetVertex> reference_planet({GfxMesh::VertexField::Position, GfxMesh::VertexField::Normal, GfxMesh::VertexField::TexCoord}, 1.F, 32, 32);
    const Mesh planet = planet_mesh();
    require(planet.indices == reference_planet.GetIndices(), "source planet topology differs");
    require(planet.vertices.size() == reference_planet.GetVertices().size(), "source planet vertex count differs");
    for (size_t i = 0; i < planet.vertices.size(); ++i) {
        const auto &a = planet.vertices[i];
        const auto &e = reference_planet.GetVertices()[i];
        require(same_bits(a.position.x, e.position.x) && same_bits(a.position.y, e.position.y) && same_bits(a.position.z, e.position.z), "source planet position differs");
        require(same_bits(a.normal.x, e.normal.x) && same_bits(a.normal.y, e.normal.y) && same_bits(a.normal.z, e.normal.z), "source planet normal differs");
        require(same_bits(planet.texcoords[i][0], e.texcoord.x) && same_bits(planet.texcoords[i][1], e.texcoord.y), "source planet texcoord differs");
    }
    struct SkyVertex {
        GfxMesh::Position position;
    };
    const Methane::Graphics::CubeMesh<SkyVertex> reference_sky({GfxMesh::VertexField::Position});
    const Mesh sky = skybox_mesh();
    require(sky.indices == reference_sky.GetIndices(), "source sky topology differs");
    require(sky.vertices.size() == reference_sky.GetVertices().size(), "source sky vertex count differs");
    for (size_t i = 0; i < sky.vertices.size(); ++i) {
        const auto &a = sky.vertices[i].position;
        const auto &e = reference_sky.GetVertices()[i].position;
        require(same_bits(a.x, e.x) && same_bits(a.y, e.y) && same_bits(a.z, e.z), "source sky position differs");
    }
    std::cout << "PASS: unmodified source planet and skybox geometry parity\n";

    for (uint32_t unique : {35U, 50U, 75U, 100U, 200U, 300U, 400U, 500U, 750U, 1000U}) {
        std::mt19937 rng(1123);
        for (uint32_t subdivision = 0; subdivision < 4; ++subdivision) {
            const Mesh base = base_mesh(subdivision);
            for (uint32_t variant = 0; variant < unique; ++variant) {
                const uint32_t seed = rng();
                Mesh actual = base;
                randomize(actual, seed);
                ReferenceMesh expected(subdivision);
                expected.Randomize(seed);
                require(actual.indices == expected.GetIndices(), "original topology differs");
                require(actual.vertices.size() == expected.GetVertices().size(), "original vertex count differs");
                require(same_bits(actual.depth_min, expected.m_depth_range.first) && same_bits(actual.depth_max, expected.m_depth_range.second), "original depth range differs");
                for (size_t i = 0; i < actual.vertices.size(); ++i) {
                    const auto &a = actual.vertices[i];
                    const auto &e = expected.GetVertices()[i];
                    require(same_bits(a.position.x, e.position.x) && same_bits(a.position.y, e.position.y) && same_bits(a.position.z, e.position.z), "original position differs");
                    require(same_bits(a.normal.x, e.normal.x) && same_bits(a.normal.y, e.normal.y) && same_bits(a.normal.z, e.normal.z), "original averaged normal differs");
                }
            }
        }
        std::cout << "PASS: unmodified upstream geometry/noise parity, unique=" << unique << '\n';
    }
}
