#include "BaseScene.h"

namespace Hagine {
void BaseScene::Initialize()
{
    pAudio_ = Audio::GetInstance();
    pInput_ = Input::GetInstance();
    pLightGroup_ = LightGroup::GetInstance();
    pPtEditor_ = ParticleEditor::GetInstance();
    pPtCSEditor_ = ParticleCSEditor::GetInstance();
    pSpriteManager_ = SpriteManager::GetInstance();
    pObjectManager_ = BaseObjectManager::GetInstance();
}

void BaseScene::Finalize()
{
    if (pDrawSystem_)
    {
        pDrawSystem_->Clear();
    }
}

void BaseScene::Update()
{
}

void BaseScene::Draw()
{
}

void BaseScene::AddSceneSetting()
{
}

void BaseScene::AddObjectSetting()
{
}

void BaseScene::AddParticleSetting()
{
}

void BaseScene::DrawForOffScreen()
{
}

void BaseScene::DrawParticleEditorUI()
{
#ifdef USE_IMGUI
    // CPUパーティクルは従来どおり専用ウィンドウで編集。
    ImGui::Begin("CPUパーティクル");
    pPtEditor_->ShowImGuiEditor();
    pPtEditor_->DebugAll();
    ImGui::End();

    // GPUパーティクルの編集UI（作成・選択・動き設定）はプレビュー窓に統合済み。
    // 「表示 > ウィンドウ > パーティクルプレビュー」で開く（ImGuiManager::ShowParticlePreviewWindow）。

#endif // USE_IMGUI
}

void BaseScene::DrawAllObjects()
{
    pSpriteManager_->DrawAll();
    pObjectManager_->Draw(vp_);

    pPtEditor_->DrawAll(vp_);
    pPtCSEditor_->DrawAll(vp_);
}

} // namespace Hagine
