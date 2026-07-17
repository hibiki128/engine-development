#pragma once
#include "Camera/ViewProjection/ViewProjection.h"
#include "Data/DataHandler.h"
#include "myMath.h"
#include "type/Quaternion.h"
#include "type/Vector3.h"
#include <functional>
#include <memory>
#include <string>
#include <unordered_set>

namespace Hagine {

/// <summary>
/// コライダーのタグを一元管理するシングルトン
/// 利用可能なタグの登録・削除・存在確認を行う
/// </summary>
class ColliderTagManager {
  public:
    /// ===================================================
    /// public method
    /// ===================================================

    /// <summary>
    /// インスタンスを取得
    /// </summary>
    /// <returns>ColliderTagManager*: シングルトンインスタンス</returns>
    static ColliderTagManager *GetInstance() {
        static ColliderTagManager instance;
        return &instance;
    }

    /// <summary>
    /// タグを追加
    /// </summary>
    /// <param name="tag">追加するタグ名</param>
    void AddTag(const std::string &tag) {
        if (!tag.empty()) {
            availableTags_.insert(tag);
        }
    }

    /// <summary>
    /// タグを削除
    /// </summary>
    /// <param name="tag">削除するタグ名</param>
    void RemoveTag(const std::string &tag) {
        availableTags_.erase(tag);
    }

    /// <summary>
    /// 全タグを取得
    /// </summary>
    /// <returns>const std::unordered_set&lt;std::string&gt;&: 登録済みタグの集合</returns>
    const std::unordered_set<std::string> &GetAllTags() const {
        return availableTags_;
    }

    /// <summary>
    /// タグが存在するか確認
    /// </summary>
    /// <param name="tag">確認するタグ名</param>
    /// <returns>bool: 存在すれば true</returns>
    bool HasTag(const std::string &tag) const {
        return availableTags_.find(tag) != availableTags_.end();
    }

    /// <summary>
    /// デフォルトタグを初期化
    /// </summary>
    void InitializeDefaultTags() {
        AddTag("None");
        AddTag("Environment");
        AddTag("Player");
        AddTag("Enemy");
        AddTag("Projectile");
        AddTag("Makan");
        AddTag("PlayerBullet");
        AddTag("PlayerChargeBullet");
        AddTag("PlayerHand");
        AddTag("EnemyBullet");
        AddTag("EnemyHand");
        AddTag("PlayerWall");
        AddTag("EnemyWall");
        AddTag("CylinderField");
    }

#ifdef _DEBUG
    /// <summary>
    /// ImGuiでタグ管理UIを表示
    /// </summary>
    void ImGuiTagManager();
#endif

  private:
    /// ===================================================
    /// private method
    /// ===================================================

    /// <summary>
    /// コンストラクタ（デフォルトタグを初期化）
    /// </summary>
    ColliderTagManager() {
        InitializeDefaultTags();
    }

    /// <summary>
    /// デストラクタ
    /// </summary>
    ~ColliderTagManager() = default;
    ColliderTagManager(const ColliderTagManager &) = delete;
    ColliderTagManager &operator=(const ColliderTagManager &) = delete;

    /// ===================================================
    /// private variants
    /// ===================================================

    std::unordered_set<std::string> availableTags_; // 利用可能なタグの集合
};
} // namespace Hagine
