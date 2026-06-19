#include "MyGame.h"
#include <Frame.h>
#include <Object/Base/BaseObjectManager.h>

namespace Hagine {
void MyGame::Initialize() {
    Framework::Initialize();
    Framework::LoadResource();
    Framework::PlaySounds();
    Framework::RegisterShortcutKey();
    // -----ゲーム固有の処理-----

   
    // 最初のシーンを予約（シーンは REGISTER_SCENE で自己登録済み）
#ifdef _DEBUG
    sceneManager_->NextSceneReservation("TITLE");
#else
    sceneManager_->NextSceneReservation("TITLE");
#endif // _DEBUG
    // -----------------------
}

void MyGame::Finalize() {
    // -----ゲーム固有の処理-----
    
    // -----------------------

    Framework::Finalize();
}

void MyGame::Update() {
    Framework::Update();

    // -----ゲーム固有の処理-----
#ifdef _DEBUG
    if (imGuiManager_->GetEditorMode()) {
        input_->UpdateRay(*sceneManager_->GetBaseScene()->GetViewProjection(), {imGuiManager_->GetScenePos(), imGuiManager_->GetSceneSize()}, 10000.0f);
    } else {
        input_->UpdateRay(*sceneManager_->GetBaseScene()->GetViewProjection(), {Vector2(0, 0), Vector2(winApp_->kClientWidth, winApp_->kClientHeight)}, 10000.0f);
    }

    imGuiManager_->Begin();
    imGuizmoManager_->BeginFrame();
    imGuizmoManager_->SetViewProjection(sceneManager_->GetBaseScene()->GetViewProjection());
    imGuiManager_->UpdateIni();
    imGuiManager_->SetCurrentScene(sceneManager_->GetBaseScene());
    imGuiManager_->ShowMainMenu();
    if (imGuiManager_->GetIsShowMainUI()) {
        imGuiManager_->ShowDockSpace();
        imGuiManager_->ShowSceneWindow(offscreen_.get(), sceneManager_->GetCurrentSceneName());
    }
    imGuiManager_->ShowMainUI(offscreen_.get());
    imGuiManager_->End();
#endif // _DEBUG
#ifndef _DEBUG
    input_->UpdateRay(*sceneManager_->GetBaseScene()->GetViewProjection(), {Vector2(0, 0), Vector2(winApp_->kClientWidth, winApp_->kClientHeight)});
#endif // _DEBUG

    // -----------------------
}

void MyGame::Draw() {
    drawSystem_->Draw(*sceneManager_->GetBaseScene()->GetViewProjection());

#ifdef _DEBUG
    imGuiManager_->Draw();
#endif // _DEBUG

    dxCommon_->PostDraw();
}
} // namespace Hagine
