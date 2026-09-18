#include "RenderingSystem.h"
#include "d3dUtil.h"
#include "ThrowIfFailed.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <windows.h>

using namespace DirectX;

namespace
{
    std::filesystem::path GetExeDirectory()
    {
        char path[MAX_PATH] = {};
        GetModuleFileNameA(nullptr, path, MAX_PATH);
        return std::filesystem::path(path).parent_path();
    }

    void StartupLog(const std::string& message)
    {
        OutputDebugStringA((message + "\n").c_str());

        std::ofstream log(GetExeDirectory() / "Project1_startup.log", std::ios::app);
        if (log)
            log << message << '\n';
    }

    void StartupLogHr(const std::string& message, HRESULT hr)
    {
        char buffer[128] = {};
        sprintf_s(buffer, " HRESULT=0x%08X", static_cast<unsigned int>(hr));
        StartupLog(message + buffer);
    }

    D3D12_RESOURCE_BARRIER TransitionBarrier(
        ID3D12Resource* resource,
        D3D12_RESOURCE_STATES before,
        D3D12_RESOURCE_STATES after)
    {
        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = resource;
        barrier.Transition.StateBefore = before;
        barrier.Transition.StateAfter = after;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        return barrier;
    }
}

bool GBuffer::Initialize(
    ID3D12Device* device,
    UINT width,
    UINT height,
    UINT rtvDescriptorSize)
{
    mRtvDescriptorSize = rtvDescriptorSize;

    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
    rtvHeapDesc.NumDescriptors = 3;
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    ThrowIfFailed(device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&mRtvHeap)));

    D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
    dsvHeapDesc.NumDescriptors = 1;
    dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    ThrowIfFailed(device->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&mDsvHeap)));

    auto createRenderTarget = [&](DXGI_FORMAT format, ComPtr<ID3D12Resource>& resource)
    {
        D3D12_RESOURCE_DESC desc = {};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = width;
        desc.Height = height;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = format;
        desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

        D3D12_HEAP_PROPERTIES heapProps = {};
        heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

        D3D12_CLEAR_VALUE clearValue = {};
        clearValue.Format = format;
        clearValue.Color[3] = 1.0f;

        ThrowIfFailed(device->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &desc,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            &clearValue,
            IID_PPV_ARGS(&resource)));
    };

    createRenderTarget(DXGI_FORMAT_R8G8B8A8_UNORM, mAlbedo);
    createRenderTarget(DXGI_FORMAT_R16G16B16A16_FLOAT, mNormal);
    createRenderTarget(DXGI_FORMAT_R16G16B16A16_FLOAT, mLighting);

    D3D12_RESOURCE_DESC depthDesc = {};
    depthDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    depthDesc.Width = width;
    depthDesc.Height = height;
    depthDesc.DepthOrArraySize = 1;
    depthDesc.MipLevels = 1;
    depthDesc.Format = DXGI_FORMAT_R24G8_TYPELESS;
    depthDesc.SampleDesc.Count = 1;
    depthDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    depthDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_CLEAR_VALUE depthClear = {};
    depthClear.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    depthClear.DepthStencil.Depth = 1.0f;
    depthClear.DepthStencil.Stencil = 0;

    ThrowIfFailed(device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &depthDesc,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        &depthClear,
        IID_PPV_ARGS(&mDepth)));

    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = mRtvHeap->GetCPUDescriptorHandleForHeapStart();
    device->CreateRenderTargetView(mAlbedo.Get(), nullptr, rtvHandle);
    rtvHandle.ptr += mRtvDescriptorSize;
    device->CreateRenderTargetView(mNormal.Get(), nullptr, rtvHandle);
    rtvHandle.ptr += mRtvDescriptorSize;
    device->CreateRenderTargetView(mLighting.Get(), nullptr, rtvHandle);

    D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
    dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    device->CreateDepthStencilView(mDepth.Get(), &dsvDesc, mDsvHeap->GetCPUDescriptorHandleForHeapStart());

    return true;
}

void GBuffer::ClearGeometryTargets(ID3D12GraphicsCommandList* commandList) const
{
    const float clearAlbedo[] = { 0.0f, 0.0f, 0.0f, 1.0f };
    const float clearNormal[] = { 0.5f, 0.5f, 1.0f, 1.0f };

    commandList->ClearRenderTargetView(GetAlbedoRtv(), clearAlbedo, 0, nullptr);
    commandList->ClearRenderTargetView(GetNormalRtv(), clearNormal, 0, nullptr);
    commandList->ClearDepthStencilView(GetDepthDsv(), D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
}

void GBuffer::ClearLightingTarget(ID3D12GraphicsCommandList* commandList) const
{
    const float clearLighting[] = { 0.0f, 0.0f, 0.0f, 1.0f };
    commandList->ClearRenderTargetView(GetLightingRtv(), clearLighting, 0, nullptr);
}

void GBuffer::BindForGeometryPass(ID3D12GraphicsCommandList* commandList) const
{
    D3D12_CPU_DESCRIPTOR_HANDLE rtvs[] = { GetAlbedoRtv(), GetNormalRtv() };
    D3D12_CPU_DESCRIPTOR_HANDLE depthDsv = GetDepthDsv();
    commandList->OMSetRenderTargets(2, rtvs, FALSE, &depthDsv);
}

void GBuffer::TransitionToGeometryPass(ID3D12GraphicsCommandList* commandList) const
{
    D3D12_RESOURCE_BARRIER barriers[] =
    {
        TransitionBarrier(mAlbedo.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET),
        TransitionBarrier(mNormal.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET),
        TransitionBarrier(mDepth.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE)
    };
    commandList->ResourceBarrier(_countof(barriers), barriers);
}

void GBuffer::TransitionToLightingPass(ID3D12GraphicsCommandList* commandList) const
{
    D3D12_RESOURCE_BARRIER barriers[] =
    {
        TransitionBarrier(mAlbedo.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE),
        TransitionBarrier(mNormal.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE),
        TransitionBarrier(mDepth.Get(), D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)
    };
    commandList->ResourceBarrier(_countof(barriers), barriers);
}

void GBuffer::TransitionLightingToRenderTarget(ID3D12GraphicsCommandList* commandList) const
{
    D3D12_RESOURCE_BARRIER barrier =
        TransitionBarrier(mLighting.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
    commandList->ResourceBarrier(1, &barrier);
}

void GBuffer::TransitionLightingToShaderResource(ID3D12GraphicsCommandList* commandList) const
{
    D3D12_RESOURCE_BARRIER barrier =
        TransitionBarrier(mLighting.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    commandList->ResourceBarrier(1, &barrier);
}

D3D12_CPU_DESCRIPTOR_HANDLE GBuffer::GetAlbedoRtv() const
{
    return mRtvHeap->GetCPUDescriptorHandleForHeapStart();
}

D3D12_CPU_DESCRIPTOR_HANDLE GBuffer::GetNormalRtv() const
{
    D3D12_CPU_DESCRIPTOR_HANDLE handle = GetAlbedoRtv();
    handle.ptr += mRtvDescriptorSize;
    return handle;
}

D3D12_CPU_DESCRIPTOR_HANDLE GBuffer::GetLightingRtv() const
{
    D3D12_CPU_DESCRIPTOR_HANDLE handle = GetAlbedoRtv();
    handle.ptr += mRtvDescriptorSize * 2;
    return handle;
}

D3D12_CPU_DESCRIPTOR_HANDLE GBuffer::GetDepthDsv() const
{
    return mDsvHeap->GetCPUDescriptorHandleForHeapStart();
}

bool RenderingSystem::Initialize(
    ID3D12Device* device,
    UINT width,
    UINT height,
    DXGI_FORMAT backBufferFormat,
    UINT cbvSrvUavDescriptorSize,
    UINT rtvDescriptorSize)
{
    mDevice = device;
    mCbvSrvUavDescriptorSize = cbvSrvUavDescriptorSize;
    mWidth = width;
    mHeight = height;

    StartupLog("RenderingSystem BuildLights begin");
    BuildLights();
    StartupLog("RenderingSystem BuildLights ok");
    StartupLog("RenderingSystem GBuffer Initialize begin");
    mGBuffer.Initialize(device, width, height, rtvDescriptorSize);
    StartupLog("RenderingSystem GBuffer Initialize ok");
    StartupLog("RenderingSystem BuildPointLightVolumeMesh begin");
    BuildPointLightVolumeMesh();
    mCullingScene.Build();
    mInstanceCapacity = static_cast<UINT>(mCullingScene.Instances.size());
    mInstanceBuffer = std::make_unique<UploadBuffer<InstanceData>>(device, mInstanceCapacity, false);
    StartupLog("RenderingSystem BuildPointLightVolumeMesh ok");
    StartupLog("RenderingSystem BuildShaders begin");
    BuildShaders();
    StartupLog("RenderingSystem BuildShaders ok");
    StartupLog("RenderingSystem BuildRootSignatures begin");
    BuildRootSignatures();
    StartupLog("RenderingSystem BuildRootSignatures ok");
    StartupLog("RenderingSystem BuildPsos begin");
    BuildPsos(backBufferFormat);
    StartupLog("RenderingSystem BuildPsos ok");
    StartupLog("RenderingSystem BuildDeferredDescriptorHeap begin");
    BuildDeferredDescriptorHeap();
    StartupLog("RenderingSystem BuildDeferredDescriptorHeap ok");
    StartupLog("RenderingSystem BuildFrameConstants begin");
    BuildFrameConstants();
    StartupLog("RenderingSystem BuildFrameConstants ok");

    return true;
}

void RenderingSystem::Render(
    ID3D12GraphicsCommandList* commandList,
    ID3D12Resource* backBuffer,
    D3D12_CPU_DESCRIPTOR_HANDLE backBufferView,
    const SceneRenderContext& scene)
{
    UpdateFrameConstants(scene);

    mGBuffer.TransitionToGeometryPass(commandList);
    mGBuffer.ClearGeometryTargets(commandList);
    mGBuffer.BindForGeometryPass(commandList);

    commandList->SetGraphicsRootSignature(mGeometryRootSignature.Get());
    ID3D12DescriptorHeap* materialHeaps[] = { scene.MaterialHeap };
    commandList->SetDescriptorHeaps(1, materialHeaps);
    commandList->SetGraphicsRootDescriptorTable(0, scene.MaterialHeap->GetGPUDescriptorHandleForHeapStart());
    commandList->IASetVertexBuffers(0, 1, &scene.VertexBufferView);
    commandList->IASetIndexBuffer(&scene.IndexBufferView);

    for (auto& submesh : *scene.Submeshes)
    {
        Material* material = FindMaterial(scene, submesh.MaterialName);
        if (!material)
            continue;

        const bool hasDisplacement = !material->DisplacementMap.empty() &&
            material->DisplacementTexture.Get() != nullptr &&
            material->DisplacementScale > 0.0f;
        if (hasDisplacement)
        {
            commandList->SetPipelineState(scene.Wireframe ? mGeometryWireframePso.Get() : mGeometryPso.Get());
            commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST);
        }
        else
        {
            commandList->SetPipelineState(scene.Wireframe ? mGeometryNoTessWireframePso.Get() : mGeometryNoTessPso.Get());
            commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        }

        ObjectConstants objectConstants;
        XMMATRIX view = XMLoadFloat4x4(&scene.View);
        XMMATRIX proj = XMLoadFloat4x4(&scene.Proj);
        XMMATRIX wvp = XMMatrixIdentity() * view * proj;

        XMStoreFloat4x4(&objectConstants.mWorldViewProj, XMMatrixTranspose(wvp));
        objectConstants.uvTiling = material->Tiling;
        objectConstants.uvOffset =
        {
            scene.UvOffset.x + material->StaticUvOffset.x,
            scene.UvOffset.y + material->StaticUvOffset.y
        };
        objectConstants.eyePosAndDisplacementScale =
            XMFLOAT4(scene.EyePos.x, scene.EyePos.y, scene.EyePos.z, material->DisplacementScale);
        objectConstants.tessellationParams = material->TessellationParams;
        objectConstants.normalParams =
            XMFLOAT4(material->NormalStrength, 2048.0f, 2048.0f, static_cast<float>(scene.DebugViewMode));

        scene.ObjectConstantsBuffer->CopyData(0, objectConstants);

        D3D12_GPU_DESCRIPTOR_HANDLE materialHandle = scene.MaterialHeap->GetGPUDescriptorHandleForHeapStart();
        materialHandle.ptr += (1 + material->SrvHeapIndex) * scene.MaterialDescriptorSize;
        commandList->SetGraphicsRootDescriptorTable(1, materialHandle);
        commandList->DrawIndexedInstanced(submesh.IndexCount, 1, submesh.IndexStart, 0, 0);
    }

    if (scene.ShowCullingScene)
        RenderInstances(commandList, scene);
    else
        mCullingScene.Stats = {};

    mGBuffer.TransitionToLightingPass(commandList);
    mGBuffer.TransitionLightingToRenderTarget(commandList);
    mGBuffer.ClearLightingTarget(commandList);

    ID3D12DescriptorHeap* deferredHeaps[] = { mDeferredHeap.Get() };
    commandList->SetDescriptorHeaps(1, deferredHeaps);
    commandList->SetGraphicsRootSignature(mLightingRootSignature.Get());
    commandList->SetPipelineState(mLightingPso.Get());
    D3D12_CPU_DESCRIPTOR_HANDLE lightingRtv = mGBuffer.GetLightingRtv();
    commandList->OMSetRenderTargets(1, &lightingRtv, FALSE, nullptr);
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    D3D12_GPU_DESCRIPTOR_HANDLE gbufferSrvHandle = mDeferredHeap->GetGPUDescriptorHandleForHeapStart();
    commandList->SetGraphicsRootDescriptorTable(0, gbufferSrvHandle);

    D3D12_GPU_DESCRIPTOR_HANDLE frameCbvHandle = gbufferSrvHandle;
    frameCbvHandle.ptr += 4 * mCbvSrvUavDescriptorSize;
    commandList->SetGraphicsRootDescriptorTable(1, frameCbvHandle);
    commandList->DrawInstanced(3, 1, 0, 0);

    // Dynamic point lights are accumulated with additive blending by drawing one light volume per source.
    commandList->SetGraphicsRootSignature(mPointLightRootSignature.Get());
    commandList->SetPipelineState(mPointLightPso.Get());
    commandList->SetGraphicsRootDescriptorTable(0, gbufferSrvHandle);
    commandList->SetGraphicsRootDescriptorTable(1, frameCbvHandle);
    commandList->IASetVertexBuffers(0, 1, &mPointLightVertexBufferView);
    commandList->IASetIndexBuffer(&mPointLightIndexBufferView);

    UINT lightIndex = 0;
    for (const PointLight& light : mPointLights)
    {
        if (lightIndex >= MaxPointLightVolumes)
            break;

        RenderPointLightVolume(commandList, light, lightIndex++);
    }

    if (scene.DynamicPointLights)
    {
        for (const DynamicPointLight& dynamicLight : *scene.DynamicPointLights)
        {
            if (lightIndex >= MaxPointLightVolumes)
                break;

            RenderPointLightVolume(commandList, dynamicLight.Light, lightIndex++);
        }
    }

    mGBuffer.TransitionLightingToShaderResource(commandList);

    D3D12_RESOURCE_BARRIER backBufferBarrier =
        TransitionBarrier(backBuffer, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
    commandList->ResourceBarrier(1, &backBufferBarrier);

    commandList->SetGraphicsRootSignature(mFinalRootSignature.Get());
    commandList->SetPipelineState(mFinalPso.Get());
    commandList->OMSetRenderTargets(1, &backBufferView, FALSE, nullptr);
    const float clearColor[] = { 0.0f, 0.0f, 0.0f, 1.0f };
    commandList->ClearRenderTargetView(backBufferView, clearColor, 0, nullptr);

    D3D12_GPU_DESCRIPTOR_HANDLE lightingSrvHandle = mDeferredHeap->GetGPUDescriptorHandleForHeapStart();
    lightingSrvHandle.ptr += 3 * mCbvSrvUavDescriptorSize;
    commandList->SetGraphicsRootDescriptorTable(0, lightingSrvHandle);
    commandList->DrawInstanced(3, 1, 0, 0);
}

void RenderingSystem::RenderInstances(ID3D12GraphicsCommandList* commandList, const SceneRenderContext& scene)
{
    BoundingFrustum viewFrustum;
    BoundingFrustum::CreateFromMatrix(viewFrustum, XMLoadFloat4x4(&scene.Proj));
    BoundingFrustum worldFrustum;
    viewFrustum.Transform(worldFrustum, XMMatrixInverse(nullptr, XMLoadFloat4x4(&scene.View)));
    mCullingScene.Select(worldFrustum, scene.Culling);
    if (mCullingScene.VisibleIds.empty())
        return;

    // Draw waits for the GPU at frame end, so this upload buffer can be reused here.
    if (mCullingScene.VisibleIds.size() > mInstanceCapacity)
    {
        mInstanceCapacity = static_cast<UINT>(mCullingScene.VisibleIds.size());
        mInstanceBuffer = std::make_unique<UploadBuffer<InstanceData>>(mDevice.Get(), mInstanceCapacity, false);
    }
    for (size_t i = 0; i < mCullingScene.VisibleIds.size(); ++i)
        mInstanceBuffer->CopyData(static_cast<int>(i),
            mCullingScene.Instances[mCullingScene.VisibleIds[i]]);
    D3D12_VERTEX_BUFFER_VIEW instanceView = {};
    instanceView.BufferLocation = mInstanceBuffer->Resource()->GetGPUVirtualAddress();
    instanceView.SizeInBytes = mInstanceCapacity * sizeof(InstanceData);
    instanceView.StrideInBytes = sizeof(InstanceData);
    const D3D12_VERTEX_BUFFER_VIEW views[] = { mPointLightVertexBufferView, instanceView };
    commandList->IASetVertexBuffers(0, 2, views);
    commandList->IASetIndexBuffer(&mPointLightIndexBufferView);
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commandList->SetPipelineState(scene.Wireframe ? mInstanceWireframePso.Get() : mInstancePso.Get());
    commandList->DrawIndexedInstanced(mPointLightIndexCount,
        static_cast<UINT>(mCullingScene.VisibleIds.size()), 0, 0, 0);
}

void RenderingSystem::BuildLights()
{
    mDirectionalLights = { {} };
    mDirectionalLights[0].Direction = { -0.35f, -0.85f, 0.35f };
    mDirectionalLights[0].Intensity = 1.05f;
    mDirectionalLights[0].Color = { 1.0f, 0.90f, 0.72f };

    mPointLights =
    {
        { { -3.2f, 2.4f, -3.6f }, 7.5f, { 1.0f, 0.62f, 0.34f }, 0.55f },
        { {  3.0f, 3.2f,  2.8f }, 8.0f, { 0.45f, 0.62f, 1.0f }, 0.35f },
        { {  0.5f, 4.6f, -0.5f }, 7.0f, { 0.85f, 1.0f, 0.72f }, 0.25f }
    };

    mSpotLights =
    {
        { { -2.5f, 5.2f, -5.5f }, 13.0f, { 0.25f, -1.0f, 0.45f }, 0.72f, { 1.0f, 0.86f, 0.62f }, 0.75f },
        { {  3.8f, 4.2f,  3.8f }, 11.0f, { -0.45f, -0.8f, -0.35f }, 0.80f, { 0.55f, 0.72f, 1.0f }, 0.42f }
    };
}

void RenderingSystem::BuildRootSignatures()
{
    D3D12_DESCRIPTOR_RANGE geometryRanges[2] = {};
    geometryRanges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
    geometryRanges[0].NumDescriptors = 1;
    geometryRanges[0].BaseShaderRegister = 0;
    geometryRanges[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
    geometryRanges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    geometryRanges[1].NumDescriptors = 3;
    geometryRanges[1].BaseShaderRegister = 0;
    geometryRanges[1].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER geometryParams[2] = {};
    geometryParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    geometryParams[0].DescriptorTable.NumDescriptorRanges = 1;
    geometryParams[0].DescriptorTable.pDescriptorRanges = &geometryRanges[0];
    geometryParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    geometryParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    geometryParams[1].DescriptorTable.NumDescriptorRanges = 1;
    geometryParams[1].DescriptorTable.pDescriptorRanges = &geometryRanges[1];
    // Displacement is sampled in the domain shader, normal/albedo in the pixel shader.
    geometryParams[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_STATIC_SAMPLER_DESC sampler = {};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.ShaderRegister = 0;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_ROOT_SIGNATURE_DESC geometryDesc = {};
    geometryDesc.NumParameters = 2;
    geometryDesc.pParameters = geometryParams;
    geometryDesc.NumStaticSamplers = 1;
    geometryDesc.pStaticSamplers = &sampler;
    geometryDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> serialized = nullptr;
    ComPtr<ID3DBlob> error = nullptr;
    ThrowIfFailed(D3D12SerializeRootSignature(&geometryDesc, D3D_ROOT_SIGNATURE_VERSION_1, &serialized, &error));
    ThrowIfFailed(mDevice->CreateRootSignature(0, serialized->GetBufferPointer(), serialized->GetBufferSize(), IID_PPV_ARGS(&mGeometryRootSignature)));

    D3D12_DESCRIPTOR_RANGE lightingRanges[2] = {};
    lightingRanges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    lightingRanges[0].NumDescriptors = 3;
    lightingRanges[0].BaseShaderRegister = 0;
    lightingRanges[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
    lightingRanges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
    lightingRanges[1].NumDescriptors = 1;
    lightingRanges[1].BaseShaderRegister = 0;
    lightingRanges[1].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER lightingParams[2] = {};
    lightingParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    lightingParams[0].DescriptorTable.NumDescriptorRanges = 1;
    lightingParams[0].DescriptorTable.pDescriptorRanges = &lightingRanges[0];
    lightingParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    lightingParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    lightingParams[1].DescriptorTable.NumDescriptorRanges = 1;
    lightingParams[1].DescriptorTable.pDescriptorRanges = &lightingRanges[1];
    lightingParams[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_STATIC_SAMPLER_DESC pointSampler = {};
    pointSampler.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
    pointSampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    pointSampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    pointSampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    pointSampler.ShaderRegister = 0;
    pointSampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC lightingDesc = {};
    lightingDesc.NumParameters = 2;
    lightingDesc.pParameters = lightingParams;
    lightingDesc.NumStaticSamplers = 1;
    lightingDesc.pStaticSamplers = &pointSampler;
    lightingDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    serialized.Reset();
    error.Reset();
    ThrowIfFailed(D3D12SerializeRootSignature(&lightingDesc, D3D_ROOT_SIGNATURE_VERSION_1, &serialized, &error));
    ThrowIfFailed(mDevice->CreateRootSignature(0, serialized->GetBufferPointer(), serialized->GetBufferSize(), IID_PPV_ARGS(&mLightingRootSignature)));

    D3D12_ROOT_PARAMETER pointLightParams[3] = {};
    pointLightParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    pointLightParams[0].DescriptorTable.NumDescriptorRanges = 1;
    pointLightParams[0].DescriptorTable.pDescriptorRanges = &lightingRanges[0];
    pointLightParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    pointLightParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    pointLightParams[1].DescriptorTable.NumDescriptorRanges = 1;
    pointLightParams[1].DescriptorTable.pDescriptorRanges = &lightingRanges[1];
    pointLightParams[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    pointLightParams[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    pointLightParams[2].Descriptor.ShaderRegister = 1;
    pointLightParams[2].Descriptor.RegisterSpace = 0;
    pointLightParams[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_ROOT_SIGNATURE_DESC pointLightDesc = {};
    pointLightDesc.NumParameters = 3;
    pointLightDesc.pParameters = pointLightParams;
    pointLightDesc.NumStaticSamplers = 1;
    pointLightDesc.pStaticSamplers = &pointSampler;
    pointLightDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    serialized.Reset();
    error.Reset();
    ThrowIfFailed(D3D12SerializeRootSignature(&pointLightDesc, D3D_ROOT_SIGNATURE_VERSION_1, &serialized, &error));
    ThrowIfFailed(mDevice->CreateRootSignature(0, serialized->GetBufferPointer(), serialized->GetBufferSize(), IID_PPV_ARGS(&mPointLightRootSignature)));

    D3D12_DESCRIPTOR_RANGE finalRange = {};
    finalRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    finalRange.NumDescriptors = 1;
    finalRange.BaseShaderRegister = 0;
    finalRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER finalParam = {};
    finalParam.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    finalParam.DescriptorTable.NumDescriptorRanges = 1;
    finalParam.DescriptorTable.pDescriptorRanges = &finalRange;
    finalParam.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC finalDesc = {};
    finalDesc.NumParameters = 1;
    finalDesc.pParameters = &finalParam;
    finalDesc.NumStaticSamplers = 1;
    finalDesc.pStaticSamplers = &pointSampler;
    finalDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    serialized.Reset();
    error.Reset();
    ThrowIfFailed(D3D12SerializeRootSignature(&finalDesc, D3D_ROOT_SIGNATURE_VERSION_1, &serialized, &error));
    ThrowIfFailed(mDevice->CreateRootSignature(0, serialized->GetBufferPointer(), serialized->GetBufferSize(), IID_PPV_ARGS(&mFinalRootSignature)));
}

void RenderingSystem::BuildPsos(DXGI_FORMAT backBufferFormat)
{
    D3D12_INPUT_ELEMENT_DESC inputLayout[] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 32, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "BINORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 44, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
    };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC geometryPso = {};
    geometryPso.InputLayout = { inputLayout, _countof(inputLayout) };
    geometryPso.pRootSignature = mGeometryRootSignature.Get();
    geometryPso.VS = { reinterpret_cast<BYTE*>(mGeometryVs->GetBufferPointer()), mGeometryVs->GetBufferSize() };
    geometryPso.HS = { reinterpret_cast<BYTE*>(mGeometryHs->GetBufferPointer()), mGeometryHs->GetBufferSize() };
    geometryPso.DS = { reinterpret_cast<BYTE*>(mGeometryDs->GetBufferPointer()), mGeometryDs->GetBufferSize() };
    geometryPso.PS = { reinterpret_cast<BYTE*>(mGeometryPs->GetBufferPointer()), mGeometryPs->GetBufferSize() };
    geometryPso.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    geometryPso.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    geometryPso.RasterizerState.DepthClipEnable = TRUE;
    geometryPso.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    geometryPso.BlendState.RenderTarget[1].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    geometryPso.SampleMask = UINT_MAX;
    geometryPso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH;
    geometryPso.NumRenderTargets = 2;
    geometryPso.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    geometryPso.RTVFormats[1] = DXGI_FORMAT_R16G16B16A16_FLOAT;
    geometryPso.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
    geometryPso.SampleDesc.Count = 1;
    geometryPso.DepthStencilState.DepthEnable = TRUE;
    geometryPso.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    geometryPso.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
    HRESULT hr = mDevice->CreateGraphicsPipelineState(&geometryPso, IID_PPV_ARGS(&mGeometryPso));
    if (FAILED(hr))
    {
        StartupLogHr("Create geometry PSO failed", hr);
        ThrowIfFailed(hr, "Create geometry PSO failed");
    }
    StartupLog("Create geometry PSO ok");

    geometryPso.RasterizerState.FillMode = D3D12_FILL_MODE_WIREFRAME;
    geometryPso.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    hr = mDevice->CreateGraphicsPipelineState(&geometryPso, IID_PPV_ARGS(&mGeometryWireframePso));
    if (FAILED(hr))
    {
        StartupLogHr("Create geometry wireframe PSO failed", hr);
        ThrowIfFailed(hr, "Create geometry wireframe PSO failed");
    }
    StartupLog("Create geometry wireframe PSO ok");

    D3D12_GRAPHICS_PIPELINE_STATE_DESC geometryNoTessPso = geometryPso;
    geometryNoTessPso.VS = { reinterpret_cast<BYTE*>(mGeometryNoTessVs->GetBufferPointer()), mGeometryNoTessVs->GetBufferSize() };
    geometryNoTessPso.HS = {};
    geometryNoTessPso.DS = {};
    geometryNoTessPso.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    geometryNoTessPso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    hr = mDevice->CreateGraphicsPipelineState(&geometryNoTessPso, IID_PPV_ARGS(&mGeometryNoTessPso));
    if (FAILED(hr))
    {
        StartupLogHr("Create geometry non-tessellated PSO failed", hr);
        ThrowIfFailed(hr, "Create geometry non-tessellated PSO failed");
    }
    StartupLog("Create geometry non-tessellated PSO ok");

    const D3D12_INPUT_ELEMENT_DESC instanceLayout[] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 20, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "BINORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 32, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "INSTANCE", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 0, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
        { "INSTANCE", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 16, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
        { "INSTANCE", 2, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 32, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
        { "INSTANCE", 3, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 48, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
        { "NORMALWORLD", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 64, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
        { "NORMALWORLD", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 80, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
        { "NORMALWORLD", 2, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 96, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
        { "NORMALWORLD", 3, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 112, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 }
    };
    auto instancePso = geometryNoTessPso;
    instancePso.InputLayout = { instanceLayout, _countof(instanceLayout) };
    instancePso.VS = { mInstanceVs->GetBufferPointer(), mInstanceVs->GetBufferSize() };
    ThrowIfFailed(mDevice->CreateGraphicsPipelineState(&instancePso, IID_PPV_ARGS(&mInstancePso)));
    instancePso.RasterizerState.FillMode = D3D12_FILL_MODE_WIREFRAME;
    ThrowIfFailed(mDevice->CreateGraphicsPipelineState(&instancePso, IID_PPV_ARGS(&mInstanceWireframePso)));

    geometryNoTessPso.RasterizerState.FillMode = D3D12_FILL_MODE_WIREFRAME;
    hr = mDevice->CreateGraphicsPipelineState(&geometryNoTessPso, IID_PPV_ARGS(&mGeometryNoTessWireframePso));
    if (FAILED(hr))
    {
        StartupLogHr("Create geometry non-tessellated wireframe PSO failed", hr);
        ThrowIfFailed(hr, "Create geometry non-tessellated wireframe PSO failed");
    }
    StartupLog("Create geometry non-tessellated wireframe PSO ok");

    D3D12_GRAPHICS_PIPELINE_STATE_DESC lightingPso = {};
    lightingPso.pRootSignature = mLightingRootSignature.Get();
    lightingPso.VS = { reinterpret_cast<BYTE*>(mLightingVs->GetBufferPointer()), mLightingVs->GetBufferSize() };
    lightingPso.PS = { reinterpret_cast<BYTE*>(mLightingPs->GetBufferPointer()), mLightingPs->GetBufferSize() };
    lightingPso.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    lightingPso.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    lightingPso.RasterizerState.DepthClipEnable = TRUE;
    lightingPso.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    lightingPso.SampleMask = UINT_MAX;
    lightingPso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    lightingPso.NumRenderTargets = 1;
    lightingPso.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
    lightingPso.SampleDesc.Count = 1;
    lightingPso.DepthStencilState.DepthEnable = FALSE;
    hr = mDevice->CreateGraphicsPipelineState(&lightingPso, IID_PPV_ARGS(&mLightingPso));
    if (FAILED(hr))
    {
        StartupLogHr("Create lighting PSO failed", hr);
        ThrowIfFailed(hr, "Create lighting PSO failed");
    }
    StartupLog("Create lighting PSO ok");

    D3D12_INPUT_ELEMENT_DESC pointLightInputLayout[] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
    };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pointLightPso = lightingPso;
    pointLightPso.InputLayout = { pointLightInputLayout, _countof(pointLightInputLayout) };
    pointLightPso.pRootSignature = mPointLightRootSignature.Get();
    pointLightPso.VS = { reinterpret_cast<BYTE*>(mPointLightVs->GetBufferPointer()), mPointLightVs->GetBufferSize() };
    pointLightPso.PS = { reinterpret_cast<BYTE*>(mPointLightPs->GetBufferPointer()), mPointLightPs->GetBufferSize() };
    pointLightPso.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    pointLightPso.BlendState.RenderTarget[0].BlendEnable = TRUE;
    pointLightPso.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;
    pointLightPso.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
    pointLightPso.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    pointLightPso.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
    pointLightPso.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ONE;
    pointLightPso.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    pointLightPso.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    // This is additive light accumulation, not alpha transparency blending.
    hr = mDevice->CreateGraphicsPipelineState(&pointLightPso, IID_PPV_ARGS(&mPointLightPso));
    if (FAILED(hr))
    {
        StartupLogHr("Create point light PSO failed", hr);
        ThrowIfFailed(hr, "Create point light PSO failed");
    }
    StartupLog("Create point light PSO ok");

    D3D12_GRAPHICS_PIPELINE_STATE_DESC finalPso = lightingPso;
    finalPso.pRootSignature = mFinalRootSignature.Get();
    finalPso.VS = { reinterpret_cast<BYTE*>(mFinalVs->GetBufferPointer()), mFinalVs->GetBufferSize() };
    finalPso.PS = { reinterpret_cast<BYTE*>(mFinalPs->GetBufferPointer()), mFinalPs->GetBufferSize() };
    finalPso.RTVFormats[0] = backBufferFormat;
    hr = mDevice->CreateGraphicsPipelineState(&finalPso, IID_PPV_ARGS(&mFinalPso));
    if (FAILED(hr))
    {
        StartupLogHr("Create final PSO failed", hr);
        ThrowIfFailed(hr, "Create final PSO failed");
    }
    StartupLog("Create final PSO ok");
}

void RenderingSystem::BuildDeferredDescriptorHeap()
{
    D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
    heapDesc.NumDescriptors = 5;
    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ThrowIfFailed(mDevice->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&mDeferredHeap)));

    D3D12_CPU_DESCRIPTOR_HANDLE handle = mDeferredHeap->GetCPUDescriptorHandleForHeapStart();

    D3D12_SHADER_RESOURCE_VIEW_DESC albedoSrv = {};
    albedoSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    albedoSrv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    albedoSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    albedoSrv.Texture2D.MipLevels = 1;
    mDevice->CreateShaderResourceView(mGBuffer.GetAlbedoResource(), &albedoSrv, handle);

    handle.ptr += mCbvSrvUavDescriptorSize;
    D3D12_SHADER_RESOURCE_VIEW_DESC normalSrv = albedoSrv;
    normalSrv.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    mDevice->CreateShaderResourceView(mGBuffer.GetNormalResource(), &normalSrv, handle);

    handle.ptr += mCbvSrvUavDescriptorSize;
    D3D12_SHADER_RESOURCE_VIEW_DESC depthSrv = albedoSrv;
    depthSrv.Format = mGBuffer.GetDepthSrvFormat();
    mDevice->CreateShaderResourceView(mGBuffer.GetDepthResource(), &depthSrv, handle);

    handle.ptr += mCbvSrvUavDescriptorSize;
    D3D12_SHADER_RESOURCE_VIEW_DESC lightingSrv = albedoSrv;
    lightingSrv.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    mDevice->CreateShaderResourceView(mGBuffer.GetLightingResource(), &lightingSrv, handle);
}

void RenderingSystem::BuildFrameConstants()
{
    mFrameConstants = std::make_unique<UploadBuffer<FrameConstants>>(mDevice.Get(), 1, true);
    mPointLightConstants = std::make_unique<UploadBuffer<PointLightVolumeConstants>>(mDevice.Get(), MaxPointLightVolumes, true);

    UINT cbSize = d3dUtil::CalcConstantBufferByteSize(sizeof(FrameConstants));
    D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
    cbvDesc.BufferLocation = mFrameConstants->Resource()->GetGPUVirtualAddress();
    cbvDesc.SizeInBytes = cbSize;

    D3D12_CPU_DESCRIPTOR_HANDLE handle = mDeferredHeap->GetCPUDescriptorHandleForHeapStart();
    handle.ptr += 4 * mCbvSrvUavDescriptorSize;
    mDevice->CreateConstantBufferView(&cbvDesc, handle);
}

void RenderingSystem::BuildShaders()
{
    const std::wstring shaderFile = L"../Project1/shaders.hlsl";
    mGeometryVs = d3dUtil::CompileShader(shaderFile, nullptr, "GeometryVS", "vs_5_0");
    mInstanceVs = d3dUtil::CompileShader(shaderFile, nullptr, "GeometryInstanceVS", "vs_5_0");
    mGeometryNoTessVs = d3dUtil::CompileShader(shaderFile, nullptr, "GeometryNoTessVS", "vs_5_0");
    mGeometryHs = d3dUtil::CompileShader(shaderFile, nullptr, "GeometryHS", "hs_5_0");
    mGeometryDs = d3dUtil::CompileShader(shaderFile, nullptr, "GeometryDS", "ds_5_0");
    mGeometryPs = d3dUtil::CompileShader(shaderFile, nullptr, "GeometryPS", "ps_5_0");
    mLightingVs = d3dUtil::CompileShader(shaderFile, nullptr, "LightingVS", "vs_5_0");
    mLightingPs = d3dUtil::CompileShader(shaderFile, nullptr, "LightingPS", "ps_5_0");
    mPointLightVs = d3dUtil::CompileShader(shaderFile, nullptr, "PointLightVolumeVS", "vs_5_0");
    mPointLightPs = d3dUtil::CompileShader(shaderFile, nullptr, "PointLightVolumePS", "ps_5_0");
    mFinalVs = d3dUtil::CompileShader(shaderFile, nullptr, "FinalVS", "vs_5_0");
    mFinalPs = d3dUtil::CompileShader(shaderFile, nullptr, "FinalPS", "ps_5_0");
}

void RenderingSystem::BuildPointLightVolumeMesh()
{
    std::vector<SphereVertex> vertices;
    std::vector<uint32_t> indices;
    constexpr UINT subdivisions = 12;
    struct Face { XMFLOAT3 Normal, U, V; XMFLOAT2 Tile; };
    // Earth_ALB/NORM use a vertical cube-cross atlas, not an equirectangular map.
    const Face faces[] = {
        { {0,0,1},  {1,0,0},  {0,-1,0}, {0.375f,0.25f} },
        { {-1,0,0}, {0,0,1},  {0,-1,0}, {0.125f,0.25f} },
        { {1,0,0},  {0,0,-1}, {0,-1,0}, {0.625f,0.25f} },
        { {0,1,0},  {1,0,0},  {0,0,1},  {0.375f,0.0f} },
        { {0,-1,0}, {1,0,0},  {0,0,-1}, {0.375f,0.5f} },
        { {0,0,-1}, {1,0,0},  {0,1,0},  {0.375f,0.75f} }
    };
    for (const Face& face : faces)
    {
        const UINT base = static_cast<UINT>(vertices.size());
        for (UINT y = 0; y <= subdivisions; ++y)
            for (UINT x = 0; x <= subdivisions; ++x)
            {
                const float u = static_cast<float>(x) / subdivisions;
                const float v = static_cast<float>(y) / subdivisions;
                const XMVECTOR normal = XMVector3Normalize(XMLoadFloat3(&face.Normal) +
                    (2 * u - 1) * XMLoadFloat3(&face.U) + (2 * v - 1) * XMLoadFloat3(&face.V));
                const XMVECTOR tangent = XMVector3Normalize(XMLoadFloat3(&face.U) -
                    XMVector3Dot(XMLoadFloat3(&face.U), normal) * normal);
                SphereVertex vertex;
                XMStoreFloat3(&vertex.Position, normal);
                XMStoreFloat3(&vertex.Tangent, tangent);
                XMStoreFloat3(&vertex.Bitangent, XMVector3Cross(normal, tangent));
                vertex.UV = { face.Tile.x + u * 0.25f, face.Tile.y + v * 0.25f };
                vertices.push_back(vertex);
            }
        for (UINT y = 0; y < subdivisions; ++y)
            for (UINT x = 0; x < subdivisions; ++x)
            {
                const UINT a = base + y * (subdivisions + 1) + x;
                const UINT b = a + subdivisions + 1;
                indices.insert(indices.end(), { a, a + 1, b, b, a + 1, b + 1 });
            }
    }

    const UINT vertexBufferSize = static_cast<UINT>(vertices.size() * sizeof(SphereVertex));
    const UINT indexBufferSize = static_cast<UINT>(indices.size() * sizeof(uint32_t));

    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

    D3D12_RESOURCE_DESC vertexDesc = {};
    vertexDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    vertexDesc.Width = vertexBufferSize;
    vertexDesc.Height = 1;
    vertexDesc.DepthOrArraySize = 1;
    vertexDesc.MipLevels = 1;
    vertexDesc.SampleDesc.Count = 1;
    vertexDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    ThrowIfFailed(mDevice->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &vertexDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&mPointLightVertexBuffer)));

    void* mappedData = nullptr;
    mPointLightVertexBuffer->Map(0, nullptr, &mappedData);
    memcpy(mappedData, vertices.data(), vertexBufferSize);
    mPointLightVertexBuffer->Unmap(0, nullptr);

    D3D12_RESOURCE_DESC indexDesc = vertexDesc;
    indexDesc.Width = indexBufferSize;
    ThrowIfFailed(mDevice->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &indexDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&mPointLightIndexBuffer)));

    mPointLightIndexBuffer->Map(0, nullptr, &mappedData);
    memcpy(mappedData, indices.data(), indexBufferSize);
    mPointLightIndexBuffer->Unmap(0, nullptr);

    mPointLightVertexBufferView.BufferLocation = mPointLightVertexBuffer->GetGPUVirtualAddress();
    mPointLightVertexBufferView.StrideInBytes = sizeof(SphereVertex);
    mPointLightVertexBufferView.SizeInBytes = vertexBufferSize;

    mPointLightIndexBufferView.BufferLocation = mPointLightIndexBuffer->GetGPUVirtualAddress();
    mPointLightIndexBufferView.Format = DXGI_FORMAT_R32_UINT;
    mPointLightIndexBufferView.SizeInBytes = indexBufferSize;
    mPointLightIndexCount = static_cast<UINT>(indices.size());
}

void RenderingSystem::UpdateFrameConstants(const SceneRenderContext& scene)
{
    FrameConstants frame = {};

    XMMATRIX view = XMLoadFloat4x4(&scene.View);
    XMMATRIX proj = XMLoadFloat4x4(&scene.Proj);
    XMMATRIX viewProj = view * proj;
    XMMATRIX invViewProj = XMMatrixInverse(nullptr, viewProj);
    XMStoreFloat4x4(&frame.InvViewProj, XMMatrixTranspose(invViewProj));
    XMStoreFloat4x4(&frame.ViewProj, XMMatrixTranspose(viewProj));
    frame.CameraPosition = XMFLOAT4(scene.EyePos.x, scene.EyePos.y, scene.EyePos.z, 1.0f);
    frame.LightCounts = XMFLOAT4(
        static_cast<float>(mDirectionalLights.size()),
        scene.DynamicPointLights ? static_cast<float>(scene.DynamicPointLights->size()) : 0.0f,
        static_cast<float>(mSpotLights.size()),
        0.0f);
    frame.ScreenSize = XMFLOAT4(
        static_cast<float>(mWidth),
        static_cast<float>(mHeight),
        1.0f / static_cast<float>(mWidth),
        1.0f / static_cast<float>(mHeight));

    for (size_t i = 0; i < mDirectionalLights.size() && i < MaxDirectionalLights; ++i)
    {
        frame.DirectionalLights[i].DirectionIntensity =
            XMFLOAT4(mDirectionalLights[i].Direction.x, mDirectionalLights[i].Direction.y, mDirectionalLights[i].Direction.z, mDirectionalLights[i].Intensity);
        frame.DirectionalLights[i].Color =
            XMFLOAT4(mDirectionalLights[i].Color.x, mDirectionalLights[i].Color.y, mDirectionalLights[i].Color.z, 1.0f);
    }

    for (size_t i = 0; i < mSpotLights.size() && i < MaxSpotLights; ++i)
    {
        frame.SpotLights[i].PositionRange =
            XMFLOAT4(mSpotLights[i].Position.x, mSpotLights[i].Position.y, mSpotLights[i].Position.z, mSpotLights[i].Range);
        frame.SpotLights[i].DirectionAngle =
            XMFLOAT4(mSpotLights[i].Direction.x, mSpotLights[i].Direction.y, mSpotLights[i].Direction.z, mSpotLights[i].Angle);
        frame.SpotLights[i].ColorIntensity =
            XMFLOAT4(mSpotLights[i].Color.x, mSpotLights[i].Color.y, mSpotLights[i].Color.z, mSpotLights[i].Intensity);
    }

    mFrameConstants->CopyData(0, frame);
}

void RenderingSystem::RenderPointLightVolume(
    ID3D12GraphicsCommandList* commandList,
    const PointLight& light,
    UINT lightIndex)
{
    PointLightVolumeConstants constants = {};
    constants.PositionRange = XMFLOAT4(light.Position.x, light.Position.y, light.Position.z, light.Range);
    constants.ColorIntensity = XMFLOAT4(light.Color.x, light.Color.y, light.Color.z, light.Intensity);
    mPointLightConstants->CopyData(lightIndex, constants);

    const UINT cbSize = d3dUtil::CalcConstantBufferByteSize(sizeof(PointLightVolumeConstants));
    D3D12_GPU_VIRTUAL_ADDRESS cbAddress =
        mPointLightConstants->Resource()->GetGPUVirtualAddress() + static_cast<UINT64>(lightIndex) * cbSize;
    commandList->SetGraphicsRootConstantBufferView(2, cbAddress);
    commandList->DrawIndexedInstanced(mPointLightIndexCount, 1, 0, 0, 0);
}

Material* RenderingSystem::FindMaterial(const SceneRenderContext& scene, const std::string& materialName) const
{
    for (auto& material : *scene.Materials)
    {
        if (material.Name == materialName)
            return &material;
    }

    return nullptr;
}
