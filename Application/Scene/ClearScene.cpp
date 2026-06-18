#include "Engine/Utility/Scene/SceneManager.h"
#include "ClearScene.h"

using namespace Hagine;
void ClearScene::Initialize() {
    /// ===================================================
    /// 初期化
    /// ===================================================
    BaseScene::Initialize();
    vp_.Initialize();

    debugCamera_ = std::make_unique<DebugCamera>();
    debugCamera_->Initialize(&vp_);

      drawSystem_->Register("Clear_PreDraw", DrawLayer::kPreEffect, [this](const ViewProjection &vp) {
        objectManager_->Draw(vp);
        spriteManager_->DrawAll();
    });
}

void ClearScene::Finalize() {
    /// ===================================================
    /// 終了処理
    /// ===================================================
    BaseScene::Finalize();
}

void ClearScene::Update() {
    /// ===================================================
    /// 更新処理
    /// ===================================================

    // カメラの更新
    CameraUpdate();

    // シーン切り替えの更新
    ChangeScene();
}

void ClearScene::Draw() {
    /// ===================================================
    /// 描画処理
    /// ===================================================
}

void ClearScene::DrawForOffScreen() {
    /// ===================================================
    /// オフスクリーン描画処理
    /// ===================================================
}

void ClearScene::AddSceneSetting() {
    /// ===================================================
    /// シーン設定（デバッグ）
    /// ===================================================
    debugCamera_->imgui();
}

void ClearScene::AddObjectSetting() {
    /// ===================================================
    /// オブジェクト設定（デバッグ）
    /// ===================================================
}

void ClearScene::AddParticleSetting() {
    /// ===================================================
    /// パーティクル設定（デバッグ）
    /// ===================================================
}

void ClearScene::CameraUpdate() {
    /// ===================================================
    /// カメラ更新
    /// ===================================================
    if (debugCamera_->GetActive()) {
        debugCamera_->Update();
    } else {
        vp_.UpdateMatrix();
    }
}

void ClearScene::ChangeScene() {
    /// ===================================================
    /// シーン切り替え
    /// ===================================================
}
