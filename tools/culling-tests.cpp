#include "../Lab_4/Project1/CullingScene.h"
#include <algorithm>
#include <iostream>
#include <random>

int main()
{
    using namespace DirectX;
    CullingScene scene;
    scene.Build();
    std::mt19937 rng(42);
    std::uniform_real_distribution<float> position(-160.0f, 160.0f);
    size_t linearTests = 0, treeTests = 0;
    for (int sample = 0; sample < 250; ++sample)
    {
        const XMVECTOR eye = XMVectorSet(position(rng), position(rng), position(rng), 1);
        const XMVECTOR target = XMVectorSet(position(rng), position(rng), position(rng), 1);
        const XMMATRIX view = XMMatrixLookAtLH(eye, target, XMVectorSet(0, 1, 0, 0));
        const XMMATRIX proj = XMMatrixPerspectiveFovLH(0.4f + (sample % 10) * 0.15f,
            sample % 2 ? 16.0f / 9.0f : 9.0f / 16.0f, 0.1f, 50.0f + sample);
        BoundingFrustum local, world;
        BoundingFrustum::CreateFromMatrix(local, proj);
        local.Transform(world, XMMatrixInverse(nullptr, view));
        scene.Select(world, CullingMode::Linear);
        auto reference = scene.VisibleIds;
        linearTests += scene.Stats.ObjectTests;
        scene.Select(world, CullingMode::Octree);
        treeTests += scene.Stats.ObjectTests + scene.Stats.NodeTests;
        auto actual = scene.VisibleIds;
        std::sort(actual.begin(), actual.end());
        if (reference != actual)
        {
            std::cerr << "Visibility mismatch at camera " << sample << '\n';
            return 1;
        }
        scene.Select(world, CullingMode::None);
        if (scene.VisibleIds.size() != CullingScene::ObjectCount || scene.Stats.ObjectTests != 0)
            return 2;
    }
    // A frustum looking away from the complete scene must reject the root.
    BoundingFrustum outside;
    BoundingFrustum::CreateFromMatrix(outside, XMMatrixPerspectiveFovLH(XM_PIDIV4, 1, 0.1f, 10));
    outside.Origin = { 1000, 1000, 1000 };
    scene.Select(outside, CullingMode::Octree);
    if (!scene.VisibleIds.empty() || scene.Stats.NodeTests != 1 || scene.Stats.ObjectTests != 0)
        return 3;
    // A very wide distant frustum contains the root and accepts its entire subtree.
    BoundingFrustum enclosing;
    BoundingFrustum::CreateFromMatrix(enclosing, XMMatrixPerspectiveFovLH(XM_PIDIV2, 1, 0.1f, 2000));
    enclosing.Origin = { 0, 0, -1000 };
    scene.Select(enclosing, CullingMode::Octree);
    if (scene.VisibleIds.size() != CullingScene::ObjectCount || scene.Stats.ObjectTests != 0)
        return 4;
    if (treeTests >= linearTests)
        return 5;
    for (auto& object : scene.Objects)
    {
        object.Position.x += 1500;
        object.Position.y -= 300;
        object.Scale = { 0.4f, 2.0f, 1.3f };
        object.Rotation = { 0.3f, 0.7f, 0.2f };
    }
    scene.Rebuild();
    for (size_t i = 0; i < scene.Objects.size(); ++i)
    {
        if (scene.RootBounds().Contains(scene.Bounds[i]) != CONTAINS)
            return 6;
        XMFLOAT3 corners[8];
        scene.Objects[i].LocalBounds.GetCorners(corners);
        const XMMATRIX world = XMLoadFloat4x4(&scene.Instances[i].World);
        for (const auto& corner : corners)
        {
            XMFLOAT3 p;
            XMStoreFloat3(&p, XMVector3TransformCoord(XMLoadFloat3(&corner), world));
            const auto& bounds = scene.Bounds[i];
            if (std::abs(p.x - bounds.Center.x) > bounds.Extents.x + 0.001f ||
                std::abs(p.y - bounds.Center.y) > bounds.Extents.y + 0.001f ||
                std::abs(p.z - bounds.Center.z) > bounds.Extents.z + 0.001f)
                return 7;
        }
        const XMVECTOR n = XMVector3TransformNormal(XMVectorSet(1, 1, 0, 0),
            XMLoadFloat4x4(&scene.Instances[i].NormalWorld));
        const XMVECTOR t = XMVector3TransformNormal(XMVectorSet(1, -1, 0, 0), world);
        if (std::abs(XMVectorGetX(XMVector3Dot(n, t))) > 0.001f)
            return 8;
    }
    for (int sample = 0; sample < 100; ++sample)
    {
        BoundingFrustum frustum;
        BoundingFrustum::CreateFromMatrix(frustum, XMMatrixPerspectiveFovLH(XM_PIDIV4, 1.7f, 0.1f, 400));
        frustum.Origin = { 1500 + position(rng), -300 + position(rng), position(rng) };
        scene.Select(frustum, CullingMode::Linear);
        const auto expected = scene.VisibleIds;
        scene.Select(frustum, CullingMode::Octree);
        auto actual = scene.VisibleIds;
        std::sort(actual.begin(), actual.end());
        if (actual != expected)
            return 9;
    }
    std::cout << "PASS: 250 cameras (landscape/portrait), off mode, rejected/contained root.\n"
        << "Objects: " << scene.Instances.size() << ", nodes: " << scene.NodeCount()
        << ", linear tests: " << linearTests << ", tree tests: " << treeTests << '\n';
    scene.Objects.clear();
    scene.Rebuild();
    for (auto mode : { CullingMode::None, CullingMode::Linear, CullingMode::Octree })
    {
        scene.Select(enclosing, mode);
        if (!scene.VisibleIds.empty() || scene.NodeCount() != 0)
            return 10;
    }
    std::cout << "PASS: moved scene, 100 extra cameras, transformed bounds, normal matrix, empty scene.\n";
}
