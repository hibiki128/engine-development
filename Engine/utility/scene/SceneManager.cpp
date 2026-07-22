#include "SceneManager.h"
#include <DirectXCommon.h>
#include <SpriteManager.h>
#include <cassert>
#include <particle/gpu/ParticleCSSpawner.h>
#include <utility/debug/imgui/ImGuiNotification.h>
#ifdef _DEBUG
#include <edit/undo/UndoRedoManager.h>
#endif // _DEBUG

namespace Hagine {
SceneManager::~SceneManager() {
}

void SceneManager::Initialize(SceneTransition *transition) {
    pTransition_ = transition;
    pTransition_->Initialize();
}

void SceneManager::SceneFinalize() {
    if (scene_) {
        scene_->Finalize();
        firstChange_ = false;
    }
}

void SceneManager::Finalize() {
    // 次シーンが残っていれば先に解放
    nextScene_.reset();
    // 現在のシーンを解放
    scene_.reset();
}

void SceneManager::Update() {
    // 次のシーンの予約があるなら
    if (nextScene_) {
        if (!firstChange_) {
            pTransition_->SetFadeInFinish(true);
            firstChange_ = true;
        }
        SceneChange();
    }

    if (!pTransition_->IsEnd()) {
        transitionEnd_ = false;
        pTransition_->Update();
    } else {
        transitionEnd_ = true;
    }

    if (scene_) {
        scene_->Update();
    }
}

void SceneManager::Draw() {
    if (scene_) {
        scene_->Draw();
    }
}

void SceneManager::DrawForOffScreen() {
    if (scene_) {
        scene_->DrawForOffScreen();
    }
}

void SceneManager::SceneSelection(const std::string &sceneName) {
#ifdef _DEBUG
    if (!pTransition_->IsEnd() && pTransition_->FadeInStart()) {
        return;
    }
    pTransition_->Reset();
    nextScene_ = SceneRegistry::GetInstance()->Create(sceneName);
    pTransition_->SetFadeInStart(true);
#endif // _DEBUG
}

void SceneManager::DrawTransition() {
    if (!pTransition_->IsEnd()) {
        pTransition_->Draw();
    }
}

void SceneManager::NextSceneReservation(const std::string &sceneName) {
    if (!pTransition_->IsEnd() && pTransition_->FadeInStart()) {
        return; // すでに遷移中なので次の予約はしない
    }
    pTransition_->Reset();
    assert(nextScene_ == nullptr);

    currentSceneName_ = sceneName;

    // 次シーンを生成（unique_ptr で受け取る）
    // シーンは各 .cpp の REGISTER_SCENE により SceneRegistry へ自己登録済み
    nextScene_ = SceneRegistry::GetInstance()->Create(sceneName);
    assert(nextScene_ && "シーンが登録されていません。REGISTER_SCENE を確認してください");
    nextScene_->SetOffScreen(pOffscreen_);
    nextScene_->SetDrawSystem(pDrawSystem_);
    nextScene_->SetWinApp(pWinApp_);
    if (!firstChange_) {
        pTransition_->SetFadeOutStart(true);
    } else {
        pTransition_->SetFadeInStart(true);
    }
}

void SceneManager::SceneChange() {
    if (pTransition_->FadeInFinish()) {
        // 旧シーンの終了
        if (scene_) {
            // 旧シーンのオブジェクト（D3D12リソース）を破棄する前に GPU の全作業完了を待つ。
            // ダブルバッファのため、前フレームで投入したGPUコマンドが旧シーンのリソースを
            // まだ参照している状態で解放すると、デバッグレイヤーが「使用中リソースの解放」を
            // ERROR 検出してブレークする（シーン遷移時に偶に起きるクラッシュの原因）。
            DirectXCommon::GetInstance()->WaitForGPU();

            scene_->Finalize();
            // delete 不要、reset() で解放
            scene_.reset();
            // 実行時配置の GPU パーティクルを片付ける。
            // オブジェクト破棄より前に行うこと（親子付けしたエミッターが
            // 破棄済み BaseObject を指したままになるのを防ぐ）。
            // 後段の ClearIndependentGroups より前でもある必要がある
            // （エミッターは破棄時にグループをプールへ返却するため）。
            ParticleCSSpawner::GetInstance()->ClearSceneScoped();
            BaseObjectManager::GetInstance()->RemoveAllObjects();
            SpriteManager::GetInstance()->Clear();
#ifndef _DEBUG
            ParticleCSGroupManager::GetInstance()->ClearIndependentGroups();
#endif // _DEBUG
#ifdef _DEBUG
            // 旧シーンの編集履歴は新シーンでは無効（Undoで旧シーンのオブジェクトを
            // 再生成してしまう事故を防ぐため、シーン切替時に全履歴を破棄する）
            UndoRedoManager::GetInstance()->Clear();
#endif // _DEBUG
        }

        // 旧シーンの描画エントリをすべて削除（ダングリングラムダ呼び出し防止）
        if (pDrawSystem_) {
            pDrawSystem_->Clear();
        }

        // 所有権を移譲（nextScene_ は自動的に nullptr になる）
        scene_ = std::move(nextScene_);

        scene_->SetSceneManager(this);
        scene_->Initialize();
        pTransition_->SetFadeOutStart(true);
        ImGuiNotification::Post("シーンを切り替えました: " + currentSceneName_, {0.4f, 0.8f, 1.0f, 1.0f});
    }
}
} // namespace Hagine
