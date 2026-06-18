#include "Object3dCommon.h"
namespace Hagine {
void Object3dCommon::Initialize() {
   psoManager_ = PipeLineManager::GetInstance();
    computePsoManager_ = ComputePipeLineManager::GetInstance();
}

void Object3dCommon::DrawCommonSetting() {
    psoManager_->DrawCommonSetting(PipelineType::kStandard);
}

void Object3dCommon::skinningDrawCommonSetting() {
    psoManager_->DrawCommonSetting(PipelineType::kSkinning);
}

void Object3dCommon::computeSkinningDrawCommonSetting() {
    computePsoManager_->DrawCommonSetting(ComputePipelineType::kSkinning);
}

void Object3dCommon::SetBlendMode(BlendMode blendMode) {
    psoManager_->DrawCommonSetting(PipelineType::kStandard, blendMode);
}
} // namespace Hagine
