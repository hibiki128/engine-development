#pragma once
#include "Audio.h"
#include "camera/CameraManager.h"
#include "camera/debug/DebugCamera.h"
#include "camera/projection/ViewProjection.h"
#include "Input.h"
#include "light/LightGroup.h"
#include "object/base/BaseObject.h"
#include "object/base/BaseObjectManager.h"
#include "object/Object3dCommon.h"
#include "particle/gpu/ParticleCSEditor.h"
#include "particle/ParticleCommon.h"
#include "particle/ParticleEditor.h"
#include "particle/ParticleEmitter.h"
#include "Sprite.h"
#include "SpriteCommon.h"
#include "SpriteManager.h"
#include "transform/WorldTransform.h"
#include "line/LineRenderer.h"
#ifdef USE_IMGUI
#include <imgui.h>
#endif // USE_IMGUI
#include <OffScreen.h>
#include "SpriteManager.h"
#include "object/base/BaseObjectManager.h"
#include "render/DrawSystem.h"

namespace Hagine {
class SceneManager;

class BaseScene
{
  public:
    virtual ~BaseScene() = default;

    /// <summary>
    /// 初期化
    /// </summary>
    virtual void Initialize();

    /// <summary>
    /// 終了
    /// </summary>
    virtual void Finalize();

    /// <summary>
    /// 更新
    /// </summary>
    virtual void Update();

    /// <summary>
    /// 描画
    /// </summary>
    virtual void Draw();

    /// <summary>
    /// ヒエラルキーに追加
    /// </summary>
    virtual void AddSceneSetting();

    /// <summary>
    /// インスペクターに追加
    /// </summary>
    virtual void AddObjectSetting();

    /// <summary>
    /// プロジェクトに追加
    /// </summary>
    virtual void AddParticleSetting();

    /// <summary>
    /// 描画
    /// </summary>
    virtual void DrawForOffScreen();

    virtual void SetSceneManager(SceneManager *sceneManager) { pSceneManager_ = sceneManager; }

    void SetOffScreen(OffScreen *pOffScreen) { pOffScreen_ = pOffScreen; }
    void SetDrawSystem(DrawSystem *drawSystem) { pDrawSystem_ = drawSystem; }
    void SetWinApp(WinApp *winApp) { pWinApp_ = winApp; }

    void DrawParticleEditorUI();

    void DrawAllObjects();

    /// ===================================================
    /// デバッグカメラ
    /// 生成・初期化は BaseScene::Initialize が行うので、派生シーンはこれらを呼ぶだけでよい
    /// ===================================================

    /// <summary>
    /// デバッグカメラを更新する（有効中は自動でデバッグカメラへ切り替わる）
    /// </summary>
    void UpdateDebugCamera();

    /// <summary>
    /// デバッグカメラの設定UIを描画する
    /// </summary>
    void DrawDebugCameraImGui();

    /// <summary>
    /// デバッグカメラが有効か（有効中はゲーム側のカメラ制御を止めるなどの判断に使う）
    /// </summary>
    /// <returns>bool: 有効なら true</returns>
    bool IsDebugCameraActive() const;

    /// <summary>
    /// デバッグカメラの有効／無効を切り替える（F3 のショートカットから呼ばれる）
    /// </summary>
    /// <returns>bool: 切り替え後の状態</returns>
    bool ToggleDebugCamera();

    /// <summary>
    /// デバッグカメラを取得する。
    /// シーンを作り直すときに編集中の視点を引き継ぐ用途で使う
    /// </summary>
    /// <returns>DebugCamera*: 未生成なら nullptr</returns>
    DebugCamera *GetDebugCamera() const { return debugCamera_.get(); }

    /// <summary>
    /// 描画へ渡すビュープロジェクションを取得する（今アクティブなカメラのもの）
    /// </summary>
    ViewProjection *GetViewProjection() { return &vp(); }

    /// <summary>
    /// カメラを追加する（名前で管理される。最初の1台は自動でアクティブになる）。
    /// アクティブカメラの結果はそのまま描画に使われるので、行列を手でコピーする必要はない。
    /// </summary>
    /// <param name="name">カメラ名</param>
    /// <returns>Camera*: 追加したカメラ（所有は CameraManager）</returns>
    Camera *AddCamera(const std::string &name) { return pCameraManager_->Create(name); }

    /// <summary>名前でカメラを取得する</summary>
    /// <param name="name">カメラ名</param>
    /// <returns>Camera*: 見つからなければ nullptr</returns>
    Camera *GetCamera(const std::string &name) { return pCameraManager_->Find(name); }

  protected:
    // シーンマネージャ
    Audio *pAudio_ = nullptr;
    Input *pInput_ = nullptr;
    LightGroup *pLightGroup_ = nullptr;
    ParticleEditor *pPtEditor_ = nullptr;
    ParticleCSEditor *pPtCSEditor_ = nullptr;
    OffScreen *pOffScreen_ = nullptr;
    WinApp *pWinApp_ = nullptr;

    /// <summary>
    /// このシーンのメインカメラ（BaseScene::Initialize で生成。所有は CameraManager）。
    /// 位置や向きはこのカメラに対して設定する。
    /// </summary>
    Camera *camera_ = nullptr;

    /// <summary>
    /// 描画へ渡すビュープロジェクション（今アクティブなカメラのもの）。
    /// ViewProjection を持つのは Camera だけなので、描画に渡すときはこれを経由する。
    /// </summary>
    ViewProjection &vp();

    std::unique_ptr<DebugCamera> debugCamera_;

    SceneManager *pSceneManager_ = nullptr;
    CameraManager *pCameraManager_ = nullptr;
    SpriteManager *pSpriteManager_ = nullptr;
    BaseObjectManager *pObjectManager_ = nullptr;
    DrawSystem *pDrawSystem_ = nullptr;

    float clearTime_ = 0.0f;
    float hp_ = 0.0f;
};
} // namespace Hagine
