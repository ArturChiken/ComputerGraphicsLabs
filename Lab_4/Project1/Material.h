#pragma once

#include <string>
#include <wrl/client.h>
#include <d3d12.h>
#include <DirectXMath.h>

struct Material
{
    std::string Name;

    std::string DiffuseMap;
    UINT SrvHeapIndex = 0;

    Microsoft::WRL::ComPtr<ID3D12Resource> DiffuseTexture;
    DirectX::XMFLOAT2 Tiling = { 1.0f, 1.0f }; // сколько раз повторять текстуру
    DirectX::XMFLOAT2 UVSpeed = { 0.0f, 0.0f }; // скорость анимации
};