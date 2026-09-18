#pragma once

#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <DirectXMath.h>
#include <memory>
#include <vector>
#include "ObjectConstants.h"
#include "UploadBuffer.h"
#include "Material.h"
#include "Submesh.h"

using Microsoft::WRL::ComPtr;

struct DirectionalLight
{
    DirectX::XMFLOAT3 Direction = { -0.4f, -1.0f, 0.2f };
    float Intensity = 1.0f;
    DirectX::XMFLOAT3 Color = { 1.0f, 0.95f, 0.85f };
    float Padding = 0.0f;
};

struct PointLight
{
    DirectX::XMFLOAT3 Position = { 0.0f, 2.0f, 0.0f };
    float Range = 8.0f;
    DirectX::XMFLOAT3 Color = { 1.0f, 0.7f, 0.4f };
    float Intensity = 4.0f;
};

struct SpotLight
{
    DirectX::XMFLOAT3 Position = { 0.0f, 4.0f, -4.0f };
    float Range = 14.0f;
    DirectX::XMFLOAT3 Direction = { 0.0f, -1.0f, 0.3f };
    float Angle = 0.75f;
    DirectX::XMFLOAT3 Color = { 0.6f, 0.8f, 1.0f };
    float Intensity = 6.0f;
};

struct SceneRenderContext
{
    D3D12_VERTEX_BUFFER_VIEW VertexBufferView = {};
    D3D12_INDEX_BUFFER_VIEW IndexBufferView = {};
    UploadBuffer<ObjectConstants>* ObjectConstantsBuffer = nullptr;
    ID3D12DescriptorHeap* MaterialHeap = nullptr;
    UINT MaterialDescriptorSize = 0;
    std::vector<Submesh>* Submeshes = nullptr;
    std::vector<Material>* Materials = nullptr;
    DirectX::XMFLOAT4X4 View = {};
    DirectX::XMFLOAT4X4 Proj = {};
    DirectX::XMFLOAT3 EyePos = { 0.0f, 0.0f, 0.0f };
    DirectX::XMFLOAT2 UvOffset = { 0.0f, 0.0f };
    bool Wireframe = false;
};

class GBuffer
{
public:
    bool Initialize(
        ID3D12Device* device,
        UINT width,
        UINT height,
        UINT rtvDescriptorSize);

    void Clear(ID3D12GraphicsCommandList* commandList) const;
    void BindForGeometryPass(ID3D12GraphicsCommandList* commandList) const;
    void TransitionToGeometryPass(ID3D12GraphicsCommandList* commandList) const;
    void TransitionToLightingPass(ID3D12GraphicsCommandList* commandList) const;
    void TransitionLightingToRenderTarget(ID3D12GraphicsCommandList* commandList) const;
    void TransitionLightingToShaderResource(ID3D12GraphicsCommandList* commandList) const;

    ID3D12Resource* GetAlbedoResource() const { return mAlbedo.Get(); }
    ID3D12Resource* GetNormalResource() const { return mNormal.Get(); }
    ID3D12Resource* GetDepthResource() const { return mDepth.Get(); }
    ID3D12Resource* GetLightingResource() const { return mLighting.Get(); }

    D3D12_CPU_DESCRIPTOR_HANDLE GetAlbedoRtv() const;
    D3D12_CPU_DESCRIPTOR_HANDLE GetNormalRtv() const;
    D3D12_CPU_DESCRIPTOR_HANDLE GetLightingRtv() const;
    D3D12_CPU_DESCRIPTOR_HANDLE GetDepthDsv() const;

    DXGI_FORMAT GetDepthSrvFormat() const { return DXGI_FORMAT_R24_UNORM_X8_TYPELESS; }

private:
    ComPtr<ID3D12DescriptorHeap> mRtvHeap;
    ComPtr<ID3D12DescriptorHeap> mDsvHeap;
    ComPtr<ID3D12Resource> mAlbedo;
    ComPtr<ID3D12Resource> mNormal;
    ComPtr<ID3D12Resource> mLighting;
    ComPtr<ID3D12Resource> mDepth;
    UINT mRtvDescriptorSize = 0;
};

class RenderingSystem
{
public:
    bool Initialize(
        ID3D12Device* device,
        UINT width,
        UINT height,
        DXGI_FORMAT backBufferFormat,
        UINT cbvSrvUavDescriptorSize,
        UINT rtvDescriptorSize);

    void Render(
        ID3D12GraphicsCommandList* commandList,
        ID3D12Resource* backBuffer,
        D3D12_CPU_DESCRIPTOR_HANDLE backBufferView,
        const SceneRenderContext& scene);

private:
    static constexpr UINT MaxDirectionalLights = 1;
    static constexpr UINT MaxPointLights = 4;
    static constexpr UINT MaxSpotLights = 2;

    struct DirectionalLightGpu
    {
        DirectX::XMFLOAT4 DirectionIntensity;
        DirectX::XMFLOAT4 Color;
    };

    struct PointLightGpu
    {
        DirectX::XMFLOAT4 PositionRange;
        DirectX::XMFLOAT4 ColorIntensity;
    };

    struct SpotLightGpu
    {
        DirectX::XMFLOAT4 PositionRange;
        DirectX::XMFLOAT4 DirectionAngle;
        DirectX::XMFLOAT4 ColorIntensity;
    };

    struct FrameConstants
    {
        DirectX::XMFLOAT4X4 InvViewProj;
        DirectX::XMFLOAT4 CameraPosition;
        DirectX::XMFLOAT4 LightCounts;
        DirectionalLightGpu DirectionalLights[MaxDirectionalLights];
        PointLightGpu PointLights[MaxPointLights];
        SpotLightGpu SpotLights[MaxSpotLights];
    };

    void BuildLights();
    void BuildRootSignatures();
    void BuildPsos(DXGI_FORMAT backBufferFormat);
    void BuildDeferredDescriptorHeap();
    void BuildFrameConstants();
    void BuildShaders();
    void UpdateFrameConstants(const SceneRenderContext& scene);
    Material* FindMaterial(const SceneRenderContext& scene, const std::string& materialName) const;

    ComPtr<ID3D12Device> mDevice;
    UINT mCbvSrvUavDescriptorSize = 0;
    GBuffer mGBuffer;

    ComPtr<ID3D12RootSignature> mGeometryRootSignature;
    ComPtr<ID3D12RootSignature> mLightingRootSignature;
    ComPtr<ID3D12RootSignature> mFinalRootSignature;

    ComPtr<ID3D12PipelineState> mGeometryPso;
    ComPtr<ID3D12PipelineState> mGeometryWireframePso;
    ComPtr<ID3D12PipelineState> mLightingPso;
    ComPtr<ID3D12PipelineState> mFinalPso;

    ComPtr<ID3DBlob> mGeometryVs;
    ComPtr<ID3DBlob> mGeometryPs;
    ComPtr<ID3DBlob> mLightingVs;
    ComPtr<ID3DBlob> mLightingPs;
    ComPtr<ID3DBlob> mFinalVs;
    ComPtr<ID3DBlob> mFinalPs;

    ComPtr<ID3D12DescriptorHeap> mDeferredHeap;
    std::unique_ptr<UploadBuffer<FrameConstants>> mFrameConstants;

    std::vector<DirectionalLight> mDirectionalLights;
    std::vector<PointLight> mPointLights;
    std::vector<SpotLight> mSpotLights;
};
