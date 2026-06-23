#include "ColliderTagManager.h"

#ifdef _DEBUG
#include <imgui.h>
#include "Utility/Debug/ImGui/ImGuiNotification.h"
#include "Utility/Debug/ImGui/Debugui_improved.h"

namespace Hagine {
void ColliderTagManager::ImGuiTagManager() {

    SectionHeader("[ 登録済みタグ ]", DebugTheme::kAccentBlue);

    // タグ名でソートして一覧化する
    std::vector<std::string> tagList(availableTags_.begin(), availableTags_.end());
    std::sort(tagList.begin(), tagList.end());

    // 既定タグ（削除不可）かどうか
    auto isDefault = [](const std::string &t) {
        return t == "None" || t == "Environment" || t == "Player";
    };

    if (ImGui::BeginTable("##TagTable", 2,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                              ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("タグ", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("操作", ImGuiTableColumnFlags_WidthFixed, 72.0f);

        for (const auto &tag : tagList) {
            ImGui::TableNextRow();

            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::BulletText("%s", tag.c_str());

            ImGui::TableNextColumn();
            if (isDefault(tag)) {
                ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
                ImGui::TextUnformatted("既定");
                ImGui::PopStyleColor();
                ImGui::SetItemTooltip("既定タグは削除できません");
            } else {
                ImGui::PushID(tag.c_str());
                ImGui::PushStyleColor(ImGuiCol_Button, DebugTheme::kBgRed);
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.80f, 0.46f, 0.46f, 0.40f));
                if (ImGui::SmallButton("削除")) {
                    RemoveTag(tag);
                    ImGuiNotification::Post("タグを削除しました: " + tag, {0.82f, 0.58f, 0.36f, 1.0f});
                }
                ImGui::PopStyleColor(2);
                ImGui::PopID();
            }
        }
        ImGui::EndTable();
    }

    ImGui::Spacing();
    SectionHeader("[ 新規タグ追加 ]", DebugTheme::kAccentGreen);

    static char newTagBuffer[64] = "";
    ImGui::SetNextItemWidth(-86.0f);
    bool entered = ImGui::InputTextWithHint("##NewTag", "タグ名を入力", newTagBuffer,
                                            sizeof(newTagBuffer), ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, DebugTheme::kBgGreen);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.45f, 0.68f, 0.52f, 0.40f));
    bool clicked = ImGui::Button("追加", ImVec2(78.0f, 0.0f));
    ImGui::PopStyleColor(2);

    if (entered || clicked) {
        std::string newTag = newTagBuffer;
        if (newTag.empty()) {
            ImGuiNotification::Post("タグ名を入力してください", {0.82f, 0.58f, 0.36f, 1.0f});
        } else if (HasTag(newTag)) {
            ImGuiNotification::Post("既に存在するタグです: " + newTag, {0.82f, 0.58f, 0.36f, 1.0f});
        } else {
            AddTag(newTag);
            ImGuiNotification::Post("タグを追加しました: " + newTag, {0.45f, 0.68f, 0.52f, 1.0f});
            newTagBuffer[0] = '\0';
        }
    }
}

} // namespace Hagine
#endif
