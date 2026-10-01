#include "Framework.h"
#include <metaball/MetaBallGroupManager.h>
#include "utility/debug/imgui/ImGuiNotification.h"
#include "utility/scene/SceneRegistry.h"
#include "utility/scene/SceneSerializer.h"
#include <2d/ui/UIAnimator.h>
#include <debug/profiler/CpuProfiler.h>
#include <debug/profiler/GpuProfiler.h>
#include <debug/log/Logger.h>
#include <Frame.h>
#include <camera/CameraManager.h>
#include <render/RenderCulling.h>
#include <render/SceneViewRenderer.h>
#include <object/Object3dInstancing.h>
#include <particle/gpu/ParticleCSSpawner.h>
#include <attachment/AttachmentManager.h>
#include <light/ToonSettings.h>
#include <graphics/pipeline/ShaderHotReload.h>
#include <render/raytracing/RaytracingScene.h>
#include <render/raytracing/RtAoPass.h>
#include <render/raytracing/RtShadowPass.h>
#include <render/ToneMapSettings.h>
#include <render/bloom/BloomPass.h>
#include <render/ssao/SsaoRenderer.h>
#include <shadow/ShadowMap.h>
#include <iterator>
#ifdef USE_IMGUI
#include <edit/undo/UndoRedoManager.h>
#include <imgui.h>
#endif // USE_IMGUI

namespace Hagine {
void Framework::Run()
{
    // ゲームの初期化
    Initialize();

    while (true) // ゲームループ
    {
        // 更新
        Update();
        // 終了リクエストが来たら抜ける
        if (IsEndRequest())
        {
            break;
        }
        // 描画
        Draw();
    }
    // ゲームの終了
    Finalize();
}

void Framework::Initialize()
{
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
#ifdef USE_IMGUI
    pImGuizmoManager_ = ImGuizmoManager::GetInstance();
#endif // USE_IMGUI
       /// -----------------------

    /// ---------ImGui---------
#ifdef USE_IMGUI
    imGuiManager_ = std::make_unique<ImGuiManager>();
    imGuiManager_->Initialize(winApp_.get(), pImGuizmoManager_);
    imGuiManager_->GetIsShowMainUI() = true;
#endif // USE_IMGUI
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

    ///-----------ComputeEffectPipeline-----------
    // コンピュートシェーダー版ポストエフェクトのパイプライン置き場。
    // ルートシグネチャはシェーダーのリフレクションから自動生成されるので、
    // ここでは初期化だけしておけばよい（PSOは初回使用時に作られてキャッシュされる）。
    ComputeEffectPipeline::GetInstance()->Initialize(pDxCommon_);
    ///-------------------------------------------

    ///-----------ShaderHotReload-----------------
    // HLSLを保存したらパイプラインを作り直す。ここで現在の更新時刻を基準として控える
    // （パイプラインが全部できたあとに呼ぶこと。基準より前の変更は拾わない）
    ShaderHotReload::GetInstance()->Initialize(pDxCommon_);
    ///-------------------------------------------

    ///-----------Raytracing----------------------
    // インラインRT（RayQuery）用の加速構造と、RTの影パス。
    // 非対応環境では初期化の中で「使わない」と判断してすべて素通りする
    RaytracingScene::GetInstance()->Initialize(pDxCommon_, pSrvManager_);
    RtShadowPass::GetInstance()->Initialize(pDxCommon_, pSrvManager_);
    ///-------------------------------------------

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

    ///---------CaptureManager / DebugConsole-------------
    // 画面の撮影と、実行中にコマンドを打てるコンソール。
    // どちらも Present 直後やImGui描画時に呼ばれるので、この時点で立ち上げておく
    CaptureManager::GetInstance()->Initialize(pDxCommon_);
    DebugConsole::GetInstance()->Initialize();
    ///---------------------------------------------------

    ///---------TimelineManager-------------
    // カメラ・オブジェクト・音・パーティクルをまとめて動かす演出の再生役
    TimelineManager::GetInstance()->Initialize();
    ///-------------------------------------

    ///---------MusicEngine-------------
    // 音楽制作（シンセ・シーケンサ）。Audio の XAudio2 を借りるので必ずこの後に初期化する。
    // 実際のミキサースレッドは、初めて音を鳴らすときまで立ち上がらない
    MusicEngine::GetInstance()->Initialize(pAudio_->GetXAudio2());
    ///---------------------------------

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
#ifdef USE_IMGUI
    imGuiManager_->SetDrawSystem(pDrawSystem_.get());
#endif // USE_IMGUI
    ///------------------------

    ///-------LineRenderer-------
    pLineRenderer_ = LineRenderer::GetInstance();
    pLineRenderer_->Initialize();
    ///--------------------------

    ///-------SkyBox-------
    pSkyBox_ = SkyBox::GetInstance();
    ///--------------------

    ///--------LightGroup------------
    pLightGroup_ = LightGroup::GetInstance();
    pLightGroup_->Initialize();
    // トゥーンシェーディングのつまみ。前方描画とディファードの両方へ同じ値を配る
    ToonSettings::GetInstance()->Initialize();
    ToonSettings::GetInstance()->LoadData("ToonData");
    // HDR で描いたシーンを画面に出せる範囲へ収めるつまみ（露出・トーンマップ）
    ToneMapSettings::GetInstance()->Initialize();
    ///------------------------------

    ///-------DeferredRenderer-------
    // SrvManager / PipelineManager / LightGroup の初期化後に行う
    pDeferredRenderer_ = DeferredRenderer::GetInstance();
    pDeferredRenderer_->Initialize();
    // SSAO（接地の陰り）。G-Buffer を使うのでディファードの直後に立ち上げる
    SsaoRenderer::GetInstance()->Initialize();
    // RTの遮蔽。SSAO と同じ物を作るので、置き換えられるよう隣で立ち上げる
    RtAoPass::GetInstance()->Initialize(pDxCommon_, pSrvManager_);
    // ブルーム（ミップ列）。シーンのカラーターゲットを読むので DirectXCommon の後
    BloomPass::GetInstance()->Initialize();
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

void Framework::Finalize()
{
    Logger::Info("Application shutting down.");

    pCollisionManager_->Clear();
    pSceneManager_->Finalize();
    sceneTransition_->Finalize();
    winApp_->Finalize();
    pPipeLineManager_->Finalize();
    pComputePipelineManager_->Finalize();
    ComputeEffectPipeline::GetInstance()->Finalize();
    pTextureManager_->Finalize();
    // メタボールのグループが持つモデルを先に返してから ModelManager を畳む
    MetaBallGroupManager::GetInstance()->Finalize();
    pModelManager_->Finalize();
    pPrimitiveModel_->Finalize();
    pParticleGroupManager_->Finalize();
    // 実行時配置のエミッターは破棄時にグループをグループマネージャーへ返却するので、
    // そのマネージャーを Finalize する前に片付ける。
    ParticleCSSpawner::GetInstance()->Finalize();
    pParticleCSGroupManager_->Finalize();
    csvLoad_->Finalize();

#ifdef USE_IMGUI
    imGuiManager_->Finalize();
    pImGuizmoManager_->Finalize();
#endif
    shortcutManager_->Finalize();
    pSpriteManager_->Finalize();
    pLineRenderer_->Finalize();
    SsaoRenderer::GetInstance()->Finalize();
    BloomPass::GetInstance()->Finalize();
    // 加速構造はGPUメモリを掴んだままなので、SRVマネージャーより先に畳む
    RtAoPass::GetInstance()->Finalize();
    RtShadowPass::GetInstance()->Finalize();
    RaytracingScene::GetInstance()->Finalize();
    pDeferredRenderer_->Finalize();
    pSkyBox_->Finalize();
    SceneViewRenderer::GetInstance()->Finalize();
    ShadowMap::GetInstance()->Finalize();
    pSrvManager_->Finalize();
    // 書き出し待ちの画像を吐き切ってからスレッドを畳む
    CaptureManager::GetInstance()->Finalize();
    DebugConsole::GetInstance()->Finalize();
    TimelineManager::GetInstance()->Finalize();
    // ミキサースレッドとソースボイスは、XAudio2 本体を畳む前に片付ける
    MusicEngine::GetInstance()->Finalize();
    pAudio_->Finalize();
    pLightGroup_->Finalize();
    ToonSettings::GetInstance()->Finalize();
    ToneMapSettings::GetInstance()->Finalize();
    AttachmentManager::GetInstance()->Finalize();
    pMotionEditor_->Finalize();
    pParticleEditor_->Finalize();
    pParticleCSFieldManager_->Finalize();
    pParticleCSEditor_->Finalize();
    pSpriteCommon_->Finalize();
    pParticleCommon_->Finalize();
    modelCommon_->Finalize();

    pBaseObjectManager_->Finalize();
    CameraManager::GetInstance()->Finalize();      // カメラが持つ定数バッファの解放
    Object3dInstancing::GetInstance()->Finalize(); // インスタンシング用アップロードバッファの解放
    GpuProfiler::GetInstance()->Finalize();        // クエリヒープと読み戻しバッファの解放
    pDxCommon_->Finalize();
}

void Framework::RegisterShortcutKey()
{
    // フルスクリーン
    shortcutManager_->RegisterShortcut("FullScreen", DIK_F11, [this]() {
        winApp_->ToggleFullScreen();
    });
    // スクリーンショット。ウィンドウを開かずに素材を撮れるよう、常に効くようにしてある
    shortcutManager_->RegisterShortcut("Screenshot", DIK_F9, []() {
        CaptureManager::GetInstance()->RequestScreenshot();
    });
    // 連番録画の開始・停止
    shortcutManager_->RegisterShortcut("RecordToggle", DIK_F10, []() {
        CaptureManager *capture = CaptureManager::GetInstance();
        if (capture->IsRecording())
        {
            capture->StopSequence();
        }
        else
        {
            capture->StartSequence();
        }
    });
#ifdef USE_IMGUI
    shortcutManager_->RegisterShortcut("ShowShortcuts", DIK_F1, [this]() {
        imGuiManager_->SetShortcutWindow(true);
    });
    // 再生 / 一時停止のトグル（Unity の Ctrl+P 相当）
    shortcutManager_->RegisterShortcut("PlayPause", {DIK_LCONTROL, DIK_P}, []() {
        PlayModeManager *playMode = PlayModeManager::GetInstance();
        if (playMode->IsPlaying())
        {
            playMode->Pause();
        }
        else
        {
            playMode->Play();
        }
    });
    // 停止（再生前の状態へ戻す）
    shortcutManager_->RegisterShortcut("PlayStop", {DIK_LCONTROL, DIK_LSHIFT, DIK_P}, []() {
        PlayModeManager::GetInstance()->Stop();
    });
    // デバッグカメラ切り替え（シーン設定ウィンドウのチェックボックスと同じ操作）
    shortcutManager_->RegisterShortcut("DebugCamera", DIK_F3, [this]() {
        BaseScene *currentScene = pSceneManager_->GetBaseScene();
        if (!currentScene)
        {
            return;
        }
        const bool active = currentScene->ToggleDebugCamera();
        ImGuiNotification::Post(active ? "デバッグカメラ: ON (WASD/Space/Shift・右ドラッグで回転)"
                                       : "デバッグカメラ: OFF",
                                active ? Vector4{0.45f, 0.68f, 0.52f, 1.0f}
                                       : Vector4{0.55f, 0.55f, 0.60f, 1.0f});
    });
    // オブジェクトロード
    shortcutManager_->RegisterShortcut("ObjectLoad", {DIK_LSHIFT, DIK_LCONTROL, DIK_M}, [this]() {
        pBaseObjectManager_->OpenObjectLoadModal();
    });
    // 終了
    shortcutManager_->RegisterShortcut("End", {DIK_LALT, DIK_F4}, [this]() {
        winApp_->ClosedWindow();
    });
    // シーンを上書き保存（今のシーン名のファイルへ。確認ダイアログは出さない）
    shortcutManager_->RegisterShortcut("SceneSave", {DIK_LCONTROL, DIK_S}, []() {
        SceneSerializer::GetInstance()->SaveCurrentScene();
    });
    // 名前を付けて保存（保存する物の確認もここで行う）
    shortcutManager_->RegisterShortcut("SceneSaveAs", {DIK_LCONTROL, DIK_LSHIFT, DIK_S}, []() {
        SceneSerializer::GetInstance()->OpenSaveDialog();
    });
    // シーン読み込み
    shortcutManager_->RegisterShortcut("SceneLoad", {DIK_LCONTROL, DIK_LSHIFT, DIK_L}, []() {
        SceneSerializer::GetInstance()->OpenLoadDialog();
    });
    // モデル作成
    shortcutManager_->RegisterShortcut("CreateModel", {DIK_LCONTROL, DIK_LSHIFT, DIK_N}, [this]() {
        pBaseObjectManager_->OpenObjectCreationModal();
    });
    // シーン切替（SceneRegistry に自己登録された全シーンへ Ctrl+数字 を割り当てる）
    const std::vector<std::string> sceneNames = SceneRegistry::GetInstance()->GetSceneNames();
    constexpr BYTE kNumberKeys[] = {DIK_1, DIK_2, DIK_3, DIK_4, DIK_5, DIK_6, DIK_7, DIK_8, DIK_9};
    for (size_t i = 0; i < sceneNames.size() && i < std::size(kNumberKeys); ++i)
    {
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
        if (ImGui::GetCurrentContext() && ImGui::GetIO().WantTextInput)
        {
            return;
        }
        UndoRedoManager *undoMgr = UndoRedoManager::GetInstance();
        const std::string label = undoMgr->GetUndoLabel();
        if (undoMgr->Undo())
        {
            ImGuiNotification::Post("元に戻す: " + label, {0.42f, 0.66f, 0.68f, 1.0f});
        }
    });
    // やり直し
    shortcutManager_->RegisterShortcut("Redo", {DIK_LCONTROL, DIK_Y}, []() {
        if (ImGui::GetCurrentContext() && ImGui::GetIO().WantTextInput)
        {
            return;
        }
        UndoRedoManager *undoMgr = UndoRedoManager::GetInstance();
        const std::string label = undoMgr->GetRedoLabel();
        if (undoMgr->Redo())
        {
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
    // 複製（コピーバッファを経由せずその場で増やす。並べる作業はこちらの方が速い）
    shortcutManager_->RegisterShortcut("Duplicate", {DIK_LCONTROL, DIK_D}, [this]() {
        pImGuizmoManager_->DuplicateSelectedObjects();
    });
    // デリート
    shortcutManager_->RegisterShortcut("Delete", DIK_DELETE, [this]() {
        pImGuizmoManager_->DeleteSelectedObjects();
    });

#endif // USE_IMGUI
}

void Framework::Update()
{

    /// deltaTimeの更新
    Frame::Update();

    // HLSLが保存されていたらパイプラインを作り直す。
    // 描画を始める前のこの位置で行うことで、このフレームから新しいシェーダーが使われる
    ShaderHotReload::GetInstance()->Update();

    // 破棄されたオブジェクトが返したモデルとSRVインデックスのうち、
    // GPU が触り終わったものを実際に捨てる。描画を始める前のここで済ませておく
    pModelManager_->Update();
    pSrvManager_->Update();

    // 線の積み上げをリセットし、視錐台カリング用の平面を更新する。
    // このフレーム中に積まれた線は、DrawSystem の Render で一括描画される。
    // （カリングには前フレームのカメラ行列を使う。1フレームぶんの遅れはデバッグ線では問題にならない）
    if (BaseScene *currentScene = pSceneManager_->GetBaseScene())
    {
        pLineRenderer_->BeginFrame(*currentScene->GetViewProjection());

        // デバッグカメラを使っている間は、使う前のカメラ（メイン）の行列をカリングへ渡す。
        // ポインタではなく行列を写すので、シーンを切り替えても古いカメラを指さない
        DebugCamera *pDebugCamera = currentScene->GetDebugCamera();
        Camera *pMainCamera = pDebugCamera ? pDebugCamera->GetPreviousCamera() : nullptr;
        if (pMainCamera)
        {
            const ViewProjection &mainView = pMainCamera->GetViewProjection();
            const Matrix4x4 mainViewProjection = mainView.matView_ * mainView.matProjection_;
            RenderCulling::SetInspectionCamera(&mainViewProjection);
        }
        else
        {
            RenderCulling::SetInspectionCamera(nullptr);
        }
        RenderCulling::SubmitDebugLines();
    }

    // 動的ポイントライト（GPUパーティクルの発光など）の登録もフレーム単位。
    // 実際の登録は描画フェーズのエミッター更新中に行われ、DrawSystem が
    // シーン描画の直前に CommitPointLights() で定数バッファへ反映する。
    pLightGroup_->ClearDynamicPointLights();

    // 一時停止・停止中に進めない「ゲーム世界」の更新かどうか。
    // 描画・エディタ・カメラ・トランスフォーム伝播は止めない（編集を続けられるようにするため）。
#ifdef USE_IMGUI
    const bool updateGameWorld = PlayModeManager::GetInstance()->ShouldUpdateGame();
#else
    const bool updateGameWorld = true;
#endif // USE_IMGUI

    if (updateGameWorld)
    {
        HAGINE_CPU_PROFILE("Update/ParticleField");
        pParticleCSFieldManager_->Update();
    }
    {
        HAGINE_CPU_PROFILE("Update/Scene(logic+ImGui)");
        pSceneManager_->Update();
    }
    {
        // 演出タイムライン。オブジェクトの姿勢を書き換えるので、
        // ワールド行列が組み立てられる BaseObjectManager::Update より前に流す。
        // 一時停止中でも動かす（止めた状態で演出を作り込めるようにするため）
        HAGINE_CPU_PROFILE("Update/Timeline");
        TimelineManager::GetInstance()->Update(Frame::DeltaTime());
    }
    {
        HAGINE_CPU_PROFILE("Update/Objects(anim+phys)");
        pBaseObjectManager_->Update();
    }
    {
        // UIトゥイーンの補間とグループ相対位置の反映（スプライト行列構築の前に行う）
        // UI はヒットストップ・スローモーション中も止めないので実時間で進める
        HAGINE_CPU_PROFILE("Update/UIAnimator");
        UIAnimator::GetInstance()->Update(Frame::UnscaledDeltaTime());
    }
    {
        HAGINE_CPU_PROFILE("Update/Sprites");
        pSpriteManager_->UpdateAll(Frame::UnscaledDeltaTime());
    }
    {
        // テクスチャ差し替えで退避したリソースを、GPUが使い終わった頃に解放する
        pTextureManager_->EndFrame();
    }
    if (updateGameWorld)
    {
        HAGINE_CPU_PROFILE("Update/Collision");
        pCollisionManager_->Update();
    }
    {
        // オブジェクトのワールド行列が確定した後に、種類をまたいだ親子付けを解決する。
        // 光源やパーティクルの位置をここで親に合わせるので、ライト更新より前に行う。
        HAGINE_CPU_PROFILE("Update/Attachment");
        AttachmentManager::GetInstance()->Update();
    }
    {
        HAGINE_CPU_PROFILE("Update/Light");
        LightGroup::GetInstance()->Update(*pSceneManager_->GetBaseScene()->GetViewProjection());
        ToonSettings::GetInstance()->Update();
    }
    {
        // 3Dオーディオの聞き手をカメラに合わせ、フェード進行と再生済みボイスの回収も行う
        HAGINE_CPU_PROFILE("Update/Audio");
        // 音はゲーム時間を止めても鳴り続けるので実時間で進める
        // （ゲーム時間で割るとヒットストップ中のカメラの揺れがドップラーを暴れさせる）
        const float deltaTime = Frame::UnscaledDeltaTime();
        if (BaseScene *currentScene = pSceneManager_->GetBaseScene())
        {
            const ViewProjection &viewProjection = *currentScene->GetViewProjection();
            SoundListener listener;
            listener.position = viewProjection.translation_;
            // カメラのワールド行列の基底が、そのまま右方向と前方向になる
            listener.right = Vector3(viewProjection.matWorld_.m[0][0], viewProjection.matWorld_.m[0][1],
                                     viewProjection.matWorld_.m[0][2])
                                 .Normalize();
            listener.forward = Vector3(viewProjection.matWorld_.m[2][0], viewProjection.matWorld_.m[2][1],
                                       viewProjection.matWorld_.m[2][2])
                                   .Normalize();
            // ドップラー用の速度は前フレームとの差から求める。
            // シーン切り替え直後は位置が飛ぶので、その1フレームだけ速度0にする
            if (audioListenerInitialized_ && deltaTime > 0.0f)
            {
                listener.velocity = (listener.position - audioListenerPreviousPosition_) * (1.0f / deltaTime);
            }
            audioListenerPreviousPosition_ = listener.position;
            audioListenerInitialized_ = true;
            pAudio_->SetListener(listener);
        }
        pAudio_->Update(deltaTime);
    }

    {
        HAGINE_CPU_PROFILE("Update/Input");
        pInput_->Update();
        shortcutManager_->Update();
        endRequest_ = winApp_->ProcessMessage();
    }

#ifdef USE_IMGUI
    // コマ送りの1フレームをここで消費する
    PlayModeManager::GetInstance()->EndFrame();
#endif // USE_IMGUI

    // ウィンドウサイズが変わっていたらスワップチェーンを追従させる
    // （内部レンダリング解像度は固定のまま、最終合成時に拡縮される）
    {
        uint32_t newWidth = 0, newHeight = 0;
        if (winApp_->ConsumeResize(newWidth, newHeight))
        {
            pDxCommon_->ResizeSwapChain(newWidth, newHeight);
        }
    }
}

void Framework::LoadResource()
{

    pTextureManager_->LoadAllTextures();

    // UIの文字はここで焼いた高さのピクセル数でそのまま画像化される（アトラスは自動で広がる）。
    // 画面に出す最大の文字より大きく取っておけば、拡大してもぼやけない
    pTextureManager_->LoadFontTexture("NotoSansJP-Medium.ttf", 256);

    ImGuiNotification::Post("全ての基本リソースを読み込みました", {0.2f, 0.8f, 0.2f, 1.0f});
    Logger::Info("All base resources loaded.");
}

void Framework::PlaySounds()
{
}

void Framework::Draw()
{
}
} // namespace Hagine
