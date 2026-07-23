#include "Framework.h"
#include "utility/debug/imgui/ImGuiNotification.h"
#include "utility/scene/SceneRegistry.h"
#include <Frame.h>
#include <debug/log/Logger.h>
#include <debug/profiler/CpuProfiler.h>
#include <iterator>
#include <particle/gpu/ParticleCSSpawner.h>
#include <shadow/ShadowMap.h>
#ifdef _DEBUG
#include <edit/undo/UndoRedoManager.h>
#include <imgui.h>
#endif // _DEBUG

namespace Hagine {
void Framework::Run() {
    // ゲームの初期化
    Initialize();

    while (true) // ゲームループ
    {
        // 更新
        Update();
        // 終了リクエストが来たら抜ける
        if (IsEndRequest()) {
            break;
        }
        // 描画
        Draw();
    }
    // ゲームの終了
    Finalize();
}

void Framework::Initialize() {
    Logger::Info("Application initialization started.");

    ///---------WinApp--------
    // WindowsAPIの初期化
    winApp_ = std::make_unique<WinApp>();
    winApp_->Initialize();
    ///-----------------------

    ///---------DirectXCommon----------
    // DirectXCommonの初期化
    pDxCommon_ = DirectXCommon::GetInstance();
    pDxCommon_->Initialize(winApp_.get());
    ///--------------------------------

    ///--------SRVManager--------
    // SRVマネージャの初期化
    pSrvManager_ = SrvManager::GetInstance();
    pSrvManager_->Initialize();
    ///--------------------------

    ///-------CollisionManager--------
    pCollisionManager_ = CollisionManager::GetInstance();
    ///----------------------------------

    ///--------BaseObjectManager--------
    pBaseObjectManager_ = BaseObjectManager::GetInstance();
    ///---------------------------------

    ///--------SpriteManager--------
    pSpriteManager_ = SpriteManager::GetInstance();
    ///---------------------------------

    /// ---------ImGuizmo---------
#ifdef _DEBUG
    pImGuizmoManager_ = ImGuizmoManager::GetInstance();
#endif // _DEBUG
       /// -----------------------

    /// ---------ImGui---------
#ifdef _DEBUG
    imGuiManager_ = std::make_unique<ImGuiManager>();
    imGuiManager_->Initialize(winApp_.get(), pImGuizmoManager_);
    imGuiManager_->GetIsShowMainUI() = true;
#endif // _DEBUG
       /// -----------------------

    // offscreenのSRV作成
    pDxCommon_->CreateOffscreenSRV();
    // depthのSRV作成
    pDxCommon_->CreateDepthSRV();

    ///----------Input-----------
    // 入力の初期化
    pInput_ = Input::GetInstance();
    pInput_->Init(winApp_->GetHInstance(), winApp_->GetHwnd());
    ///--------------------------

    ///-----------PipelineManager-----------
    pPipeLineManager_ = PipelineManager::GetInstance();
    pPipeLineManager_->Initialize(pDxCommon_);
    ///-------------------------------------

    ///-----------PipelineManager-----------
    pComputePipelineManager_ = ComputePipelineManager::GetInstance();
    pComputePipelineManager_->Initialize(pDxCommon_);
    ///-------------------------------------

    ///-----------TextureManager----------
    pTextureManager_ = TextureManager::GetInstance();
    pTextureManager_->Initialize(pSrvManager_);
    ///-----------------------------------

    ///-----------ModelCommon-------------
    modelCommon_ = std::make_unique<ModelCommon>();
    modelCommon_->Initialize();
    ///-----------------------------------

    ///-----------ModelManager------------
    pModelManager_ = ModelManager::GetInstance();
    pModelManager_->Initialize(pSrvManager_, modelCommon_.get());
    ///----------------------------------

    ///----------PrimitiveModel-----------
    pPrimitiveModel_ = PrimitiveModel::GetInstance();
    pPrimitiveModel_->Initialize();
    ///-----------------------------------

    ///----------SpriteCommon------------
    // スプライト共通部の初期化
    pSpriteCommon_ = SpriteCommon::GetInstance();
    pSpriteCommon_->Initialize();
    ///----------------------------------

    ///----------ParticleCommon------------
    pParticleCommon_ = ParticleCommon::GetInstance();
    pParticleCommon_->Initialize(pDxCommon_);
    ///------------------------------------

    ///---------Audio-------------
    pAudio_ = Audio::GetInstance();
    pAudio_->Initialize();
    ///---------------------------

    ///-------SceneTransition-------
    sceneTransition_ = std::make_unique<SceneTransition>();
    ///-----------------------------

    ///-------SceneManager--------
    pSceneManager_ = SceneManager::GetInstance();
    pSceneManager_->Initialize(sceneTransition_.get());
    pSceneManager_->SetWinApp(winApp_.get());
    ///---------------------------

    ///-------OffScreen--------
    offscreen_ = std::make_unique<OffScreen>();
    offscreen_->Initialize();
    pSceneManager_->SetOffScreen(offscreen_.get());
    ///------------------------

    ///-------DrawSystem-------
    pDrawSystem_ = std::make_unique<DrawSystem>();
    pDrawSystem_->Initialize(pDxCommon_, pSrvManager_, offscreen_.get(), pSceneManager_, pCollisionManager_);
    pSceneManager_->SetDrawSystem(pDrawSystem_.get());
#ifdef _DEBUG
    imGuiManager_->SetDrawSystem(pDrawSystem_.get());
#endif // _DEBUG
    ///------------------------

    ///-------DrawLine3D-------
    pLine3d_ = DrawLine3D::GetInstance();
    pLine3d_->Initialize();
    ///------------------------

    ///-------SkyBox-------
    pSkyBox_ = SkyBox::GetInstance();
    ///--------------------

    ///--------LightGroup------------
    pLightGroup_ = LightGroup::GetInstance();
    pLightGroup_->Initialize();
    ///------------------------------

    ///-------ParticleEditor-------
    pParticleEditor_ = ParticleEditor::GetInstance();
    pParticleEditor_->Initialize();
    ///----------------------------

    ///-------ParticleGroupManager-------
    pParticleGroupManager_ = ParticleGroupManager::GetInstance();
    pParticleGroupManager_->Initialize();
    ///---------------------------------

    ///-------ParticleCSEditor-------
    pParticleCSEditor_ = ParticleCSEditor::GetInstance();
    pParticleCSEditor_->Initialize();
    ///----------------------------

    ///-------ParticleCSGroupManager-------
    pParticleCSGroupManager_ = ParticleCSGroupManager::GetInstance();
    pParticleCSGroupManager_->Initialize();
    ///---------------------------------

    ///------ParticleCSFieldManager------
    pParticleCSFieldManager_ = ParticleCSFieldManager::GetInstance();
    pParticleCSFieldManager_->Initialize();
    ///---------------------------------

    ///--------ShortcutManager------------
    shortcutManager_ = std::make_unique<ShortcutManager>();
    shortcutManager_->Initialize(pInput_);
    ///-----------------------------------

    ///-------MotionEditor-------
    pMotionEditor_ = MotionEditor::GetInstance();
    ///--------------------------

    ///-------csvLoad-------
    csvLoad_ = std::make_unique<CsvLoad>();
    ///---------------------

    ///-------ShadowMap-------
    ShadowMap::GetInstance()->Initialize();
    ShadowMap::GetInstance()->LoadConfig();
    ///------------------------

    /// 時間の初期化
    Frame::Init();

    Logger::Info("Application initialization finished.");
}

void Framework::Finalize() {
    Logger::Info("Application shutting down.");

    pCollisionManager_->Clear();
    pSceneManager_->Finalize();
    sceneTransition_->Finalize();
    winApp_->Finalize();
    pPipeLineManager_->Finalize();
    pComputePipelineManager_->Finalize();
    pTextureManager_->Finalize();
    pModelManager_->Finalize();
    pPrimitiveModel_->Finalize();
    pParticleGroupManager_->Finalize();
    // 実行時配置のエミッターは破棄時にグループをグループマネージャーへ返却するので、
    // そのマネージャーを Finalize する前に片付ける。
    ParticleCSSpawner::GetInstance()->Finalize();
    pParticleCSGroupManager_->Finalize();
    csvLoad_->Finalize();

#ifdef _DEBUG
    imGuiManager_->Finalize();
    pImGuizmoManager_->Finalize();
#endif
    shortcutManager_->Finalize();
    pSpriteManager_->Finalize();
    pLine3d_->Finalize();
    pSkyBox_->Finalize();
    ShadowMap::GetInstance()->Finalize();
    pSrvManager_->Finalize();
    pAudio_->Finalize();
    pLightGroup_->Finalize();
    pMotionEditor_->Finalize();
    pParticleEditor_->Finalize();
    pParticleCSFieldManager_->Finalize();
    pParticleCSEditor_->Finalize();
    pSpriteCommon_->Finalize();
    pParticleCommon_->Finalize();
    modelCommon_->Finalize();

    pBaseObjectManager_->Finalize();
    pDxCommon_->Finalize();
}

void Framework::RegisterShortcutKey() {
    // フルスクリーン
    shortcutManager_->RegisterShortcut("FullScreen", DIK_F11, [this]() {
        winApp_->ToggleFullScreen();
    });
#ifdef _DEBUG
    shortcutManager_->RegisterShortcut("ShowShortcuts", DIK_F1, [this]() {
        imGuiManager_->SetShortcutWindow(true);
    });
    // オブジェクトロード
    shortcutManager_->RegisterShortcut("ObjectLoad", {DIK_LSHIFT, DIK_LCONTROL, DIK_M}, [this]() {
        pBaseObjectManager_->OpenObjectLoadModal();
    });
    // 終了
    shortcutManager_->RegisterShortcut("End", {DIK_LALT, DIK_F4}, [this]() {
        winApp_->ClosedWindow();
    });
    // シーンセーブ
    shortcutManager_->RegisterShortcut("SceneSave", {DIK_LCONTROL, DIK_LSHIFT, DIK_S}, [this]() {
        pBaseObjectManager_->OpenSceneSaveModal();
    });
    // シーン読み込み
    shortcutManager_->RegisterShortcut("SceneLoad", {DIK_LCONTROL, DIK_LSHIFT, DIK_L}, [this]() {
        pBaseObjectManager_->OpenSceneLoadModal();
    });
    // モデル作成
    shortcutManager_->RegisterShortcut("CreateModel", {DIK_LCONTROL, DIK_LSHIFT, DIK_N}, [this]() {
        pBaseObjectManager_->OpenObjectCreationModal();
    });
    // シーン切替（SceneRegistry に自己登録された全シーンへ Ctrl+数字 を割り当てる）
    const std::vector<std::string> sceneNames = SceneRegistry::GetInstance()->GetSceneNames();
    constexpr BYTE kNumberKeys[] = {DIK_1, DIK_2, DIK_3, DIK_4, DIK_5, DIK_6, DIK_7, DIK_8, DIK_9};
    for (size_t i = 0; i < sceneNames.size() && i < std::size(kNumberKeys); ++i) {
        const std::string sceneName = sceneNames[i];
        shortcutManager_->RegisterShortcut(sceneName + "Scene", {DIK_LCONTROL, kNumberKeys[i]}, [this, sceneName]() {
            pSceneManager_->SceneSelection(sceneName);
        });
    }
    // ゲームデバッグ画面切り替え
    shortcutManager_->RegisterShortcut("SwitchMode", DIK_F5, [this]() {
        imGuiManager_->GetIsShowMainUI() = !imGuiManager_->GetIsShowMainUI();
    });
    // 元に戻す（ImGuiのテキスト入力中は入力欄自身のUndoを優先してスキップ）
    shortcutManager_->RegisterShortcut("Undo", {DIK_LCONTROL, DIK_Z}, []() {
        if (ImGui::GetCurrentContext() && ImGui::GetIO().WantTextInput) {
            return;
        }
        UndoRedoManager *undoMgr = UndoRedoManager::GetInstance();
        const std::string label = undoMgr->GetUndoLabel();
        if (undoMgr->Undo()) {
            ImGuiNotification::Post("元に戻す: " + label, {0.42f, 0.66f, 0.68f, 1.0f});
        }
    });
    // やり直し
    shortcutManager_->RegisterShortcut("Redo", {DIK_LCONTROL, DIK_Y}, []() {
        if (ImGui::GetCurrentContext() && ImGui::GetIO().WantTextInput) {
            return;
        }
        UndoRedoManager *undoMgr = UndoRedoManager::GetInstance();
        const std::string label = undoMgr->GetRedoLabel();
        if (undoMgr->Redo()) {
            ImGuiNotification::Post("やり直し: " + label, {0.42f, 0.66f, 0.68f, 1.0f});
        }
    });
    // コピー
    shortcutManager_->RegisterShortcut("Copy", {DIK_LCONTROL, DIK_C}, [this]() {
        pImGuizmoManager_->CopySelectedObjects();
    });
    // ペースト
    shortcutManager_->RegisterShortcut("Paste", {DIK_LCONTROL, DIK_V}, [this]() {
        pImGuizmoManager_->PasteObjects();
    });
    // デリート
    shortcutManager_->RegisterShortcut("Delete", DIK_DELETE, [this]() {
        pImGuizmoManager_->DeleteSelectedObjects();
    });

#endif // _DEBUG
}

void Framework::Update() {

    /// deltaTimeの更新
    Frame::Update();

    {
        HAGINE_CPU_PROFILE("Update/ParticleField");
        pParticleCSFieldManager_->Update();
    }
    {
        HAGINE_CPU_PROFILE("Update/Scene(logic+ImGui)");
        pSceneManager_->Update();
    }
    {
        HAGINE_CPU_PROFILE("Update/Objects(anim+phys)");
        pBaseObjectManager_->Update();
    }
    {
        HAGINE_CPU_PROFILE("Update/Sprites");
        pSpriteManager_->UpdateAll(Frame::DeltaTime());
    }
    {
        HAGINE_CPU_PROFILE("Update/Collision");
        pCollisionManager_->Update();
    }
    {
        HAGINE_CPU_PROFILE("Update/Light");
        LightGroup::GetInstance()->Update(*pSceneManager_->GetBaseScene()->GetViewProjection());
    }
    {
        HAGINE_CPU_PROFILE("Update/Input");
        pInput_->Update();
        shortcutManager_->Update();
        endRequest_ = winApp_->ProcessMessage();
    }

    // 鳴り終わったボイスを回収する（毎フレーム呼ばないと鳴らすたびに溜まっていく）
    pAudio_->CleanupFinishedVoices();

    // ウィンドウサイズが変わっていたらスワップチェーンを追従させる
    // （内部レンダリング解像度は固定のまま、最終合成時に拡縮される）
    {
        uint32_t newWidth = 0, newHeight = 0;
        if (winApp_->ConsumeResize(newWidth, newHeight)) {
            pDxCommon_->ResizeSwapChain(newWidth, newHeight);
        }
    }
}

void Framework::LoadResource() {

    pTextureManager_->LoadAllTextures();

    pTextureManager_->LoadFontTexture("NotoSansJP-Medium.ttf", 100);
    pTextureManager_->LoadFontTexture("YDWyadewanoji.ttf", 50);

    ImGuiNotification::Post("全ての基本リソースを読み込みました", {0.2f, 0.8f, 0.2f, 1.0f});
    Logger::Info("All base resources loaded.");
}

void Framework::PlaySounds() {
}

void Framework::Draw() {
}
} // namespace Hagine
