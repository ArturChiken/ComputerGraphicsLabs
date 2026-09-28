#pragma once
#include <DirectXCollision.h>
#include <array>
#include <algorithm>
#include <cmath>
#include <cfloat>

struct ShadowSettings
{
    bool Enabled = true;
    bool ShowCascades = false;
    bool ShowGround = true;
    unsigned PcfRadius = 1; // 0: hard shadow, 1: 3x3 PCF, 2: 5x5 PCF.
    float SplitLambda = 0.7f;
    float Distance = 80.0f;
};

struct ShadowCascades
{
    static constexpr unsigned Count = 4;
    static constexpr unsigned Resolution = 2048;
    static constexpr float BlendFraction = 0.1f;
    std::array<float, Count> Splits = {};
    std::array<DirectX::XMFLOAT4X4, Count> ViewProjection = {};
    float Near = 0.1f;

    static std::array<float, Count> SplitDistances(float nearZ, float farZ, float lambda)
    {
        std::array<float, Count> result;
        lambda = (std::clamp)(lambda, 0.0f, 1.0f);
        for (unsigned i = 0; i < Count; ++i)
        {
            const float fraction = float(i + 1) / Count;
            const float linear = nearZ + (farZ - nearZ) * fraction;
            const float logarithmic = nearZ * std::pow(farZ / nearZ, fraction);
            result[i] = linear * (1 - lambda) + logarithmic * lambda;
        }
        result.back() = farZ;
        return result;
    }

    void Update(const DirectX::XMFLOAT4X4& view, const DirectX::XMFLOAT4X4& projection,
        const DirectX::XMFLOAT3& lightDirection, const DirectX::BoundingBox& casters,
        const ShadowSettings& settings)
    {
        using namespace DirectX;
        Near = -projection._43 / projection._33;
        const float cameraFar = -projection._43 / (projection._33 - 1.0f);
        const float farZ = (std::clamp)(settings.Distance, Near + 0.1f, cameraFar);
        Splits = SplitDistances(Near, farZ, settings.SplitLambda);
        const XMMATRIX inverseView = XMMatrixInverse(nullptr, XMLoadFloat4x4(&view));
        const XMVECTOR direction = XMVector3Normalize(XMLoadFloat3(&lightDirection));
        const XMVECTOR up = std::abs(XMVectorGetY(direction)) > 0.95f ?
            XMVectorSet(0, 0, 1, 0) : XMVectorSet(0, 1, 0, 0);
        // A fixed, rotation-only light view makes texel snapping effective in world space.
        const XMMATRIX lightView = XMMatrixLookToLH(XMVectorZero(), direction, up);
        XMFLOAT3 casterCorners[8];
        casters.GetCorners(casterCorners);
        float casterMinZ = FLT_MAX, casterMaxZ = -FLT_MAX;
        for (const auto& corner : casterCorners)
        {
            const float z = XMVectorGetZ(XMVector3TransformCoord(XMLoadFloat3(&corner), lightView));
            casterMinZ = (std::min)(casterMinZ, z);
            casterMaxZ = (std::max)(casterMaxZ, z);
        }
        for (unsigned cascade = 0; cascade < Count; ++cascade)
        {
            float start = cascade == 0 ? Near : Splits[cascade - 1];
            // Include the previous cascade's blend band in the next map.
            if (cascade > 0)
            {
                const float previousStart = cascade == 1 ? Near : Splits[cascade - 2];
                start -= (start - previousStart) * BlendFraction;
            }
            std::array<XMVECTOR, 8> corners;
            XMVECTOR center = XMVectorZero();
            unsigned index = 0;
            for (float z : { start, Splits[cascade] })
                for (float y : { -1.0f, 1.0f })
                    for (float x : { -1.0f, 1.0f })
                    {
                        corners[index] = XMVector3TransformCoord(XMVectorSet(
                            x * z / projection._11, y * z / projection._22, z, 1), inverseView);
                        center += corners[index++];
                    }
            center *= 0.125f;
            float radius = 0;
            for (const auto& corner : corners)
                radius = (std::max)(radius, XMVectorGetX(XMVector3Length(corner - center)));
            radius = std::ceil(radius * 16.0f) / 16.0f;
            // Extra texels cover both snapping and the largest PCF kernel at map borders.
            radius *= float(Resolution) / float(Resolution - 8);
            const float texelSize = 2 * radius / Resolution;
            const XMVECTOR centerL = XMVector3TransformCoord(center, lightView);
            const float cx = std::round(XMVectorGetX(centerL) / texelSize) * texelSize;
            const float cy = std::round(XMVectorGetY(centerL) / texelSize) * texelSize;
            float minZ = casterMinZ, maxZ = casterMaxZ;
            for (const auto& corner : corners)
            {
                const float z = XMVectorGetZ(XMVector3TransformCoord(corner, lightView));
                minZ = (std::min)(minZ, z);
                maxZ = (std::max)(maxZ, z);
            }
            // Include off-camera casters in depth, not only the receiver slice.
            const XMMATRIX lightProjection = XMMatrixOrthographicOffCenterLH(
                cx - radius, cx + radius, cy - radius, cy + radius, minZ - 1, maxZ + 1);
            XMStoreFloat4x4(&ViewProjection[cascade], lightView * lightProjection);
        }
    }

    bool Intersects(unsigned cascade, const DirectX::BoundingBox& bounds) const
    {
        using namespace DirectX;
        XMFLOAT3 corners[8];
        bounds.GetCorners(corners);
        XMVECTOR minimum = XMVectorReplicate(FLT_MAX), maximum = XMVectorReplicate(-FLT_MAX);
        const XMMATRIX matrix = XMLoadFloat4x4(&ViewProjection[cascade]);
        for (const auto& corner : corners)
        {
            const XMVECTOR p = XMVector3TransformCoord(XMLoadFloat3(&corner), matrix);
            minimum = XMVectorMin(minimum, p);
            maximum = XMVectorMax(maximum, p);
        }
        return XMVectorGetX(maximum) >= -1 && XMVectorGetX(minimum) <= 1 &&
            XMVectorGetY(maximum) >= -1 && XMVectorGetY(minimum) <= 1 &&
            XMVectorGetZ(maximum) >= 0 && XMVectorGetZ(minimum) <= 1;
    }
};
