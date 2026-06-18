#pragma once
#include "Data/DataHandler.h"
#include <Particle/CSParticle/ParticleCSGroup.h>
#include <algorithm>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
namespace Hagine {

/// <summary>
/// GPUパーティクルグループを一元管理するシングルトン
/// グループの生成・複製・削除に加え、エミッター用独立グループの再利用プールを管理し、
/// クローン毎のGPUバッファ確保とSRVの入れ替わりを抑制する
/// </summary>
class ParticleCSGroupManager {
  private:
    /// ===================================================
    /// private methods
    /// ===================================================

    /// <summary>
    /// コンストラクタ
    /// </summary>
    ParticleCSGroupManager() = default;

    /// <summary>
    /// デストラクタ
    /// </summary>
    ~ParticleCSGroupManager() = default;
    ParticleCSGroupManager(ParticleCSGroupManager &) = delete;
    ParticleCSGroupManager &operator=(ParticleCSGroupManager &) = delete;

  public:
    /// ===================================================
    /// public methods
    /// ===================================================

    /// <summary>
    /// インスタンスを取得
    /// </summary>
    /// <returns>ParticleCSGroupManager*: シングルトンインスタンス</returns>
    static ParticleCSGroupManager *GetInstance() {
        static ParticleCSGroupManager instance;
        return &instance;
    }

    /// <summary>
    /// 初期化
    /// </summary>
    void Initialize();

    /// <summary>
    /// 終了処理
    /// </summary>
    void Finalize();

    /// <summary>
    /// GPUパーティクルグループを追加
    /// </summary>
    /// <param name="particleCSGroup">追加するグループ</param>
    void AddParticleCSGroup(std::unique_ptr<ParticleCSGroup> particleCSGroup);

    /// <summary>
    /// モデルファイルからGPUパーティクルグループを生成
    /// </summary>
    /// <param name="groupName">グループ名</param>
    /// <param name="fileName">モデルファイル名</param>
    /// <param name="maxParticleCount">最大パーティクル数</param>
    /// <param name="texturePath">テクスチャパス（省略可）</param>
    /// <param name="blendMode">ブレンドモード</param>
    void CreateParticleCSGroup(const std::string &groupName, const std::string &fileName, uint32_t maxParticleCount = 10000, const std::string &texturePath = {}, BlendMode blendMode = BlendMode::kAdd);

    /// <summary>
    /// プリミティブ形状からGPUパーティクルグループを生成
    /// </summary>
    /// <param name="groupName">グループ名</param>
    /// <param name="type">プリミティブの種類</param>
    /// <param name="maxParticleCount">最大パーティクル数</param>
    /// <param name="texturePath">テクスチャパス（省略可）</param>
    /// <param name="blendMode">ブレンドモード</param>
    void CreatePrimitiveParticleCSGroup(const std::string &groupName, PrimitiveType type, uint32_t maxParticleCount = 10000, const std::string &texturePath = {}, BlendMode blendMode = BlendMode::kAdd);

    /// <summary>
    /// 名前を指定してGPUパーティクルグループを取得
    /// </summary>
    /// <param name="name">グループ名</param>
    /// <returns>ParticleCSGroup*: 該当グループ（なければ nullptr）</returns>
    ParticleCSGroup *GetParticleCSGroup(const std::string &name) {
        for (const auto &group : particleGroups_) {
            if (group->GetGroupName() == name) {
                return group.get();
            }
        }
        return nullptr;
    }

    std::unique_ptr<ParticleCSGroup> CreateParticleCSGroupCopy(const std::string &name) {
        ParticleCSGroup *originalGroup = GetParticleCSGroup(name);
        if (!originalGroup) {
            return nullptr;
        }

        auto copiedGroup = std::make_unique<ParticleCSGroup>();

        // プリミティブタイプか通常のモデルかを判定してコピー
        if (originalGroup->GetPrimitiveType() != PrimitiveType::None) {
            // プリミティブパーティクルグループの場合
            std::string texturePath = originalGroup->GetParticleGroupData().materials.empty() ? "" : originalGroup->GetParticleGroupData().materials[0].textureFilePath;
            uint32_t maxParticleCount = originalGroup->GetSettingsData()->maxParticleCount;
            copiedGroup->CreatePrimitiveParticleGroup(name, originalGroup->GetPrimitiveType(), maxParticleCount, texturePath);
        } else {
            // 通常のモデルパーティクルグループの場合
            std::string texturePath = originalGroup->GetParticleGroupData().materials.empty() ? "" : originalGroup->GetParticleGroupData().materials[0].textureFilePath;
            uint32_t maxParticleCount = originalGroup->GetSettingsData()->maxParticleCount;
            copiedGroup->CreateParticleGroup(name, originalGroup->GetModelPath(), maxParticleCount, texturePath);
        }

        return copiedGroup;
    }

    // エミッター用の独立したパーティクルグループを取得する。
    // テンプレート名キーの再利用プールに空きがあれば GPU バッファ/SRV を
    // 再確保せず使い回し（InitParticle で状態だけリセット）、無ければ新規生成する。
    // これによりクローン毎の maxParticleCount*sizeof(CSParticle) 確保 + SRV churn を解消する。
    ParticleCSGroup *GetIndependentParticleGroup(const std::string &name) {
        auto poolIt = groupPool_.find(name);
        if (poolIt != groupPool_.end() && !poolIt->second.empty()) {
            std::unique_ptr<ParticleCSGroup> reused = std::move(poolIt->second.back());
            poolIt->second.pop_back();
            ParticleCSGroup *groupPtr = reused.get();
            groupPtr->ResetForReuse(); // GPU 上のパーティクル状態を初期化し直す
            independentGroups_.emplace_back(std::move(reused));
            return groupPtr;
        }

        // プールに無ければ新規生成（従来通り）
        auto copiedGroup = CreateParticleCSGroupCopy(name);
        if (!copiedGroup) {
            return nullptr;
        }

        ParticleCSGroup *groupPtr = copiedGroup.get();
        independentGroups_.emplace_back(std::move(copiedGroup));
        return groupPtr;
    }

    // 使用済みの独立グループを破棄せず再利用プールへ返却する。
    // エミッター破棄時に呼ぶことで、シーン内での無制限なバッファ累積を防ぐ。
    //
    // 重要: 引数 group は既に破棄済み(ダングリング)の可能性があるため、
    //       ここでは「ポインタ値の比較」しか行わずデリファレンスしない。
    //       名前は所有側 (*it) の有効なオブジェクトから取得する。
    //       Finalize 後など independentGroups_ が空なら単に何もしない（安全）。
    void ReleaseIndependentGroup(ParticleCSGroup *group) {
        if (!group) {
            return;
        }
        for (auto it = independentGroups_.begin(); it != independentGroups_.end(); ++it) {
            if (it->get() == group) {                          // ポインタ比較のみ（derefしない）
                const std::string name = (*it)->GetGroupName(); // 所有側は有効なので安全
                auto &pool = groupPool_[name];
                if (pool.size() < kMaxPooledPerTemplate) {
                    pool.emplace_back(std::move(*it)); // 上限内なら再利用のため保持
                }
                // 上限超過分は unique_ptr 破棄（GPU リソース解放）
                independentGroups_.erase(it);
                return;
            }
        }
        // 見つからない（既に返却済み / Finalize 済み）→ 何もしない
    }

    std::vector<ParticleCSGroup *> GetParticleGroups() {
        std::vector<ParticleCSGroup *> result;
        for (const auto &group : particleGroups_) {
            result.push_back(group.get()); // unique_ptr から生ポインタを取得
        }
        return result;
    }

    // シーン遷移時に呼ばれる。使用中の独立グループを破棄せず（上限内で）プールへ
    // 退避し、次シーンで再利用できるようにする。上限超過分のみ解放する。
    void ClearIndependentGroups() {
        for (auto &group : independentGroups_) {
            if (!group) {
                continue;
            }
            auto &pool = groupPool_[group->GetGroupName()];
            if (pool.size() < kMaxPooledPerTemplate) {
                pool.emplace_back(std::move(group));
            }
        }
        independentGroups_.clear(); // 退避できなかった(上限超過)分はここで解放
    }

    // 名前を指定してパーティクルグループを削除する
    // particleGroups_ と independentGroups_ の両方から探して削除する
    void RemoveParticleCSGroup(const std::string &groupName) {
        // particleGroups_ から削除
        particleGroups_.erase(
            std::remove_if(particleGroups_.begin(), particleGroups_.end(),
                           [&groupName](const std::unique_ptr<ParticleCSGroup> &group) {
                               return group->GetGroupName() == groupName;
                           }),
            particleGroups_.end());
        // independentGroups_ からも削除（エミッターにアタッチされたコピー）
        independentGroups_.erase(
            std::remove_if(independentGroups_.begin(), independentGroups_.end(),
                           [&groupName](const std::unique_ptr<ParticleCSGroup> &group) {
                               return group->GetGroupName() == groupName;
                           }),
            independentGroups_.end());
    }

    void RemoveUnusedIndependentGroups(const std::unordered_set<std::string> &usedGroupNames) {
        independentGroups_.erase(
            std::remove_if(independentGroups_.begin(), independentGroups_.end(),
                           [&usedGroupNames](const std::unique_ptr<ParticleCSGroup> &group) {
                               return usedGroupNames.find(group->GetGroupName()) == usedGroupNames.end();
                           }),
            independentGroups_.end());
    }

  private:
    /// ===================================================
    /// private variaus
    /// ===================================================

    std::vector<std::unique_ptr<ParticleCSGroup>> particleGroups_;

    // 現在エミッターに割り当て中の独立グループ
    std::vector<std::unique_ptr<ParticleCSGroup>> independentGroups_;

    // 解放済みグループの再利用プール（テンプレート名 → 空きグループ群）
    std::unordered_map<std::string, std::vector<std::unique_ptr<ParticleCSGroup>>> groupPool_;

    // テンプレートごとのプール保持上限（メモリの青天井退避を防ぐ）
    static constexpr size_t kMaxPooledPerTemplate = 32;
};
} // namespace Hagine
