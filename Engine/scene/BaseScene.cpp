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
    pCameraManager_ = CameraManager::GetInstance();

    // シーンのメインカメラ。最初の1台なので自動的にアクティブになる。
    camera_ = pCameraManager_->Create("メインカメラ");

    // デバッグカメラは全シーン共通の仕組みなので、生成と初期化はここでまとめて行う。
    // 派生シーン側では UpdateDebugCamera() / DrawDebugCameraImGui() を呼ぶだけでよい。
    debugCamera_ = std::make_unique<DebugCamera>();
    debugCamera_->Initialize();
}

void BaseScene::UpdateDebugCamera()
{
    if (debugCamera_)
    {
        // 有効中は自動でデバッグカメラへ切り替わる
        debugCamera_->Update();
    }
}

void BaseScene::DrawDebugCameraImGui()
{
    if (debugCamera_)
    {
        debugCamera_->DrawImGui();
    }
}

bool BaseScene::IsDebugCameraActive() const
{
    return debugCamera_ && debugCamera_->GetActive();
}

bool BaseScene::ToggleDebugCamera()
{
    if (!debugCamera_)
    {
        return false;
    }
    const bool next = !debugCamera_->GetActive();
    debugCamera_->SetActive(next);
    return next;
}

ViewProjection &BaseScene::vp()
{
    // 派生シーンが BaseScene::Initialize を呼んでいない場合に備えて用意しておく
    if (!pCameraManager_)
    {
        pCameraManager_ = CameraManager::GetInstance();
    }
    if (!camera_)
    {
        camera_ = pCameraManager_->Create("メインカメラ");
    }
    // 描画には「今アクティブなカメラ」のビュープロジェクションを使う。
    // カメラ切り替えの補間中はその補間結果になる。
    if (const ViewProjection *active = pCameraManager_->GetActiveViewProjection())
    {
        return const_cast<ViewProjection &>(*active);
    }
    return camera_->GetViewProjection();
}

void BaseScene::Finalize()
{
    if (pDrawSystem_)
    {
        pDrawSystem_->Clear();
    }
    // 実行時配置の GPU パーティクルの片付けは SceneManager 側で行う
    // （派生シーンが BaseScene::Finalize を呼ばないことがあるため、ここには置かない）。
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
    pObjectManager_->Draw(vp());

    pPtEditor_->DrawAll(vp());
    pPtCSEditor_->DrawAll(vp());
}

} // namespace Hagine
