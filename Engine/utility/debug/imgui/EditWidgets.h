#pragma once
#ifdef USE_IMGUI
#include <imgui.h>

namespace Hagine {

/// <summary>
/// 調整画面の数値欄。ImGui の同名関数と同じ引数で呼べ、右クリックで
/// 「コピー / 貼り付け / 既定値へ戻す」を出す。
/// 既定値は、その変数がゲームパラメータに登録されていればコードの既定値、
/// そうでなければ最初に表示したときの値（＝起動時・読み込み直後の値）
/// </summary>
namespace EditUI {

bool DragFloat(const char *label, float *v, float speed = 1.0f, float min = 0.0f, float max = 0.0f, const char *format = "%.3f",
               ImGuiSliderFlags flags = 0);
bool DragFloat2(const char *label, float v[2], float speed = 1.0f, float min = 0.0f, float max = 0.0f, const char *format = "%.3f",
                ImGuiSliderFlags flags = 0);
bool DragFloat3(const char *label, float v[3], float speed = 1.0f, float min = 0.0f, float max = 0.0f, const char *format = "%.3f",
                ImGuiSliderFlags flags = 0);
bool DragFloat4(const char *label, float v[4], float speed = 1.0f, float min = 0.0f, float max = 0.0f, const char *format = "%.3f",
                ImGuiSliderFlags flags = 0);
bool DragInt(const char *label, int *v, float speed = 1.0f, int min = 0, int max = 0, const char *format = "%d", ImGuiSliderFlags flags = 0);
bool SliderFloat(const char *label, float *v, float min, float max, const char *format = "%.3f", ImGuiSliderFlags flags = 0);
bool SliderInt(const char *label, int *v, int min, int max, const char *format = "%d", ImGuiSliderFlags flags = 0);

/// <summary>
/// 直前の数値欄に右クリックメニューを付ける（ImGui の数値欄を自前で描いたとき用）
/// </summary>
/// <param name="v">対象の値（float の並び）</param>
/// <param name="count">要素数（1〜4）</param>
/// <returns>bool: 貼り付け・既定値へ戻すで値が変わったら true</returns>
bool FloatItemMenu(float *v, int count);

/// <summary>直前の int の数値欄に右クリックメニューを付ける</summary>
bool IntItemMenu(int *v);

} // namespace EditUI
} // namespace Hagine
#endif // USE_IMGUI
