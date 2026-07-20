#include "ParticleCommon.h"

namespace Hagine {
void ParticleCommon::Finalize()
{
    pDxCommon_ = nullptr;
    pPsoManager_ = nullptr;
    pComputePsoManager_ = nullptr;
}

void ParticleCommon::Initialize(DirectXCommon *dxCommon)
{
    assert(dxCommon);
    pDxCommon_ = dxCommon;
    pPsoManager_ = PipelineManager::GetInstance();
    pComputePsoManager_ = ComputePipelineManager::GetInstance();
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
