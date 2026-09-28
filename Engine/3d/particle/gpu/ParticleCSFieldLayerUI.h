#pragma once
#ifdef USE_IMGUI
// フィールドのレイヤー（どのエミッターに効くか）を選ぶ小さなボタン列。
// フィールド側の「効くレイヤー」とエミッター側の「受けるレイヤー」で同じ見た目にする。
#include <cstdint>
#include <format>
#include <imgui.h>
// DebugUIHelper.h は ImGui:: を使うので imgui.h の後に include する
#include "utility/debug/imgui/DebugUIHelper.h"

namespace Hagine {
namespace FieldLayerUI {
// 画面に出すレイヤーの数（中身は 32bit 持っているが、使うのはこの範囲）
inline constexpr int kShownLayers = 8;
inline constexpr uint32_t kAllLayers = 0xFFFFFFFFu;

/// <summary>
/// レイヤーのボタン列。押すとそのレイヤーを入れ切り、右クリックで「それだけ」、「全部」で全部に戻す
/// </summary>
/// <returns>bool: 変わったら true</returns>
inline bool DrawLayerChips(const char *id, uint32_t &layers)
{
    bool changed = false;
    ImGui::PushID(id);
    const float size = ImGui::GetFrameHeight();
    const ImVec4 onColor = DebugTheme::kButtonPrimary;
    const ImVec4 offColor = DebugTheme::kButtonNeutral;
    for (int i = 0; i < kShownLayers; ++i)
    {
        if (i > 0)
            ImGui::SameLine(0.0f, 2.0f);
        const uint32_t bit = 1u << i;
        const bool on = (layers & bit) != 0;
        ImGui::PushStyleColor(ImGuiCol_Button, on ? onColor : offColor);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, on ? DebugTheme::kButtonPrimaryHover : DebugTheme::kButtonNeutralHover);
        ImGui::PushStyleColor(ImGuiCol_Text, on ? ImVec4(1.0f, 1.0f, 1.0f, 1.0f) : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        const std::string label = std::to_string(i + 1);
        if (ImGui::Button(label.c_str(), ImVec2(size, size)))
        {
            layers ^= bit;
            changed = true;
        }
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
        {
            layers = bit;
            changed = true;
        }
        ImGui::PopStyleColor(3);
        ImGui::SetItemTooltip("レイヤー %d（右クリックでこれだけ）", i + 1);
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("全部"))
    {
        layers = kAllLayers;
        changed = true;
    }
    ImGui::PopID();
    return changed;
}

/// <summary>レイヤーの短い説明（「全部」「1, 3」など）</summary>
inline std::string DescribeLayers(uint32_t layers)
{
    const uint32_t shownMask = (1u << kShownLayers) - 1u;
    if ((layers & shownMask) == shownMask)
        return "全部";
    if ((layers & shownMask) == 0)
        return "なし";
    std::string text;
    for (int i = 0; i < kShownLayers; ++i)
    {
        if (layers & (1u << i))
        {
            text += text.empty() ? std::to_string(i + 1) : ", " + std::to_string(i + 1);
        }
    }
    return text;
}
} // namespace FieldLayerUI
} // namespace Hagine
#endif // USE_IMGUI
