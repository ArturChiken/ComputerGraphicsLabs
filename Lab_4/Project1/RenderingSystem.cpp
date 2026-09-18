#include "RenderingSystem.h"
#include "d3dUtil.h"
#include "ThrowIfFailed.h"

using namespace DirectX;

namespace
{
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

void GBuffer::Clear(ID3D12GraphicsCommandList* commandList) const
{
    const float clearAlbedo[] = { 0.0f, 0.0f, 0.0f, 1.0f };
    const float clearNormal[] = { 0.5f, 0.5f, 1.0f, 1.0f };
    const float clearLighting[] = { 0.0f, 0.0f, 0.0f, 1.0f };

    commandList->ClearRenderTargetView(GetAlbedoRtv(), clearAlbedo, 0, nullptr);
    commandList->ClearRenderTargetView(GetNormalRtv(), clearNormal, 0, nullptr);
    commandList->ClearRenderTargetView(GetLightingRtv(), clearLighting, 0, nullptr);
    commandList->ClearDepthStencilView(GetDepthDsv(), D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
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

    BuildLights();
    mGBuffer.Initialize(device, width, height, rtvDescriptorSize);
    BuildShaders();
    BuildRootSignatures();
    BuildPsos(backBufferFormat);
    BuildDeferredDescriptorHeap();
    BuildFrameConstants();

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
    mGBuffer.Clear(commandList);
    mGBuffer.BindForGeometryPass(commandList);

    commandList->SetGraphicsRootSignature(mGeometryRootSignature.Get());
    ID3D12DescriptorHeap* materialHeaps[] = { scene.MaterialHeap };
    commandList->SetDescriptorHeaps(1, materialHeaps);
    commandList->SetGraphicsRootDescriptorTable(0, scene.MaterialHeap->GetGPUDescriptorHandleForHeapStart());
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commandList->IASetVertexBuffers(0, 1, &scene.VertexBufferView);
    commandList->IASetIndexBuffer(&scene.IndexBufferView);
    commandList->SetPipelineState(scene.Wireframe ? mGeometryWireframePso.Get() : mGeometryPso.Get());

    for (auto& submesh : *scene.Submeshes)
    {
        Material* material = FindMaterial(scene, submesh.MaterialName);
        if (!material)
            continue;

        ObjectConstants objectConstants;
        XMMATRIX view = XMLoadFloat4x4(&scene.View);
        XMMATRIX proj = XMLoadFloat4x4(&scene.Proj);
        XMMATRIX wvp = XMMatrixIdentity() * view * proj;

        XMStoreFloat4x4(&objectConstants.mWorldViewProj, XMMatrixTranspose(wvp));
        objectConstants.uvTiling = material->Tiling;
        objectConstants.uvOffset = scene.UvOffset;

        scene.ObjectConstantsBuffer->CopyData(0, objectConstants);

        D3D12_GPU_DESCRIPTOR_HANDLE materialHandle = scene.MaterialHeap->GetGPUDescriptorHandleForHeapStart();
        materialHandle.ptr += (1 + material->SrvHeapIndex) * scene.MaterialDescriptorSize;
        commandList->SetGraphicsRootDescriptorTable(1, materialHandle);
        commandList->DrawIndexedInstanced(submesh.IndexCount, 1, submesh.IndexStart, 0, 0);
    }

    mGBuffer.TransitionToLightingPass(commandList);
    mGBuffer.TransitionLightingToRenderTarget(commandList);

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

void RenderingSystem::BuildLights()
{
    mDirectionalLights = { {} };

    mPointLights =
    {
        { { -3.0f, 2.5f, -2.0f }, 8.0f, { 1.0f, 0.7f, 0.4f }, 5.0f },
        { {  2.0f, 2.0f,  1.0f }, 7.0f, { 0.4f, 0.7f, 1.0f }, 4.0f },
        { {  0.0f, 3.0f,  4.0f }, 9.0f, { 0.8f, 1.0f, 0.6f }, 3.5f }
    };

    mSpotLights =
    {
        { { -1.5f, 4.0f, -6.0f }, 14.0f, { 0.2f, -1.0f, 0.5f }, 0.75f, { 1.0f, 0.9f, 0.7f }, 7.0f },
        { {  2.5f, 4.5f,  5.0f }, 12.0f, { -0.2f, -1.0f, -0.4f }, 0.8f, { 0.6f, 0.8f, 1.0f }, 6.0f }
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
    geometryRanges[1].NumDescriptors = 1;
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
    geometryParams[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_STATIC_SAMPLER_DESC sampler = {};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.ShaderRegister = 0;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

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
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
    };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC geometryPso = {};
    geometryPso.InputLayout = { inputLayout, _countof(inputLayout) };
    geometryPso.pRootSignature = mGeometryRootSignature.Get();
    geometryPso.VS = { reinterpret_cast<BYTE*>(mGeometryVs->GetBufferPointer()), mGeometryVs->GetBufferSize() };
    geometryPso.PS = { reinterpret_cast<BYTE*>(mGeometryPs->GetBufferPointer()), mGeometryPs->GetBufferSize() };
    geometryPso.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    geometryPso.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
    geometryPso.RasterizerState.DepthClipEnable = TRUE;
    geometryPso.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    geometryPso.BlendState.RenderTarget[1].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    geometryPso.SampleMask = UINT_MAX;
    geometryPso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    geometryPso.NumRenderTargets = 2;
    geometryPso.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    geometryPso.RTVFormats[1] = DXGI_FORMAT_R16G16B16A16_FLOAT;
    geometryPso.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
    geometryPso.SampleDesc.Count = 1;
    geometryPso.DepthStencilState.DepthEnable = TRUE;
    geometryPso.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    geometryPso.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
    ThrowIfFailed(mDevice->CreateGraphicsPipelineState(&geometryPso, IID_PPV_ARGS(&mGeometryPso)));

    geometryPso.RasterizerState.FillMode = D3D12_FILL_MODE_WIREFRAME;
    geometryPso.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    ThrowIfFailed(mDevice->CreateGraphicsPipelineState(&geometryPso, IID_PPV_ARGS(&mGeometryWireframePso)));

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
    ThrowIfFailed(mDevice->CreateGraphicsPipelineState(&lightingPso, IID_PPV_ARGS(&mLightingPso)));

    D3D12_GRAPHICS_PIPELINE_STATE_DESC finalPso = lightingPso;
    finalPso.pRootSignature = mFinalRootSignature.Get();
    finalPso.VS = { reinterpret_cast<BYTE*>(mFinalVs->GetBufferPointer()), mFinalVs->GetBufferSize() };
    finalPso.PS = { reinterpret_cast<BYTE*>(mFinalPs->GetBufferPointer()), mFinalPs->GetBufferSize() };
    finalPso.RTVFormats[0] = backBufferFormat;
    ThrowIfFailed(mDevice->CreateGraphicsPipelineState(&finalPso, IID_PPV_ARGS(&mFinalPso)));
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
    mGeometryPs = d3dUtil::CompileShader(shaderFile, nullptr, "GeometryPS", "ps_5_0");
    mLightingVs = d3dUtil::CompileShader(shaderFile, nullptr, "LightingVS", "vs_5_0");
    mLightingPs = d3dUtil::CompileShader(shaderFile, nullptr, "LightingPS", "ps_5_0");
    mFinalVs = d3dUtil::CompileShader(shaderFile, nullptr, "FinalVS", "vs_5_0");
    mFinalPs = d3dUtil::CompileShader(shaderFile, nullptr, "FinalPS", "ps_5_0");
}

void RenderingSystem::UpdateFrameConstants(const SceneRenderContext& scene)
{
    FrameConstants frame = {};

    XMMATRIX view = XMLoadFloat4x4(&scene.View);
    XMMATRIX proj = XMLoadFloat4x4(&scene.Proj);
    XMMATRIX invViewProj = XMMatrixInverse(nullptr, view * proj);
    XMStoreFloat4x4(&frame.InvViewProj, XMMatrixTranspose(invViewProj));
    frame.CameraPosition = XMFLOAT4(scene.EyePos.x, scene.EyePos.y, scene.EyePos.z, 1.0f);
    frame.LightCounts = XMFLOAT4(
        static_cast<float>(mDirectionalLights.size()),
        static_cast<float>(mPointLights.size()),
        static_cast<float>(mSpotLights.size()),
        0.0f);

    for (size_t i = 0; i < mDirectionalLights.size() && i < MaxDirectionalLights; ++i)
    {
        frame.DirectionalLights[i].DirectionIntensity =
            XMFLOAT4(mDirectionalLights[i].Direction.x, mDirectionalLights[i].Direction.y, mDirectionalLights[i].Direction.z, mDirectionalLights[i].Intensity);
        frame.DirectionalLights[i].Color =
            XMFLOAT4(mDirectionalLights[i].Color.x, mDirectionalLights[i].Color.y, mDirectionalLights[i].Color.z, 1.0f);
    }

    for (size_t i = 0; i < mPointLights.size() && i < MaxPointLights; ++i)
    {
        frame.PointLights[i].PositionRange =
            XMFLOAT4(mPointLights[i].Position.x, mPointLights[i].Position.y, mPointLights[i].Position.z, mPointLights[i].Range);
        frame.PointLights[i].ColorIntensity =
            XMFLOAT4(mPointLights[i].Color.x, mPointLights[i].Color.y, mPointLights[i].Color.z, mPointLights[i].Intensity);
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

Material* RenderingSystem::FindMaterial(const SceneRenderContext& scene, const std::string& materialName) const
{
    for (auto& material : *scene.Materials)
    {
        if (material.Name == materialName)
            return &material;
    }

    return nullptr;
}
