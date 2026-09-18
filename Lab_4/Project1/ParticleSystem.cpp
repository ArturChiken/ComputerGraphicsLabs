#include "ParticleSystem.h"
#include "d3dUtil.h"
#include "ThrowIfFailed.h"
#include <algorithm>
#include <cmath>
#include <cstring>

using namespace DirectX;
using Microsoft::WRL::ComPtr;

namespace
{
    void Transition(ID3D12GraphicsCommandList* commands, ID3D12Resource* resource,
        D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after)
    {
        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition = { resource, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, before, after };
        commands->ResourceBarrier(1, &barrier);
    }

    ComPtr<ID3D12Resource> Buffer(ID3D12Device* device, UINT64 bytes,
        D3D12_HEAP_TYPE heap, D3D12_RESOURCE_STATES state, bool uav = false)
    {
        D3D12_HEAP_PROPERTIES properties = {};
        properties.Type = heap;
        D3D12_RESOURCE_DESC desc = {};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Width = bytes;
        desc.Height = desc.DepthOrArraySize = desc.MipLevels = desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        desc.Flags = uav ? D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS : D3D12_RESOURCE_FLAG_NONE;
        ComPtr<ID3D12Resource> resource;
        ThrowIfFailed(device->CreateCommittedResource(&properties, D3D12_HEAP_FLAG_NONE,
            &desc, state, nullptr, IID_PPV_ARGS(&resource)));
        return resource;
    }
}

void ParticleSystem::Initialize(ID3D12Device* device)
{
    mDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    D3D12_DESCRIPTOR_HEAP_DESC heap = {};
    heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heap.NumDescriptors = 2;
    heap.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ThrowIfFailed(device->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&mHeap)));
    auto handle = mHeap->GetCPUDescriptorHandleForHeapStart();
    for (UINT i = 0; i < 2; ++i)
    {
        mParticles[i] = Buffer(device, Capacity * sizeof(Particle), D3D12_HEAP_TYPE_DEFAULT,
            D3D12_RESOURCE_STATE_COMMON, true);
        mCounters[i] = Buffer(device, 4, D3D12_HEAP_TYPE_DEFAULT,
            D3D12_RESOURCE_STATE_COMMON, true);
        D3D12_UNORDERED_ACCESS_VIEW_DESC uav = {};
        uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        uav.Buffer.NumElements = Capacity;
        uav.Buffer.StructureByteStride = sizeof(Particle);
        // Each UAV has its own counter resource; offset 0 meets the alignment requirement.
        device->CreateUnorderedAccessView(mParticles[i].Get(), mCounters[i].Get(), &uav, handle);
        handle.ptr += mDescriptorSize;
    }
    mCountSnapshot = Buffer(device, 4, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COMMON);
    mDrawArguments = Buffer(device, sizeof(D3D12_DRAW_ARGUMENTS), D3D12_HEAP_TYPE_DEFAULT,
        D3D12_RESOURCE_STATE_COMMON);
    mResetUpload = Buffer(device, sizeof(D3D12_DRAW_ARGUMENTS), D3D12_HEAP_TYPE_UPLOAD,
        D3D12_RESOURCE_STATE_GENERIC_READ);
    mReadback = Buffer(device, 4, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
    D3D12_DRAW_ARGUMENTS initial = { 0, 1, 0, 0 };
    void* mapped = nullptr;
    D3D12_RANGE noRead = { 0, 0 };
    ThrowIfFailed(mResetUpload->Map(0, &noRead, &mapped));
    memcpy(mapped, &initial, sizeof(initial));
    mResetUpload->Unmap(0, nullptr);
    mConstants = std::make_unique<UploadBuffer<Constants>>(device, 1, true);

    D3D12_DESCRIPTOR_RANGE ranges[2] = {};
    D3D12_ROOT_PARAMETER parameters[5] = {};
    parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    parameters[0].Descriptor.ShaderRegister = 0;
    for (UINT i = 0; i < 2; ++i)
    {
        ranges[i].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
        ranges[i].NumDescriptors = 1;
        ranges[i].BaseShaderRegister = i;
        parameters[1 + i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        parameters[1 + i].DescriptorTable = { 1, &ranges[i] };
    }
    parameters[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    parameters[3].Descriptor.ShaderRegister = 1;
    parameters[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    parameters[4].Descriptor.ShaderRegister = 0;
    D3D12_ROOT_SIGNATURE_DESC root = {};
    root.NumParameters = _countof(parameters);
    root.pParameters = parameters;
    ComPtr<ID3DBlob> serialized, error;
    ThrowIfFailed(D3D12SerializeRootSignature(&root, D3D_ROOT_SIGNATURE_VERSION_1, &serialized, &error));
    ThrowIfFailed(device->CreateRootSignature(0, serialized->GetBufferPointer(),
        serialized->GetBufferSize(), IID_PPV_ARGS(&mRoot)));

    const std::wstring path = L"../Project1/particles.hlsl";
    const std::string capacity = std::to_string(Capacity);
    const D3D_SHADER_MACRO defines[] = { { "PARTICLE_CAPACITY", capacity.c_str() }, { nullptr, nullptr } };
    auto cs = d3dUtil::CompileShader(path, defines, "SimulateCS", "cs_5_0");
    auto vs = d3dUtil::CompileShader(path, defines, "ParticleVS", "vs_5_0");
    auto gs = d3dUtil::CompileShader(path, defines, "ParticleGS", "gs_5_0");
    auto ps = d3dUtil::CompileShader(path, defines, "ParticlePS", "ps_5_0");
    D3D12_COMPUTE_PIPELINE_STATE_DESC compute = {};
    compute.pRootSignature = mRoot.Get();
    compute.CS = { cs->GetBufferPointer(), cs->GetBufferSize() };
    ThrowIfFailed(device->CreateComputePipelineState(&compute, IID_PPV_ARGS(&mComputePso)));
    D3D12_GRAPHICS_PIPELINE_STATE_DESC draw = {};
    draw.pRootSignature = mRoot.Get();
    draw.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
    draw.GS = { gs->GetBufferPointer(), gs->GetBufferSize() };
    draw.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
    draw.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    draw.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    draw.RasterizerState.DepthClipEnable = TRUE;
    draw.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    draw.DepthStencilState.DepthEnable = TRUE;
    draw.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    draw.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
    draw.SampleMask = UINT_MAX;
    draw.SampleDesc.Count = 1;
    draw.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
    draw.NumRenderTargets = 2;
    draw.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    draw.RTVFormats[1] = DXGI_FORMAT_R16G16B16A16_FLOAT;
    draw.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
    ThrowIfFailed(device->CreateGraphicsPipelineState(&draw, IID_PPV_ARGS(&mDrawPso)));
    D3D12_INDIRECT_ARGUMENT_DESC argument = {};
    argument.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW;
    D3D12_COMMAND_SIGNATURE_DESC signature = {};
    signature.ByteStride = sizeof(D3D12_DRAW_ARGUMENTS);
    signature.NumArgumentDescs = 1;
    signature.pArgumentDescs = &argument;
    ThrowIfFailed(device->CreateCommandSignature(&signature, nullptr, IID_PPV_ARGS(&mDrawSignature)));
}

void ParticleSystem::Bind(ID3D12GraphicsCommandList* commands, bool compute)
{
    ID3D12DescriptorHeap* heaps[] = { mHeap.Get() };
    commands->SetDescriptorHeaps(1, heaps);
    if (compute)
    {
        commands->SetComputeRootSignature(mRoot.Get());
        commands->SetComputeRootConstantBufferView(0, mConstants->Resource()->GetGPUVirtualAddress());
        auto input = mHeap->GetGPUDescriptorHandleForHeapStart();
        auto output = input;
        input.ptr += mCurrent * mDescriptorSize;
        output.ptr += (1 - mCurrent) * mDescriptorSize;
        commands->SetComputeRootDescriptorTable(1, input);
        commands->SetComputeRootDescriptorTable(2, output);
        commands->SetComputeRootShaderResourceView(3, mCountSnapshot->GetGPUVirtualAddress());
    }
    else
    {
        commands->SetGraphicsRootSignature(mRoot.Get());
        commands->SetGraphicsRootConstantBufferView(0, mConstants->Resource()->GetGPUVirtualAddress());
        commands->SetGraphicsRootShaderResourceView(4, mParticles[mCurrent]->GetGPUVirtualAddress());
    }
}

void ParticleSystem::Simulate(ID3D12GraphicsCommandList* commands, float dt,
    const ParticleSettings& settings, const XMFLOAT4X4& view, const XMFLOAT4X4& projection)
{
    // This application waits for the GPU at frame end before reusing upload/readback memory.
    dt = settings.Paused ? 0.0f : (std::clamp)(dt, 0.0f, 1.0f / 30.0f);
    Constants constants = {};
    XMMATRIX camera = XMMatrixInverse(nullptr, XMLoadFloat4x4(&view));
    XMStoreFloat4x4(&constants.ViewProj,
        XMMatrixTranspose(XMLoadFloat4x4(&view) * XMLoadFloat4x4(&projection)));
    XMStoreFloat4(&constants.CameraRight, camera.r[0]);
    XMStoreFloat4(&constants.CameraUp, camera.r[1]);
    constants.EmitterSize = { settings.Emitter.x, settings.Emitter.y, settings.Emitter.z,
        (std::clamp)(settings.Size, 0.005f, 0.25f) };
    constants.DeltaTime = dt;
    constants.Gravity = settings.Gravity;
    constants.FrameSeed = ++mFrameSeed;

    if (!mInitialized)
    {
        for (UINT i = 0; i < 2; ++i)
        {
            Transition(commands, mParticles[i].Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            Transition(commands, mCounters[i].Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        }
        Transition(commands, mCountSnapshot.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        Transition(commands, mDrawArguments.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
    }

    if (mReset)
    {
        mEmissionRemainder = 0;
        for (auto& counter : mCounters)
        {
            Transition(commands, counter.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_DEST);
            commands->CopyBufferRegion(counter.Get(), 0, mResetUpload.Get(), 0, 4);
            Transition(commands, counter.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        }
        Transition(commands, mDrawArguments.Get(), D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT, D3D12_RESOURCE_STATE_COPY_DEST);
        commands->CopyBufferRegion(mDrawArguments.Get(), 0, mResetUpload.Get(), 0, sizeof(D3D12_DRAW_ARGUMENTS));
        Transition(commands, mDrawArguments.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
        mReset = false;
    }
    if (settings.Emit)
    {
        mEmissionRemainder += (std::clamp)(settings.EmissionRate, 0.0f, 30000.0f) * dt;
        constants.SpawnCount = static_cast<UINT>(mEmissionRemainder);
        mEmissionRemainder -= constants.SpawnCount;
    }
    mConstants->CopyData(0, constants);

    if (mInitialized)
        Transition(commands, mParticles[mCurrent].Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    const UINT output = 1 - mCurrent;
    Transition(commands, mCounters[output].Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_DEST);
    commands->CopyBufferRegion(mCounters[output].Get(), 0, mResetUpload.Get(), 0, 4);
    Transition(commands, mCounters[output].Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    Transition(commands, mCounters[mCurrent].Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
    Transition(commands, mCountSnapshot.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
    commands->CopyBufferRegion(mCountSnapshot.Get(), 0, mCounters[mCurrent].Get(), 0, 4);
    Transition(commands, mCountSnapshot.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    Transition(commands, mCounters[mCurrent].Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    Bind(commands, true);
    commands->SetPipelineState(mComputePso.Get());
    commands->Dispatch((Capacity + 255) / 256, 1, 1);
    // Order all data/counter UAV writes before copying the count and drawing the output.
    D3D12_RESOURCE_BARRIER uav = {};
    uav.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    commands->ResourceBarrier(1, &uav);
    mCurrent = output;
    Transition(commands, mParticles[mCurrent].Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    Transition(commands, mCounters[mCurrent].Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
    Transition(commands, mDrawArguments.Get(), D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT, D3D12_RESOURCE_STATE_COPY_DEST);
    commands->CopyBufferRegion(mDrawArguments.Get(), 0, mCounters[mCurrent].Get(), 0, 4);
    commands->CopyBufferRegion(mReadback.Get(), 0, mCounters[mCurrent].Get(), 0, 4);
    Transition(commands, mDrawArguments.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
    Transition(commands, mCounters[mCurrent].Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    mInitialized = mReadbackRecorded = true;
}

void ParticleSystem::Draw(ID3D12GraphicsCommandList* commands)
{
    if (!mInitialized) return;
    Bind(commands, false);
    commands->SetPipelineState(mDrawPso.Get());
    commands->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_POINTLIST);
    commands->IASetVertexBuffers(0, 0, nullptr);
    commands->IASetIndexBuffer(nullptr);
    commands->ExecuteIndirect(mDrawSignature.Get(), 1, mDrawArguments.Get(), 0, nullptr, 0);
}

void ParticleSystem::ReadCompletedCount()
{
    if (!mReadbackRecorded) return;
    void* mapped = nullptr;
    D3D12_RANGE read = { 0, 4 };
    ThrowIfFailed(mReadback->Map(0, &read, &mapped));
    memcpy(&mAliveCount, mapped, 4);
    D3D12_RANGE noWrite = { 0, 0 };
    mReadback->Unmap(0, &noWrite);
}

void ParticleSystem::CopyParticlesForValidation(ID3D12GraphicsCommandList* commands, ID3D12Resource* destination)
{
    Transition(commands, mParticles[mCurrent].Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_COPY_SOURCE);
    commands->CopyBufferRegion(destination, 0, mParticles[mCurrent].Get(), 0, Capacity * sizeof(Particle));
    Transition(commands, mParticles[mCurrent].Get(), D3D12_RESOURCE_STATE_COPY_SOURCE,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
}
