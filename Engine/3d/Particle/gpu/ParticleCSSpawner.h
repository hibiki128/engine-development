#pragma once
#include "ParticleCSEmitter.h"
#include <memory>
#include <string>
#include <vector>

namespace Hagine {

class BaseObject;
class WorldTransform;

/// <summary>
/// 実行時にシーンへ置く GPU パーティクルエミッターを所有・駆動するシングルトン。
///
/// ParticleCSEditor が持つエミッターは「編集用」でプレビュー窓にしか描かれないため、
/// ゲームシーンに出したい場合はこちらへ Spawn する。生成したインスタンスの
/// 更新・コンピュート・描画は DrawSystem から自動で回るので、
/// 呼び出し側は「いつ出すか」「どんな値にするか」だけ書けばよい。
///
/// 使い方:
///     auto *portal = ParticleCSSpawner::GetInstance()->Spawn("portal");
///     portal->SetParent(enemy);              // 位置・回転が親に追従
///     portal->SetLocalTranslate({0, 2, 0});
///     portal->SetFrequency(0.02f);
///     ...
///     ParticleCSSpawner::GetInstance()->DespawnWhenFinished(portal);
///
/// 【寿命】インスタンスはこのマネージャーが所有する。Despawn するまで生き続け、
///   シーン遷移時は SetPersistent(true) にしたもの以外まとめて破棄される。
///   破棄後のポインタは無効になるため、長く持つ場合は IsAlive() で確認すること。
/// </summary>
class ParticleCSSpawner
{
  public:
    static ParticleCSSpawner *GetInstance()
    {
        static ParticleCSSpawner instance;
        return &instance;
    }

    /// <summary>
    /// テンプレート（Assets/jsons/ParticleCS/&lt;templateName&gt;.json）を読み込んで
    /// シーンに出るエミッターを生成する。
    /// </summary>
    /// <param name="templateName">テンプレート名（.json を除いたファイル名）</param>
    /// <returns>生成されたエミッター（失敗時 nullptr）。所有はマネージャー側</returns>
    ParticleCSEmitter *Spawn(const std::string &templateName);

    /// <summary>生成したエミッターを即座に破棄する（残っている粒子も消える）</summary>
    void Despawn(ParticleCSEmitter *emitter);

    /// <summary>
    /// 発生を止め、生き残っている粒子が消えた時点で自動的に破棄する。
    /// 撃ち切りの演出を自然に終わらせたいときに使う。
    /// </summary>
    void DespawnWhenFinished(ParticleCSEmitter *emitter);

    /// <summary>
    /// シーン遷移で破棄されないようにするか。既定は false（＝遷移時に破棄）。
    /// </summary>
    void SetPersistent(ParticleCSEmitter *emitter, bool persistent);

    /// <summary>そのポインタがまだ生きているか（Despawn 済みなら false）</summary>
    bool IsAlive(const ParticleCSEmitter *emitter) const;

    /// <summary>シーン遷移時に呼ぶ。persistent 以外を全て破棄する</summary>
    void ClearSceneScoped();

    /// <summary>全インスタンスを破棄する（アプリ終了時）</summary>
    void Finalize();

    /// ===================================================
    /// DrawSystem から呼ばれる駆動処理
    /// ===================================================

    /// <summary>更新＋コンピュート（DrawSystem の GPU パーティクル Compute フェーズ）</summary>
    void DrawCompute(const ViewProjection &vp);

    /// <summary>描画（stageIndex に属するエミッターのみ）</summary>
    /// <param name="vp">ビュープロジェクション</param>
    /// <param name="stageIndex">DrawGroupManager::kStage3D または kStageUI</param>
    void DrawGraphics(const ViewProjection &vp, int stageIndex);

    /// ===================================================
    /// デバッグ / UI
    /// ===================================================

    /// <summary>
    /// 実行時エミッターの生成・破棄を行う ImGui UI（呼び出し側のウィンドウ内に描く）。
    /// ImGuiManager の「パーティクル設定」ウィンドウから呼ばれる。
    /// </summary>
    void DrawImGui();

    size_t GetSpawnedCount() const { return instances_.size(); }

  private:
    ParticleCSSpawner() = default;
    ~ParticleCSSpawner() = default;
    ParticleCSSpawner(const ParticleCSSpawner &) = delete;
    ParticleCSSpawner &operator=(const ParticleCSSpawner &) = delete;

    struct Instance
    {
        std::unique_ptr<ParticleCSEmitter> emitter;
        std::string templateName;      // 生成元のテンプレート名
        bool persistent = false;       // シーン遷移で破棄しない
        bool despawnWhenFinished = false; // 粒子が尽きたら自動破棄
    };

    /// ポインタから該当インスタンスを引く（見つからなければ nullptr）
    Instance *Find(const ParticleCSEmitter *emitter);
    const Instance *Find(const ParticleCSEmitter *emitter) const;

    /// テンプレート名から重複しないインスタンス名を作る
    std::string MakeInstanceName(const std::string &templateName);

    /// Assets/jsons/ParticleCS 直下の .json を列挙する（拡張子なし）
    static std::vector<std::string> GetTemplateNames();

    std::vector<Instance> instances_;
    int instanceCounter_ = 0;

#ifdef USE_IMGUI
    int selectedTemplateIndex_ = 0;
    int selectedInstanceIndex_ = -1;
#endif
};
} // namespace Hagine
