#pragma once

#include <DirectXCollision.h>
#include <array>
#include <chrono>
#include <numeric>
#include <vector>
#include <algorithm>
#include <cmath>
#include <stdexcept>

struct SceneObject
{
    DirectX::XMFLOAT3 Position = { 0, 0, 0 };
    DirectX::XMFLOAT3 Rotation = { 0, 0, 0 }; // Pitch, yaw, roll in radians.
    DirectX::XMFLOAT3 Scale = { 1, 1, 1 };
    DirectX::BoundingBox LocalBounds = { { 0, 0, 0 }, { 1, 1, 1 } };
};

struct InstanceData
{
    DirectX::XMFLOAT4X4 World;
    DirectX::XMFLOAT4X4 NormalWorld;
};
static_assert(sizeof(InstanceData) == 128);

enum class CullingMode { None, Linear, Octree };

struct CullingStats
{
    size_t Visible = 0;
    size_t ObjectTests = 0;
    size_t NodeTests = 0;
    double Milliseconds = 0.0;
};

class CullingScene
{
public:
    static constexpr unsigned ObjectCount = 4096;
    std::vector<SceneObject> Objects;
    std::vector<InstanceData> Instances;
    std::vector<DirectX::BoundingBox> Bounds;
    std::vector<unsigned> VisibleIds;
    CullingStats Stats;

    void Build()
    {
        Objects.clear();
        VisibleIds.reserve(ObjectCount);
        // A deterministic 16^3 grid keeps comparisons between modes reproducible.
        for (unsigned y = 0; y < 16; ++y)
            for (unsigned z = 0; z < 16; ++z)
                for (unsigned x = 0; x < 16; ++x)
                {
                    const float radius = 0.6f + 0.15f * ((x + 3 * y + 7 * z) % 5);
                    DirectX::XMFLOAT3 center((x - 7.5f) * 8.0f,
                        (y - 7.5f) * 5.0f, (z - 7.5f) * 8.0f);
                    SceneObject object;
                    object.Position = center;
                    object.Scale = { radius, radius, radius };
                    object.Rotation.y = 0.25f * ((x + 3 * y + 7 * z) % 24);
                    Objects.push_back(object);
                }
        Rebuild();
    }

    
    void Rebuild()
    {
        using namespace DirectX;
        Instances.clear();
        Bounds.clear();
        VisibleIds.clear();
        mNodes.clear();
        Stats = {};
        for (const auto& object : Objects)
        {
            if (!std::isfinite(object.Scale.x) || !std::isfinite(object.Scale.y) ||
                !std::isfinite(object.Scale.z) || std::abs(object.Scale.x) < 1e-6f ||
                std::abs(object.Scale.y) < 1e-6f || std::abs(object.Scale.z) < 1e-6f)
                throw std::invalid_argument("Object scale must be finite and non-zero");
            const XMMATRIX world = XMMatrixScaling(object.Scale.x, object.Scale.y, object.Scale.z) *
                XMMatrixRotationRollPitchYaw(object.Rotation.x, object.Rotation.y, object.Rotation.z) *
                XMMatrixTranslation(object.Position.x, object.Position.y, object.Position.z);
            InstanceData data;
            // Vertex attributes store matrix rows directly; no constant-buffer transpose is needed.
            XMStoreFloat4x4(&data.World, world);
            XMStoreFloat4x4(&data.NormalWorld, XMMatrixTranspose(XMMatrixInverse(nullptr, world)));
            Instances.push_back(data);
            BoundingBox bounds;
            object.LocalBounds.Transform(bounds, world);
            Bounds.push_back(bounds);
        }
        if (Bounds.empty())
            return;
        BoundingBox root = Bounds.front();
        for (size_t i = 1; i < Bounds.size(); ++i)
            BoundingBox::CreateMerged(root, root, Bounds[i]);
        // Keep the octree root cubic and include a small margin for rounding at its faces.
        const float extent = (std::max)({ root.Extents.x, root.Extents.y, root.Extents.z, 0.001f });
        const float padded = extent + (std::max)(0.001f, extent * 1e-5f);
        root.Extents = { padded, padded, padded };
        std::vector<unsigned> ids(Instances.size());
        std::iota(ids.begin(), ids.end(), 0u);
        BuildNode(root, ids, 0);
    }

    void Select(const DirectX::BoundingFrustum& frustum, CullingMode mode)
    {
        const auto start = std::chrono::steady_clock::now();
        Stats = {};
        VisibleIds.clear();
        if (mode == CullingMode::None)
        {
            VisibleIds.resize(Instances.size());
            std::iota(VisibleIds.begin(), VisibleIds.end(), 0u);
        }
        else if (mode == CullingMode::Linear)
        {
            for (unsigned id = 0; id < Bounds.size(); ++id)
                TestObject(frustum, id);
        }
        else if (!mNodes.empty())
            Visit(frustum, 0);
        Stats.Visible = VisibleIds.size();
        Stats.Milliseconds = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start).count();
    }

    size_t NodeCount() const { return mNodes.size(); }
    DirectX::BoundingBox RootBounds() const { return mNodes.empty() ? DirectX::BoundingBox{} : mNodes.front().Box; }

private:
    struct Node
    {
        DirectX::BoundingBox Box;
        std::vector<unsigned> Objects;
        std::array<int, 8> Children = { -1, -1, -1, -1, -1, -1, -1, -1 };
    };
    std::vector<Node> mNodes;

    int BuildNode(const DirectX::BoundingBox& box, const std::vector<unsigned>& ids, unsigned depth)
    {
        const int index = static_cast<int>(mNodes.size());
        mNodes.push_back({ box });
        if (ids.size() <= 16 || depth >= 6)
        {
            mNodes[index].Objects = ids;
            return index;
        }
        std::array<DirectX::BoundingBox, 8> children;
        std::array<std::vector<unsigned>, 8> groups;
        const DirectX::XMFLOAT3 half(box.Extents.x * 0.5f,
            box.Extents.y * 0.5f, box.Extents.z * 0.5f);
        for (unsigned c = 0; c < 8; ++c)
            children[c] = DirectX::BoundingBox({
                box.Center.x + ((c & 1) ? half.x : -half.x),
                box.Center.y + ((c & 2) ? half.y : -half.y),
                box.Center.z + ((c & 4) ? half.z : -half.z) }, half);
        for (unsigned id : ids)
        {
            bool placed = false;
            for (unsigned c = 0; c < 8; ++c)
                if (children[c].Contains(Bounds[id]) == DirectX::CONTAINS)
                {
                    groups[c].push_back(id);
                    placed = true;
                    break;
                }
            // Straddling objects stay in the parent: never discard part of their bounds.
            if (!placed)
                mNodes[index].Objects.push_back(id);
        }
        for (unsigned c = 0; c < 8; ++c)
            if (!groups[c].empty())
            {
                const int child = BuildNode(children[c], groups[c], depth + 1);
                mNodes[index].Children[c] = child;
            }
        return index;
    }

    void TestObject(const DirectX::BoundingFrustum& frustum, unsigned id)
    {
        ++Stats.ObjectTests;
        if (frustum.Contains(Bounds[id]) != DirectX::DISJOINT)
            VisibleIds.push_back(id);
    }

    void Collect(int index)
    {
        const Node& node = mNodes[index];
        VisibleIds.insert(VisibleIds.end(), node.Objects.begin(), node.Objects.end());
        for (int child : node.Children)
            if (child >= 0)
                Collect(child);
    }

    void Visit(const DirectX::BoundingFrustum& frustum, int index)
    {
        ++Stats.NodeTests;
        const auto relation = frustum.Contains(mNodes[index].Box);
        if (relation == DirectX::DISJOINT)
            return;
        if (relation == DirectX::CONTAINS)
        {
            Collect(index);
            return;
        }
        for (unsigned id : mNodes[index].Objects)
            TestObject(frustum, id);
        for (int child : mNodes[index].Children)
            if (child >= 0)
                Visit(frustum, child);
    }
};
