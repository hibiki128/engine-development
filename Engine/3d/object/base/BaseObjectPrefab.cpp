#include "BaseObjectManager.h"
#ifdef USE_IMGUI
#include "debug/imgui/ImGuizmoManager.h"
#include <asset/AssetPath.h>
#include <debug/log/Logger.h>
#include <filesystem>
#include <fstream>
#include <utility/debug/imgui/ImGuiNotification.h>

// プレハブ（よく使う配置を子孫ごとテンプレートとして保存し、別の場所へ置き直す仕組み）。
// 中身は Undo と同じ「1体ぶんの状態JSON」を並べただけなので、Undo で戻せる項目はプレハブにも残る。
namespace Hagine {
namespace {
constexpr int kPrefabVersion = 1;

/// <summary>ファイル名に使えない文字を _ に置き換える</summary>
std::string SanitizeFileName(const std::string &name)
{
    std::string result = name;
    for (char &c : result)
    {
        if (c == '\\' || c == '/' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|')
        {
            c = '_';
        }
    }
    return result;
}

/// <summary>根から子孫へ幅優先で並べる（親が必ず子より先に来るので、読み込み時に順に付けられる）</summary>
std::vector<BaseObject *> CollectSubtree(BaseObject *root)
{
    std::vector<BaseObject *> order;
    if (!root)
    {
        return order;
    }
    order.push_back(root);
    for (size_t i = 0; i < order.size(); ++i)
    {
        for (BaseObject *child : *order[i]->GetChildren())
        {
            if (child)
            {
                order.push_back(child);
            }
        }
    }
    return order;
}

/// <summary>
/// コライダー名の先頭についている元のオブジェクト名を、新しい名前へ付け替える。
/// コライダーは「オブジェクト名_種類Collider_番号」で保存されるので、
/// 付け替えないと置いた物同士で保存先が衝突する
/// </summary>
void RenameColliders(nlohmann::json &state, const std::string &oldName, const std::string &newName)
{
    if (!state.contains("colliders") || !state["colliders"].is_array())
    {
        return;
    }
    const std::string oldPrefix = oldName + "_";
    for (nlohmann::json &entry : state["colliders"])
    {
        std::string colliderName = entry.value("name", std::string());
        if (colliderName.rfind(oldPrefix, 0) == 0)
        {
            entry["name"] = newName + "_" + colliderName.substr(oldPrefix.size());
        }
    }
}
} // namespace

std::string BaseObjectManager::PrefabFilePath(const std::string &prefabName)
{
    return AssetPath::Json("Prefab/" + SanitizeFileName(prefabName) + ".json");
}

bool BaseObjectManager::SavePrefab(const std::string &rootName, const std::string &prefabName)
{
    BaseObject *root = GetObjectByName(rootName);
    if (!root)
    {
        return false;
    }
    const std::string fileName = prefabName.empty() ? rootName : prefabName;

    nlohmann::json objects = nlohmann::json::array();
    for (BaseObject *obj : CollectSubtree(root))
    {
        nlohmann::json state = CaptureObjectState(obj);
        // プレハブの中身にはリンクを持たせない（置いた側が付ける）
        state.erase("prefabSource");
        if (obj == root)
        {
            // 根は置き場所を後から決めるので、位置と親は持たない（回転・拡縮は残す）
            state["translation"] = Vector3{0.0f, 0.0f, 0.0f};
            state["parent"] = "";
        }
        nlohmann::json entry;
        entry["name"] = obj->GetName();
        entry["state"] = std::move(state);
        objects.push_back(std::move(entry));
    }

    nlohmann::json prefab;
    prefab["version"] = kPrefabVersion;
    prefab["root"] = rootName;
    prefab["objects"] = std::move(objects);

    const std::string path = PrefabFilePath(fileName);
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
    std::ofstream file(path);
    if (!file)
    {
        Logger::Error("プレハブを保存できませんでした: " + path);
        return false;
    }
    file << prefab.dump(4);
    return true;
}

std::string BaseObjectManager::InstantiatePrefab(const std::string &prefabName, const Vector3 &position)
{
    std::ifstream file(PrefabFilePath(prefabName));
    if (!file)
    {
        return {};
    }
    nlohmann::json prefab = nlohmann::json::parse(file, nullptr, false);
    if (prefab.is_discarded() || !prefab.contains("objects") || !prefab["objects"].is_array())
    {
        Logger::Error("プレハブの形式が正しくありません: " + prefabName);
        return {};
    }

    // 保存時の名前 → 今回付けた名前。子の親を引き直すのに使う
    std::unordered_map<std::string, std::string> renamed;
    std::string rootInstanceName;

    for (const nlohmann::json &entry : prefab["objects"])
    {
        const std::string originalName = entry.value("name", std::string());
        if (originalName.empty() || !entry.contains("state"))
        {
            continue;
        }
        nlohmann::json state = entry["state"];
        const std::string newName = MakeUniqueObjectName(originalName);

        BaseObject *obj = CreateObjectFromState(newName, state);
        if (!obj)
        {
            continue;
        }
        RenameColliders(state, originalName, newName);

        const bool isRoot = rootInstanceName.empty();
        if (isRoot)
        {
            state["translation"] = position;
            rootInstanceName = newName;
        }
        // 根だけがプレハブを指す（子は根と一緒に扱う）
        state["prefabSource"] = isRoot ? prefabName : std::string();
        ApplyObjectState(obj, state);
        renamed[originalName] = newName;

        // 親は先に作られている（幅優先で保存してある）ので、その場で付けられる
        const std::string originalParent = state.value("parent", std::string());
        if (!isRoot && !originalParent.empty())
        {
            auto it = renamed.find(originalParent);
            if (it != renamed.end())
            {
                SetParentChild(newName, it->second);
            }
        }
        obj->GetWorldTransform()->UpdateMatrix();
    }
    return rootInstanceName;
}

bool BaseObjectManager::ApplyInstanceToPrefab(const std::string &instanceName)
{
    BaseObject *root = GetObjectByName(instanceName);
    if (!root || root->GetPrefabSource().empty())
    {
        return false;
    }
    return SavePrefab(instanceName, root->GetPrefabSource());
}

std::string BaseObjectManager::RevertInstanceToPrefab(const std::string &instanceName)
{
    BaseObject *root = GetObjectByName(instanceName);
    if (!root || root->GetPrefabSource().empty())
    {
        return {};
    }
    const std::string prefabName = root->GetPrefabSource();
    if (!std::filesystem::exists(PrefabFilePath(prefabName)))
    {
        return {};
    }
    // 置き直しても場所と親は変えない
    const Vector3 position = root->GetWorldTransform()->translation_;
    const std::string parentName = root->GetParentName();

    // 子から順に消す（親を先に消すと子の親ポインタがぶら下がる）
    std::vector<BaseObject *> subtree = CollectSubtree(root);
    for (auto it = subtree.rbegin(); it != subtree.rend(); ++it)
    {
        const std::string name = (*it)->GetName();
        RemoveObject(name);
        ImGuizmoManager::GetInstance()->RemoveTarget(name);
    }

    const std::string newRoot = InstantiatePrefab(prefabName, position);
    if (!newRoot.empty() && !parentName.empty() && GetObjectByName(parentName))
    {
        SetParentChild(newRoot, parentName);
    }
    return newRoot;
}

int BaseObjectManager::CountPrefabInstances(const std::string &prefabName) const
{
    int count = 0;
    for (const auto &[name, obj] : objects_)
    {
        if (obj && obj->GetPrefabSource() == prefabName)
        {
            ++count;
        }
    }
    return count;
}

std::vector<std::string> BaseObjectManager::ListPrefabNames() const
{
    std::vector<std::string> names;
    const std::filesystem::path directory = std::filesystem::path(PrefabFilePath("_")).parent_path();
    std::error_code ec;
    if (!std::filesystem::exists(directory, ec))
    {
        return names;
    }
    for (const auto &entry : std::filesystem::directory_iterator(directory, ec))
    {
        if (entry.is_regular_file() && entry.path().extension() == ".json")
        {
            names.push_back(entry.path().stem().string());
        }
    }
    std::sort(names.begin(), names.end());
    return names;
}

bool BaseObjectManager::DeletePrefab(const std::string &prefabName)
{
    std::error_code ec;
    return std::filesystem::remove(PrefabFilePath(prefabName), ec);
}

BaseObjectManager::PrefabInfo BaseObjectManager::PeekPrefab(const std::string &prefabName) const
{
    PrefabInfo info;
    std::ifstream file(PrefabFilePath(prefabName));
    if (!file)
    {
        return info;
    }
    const nlohmann::json prefab = nlohmann::json::parse(file, nullptr, false);
    if (prefab.is_discarded() || !prefab.contains("objects") || !prefab["objects"].is_array())
    {
        return info;
    }
    info.objectCount = static_cast<int>(prefab["objects"].size());
    if (!prefab["objects"].empty())
    {
        const nlohmann::json &rootState = prefab["objects"][0].value("state", nlohmann::json::object());
        info.rootModel = rootState.value("modelPath", std::string());
        if (info.rootModel.empty() && rootState.value("isPrimitive", false))
        {
            info.rootModel = "(プリミティブ)";
        }
    }
    return info;
}
} // namespace Hagine
#endif // USE_IMGUI
