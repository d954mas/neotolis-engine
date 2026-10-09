/* Test-only storage adapter. The actual geometry algorithms are upstream files. */
#pragma once
#include <Methane/Checks.hpp>
#include <Methane/Instrumentation.h>
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <hlsl++/vector_float.h>
#include <map>
#include <vector>

namespace Methane::Data {
using Size = uint32_t;
using Index = uint32_t;
using ConstRawPtr = const std::byte *;
} // namespace Methane::Data
namespace Methane::Graphics {
struct Raw3 {
    float x = 0.F, y = 0.F, z = 0.F;
    Raw3() = default;
    Raw3(float a, float b, float c) : x(a), y(b), z(c) {}
    explicit Raw3(const hlslpp::float3 &v) : x(v.x), y(v.y), z(v.z) {}
    hlslpp::float3 AsHlsl() const { return {x, y, z}; }
    float GetX() const { return x; }
    float GetY() const { return y; }
    float GetZ() const { return z; }
    void SetX(float v) { x = v; }
    void SetY(float v) { y = v; }
    void SetZ(float v) { z = v; }
    float GetLength() const {
        float sum = 0.F;
        sum += x * x;
        sum += y * y;
        sum += z * z;
        return std::sqrt(sum);
    }
    Raw3 &operator*=(float scale) {
        x *= scale;
        y *= scale;
        z *= scale;
        return *this;
    }
};
struct Raw2 {
    float x = 0.F, y = 0.F;
    Raw2() = default;
    Raw2(float a, float b) : x(a), y(b) {}
    float operator[](size_t i) const { return i == 0 ? x : y; }
    explicit Raw2(const hlslpp::float2 &v) : x(v.x), y(v.y) {}
    hlslpp::float2 AsHlsl() const { return {x, y}; }
    float GetX() const { return x; }
    float GetY() const { return y; }
    void SetX(float v) { x = v; }
    void SetY(float v) { y = v; }
};
class Mesh {
  public:
    using Position = Raw3;
    using Position2D = Raw2;
    using Normal = Raw3;
    using Color = Raw3;
    using TexCoord = Raw2;
    using Index = uint16_t;
    using Indices = std::vector<Index>;
    enum class Type { Icosahedron, Sphere, Cube, Quad };
    enum class VertexField { Position, Normal, TexCoord, Color };
    using VertexLayout = std::vector<VertexField>;
    Mesh(Type, const VertexLayout &layout) : layout_(layout) {}
    virtual ~Mesh() = default;
    virtual Data::Size GetVertexCount() const noexcept = 0;
    virtual Data::Size GetVertexDataSize() const noexcept = 0;
    virtual Data::ConstRawPtr GetVertexData() const noexcept = 0;
    Data::Size GetVertexSize() const {
        Data::Size size = 0;
        for (VertexField field : layout_)
            size += field == VertexField::TexCoord ? 8U : 12U;
        return size;
    }
    Data::Size GetIndexCount() const { return static_cast<Data::Size>(indices_.size()); }
    Index GetIndex(Data::Index i) const { return indices_.at(i); }
    const Indices &GetIndices() const { return indices_; }

  protected:
    using HlslPosition = hlslpp::float3;
    using HlslNormal = hlslpp::float3;
    using HlslColor = hlslpp::float3;
    using HlslTexCoord = hlslpp::float2;
    struct Edge {
        Index first_index, second_index;
        Edge(Index a, Index b) : first_index(std::min(a, b)), second_index(std::max(a, b)) {}
        friend auto operator<=>(const Edge &, const Edge &) = default;
    };
    bool HasVertexField(VertexField field) const { return std::find(layout_.begin(), layout_.end(), field) != layout_.end(); }
    void CheckLayoutHasVertexField(VertexField field) const {
        if (!HasVertexField(field))
            throw std::runtime_error("reference layout");
    }
    int32_t GetVertexFieldOffset(VertexField field) const {
        int32_t offset = 0;
        for (VertexField f : layout_) {
            if (f == field)
                return offset;
            offset += f == VertexField::TexCoord ? 8 : 12;
        }
        throw std::runtime_error("missing field");
    }
    void ResizeIndices(size_t count) { indices_.resize(count, 0); }
    void SetIndex(Data::Index index, Index value) { indices_.at(index) = value; }
    auto GetIndicesBackInserter() { return std::back_inserter(indices_); }
    static Data::Size GetFacePositionCount() { return 4; }
    static Mesh::Index GetFaceIndicesCount() { return 6; }
    static Mesh::Index GetFaceIndex(size_t i) {
        static const std::array<Index, 6> values{0, 1, 2, 0, 2, 3};
        return values.at(i);
    }
    static const Position2D &GetFacePosition2D(size_t i) {
        static const std::array<Position2D, 4> values{{{-0.5F, -0.5F}, {-0.5F, 0.5F}, {0.5F, 0.5F}, {0.5F, -0.5F}}};
        return values.at(i);
    }
    static const TexCoord &GetFaceTexCoord(size_t i) {
        static const std::array<TexCoord, 4> values{{{0.F, 1.F}, {0.F, 0.F}, {1.F, 0.F}, {1.F, 1.F}}};
        return values.at(i);
    }
    static Data::Size GetColorsCount() { return 6; }
    static const Color &GetColor(size_t i) {
        static const std::array<Color, 6> values{{{1.F, 0.F, 0.F}, {0.F, 1.F, 0.F}, {0.F, 0.F, 1.F}, {1.F, 0.F, 1.F}, {0.F, 1.F, 1.F}, {1.F, 1.F, 0.F}}};
        return values.at(i);
    }
    void SetIndices(Indices &&indices) { indices_ = std::move(indices); }
    void SwapIndices(Indices &indices) { indices_.swap(indices); }

  private:
    VertexLayout layout_;
    Indices indices_;
};
} // namespace Methane::Graphics
