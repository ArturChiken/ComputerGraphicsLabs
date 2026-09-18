#pragma once
#include <DirectXMath.h>

struct ObjectConstants
{
    DirectX::XMFLOAT4X4 mWorldViewProj;

    DirectX::XMFLOAT2 uvTiling;
    DirectX::XMFLOAT2 uvOffset;

    DirectX::XMFLOAT4 padding;

    ObjectConstants()
    {
        DirectX::XMStoreFloat4x4(
            &mWorldViewProj,
            DirectX::XMMatrixIdentity());
    }
};