#define NOMINMAX
#ifdef USE_IMGUI
#include "ImGuizmoManager.h"
#include "ImGuiNotification.h"
#include "Input.h"
#include "Sprite.h"
#include <line/LineRenderer.h>
#include <object/base/BaseObjectManager.h>
#include <transform/WorldTransform.h>
#include <edit/undo/UndoRedoManager.h>
#include "WinApp.h"
#include <format>
#include <imgui.h>
// DebugUIHelper.h は ImVec4 / ImGui:: を使うので imgui.h の後に include する
#include "DebugUIHelper.h"

// =======================================================================
// ImGuizmoManager: エディタUI（一覧・検索・操作モード・フィルタ）
// =======================================================================

namespace Hagine {
// ---- imgui ------------------------------------------------------------

void ImGuizmoManager::DrawImGui()
{
    if (!pViewProjection_)
        return;

    ImGui::PushStyleColor(ImGuiCol_CheckMark, DebugTheme::kAccentGreen);
    ImGui::Checkbox("デバッグ表示する", &isDrawDebug_);
    ImGui::PopStyleColor();
    ImGui::SetItemTooltip("選択中オブジェクトの AABB / スフィア / レイを線で表示します");

    if (isDrawDebug_)
    {
        ImGui::Indent();
        ImGui::PushStyleColor(ImGuiCol_CheckMark, DebugTheme::kAccentGreen);
        ImGui::Checkbox("選択中のみ", &debugSelectedOnly_);
        ImGui::SameLine();
        ImGui::Checkbox("AABB", &showDebugAABB_);
        ImGui::SameLine();
        ImGui::Checkbox("スフィア", &showDebugSphere_);
        ImGui::SameLine();
        ImGui::Checkbox("レイ", &showDebugHitPoints_);
        ImGui::PopStyleColor();
        ImGui::Unindent();
    }

    // ---- 操作説明 ----
    ImGui::Spacing();
    if (ImGui::CollapsingHeader("[ ショートカット ]"))
    {
        ImGui::BulletText("1 / 2 / 3 : 移動 / 回転 / スケール");
        ImGui::BulletText("4 : ローカル ⇔ ワールド 切替");
        ImGui::BulletText("5 : スナップ ON/OFF （Shift 押下中は一時反転）");
        ImGui::BulletText("F : 選択オブジェクトへ視点を寄せる");
        ImGui::BulletText("Tab : 重なったオブジェクトを順に選択");
        ImGui::BulletText("Ctrl+D : 複製 / Ctrl+C・Ctrl+V : コピー・貼り付け");
        ImGui::BulletText("空ドラッグ : 矩形選択（Ctrl 併用で選択に追加）");
        ImGui::TextDisabled("※ シーンウィンドウにマウスがある時だけ効きます");
    }

    // ---- 操作対象フィルタ ----
    // 4種類（オブジェクト/スプライト/パーティクル/ライト）が同時にあると掴みたい物を選びづらいので、
    // チェックした種類だけを選択・マウスピック・ギズモ表示・デバッグ描画の対象にする。
    ImGui::Spacing();
    SectionHeader("[ 操作対象フィルタ ]", DebugTheme::kAccentGreen);
    ImGui::TextDisabled("チェックした種類だけ選択・操作できます");
    bool filterChanged = false;

    // 分類の表示名。追加時はここと GizmoCategory を対応させる
    static const char *kCategoryLabels[kGizmoCategoryCount] = {
        "オブジェクト", "スプライト", "パーティクル", "ライト"};

    ImGui::PushStyleColor(ImGuiCol_CheckMark, DebugTheme::kAccentGreen);
    for (int i = 0; i < kGizmoCategoryCount; ++i)
    {
        if (i > 0)
            ImGui::SameLine();
        filterChanged |= ImGui::Checkbox(kCategoryLabels[i], &categoryEnabled_[i]);
    }
    ImGui::PopStyleColor();

    // 「この種類だけ」を素早く選べるショートカット
    auto SoloCategory = [this](int index) {
        for (int i = 0; i < kGizmoCategoryCount; ++i)
            categoryEnabled_[i] = (i == index);
    };
    if (ImGui::SmallButton("全部##catAll"))
    {
        for (bool &e : categoryEnabled_)
            e = true;
        filterChanged = true;
    }
    for (int i = 0; i < kGizmoCategoryCount; ++i)
    {
        ImGui::SameLine();
        ImGui::PushID(i);
        if (ImGui::SmallButton(std::format("{}のみ", kCategoryLabels[i]).c_str()))
        {
            SoloCategory(i);
            filterChanged = true;
        }
        ImGui::PopID();
    }
    if (filterChanged)
    {
        // 無効化された種類の選択を解除し、一覧も更新する
        PruneSelectionByFilter();
        UpdateFilteredNames();
    }

    ImGui::Spacing();
    SectionHeader("[ 操作モード ]", DebugTheme::kAccentBlue);
    ImGui::PushStyleColor(ImGuiCol_CheckMark, DebugTheme::kAccentBlue);
    if (ImGui::RadioButton("移動", currentOperation_ == ImGuizmo::TRANSLATE))
        currentOperation_ = ImGuizmo::TRANSLATE;
    ImGui::SameLine();
    if (ImGui::RadioButton("回転", currentOperation_ == ImGuizmo::ROTATE))
        currentOperation_ = ImGuizmo::ROTATE;
    ImGui::SameLine();
    if (ImGui::RadioButton("スケール", currentOperation_ == ImGuizmo::SCALE))
        currentOperation_ = ImGuizmo::SCALE;
    ImGui::PopStyleColor();

    ImGui::Spacing();
    SectionHeader("[ 座標系 ]", DebugTheme::kAccentCyan);
    ImGui::PushStyleColor(ImGuiCol_CheckMark, DebugTheme::kAccentCyan);
    if (ImGui::RadioButton("ローカル", currentMode_ == ImGuizmo::LOCAL))
        currentMode_ = ImGuizmo::LOCAL;
    ImGui::SameLine();
    if (ImGui::RadioButton("ワールド", currentMode_ == ImGuizmo::WORLD))
        currentMode_ = ImGuizmo::WORLD;
    ImGui::PopStyleColor();

    ImGui::Spacing();
    SectionHeader("[ スナップ（グリッド吸着）]", DebugTheme::kAccentYellow);
    ImGui::PushStyleColor(ImGuiCol_CheckMark, DebugTheme::kAccentYellow);
    ImGui::Checkbox("スナップを使う", &useSnap_);
    ImGui::PopStyleColor();
    ImGui::SetItemTooltip("操作量を刻み幅に丸めます。Shift 押下中はこの設定が一時的に反転します");

    ImGui::SetNextItemWidth(120.0f);
    ImGui::DragFloat("移動の刻み", &snapTranslate_, 0.05f, 0.01f, 100.0f, "%.2f");
    ImGui::SameLine();
    // 等間隔に並べるときによく使う刻みをワンタッチで
    if (ImGui::SmallButton("0.5##snapT"))
        snapTranslate_ = 0.5f;
    ImGui::SameLine();
    if (ImGui::SmallButton("1##snapT"))
        snapTranslate_ = 1.0f;
    ImGui::SameLine();
    if (ImGui::SmallButton("5##snapT"))
        snapTranslate_ = 5.0f;

    ImGui::SetNextItemWidth(120.0f);
    ImGui::DragFloat("回転の刻み(度)", &snapRotateDegree_, 1.0f, 1.0f, 180.0f, "%.0f");
    ImGui::SameLine();
    if (ImGui::SmallButton("15##snapR"))
        snapRotateDegree_ = 15.0f;
    ImGui::SameLine();
    if (ImGui::SmallButton("45##snapR"))
        snapRotateDegree_ = 45.0f;
    ImGui::SameLine();
    if (ImGui::SmallButton("90##snapR"))
        snapRotateDegree_ = 90.0f;

    ImGui::SetNextItemWidth(120.0f);
    ImGui::DragFloat("拡縮の刻み", &snapScale_, 0.01f, 0.01f, 10.0f, "%.2f");

    ImGui::Spacing();
    SectionHeader("[ 整列・配置 ]", DebugTheme::kAccentPurple);
    ImGui::TextDisabled("選択中のオブジェクトをまとめて並べます");

    static const char *kAxisLabels[3] = {"X", "Y", "Z"};
    // 軸ごとに「最小 / 中央 / 最大 に揃える」を並べる
    for (int axis = 0; axis < 3; ++axis)
    {
        ImGui::PushID(axis);
        ImGui::TextUnformatted(kAxisLabels[axis]);
        ImGui::SameLine();
        if (ImGui::SmallButton("最小##align"))
            AlignSelected(axis, AlignMode::Min);
        ImGui::SameLine();
        if (ImGui::SmallButton("中央##align"))
            AlignSelected(axis, AlignMode::Center);
        ImGui::SameLine();
        if (ImGui::SmallButton("最大##align"))
            AlignSelected(axis, AlignMode::Max);
        ImGui::SameLine();
        if (ImGui::SmallButton("等間隔##dist"))
            DistributeSelected(axis);
        ImGui::PopID();
    }
    ImGui::SetItemTooltip("等間隔は両端をそのままに、間のオブジェクトを均等な位置へ動かします（3つ以上必要）");

    if (ImGui::Button("地面に接地##snapGround", ImVec2(-1, 0)))
    {
        SnapSelectedToGround();
    }
    ImGui::SetItemTooltip("選択中のオブジェクトを、真下にある他のオブジェクトの上面へ落とします\n"
                          "（下に何も無ければ Y=0 へ）");

    ImGui::Separator();

    SectionHeader("[ オブジェクト選択 ]", DebugTheme::kAccentPurple);
    // 検索ボックス（ヒント付き・全幅）
    ImGui::SetNextItemWidth(-1);
    bool searchChanged = ImGui::InputTextWithHint("##ObjectSearch", "名前で絞り込み...", searchBuffer_, sizeof(searchBuffer_));
    if (searchChanged)
        UpdateFilteredNames();
    if (filteredNames_.empty())
        UpdateFilteredNames();

    std::string currentDisplayName = selectedNames_.empty() ? "なし"
                                                            : (selectedNames_.size() == 1 ? *selectedNames_.begin()
                                                                                          : "複数選択 (" + std::to_string(selectedNames_.size()) + "個)");

    if (ImGui::BeginCombo("選択オブジェクト", currentDisplayName.c_str()))
    {
        bool isNoneSelected = selectedNames_.empty();
        if (ImGui::Selectable("なし", isNoneSelected))
            selectedNames_.clear();
        if (isNoneSelected)
            ImGui::SetItemDefaultFocus();

        for (const std::string &name : filteredNames_)
        {
            auto it = transformMap_.find(name);
            if (it != transformMap_.end())
            {
                bool isSelected = (selectedNames_.find(name) != selectedNames_.end());
                if (ImGui::Selectable(name.c_str(), isSelected))
                {
                    selectedNames_.clear();
                    selectedNames_.insert(name);
                }
                if (isSelected)
                    ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }

    if (strlen(searchBuffer_) > 0)
    {
        ImGui::Text("検索結果: %zu個", filteredNames_.size());
    }

    ImGui::Spacing();
    ImGui::Text("選択中のオブジェクト数: %zu", selectedNames_.size());
    if (!selectedNames_.empty())
    {
        ImGui::Text("選択中:");
        for (const std::string &name : selectedNames_)
        {
            ImGui::BulletText("%s", name.c_str());
        }
    }

    ImGui::Separator();

    if (ImGui::Button("全選択"))
    {
        selectedNames_.clear();
        for (const auto &pair : transformMap_)
            selectedNames_.insert(pair.first);
    }
    ImGui::SameLine();
    if (ImGui::Button("選択解除"))
        selectedNames_.clear();

    ImGui::Spacing();

    if (!selectedNames_.empty())
    {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 0.6f, 1.0f));
        ImGui::Text("オブジェクト詳細 (%s)", selectedNames_.begin()->c_str());
        ImGui::PopStyleColor();
        ImGui::Separator();

        ShowSelectedObjectImGui();

        ImGui::Spacing();
        ImGui::Spacing();

        // BaseObject のみコピー・ペーストが可能
        auto it = transformMap_.find(*selectedNames_.begin());
        if (it != transformMap_.end() && it->second.type == GizmoTarget::Type::BaseObject)
        {
            if (ImGui::Button("複製 (Ctrl+D)", ImVec2(-1, 30)))
                DuplicateSelectedObjects();
            ImGui::SetItemTooltip("選択中のオブジェクトをその場で複製し、複製したほうを選択状態にします");
            if (ImGui::Button("コピー", ImVec2(-1, 30)))
                CopySelectedObjects();
            if (!copiedNames_.empty())
            {
                if (ImGui::Button("ペースト", ImVec2(-1, 30)))
                    PasteObjects();
            }
            ImGui::Spacing();
        }

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.3f, 0.3f, 0.8f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.7f, 0.2f, 0.2f, 1.0f));
        if (ImGui::Button("選択オブジェクトを削除", ImVec2(-1, 0)))
            DeleteSelectedObjects();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("選択中の全オブジェクトを削除します");
        ImGui::PopStyleColor(3);
    }

    // 重複オブジェクト候補（Tab でサイクル）
    if (overlapCandidates_.size() > 1)
    {
        ImGui::Separator();
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.9f, 0.4f, 1.0f));
        ImGui::Text("重複候補: %zu個 (Tab でサイクル選択)", overlapCandidates_.size());
        ImGui::PopStyleColor();
        for (int i = 0; i < static_cast<int>(overlapCandidates_.size()); ++i)
        {
            bool isCurrent = (i == overlapCycleIndex_);
            if (isCurrent)
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 1.0f, 0.4f, 1.0f));
            ImGui::Text("  [%d] %s", i, overlapCandidates_[i].first.c_str());
            if (isCurrent)
                ImGui::PopStyleColor();
        }
    }

    ImGui::Separator();
    if (isDrawDebug_)
        DrawDebugRaycast();
}

// ---- ShowSelectedObjectImGui ------------------------------------------

// 選択中エントリの ShowImGui を呼び出す
void ImGuizmoManager::ShowSelectedObjectImGui()
{
    if (selectedNames_.empty())
        return;

    std::string firstName = *selectedNames_.begin();
    auto it = transformMap_.find(firstName);
    if (it != transformMap_.end())
    {
        it->second.ShowImGui();
    }

    if (selectedNames_.size() > 1)
    {
        ImGui::Separator();
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 1.0f, 1.0f));
        ImGui::Text("※ %zu個のオブジェクトが選択されています", selectedNames_.size());
        ImGui::Text("表示しているのは '%s' の設定です", firstName.c_str());
        ImGui::PopStyleColor();
    }
}

// ---- UpdateFilteredNames ----------------------------------------------

// 検索バッファに基づいてフィルタ済みの名前リストを更新する
void ImGuizmoManager::UpdateFilteredNames()
{
    filteredNames_.clear();

    std::vector<std::string> allNames;
    for (const auto &pair : transformMap_)
    {
        // 操作対象フィルタで無効化された種類は一覧に出さない
        if (!IsCategoryEnabled(pair.second.category))
            continue;
        allNames.push_back(pair.first);
    }
    std::sort(allNames.begin(), allNames.end());

    std::string searchStr = searchBuffer_;
    std::transform(searchStr.begin(), searchStr.end(), searchStr.begin(), ::tolower);

    for (const std::string &name : allNames)
    {
        if (strlen(searchBuffer_) == 0)
        {
            filteredNames_.push_back(name);
        }
        else
        {
            std::string lowerName = name;
            std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);
            if (lowerName.find(searchStr) != std::string::npos)
            {
                filteredNames_.push_back(name);
            }
        }
    }
}

} // namespace Hagine
#endif // USE_IMGUI
