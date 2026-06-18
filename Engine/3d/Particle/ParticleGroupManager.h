#pragma once

#include "Data/DataHandler.h"
#include "ParticleGroup.h"
#include "memory"

namespace Hagine {

/// <summary>
/// パーティクルグループを一元管理するシングルトン
/// グループの生成・複製・取得を行い、エミッター用の独立コピーも提供する
/// </summary>
class ParticleGroupManager {
  private:
    /// ===================================================
    /// private method
    /// ===================================================

    /// <summary>
    /// コンストラクタ
    /// </summary>
    ParticleGroupManager() = default;

    /// <summary>
    /// デストラクタ
    /// </summary>
    ~ParticleGroupManager() = default;
    ParticleGroupManager(ParticleGroupManager &) = delete;
    ParticleGroupManager &operator=(ParticleGroupManager &) = delete;

  public:
    /// ===================================================
    /// public method
    /// ===================================================

    /// <summary>
    /// インスタンスを取得
    /// </summary>
    /// <returns>ParticleGroupManager*: シングルトンインスタンス</returns>
    static ParticleGroupManager* GetInstance() {
          static ParticleGroupManager instance;
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
    /// パーティクルグループを追加
    /// </summary>
    /// <param name="particleGroup">追加するパーティクルグループ</param>
    void AddParticleGroup(std::unique_ptr<ParticleGroup> particleGroup);

    /// <summary>
    /// モデルファイルからパーティクルグループを生成
    /// </summary>
    /// <param name="groupName">グループ名</param>
    /// <param name="filename">モデルファイル名</param>
    /// <param name="texturePath">テクスチャパス（省略可）</param>
    void CreateParticleGroup(const std::string &groupName, const std::string &filename, const std::string &texturePath = {});

    /// <summary>
    /// プリミティブ形状からパーティクルグループを生成
    /// </summary>
    /// <param name="groupName">グループ名</param>
    /// <param name="type">プリミティブの種類</param>
    /// <param name="texturePath">テクスチャパス</param>
    void CreatePrimitiveParticleGroup(const std::string &groupName, PrimitiveType type, const std::string &texturePath);

    /// <summary>
    /// 名前を指定してパーティクルグループを取得（参照用）
    /// </summary>
    /// <param name="name">グループ名</param>
    /// <returns>ParticleGroup*: 該当グループ（なければ nullptr）</returns>
    ParticleGroup *GetParticleGroup(const std::string &name) {
        for (const auto &group : particleGroups_) {
            if (group->GetGroupName() == name) {
                return group.get();
            }
        }
        return nullptr;
    }

    /// <summary>
    /// 指定グループの複製を作成する
    /// </summary>
    /// <param name="name">複製元のグループ名</param>
    /// <returns>std::unique_ptr&lt;ParticleGroup&gt;: 複製されたグループ（元がなければ nullptr）</returns>
    std::unique_ptr<ParticleGroup> CreateParticleGroupCopy(const std::string &name) {
        ParticleGroup *originalGroup = GetParticleGroup(name);
        if (!originalGroup) {
            return nullptr;
        }

        auto copiedGroup = std::make_unique<ParticleGroup>();

        // プリミティブタイプか通常のモデルかを判定してコピー
        if (originalGroup->GetPrimitiveType() != PrimitiveType::None) {
            // プリミティブパーティクルグループの場合
            std::string texturePath = originalGroup->GetParticleGroupData().materials.empty() ? "" : originalGroup->GetParticleGroupData().materials[0].textureFilePath;
            copiedGroup->CreatePrimitiveParticleGroup(name, originalGroup->GetPrimitiveType(), texturePath);
        } else {
            // 通常のモデルパーティクルグループの場合
            std::string texturePath = originalGroup->GetParticleGroupData().materials.empty() ? "" : originalGroup->GetParticleGroupData().materials[0].textureFilePath;
            copiedGroup->CreateParticleGroup(name, originalGroup->GetModelPath(), texturePath);
        }

        return copiedGroup;
    }

    /// <summary>
    /// エミッター用の独立したパーティクルグループを取得（複製を生成して保持）
    /// </summary>
    /// <param name="name">複製元のグループ名</param>
    /// <returns>ParticleGroup*: 独立した複製グループ（元がなければ nullptr）</returns>
    ParticleGroup *GetIndependentParticleGroup(const std::string &name) {
        auto copiedGroup = CreateParticleGroupCopy(name);
        if (!copiedGroup) {
            return nullptr;
        }

        ParticleGroup *groupPtr = copiedGroup.get();
        independentGroups_.emplace_back(std::move(copiedGroup));
        return groupPtr;
    }

    /// <summary>
    /// 管理中の全パーティクルグループを取得
    /// </summary>
    /// <returns>std::vector&lt;ParticleGroup*&gt;: 全グループの生ポインタ一覧</returns>
    std::vector<ParticleGroup *> GetParticleGroups() {
        std::vector<ParticleGroup *> result;
        for (const auto &group : particleGroups_) {
            result.push_back(group.get()); // unique_ptr から生ポインタを取得
        }
        return result;
    }

  private:
    /// ============================================
    /// private variants
    /// ============================================

    std::vector<std::unique_ptr<ParticleGroup>> particleGroups_;   // 管理する全パーティクルグループ
    std::vector<std::unique_ptr<ParticleGroup>> independentGroups_; // エミッター用の独立したパーティクルグループ
};
} // namespace Hagine
