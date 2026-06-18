#include "TutorialScene.h"
#include "Engine/Utility/Scene/SceneManager.h"

using namespace Hagine;
void TutorialScene::Initialize() {
    /// ===================================================
    /// 初期化
    /// ===================================================
    BaseScene::Initialize();
    vp_.Initialize();

    debugCamera_ = std::make_unique<DebugCamera>();
    debugCamera_->Initialize(&vp_);

  drawSystem_->Register("Tutorial_PreDraw", DrawLayer::kPreEffect, [this](const ViewProjection &vp) {
        spriteManager_->DrawAll();
        objectManager_->Draw(vp);
    });
}

void TutorialScene::Finalize() {
    /// ===================================================
    /// 終了処理
    /// ===================================================
    BaseScene::Finalize();
}

void TutorialScene::Update() {
    /// ===================================================
    /// 更新処理
    /// ===================================================

    // カメラの更新
    CameraUpdate();

    // シーン切り替えの更新
    ChangeScene();
}

void TutorialScene::Draw() {
    /// ===================================================
    /// 描画処理
    /// ===================================================
}

void TutorialScene::DrawForOffScreen() {
    /// ===================================================
    /// オフスクリーン描画処理
    /// ===================================================
}

void TutorialScene::AddSceneSetting() {
    /// ===================================================
    /// シーン設定（デバッグ）
    /// ===================================================
    debugCamera_->imgui();
}

void TutorialScene::AddObjectSetting() {
    /// ===================================================
    /// オブジェクト設定（デバッグ）
    /// ===================================================
}

void TutorialScene::AddParticleSetting() {
    /// ===================================================
    /// パーティクル設定（デバッグ）
    /// ===================================================
}

void TutorialScene::CameraUpdate() {
    /// ===================================================
    /// カメラ更新
    /// ===================================================
    if (debugCamera_->GetActive()) {
        debugCamera_->Update();
    } else {
        vp_.UpdateMatrix();
    }
}

void TutorialScene::ChangeScene() {
    /// ===================================================
    /// シーン切り替え
    /// ===================================================
}
