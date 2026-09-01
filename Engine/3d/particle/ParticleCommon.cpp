#include "ParticleCommon.h"

namespace Hagine {
void ParticleCommon::Finalize()
{
    drawIndexedCommandSignature_.Reset();
    pDxCommon_ = nullptr;
    pPsoManager_ = nullptr;
    pComputePsoManager_ = nullptr;
}

void ParticleCommon::Initialize(DirectXCommon *pDxCommon)
{
    assert(pDxCommon);
    pDxCommon_ = pDxCommon;
    pPsoManager_ = PipelineManager::GetInstance();
    pComputePsoManager_ = ComputePipelineManager::GetInstance();
    CreateDrawIndexedCommandSignature();
}

void ParticleCommon::CreateDrawIndexedCommandSignature()
{
    // GPU駆動描画（DrawInstanceIndirect）用。
    //   引数バッファ 1件 = D3D12_DRAW_INDEXED_ARGUMENTS(20B) で、そのうち InstanceCount は
    //   GPU 上の生存/可視カウンタからコピーされる。CPU は「描画して」とだけ言えばよくなり、
    //   読み戻し値＋マージンで過剰に instance を発行する必要がなくなる。
    D3D12_INDIRECT_ARGUMENT_DESC argumentDesc{};
    argumentDesc.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED;

    D3D12_COMMAND_SIGNATURE_DESC desc{};
    desc.ByteStride = sizeof(D3D12_DRAW_INDEXED_ARGUMENTS);
    desc.NumArgumentDescs = 1;
    desc.pArgumentDescs = &argumentDesc;

    // ルート引数を含まないシグネチャなので pRootSignature は nullptr でよい。
    HRESULT hr = pDxCommon_->GetDevice()->CreateCommandSignature(
        &desc, nullptr, IID_PPV_ARGS(&drawIndexedCommandSignature_));
    assert(SUCCEEDED(hr));
    (void)hr;
}

void ParticleCommon::DrawCommonSetting(BlendMode blendMode)
{
    pPsoManager_->DrawCommonSetting(PipelineType::Particle, blendMode);
}

void ParticleCommon::GPUDrawCommonSetting(BlendMode blendMode)
{
    pPsoManager_->DrawCommonSetting(PipelineType::GPUParticle, blendMode);
}

void ParticleCommon::ComputeInitDrawCommonSetting()
{
    pComputePsoManager_->DrawCommonSetting(ComputePipelineType::InitParticle);
}

void ParticleCommon::ComputeEmitterDrawCommonSetting()
{
    pComputePsoManager_->DrawCommonSetting(ComputePipelineType::Emitter);
}

void ParticleCommon::ComputeUpdateEmitterDrawCommonSetting()
{
    pComputePsoManager_->DrawCommonSetting(ComputePipelineType::UpdateEmitter);
}

void ParticleCommon::ComputeCountDrawCommonSetting()
{
    pComputePsoManager_->DrawCommonSetting(ComputePipelineType::Count);
}
} // namespace Hagine
