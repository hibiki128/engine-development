#pragma once
#ifdef USE_IMGUI
#include <animation/state/AnimationStateMachine.h>
#include <imgui.h>
#include <imgui_node_editor.h>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace Hagine {
class AnimationStateMachineRunner;

/// <summary>
/// アニメーションのステートマシンのエディタ（ノードグラフ）。
/// ステート＝ノード、遷移＝線。右のインスペクタで中身を編集する。
/// 編集しているデータは AnimationStateMachineLibrary の共有物なので、
/// そのステートマシンで動いているキャラへすぐ効く（動いている様子も光って見える）
/// </summary>
class AnimationStateMachineEditor
{
  public:
    AnimationStateMachineEditor();
    ~AnimationStateMachineEditor();

    /// <summary>
    /// ImGui描画（ウィンドウの中身だけ。Begin/End は呼び出し側）
    /// </summary>
    void OnImGuiRender();

    /// <summary>
    /// ファイルを開く（拡張子なし）
    /// </summary>
    bool Open(const std::string &file);

    /// <summary>
    /// 次に描くときにこのファイルを開いてもらう（インスペクタの「エディタで開く」から）
    /// </summary>
    static void RequestOpen(const std::string &file);

    /// <summary>
    /// 開いてほしいファイルがあるか（窓を表示する側が見る）
    /// </summary>
    static bool HasOpenRequest();

  private:
    // ---- ファイル ----
    void Save();
    void NewAsset();

    // ---- 編集 ----
    int AddState(const ImVec2 &position, AnimStateKind kind);
    void DeleteState(int stateId);
    void AddTransition(int from, int to);
    void HandleCreateAction();
    void HandleDeleteAction();

    // ---- 描画 ----
    void DrawToolbar();
    void DrawCanvas(float width);
    void DrawStateNode(AnimStateData &state, bool isEntry, bool isCurrent, float pulse);
    void DrawAnyStateNode();
    void DrawLinks(float pulse);
    void DrawMenus();
    void DrawInspector();
    void DrawParamsSection();
    void DrawStateSection(AnimStateData &state);
    void DrawTransitionSection(AnimTransitionData &transition);
    bool DrawFileCombo(const char *id, std::string &file);
    void RefreshAnimationFiles();

    // ---- 実行中の様子 ----
    AnimationStateMachineRunner *GetLiveRunner() const;

    // ---- Undo ----
    void CommitUndo();
    void ResetUndoBaseline();
    void RestoreUndoState(const nlohmann::json &state);

    // ---- ID（ノードエディタは 0 を使えないので、種類ごとに範囲を分ける）----
    static int NodeId(int stateId) { return stateId + 1; }
    static int InputPin(int stateId) { return 100000 + stateId * 2; }
    static int OutputPin(int stateId) { return 100000 + stateId * 2 + 1; }
    static int LinkId(int transitionId) { return 500000 + transitionId; }
    static constexpr int kAnyNodeId = 90000;
    static constexpr int kAnyOutputPin = 90001;

    ax::NodeEditor::EditorContext *pContext_ = nullptr;
    std::shared_ptr<AnimationStateMachineAsset> asset_;
    std::string fileName_ = "NewStateMachine";
    std::vector<std::string> fileList_;
    std::vector<std::string> animationFiles_; // models/animation 以下の gltf
    bool modified_ = false;
    bool positionsPending_ = false;
    int navigateCountdown_ = 0;

    int selectedState_ = -1;
    int selectedTransition_ = -1;
    int liveRunnerIndex_ = 0; // 様子を見るキャラ（同じステートマシンを使っているものの中の番号）

    ImVec2 createPos_ = ImVec2(0, 0);
    bool openCreateMenu_ = false;
    int contextStateId_ = -1;
    int contextTransitionId_ = -1;
    int pendingFrom_ = kNoPending; // 出力ピンを空き地へ離したとき、その遷移元（追加したステートへ自動で繋ぐ）
    static constexpr int kNoPending = -1000;
    float inspectorWidth_ = 360.0f;
    int lastDrawFrame_ = -1;

    nlohmann::json undoBaseline_;
    std::string undoLabel_;
    std::shared_ptr<AnimationStateMachineEditor *> alive_;
};

} // namespace Hagine

#endif // USE_IMGUI
