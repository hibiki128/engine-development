#pragma once
#include "object/base/BaseObject.h"
#include "unordered_map"
#ifdef USE_IMGUI
#endif // USE_IMGUI
namespace Hagine {

/// <summary>
/// シーン上の全BaseObjectを一元管理するシングルトン
/// 生成・削除・更新・描画、親子付け、シーンファイル用の書き出し・作り直しを行う
/// （ファイルの読み書きとダイアログは SceneSerializer）
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

    /// ===================================================
    /// シーンファイル（保存・読み込みの入口は SceneSerializer）
    /// ===================================================

    /// <summary>
    /// このマネージャが所有しているオブジェクトか（エディタで置いた物・シーンファイルから作った物）。
    /// RegisterExternal で登録された、ゲーム側がコードで作るオブジェクトは false
    /// </summary>
    bool IsOwned(const BaseObject *pObject) const;

    /// <summary>
    /// シーンファイルへ保存される物か。
    /// 所有オブジェクトで「シーンに保存する」が付いていて、所有している祖先もすべて保存対象のときだけ true。
    /// （親を保存しないなら子も保存しない。ゲーム側のオブジェクトの子に付けた物は、親の名前ごと保存する）
    /// </summary>
    bool IsSceneSaveTarget(BaseObject *pObject) const;

    /// <summary>
    /// 保存対象のオブジェクトを、親が子より先に来る順で JSON 配列にする。
    /// 兄弟は名前順に並べるので、保存のたびに並びが変わらない
    /// </summary>
    /// <returns>nlohmann::json: BaseObject::Serialize の結果を並べた配列</returns>
    nlohmann::json SerializeSceneObjects();

    /// <summary>
    /// 所有オブジェクトを全部消してから、SerializeSceneObjects の配列どおりに作り直す。
    /// ゲーム側のオブジェクトには触れない。親がまだ居ない（ゲーム側が後で登録する）物は
    /// ResolvePendingParents まで親子付けを待つ
    /// </summary>
    /// <param name="objects">SerializeSceneObjects の結果</param>
    /// <returns>int: 作れたオブジェクトの数</returns>
    int DeserializeSceneObjects(const nlohmann::json &objects);

    /// <summary>
    /// 読み込み時に親が見つからなかった親子付けを、今いるオブジェクトでもう一度試す。
    /// シーンの Initialize でゲーム側のオブジェクトが登録された後に呼ぶ
    /// </summary>
    void ResolvePendingParents();

    /// <summary>
    /// 名前を指定してオブジェクトを取得
    /// </summary>
    /// <param name="name">オブジェクト名</param>
    /// <returns>BaseObject*: 該当オブジェクト（なければ nullptr）</returns>
    BaseObject *GetObjectByName(const std::string &name);

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
    /// メタボールオブジェクトを生成して追加する
    /// </summary>
    /// <param name="baseName">名前のもと（重複したら連番が付く）</param>
    /// <returns>BaseObject*: 生成されたオブジェクト</returns>
    BaseObject *CreateMetaBallObject(const std::string &baseName);

    /// <summary>
    /// 既存オブジェクトを複製してマネージャへ登録する。
    /// モデル・プリミティブ・メタボールのいずれも、元と同じ種類で作り直したうえで
    /// トランスフォームやマテリアル、派生クラス固有のデータまで写す。
    ///
    /// コピー＆ペースト（Ctrl+C / Ctrl+V）・複製（Ctrl+D）・インスペクタの複製ボタンは
    /// すべてここを通る。オブジェクトの種類を増やしたらこの関数だけ直せばよい。
    /// </summary>
    /// <param name="pSource">複製元</param>
    /// <param name="offset">複製先のローカル座標へ加えるずらし量</param>
    /// <param name="desiredName">付けたい名前（空なら複製元の名前を元に連番を振る）</param>
    /// <returns>BaseObject*: 複製されたオブジェクト（失敗時は nullptr）</returns>
    BaseObject *CloneObject(BaseObject *pSource, const Vector3 &offset = {0.0f, 0.0f, 0.0f},
                            const std::string &desiredName = "");

    /// <summary>
    /// 名前で引いた既存オブジェクトを複製する。
    /// 少しずらして置くので複製直後に掴める。
    /// </summary>
    /// <param name="sourceName">複製元のオブジェクト名</param>
    /// <returns>BaseObject*: 複製されたオブジェクト（失敗時は nullptr）</returns>
    BaseObject *DuplicateObject(const std::string &sourceName);

    /// <summary>
    /// 複製を次の Update まで遅らせて予約する。
    /// インスペクタ描画中に objects_ を書き換えるとイテレータが壊れるので、
    /// UI から複製したいときは必ずこちらを使う。
    /// </summary>
    void RequestDuplicate(const std::string &sourceName);

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
    /// このオブジェクトに付いている光源・パーティクルを階層に並べる
    /// </summary>
    /// <param name="parentName">親の名前</param>
    /// <param name="depth">インデントの深さ</param>
    void ShowAttachChildrenOf(const std::string &parentName, int depth);

    /// <summary>
    /// どのオブジェクトにも付いていない光源・パーティクルをルートに並べる
    /// </summary>
    void ShowRootAttachNodes();

    /// <summary>
    /// 種類をまたいだ親子付けのドラッグ＆ドロップ結果を適用する（ツリー描画後に呼ぶ）
    /// </summary>
    void ApplyPendingAttachRequest();

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
    /// 名前を指定してオブジェクトを削除
    /// </summary>
    /// <param name="name">削除するオブジェクト名</param>
    void RemoveObject(const std::string &name);

    /// <summary>
    /// モデルのファイルが書き換わったとき、そのモデルを使っている全オブジェクトを読み直す（ホットリロード）。
    /// GPU を待ってから差し替えるので、少し止まる
    /// </summary>
    /// <param name="modelPath">models ルートからの相対パス</param>
    /// <returns>int: 読み直したオブジェクトの数</returns>
    int ReloadModelFile(const std::string &modelPath);

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

    /// <summary>
    /// 進行中の編集ジェスチャの追跡を捨てる。同じ変更を明示的に Undo 履歴へ積んだ直後に呼び、
    /// ドラッグ＆ドロップ等のジェスチャ終わりに同じ差分が二重に積まれるのを防ぐ
    /// </summary>
    void SkipUndoGesture();

    /// <summary>
    /// 1体ぶんの編集可能状態をJSON化する（Undo・プレハブ共通の形式）
    /// </summary>
    /// <param name="pObject">対象オブジェクト</param>
    /// <returns>nlohmann::json: 状態JSON（pObject が null なら空）</returns>
    nlohmann::json CaptureObjectState(BaseObject *pObject) const;
#endif // USE_IMGUI

    /// <summary>
    /// 状態JSONのモデル・プリミティブ情報からオブジェクトを作り、所有オブジェクトとして登録する。
    /// トランスフォームなどの中身は ApplyObjectState / BaseObject::Deserialize で別に流し込む。
    /// シーンの読み込みは Release でも行うので、これは USE_IMGUI では囲まない
    /// </summary>
    /// <param name="name">登録名（一意であること）</param>
    /// <param name="state">CaptureObjectState / BaseObject::Serialize の結果</param>
    /// <returns>BaseObject*: 作ったオブジェクト（作れなければ nullptr）</returns>
    BaseObject *CreateObjectFromState(const std::string &name, const nlohmann::json &state);

#ifdef USE_IMGUI

    /// <summary>
    /// 状態JSONのトランスフォーム・フラグ・マテリアル・コライダーを既存オブジェクトへ流し込む。
    /// 親子関係（"parent"）は扱わない（相手が揃ってから呼び出し元で付ける）
    /// </summary>
    /// <param name="pObject">対象オブジェクト</param>
    /// <param name="state">CaptureObjectState の結果</param>
    void ApplyObjectState(BaseObject *pObject, const nlohmann::json &state);

    /// ===================================================
    /// プレハブ（BaseObjectPrefab.cpp）
    /// ===================================================

    /// <summary>
    /// オブジェクトを子孫ごとプレハブとして保存する（jsons/Prefab/名前.json）。
    /// 根の位置は原点に直して保存するので、置くときは置き場所がそのまま根の位置になる
    /// </summary>
    /// <param name="rootName">根にするオブジェクト名</param>
    /// <param name="prefabName">プレハブ名（ファイル名。空なら根の名前）</param>
    /// <returns>bool: 保存できたか</returns>
    bool SavePrefab(const std::string &rootName, const std::string &prefabName);

    /// <summary>
    /// プレハブを読み込んでシーンに置く。名前は重複しないよう自動で振り直す
    /// </summary>
    /// <param name="prefabName">プレハブ名（拡張子なし）</param>
    /// <param name="position">根を置くワールド座標</param>
    /// <returns>std::string: 置いた根のオブジェクト名（失敗時は空）</returns>
    std::string InstantiatePrefab(const std::string &prefabName, const Vector3 &position);

    /// <summary>
    /// 置いた物（プレハブの根）の今の内容で、元のプレハブを上書き保存する
    /// </summary>
    /// <param name="instanceName">置いた物の根の名前</param>
    /// <returns>bool: 保存できたか（プレハブから置いた物でなければ false）</returns>
    bool ApplyInstanceToPrefab(const std::string &instanceName);

    /// <summary>
    /// 置いた物を子ごと消して、元のプレハブの内容で同じ場所に置き直す（位置と親は保つ）
    /// </summary>
    /// <param name="instanceName">置いた物の根の名前</param>
    /// <returns>std::string: 置き直した根の名前（失敗時は空）</returns>
    std::string RevertInstanceToPrefab(const std::string &instanceName);

    /// <summary>シーンに置かれている、そのプレハブ由来の物の数</summary>
    int CountPrefabInstances(const std::string &prefabName) const;

    /// <summary>保存済みプレハブの名前一覧（名前順）</summary>
    std::vector<std::string> ListPrefabNames() const;

    /// <summary>プレハブを削除する</summary>
    bool DeletePrefab(const std::string &prefabName);

    /// <summary>プレハブのファイルパス（jsons/Prefab/名前.json）</summary>
    static std::string PrefabFilePath(const std::string &prefabName);

    /// <summary>
    /// 保存しておいたプレハブ1件の中身（オブジェクト数と根のモデル）。一覧の表示用
    /// </summary>
    struct PrefabInfo
    {
        int objectCount = 0;
        std::string rootModel;
    };

    /// <summary>プレハブの中身を軽く読む（一覧のツールチップ用。読めなければ objectCount=0）</summary>
    PrefabInfo PeekPrefab(const std::string &prefabName) const;
#endif // USE_IMGUI

  private:
    /// ===================================================
    /// private method（各機能の個別描画・内部処理）
    /// ===================================================

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

    // シーンファイルの読み込み・エディタでの生成で作った、このマネージャが所有するオブジェクト
    std::unordered_map<std::string, std::unique_ptr<BaseObject>> ownedObjects_;
    // Draw/Update/GetObjectByName で使う統合ビュー（所有・外部両方）
    std::unordered_map<std::string, BaseObject *> objects_;

    // 読み込み時に親が見つからなかった親子付け（子の名前 → 親の名前）。ResolvePendingParents で付ける
    std::unordered_map<std::string, std::string> pendingParents_;

    std::string objectName_;               // 入力中のオブジェクト名
    std::string modelPath_;                // 入力中のモデルパス
    std::string texturePath_;              // 入力中のテクスチャパス

    // モーダルの状態を管理するフラグ
    bool showObjectCreationModal_ = false; // オブジェクト生成モーダル表示フラグ
    // 次の Update で複製するオブジェクト名（UI から予約される）
    std::vector<std::string> pendingDuplicates_{};
    bool showObjectLoadModal_ = false;     // オブジェクト読み込みモーダル表示フラグ
    std::string selectedJsonPath_;         // 選択中のJsonパス
#ifdef USE_IMGUI
#endif                             // _DEBUG
};
} // namespace Hagine
