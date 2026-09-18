#pragma once

#include <d3d12.h>
#include <wrl/client.h>
#include <DirectXMath.h>
#include <array>
#include <memory>
#include "UploadBuffer.h"

struct ParticleSettings
{
    bool Visible = true;
    bool Paused = false;
    bool Emit = true;
    float EmissionRate = 1800.0f;
    float Size = 0.045f;
    float Gravity = 3.0f;
    DirectX::XMFLOAT3 Emitter = { -3.8f, -1.0f, -3.5f };
};

class ParticleSystem
{
public:
    static constexpr UINT Capacity = 16384;
    struct Particle
    {
        DirectX::XMFLOAT3 Position;
        float Age;
        DirectX::XMFLOAT3 Velocity;
        float Lifetime;
        DirectX::XMFLOAT4 Color;
        float Size;
        float Padding[3];
    };
    static_assert(sizeof(Particle) == 64);

    void Initialize(ID3D12Device* device);
    void Simulate(ID3D12GraphicsCommandList* commands, float dt,
        const ParticleSettings& settings, const DirectX::XMFLOAT4X4& view,
        const DirectX::XMFLOAT4X4& projection);
    void Draw(ID3D12GraphicsCommandList* commands);
    void RequestReset() { mReset = true; }
    void ReadCompletedCount();
    UINT AliveCount() const { return mAliveCount; }
    // Optional GPU test/debug capture; destination must be a sufficiently large readback buffer.
    void CopyParticlesForValidation(ID3D12GraphicsCommandList* commands, ID3D12Resource* destination);

private:
    struct Constants
    {
        DirectX::XMFLOAT4X4 ViewProj;
        DirectX::XMFLOAT4 CameraRight;
        DirectX::XMFLOAT4 CameraUp;
        DirectX::XMFLOAT4 EmitterSize;
        float DeltaTime;
        float Gravity;
        UINT SpawnCount;
        UINT FrameSeed;
    };
    static_assert(sizeof(Constants) == 128);
    void Bind(ID3D12GraphicsCommandList* commands, bool compute);
    std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, 2> mParticles, mCounters;
    Microsoft::WRL::ComPtr<ID3D12Resource> mCountSnapshot, mDrawArguments, mResetUpload, mReadback;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> mHeap;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> mRoot;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> mComputePso, mDrawPso;
    Microsoft::WRL::ComPtr<ID3D12CommandSignature> mDrawSignature;
    std::unique_ptr<UploadBuffer<Constants>> mConstants;
    UINT mDescriptorSize = 0;
    UINT mCurrent = 0;
    UINT mFrameSeed = 0;
    UINT mAliveCount = 0;
    float mEmissionRemainder = 0.0f;
    bool mReset = true;
    bool mInitialized = false;
    bool mReadbackRecorded = false;
};
