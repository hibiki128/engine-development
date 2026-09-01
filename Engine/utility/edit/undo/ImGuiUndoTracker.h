#pragma once
#ifdef USE_IMGUI
#include "UndoRedoManager.h"
#include <functional>
#include <imgui.h>
#include <string>

namespace Hagine {

/// <summary>
/// ImGuiエディタウィンドウ用のJSON差分Undoトラッカー
///
/// 毎フレーム、ウィジェット描画の前に Begin、描画後に End を呼ぶだけで
/// 「編集ジェスチャ開始前の状態 → 編集確定後の状態」の差分を UndoRedoManager へ積む。
///
/// - 状態は「名前 → 状態」のトップレベルマップ形式のJSONで表す（getState/applyは利用側が渡す）
/// - 差分はトップレベルキー単位でフィルタされるため、変更されたエンティティだけがUndo対象になる
/// - ゲームロジックによる毎フレームの状態変化は、編集ジェスチャ中でなければ
///   ベースラインの更新として吸収され、履歴には積まれない
///
/// 使用例:
///   tracker_.Begin([this]{ return CaptureUndoState(); });
///   ... ImGuiウィジェット描画 ...
///   tracker_.End("スプライト編集",
///                [this]{ return CaptureUndoState(); },
///                [](const nlohmann::json &s){ SpriteManager::GetInstance()->RestoreUndoState(s); });
/// </summary>
class ImGuiUndoTracker
{
  public:
    /// <summary>
    /// ウィジェット描画前に呼ぶ（編集中でなければ現在状態をベースラインとして記録）
    /// </summary>
    /// <param name="getState">現在状態を取得する関数</param>
    void Begin(const std::function<nlohmann::json()> &getState)
    {
        // ウィンドウが閉じられていた等でフレームが飛んでいたら編集状態をリセットする
        // （古いベースラインとの比較による誤登録を防ぐ）
        const int frame = ImGui::GetFrameCount();
        if (frame - lastFrame_ > 1)
        {
            editing_ = false;
        }
        lastFrame_ = frame;

        if (!editing_)
        {
            baseline_ = getState();
        }
    }

    /// <summary>
    /// ウィジェット描画後に呼ぶ（編集ジェスチャが確定したら差分をUndo履歴へ積む）
    /// </summary>
    /// <param name="label">操作名（履歴表示用）</param>
    /// <param name="getState">現在状態を取得する関数</param>
    /// <param name="apply">状態JSONを対象へ適用する関数</param>
    /// <param name="externallyEditing">ImGuiウィジェット以外の編集中フラグ（ギズモドラッグ等）</param>
    void End(const std::string &label,
             const std::function<nlohmann::json()> &getState,
             std::function<void(const nlohmann::json &)> apply,
             bool externallyEditing = false)
    {
        const bool active = ImGui::IsAnyItemActive() || externallyEditing;

        // 編集ジェスチャが終わった瞬間に、ベースラインとの差分を履歴へ積む
        if (editing_ && !active)
        {
            nlohmann::json after = getState();
            if (after != baseline_)
            {
                auto [diffBefore, diffAfter] = MakeTopLevelJsonDiff(baseline_, after);
                UndoRedoManager::GetInstance()->Push(
                    std::make_unique<JsonStateCommand>(label, std::move(diffBefore), std::move(diffAfter), std::move(apply)));
            }
        }
        editing_ = active;
    }

    /// <summary>
    /// 進行中の編集ジェスチャ追跡を破棄する
    /// （同じ変更を明示的に Push した直後に呼び、二重登録を防ぐ）
    /// </summary>
    void SkipCurrentGesture() { editing_ = false; }

  private:
    nlohmann::json baseline_; // 編集ジェスチャ開始前の状態
    bool editing_ = false;    // 前フレーム終了時点で編集ジェスチャ中だったか
    int lastFrame_ = -100;    // 最後に Begin が呼ばれた ImGui フレーム番号
};

} // namespace Hagine
#endif // USE_IMGUI
