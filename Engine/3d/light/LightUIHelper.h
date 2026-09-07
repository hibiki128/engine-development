#pragma once
#ifdef USE_IMGUI
#include "LightTypes.h"
#include "imgui.h"
#include <algorithm>
#include <cctype>
#include <string>
#include <type/Vector4.h>
#include <utility/debug/imgui/DebugUIHelper.h>

// ============================================================
//  光源まわりのImGuiで共通して使う小物。
//  平行光源・点光源・スポットライトの各クラスから使うのでヘッダに置く。
// ============================================================
namespace Hagine {
namespace LightUI {

/// <summary>
/// 「ラベル + 全幅ウィジェット」の2列行を描く
/// </summary>
/// <param name="label">左に出す項目名</param>
/// <param name="tip">ラベルにつけるツールチップ（不要なら nullptr）</param>
/// <param name="drawWidget">右列に描くウィジェット</param>
template <typename F>
void LabeledRow(const char *label, const char *tip, F &&drawWidget)
{
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::AlignTextToFramePadding();
    ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
    ImGui::TextUnformatted(label);
    ImGui::PopStyleColor();
    if (tip && tip[0])
        ImGui::SetItemTooltip("%s", tip);
    ImGui::TableNextColumn();
    ImGui::SetNextItemWidth(-1);
    drawWidget();
}

/// <summary>
/// 一覧行の先頭に置く色見本。ColorButton と違いクリックしてもピッカーが開かない
/// </summary>
/// <param name="color">表示する色</param>
inline void ColorSwatch(const Vector4 &color)
{
    const float height = ImGui::GetFrameHeight();
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    ImDrawList *drawList = ImGui::GetWindowDrawList();
    const ImU32 fill = ImGui::ColorConvertFloat4ToU32(ImVec4(color.x, color.y, color.z, 1.0f));
    drawList->AddRectFilled(pos, ImVec2(pos.x + 10.0f, pos.y + height), fill, 2.0f);
    drawList->AddRect(pos, ImVec2(pos.x + 10.0f, pos.y + height),
                      ImGui::ColorConvertFloat4ToU32(ImVec4(0.0f, 0.0f, 0.0f, 0.5f)), 2.0f);
    ImGui::Dummy(ImVec2(10.0f, height));
}

/// <summary>
/// 大文字小文字を無視した部分一致（一覧の絞り込み用）
/// </summary>
/// <param name="haystack">検索される側</param>
/// <param name="needle">検索する文字列（空なら常に true）</param>
/// <returns>bool: 含んでいれば true</returns>
inline bool ContainsIgnoreCase(const std::string &haystack, const char *needle)
{
    if (!needle || !needle[0])
        return true;
    std::string lowerHay = haystack;
    std::string lowerNeedle = needle;
    std::transform(lowerHay.begin(), lowerHay.end(), lowerHay.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    std::transform(lowerNeedle.begin(), lowerNeedle.end(), lowerNeedle.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return lowerHay.find(lowerNeedle) != std::string::npos;
}

/// <summary>
/// 「陰影計算モデル」のコンボ。HalfLambert / BlinnPhong はどのライトでも同じ選び方をする
/// </summary>
/// <param name="halfLambert">ハーフランバートのフラグ</param>
/// <param name="blinnPhong">Blinn-Phong のフラグ</param>
inline void LightingModelCombo(int32_t &halfLambert, int32_t &blinnPhong)
{
    const char *types[] = {"HalfLambert", "BlinnPhong"};
    int sel = blinnPhong ? 1 : 0;
    if (ImGui::Combo("##lightingModel", &sel, types, IM_ARRAYSIZE(types)))
    {
        halfLambert = (sel == 0) ? 1 : 0;
        blinnPhong = (sel == 1) ? 1 : 0;
    }
}

/// <summary>
/// プロパティ下部の「複製 / 削除」ボタン。押されたものを LightEditRequest で返す
/// </summary>
/// <param name="canDuplicate">複製できる状態か（上限に達していれば false）</param>
/// <returns>LightEditRequest: 押されていなければ Kind::None</returns>
inline LightEditRequest DrawDuplicateRemoveButtons(bool canDuplicate)
{
    LightEditRequest request;

    const float buttonWidth = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
    ImGui::BeginDisabled(!canDuplicate);
    if (ImGui::Button("複製", ImVec2(buttonWidth, 0.0f)))
    {
        request.kind = LightEditRequest::Kind::Duplicate;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, DebugTheme::kBgRed);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.80f, 0.46f, 0.46f, 0.40f));
    const bool remove = ImGui::Button("削除", ImVec2(buttonWidth, 0.0f));
    ImGui::PopStyleColor(2);
    if (remove)
    {
        request.kind = LightEditRequest::Kind::Remove;
    }
    return request;
}
} // namespace LightUI
} // namespace Hagine
#endif // USE_IMGUI
