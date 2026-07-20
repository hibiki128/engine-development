#pragma once
#include "Audio.h"
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
#include "line/DrawLine3D.h"
#ifdef _DEBUG
#include <imgui.h>
#endif // _DEBUG
#include <OffScreen.h>
#include "SpriteManager.h"
#include "object/base/BaseObjectManager.h"
#include "render/DrawSystem.h"

namespace Hagine {
class SceneManager;
class WinApp;

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

    void SetOffScreen(OffScreen *offscreen) { pOffScreen_ = offscreen; }
    void SetDrawSystem(DrawSystem *drawSystem) { pDrawSystem_ = drawSystem; }
    void SetWinApp(WinApp *winApp) { pWinApp_ = winApp; }

    void DrawParticleEditorUI();

    void DrawAllObjects();

    ViewProjection *GetViewProjection() { return &vp_; }

  protected:
    // シーンマネージャ
    Audio *pAudio_ = nullptr;
    Input *pInput_ = nullptr;
    LightGroup *pLightGroup_ = nullptr;
    ParticleEditor *pPtEditor_ = nullptr;
    ParticleCSEditor *pPtCSEditor_ = nullptr;
    OffScreen *pOffScreen_ = nullptr;

    ViewProjection vp_;
    std::unique_ptr<DebugCamera> debugCamera_;

    SceneManager *pSceneManager_ = nullptr;
    WinApp *pWinApp_ = nullptr;
    SpriteManager *pSpriteManager_ = nullptr;
    BaseObjectManager *pObjectManager_ = nullptr;
    DrawSystem *pDrawSystem_ = nullptr;

    float ClearTime_ = 0.0f;
    float HP_ = 0.0f;
};
} // namespace Hagine
