#pragma once
#include "object/base/BaseObject.h"
#include "unordered_map"
#ifdef USE_IMGUI
#include <edit/undo/ImGuiUndoTracker.h>
#endif // USE_IMGUI
namespace Hagine {

/// <summary>
/// シーン上の全BaseObjectを一元管理するシングルトン
/// 生成・削除・更新・描画、親子付け、シーン/オブジェクトの保存・読み込みを行う
/// </summary>
class BaseObjectManager
{
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
    static BaseObjectManager *GetInstance()
    {
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
    /// 非所有でオブジェクトを登録（シーンが unique_ptr を保持したまま登録する）
    /// </summary>
    /// <param name="pObject">登録するオブジェクト</param>
    void RegisterExternal(BaseObject *pObject);

    /// <summary>
    /// 非所有登録したオブジェクトを登録解除
    /// </summary>
    /// <param name="pObject">解除するオブジェクト</param>
    void UnregisterExternal(BaseObject *pObject);

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
    /// モデルパスからオブジェクトを生成して追加する
    /// 名前はモデルのファイル名から自動で付け、重複したら連番を振る
    /// </summary>
    /// <param name="modelPath">モデルの相対パス（models ルート基準）</param>
    /// <param name="position">配置するローカル座標</param>
    /// <returns>BaseObject*: 生成されたオブジェクト（失敗時は nullptr）</returns>
    BaseObject *CreateObjectFromModel(const std::string &modelPath, const Vector3 &position);

    /// <summary>
    /// プリミティブ形状のオブジェクトを生成して追加する
    /// 名前は自動で一意化され、配置は現在のカメラ前方になる
    /// </summary>
    /// <param name="type">プリミティブの種類</param>
    /// <param name="baseName">名前の元（例: "cube"）</param>
    /// <returns>BaseObject*: 生成されたオブジェクト</returns>
    BaseObject *CreatePrimitiveObject(PrimitiveType type, const std::string &baseName);

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
    /// <param name="pObject">表示の起点オブジェクト</param>
    /// <param name="depth">階層の深さ</param>
    void ShowObjectHierarchy(BaseObject *pObject, int depth);

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
    /// 登録済みオブジェクト名を名前順に並べて取得する
    /// 内部が unordered_map なので、一覧UIの並びを安定させたい場合はこちらを使う
    /// </summary>
    /// <returns>std::vector&lt;std::string&gt;: 名前順のオブジェクト名一覧</returns>
    std::vector<std::string> GetSortedObjectNames() const;

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

#ifdef USE_IMGUI
    /// <summary>
    /// Undo用: 所有オブジェクトの編集可能状態をJSON化する（トップレベル = 名前 → 状態）
    /// 対象は所有オブジェクトのみ（シーン所有のゲームエンティティはゲームロジックが
    /// 毎フレーム書き換えるため追跡しない）
    /// </summary>
    /// <returns>nlohmann::json: 状態JSON</returns>
    nlohmann::json CaptureUndoState();

    /// <summary>
    /// Undo用: CaptureUndoState で得た状態（差分可）を適用する
    /// null のキーはオブジェクト削除、存在しない名前は再生成として扱う
    /// </summary>
    /// <param name="state">適用する状態JSON</param>
    void RestoreUndoState(const nlohmann::json &state);
#endif // USE_IMGUI

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
    /// <param name="pObject">対象オブジェクト</param>
    void RestoreParentChildRelationshipForObject(BaseObject *pObject);

    /// <summary>
    /// 登録済みの名前と衝突しないオブジェクト名を作る（衝突時は _1, _2 … と連番を振る）
    /// </summary>
    /// <param name="baseName">希望する名前</param>
    /// <returns>std::string: 一意なオブジェクト名</returns>
    std::string MakeUniqueObjectName(const std::string &baseName) const;

    /// <summary>
    /// 破棄・登録解除の直前に、他マネージャが持つこのオブジェクトへの参照を落とす
    /// （ギズモの操作対象・モーションエディタの登録）
    /// </summary>
    /// <param name="pObject">対象オブジェクト</param>
    /// <param name="name">登録名</param>
    void DetachRegistrations(BaseObject *pObject, const std::string &name);

    /// <summary>
    /// オブジェクトを生成して追加
    /// </summary>
    /// <param name="objectName">オブジェクト名</param>
    /// <param name="modelPath">モデルパス</param>
    /// <param name="texturePath">テクスチャパス（省略可）</param>
    void CreateObject(std::string objectName, std::string modelPath, std::string texturePath = "");

  private:
    /// ===================================================
    /// private variables
    /// ===================================================

    // LoadAll/CreateObject が所有するオブジェクト
    std::unordered_map<std::string, std::unique_ptr<BaseObject>> ownedObjects_;
    // Draw/Update/GetObjectByName で使う統合ビュー（所有・外部両方）
    std::unordered_map<std::string, BaseObject *> objects_;

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
#ifdef USE_IMGUI
    ImGuiUndoTracker undoTracker_; // オブジェクト編集のUndoトラッカー
#endif                             // _DEBUG
};
} // namespace Hagine
