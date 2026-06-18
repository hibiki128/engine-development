#pragma once
#include "Camera/ViewProjection/ViewProjection.h"
#include "Graphics/Srv/SrvManager.h"
#include "ParticleCommon.h"
#include "ParticleGroup.h"
#include "type/Vector2.h"
#include "type/Vector3.h"
#include "type/Vector4.h"
#include <Model/ModelStructs.h>
#include <Transform/WorldTransform.h>
#include <random>
#include <type/Matrix4x4.h>
#include <unordered_map>
#include"ParticleStruct.h"

namespace Hagine {

/// <summary>
/// CPUパーティクルの更新・描画・発生を管理するクラス
/// グループごとの設定を保持し、パーティクルの生成と寿命管理を行う
/// </summary>
class ParticleManager {
  public:
    /// ===================================================
    /// public method
    /// ===================================================

    /// <summary>
    /// 初期化
    /// </summary>
    /// <param name="srvManager">SRVマネージャー</param>
    void Initialize(SrvManager *srvManager);

    /// <summary>
    /// 更新処理
    /// </summary>
    /// <param name="viewProjeciton">ビュープロジェクション</param>
    void Update(const ViewProjection &viewProjeciton);

    /// <summary>
    /// 描画処理
    /// </summary>
    void Draw();

    /// <summary>
    /// パーティクルグループを追加
    /// </summary>
    /// <param name="particleGroup">追加するパーティクルグループ</param>
    void AddParticleGroup(ParticleGroup *particleGroup);

    /// <summary>
    /// 名前を指定してパーティクルグループを削除
    /// </summary>
    /// <param name="name">削除するグループ名</param>
    void RemoveParticleGroup(const std::string &name);

    /// <summary>
    /// グループごとのパーティクル設定を設定
    /// </summary>
    /// <param name="groupName">グループ名</param>
    /// <param name="setting">設定</param>
    void SetParticleSetting(const std::string &groupName, const ParticleSetting &setting);

    /// <summary>
    /// グループごとのパーティクル設定を取得
    /// </summary>
    /// <param name="groupName">グループ名</param>
    /// <returns>ParticleSetting&: 該当グループの設定</returns>
    ParticleSetting &GetParticleSetting(const std::string &groupName);

    /// <summary>
    /// 登録済みのグループ名一覧を取得
    /// </summary>
    /// <returns>std::vector&lt;std::string&gt;: グループ名一覧</returns>
    std::vector<std::string> GetParticleGroupsName();

    /// <summary>
    /// 指定グループの軌跡（トレイル）の有効/無効を設定
    /// </summary>
    /// <param name="groupName">グループ名</param>
    /// <param name="enabled">有効にするなら true</param>
    void SetTrailEnabled(const std::string &groupName, bool enabled);

    /// <summary>
    /// 指定グループの軌跡設定を設定
    /// </summary>
    /// <param name="groupName">グループ名</param>
    /// <param name="interval">生成間隔</param>
    /// <param name="maxTrails">最大軌跡数</param>
    void SetTrailSettings(const std::string &groupName, float interval, int maxTrails);

    /// <summary>
    /// エミッターの中心位置を設定
    /// </summary>
    /// <param name="center">中心位置</param>
    void SetEmitterCenter(Vector3 center) { emitterCenter_ = center; }

    /// <summary>
    /// 全てのパーティクルが消えたかチェック
    /// </summary>
    /// <returns>bool: 全て消えていれば true</returns>
    bool IsAllParticlesComplete() const;

    /// <summary>
    /// 特定のグループのパーティクルが全て消えたかチェック
    /// </summary>
    /// <param name="groupName">グループ名</param>
    /// <returns>bool: 全て消えていれば true</returns>
    bool IsParticleGroupComplete(const std::string &groupName) const;

    /// <summary>
    /// アクティブなパーティクルの総数を取得
    /// </summary>
    /// <returns>size_t: アクティブなパーティクル数</returns>
    size_t GetActiveParticleCount() const;

    /// <summary>
    /// 特定のグループのアクティブなパーティクル数を取得
    /// </summary>
    /// <param name="groupName">グループ名</param>
    /// <returns>size_t: アクティブなパーティクル数</returns>
    size_t GetActiveParticleCount(const std::string &groupName) const;

  private:
    /// ===================================================
    /// private variants
    /// ===================================================

    ParticleCommon *particleCommon_ = nullptr;                          // パーティクル共通描画処理
    SrvManager *srvManager_;                                            // SRVマネージャー
    std::unordered_map<std::string, ParticleGroup *> particleGroups_;   // 名前ごとのパーティクルグループ
    std::unordered_map<std::string, ParticleSetting> particleSettings_; // 名前ごとのパーティクル設定

    std::vector<std::string> particleGroupNames_; // グループ名一覧
    std::random_device seedGenerator_;            // 乱数シード生成器
    std::mt19937 randomEngine_;                   // 乱数生成エンジン
    Vector3 emitterCenter_{};                     // エミッターの中心位置

  public:
    /// <summary>
    /// パーティクルを発生させる
    /// </summary>
    /// <returns>std::list&lt;Particle&gt;: 発生したパーティクル一覧</returns>
    std::list<Particle> Emit();

  private:
    /// <summary>
    /// 親パーティクルから軌跡パーティクルを生成
    /// </summary>
    /// <param name="parent">親パーティクル</param>
    /// <param name="setting">パーティクル設定</param>
    void CreateTrailParticle(const Particle &parent, const ParticleSetting &setting);

    /// <summary>
    /// 設定に基づいて新しいパーティクルを1つ生成
    /// </summary>
    /// <param name="randomEngine_">乱数生成エンジン</param>
    /// <param name="setting">パーティクル設定</param>
    /// <returns>Particle: 生成されたパーティクル</returns>
    Particle MakeNewParticle(std::mt19937 &randomEngine_, const ParticleSetting &setting);
};
} // namespace Hagine
