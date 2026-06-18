#include "SpriteCommon.h"

namespace Hagine {
void SpriteCommon::Finalize() {
    // 参照を解放するためnullptrに設定する
    dxCommon_ = nullptr;
    psoManager_ = nullptr;
}

void SpriteCommon::Initialize() {
    // DirectXおよびパイプライン管理のインスタンスを取得して保持する
    dxCommon_ = DirectXCommon::GetInstance();
    psoManager_ = PipeLineManager::GetInstance();
}

void SpriteCommon::DrawCommonSetting() {
    // パイプライン管理を使用してスプライト描画用の共通設定を適用する
    psoManager_->DrawCommonSetting(PipelineType::kSprite);
}

void SpriteCommon::SetBlendMode(BlendMode blendMode) {
    // パイプライン管理を使用して、指定されたブレンドモードでスプライト描画の設定を適用する
    psoManager_->DrawCommonSetting(PipelineType::kSprite, blendMode);
}
} // namespace Hagine
