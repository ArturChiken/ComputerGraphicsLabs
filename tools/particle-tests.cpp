#include "../Lab_4/Project1/ParticleSystem.h"
#include "../Lab_4/Project1/ThrowIfFailed.h"
#include <d3d12sdklayers.h>
#include <dxgi1_6.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <vector>

using Microsoft::WRL::ComPtr;
using namespace DirectX;

void Require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

int main()
try
{
    ComPtr<ID3D12Debug> debug;
    const bool debugAvailable = SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)));
    if (debugAvailable)
    {
        debug->EnableDebugLayer();
        ComPtr<ID3D12Debug1> validation;
        if (SUCCEEDED(debug.As(&validation))) validation->SetEnableGPUBasedValidation(TRUE);
    }
    ComPtr<ID3D12Device> device;
    ThrowIfFailed(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)));
    ComPtr<ID3D12InfoQueue> messages;
    device.As(&messages);
    D3D12_COMMAND_QUEUE_DESC desc = {};
    ComPtr<ID3D12CommandQueue> queue;
    ThrowIfFailed(device->CreateCommandQueue(&desc, IID_PPV_ARGS(&queue)));
    ComPtr<ID3D12CommandAllocator> allocator;
    ThrowIfFailed(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)));
    ComPtr<ID3D12GraphicsCommandList> commands;
    ThrowIfFailed(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr,
        IID_PPV_ARGS(&commands)));
    ThrowIfFailed(commands->Close());
    ComPtr<ID3D12Fence> fence;
    ThrowIfFailed(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)));
    HANDLE event = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    Require(event != nullptr, "CreateEvent failed");
    UINT64 frame = 0;

    D3D12_HEAP_PROPERTIES heap = {};
    heap.Type = D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC buffer = {};
    buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    buffer.Width = ParticleSystem::Capacity * sizeof(ParticleSystem::Particle);
    buffer.Height = buffer.DepthOrArraySize = buffer.MipLevels = buffer.SampleDesc.Count = 1;
    buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ComPtr<ID3D12Resource> readback;
    ThrowIfFailed(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &buffer,
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&readback)));

    ParticleSystem system;
    system.Initialize(device.Get());
    ParticleSettings settings;
    settings.Emitter = { 0, 0, 0 };
    XMFLOAT4X4 view, projection;
    XMStoreFloat4x4(&view, XMMatrixLookAtLH(XMVectorSet(0, 2, -10, 1), XMVectorSet(0, 2, 0, 1), XMVectorSet(0, 1, 0, 0)));
    XMStoreFloat4x4(&projection, XMMatrixPerspectiveFovLH(XM_PIDIV2, 1, 0.1f, 100));
    ComPtr<ID3D12DescriptorHeap> rtvs, dsvs;
    D3D12_DESCRIPTOR_HEAP_DESC targetHeap = {};
    targetHeap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    targetHeap.NumDescriptors = 2;
    ThrowIfFailed(device->CreateDescriptorHeap(&targetHeap, IID_PPV_ARGS(&rtvs)));
    targetHeap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    targetHeap.NumDescriptors = 2;
    ThrowIfFailed(device->CreateDescriptorHeap(&targetHeap, IID_PPV_ARGS(&dsvs)));
    auto albedoRtv = rtvs->GetCPUDescriptorHandleForHeapStart();
    auto normalRtv = albedoRtv;
    normalRtv.ptr += device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    auto depthDsv = dsvs->GetCPUDescriptorHandleForHeapStart();
    auto occludedDsv = depthDsv;
    occludedDsv.ptr += device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
    const auto texture = [&](DXGI_FORMAT format, bool depth, float depthValue = 1.0f)
    {
        D3D12_RESOURCE_DESC desc = {};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = desc.Height = 128;
        desc.DepthOrArraySize = desc.MipLevels = desc.SampleDesc.Count = 1;
        desc.Format = format;
        desc.Flags = depth ? D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL : D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        D3D12_HEAP_PROPERTIES properties = {};
        properties.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_CLEAR_VALUE clear = {};
        clear.Format = format;
        if (depth) clear.DepthStencil.Depth = depthValue;
        ComPtr<ID3D12Resource> result;
        ThrowIfFailed(device->CreateCommittedResource(&properties, D3D12_HEAP_FLAG_NONE, &desc,
            depth ? D3D12_RESOURCE_STATE_DEPTH_WRITE : D3D12_RESOURCE_STATE_RENDER_TARGET, &clear, IID_PPV_ARGS(&result)));
        return result;
    };
    auto albedo = texture(DXGI_FORMAT_R8G8B8A8_UNORM, false);
    auto normal = texture(DXGI_FORMAT_R16G16B16A16_FLOAT, false);
    auto depth = texture(DXGI_FORMAT_D24_UNORM_S8_UINT, true);
    auto occludedDepth = texture(DXGI_FORMAT_D24_UNORM_S8_UINT, true, 0.0f);
    device->CreateRenderTargetView(albedo.Get(), nullptr, albedoRtv);
    device->CreateRenderTargetView(normal.Get(), nullptr, normalRtv);
    device->CreateDepthStencilView(depth.Get(), nullptr, depthDsv);
    device->CreateDepthStencilView(occludedDepth.Get(), nullptr, occludedDsv);
    auto imageDesc = buffer;
    imageDesc.Width = 128 * 512;
    ComPtr<ID3D12Resource> imageReadback;
    ThrowIfFailed(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &imageDesc,
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&imageReadback)));
    bool occludeAll = false;
    auto step = [&](float dt, bool capture = false)
    {
        ThrowIfFailed(allocator->Reset());
        ThrowIfFailed(commands->Reset(allocator.Get(), nullptr));
        system.Simulate(commands.Get(), dt, settings, view, projection);
        D3D12_VIEWPORT viewport = { 0, 0, 128, 128, 0, 1 };
        D3D12_RECT scissor = { 0, 0, 128, 128 };
        commands->RSSetViewports(1, &viewport);
        commands->RSSetScissorRects(1, &scissor);
        const D3D12_CPU_DESCRIPTOR_HANDLE targets[] = { albedoRtv, normalRtv };
        const float black[4] = {};
        commands->ClearRenderTargetView(albedoRtv, black, 0, nullptr);
        commands->ClearRenderTargetView(normalRtv, black, 0, nullptr);
        const auto activeDepth = occludeAll ? occludedDsv : depthDsv;
        commands->ClearDepthStencilView(activeDepth, D3D12_CLEAR_FLAG_DEPTH, occludeAll ? 0.0f : 1.0f, 0, 0, nullptr);
        commands->OMSetRenderTargets(2, targets, FALSE, &activeDepth);
        system.Draw(commands.Get());
        if (capture) system.CopyParticlesForValidation(commands.Get(), readback.Get());
        if (capture)
        {
            D3D12_RESOURCE_BARRIER barrier = {};
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition = { albedo.Get(), 0, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE };
            commands->ResourceBarrier(1, &barrier);
            D3D12_TEXTURE_COPY_LOCATION source = {};
            source.pResource = albedo.Get();
            source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            D3D12_TEXTURE_COPY_LOCATION destination = {};
            destination.pResource = imageReadback.Get();
            destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            destination.PlacedFootprint.Footprint = { DXGI_FORMAT_R8G8B8A8_UNORM, 128, 128, 1, 512 };
            commands->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
            std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
            commands->ResourceBarrier(1, &barrier);
        }
        ThrowIfFailed(commands->Close());
        ID3D12CommandList* lists[] = { commands.Get() };
        queue->ExecuteCommandLists(1, lists);
        ThrowIfFailed(queue->Signal(fence.Get(), ++frame));
        ThrowIfFailed(fence->SetEventOnCompletion(frame, event));
        Require(WaitForSingleObject(event, 30000) == WAIT_OBJECT_0, "GPU timeout");
        system.ReadCompletedCount();
        Require(system.AliveCount() <= ParticleSystem::Capacity, "Counter overflow");
    };
    auto particles = [&]()
    {
        void* data = nullptr;
        D3D12_RANGE range = { 0, static_cast<SIZE_T>(buffer.Width) };
        ThrowIfFailed(readback->Map(0, &range, &data));
        const auto* first = static_cast<const ParticleSystem::Particle*>(data);
        std::vector<ParticleSystem::Particle> result(first, first + system.AliveCount());
        D3D12_RANGE noWrite = { 0, 0 };
        readback->Unmap(0, &noWrite);
        std::sort(result.begin(), result.end(), [](const auto& a, const auto& b)
            { return std::memcmp(&a, &b, sizeof(a)) < 0; });
        return result;
    };
    auto coloredPixels = [&]()
    {
        void* data = nullptr;
        D3D12_RANGE range = { 0, 128 * 512 };
        ThrowIfFailed(imageReadback->Map(0, &range, &data));
        const auto* pixels = static_cast<const unsigned char*>(data);
        UINT colored = 0;
        for (UINT i = 0; i < 128 * 128; ++i)
            if (pixels[i * 4] || pixels[i * 4 + 1] || pixels[i * 4 + 2]) ++colored;
        D3D12_RANGE noWrite = { 0, 0 };
        imageReadback->Unmap(0, &noWrite);
        return colored;
    };

    settings.Emit = false;
    step(1.0f / 60);
    Require(system.AliveCount() == 0, "Empty initialization");
    settings.Emit = true;
    settings.EmissionRate = 600;
    for (int i = 0; i < 60; ++i) step(1.0f / 60, i == 59);
    Require(system.AliveCount() == 600, "Birth rate is not time based");
    Require(coloredPixels() > 10, "Indirect GS billboard rendering is blank");
    auto before = particles();
    for (const auto& p : before)
        Require(std::isfinite(p.Position.x) && std::isfinite(p.Position.y) && std::isfinite(p.Position.z)
            && p.Age >= 0 && p.Age < p.Lifetime && p.Size > 0 && p.Color.w == 1, "Invalid particle data");
    settings.Paused = true;
    step(1, true);
    auto paused = particles();
    Require(paused.size() == before.size()
        && std::memcmp(paused.data(), before.data(), before.size() * sizeof(before[0])) == 0, "Pause changed particles");
    occludeAll = true;
    step(0, true);
    Require(coloredPixels() == 0, "Particles ignore the depth buffer");
    occludeAll = false;
    settings.Paused = false;
    settings.Emit = false;
    for (int i = 0; i < 310; ++i) step(1.0f / 60);
    Require(system.AliveCount() == 0, "Expired particles were not removed");

    system.RequestReset();
    settings.Emit = true;
    settings.EmissionRate = 30000;
    for (int i = 0; i < 80; ++i) step(1.0f / 60);
    Require(system.AliveCount() == ParticleSystem::Capacity, "Capacity not reached");
    settings.Paused = true;
    system.RequestReset();
    step(1);
    Require(system.AliveCount() == 0, "Reset while paused failed");
    settings.Paused = false;
    settings.EmissionRate = 600;
    step(10, true);
    Require(system.AliveCount() == 20, "Large dt not clamped");
    settings.Emit = false;
    auto initial = particles();
    step(1.0f / 60, true);
    auto moved = particles();
    // Match particles by their unchanged random color, then verify semi-implicit Euler on the GPU.
    for (const auto& p : initial)
    {
        auto next = std::find_if(moved.begin(), moved.end(), [&](const auto& q)
            { return std::memcmp(&p.Color, &q.Color, sizeof(p.Color)) == 0; });
        Require(next != moved.end(), "Particle lost during update");
        const float vy = p.Velocity.y - settings.Gravity / 60;
        Require(std::abs(next->Velocity.y - vy) < 1e-5f
            && std::abs(next->Position.y - (p.Position.y + vy / 60)) < 1e-5f, "GPU motion integration incorrect");
    }
    settings.EmissionRate = 1;
    settings.Emit = true;
    system.RequestReset();
    for (int i = 0; i < 120; ++i) step(1.0f / 60);
    Require(system.AliveCount() >= 1 && system.AliveCount() <= 2, "Fractional emission lost");

    UINT errors = 0;
    if (messages)
    {
        for (UINT64 i = 0; i < messages->GetNumStoredMessages(); ++i)
        {
            SIZE_T size = 0;
            messages->GetMessage(i, nullptr, &size);
            std::vector<char> storage(size);
            auto* message = reinterpret_cast<D3D12_MESSAGE*>(storage.data());
            messages->GetMessage(i, message, &size);
            if (message->Severity <= D3D12_MESSAGE_SEVERITY_WARNING)
            {
                std::cerr << message->pDescription << '\n';
                ++errors;
            }
        }
    }
    CloseHandle(event);
    Require(errors == 0, "D3D12 validation errors/warnings");
    std::cout << "PASS: GPU births, death, pause, reset, capacity, dt clamp, motion, fractional emission, indirect billboards, depth. Frames: "
        << frame << ". Debug layer: " << (debugAvailable ? "enabled" : "unavailable") << '\n';
    return 0;
}
catch (const std::exception& e)
{
    std::cerr << "FAIL: " << e.what() << '\n';
    return 1;
}
