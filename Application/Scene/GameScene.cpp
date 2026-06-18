#include "Engine/Utility/Scene/SceneManager.h"
#include "GameScene.h"

using namespace Hagine;
void GameScene::Initialize() {
    /// ===================================================
    /// 初期化
    /// ===================================================
    BaseScene::Initialize();
    vp_.Initialize();

    debugCamera_ = std::make_unique<DebugCamera>();
    debugCamera_->Initialize(&vp_);

      drawSystem_->Register("Game_PreDraw", DrawLayer::kPreEffect, [this](const ViewProjection &vp) {
        spriteManager_->DrawAll();
        objectManager_->Draw(vp);
    });
}

void GameScene::Finalize() {
    /// ===================================================
    /// 終了処理
    /// ===================================================
    BaseScene::Finalize();
}

void GameScene::Update() {
    /// ===================================================
    /// 更新処理
    /// ===================================================

    // カメラの更新
    CameraUpdate();

    // シーン切り替えの更新
    ChangeScene();
}

void GameScene::Draw() {
    /// ===================================================
    /// 描画処理
    /// ===================================================
}

void GameScene::DrawForOffScreen() {
    /// ===================================================
    /// オフスクリーン描画処理
    /// ===================================================
}

void GameScene::AddSceneSetting() {
    /// ===================================================
    /// シーン設定（デバッグ）
    /// ===================================================
    debugCamera_->imgui();
}

void GameScene::AddObjectSetting() {
    /// ===================================================
    /// オブジェクト設定（デバッグ）
    /// ===================================================
}

void GameScene::AddParticleSetting() {
    /// ===================================================
    /// パーティクル設定（デバッグ）
    /// ===================================================
}

void GameScene::CameraUpdate() {
    /// ===================================================
    /// カメラ更新
    /// ===================================================
    if (debugCamera_->GetActive()) {
        debugCamera_->Update();
    } else {
        vp_.UpdateMatrix();
    }
}

void GameScene::ChangeScene() {
    /// ===================================================
    /// シーン切り替え
    /// ===================================================
}
