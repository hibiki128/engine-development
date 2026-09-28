#include "ImGuiManager.h"
#ifdef USE_IMGUI
// エディタの補助ツール窓。
//   ・Undo 履歴: これまでの操作を並べ、行を押すとその時点まで一気に戻す／進める
//   ・配置ツール: 選択中のオブジェクトを直線・格子・円・ばらまきで並べて複製する
//   ・カラーパレット: 色を貯めておき、各所の色の欄へドラッグで渡す
#include "DebugUIHelper.h"
#include "ImGuiNotification.h"
#include "ImGuizmoManager.h"
#include <edit/undo/UndoRedoManager.h>
#include <format>
#include <fstream>
#include <icon/IconsFontAwesome5.h>
#include <imgui.h>

namespace Hagine {

void ImGuiManager::ShowUndoHistoryWindow()
{
    if (!showUndoHistoryView_)
    {
        return;
    }
    ImGui::SetNextWindowSize(ImVec2(340.0f, 420.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(ICON_FA_HISTORY " 操作の履歴###UndoHistory", &showUndoHistoryView_, ImGuiWindowFlags_NoFocusOnAppearing))
    {
        ImGui::End();
        return;
    }

    UndoRedoManager *undo = UndoRedoManager::GetInstance();
    const size_t undoCount = undo->GetUndoCount();
    const size_t redoCount = undo->GetRedoCount();

    // ---- 操作ボタン ----
    const float half = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
    ImGui::BeginDisabled(undoCount == 0);
    if (PrimaryButton(ICON_FA_UNDO " 元に戻す", ImVec2(half, 0.0f)))
    {
        undo->Undo();
    }
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("Ctrl+Z");
    ImGui::SameLine();
    ImGui::BeginDisabled(redoCount == 0);
    if (PrimaryButton(ICON_FA_REDO " やり直し", ImVec2(-1.0f, 0.0f)))
    {
        undo->Redo();
    }
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("Ctrl+Y");

    ImGui::TextDisabled("戻せる %zu 件 / やり直せる %zu 件（最大 %zu 件）", undoCount, redoCount, UndoRedoManager::kMaxHistory);
    ImGui::Separator();

    // ---- 一覧（上が古い。▶ が今いる位置。薄い行は取り消した操作）----
    size_t jumpTarget = SIZE_MAX;
    ImGui::BeginChild("##undoList", ImVec2(0.0f, -ImGui::GetFrameHeightWithSpacing()), ImGuiChildFlags_Borders);
    const ImVec4 accent = ImGui::GetStyleColorVec4(ImGuiCol_CheckMark);

    auto row = [&](size_t targetCount, const char *icon, const std::string &label, const std::string &time, bool current, bool future) {
        ImGui::PushID(static_cast<int>(targetCount));
        if (current)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, accent);
        }
        else if (future)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        }
        const std::string text = std::format("{} {}", current ? ICON_FA_CARET_RIGHT : icon, label);
        if (ImGui::Selectable(text.c_str(), current, ImGuiSelectableFlags_None) && !current)
        {
            jumpTarget = targetCount;
        }
        if (ImGui::IsItemHovered() && !current)
        {
            ImGui::SetTooltip(future ? "ここまでやり直す" : "ここまで戻す");
        }
        if (current || future)
        {
            ImGui::PopStyleColor();
        }
        if (!time.empty())
        {
            ImGui::SameLine(ImGui::GetContentRegionMax().x - ImGui::CalcTextSize(time.c_str()).x);
            ImGui::TextDisabled("%s", time.c_str());
        }
        ImGui::PopID();
    };

    row(0, ICON_FA_FLAG, "最初の状態", "", undoCount == 0, false);
    for (size_t i = 0; i < undoCount; ++i)
    {
        const UndoRedoManager::HistoryItem item = undo->GetUndoItem(i);
        row(i + 1, ICON_FA_CIRCLE, item.label, item.time, i + 1 == undoCount, false);
    }
    for (size_t i = 0; i < redoCount; ++i)
    {
        const UndoRedoManager::HistoryItem item = undo->GetRedoItem(i);
        row(undoCount + i + 1, ICON_FA_CIRCLE, item.label, item.time, false, true);
    }
    // 新しい操作が積まれたら一番下まで追う
    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4.0f)
    {
        ImGui::SetScrollHereY(1.0f);
    }
    ImGui::EndChild();

    if (jumpTarget != SIZE_MAX)
    {
        const int steps = undo->JumpTo(jumpTarget);
        if (steps != 0)
        {
            ImGuiNotification::Post(steps < 0 ? std::format("{} 件戻しました", -steps) : std::format("{} 件やり直しました", steps),
                                    {0.42f, 0.66f, 0.68f, 1.0f});
        }
    }

    // ---- 履歴を消す ----
    ImGui::BeginDisabled(undoCount + redoCount == 0);
    if (NeutralButton(ICON_FA_TRASH_ALT " 履歴を消す", ImVec2(-1.0f, 0.0f)))
    {
        undo->Clear();
    }
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("今の状態はそのままで、戻す・やり直しの記録だけを消します");

    ImGui::End();
}

void ImGuiManager::ShowPlacementToolWindow()
{
    if (!showPlacementToolView_)
    {
        return;
    }
    ImGui::SetNextWindowSize(ImVec2(360.0f, 480.0f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin(ICON_FA_TH " 配置ツール###PlacementTool", &showPlacementToolView_, ImGuiWindowFlags_NoFocusOnAppearing))
    {
        pImGuizmoManager_->DrawPlacementTool();
    }
    ImGui::End();
}

// ---- カラーパレット -------------------------------------------------------

namespace {
std::string ColorPalettePath()
{
    return AssetPath::Json("ImGuiSetting/ColorPalette.json");
}
} // namespace

void ImGuiManager::LoadColorPalette()
{
    paletteLoaded_ = true;
    paletteSwatches_.clear();
    std::ifstream file(ColorPalettePath());
    if (file)
    {
        const nlohmann::json data = nlohmann::json::parse(file, nullptr, false);
        if (!data.is_discarded() && data.is_array())
        {
            for (const nlohmann::json &entry : data)
            {
                PaletteSwatch swatch;
                swatch.color = entry.value("color", Vector4{1.0f, 1.0f, 1.0f, 1.0f});
                swatch.name = entry.value("name", std::string());
                paletteSwatches_.push_back(swatch);
            }
            return;
        }
    }
    // 初めて開いたときは、エディタのアクセント色と白黒を並べておく
    paletteSwatches_ = {
        {{1.0f, 1.0f, 1.0f, 1.0f}, "白"},
        {{0.0f, 0.0f, 0.0f, 1.0f}, "黒"},
        {{0.45f, 0.60f, 0.78f, 1.0f}, "青"},
        {{0.45f, 0.68f, 0.52f, 1.0f}, "緑"},
        {{0.82f, 0.58f, 0.36f, 1.0f}, "橙"},
        {{0.80f, 0.46f, 0.46f, 1.0f}, "赤"},
        {{0.62f, 0.50f, 0.74f, 1.0f}, "紫"},
        {{0.80f, 0.72f, 0.42f, 1.0f}, "黄"},
    };
}

void ImGuiManager::SaveColorPalette() const
{
    nlohmann::json data = nlohmann::json::array();
    for (const PaletteSwatch &swatch : paletteSwatches_)
    {
        nlohmann::json entry;
        entry["color"] = swatch.color;
        entry["name"] = swatch.name;
        data.push_back(entry);
    }
    std::ofstream file(ColorPalettePath());
    if (file)
    {
        file << data.dump(4);
    }
}

void ImGuiManager::ShowColorPaletteWindow()
{
    if (!showColorPaletteView_)
    {
        return;
    }
    if (!paletteLoaded_)
    {
        LoadColorPalette();
    }
    ImGui::SetNextWindowSize(ImVec2(300.0f, 260.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(ICON_FA_PALETTE " カラーパレット###ColorPalette", &showColorPaletteView_, ImGuiWindowFlags_NoFocusOnAppearing))
    {
        ImGui::End();
        return;
    }

    DimText("色の欄へドラッグで渡す / 色の欄をここへドラッグで追加");
    ImGui::Separator();

    bool changed = false;
    int removeIndex = -1;
    const float swatchSize = ImGui::GetFrameHeight() * 1.6f;
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const int columns = std::max(1, static_cast<int>((ImGui::GetContentRegionAvail().x + spacing) / (swatchSize + spacing)));

    for (int i = 0; i < static_cast<int>(paletteSwatches_.size()); ++i)
    {
        PaletteSwatch &swatch = paletteSwatches_[i];
        ImGui::PushID(i);
        if (i % columns != 0)
        {
            ImGui::SameLine();
        }
        const ImVec4 color(swatch.color.x, swatch.color.y, swatch.color.z, swatch.color.w);
        // ColorButton はそのまま色のドラッグ元になる（受け側は ImGui の色の欄すべて）
        if (ImGui::ColorButton("##swatch", color, ImGuiColorEditFlags_AlphaPreviewHalf, ImVec2(swatchSize, swatchSize)))
        {
            // クリックで数値をコピー（数値欄の右クリック →「貼り付け」で使える）
            const float values[4] = {swatch.color.x, swatch.color.y, swatch.color.z, swatch.color.w};
            ImGui::SetClipboardText(ValueClipboard::Format(values, 4).c_str());
            ImGuiNotification::Post("色をコピーしました: " + ValueClipboard::Format(values, 4), swatch.color);
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("%s\n(%.2f, %.2f, %.2f, %.2f)\nクリック: 数値をコピー / ドラッグ: 色の欄へ / 右クリック: 編集",
                              swatch.name.empty() ? "（名前なし）" : swatch.name.c_str(), swatch.color.x, swatch.color.y,
                              swatch.color.z, swatch.color.w);
        }
        // 色の欄を重ねたら、その色で上書き
        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload(IMGUI_PAYLOAD_TYPE_COLOR_4F))
            {
                const float *c = static_cast<const float *>(payload->Data);
                swatch.color = {c[0], c[1], c[2], c[3]};
                changed = true;
            }
            else if (const ImGuiPayload *payload3 = ImGui::AcceptDragDropPayload(IMGUI_PAYLOAD_TYPE_COLOR_3F))
            {
                const float *c = static_cast<const float *>(payload3->Data);
                swatch.color = {c[0], c[1], c[2], 1.0f};
                changed = true;
            }
            ImGui::EndDragDropTarget();
        }
        if (ImGui::BeginPopupContextItem("##swatchMenu"))
        {
            ImGui::SetNextItemWidth(200.0f);
            if (ImGui::InputTextWithHint("##name", "名前", &swatch.name))
                changed = true;
            float edit[4] = {swatch.color.x, swatch.color.y, swatch.color.z, swatch.color.w};
            if (ImGui::ColorPicker4("##picker", edit, ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf))
            {
                swatch.color = {edit[0], edit[1], edit[2], edit[3]};
                changed = true;
            }
            if (DangerButton(ICON_FA_TRASH_ALT " 削除", ImVec2(-1.0f, 0.0f)))
            {
                removeIndex = i;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        ImGui::PopID();
    }

    // 追加の枠（ボタンを押すと白を足す。色の欄を重ねるとその色を足す）
    if (!paletteSwatches_.empty() && static_cast<int>(paletteSwatches_.size()) % columns != 0)
    {
        ImGui::SameLine();
    }
    if (NeutralButton(ICON_FA_PLUS "##addSwatch", ImVec2(swatchSize, swatchSize)))
    {
        paletteSwatches_.push_back({});
        changed = true;
    }
    ImGui::SetItemTooltip("色を足す（色の欄をここへドラッグしても足せます）");
    if (ImGui::BeginDragDropTarget())
    {
        if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload(IMGUI_PAYLOAD_TYPE_COLOR_4F))
        {
            const float *c = static_cast<const float *>(payload->Data);
            paletteSwatches_.push_back({{c[0], c[1], c[2], c[3]}, ""});
            changed = true;
        }
        else if (const ImGuiPayload *payload3 = ImGui::AcceptDragDropPayload(IMGUI_PAYLOAD_TYPE_COLOR_3F))
        {
            const float *c = static_cast<const float *>(payload3->Data);
            paletteSwatches_.push_back({{c[0], c[1], c[2], 1.0f}, ""});
            changed = true;
        }
        ImGui::EndDragDropTarget();
    }

    if (removeIndex >= 0)
    {
        paletteSwatches_.erase(paletteSwatches_.begin() + removeIndex);
        changed = true;
    }
    if (changed)
    {
        SaveColorPalette();
    }
    ImGui::End();
}

} // namespace Hagine
#endif // USE_IMGUI
