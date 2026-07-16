#include "Object3dCommon.h"
namespace Hagine {
void Object3dCommon::Initialize()
{
    pPsoManager_ = PipelineManager::GetInstance();
    pComputePsoManager_ = ComputePipelineManager::GetInstance();
}

void Object3dCommon::DrawCommonSetting()
{
    pPsoManager_->DrawCommonSetting(PipelineType::Standard);
}

void Object3dCommon::skinningDrawCommonSetting()
{
    pPsoManager_->DrawCommonSetting(PipelineType::Skinning);
}

void Object3dCommon::computeSkinningDrawCommonSetting()
{
    pComputePsoManager_->DrawCommonSetting(ComputePipelineType::Skinning);
}

void Object3dCommon::SetBlendMode(BlendMode blendMode)
{
    pPsoManager_->DrawCommonSetting(PipelineType::Standard, blendMode);
}
} // namespace Hagine
