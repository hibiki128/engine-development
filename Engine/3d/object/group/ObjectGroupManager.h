#pragma once
#include "ObjectGroup.h"
#include <functional>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace Hagine {

/// <summary>
/// オブジェクトグループを名前で管理し、シーンファイルとグループファイルへ保存するシングルトン
/// </summary>
class ObjectGroupManager
{
  private:
    ObjectGroupManager() = default;
    ~ObjectGroupManager() = default;
    ObjectGroupManager(const ObjectGroupManager &) = delete;
    ObjectGroupManager &operator=(const ObjectGroupManager &) = delete;

  public:
    /// <summary>
    /// インスタンスを取得
    /// </summary>
    /// <returns>ObjectGroupManager*: シングルトンインスタンス</returns>
    static ObjectGroupManager *GetInstance();

    /// ===================================================
    /// グループの生成・取得
    /// ===================================================

    /// <summary>
    /// グループを作成（同じ名前があればそれを返す）
    /// </summary>
    /// <param name="name">グループ名</param>
    /// <returns>ObjectGroup*: 作成したグループ（名前が空なら nullptr）</returns>
    ObjectGroup *Create(const std::string &name);

    /// <summary>
    /// グループを名前で取得
    /// </summary>
    /// <param name="name">グループ名</param>
    /// <returns>ObjectGroup*: 見つからなければ nullptr</returns>
    ObjectGroup *Find(const std::string &name);

    /// <summary>
    /// グループを削除（メンバーのオブジェクトは残す）
    /// </summary>
    /// <param name="name">グループ名</param>
    /// <returns>bool: 削除したら true</returns>
    bool Remove(const std::string &name);

    /// <summary>
    /// グループと、メンバーのうちエディタで置いた物を子ごと削除
    /// </summary>
    /// <param name="name">グループ名</param>
    /// <returns>int: 削除したオブジェクトの数</returns>
    int Destroy(const std::string &name);

    /// <summary>
    /// グループ名を変更（使われている名前なら連番を付ける）
    /// </summary>
    /// <param name="oldName">今の名前</param>
    /// <param name="desiredName">付けたい名前</param>
    /// <returns>std::string: 実際に付いた名前（変更できなければ空）</returns>
    std::string Rename(const std::string &oldName, const std::string &desiredName);

    /// <summary>
    /// 全グループを削除（シーン遷移時に呼ばれる）
    /// </summary>
    void Clear() { groups_.clear(); }

    /// <summary>
    /// グループ名を名前順で取得
    /// </summary>
    /// <returns>std::vector&lt;std::string&gt;: グループ名一覧</returns>
    std::vector<std::string> GetGroupNames() const;

    /// <summary>
    /// 指定オブジェクトが入っているグループを取得
    /// </summary>
    /// <param name="objectName">オブジェクト名</param>
    /// <returns>std::vector&lt;ObjectGroup*&gt;: 入っているグループ</returns>
    std::vector<ObjectGroup *> FindGroupsOf(const std::string &objectName);

    /// <summary>
    /// 使われていないグループ名を作る（使われていれば _0, _1 … を付ける）
    /// </summary>
    /// <param name="baseName">付けたい名前</param>
    /// <returns>std::string: 一意なグループ名</returns>
    std::string MakeUniqueName(const std::string &baseName) const;

    /// <summary>
    /// オブジェクト名の変更をメンバー名へ反映（BaseObjectManager::RenameObject から呼び出し）
    /// </summary>
    /// <param name="oldName">変更前の名前</param>
    /// <param name="newName">変更後の名前</param>
    void OnObjectRenamed(const std::string &oldName, const std::string &newName);

    /// ===================================================
    /// シーンファイル・Undo
    /// ===================================================

    /// <summary>
    /// シーンに保存するグループを JSON 配列にまとめる
    /// </summary>
    /// <returns>nlohmann::json: グループ名とメンバー名の配列</returns>
    nlohmann::json ToJson() const;

    /// <summary>
    /// 全グループの状態を保存の有無も含めて JSON にまとめる（Undo・再生の巻き戻し用）
    /// </summary>
    /// <returns>nlohmann::json: グループの状態</returns>
    nlohmann::json CaptureState() const;

    /// <summary>
    /// ToJson・CaptureState の結果でグループを置き換え、同じ名前のグループはポインタを保ったまま中身を書き換える
    /// </summary>
    /// <param name="state">ToJson・CaptureState の結果</param>
    void FromJson(const nlohmann::json &state);

    /// ===================================================
    /// グループファイル（jsons/ObjectGroups/名前.json）
    /// ===================================================

    /// <summary>
    /// グループをメンバーの中身ごとグループファイルへ書き出す
    /// </summary>
    /// <param name="groupName">グループ名</param>
    /// <param name="fileName">ファイル名（空ならグループ名）</param>
    /// <returns>bool: 書き出せたら true</returns>
    bool SaveToFile(const std::string &groupName, const std::string &fileName = "");

    /// <summary>
    /// グループファイルからオブジェクトを作り、新しいグループとしてシーンに置く
    /// </summary>
    /// <param name="fileName">ファイル名（拡張子なし）</param>
    /// <param name="offset">置くときに加える位置のずらし量</param>
    /// <param name="saveInScene">置いた物とグループをシーンファイルに書くか</param>
    /// <returns>ObjectGroup*: 置いたグループ（読めなければ nullptr）</returns>
    ObjectGroup *Spawn(const std::string &fileName, const Vector3 &offset = {0.0f, 0.0f, 0.0f}, bool saveInScene = false);

    /// <summary>
    /// 保存済みのグループファイル名を名前順で取得
    /// </summary>
    /// <returns>std::vector&lt;std::string&gt;: ファイル名一覧（拡張子なし）</returns>
    std::vector<std::string> ListGroupFiles() const;

    /// <summary>
    /// グループファイルを削除
    /// </summary>
    /// <param name="fileName">ファイル名（拡張子なし）</param>
    /// <returns>bool: 削除したら true</returns>
    bool DeleteGroupFile(const std::string &fileName);

    /// <summary>
    /// グループファイルのパスを取得
    /// </summary>
    /// <param name="fileName">ファイル名（拡張子なし）</param>
    /// <returns>std::string: jsons/ObjectGroups/名前.json</returns>
    static std::string GroupFilePath(const std::string &fileName);

    /// ===================================================
    /// ImGui（ObjectGroupManagerImGui.cpp）
    /// ===================================================

    /// <summary>
    /// グループ窓の中身を描画（窓の Begin/End は呼び出し元）
    /// </summary>
    void DrawImGui();

    /// <summary>
    /// 階層の右クリックメニューに出すグループの項目を描画（BeginMenu の内側で呼ぶ）
    /// </summary>
    /// <param name="objectNames">対象のオブジェクト名</param>
    void DrawMembershipMenu(const std::vector<std::string> &objectNames);

  private:
    /// ===================================================
    /// private method
    /// ===================================================

#ifdef USE_IMGUI
    /// <summary>
    /// 今のグループ状態を控えて変更し、Undo 履歴へ積む
    /// </summary>
    /// <param name="label">履歴に出す操作名</param>
    /// <param name="edit">変更処理</param>
    void EditWithUndo(const char *label, const std::function<void()> &edit);

    /// <summary>
    /// オブジェクトの生成・削除を伴う変更を、オブジェクトとグループの両方まとめて Undo 履歴へ積む
    /// </summary>
    /// <param name="label">履歴に出す操作名</param>
    /// <param name="edit">変更処理</param>
    void EditObjectsWithUndo(const char *label, const std::function<void()> &edit);

    /// <summary>
    /// グループ一覧を描画
    /// </summary>
    void DrawGroupList();

    /// <summary>
    /// 選択中グループの中身を描画
    /// </summary>
    void DrawGroupDetail();

    /// <summary>
    /// グループファイルの一覧を描画
    /// </summary>
    void DrawFileList();
#endif // USE_IMGUI

  private:
    /// ===================================================
    /// private variables
    /// ===================================================

    std::map<std::string, std::unique_ptr<ObjectGroup>> groups_; // グループ名 → グループ（名前順）

#ifdef USE_IMGUI
    std::string selectedName_;                 // 窓で選択中のグループ名
    std::string newGroupName_;                 // 作成欄の入力
    std::string renameBuffer_;                 // 名前の変更欄の入力
    std::string renameSource_;                 // 名前の変更欄が対象にしているグループ
    std::string menuNewGroupName_;             // 階層メニューの作成欄の入力
    std::vector<std::string> fileList_;        // グループファイル名の一覧
    bool fileListDirty_ = true;                // 一覧を読み直すか
#endif // USE_IMGUI
};
} // namespace Hagine
