#include "AttachmentManager.h"
#include <MyMath.h>
#include <algorithm>
#include <data/DataHandler.h>
#include <format>
#include <transform/WorldTransform.h>
#ifdef USE_IMGUI
#include "imgui.h"
#include <utility/debug/imgui/DebugUIHelper.h>
#include <utility/debug/imgui/ImGuiNotification.h>
#endif

namespace Hagine {
namespace {
// 「子が外から動かされたか」を判定するときの許容差。
// 浮動小数の丸めで毎フレーム相対値を取り直してしまわない程度に緩くしておく。
constexpr float kMoveEpsilon = 1e-4f;

/// <summary>
/// 種類の表示名
/// </summary>
const char *KindLabel(AttachKind kind)
{
    switch (kind)
    {
    case AttachKind::Light:
        return "光源";
    case AttachKind::Particle:
        return "パーティクル";
    case AttachKind::Object:
    default:
        return "オブジェクト";
    }
}

/// <summary>
/// 種類を保存用の整数へ／整数から
/// </summary>
int KindToInt(AttachKind kind) { return static_cast<int>(kind); }
AttachKind IntToKind(int value)
{
    switch (value)
    {
    case 1:
        return AttachKind::Light;
    case 2:
        return AttachKind::Particle;
    default:
        return AttachKind::Object;
    }
}

/// <summary>
/// 2つの位置が実質同じか
/// </summary>
bool NearlyEqual(const Vector3 &a, const Vector3 &b)
{
    return (a - b).Length() <= kMoveEpsilon;
}
} // namespace

void AttachmentManager::Finalize()
{
    targets_.clear();
    links_.clear();
}

// ===================================================
//  対象の登録
// ===================================================

void AttachmentManager::Register(const AttachTarget &target)
{
    if (target.name.empty() || !target.IsValid())
    {
        return;
    }
    targets_[target.name] = target;
}

void AttachmentManager::Unregister(const std::string &name)
{
    targets_.erase(name);
}

void AttachmentManager::UnregisterKind(AttachKind kind)
{
    for (auto it = targets_.begin(); it != targets_.end();)
    {
        it = (it->second.kind == kind) ? targets_.erase(it) : std::next(it);
    }
}

const AttachTarget *AttachmentManager::FindTarget(const std::string &name) const
{
    auto it = targets_.find(name);
    return (it != targets_.end()) ? &it->second : nullptr;
}

std::vector<std::string> AttachmentManager::GetTargetNames(AttachKind kind) const
{
    std::vector<std::string> names;
    for (const auto &[name, target] : targets_)
    {
        if (target.kind == kind)
        {
            names.push_back(name);
        }
    }
    std::sort(names.begin(), names.end());
    return names;
}

// ===================================================
//  親子付け
// ===================================================

bool AttachmentManager::WouldCreateCycle(const std::string &childName, const std::string &parentName) const
{
    // 親をたどっていって自分に戻ってきたら循環
    std::string current = parentName;
    for (int guard = 0; guard < 256 && !current.empty(); ++guard)
    {
        if (current == childName)
        {
            return true;
        }
        auto it = links_.find(current);
        current = (it != links_.end()) ? it->second.parentName : std::string();
    }
    return false;
}

bool AttachmentManager::Attach(const std::string &childName, const std::string &parentName)
{
    if (childName.empty() || parentName.empty() || childName == parentName)
    {
        return false;
    }
    const AttachTarget *child = FindTarget(childName);
    const AttachTarget *parent = FindTarget(parentName);
    if (!child || !parent)
    {
        return false;
    }
    if (WouldCreateCycle(childName, parentName))
    {
        return false;
    }

    Link link;
    link.parentName = parentName;
    // 今の見た目の位置関係をそのまま保つよう、相対値をここで求めておく
    RebuildLocal(link, *child, *parent);
    links_[childName] = link;
    return true;
}

void AttachmentManager::Detach(const std::string &childName)
{
    links_.erase(childName);
}

std::string AttachmentManager::GetParentName(const std::string &childName) const
{
    auto it = links_.find(childName);
    return (it != links_.end()) ? it->second.parentName : std::string();
}

std::vector<std::string> AttachmentManager::GetChildNames(const std::string &parentName) const
{
    std::vector<std::string> names;
    for (const auto &[childName, link] : links_)
    {
        if (link.parentName == parentName)
        {
            names.push_back(childName);
        }
    }
    std::sort(names.begin(), names.end());
    return names;
}

bool AttachmentManager::HasChildren(const std::string &parentName) const
{
    for (const auto &[childName, link] : links_)
    {
        if (link.parentName == parentName)
        {
            return true;
        }
    }
    return false;
}

void AttachmentManager::RenameTarget(const std::string &oldName, const std::string &newName)
{
    if (oldName == newName || oldName.empty() || newName.empty())
    {
        return;
    }

    // 登録を付け替える
    auto targetIt = targets_.find(oldName);
    if (targetIt != targets_.end())
    {
        AttachTarget moved = targetIt->second;
        moved.name = newName;
        targets_.erase(targetIt);
        targets_[newName] = moved;
    }

    // 子としてのリンク
    auto linkIt = links_.find(oldName);
    if (linkIt != links_.end())
    {
        Link moved = linkIt->second;
        links_.erase(linkIt);
        links_[newName] = moved;
    }

    // 親としてのリンク
    for (auto &[childName, link] : links_)
    {
        if (link.parentName == oldName)
        {
            link.parentName = newName;
        }
    }
}

// ===================================================
//  トランスフォームの読み書き
// ===================================================

Vector3 AttachmentManager::ReadPosition(const AttachTarget &target)
{
    if (target.worldTransform)
    {
        return target.worldTransform->GetWorldPosition();
    }
    return target.position ? *target.position : Vector3{};
}

Quaternion AttachmentManager::ReadRotation(const AttachTarget &target)
{
    if (target.worldTransform)
    {
        return target.worldTransform->GetWorldRotationQuaternion();
    }
    if (target.rotationEuler)
    {
        return Quaternion::FromEulerAngles(*target.rotationEuler);
    }
    return Quaternion::IdentityQuaternion();
}

void AttachmentManager::WritePosition(const AttachTarget &target, const Vector3 &position)
{
    if (target.worldTransform)
    {
        // 子が BaseObject 同士の親子付けを持っている場合は、そちらの仕組みが優先。
        // ここへ来るのは親を持たない対象だけなので、ローカル＝ワールドとして書いてよい。
        target.worldTransform->translation_ = position;
        return;
    }
    if (target.position)
    {
        *target.position = position;
    }
}

void AttachmentManager::WriteRotation(const AttachTarget &target, const Quaternion &rotation)
{
    if (target.worldTransform)
    {
        target.worldTransform->SetRotationQuaternion(rotation);
        return;
    }
    if (target.rotationEuler)
    {
        *target.rotationEuler = rotation.ToEulerAngles();
    }
}

void AttachmentManager::RebuildLocal(Link &link, const AttachTarget &child, const AttachTarget &parent) const
{
    const Vector3 parentPos = ReadPosition(parent);
    const Quaternion parentRot = ReadRotation(parent);
    const Quaternion invParentRot = parentRot.Inverse();

    // 親のローカル空間へ引き戻す
    link.localPosition = invParentRot * (ReadPosition(child) - parentPos);
    link.localRotation = invParentRot * ReadRotation(child);
    if (child.direction)
    {
        link.localDirection = invParentRot * (*child.direction);
    }
    link.hasLocal = true;
}

// ===================================================
//  更新
// ===================================================

void AttachmentManager::Update()
{
    for (auto &[childName, link] : links_)
    {
        const AttachTarget *child = FindTarget(childName);
        const AttachTarget *parent = FindTarget(link.parentName);
        // 相手がまだ居なければ何もしない（読み込み順を気にしなくてよいようにするため）
        if (!child || !parent)
        {
            continue;
        }

        // 前フレームに書いた値と違っていれば、子が外から動かされたということ。
        // 相対値を取り直して、その新しい位置関係でくっつけ直す。
        const bool movedByUser =
            link.hasWritten &&
            (!NearlyEqual(ReadPosition(*child), link.lastWrittenPosition) ||
             (child->direction && !NearlyEqual(*child->direction, link.lastWrittenDirection)));

        if (!link.hasLocal || movedByUser)
        {
            RebuildLocal(link, *child, *parent);
        }

        const Vector3 parentPos = ReadPosition(*parent);
        const Quaternion parentRot = ReadRotation(*parent);

        // 位置: 親の回転で相対位置を回してから親の位置へ足す
        Vector3 worldPosition = ReadPosition(*child);
        if (link.inheritTranslation)
        {
            const Vector3 rotatedOffset = link.inheritRotation ? (parentRot * link.localPosition) : link.localPosition;
            worldPosition = parentPos + rotatedOffset;
            WritePosition(*child, worldPosition);
        }

        // 回転
        if (link.inheritRotation)
        {
            const Quaternion worldRotation = parentRot * link.localRotation;
            WriteRotation(*child, worldRotation);
            link.lastWrittenRotation = worldRotation;

            // 向きベクトル（スポットライト）も親の回転で回す
            if (child->direction)
            {
                Vector3 worldDirection = parentRot * link.localDirection;
                if (worldDirection.Length() > 1e-6f)
                {
                    worldDirection = worldDirection.Normalize();
                }
                *child->direction = worldDirection;
                link.lastWrittenDirection = worldDirection;
            }
        }

        link.lastWrittenPosition = worldPosition;
        link.hasWritten = true;
    }
}

// ===================================================
//  UI
// ===================================================

void AttachmentManager::DrawImGui()
{
#ifdef USE_IMGUI
    SectionHeader("[ 親子付け（種類をまたいだアタッチ）]", DebugTheme::kAccentCyan);
    ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
    ImGui::TextWrapped("光源やパーティクルを3Dオブジェクトに付けられます。\n"
                       "付けたあとも子を動かせば、その位置関係のままついていきます");
    ImGui::PopStyleColor();

    ImGui::Spacing();

    // ---- 子の選択 ----
    // 親になれるのは全種類、子になれるのも全種類なので、一覧はまとめて出す
    std::vector<std::string> allNames;
    for (const auto &[name, target] : targets_)
    {
        allNames.push_back(name);
    }
    std::sort(allNames.begin(), allNames.end());

    if (allNames.empty())
    {
        ImGui::TextDisabled("親子付けできる対象がまだありません");
        return;
    }

    ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextCaption);
    ImGui::TextUnformatted("子（付ける側）");
    ImGui::PopStyleColor();
    ImGui::SetNextItemWidth(-1);
    if (ImGui::BeginCombo("##attachChild", uiSelectedChild_.empty() ? "選択してください" : uiSelectedChild_.c_str()))
    {
        for (const std::string &name : allNames)
        {
            const AttachTarget *target = FindTarget(name);
            const std::string label = std::format("[{}] {}", KindLabel(target->kind), name);
            if (ImGui::Selectable(label.c_str(), uiSelectedChild_ == name))
            {
                uiSelectedChild_ = name;
            }
        }
        ImGui::EndCombo();
    }

    if (uiSelectedChild_.empty() || !FindTarget(uiSelectedChild_))
    {
        return;
    }

    // ---- 親の選択 ----
    const std::string currentParent = GetParentName(uiSelectedChild_);
    ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextCaption);
    ImGui::TextUnformatted("親（付けられる側）");
    ImGui::PopStyleColor();
    ImGui::SetNextItemWidth(-1);
    if (ImGui::BeginCombo("##attachParent", currentParent.empty() ? "（親なし）" : currentParent.c_str()))
    {
        if (ImGui::Selectable("（親なし）", currentParent.empty()))
        {
            Detach(uiSelectedChild_);
        }
        for (const std::string &name : allNames)
        {
            if (name == uiSelectedChild_ || WouldCreateCycle(uiSelectedChild_, name))
            {
                continue;
            }
            const AttachTarget *target = FindTarget(name);
            const std::string label = std::format("[{}] {}", KindLabel(target->kind), name);
            if (ImGui::Selectable(label.c_str(), currentParent == name))
            {
                if (Attach(uiSelectedChild_, name))
                {
                    ImGuiNotification::Post(uiSelectedChild_ + " を " + name + " に付けました",
                                            {0.45f, 0.68f, 0.52f, 1.0f});
                }
                else
                {
                    ImGuiNotification::Post("親子付けできません（循環参照など）", {0.82f, 0.58f, 0.36f, 1.0f});
                }
            }
        }
        ImGui::EndCombo();
    }

    // ---- 付いているときの詳細 ----
    auto linkIt = links_.find(uiSelectedChild_);
    if (linkIt == links_.end())
    {
        return;
    }
    Link &link = linkIt->second;

    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
    ImGui::TextUnformatted("親の継承（外すとその成分は追従しません）");
    ImGui::PopStyleColor();
    ImGui::Checkbox("位置を継承##attachInhT", &link.inheritTranslation);
    ImGui::SameLine();
    ImGui::Checkbox("回転を継承##attachInhR", &link.inheritRotation);

    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextCaption);
    ImGui::TextUnformatted("親から見た相対位置");
    ImGui::PopStyleColor();
    ImGui::SetNextItemWidth(-1);
    if (ImGui::DragFloat3("##attachLocalPos", &link.localPosition.x, 0.05f, 0.0f, 0.0f, "%.2f"))
    {
        // ここで直接いじった値をそのまま使う（次のUpdateで取り直されないよう書き込み済み扱いにする）
        link.hasLocal = true;
        link.hasWritten = false;
    }
    ImGui::SetItemTooltip("シーン上で子を動かしてもここの値は更新されます");

    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Button, DebugTheme::kBgRed);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.80f, 0.46f, 0.46f, 0.40f));
    const bool detach = ImGui::Button("親子付けを解除", ImVec2(-1, 0));
    ImGui::PopStyleColor(2);
    if (detach)
    {
        Detach(uiSelectedChild_);
        ImGuiNotification::Post("親子付けを解除しました: " + uiSelectedChild_, {0.82f, 0.58f, 0.36f, 1.0f});
    }
#endif // USE_IMGUI
}

// ===================================================
//  セーブ / ロード
// ===================================================

void AttachmentManager::Save(const std::string &folderPath, const std::string &fileName) const
{
    auto dataHandler = std::make_unique<DataHandler>(folderPath, fileName);

    // 前回より数が減ったときに古いキーが残らないよう、まとめて消してから書き直す
    dataHandler->RemoveByPrefix("link_");

    int index = 0;
    for (const auto &[childName, link] : links_)
    {
        const std::string prefix = std::format("link_{:03d}_", index++);
        dataHandler->Save<std::string>(prefix + "child", childName);
        dataHandler->Save<std::string>(prefix + "parent", link.parentName);
        dataHandler->Save<Vector3>(prefix + "localPosition", link.localPosition);
        dataHandler->Save<Quaternion>(prefix + "localRotation", link.localRotation);
        dataHandler->Save<Vector3>(prefix + "localDirection", link.localDirection);
        dataHandler->Save<bool>(prefix + "inheritTranslation", link.inheritTranslation);
        dataHandler->Save<bool>(prefix + "inheritRotation", link.inheritRotation);
    }
    dataHandler->Save<int32_t>("link_count", index);
    dataHandler->Flush();
}

void AttachmentManager::Load(const std::string &folderPath, const std::string &fileName)
{
    auto dataHandler = std::make_unique<DataHandler>(folderPath, fileName);

    links_.clear();
    const int32_t count = dataHandler->Load<int32_t>("link_count", 0);
    for (int32_t i = 0; i < count; ++i)
    {
        const std::string prefix = std::format("link_{:03d}_", i);
        const std::string childName = dataHandler->Load<std::string>(prefix + "child", "");
        const std::string parentName = dataHandler->Load<std::string>(prefix + "parent", "");
        if (childName.empty() || parentName.empty())
        {
            continue;
        }

        Link link;
        link.parentName = parentName;
        link.localPosition = dataHandler->Load<Vector3>(prefix + "localPosition", {});
        link.localRotation = dataHandler->Load<Quaternion>(prefix + "localRotation", Quaternion::IdentityQuaternion());
        link.localDirection = dataHandler->Load<Vector3>(prefix + "localDirection", {0.0f, -1.0f, 0.0f});
        link.inheritTranslation = dataHandler->Load<bool>(prefix + "inheritTranslation", true);
        link.inheritRotation = dataHandler->Load<bool>(prefix + "inheritRotation", true);
        // 保存された相対値をそのまま使う（初回の解決で取り直させない）
        link.hasLocal = true;
        links_[childName] = link;
    }
}
} // namespace Hagine
