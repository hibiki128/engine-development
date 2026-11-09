#include "ObjectGroupManager.h"
#include <algorithm>
#include <asset/AssetPath.h>
#include <debug/log/Logger.h>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <object/base/BaseObject.h>
#include <object/base/BaseObjectManager.h>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <utility/debug/imgui/ImGuiNotification.h>

namespace Hagine {
namespace {
namespace fs = std::filesystem;
using json = nlohmann::json;

constexpr const char *kFormatName = "HagineObjectGroup";
constexpr int kVersion = 1;
constexpr const char *kFolder = "ObjectGroups";

/// <summary>
/// UTF-8 の文字列からパスを作る
/// </summary>
fs::path ToPath(const std::string &utf8)
{
    return fs::path(std::u8string(utf8.begin(), utf8.end()));
}

/// <summary>
/// パスを UTF-8 の文字列にする
/// </summary>
std::string ToUtf8(const fs::path &path)
{
    const std::u8string text = path.u8string();
    return std::string(text.begin(), text.end());
}

/// <summary>
/// 前後の空白を落とす
/// </summary>
std::string Trim(const std::string &text)
{
    const size_t first = text.find_first_not_of(" \t");
    if (first == std::string::npos)
    {
        return std::string();
    }
    const size_t last = text.find_last_not_of(" \t");
    return text.substr(first, last - first + 1);
}

/// <summary>
/// ファイル名に使えない文字を _ に置き換える
/// </summary>
std::string SanitizeFileName(const std::string &name)
{
    std::string result = Trim(name);
    for (char &c : result)
    {
        const bool forbidden = c == '\\' || c == '/' || c == ':' || c == '*' || c == '?' || c == '"' ||
                               c == '<' || c == '>' || c == '|' || static_cast<unsigned char>(c) < 0x20;
        if (forbidden)
        {
            c = '_';
        }
    }
    return result;
}

/// <summary>
/// コライダー名の先頭のオブジェクト名を付け替える
/// </summary>
void RenameColliders(json &state, const std::string &oldName, const std::string &newName)
{
    if (!state.contains("colliders") || !state["colliders"].is_array())
    {
        return;
    }
    const std::string oldPrefix = oldName + "_";
    for (json &entry : state["colliders"])
    {
        const std::string colliderName = entry.value("name", std::string());
        if (colliderName.rfind(oldPrefix, 0) == 0)
        {
            entry["name"] = newName + "_" + colliderName.substr(oldPrefix.size());
        }
    }
}

/// <summary>
/// エディタで置いたメンバーと子孫を、親が子より先に来る順で集める
/// </summary>
std::vector<BaseObject *> CollectOwnedSubtrees(const std::vector<BaseObject *> &roots)
{
    BaseObjectManager *manager = BaseObjectManager::GetInstance();
    std::unordered_set<BaseObject *> included;
    std::vector<BaseObject *> stack(roots.begin(), roots.end());
    while (!stack.empty())
    {
        BaseObject *obj = stack.back();
        stack.pop_back();
        if (!obj || !manager->IsOwned(obj) || !included.insert(obj).second)
        {
            continue;
        }
        for (BaseObject *child : *obj->GetChildren())
        {
            stack.push_back(child);
        }
    }

    std::vector<BaseObject *> order;
    for (BaseObject *obj : included)
    {
        if (!included.contains(obj->GetParent()))
        {
            order.push_back(obj);
        }
    }
    std::sort(order.begin(), order.end(), [](BaseObject *a, BaseObject *b) { return a->GetName() < b->GetName(); });
    for (size_t i = 0; i < order.size(); ++i)
    {
        for (BaseObject *child : *order[i]->GetChildren())
        {
            if (included.contains(child))
            {
                order.push_back(child);
            }
        }
    }
    return order;
}

/// <summary>
/// グループ1件を JSON にする
/// </summary>
json GroupToJson(const ObjectGroup &group, bool withSaveFlag)
{
    json entry = json::object();
    entry["name"] = group.GetName();
    entry["members"] = group.GetMemberNames();
    if (withSaveFlag)
    {
        entry["save"] = group.GetShouldSave();
    }
    return entry;
}
} // namespace

ObjectGroupManager *ObjectGroupManager::GetInstance()
{
    static ObjectGroupManager instance;
    return &instance;
}

// -------------------------------------------------------
// グループの生成・取得
// -------------------------------------------------------

ObjectGroup *ObjectGroupManager::Create(const std::string &name)
{
    const std::string groupName = Trim(name);
    if (groupName.empty())
    {
        return nullptr;
    }
    auto [it, inserted] = groups_.try_emplace(groupName, nullptr);
    if (inserted)
    {
        it->second = std::make_unique<ObjectGroup>(groupName);
    }
    return it->second.get();
}

ObjectGroup *ObjectGroupManager::Find(const std::string &name)
{
    const auto it = groups_.find(name);
    return it != groups_.end() ? it->second.get() : nullptr;
}

bool ObjectGroupManager::Remove(const std::string &name)
{
    return groups_.erase(name) > 0;
}

int ObjectGroupManager::Destroy(const std::string &name)
{
    ObjectGroup *group = Find(name);
    if (!group)
    {
        return 0;
    }

    // 子から順に消す
    std::vector<std::string> targets;
    for (BaseObject *obj : CollectOwnedSubtrees(group->GetObjects()))
    {
        targets.push_back(obj->GetName());
    }
    BaseObjectManager *manager = BaseObjectManager::GetInstance();
    for (auto it = targets.rbegin(); it != targets.rend(); ++it)
    {
        manager->RemoveObject(*it);
    }
    Remove(name);
    return static_cast<int>(targets.size());
}

std::string ObjectGroupManager::Rename(const std::string &oldName, const std::string &desiredName)
{
    const auto it = groups_.find(oldName);
    const std::string baseName = Trim(desiredName);
    if (it == groups_.end() || baseName.empty())
    {
        return std::string();
    }
    if (baseName == oldName)
    {
        return oldName;
    }
    const std::string newName = MakeUniqueName(baseName);
    auto node = groups_.extract(it);
    node.key() = newName;
    node.mapped()->name_ = newName;
    groups_.insert(std::move(node));
    return newName;
}

std::vector<std::string> ObjectGroupManager::GetGroupNames() const
{
    std::vector<std::string> names;
    names.reserve(groups_.size());
    for (const auto &[name, group] : groups_)
    {
        names.push_back(name);
    }
    return names;
}

std::vector<ObjectGroup *> ObjectGroupManager::FindGroupsOf(const std::string &objectName)
{
    std::vector<ObjectGroup *> result;
    for (auto &[name, group] : groups_)
    {
        if (group->Contains(objectName))
        {
            result.push_back(group.get());
        }
    }
    return result;
}

std::string ObjectGroupManager::MakeUniqueName(const std::string &baseName) const
{
    if (!groups_.contains(baseName))
    {
        return baseName;
    }
    for (int index = 0;; ++index)
    {
        const std::string candidate = baseName + "_" + std::to_string(index);
        if (!groups_.contains(candidate))
        {
            return candidate;
        }
    }
}

void ObjectGroupManager::OnObjectRenamed(const std::string &oldName, const std::string &newName)
{
    for (auto &[name, group] : groups_)
    {
        for (std::string &member : group->members_)
        {
            if (member == oldName)
            {
                member = newName;
            }
        }
    }
}

// -------------------------------------------------------
// シーンファイル・Undo
// -------------------------------------------------------

json ObjectGroupManager::ToJson() const
{
    json list = json::array();
    for (const auto &[name, group] : groups_)
    {
        if (group->GetShouldSave())
        {
            list.push_back(GroupToJson(*group, false));
        }
    }
    return list;
}

json ObjectGroupManager::CaptureState() const
{
    json list = json::array();
    for (const auto &[name, group] : groups_)
    {
        list.push_back(GroupToJson(*group, true));
    }
    return list;
}

void ObjectGroupManager::FromJson(const json &state)
{
    std::set<std::string> loaded;
    if (state.is_array())
    {
        for (const json &entry : state)
        {
            if (!entry.is_object())
            {
                continue;
            }
            const std::string name = Trim(entry.value("name", std::string()));
            if (name.empty() || loaded.contains(name))
            {
                continue;
            }
            ObjectGroup *group = Create(name);
            group->members_.clear();
            if (const auto members = entry.find("members"); members != entry.end() && members->is_array())
            {
                for (const json &member : *members)
                {
                    if (member.is_string())
                    {
                        group->Add(member.get<std::string>());
                    }
                }
            }
            group->shouldSave_ = entry.value("save", true);
            loaded.insert(name);
        }
    }

    for (auto it = groups_.begin(); it != groups_.end();)
    {
        it = loaded.contains(it->first) ? std::next(it) : groups_.erase(it);
    }
}

// -------------------------------------------------------
// グループファイル
// -------------------------------------------------------

std::string ObjectGroupManager::GroupFilePath(const std::string &fileName)
{
    return AssetPath::Json(std::string(kFolder) + "/" + SanitizeFileName(fileName) + ".json");
}

bool ObjectGroupManager::SaveToFile(const std::string &groupName, const std::string &fileName)
{
    ObjectGroup *group = Find(groupName);
    if (!group)
    {
        return false;
    }
    const std::string targetName = SanitizeFileName(fileName.empty() ? groupName : fileName);
    if (targetName.empty())
    {
        return false;
    }

    BaseObjectManager *manager = BaseObjectManager::GetInstance();
    std::vector<BaseObject *> owned;
    for (BaseObject *obj : group->GetObjects())
    {
        if (manager->IsOwned(obj))
        {
            owned.push_back(obj);
        }
    }
    json objects = json::array();
    for (BaseObject *obj : CollectOwnedSubtrees(owned))
    {
        objects.push_back(obj->Serialize());
    }

    json document = json::object();
    document["format"] = kFormatName;
    document["version"] = kVersion;
    document["name"] = groupName;
    document["members"] = group->GetMemberNames();
    document["objects"] = std::move(objects);

    std::string text;
    try
    {
        text = document.dump(4);
    }
    catch (const json::exception &e)
    {
        Logger::Error(std::string("[ObjectGroup] JSON にできませんでした: ") + e.what());
        return false;
    }

    const fs::path path = ToPath(GroupFilePath(targetName));
    std::error_code error;
    fs::create_directories(path.parent_path(), error);
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file)
    {
        Logger::Error("[ObjectGroup] 保存できませんでした: " + ToUtf8(path));
        return false;
    }
    file.write(text.data(), static_cast<std::streamsize>(text.size()));
#ifdef USE_IMGUI
    fileListDirty_ = true;
#endif // USE_IMGUI
    return !file.fail();
}

ObjectGroup *ObjectGroupManager::Spawn(const std::string &fileName, const Vector3 &offset, bool saveInScene)
{
    std::ifstream file(ToPath(GroupFilePath(fileName)), std::ios::binary);
    if (!file)
    {
        Logger::Warn("[ObjectGroup] グループファイルがありません: " + fileName);
        return nullptr;
    }
    const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    const json document = json::parse(text, nullptr, false, true);
    if (document.is_discarded() || !document.is_object() || !document.contains("objects") || !document["objects"].is_array())
    {
        Logger::Error("[ObjectGroup] グループファイルの形式が正しくありません: " + fileName);
        return nullptr;
    }

    ObjectGroup *group = Create(MakeUniqueName(document.value("name", fileName)));
    if (!group)
    {
        return nullptr;
    }
    group->SetShouldSave(saveInScene);

    ImGuiNotification::ScopedMute mute;
    BaseObjectManager *manager = BaseObjectManager::GetInstance();
    // ファイル上の名前 → 置いた名前
    std::unordered_map<std::string, std::string> renamed;

    for (const json &saved : document["objects"])
    {
        const std::string savedName = saved.value("name", std::string());
        if (savedName.empty())
        {
            continue;
        }
        BaseObject *obj = manager->CreateObjectFromState(savedName, saved);
        if (!obj)
        {
            Logger::Warn("[ObjectGroup] \"" + savedName + "\" を作れませんでした");
            continue;
        }
        const std::string newName = obj->GetName();
        renamed[savedName] = newName;

        json state = saved;
        if (newName != savedName)
        {
            RenameColliders(state, savedName, newName);
        }
        try
        {
            obj->Deserialize(state);
        }
        catch (const json::exception &e)
        {
            Logger::Warn("[ObjectGroup] \"" + savedName + "\" の設定の一部を読めませんでした: " + e.what());
        }
        obj->SetShouldSave(saveInScene);

        // 親はファイル内で置いた物、無ければシーンにいる同名の物
        const std::string parentName = state.value("parent", std::string());
        BaseObject *parent = nullptr;
        if (!parentName.empty())
        {
            const auto it = renamed.find(parentName);
            parent = manager->GetObjectByName(it != renamed.end() ? it->second : parentName);
        }
        if (parent && parent != obj)
        {
            obj->SetParent(parent);
        }
        else
        {
            obj->GetLocalPosition() += offset;
        }
        obj->GetWorldTransform()->UpdateMatrix();
    }

    if (const auto members = document.find("members"); members != document.end() && members->is_array())
    {
        for (const json &member : *members)
        {
            if (!member.is_string())
            {
                continue;
            }
            const std::string name = member.get<std::string>();
            const auto it = renamed.find(name);
            group->Add(it != renamed.end() ? it->second : name);
        }
    }
    return group;
}

std::vector<std::string> ObjectGroupManager::ListGroupFiles() const
{
    std::vector<std::string> names;
    std::error_code error;
    const fs::path folder = ToPath(AssetPath::Json(kFolder));
    if (!fs::is_directory(folder, error))
    {
        return names;
    }
    for (const fs::directory_entry &entry : fs::directory_iterator(folder, error))
    {
        if (entry.is_regular_file() && entry.path().extension() == ".json")
        {
            names.push_back(ToUtf8(entry.path().stem()));
        }
    }
    std::sort(names.begin(), names.end());
    return names;
}

bool ObjectGroupManager::DeleteGroupFile(const std::string &fileName)
{
    std::error_code error;
    const bool removed = fs::remove(ToPath(GroupFilePath(fileName)), error);
#ifdef USE_IMGUI
    fileListDirty_ = true;
#endif // USE_IMGUI
    return removed;
}
} // namespace Hagine
