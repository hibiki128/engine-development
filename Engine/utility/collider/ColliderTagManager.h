#pragma once
#include "camera/projection/ViewProjection.h"
#include "data/DataHandler.h"
#include "MyMath.h"
#include "type/Quaternion.h"
#include "type/Vector3.h"
#include <functional>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

namespace Hagine {

/// <summary>
/// コライダーのタグを一元管理するシングルトン
/// 利用可能なタグの登録・削除・存在確認を行う
/// </summary>
class ColliderTagManager
{
  public:
    /// ===================================================
    /// public method
    /// ===================================================

    /// <summary>
    /// インスタンスを取得
    /// </summary>
    /// <returns>ColliderTagManager*: シングルトンインスタンス</returns>
    static ColliderTagManager *GetInstance()
    {
        static ColliderTagManager instance;
        return &instance;
    }

    /// <summary>
    /// タグを追加
    /// </summary>
    /// <param name="tag">追加するタグ名</param>
    void AddTag(const std::string &tag)
    {
        if (!tag.empty())
        {
            availableTags_.insert(tag);
        }
    }

    /// <summary>
    /// タグを削除
    /// </summary>
    /// <param name="tag">削除するタグ名</param>
    void RemoveTag(const std::string &tag)
    {
        availableTags_.erase(tag);
    }

    /// <summary>
    /// 全タグを取得
    /// </summary>
    /// <returns>const std::unordered_set&lt;std::string&gt;&: 登録済みタグの集合</returns>
    const std::unordered_set<std::string> &GetAllTags() const
    {
        return availableTags_;
    }

    /// <summary>
    /// タグが存在するか確認
    /// </summary>
    /// <param name="tag">確認するタグ名</param>
    /// <returns>bool: 存在すれば true</returns>
    bool HasTag(const std::string &tag) const
    {
        return availableTags_.find(tag) != availableTags_.end();
    }

    /// <summary>
    /// エンジン組み込みタグを登録する
    /// ゲーム固有のタグ（Player / Enemy など）はここに書かず、
    /// ゲーム側の初期化から RegisterGameTags() で登録すること
    /// </summary>
    void InitializeDefaultTags()
    {
        for (const char *tag : kBuiltInTags)
        {
            AddTag(tag);
        }
    }

    /// <summary>
    /// ゲーム固有のタグをまとめて登録する
    /// エンジンはゲームのタグ名を知らないので、ゲーム側の初期化で必ず呼ぶこと
    /// （登録しないとコライダー設定UIの候補に出てこない）
    /// </summary>
    /// <param name="tags">登録するタグ名の一覧</param>
    void RegisterGameTags(const std::vector<std::string> &tags)
    {
        for (const std::string &tag : tags)
        {
            AddTag(tag);
        }
    }

    /// <summary>
    /// 新規に作ったコライダーへ既定で入れる衝突マスクを設定する
    /// 「置いた物が何と当たるべきか」はゲーム次第なので、エンジンは名前を決め打ちしない
    /// （エディタでのコライダー追加・レベルデータ読み込みの初期値になる）
    /// </summary>
    /// <param name="masks">既定の衝突マスクに使うタグ名の一覧</param>
    void SetDefaultCollisionMasks(const std::vector<std::string> &masks)
    {
        defaultCollisionMasks_ = masks;
        // マスクに使う以上、タグとしても選べるようにしておく
        RegisterGameTags(masks);
    }

    /// <summary>
    /// 既定の衝突マスクを取得する（未設定なら空。その場合マスクは付けない）
    /// </summary>
    /// <returns>const std::vector&lt;std::string&gt;&: 既定の衝突マスク</returns>
    const std::vector<std::string> &GetDefaultCollisionMasks() const
    {
        return defaultCollisionMasks_;
    }

    /// <summary>
    /// エンジン組み込みタグか（＝UIから削除させないタグか）
    /// </summary>
    /// <param name="tag">判定するタグ名</param>
    /// <returns>bool: 組み込みタグなら true</returns>
    static bool IsBuiltInTag(const std::string &tag)
    {
        for (const char *builtIn : kBuiltInTags)
        {
            if (tag == builtIn)
            {
                return true;
            }
        }
        return false;
    }

#ifdef USE_IMGUI
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
    ColliderTagManager()
    {
        InitializeDefaultTags();
    }

    /// <summary>
    /// デストラクタ
    /// </summary>
    ~ColliderTagManager() = default;
    ColliderTagManager(const ColliderTagManager &) = delete;
    ColliderTagManager &operator=(const ColliderTagManager &) = delete;

    /// ===================================================
    /// private variables
    /// ===================================================

    // エンジンが用意する汎用タグ。ゲーム固有の名前をここに足さないこと。
    static constexpr const char *kBuiltInTags[] = {"None", "Environment"};

    std::unordered_set<std::string> availableTags_;         // 利用可能なタグの集合
    std::vector<std::string> defaultCollisionMasks_;        // 新規コライダーの既定衝突マスク
};
} // namespace Hagine
