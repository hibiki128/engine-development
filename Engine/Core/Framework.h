#pragma once
#include "DirectXCommon.h"
#ifdef _DEBUG
#endif // _DEBUG
#include "Audio.h"
#include "Collider/CollisionManager.h"
#include "Debug/ImGui/ImGuiManager.h"
#include "Debug/ImGui/ImGuizmoManager.h"
#include "Debug/ResourceLeakChecker/D3DResourceLeakChecker.h"
#include "Edit/ShortcutManager/ShortcutManager.h"
#include "offscreen/OffScreen.h"
#include "Graphics/Model/ModelManager.h"
#include "Graphics/PipeLine/ComputePipeLineManager.h"
#include "Graphics/PipeLine/PipeLineManager.h"
#include "Graphics/Srv/SrvManager.h"
#include "Graphics/Texture/TextureManager.h"
#include "Input.h"
#include "Model/ModelCommon.h"
#include "Object/Base/BaseObjectManager.h"
#include "Particle/CSParticle/ParticleCSEditor.h"
#include "Particle/CSParticle/ParticleCSFieldManager.h"
#include "Particle/CSParticle/ParticleCSGroupManager.h"
#include "Particle/ParticleCommon.h"
#include "Particle/ParticleEditor.h"
#include "Particle/ParticleGroupManager.h"
#include "Scene/SceneManager.h"
#include "Scene/SceneTransition.h"
#include "SkyBox/SkyBox.h"
#include "SpriteCommon.h"
#include "SpriteManager.h"
#include "line/DrawLine3D.h"
#include"Utility/LoadFile/Csv/CsvLoad.h"
#include "Render/DrawSystem.h"
namespace Hagine {
class Framework {
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
    Input *input_ = nullptr;
    Audio *audio_ = nullptr;
    DirectXCommon *dxCommon_ = nullptr;
    WinApp *winApp_ = nullptr;
    DrawLine3D *line3d_ = nullptr;
    SkyBox *skyBox_ = nullptr;

    SceneTransition *sceneTransition_ = nullptr;

    SceneManager *sceneManager_ = nullptr;
    SrvManager *srvManager_ = nullptr;
    TextureManager *textureManager_ = nullptr;
    ModelManager *modelManager_ = nullptr;
    ImGuiManager *imGuiManager_ = nullptr;
    ImGuizmoManager *imGuizmoManager_ = nullptr;
    BaseObjectManager *baseObjectManager_ = nullptr;
    ParticleGroupManager *particleGroupManager_ = nullptr;
    ParticleCSGroupManager *particleCSGroupManager_ = nullptr;
    PipeLineManager *pipeLineManager_ = nullptr;
    ComputePipeLineManager *computePipeLineManager_ = nullptr;
    ShortcutManager *shortcutManager_ = nullptr;
    SpriteManager *spriteManager_ = nullptr;

    SpriteCommon *spriteCommon_ = nullptr;
    ParticleCommon *particleCommon_ = nullptr;
    ModelCommon *modelCommon_ = nullptr;

    LightGroup *lightGroup_ = nullptr;

    ParticleEditor *particleEditor_ = nullptr;
    ParticleCSEditor *particleCSEditor_ = nullptr;
    ParticleCSFieldManager *particleCSFieldManager_ = nullptr;

    PrimitiveModel *primitiveModel_ = nullptr;

    CollisionManager *collisionManager_ = nullptr;

    CsvLoad* csvLoad_ = nullptr;

    std::unique_ptr<OffScreen> offscreen_;

    DrawSystem *drawSystem_ = nullptr;

    bool endRequest_;
};
} // namespace Hagine
