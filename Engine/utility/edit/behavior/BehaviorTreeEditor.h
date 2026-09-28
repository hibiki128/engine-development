#pragma once
#ifdef USE_IMGUI
#include <ai/behavior/BehaviorTreeAsset.h>
#include <functional>
#include <imgui.h>
#include <imgui_node_editor.h>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace Hagine {

/// <summary>
/// ビヘイビアツリーのノードエディタ（汎用）。
/// どんなノードがあるかは BehaviorTreeRegistry の登録内容から決まるので、エディタ自体はゲームを知らない。
///
/// 使い方:
///   editor.SetDebugContext(&myContext);                         // ノードへ渡す対象
///   editor.SetOnTreeBuilt([&](auto root) { owner.SetTree(root); }); // 実行・停止のたびに呼ばれる
///   editor.Open("BehaviorTree", "EnemyBehavior");
///   ...毎フレーム ImGui のウィンドウの中で editor.OnImGuiRender();
/// </summary>
class BehaviorTreeEditor
{
  public:
    BehaviorTreeEditor();
    ~BehaviorTreeEditor();

    /// <summary>
    /// ImGui描画（ウィンドウの中身だけを描く。Begin/End は呼び出し側）
    /// </summary>
    void OnImGuiRender();

    /// <summary>
    /// 組み立てたツリーへ渡す対象（ビルド＆実行・単体テストで使う）
    /// </summary>
    void SetDebugContext(BTContext *pContext) { pDebugContext_ = pContext; }

    /// <summary>
    /// 「ビルド＆実行」で組み立てたときに根を、「停止」したときに nullptr を渡して呼ばれる
    /// </summary>
    void SetOnTreeBuilt(std::function<void(std::shared_ptr<BTNode>)> callback) { onTreeBuilt_ = std::move(callback); }

    /// <summary>
    /// 実行中のツリーの根（実行していなければ nullptr）
    /// </summary>
    std::shared_ptr<BTNode> GetRuntimeRoot() const { return runtimeRoot_; }

    /// <summary>
    /// ファイルを開く（folder はデータ置き場からの相対フォルダ、file は拡張子なし）
    /// </summary>
    bool Open(const std::string &folder, const std::string &file);

    /// <summary>
    /// ツリー全体を実行中か
    /// </summary>
    bool IsRunning() const { return isRunning_; }

    /// <summary>
    /// 今の内容で組み立てて実行する（「ビルド＆実行」と同じ）
    /// </summary>
    void Run() { BuildAndRun(); }

    /// <summary>
    /// 窓のタブに出す名前（1つのシーンに複数のエディタがあるとき）
    /// </summary>
    void SetDisplayName(std::string name) { displayName_ = std::move(name); }
    const std::string &GetDisplayName() const { return displayName_; }

    /// <summary>
    /// ツールバーの下に出す1行（例: 敵の今の作戦）。空文字なら出さない
    /// </summary>
    void SetInfoProvider(std::function<std::string()> provider) { infoProvider_ = std::move(provider); }

    /// <summary>
    /// 今あるエディタの一覧（メインメニューの「ビヘイビアツリーエディタ」窓はここから描く）
    /// </summary>
    static const std::vector<BehaviorTreeEditor *> &GetInstances();

  private:
    /// <summary>ピンの役割</summary>
    enum class PinRole
    {
        None,
        Input,
        Output,
        Success,
        Failure,
        Weighted,
    };

    // ---- ファイル ----
    void Save();
    void NewTree();
    void RefreshFileList();

    // ---- 実行 ----
    void BuildAndRun();
    void StopRun();
    void StartSingleTest(int nodeId);
    void StopSingleTest();
    void UpdateSingleTest();
    void UpdateStatusTimers(float dt);
    NodeStatus GetDisplayStatus(int nodeId) const;
    float GetStatusTimer(int nodeId) const;

    // ---- 編集 ----
    int CreateNode(int typeId, const ImVec2 &position);
    void DuplicateNodes(const std::vector<int> &nodeIds);
    void DeleteNode(int nodeId);
    void RemoveWeightedOutput(BTNodeData &node, int index);
    PinRole GetPinRole(int pinId, int *outNodeId = nullptr) const;
    /// <summary>繋げるか調べ、繋げるなら出力→入力の順に直す（だめなら理由を返す）</summary>
    const char *ValidateLink(int &startPin, int &endPin) const;
    /// <summary>新しいノードをつまんでいたピンへ自動で繋ぐ</summary>
    void ConnectToPendingPin(int newNodeId);
    int FirstOutputPin(const BTNodeData &node) const;
    void HandleCreateAction();
    void HandleDeleteAction();
    std::vector<int> GetSelectedNodeIds() const;

    // ---- 描画 ----
    void DrawToolbar();
    void DrawCanvas(float width);
    void DrawNode(BTNodeData &node, bool isRoot, bool isOrphan, float pulse);
    void DrawLinks(float pulse);
    void DrawCreateMenu();
    void DrawContextMenus();
    void DrawInspector();
    void DrawNodeParameters(BTNodeData &node, const BTNodeTypeDesc &desc);
    std::string MakeSummary(const BTNodeData &node, const BTNodeTypeDesc &desc) const;

    // ---- Undo（編集が落ち着いたフレームで差分を積む）----
    nlohmann::json CaptureUndoState() const;
    void RestoreUndoState(const nlohmann::json &state);
    void CommitUndo();
    void ResetUndoBaseline();

    ax::NodeEditor::EditorContext *pContext_ = nullptr; // ノードエディタの状態
    BehaviorTreeAsset asset_;                           // 編集中のツリー

    // ファイル
    std::string folder_ = "BehaviorTree";   // 保存先フォルダ
    std::string fileName_ = "NewBehavior";  // 保存するファイル名
    std::vector<std::string> fileList_;     // フォルダ内のファイル
    bool fileListDirty_ = true;             // 一覧を読み直す
    bool modified_ = false;                 // 保存してから変えたか

    int nextNodeId_ = 1;                 // 次のノードID
    int nextLinkId_ = 1;                 // 次のリンクID
    int nextPinId_ = BTPin::kExtraStart; // 次の追加ピンID
    bool positionsPending_ = false;      // asset_ の位置をノードエディタへ送る
    int navigateCountdown_ = 0;          // 0 になったフレームで全体が入るように寄せる（大きさが決まるのを待つ）
    ImVec2 lastCanvasSize_ = ImVec2(0, 0); // キャンバスの大きさ（変わると寄せ直しを待つ）

    // ノードの追加メニュー
    ImVec2 createPos_ = ImVec2(0, 0); // 追加する位置（キャンバス座標）
    bool openCreateMenu_ = false;     // 次の描画でメニューを開く
    int pendingLinkPin_ = 0;          // ピンから空き地へ引っ張ったとき、そのピン
    std::string createSearch_;        // 追加メニューの検索
    int contextNodeId_ = -1;          // 右クリックしたノード
    int contextLinkId_ = -1;          // 右クリックしたリンク

    // 実行
    BTContext *pDebugContext_ = nullptr;
    std::function<void(std::shared_ptr<BTNode>)> onTreeBuilt_;
    bool isRunning_ = false;
    std::shared_ptr<BTNode> runtimeRoot_ = nullptr;
    std::map<int, std::shared_ptr<BTNode>> nodeInstanceMap_; // ノードID → 実際に動くノード
    std::map<int, float> statusTimers_;                      // 成功(+)/失敗(-) の光の残り時間

    // 単体テスト
    int singleTestNodeId_ = -1;
    std::shared_ptr<BTNode> singleTestNode_ = nullptr;
    bool isSingleTesting_ = false;
    NodeStatus singleTestResult_ = NodeStatus::Idle;

    float inspectorWidth_ = 340.0f; // 右のインスペクタの幅
    int lastDrawFrame_ = -1;        // 同じフレームに2回描かないための印
    std::string displayName_ = "ビヘイビアツリー"; // 窓のタブの名前
    std::function<std::string()> infoProvider_;    // ツールバーの下の1行

    // Undo
    nlohmann::json undoBaseline_;                  // 最後に積んだ（または読み込んだ）状態
    std::string undoLabel_;                        // 次に積む操作名
    std::shared_ptr<BehaviorTreeEditor *> alive_;  // Undo の中から自分がまだ居るか確かめる印
};

} // namespace Hagine

#endif // USE_IMGUI
