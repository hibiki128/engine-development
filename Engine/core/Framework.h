#pragma once
#include "DirectXCommon.h"
#ifdef _DEBUG
#endif // _DEBUG
#include "Audio.h"
#include "collider/CollisionManager.h"
#include "debug/imgui/ImGuiManager.h"
#include "debug/imgui/ImGuizmoManager.h"
#include "debug/leak/D3DResourceLeakChecker.h"
#include "edit/shortcut/ShortcutManager.h"
#include "offscreen/OffScreen.h"
#include "graphics/model/ModelManager.h"
#include "graphics/pipeline/ComputePipelineManager.h"
#include "graphics/pipeline/PipelineManager.h"
#include "graphics/srv/SrvManager.h"
#include "graphics/texture/TextureManager.h"
#include "Input.h"
#include "model/ModelCommon.h"
#include "object/base/BaseObjectManager.h"
#include "particle/gpu/ParticleCSEditor.h"
#include "particle/gpu/ParticleCSFieldManager.h"
#include "particle/gpu/ParticleCSGroupManager.h"
#include "particle/ParticleCommon.h"
#include "particle/ParticleEditor.h"
#include "particle/ParticleGroupManager.h"
#include "scene/SceneManager.h"
#include "scene/SceneTransition.h"
#include "skybox/SkyBox.h"
#include "SpriteCommon.h"
#include "SpriteManager.h"
#include "line/DrawLine3D.h"
#include "edit/motion/MotionEditor.h"
#include "utility/loader/csv/CsvLoad.h"
#include "render/DrawSystem.h"
#include <memory>
namespace Hagine {
class Framework
{
  public: // メンバ関数
    virtual ~Framework() = default;

    /// <summary>
    /// 実行
    /// </summary>
    void Run();

    /// <summary>
    /// 初期化
    /// </summary>
    virtual void Initialize();

    /// <summary>
    /// 終了
    /// </summary>
    virtual void Finalize();

    /// <summary>
    /// ショートカットキーの登録
    /// </summary>
    void RegisterShortcutKey();

    /// <summary>
    /// 更新
    /// </summary>
    virtual void Update();

    /// <summary>
    /// リソース
    /// </summary>
    void LoadResource();

    /// <summary>
    /// 描画
    /// </summary>
    virtual void Draw() = 0;

    void PlaySounds();

    /// <summary>
    /// 終了チェック
    /// </summary>
    /// <returns></returns>
    virtual bool IsEndRequest() { return endRequest_; }

  protected:
    D3DResourceLeakChecker LeakChecker_;

    // ---- Framework が所有するインスタンス（生成・寿命を管理し、必要な所へ注入する）----
    std::unique_ptr<WinApp> winApp_;
    std::unique_ptr<SceneTransition> sceneTransition_;
    std::unique_ptr<ImGuiManager> imGuiManager_;
    std::unique_ptr<ShortcutManager> shortcutManager_;
    std::unique_ptr<ModelCommon> modelCommon_;
    std::unique_ptr<CsvLoad> csvLoad_;
    std::unique_ptr<OffScreen> offscreen_;
    std::unique_ptr<DrawSystem> pDrawSystem_;

    // ---- 広域サービス（App 全域から参照されるため、現状は意図的にシングルトンのまま）----
    Input *pInput_ = nullptr;
    Audio *pAudio_ = nullptr;
    DirectXCommon *pDxCommon_ = nullptr;
    DrawLine3D *pLine3d_ = nullptr;
    SkyBox *pSkyBox_ = nullptr;

    SceneManager *pSceneManager_ = nullptr;
    SrvManager *pSrvManager_ = nullptr;
    TextureManager *pTextureManager_ = nullptr;
    ModelManager *pModelManager_ = nullptr;
    ImGuizmoManager *pImGuizmoManager_ = nullptr;
    BaseObjectManager *pBaseObjectManager_ = nullptr;
    ParticleGroupManager *pParticleGroupManager_ = nullptr;
    ParticleCSGroupManager *pParticleCSGroupManager_ = nullptr;
    PipelineManager *pPipeLineManager_ = nullptr;
    MotionEditor *pMotionEditor_ = nullptr;
    ComputePipelineManager *pComputePipelineManager_ = nullptr;
    SpriteManager *pSpriteManager_ = nullptr;

    SpriteCommon *pSpriteCommon_ = nullptr;
    ParticleCommon *pParticleCommon_ = nullptr;

    LightGroup *pLightGroup_ = nullptr;

    ParticleEditor *pParticleEditor_ = nullptr;
    ParticleCSEditor *pParticleCSEditor_ = nullptr;
    ParticleCSFieldManager *pParticleCSFieldManager_ = nullptr;

    PrimitiveModel *pPrimitiveModel_ = nullptr;

    CollisionManager *pCollisionManager_ = nullptr;

    bool endRequest_;
};
} // namespace Hagine
