#ifdef USE_IMGUI
#include "EditWidgets.h"
#include "DebugUIHelper.h"
#include <array>
#include <format>
#include <string>
#include <unordered_map>
#include <utility/debug/param/GameParamHub.h>

namespace Hagine {
namespace EditUI {

namespace {
// 最初に表示したときの値（ゲームパラメータに無い変数の「既定値」）。変数のアドレスで覚える
std::unordered_map<const void *, std::array<float, 4>> &FirstSeenValues()
{
    static std::unordered_map<const void *, std::array<float, 4>> values;
    return values;
}

void RememberFirstSeen(const void *ptr, const float *v, int count)
{
    auto &values = FirstSeenValues();
    if (values.contains(ptr))
        return;
    std::array<float, 4> snapshot{};
    for (int i = 0; i < count && i < 4; ++i)
        snapshot[i] = v[i];
    values.emplace(ptr, snapshot);
}

/// <summary>右クリックメニューの本体（float でも int でも値は float で受け渡す）</summary>
bool ItemMenu(const void *ptr, float *values, int count, bool isInt)
{
    if (!ImGui::BeginPopupContextItem())
        return false;

    bool changed = false;
    std::string current;
    for (int i = 0; i < count; ++i)
    {
        if (i > 0)
            current += ", ";
        current += isInt ? std::format("{}", static_cast<int>(values[i])) : std::format("{:.4g}", values[i]);
    }
    ImGui::TextDisabled("%s", current.c_str());
    ImGui::Separator();
    if (ImGui::MenuItem("コピー"))
    {
        ImGui::SetClipboardText(current.c_str());
    }
    float pasted[4] = {};
    const int readable = ValueClipboard::Parse(ImGui::GetClipboardText(), pasted, 4);
    if (ImGui::MenuItem("貼り付け", nullptr, false, readable >= count))
    {
        for (int i = 0; i < count; ++i)
            values[i] = pasted[i];
        changed = true;
    }

    // 既定値: ゲームパラメータに登録があればコードの既定値、無ければ最初に表示したときの値
    float defaults[4] = {};
    int defaultCount = 0;
    std::string hubLabel;
    const bool fromHub = GameParamHub::GetInstance()->FindCodeDefault(ptr, defaults, defaultCount, hubLabel) && defaultCount == count;
    if (!fromHub)
    {
        const auto &seen = FirstSeenValues();
        if (auto it = seen.find(ptr); it != seen.end())
        {
            for (int i = 0; i < count; ++i)
                defaults[i] = it->second[i];
            defaultCount = count;
        }
    }
    if (defaultCount == count)
    {
        std::string label = fromHub ? "コードの既定値へ戻す" : "開いたときの値へ戻す";
        std::string preview;
        for (int i = 0; i < count; ++i)
        {
            if (i > 0)
                preview += ", ";
            preview += isInt ? std::format("{}", static_cast<int>(defaults[i])) : std::format("{:.4g}", defaults[i]);
        }
        if (ImGui::MenuItem(label.c_str(), preview.c_str()))
        {
            for (int i = 0; i < count; ++i)
                values[i] = defaults[i];
            changed = true;
        }
    }
    if (fromHub)
    {
        ImGui::Separator();
        ImGui::TextDisabled("ゲームパラメータにもあります: %s", hubLabel.c_str());
    }
    ImGui::EndPopup();
    return changed;
}
} // namespace

bool FloatItemMenu(float *v, int count)
{
    RememberFirstSeen(v, v, count);
    return ItemMenu(v, v, count, false);
}

bool IntItemMenu(int *v)
{
    float value = static_cast<float>(*v);
    RememberFirstSeen(v, &value, 1);
    if (ItemMenu(v, &value, 1, true))
    {
        *v = static_cast<int>(value);
        return true;
    }
    return false;
}

bool DragFloat(const char *label, float *v, float speed, float min, float max, const char *format, ImGuiSliderFlags flags)
{
    RememberFirstSeen(v, v, 1);
    bool changed = ImGui::DragFloat(label, v, speed, min, max, format, flags);
    changed |= FloatItemMenu(v, 1);
    return changed;
}

bool DragFloat2(const char *label, float v[2], float speed, float min, float max, const char *format, ImGuiSliderFlags flags)
{
    RememberFirstSeen(v, v, 2);
    bool changed = ImGui::DragFloat2(label, v, speed, min, max, format, flags);
    changed |= FloatItemMenu(v, 2);
    return changed;
}

bool DragFloat3(const char *label, float v[3], float speed, float min, float max, const char *format, ImGuiSliderFlags flags)
{
    RememberFirstSeen(v, v, 3);
    bool changed = ImGui::DragFloat3(label, v, speed, min, max, format, flags);
    changed |= FloatItemMenu(v, 3);
    return changed;
}

bool DragFloat4(const char *label, float v[4], float speed, float min, float max, const char *format, ImGuiSliderFlags flags)
{
    RememberFirstSeen(v, v, 4);
    bool changed = ImGui::DragFloat4(label, v, speed, min, max, format, flags);
    changed |= FloatItemMenu(v, 4);
    return changed;
}

bool DragInt(const char *label, int *v, float speed, int min, int max, const char *format, ImGuiSliderFlags flags)
{
    const float before = static_cast<float>(*v);
    RememberFirstSeen(v, &before, 1);
    bool changed = ImGui::DragInt(label, v, speed, min, max, format, flags);
    changed |= IntItemMenu(v);
    return changed;
}

bool SliderFloat(const char *label, float *v, float min, float max, const char *format, ImGuiSliderFlags flags)
{
    RememberFirstSeen(v, v, 1);
    bool changed = ImGui::SliderFloat(label, v, min, max, format, flags);
    changed |= FloatItemMenu(v, 1);
    return changed;
}

bool SliderInt(const char *label, int *v, int min, int max, const char *format, ImGuiSliderFlags flags)
{
    const float before = static_cast<float>(*v);
    RememberFirstSeen(v, &before, 1);
    bool changed = ImGui::SliderInt(label, v, min, max, format, flags);
    changed |= IntItemMenu(v);
    return changed;
}

} // namespace EditUI
} // namespace Hagine
#endif // USE_IMGUI
