#pragma once
#include "transition/TransitionPreset.h"
#include "transition/TransitionRenderer.h"
#include "type/Vector4.h"
#include <string>
#include <vector>

namespace Hagine {

/// <summary>
/// シーンの切り替えを覆い隠す幕。
///
/// 前半で画面を覆い、覆いきったところで SceneManager がシーンを切り替え、
/// 少し待ってから後半で明ける。どう覆ってどう明けるかは「演出（TransitionPreset）」で決まり、
/// 演出の一覧は「シーン遷移」窓で作って JSON に保存する（jsons/Transitions/Transitions.json）。
///
/// 幕は UI まで合成した最終結果に GPU で重ねる（TransitionRenderer）。
/// 形（円・ワイプ・六角形のマス…）・塗り（色・グラデーション・画像・前の画面）・ふちの光を
/// 最大4枚まで重ね、下の画面をモザイク・渦・ズームなどで崩すこともできる。
///
/// 使う演出の決まり方: NextSceneReservation で名前を渡した物 → 窓の「使い分け」の決まり → 既定。
/// </summary>
class SceneTransition
{
  public:
    SceneTransition() = default;
    ~SceneTransition() = default;
    SceneTransition(SceneTransition &) = delete;
    SceneTransition &operator=(SceneTransition &) = delete;

    /// <summary>
    /// 終了
    /// </summary>
    void Finalize();

    /// <summary>
    /// 初期化（演出の一覧を読み込み、GPU の準備をする）
    /// </summary>
    void Initialize();

    /// <summary>
    /// 更新（時間を進める）
    /// </summary>
    void Update();

    /// <summary>
    /// 幕を描く。UI まで合成した最終結果へ重ねて書き戻す
    /// </summary>
    /// <param name="pTarget">最終結果（GENERIC_READ 状態）</param>
    void Draw(ID3D12Resource *pTarget);

    /// <summary>
    /// 「シーン遷移」窓の中身（演出の一覧・編集・プレビュー・使い分け）
    /// </summary>
    void DrawEditor();

    /// <summary>
    /// SceneManager から呼ぶ合図
    /// </summary>
    void SetFadeInStart(bool start) { fadeInStart_ = start; }
    void SetFadeOutStart(bool start) { fadeOutStart_ = start; }
    void SetFadeInFinish(bool finish) { fadeInFinish_ = finish; }
    void SetUseTransition(bool use) { useTransition_ = use; }

    /// <summary>
    /// 「六角形（4色）」の演出の色を差し替える（ゲーム側の色マスタに合わせるとき用）
    /// </summary>
    /// <param name="colors">使う色（4色まで）。空なら何もしない</param>
    void SetColors(const std::vector<Vector4> &colors);

    /// <summary>
    /// 次の切り替えに使う演出を決める（Reset の後、合図の前に呼ぶ）
    /// </summary>
    /// <param name="fromScene">切り替える前のシーン名（最初のシーンなら空）</param>
    /// <param name="toScene">切り替えた後のシーン名</param>
    /// <param name="presetName">使う演出の名前（空なら決まり・既定から選ぶ）</param>
    void Prepare(const std::string &fromScene, const std::string &toScene, const std::string &presetName);

    /// <summary>
    /// getter
    /// </summary>
    bool IsEnd() { return isEnd_; }
    bool FadeInFinish() { return fadeInFinish_; }
    bool FadeInStart() { return fadeInStart_; }
    bool GetUseTransition() const { return useTransition_; }

    /// <summary>演出の一覧（ゲーム側から演出を足す・書き換えるとき用）</summary>
    TransitionLibrary &GetLibrary() { return library_; }

    /// <summary>
    /// シーンを切り替えずに、今の画面へ演出を掛けて止めて見せる（エディタ・確認用）。
    /// 実際の切り替えが始まると自動で止まる
    /// </summary>
    /// <param name="presetName">演出の名前</param>
    /// <param name="time">見せる時刻（前半の頭が 0。前半 → 待ち → 後半）</param>
    /// <returns>bool: 演出が見つかれば true</returns>
    bool PreviewAt(const std::string &presetName, float time);

    /// <summary>プレビューをやめる</summary>
    void StopPreview()
    {
        previewing_ = false;
        previewPlaying_ = false;
    }

    /// <summary>今の切り替えで使っている演出の名前</summary>
    const std::string &GetActivePresetName() const { return activePreset_.name; }

    /// <summary>
    /// リセット
    /// </summary>
    void Reset();

  private:
    /// <summary>今どの段階か</summary>
    enum class Stage
    {
        Idle,      // 何もしていない
        Covering,  // 前半（覆っている途中）
        Covered,   // 覆いきってシーンの切り替え待ち
        Holding,   // 切り替わった後の待ち
        Revealing, // 後半（明けている途中）
    };

    /// <summary>
    /// 演出と経過時間から、その瞬間の幕の状態を求める
    /// </summary>
    /// <param name="preset">演出</param>
    /// <param name="stage">段階</param>
    /// <param name="phaseTime">その段階に入ってからの時間</param>
    /// <param name="out">幕の状態</param>
    /// <returns>bool: 描く物があれば true</returns>
    static bool BuildFrame(const TransitionPreset &preset, Stage stage, float phaseTime, TransitionFrame &out);

    /// <summary>実際の切り替えの今の段階</summary>
    Stage CurrentStage() const;

    /// <summary>プレビューの時刻から段階と、その段階の中での時間を求める</summary>
    Stage PreviewStage(float time, float &phaseTime) const;

#ifdef USE_IMGUI
    /// <summary>窓の各部分</summary>
    void DrawPresetList();
    void DrawPreviewControls();
    void DrawPresetDetails(TransitionPreset &preset);
    void DrawPhaseEditor(TransitionPhase &phase, bool isReveal, TransitionRevealMode revealMode);
    bool DrawLayerEditor(TransitionLayer &layer, bool isReveal, TransitionRevealMode revealMode);
    void DrawSceneFxEditor(TransitionSceneFx &fx);
    void DrawRuleEditor();
    /// <summary>編集した物を印を付けて覚えておく（保存するまで ● を出す）</summary>
    void MarkDirty() { dirty_ = true; }
#endif // USE_IMGUI

    TransitionLibrary library_;    // 演出の一覧
    TransitionPreset activePreset_; // 今の切り替えで使う演出（切り替え中に一覧を編集しても壊れないよう写しを持つ）
    std::string pendingPreset_;    // 次の切り替えで使うよう指定された名前
    TransitionRenderer renderer_;  // GPU で幕を描く係

    // 実際の切り替えの状態（SceneManager からの合図）
    bool fadeInStart_ = false;
    bool fadeOutStart_ = false;
    bool fadeInFinish_ = false;
    bool fadeOutFinish_ = false;
    bool isEnd_ = false;
    bool useTransition_ = true;
    float coverTime_ = 0.0f;  // 前半の経過時間
    float holdTime_ = 0.0f;   // 待ちの経過時間
    float revealTime_ = 0.0f; // 後半の経過時間
    float elapsed_ = 0.0f;    // 切り替えが始まってからの時間（揺れ・波を動かす）

    // ---- エディタのプレビュー（シーンを切り替えずに、今の画面へ演出を掛けてみる）----
    bool previewing_ = false;   // プレビューを出しているか
    bool previewPlaying_ = false; // 時間を進めているか
    bool previewLoop_ = true;   // 終わったら頭から
    float previewTime_ = 0.0f;  // プレビューの時刻（前半の頭が 0）
    int selectedPreset_ = 0;    // 窓で選んでいる演出
    bool dirty_ = false;        // 保存していない変更があるか
    std::string previewScene_;  // 「このシーンへ切り替えて試す」の行き先
};
} // namespace Hagine
