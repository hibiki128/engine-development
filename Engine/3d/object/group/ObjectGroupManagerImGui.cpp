#include "ObjectGroupManager.h"
#ifdef USE_IMGUI
#include "debug/imgui/ImGuizmoManager.h"
#include <algorithm>
#include <edit/undo/UndoRedoManager.h>
#include <format>
#include <imgui.h>
#include <imgui_stdlib.h>
#include <object/base/BaseObject.h>
#include <object/base/BaseObjectManager.h>
#include <utility/debug/imgui/DebugUIHelper.h>
#include <utility/debug/imgui/ImGuiNotification.h>
#endif // USE_IMGUI

namespace Hagine {
#ifdef USE_IMGUI
namespace {
using json = nlohmann::json;

const Vector4 kSuccessColor = {0.45f, 0.68f, 0.52f, 1.0f};
const Vector4 kWarnColor = {0.82f, 0.58f, 0.36f, 1.0f};

/// <summary>
/// ギズモで選択中の BaseObject の名前を集める
/// </summary>
std::vector<std::string> SelectedObjectNames()
{
    std::vector<std::string> names;
    for (BaseObject *obj : ImGuizmoManager::GetInstance()->GetSelectedTargets())
    {
        if (obj)
        {
            names.push_back(obj->GetName());
        }
    }
    std::sort(names.begin(), names.end());
    return names;
}

/// <summary>
/// グループのメンバーをギズモの選択にする
/// </summary>
void SelectMembers(const ObjectGroup &group)
{
    ImGuizmoManager *gizmo = ImGuizmoManager::GetInstance();
    gizmo->SelectOnly("");
    for (BaseObject *obj : group.GetObjects())
    {
        gizmo->AddToSelection(obj->GetName());
    }
}
} // namespace

void ObjectGroupManager::EditWithUndo(const char *label, const std::function<void()> &edit)
{
    json before = CaptureState();
    edit();
    json after = CaptureState();
    if (before == after)
    {
        return;
    }
    UndoRedoManager::GetInstance()->Push(std::make_unique<JsonStateCommand>(
        label, std::move(before), std::move(after),
        [](const json &state) { ObjectGroupManager::GetInstance()->FromJson(state); }));
}

void ObjectGroupManager::EditObjectsWithUndo(const char *label, const std::function<void()> &edit)
{
    BaseObjectManager *objects = BaseObjectManager::GetInstance();
    const json objectsBefore = objects->CaptureUndoState();
    json groupsBefore = CaptureState();
    edit();
    const json objectsAfter = objects->CaptureUndoState();
    json groupsAfter = CaptureState();

    auto [diffBefore, diffAfter] = MakeTopLevelJsonDiff(objectsBefore, objectsAfter);
    json before = {{"objects", std::move(diffBefore)}, {"groups", std::move(groupsBefore)}};
    json after = {{"objects", std::move(diffAfter)}, {"groups", std::move(groupsAfter)}};
    UndoRedoManager::GetInstance()->Push(std::make_unique<JsonStateCommand>(
        label, std::move(before), std::move(after), [](const json &state) {
            BaseObjectManager::GetInstance()->RestoreUndoState(state.value("objects", json::object()));
            ObjectGroupManager::GetInstance()->FromJson(state.value("groups", json::array()));
        }));
    objects->SkipUndoGesture();
}
#endif // USE_IMGUI

void ObjectGroupManager::DrawImGui()
{
#ifdef USE_IMGUI
    if (!selectedName_.empty() && !Find(selectedName_))
    {
        selectedName_.clear();
    }

    // 作成
    const float buttonWidth = ImGui::CalcTextSize("作成").x + ImGui::GetStyle().FramePadding.x * 4.0f;
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - buttonWidth - ImGui::GetStyle().ItemSpacing.x);
    const bool enter = ImGui::InputTextWithHint("##newGroup", "グループ名", &newGroupName_, ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    if ((PrimaryButton("作成", ImVec2(buttonWidth, 0.0f)) || enter) && !newGroupName_.empty())
    {
        EditWithUndo("グループ作成", [this] {
            const std::string name = MakeUniqueName(newGroupName_);
            if (ObjectGroup *group = Create(name))
            {
                for (const std::string &objectName : SelectedObjectNames())
                {
                    group->Add(objectName);
                }
                selectedName_ = group->GetName();
            }
        });
        newGroupName_.clear();
    }
    ImGui::SetItemTooltip("選択中のオブジェクトを入れて作成");

    if (ImGui::BeginTabBar("##objectGroupTabs"))
    {
        if (ImGui::BeginTabItem("シーン"))
        {
            DrawGroupList();
            DrawGroupDetail();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("ファイル"))
        {
            DrawFileList();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
#endif // USE_IMGUI
}

#ifdef USE_IMGUI
void ObjectGroupManager::DrawGroupList()
{
    ImGui::BeginChild("##groupList", ImVec2(0.0f, 130.0f), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeY);
    if (groups_.empty())
    {
        ImGui::TextDisabled("グループがありません");
    }
    for (const auto &[name, group] : groups_)
    {
        const std::string label = std::format("{}  ({})###group_{}", name, group->GetMemberNames().size(), name);
        if (ImGui::Selectable(label.c_str(), selectedName_ == name))
        {
            selectedName_ = name;
        }
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
        {
            SelectMembers(*group);
        }
        // 階層からのドロップで追加
        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("OBJ_NODE"))
            {
                const std::string objectName = static_cast<const char *>(payload->Data);
                const std::string groupName = name;
                EditWithUndo("グループに追加", [this, groupName, objectName] { Find(groupName)->Add(objectName); });
            }
            ImGui::EndDragDropTarget();
        }
    }
    ImGui::EndChild();
}

void ObjectGroupManager::DrawGroupDetail()
{
    ObjectGroup *group = Find(selectedName_);
    if (!group)
    {
        return;
    }
    BaseObjectManager *objects = BaseObjectManager::GetInstance();
    const std::string groupName = group->GetName();

    ImGui::Spacing();

    // 名前
    if (renameSource_ != groupName)
    {
        renameSource_ = groupName;
        renameBuffer_ = groupName;
    }
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputText("##groupName", &renameBuffer_);
    if (ImGui::IsItemDeactivatedAfterEdit())
    {
        std::string newName;
        EditWithUndo("グループ名の変更", [&] { newName = Rename(groupName, renameBuffer_); });
        if (!newName.empty())
        {
            selectedName_ = newName;
        }
        renameSource_.clear();
        return;
    }

    // まとめて操作
    const float third = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x * 2.0f) / 3.0f;
    if (NeutralButton("選択", ImVec2(third, 0.0f)))
    {
        SelectMembers(*group);
    }
    ImGui::SameLine();
    const bool visible = group->IsAnyVisible();
    if (NeutralButton(visible ? "隠す" : "表示", ImVec2(third, 0.0f)))
    {
        group->SetVisible(!visible);
    }
    ImGui::SameLine();
    if (NeutralButton("選択中を追加", ImVec2(third, 0.0f)))
    {
        EditWithUndo("グループに追加", [group] {
            for (const std::string &objectName : SelectedObjectNames())
            {
                group->Add(objectName);
            }
        });
    }

    bool shouldSave = group->GetShouldSave();
    if (ImGui::Checkbox("シーンに保存", &shouldSave))
    {
        EditWithUndo("グループの保存設定", [group, shouldSave] { group->SetShouldSave(shouldSave); });
    }

    // メンバー
    std::string removeTarget;
    const float footerHeight = ImGui::GetFrameHeightWithSpacing() * 2.0f;
    ImGui::BeginChild("##groupMembers", ImVec2(0.0f, -footerHeight), ImGuiChildFlags_Borders);
    if (group->GetMemberNames().empty())
    {
        ImGui::TextDisabled("階層からドラッグで追加");
    }
    ImGuizmoManager *gizmo = ImGuizmoManager::GetInstance();
    for (const std::string &memberName : group->GetMemberNames())
    {
        ImGui::PushID(memberName.c_str());
        BaseObject *obj = objects->GetObjectByName(memberName);
        const char *note = !obj ? "なし" : (objects->IsOwned(obj) ? nullptr : "コード");

        const float removeWidth = ImGui::CalcTextSize("外す").x + ImGui::GetStyle().FramePadding.x * 2.0f;
        if (!obj)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        }
        if (ImGui::Selectable(memberName.c_str(), obj && gizmo->IsSelected(memberName), ImGuiSelectableFlags_AllowOverlap,
                              ImVec2(ImGui::GetContentRegionAvail().x - removeWidth - ImGui::GetStyle().ItemSpacing.x, 0.0f)) &&
            obj)
        {
            if (ImGui::GetIO().KeyCtrl)
            {
                gizmo->ToggleSelect(memberName);
            }
            else
            {
                gizmo->SelectOnly(memberName);
            }
        }
        if (obj && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
        {
            gizmo->SelectOnly(memberName);
            gizmo->FocusOnSelection();
        }
        if (!obj)
        {
            ImGui::PopStyleColor();
        }
        if (note)
        {
            ImGui::SameLine();
            ImGui::TextDisabled("%s", note);
        }
        ImGui::SameLine(ImGui::GetContentRegionMax().x - removeWidth);
        if (ImGui::SmallButton("外す"))
        {
            removeTarget = memberName;
        }
        ImGui::PopID();
    }
    ImGui::EndChild();
    if (ImGui::BeginDragDropTarget())
    {
        if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("OBJ_NODE"))
        {
            const std::string objectName = static_cast<const char *>(payload->Data);
            EditWithUndo("グループに追加", [group, objectName] { group->Add(objectName); });
        }
        ImGui::EndDragDropTarget();
    }
    if (!removeTarget.empty())
    {
        EditWithUndo("グループから外す", [group, removeTarget] { group->Remove(removeTarget); });
    }

    // 書き出し・削除
    const float half = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
    if (ConfirmButton("ファイルに書き出す", ImVec2(-1.0f, 0.0f)))
    {
        if (SaveToFile(groupName))
        {
            ImGuiNotification::Post("グループを書き出しました: " + GroupFilePath(groupName), kSuccessColor);
        }
        else
        {
            ImGuiNotification::Post("グループを書き出せませんでした: " + groupName, kWarnColor);
        }
    }
    if (NeutralButton("グループを削除", ImVec2(half, 0.0f)))
    {
        EditWithUndo("グループ削除", [this, groupName] { Remove(groupName); });
    }
    ImGui::SetItemTooltip("オブジェクトは残します");
    ImGui::SameLine();
    if (DangerButton("中身ごと削除", ImVec2(half, 0.0f)))
    {
        int removed = 0;
        EditObjectsWithUndo("グループを中身ごと削除", [this, groupName, &removed] { removed = Destroy(groupName); });
        ImGuiNotification::Post(std::format("グループ「{}」を削除しました（オブジェクト {} 個）", groupName, removed), kWarnColor);
    }
    ImGui::SetItemTooltip("コードで作ったオブジェクトは消しません");
}

void ObjectGroupManager::DrawFileList()
{
    if (fileListDirty_)
    {
        fileList_ = ListGroupFiles();
        fileListDirty_ = false;
    }
    if (NeutralButton("読み直す", ImVec2(-1.0f, 0.0f)))
    {
        fileListDirty_ = true;
    }

    std::string spawnTarget;
    std::string deleteTarget;
    ImGui::BeginChild("##groupFiles", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders);
    if (fileList_.empty())
    {
        ImGui::TextDisabled("書き出したグループがありません");
    }
    const float spawnWidth = ImGui::CalcTextSize("呼び出す").x + ImGui::GetStyle().FramePadding.x * 2.0f;
    const float deleteWidth = ImGui::CalcTextSize("削除").x + ImGui::GetStyle().FramePadding.x * 2.0f;
    for (const std::string &fileName : fileList_)
    {
        ImGui::PushID(fileName.c_str());
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(fileName.c_str());
        ImGui::SameLine(ImGui::GetContentRegionMax().x - spawnWidth - deleteWidth - ImGui::GetStyle().ItemSpacing.x);
        if (ImGui::SmallButton("呼び出す"))
        {
            spawnTarget = fileName;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("削除"))
        {
            deleteTarget = fileName;
        }
        ImGui::PopID();
    }
    ImGui::EndChild();

    if (!spawnTarget.empty())
    {
        ObjectGroup *spawned = nullptr;
        EditObjectsWithUndo("グループの呼び出し", [this, &spawnTarget, &spawned] {
            spawned = Spawn(spawnTarget, {0.0f, 0.0f, 0.0f}, true);
        });
        if (spawned)
        {
            selectedName_ = spawned->GetName();
            SelectMembers(*spawned);
            ImGuiNotification::Post(std::format("グループ「{}」を呼び出しました", spawned->GetName()), kSuccessColor);
        }
        else
        {
            ImGuiNotification::Post("グループを読み込めませんでした: " + spawnTarget, kWarnColor);
        }
    }
    if (!deleteTarget.empty() && DeleteGroupFile(deleteTarget))
    {
        ImGuiNotification::Post("グループファイルを削除しました: " + deleteTarget, kWarnColor);
    }
}
#endif // USE_IMGUI

void ObjectGroupManager::DrawMembershipMenu(const std::vector<std::string> &objectNames)
{
#ifdef USE_IMGUI
    if (objectNames.empty())
    {
        return;
    }
    for (const auto &[name, group] : groups_)
    {
        bool containsAll = true;
        for (const std::string &objectName : objectNames)
        {
            containsAll = containsAll && group->Contains(objectName);
        }
        if (ImGui::MenuItem(name.c_str(), nullptr, containsAll))
        {
            ObjectGroup *target = group.get();
            EditWithUndo(containsAll ? "グループから外す" : "グループに追加", [target, &objectNames, containsAll] {
                for (const std::string &objectName : objectNames)
                {
                    containsAll ? target->Remove(objectName) : target->Add(objectName);
                }
            });
        }
    }
    if (!groups_.empty())
    {
        ImGui::Separator();
    }
    ImGui::SetNextItemWidth(160.0f);
    const bool enter = ImGui::InputTextWithHint("##menuNewGroup", "新しいグループ", &menuNewGroupName_,
                                                ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    if ((ImGui::SmallButton("作成") || enter) && !menuNewGroupName_.empty())
    {
        EditWithUndo("グループ作成", [this, &objectNames] {
            if (ObjectGroup *group = Create(MakeUniqueName(menuNewGroupName_)))
            {
                for (const std::string &objectName : objectNames)
                {
                    group->Add(objectName);
                }
                selectedName_ = group->GetName();
            }
        });
        menuNewGroupName_.clear();
        ImGui::CloseCurrentPopup();
    }
#else
    (void)objectNames;
#endif // USE_IMGUI
}
} // namespace Hagine
