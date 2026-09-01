#pragma once
#ifdef USE_IMGUI
#include "nlohmann/json.hpp"
#include <string>

namespace Hagine {

/// <summary>
/// エディタの再生状態（Unity の Play / Pause / Stop に相当）を管理するシングルトン。
///
/// 「再生を押す前の状態を控えておき、停止で戻す」のが本体で、
/// スナップショットには Undo 用に作ってある CaptureUndoState / RestoreUndoState を流用する。
///
/// 停止では次の順で戻す:
///   1. シーンを同じ名前で作り直す（プレイヤー・敵が初期状態に戻る）
///   2. 控えたスナップショットを上から適用する（未保存の配置編集を戻す）
/// 逆順にすると、作り直しで読み込んだ JSON が未保存の編集を上書きする。
///
/// 既定は Playing。つまり何も操作しなければ従来どおりゲームが動き続ける。
/// </summary>
class PlayModeManager
{
  public:
    /// <summary>再生状態</summary>
    enum class State
    {
        Playing, ///< ゲームロジックが動いている（既定）
        Paused,  ///< ゲームロジックだけ止まっている（描画とエディタは動く）
        Editing, ///< 停止中。再生前の状態へ戻した後もこの状態になる
    };

    /// <summary>インスタンスを取得</summary>
    static PlayModeManager *GetInstance()
    {
        static PlayModeManager instance;
        return &instance;
    }

    /// <summary>
    /// ゲームロジックを更新してよいか。
    /// SceneManager や当たり判定などの「ゲーム世界の更新」はこれを見て止める。
    /// ステップ実行のときはこのフレームだけ true になる。
    /// </summary>
    /// <returns>bool: 更新してよいなら true</returns>
    bool ShouldUpdateGame() const { return state_ == State::Playing || stepRequested_; }

    /// <summary>
    /// フレームの最後に呼ぶ。ステップ実行の1フレーム消費を確定させる。
    /// </summary>
    void EndFrame();

    /// <summary>再生を開始する（現在の状態をスナップショットしてから動かす）</summary>
    void Play();

    /// <summary>一時停止する</summary>
    void Pause();

    /// <summary>1フレームだけ進める（一時停止中に使う）</summary>
    void StepOnce();

    /// <summary>停止して、再生前の状態へ戻す</summary>
    void Stop();

    /// <summary>
    /// 現在の状態をスナップショットとして控え直す。
    /// </summary>
    void CaptureBaseline();

    /// <summary>
    /// シーンが切り替わったときに呼ぶ。
    /// 控えてあるスナップショットは前のシーンのものなので必ず捨てる。
    /// これを忘れると、停止したときに前シーンのオブジェクトが今のシーンへ復元されてしまう
    /// （RestoreUndoState は「名前が無ければ作り直す」ため）。
    /// </summary>
    void OnSceneChanged();

    /// <summary>現在の再生状態</summary>
    State GetState() const { return state_; }

    /// <summary>再生中か（一時停止は含まない）</summary>
    bool IsPlaying() const { return state_ == State::Playing; }

    /// <summary>ImGui のツールバー（再生 / 一時停止 / 停止 / コマ送り）を描画する</summary>
    void DrawToolbar();

  private:
    PlayModeManager() = default;
    ~PlayModeManager() = default;
    PlayModeManager(const PlayModeManager &) = delete;
    PlayModeManager &operator=(const PlayModeManager &) = delete;

    /// <summary>控えておいたスナップショットを適用する</summary>
    void RestoreBaseline();

    State state_ = State::Playing; // 既定は再生中（従来の挙動をそのまま保つ）
    bool stepRequested_ = false;   // このフレームだけ更新する

    // 再生前の状態。編集対象（配置オブジェクト・スプライト）を JSON で控える。
    nlohmann::json objectBaseline_;
    nlohmann::json spriteBaseline_;
    bool hasBaseline_ = false;
};
} // namespace Hagine
#endif // USE_IMGUI
