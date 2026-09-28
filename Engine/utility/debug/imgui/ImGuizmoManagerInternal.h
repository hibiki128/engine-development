#pragma once
#ifdef USE_IMGUI
#include <edit/undo/UndoRedoManager.h>
#include <object/base/BaseObjectManager.h>
#include <utility/debug/imgui/ImGuiNotification.h>
#include <string>

// ImGuizmoManager を分割した .cpp 群（Edit / Placement など）が共有する小物。
namespace Hagine {
namespace GizmoInternal {

/// <summary>
/// ボタン1発で完了する一括操作を Undo 履歴へ積む。
/// ImGuiUndoTracker は「ウィジェット編集ジェスチャ」を追う仕組みなので、
/// こうした即時実行のコマンドは Copy/Paste と同様に明示的に Push する。
/// </summary>
/// <param name="label">履歴に出す操作名</param>
/// <param name="operation">実際の操作</param>
template <typename Operation>
void RunAsUndoableCommand(const std::string &label, Operation &&operation)
{
    nlohmann::json before = BaseObjectManager::GetInstance()->CaptureUndoState();
    operation();
    nlohmann::json after = BaseObjectManager::GetInstance()->CaptureUndoState();
    if (before == after)
    {
        return; // 何も変わらなかったら履歴を汚さない
    }
    auto [diffBefore, diffAfter] = MakeTopLevelJsonDiff(before, after);
    UndoRedoManager::GetInstance()->Push(std::make_unique<JsonStateCommand>(
        label, std::move(diffBefore), std::move(diffAfter),
        [](const nlohmann::json &s) { BaseObjectManager::GetInstance()->RestoreUndoState(s); }));
    // ドラッグ＆ドロップ中に呼ばれた場合、ジェスチャの終わりに同じ差分が二重に積まれないようにする
    BaseObjectManager::GetInstance()->SkipUndoGesture();
}

/// <summary>
/// 「元に戻す」ボタン付きの通知を出す。直前に Undo 履歴へ積んだ操作の直後に呼ぶこと。
/// ボタンを押した時点で履歴の先頭がその操作のままなら Undo する（間に別の操作が入っていたら何もしない）
/// </summary>
/// <param name="message">通知の文</param>
/// <param name="color">色</param>
inline void PostUndoToast(const std::string &message, const Vector4 &color)
{
    UndoRedoManager *undo = UndoRedoManager::GetInstance();
    const size_t expectedCount = undo->GetUndoCount();
    const std::string expectedLabel = undo->GetUndoLabel();
    ImGuiNotification::PostWithAction(message, color, "元に戻す", [expectedCount, expectedLabel] {
        UndoRedoManager *current = UndoRedoManager::GetInstance();
        if (current->GetUndoCount() == expectedCount && current->GetUndoLabel() == expectedLabel)
        {
            current->Undo();
            ImGuiNotification::Post("元に戻しました: " + expectedLabel, {0.42f, 0.66f, 0.68f, 1.0f});
        }
        else
        {
            ImGuiNotification::Post("このあとに別の操作があったので、ここからは戻せません（操作の履歴から戻せます）",
                                    {0.82f, 0.58f, 0.36f, 1.0f});
        }
    });
}

} // namespace GizmoInternal
} // namespace Hagine
#endif // USE_IMGUI
