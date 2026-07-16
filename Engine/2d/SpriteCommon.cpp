#include "SpriteCommon.h"

namespace Hagine {
void SpriteCommon::Finalize()
{
    // 参照を解放するためnullptrに設定する
    pDxCommon_ = nullptr;
    pPsoManager_ = nullptr;
}

void SpriteCommon::Initialize()
{
    // DirectXおよびパイプライン管理のインスタンスを取得して保持する
    pDxCommon_ = DirectXCommon::GetInstance();
    pPsoManager_ = PipelineManager::GetInstance();
}

void SpriteCommon::DrawCommonSetting()
{
    // パイプライン管理を使用してスプライト描画用の共通設定を適用する
    pPsoManager_->DrawCommonSetting(PipelineType::Sprite);
}

void SpriteCommon::SetBlendMode(BlendMode blendMode)
{
    // パイプライン管理を使用して、指定されたブレンドモードでスプライト描画の設定を適用する
    pPsoManager_->DrawCommonSetting(PipelineType::Sprite, blendMode);
}
} // namespace Hagine
