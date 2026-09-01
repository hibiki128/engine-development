#include "SceneManager.h"
#include <DirectXCommon.h>
#include <SpriteManager.h>
#include <camera/CameraManager.h>
#include <cassert>
#include <particle/gpu/ParticleCSSpawner.h>
#include <utility/debug/imgui/ImGuiNotification.h>
#ifdef USE_IMGUI
#include <edit/undo/UndoRedoManager.h>
#include <edit/play/PlayModeManager.h>
#endif // USE_IMGUI

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
#ifdef USE_IMGUI
    // Play モードの停止から予約された「シーンの作り直し」をここで消化する。
    // 予約元（ImGui のツールバー・ショートカット）は前フレームの途中なので、
    // シーンを安全に捨てられるフレーム先頭まで遅らせている。
    if (rebuildRequested_) {
        rebuildRequested_ = false;
        // シーン切り替えが予約されているならそちらが優先（作り直す意味が無い）
        if (nextScene_) {
            onSceneRebuilt_ = nullptr;
        } else {
            RebuildCurrentScene();
        }
    }
#endif // USE_IMGUI

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
        // 一時停止・停止中はゲームロジックを進めない。
#ifdef USE_IMGUI
        if (PlayModeManager::GetInstance()->ShouldUpdateGame()) {
            scene_->Update();
        } else {
            // ゲームを止めていてもデバッグカメラだけは動かす（止めると見回せなくなる）。
            // 各シーンは Update の中で UpdateDebugCamera を呼んでいるので、
            // Update を飛ばすこの経路でだけ代わりに呼ぶ。二重呼び出しにはならない。
            scene_->UpdateDebugCamera();
        }
#else
        scene_->Update();
#endif // USE_IMGUI

        // 全カメラの更新と、描画へ渡す出力の作り直し（アクティブカメラ or 切り替え補間の結果）。
        // シーンが描画で使う ViewProjection はこの出力そのものなので、行列のコピーは要らない。
        CameraManager::GetInstance()->Update();
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
#ifdef USE_IMGUI
    if (!pTransition_->IsEnd() && pTransition_->FadeInStart()) {
        return;
    }
    std::unique_ptr<BaseScene> selected = SceneRegistry::GetInstance()->Create(sceneName);
    if (!selected) {
        return; // 未登録のシーン名。今のシーンは壊さない
    }
    pTransition_->Reset();
    nextScene_ = std::move(selected);
    // 各種参照の注入を忘れると、シーンの Initialize が nullptr の DrawSystem を触って落ちる
    nextScene_->SetOffScreen(pOffscreen_);
    nextScene_->SetDrawSystem(pDrawSystem_);
    nextScene_->SetWinApp(pWinApp_);
    // 現在のシーン名を更新しないと、シーンウィンドウの表示名や
    // 停止時のシーン作り直しが「前のシーン」を指したままになる
    currentSceneName_ = sceneName;
    pTransition_->SetFadeInStart(true);
#endif // USE_IMGUI
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
            // 旧シーンが登録したカメラを破棄する（新シーンへ持ち越さない）
            CameraManager::GetInstance()->Clear();
#ifndef USE_IMGUI
            ParticleCSGroupManager::GetInstance()->ClearIndependentGroups();
#endif // USE_IMGUI
#ifdef USE_IMGUI
            // 旧シーンの編集履歴は新シーンでは無効（Undoで旧シーンのオブジェクトを
            // 再生成してしまう事故を防ぐため、シーン切替時に全履歴を破棄する）
            UndoRedoManager::GetInstance()->Clear();
            // 再生モードのスナップショットも同じ理由で捨てる
            // （残すと停止時に旧シーンのオブジェクトが新シーンへ復元される）
            PlayModeManager::GetInstance()->OnSceneChanged();
#endif // USE_IMGUI
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

#ifdef USE_IMGUI
bool SceneManager::RequestSceneRebuild(std::function<void()> onRebuilt) {
    if (!scene_ || currentSceneName_.empty()) {
        return false;
    }
    // 作り直せない名前なら予約せず知らせる（呼び出し元が別の手段へ切り替えられるように）
    if (!SceneRegistry::GetInstance()->Contains(currentSceneName_)) {
        return false;
    }
    rebuildRequested_ = true;
    onSceneRebuilt_ = std::move(onRebuilt);
    return true;
}

void SceneManager::RebuildCurrentScene() {
    // 先に新しいシーンを作っておく。ここで失敗しても今のシーンを壊さずに済む
    std::unique_ptr<BaseScene> rebuilt = SceneRegistry::GetInstance()->Create(currentSceneName_);
    if (!rebuilt) {
        onSceneRebuilt_ = nullptr;
        return;
    }

    // デバッグカメラはシーンが持っているので作り直しで初期位置に戻ってしまう。
    // 編集中の視点が飛ぶと停止のたびに置き直すことになるため、控えて引き継ぐ。
    bool debugCameraActive = false;
    Vector3 debugCameraRotation{};
    Vector3 debugCameraTranslation{};
    if (DebugCamera *debugCamera = scene_->GetDebugCamera()) {
        debugCameraActive = debugCamera->GetActive();
        debugCameraRotation = debugCamera->rotation_;
        debugCameraTranslation = debugCamera->translation_;
    }

    // ---- 旧シーンの破棄（順序は SceneChange と同じ）----
    // GPU が旧シーンのリソースを参照したまま解放しないよう、完了を待ってから捨てる
    DirectXCommon::GetInstance()->WaitForGPU();
    scene_->Finalize();
    scene_.reset();
    // 実行時配置の GPU パーティクルはオブジェクト破棄より前に片付ける
    // （親子付けしたエミッターが破棄済み BaseObject を指したままになるのを防ぐ）
    ParticleCSSpawner::GetInstance()->ClearSceneScoped();
    BaseObjectManager::GetInstance()->RemoveAllObjects();
    SpriteManager::GetInstance()->Clear();
    CameraManager::GetInstance()->Clear();
    // 旧シーンの描画エントリをすべて削除（ダングリングラムダ呼び出し防止）
    if (pDrawSystem_) {
        pDrawSystem_->Clear();
    }
    // シーン切替と違い Undo 履歴は残す。作り直しても同名のオブジェクトが並ぶだけで、
    // 履歴の中身は「名前 → 状態JSON」なのでポインタが宙に浮くことはない。

    // ---- 同じシーンを作り直す ----
    scene_ = std::move(rebuilt);
    scene_->SetOffScreen(pOffscreen_);
    scene_->SetDrawSystem(pDrawSystem_);
    scene_->SetWinApp(pWinApp_);
    scene_->SetSceneManager(this);
    scene_->Initialize();

    // 控えておいた視点を新しいデバッグカメラへ移す。
    // 有効化の実際の切り替え（アクティブカメラの差し替え）は DebugCamera::Update が行う。
    if (DebugCamera *debugCamera = scene_->GetDebugCamera()) {
        debugCamera->rotation_ = debugCameraRotation;
        debugCamera->translation_ = debugCameraTranslation;
        debugCamera->SetActive(debugCameraActive);
    }

    // ---- 作り直した後に重ねる処理 ----
    // ここまでで JSON 由来の「保存済みの状態」に戻っているので、
    // 未保存の編集はこのコールバックで上から適用してもらう。
    if (onSceneRebuilt_) {
        std::function<void()> callback = std::move(onSceneRebuilt_);
        onSceneRebuilt_ = nullptr;
        callback();
    }
}
#endif // USE_IMGUI
} // namespace Hagine
