#pragma once
#include "type/Vector3.h"
#include <string>
#include <vector>

namespace Hagine {
class BaseObject;

/// <summary>
/// BaseObject を名前でまとめて扱うグループ
/// </summary>
class ObjectGroup
{
  public:
    /// <summary>
    /// コンストラクタ
    /// </summary>
    /// <param name="name">グループ名</param>
    explicit ObjectGroup(std::string name) : name_(std::move(name)) {}

    /// ===================================================
    /// メンバー
    /// ===================================================

    /// <summary>
    /// オブジェクトを名前で追加
    /// </summary>
    /// <param name="objectName">オブジェクト名</param>
    /// <returns>bool: 追加したら true（既にいれば false）</returns>
    bool Add(const std::string &objectName);

    /// <summary>
    /// オブジェクトを追加
    /// </summary>
    /// <param name="pObject">追加するオブジェクト</param>
    /// <returns>bool: 追加したら true（既にいれば false）</returns>
    bool Add(const BaseObject *pObject);

    /// <summary>
    /// オブジェクトをグループから外す
    /// </summary>
    /// <param name="objectName">オブジェクト名</param>
    /// <returns>bool: 外したら true</returns>
    bool Remove(const std::string &objectName);

    /// <summary>
    /// メンバーをすべて外す
    /// </summary>
    void ClearMembers() { members_.clear(); }

    /// <summary>
    /// 指定の名前がメンバーか
    /// </summary>
    /// <param name="objectName">オブジェクト名</param>
    /// <returns>bool: メンバーなら true</returns>
    bool Contains(const std::string &objectName) const;

    /// <summary>
    /// メンバーの名前を追加した順で取得
    /// </summary>
    /// <returns>const std::vector&lt;std::string&gt;&amp;: メンバー名一覧</returns>
    const std::vector<std::string> &GetMemberNames() const { return members_; }

    /// ===================================================
    /// 取得
    /// ===================================================

    /// <summary>
    /// 今シーンにいるメンバーを取得
    /// </summary>
    /// <returns>std::vector&lt;BaseObject*&gt;: メンバーのオブジェクト</returns>
    std::vector<BaseObject *> GetObjects() const;

    /// <summary>
    /// 指定の派生クラスにキャストできるメンバーだけを取得
    /// </summary>
    /// <returns>std::vector&lt;T*&gt;: キャストできたオブジェクト</returns>
    template <class T>
    std::vector<T *> GetObjects() const
    {
        std::vector<T *> result;
        for (BaseObject *obj : GetObjects())
        {
            if (T *casted = dynamic_cast<T *>(obj))
            {
                result.push_back(casted);
            }
        }
        return result;
    }

    /// <summary>
    /// メンバーを名前で取得
    /// </summary>
    /// <param name="objectName">オブジェクト名</param>
    /// <returns>BaseObject*: メンバーでないか、シーンにいなければ nullptr</returns>
    BaseObject *Find(const std::string &objectName) const;

    /// ===================================================
    /// まとめて操作
    /// ===================================================

    /// <summary>
    /// メンバー全員の表示・非表示を設定
    /// </summary>
    /// <param name="visible">表示するか</param>
    void SetVisible(bool visible);

    /// <summary>
    /// 表示中のメンバーが一人でもいるか
    /// </summary>
    /// <returns>bool: 表示中のメンバーがいれば true</returns>
    bool IsAnyVisible() const;

    /// <summary>
    /// メンバーをまとめて平行移動（親もメンバーの物は親に任せる）
    /// </summary>
    /// <param name="delta">移動量</param>
    void Translate(const Vector3 &delta);

    /// <summary>
    /// メンバーのワールド座標の平均を取得
    /// </summary>
    /// <returns>Vector3: 中心（メンバーがいなければ原点）</returns>
    Vector3 GetCenter() const;

    /// ===================================================
    /// getter / setter
    /// ===================================================

    const std::string &GetName() const { return name_; }
    bool GetShouldSave() const { return shouldSave_; }
    void SetShouldSave(bool shouldSave) { shouldSave_ = shouldSave; }

  private:
    friend class ObjectGroupManager;

    std::string name_;                 // グループ名
    std::vector<std::string> members_; // メンバー名（追加順）
    bool shouldSave_ = true;           // シーンファイルに書くか
};
} // namespace Hagine
