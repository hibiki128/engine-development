#pragma once
#include "Object/Base/BaseObject.h"
#include "unordered_map"
#include <functional>
#include <vector>
namespace Hagine {

/// <summary>
/// シーン上の全BaseObjectを一元管理するシングルトン
/// 生成・削除・更新・描画、親子付け、シーン/オブジェクトの保存・読み込みを行う
/// </summary>
class BaseObjectManager {
  private:
    /// ===================================================
    /// private method
    /// ===================================================

    /// <summary>
    /// コンストラクタ
    /// </summary>
    BaseObjectManager() = default;

    /// <summary>
    /// デストラクタ
    /// </summary>
    ~BaseObjectManager() = default;
    BaseObjectManager(BaseObjectManager &) = delete;
    BaseObjectManager &operator=(BaseObjectManager &) = delete;

  public:
    /// ===================================================
    /// public method
    /// ===================================================

    /// <summary>
    /// インスタンスを取得
    /// </summary>
    /// <returns>BaseObjectManager*: シングルトンインスタンス</returns>
    static BaseObjectManager* GetInstance() {
        static BaseObjectManager instance;
        return &instance;
    }

    /// <summary>
    /// 終了処理
    /// </summary>
    void Finalize();

    /// <summary>
    /// 全オブジェクトを削除
    /// </summary>
    void RemoveAllObjects();

    /// <summary>
    /// 名前を指定してオブジェクトを削除
    /// </summary>
    /// <param name="name">削除するオブジェクト名</param>
    void RemoveObjectByName(const std::string &name);

    /// <summary>
    /// 所有権を渡してオブジェクトを追加（LoadAll/CreateObject 用）
    /// </summary>
    /// <param name="baseObject">追加するオブジェクト</param>
    void AddObject(std::unique_ptr<BaseObject> baseObject);

    /// <summary>
    /// オブジェクト登録時に呼ばれるコールバックの型
    /// </summary>
    using ObjectRegisterCallback = std::function<void(BaseObject *)>;

    /// <summary>
    /// オブジェクト登録オブザーバを追加する。
    /// エンジンが特定機能（MotionEditor 等）を直接知らずに済むよう、
    /// アプリ側がここでフックを差し込む。
    /// </summary>
    /// <param name="cb">オブジェクト登録時に呼ばれるコールバック</param>
    void AddRegisterObserver(ObjectRegisterCallback cb) { registerObservers_.push_back(std::move(cb)); }

    /// <summary>
    /// 非所有でオブジェクトを登録（シーンが unique_ptr を保持したまま登録する）
    /// </summary>
    /// <param name="obj">登録するオブジェクト</param>
    void RegisterExternal(BaseObject* obj);

    /// <summary>
    /// 非所有登録したオブジェクトを登録解除
    /// </summary>
    /// <param name="obj">解除するオブジェクト</param>
    void UnregisterExternal(BaseObject* obj);

    /// <summary>
    /// 全オブジェクトの更新
    /// </summary>
    void Update();

    /// <summary>
    /// 階層エディタを描画
    /// </summary>
    void DrawHierarchyEditor();

    /// <summary>
    /// 全オブジェクトの描画
    /// </summary>
    /// <param name="viewProjection">ビュープロジェクション</param>
    void Draw(const ViewProjection &viewProjection);

    /// <summary>
    /// ImGuiでの管理UIを更新
    /// </summary>
    void UpdateImGui();

    /// <summary>
    /// 全オブジェクトを保存
    /// </summary>
    void SaveAll();

    /// <summary>
    /// 指定シーンの全オブジェクトを読み込み
    /// </summary>
    /// <param name="sceneName">シーン名</param>
    void LoadAll(std::string sceneName);

    /// <summary>
    /// 名前を指定してオブジェクトを取得
    /// </summary>
    /// <param name="name">オブジェクト名</param>
    /// <returns>BaseObject*: 該当オブジェクト（なければ nullptr）</returns>
    BaseObject *GetObjectByName(const std::string &name);

    /// <summary>
    /// シーン保存モーダルを開く
    /// </summary>
    void OpenSceneSaveModal();

    /// <summary>
    /// シーン読み込みモーダルを開く
    /// </summary>
    void OpenSceneLoadModal();

    /// <summary>
    /// オブジェクト生成モーダルを開く
    /// </summary>
    void OpenObjectCreationModal();

    /// <summary>
    /// オブジェクト読み込みモーダルを開く
    /// </summary>
    void OpenObjectLoadModal();

    /// ===================================================
    /// 親子付け関連
    /// ===================================================

    /// <summary>
    /// 親子階層を表示
    /// </summary>
    void ShowParentChildHierarchy();

    /// <summary>
    /// 指定オブジェクトを起点に階層を再帰表示
    /// </summary>
    /// <param name="obj">表示の起点オブジェクト</param>
    /// <param name="depth">階層の深さ</param>
    void ShowObjectHierarchy(BaseObject *obj, int depth);

    /// <summary>
    /// 親子関係を設定
    /// </summary>
    /// <param name="childName">子オブジェクト名</param>
    /// <param name="parentName">親オブジェクト名</param>
    void SetParentChild(const std::string &childName, const std::string &parentName);

    /// <summary>
    /// 親子関係を解除
    /// </summary>
    /// <param name="childName">子オブジェクト名</param>
    void RemoveParentChild(const std::string &childName);

    /// <summary>
    /// 登録済みオブジェクト名の一覧を取得
    /// </summary>
    /// <returns>std::vector&lt;std::string&gt;: オブジェクト名一覧</returns>
    std::vector<std::string> GetObjectNames() const;

    /// <summary>
    /// 全オブジェクトの親子関係を保存
    /// </summary>
    void SaveAllParentChildRelationships();

    /// <summary>
    /// 全オブジェクトの親子関係を読み込み
    /// </summary>
    void LoadAllParentChildRelationships();

    /// <summary>
    /// 名前を指定してオブジェクトを削除
    /// </summary>
    /// <param name="name">削除するオブジェクト名</param>
    void RemoveObject(const std::string &name);

    /// <summary>
    /// 保存対象オブジェクトの管理UIを表示
    /// </summary>
    void ShowSaveTargetManager();

    /// ===================================================
    /// 描画グループ関連
    /// ===================================================

    /// <summary>
    /// 登録済みオブジェクトの統合ビューを取得（描画システムの一覧表示などで使用）
    /// </summary>
    /// <returns>名前 → オブジェクトのマップ（読み取り専用）</returns>
    const std::unordered_map<std::string, BaseObject *> &GetObjects() const { return objects_; }

  private:
    /// ===================================================
    /// private method（各機能の個別描画・内部処理）
    /// ===================================================

    /// <summary>
    /// シーン保存モーダルを描画
    /// </summary>
    void DrawSceneSaveModel();

    /// <summary>
    /// シーン読み込みモーダルを描画
    /// </summary>
    void DrawSceneLoadModel();

    /// <summary>
    /// オブジェクト生成モーダルを描画
    /// </summary>
    void DrawObjectCreationModel();

    /// <summary>
    /// オブジェクト読み込みモーダルを描画
    /// </summary>
    void DrawObjectLoadModel();

    /// <summary>
    /// JsonからオブジェクトをLoadする
    /// </summary>
    /// <param name="startPath">読み込み開始パス</param>
    /// <param name="objectName">オブジェクト名</param>
    void LoadObjectFromJson(const std::string &startPath, const std::string &objectName);

    /// <summary>
    /// 保存対象リストにオブジェクトを追加
    /// </summary>
    /// <param name="objectName">オブジェクト名</param>
    void AddToSaveTargets(const std::string &objectName);

    /// <summary>
    /// 保存対象リストからオブジェクトを除去
    /// </summary>
    /// <param name="objectName">オブジェクト名</param>
    void RemoveFromSaveTargets(const std::string &objectName);

    /// <summary>
    /// 指定オブジェクトの親子関係を復元
    /// </summary>
    /// <param name="object">対象オブジェクト</param>
    void RestoreParentChildRelationshipForObject(BaseObject *object);

    /// <summary>
    /// オブジェクトを生成して追加
    /// </summary>
    /// <param name="objectName">オブジェクト名</param>
    /// <param name="modelPath">モデルパス</param>
    /// <param name="texturePath">テクスチャパス（省略可）</param>
    void CreateObject(std::string objectName, std::string modelPath, std::string texturePath = "");

  private:
    /// ===================================================
    /// private variants
    /// ===================================================

    // LoadAll/CreateObject が所有するオブジェクト
    std::unordered_map<std::string, std::unique_ptr<BaseObject>> ownedObjects_;
    // Draw/Update/GetObjectByName で使う統合ビュー（所有・外部両方）
    std::unordered_map<std::string, BaseObject*> objects_;

    std::string sceneName_ = "TitleScene"; // 現在のシーン名
    std::string objectName_;               // 入力中のオブジェクト名
    std::string modelPath_;                // 入力中のモデルパス
    std::string texturePath_;              // 入力中のテクスチャパス

    // モーダルの状態を管理するフラグ
    bool showSceneSaveModal_ = false;      // シーン保存モーダル表示フラグ
    bool showSceneLoadModal_ = false;      // シーン読み込みモーダル表示フラグ
    bool showObjectCreationModal_ = false; // オブジェクト生成モーダル表示フラグ
    bool showObjectLoadModal_ = false;     // オブジェクト読み込みモーダル表示フラグ
    std::string selectedJsonPath_;         // 選択中のJsonパス

    // オブジェクト登録時に呼ばれるオブザーバ群（アプリ側が差し込む）
    std::vector<ObjectRegisterCallback> registerObservers_;
};
} // namespace Hagine
