#define NOMINMAX
#include "MetaBallGroupManager.h"
#include "MetaBallObject.h"
#include "graphics/model/ModelManager.h"
#include <algorithm>
#include <cstring>

namespace Hagine {
namespace {

/// <summary>float のビット列をそのまま混ぜてハッシュに足す</summary>
inline void HashFloat(size_t &hash, float value)
{
    uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    // boost::hash_combine と同じ混ぜ方
    hash ^= static_cast<size_t>(bits) + 0x9e3779b97f4a7c15ull + (hash << 6) + (hash >> 2);
}

inline void HashInt(size_t &hash, uint32_t value)
{
    hash ^= static_cast<size_t>(value) + 0x9e3779b97f4a7c15ull + (hash << 6) + (hash >> 2);
}

/// <summary>要素列の状態をハッシュにする。前フレームと同じなら作り直さない</summary>
size_t HashElements(const std::vector<MetaBallElement> &elements, const MetaBallGroupSettings &settings)
{
    size_t hash = 1469598103934665603ull;
    HashFloat(hash, settings.voxelSize);
    HashFloat(hash, settings.threshold);
    HashFloat(hash, settings.uvScale);
    HashInt(hash, static_cast<uint32_t>(elements.size()));
    for (const MetaBallElement &e : elements)
    {
        HashFloat(hash, e.position.x);
        HashFloat(hash, e.position.y);
        HashFloat(hash, e.position.z);
        HashFloat(hash, e.radius);
        HashFloat(hash, e.stiffness);
        HashFloat(hash, e.axis.x);
        HashFloat(hash, e.axis.y);
        HashFloat(hash, e.axis.z);
        HashInt(hash, static_cast<uint32_t>(e.shape));
        HashInt(hash, e.negative ? 1u : 0u);
        HashInt(hash, e.enabled ? 1u : 0u);
    }
    return hash;
}

} // namespace

void MetaBallGroupManager::Finalize()
{
    for (auto &[name, group] : groups_)
    {
        if (group.obj3d && !group.obj3d->GetDynamicModelKey().empty())
        {
            ModelManager::GetInstance()->RemoveModel(group.obj3d->GetDynamicModelKey());
        }
    }
    groups_.clear();
    scratch_.clear();
    scratch_.shrink_to_fit();
}

MetaBallGroupManager::Group &MetaBallGroupManager::GetOrCreateGroup(const std::string &groupName)
{
    auto it = groups_.find(groupName);
    if (it != groups_.end())
    {
        return it->second;
    }

    Group group{};
    group.obj3d = std::make_unique<Object3d>();
    group.obj3d->Initialize();
    group.obj3d->CreateDynamicModel(group.settings.texturePath);
    group.transform = std::make_unique<WorldTransform>();
    group.transform->Initialize();
    group.transform->UpdateMatrix();

    return groups_.emplace(groupName, std::move(group)).first->second;
}

void MetaBallGroupManager::Register(MetaBallObject *object)
{
    if (!object)
    {
        return;
    }
    Group &group = GetOrCreateGroup(object->GetGroupName());
    if (std::find(group.members.begin(), group.members.end(), object) == group.members.end())
    {
        group.members.push_back(object);
        group.forceRebuild = true;
    }
}

void MetaBallGroupManager::Unregister(MetaBallObject *object)
{
    if (!object)
    {
        return;
    }
    // グループ名が変わっている可能性があるので、全グループから外す
    for (auto &[name, group] : groups_)
    {
        auto it = std::find(group.members.begin(), group.members.end(), object);
        if (it != group.members.end())
        {
            group.members.erase(it);
            group.forceRebuild = true;
        }
    }
}

void MetaBallGroupManager::Rebind(MetaBallObject *object, const std::string &oldGroupName)
{
    if (!object)
    {
        return;
    }
    auto it = groups_.find(oldGroupName);
    if (it != groups_.end())
    {
        auto member = std::find(it->second.members.begin(), it->second.members.end(), object);
        if (member != it->second.members.end())
        {
            it->second.members.erase(member);
            it->second.forceRebuild = true;
        }
    }
    Register(object);
}

void MetaBallGroupManager::Update()
{
    for (auto &[name, group] : groups_)
    {
        // ---- 全メンバーの要素をワールド空間で集める ----------------------
        scratch_.clear();
        for (MetaBallObject *object : group.members)
        {
            if (object)
            {
                object->AppendWorldElements(scratch_);
            }
        }

        // ---- 変化していなければ作り直さない ------------------------------
        const size_t hash = HashElements(scratch_, group.settings);
        if (!group.forceRebuild && hash == group.lastHash)
        {
            continue;
        }
        group.lastHash = hash;
        group.forceRebuild = false;

        MetaBallWorldParams params{};
        params.voxelSize = group.settings.voxelSize;
        params.threshold = group.settings.threshold;
        params.uvScale = group.settings.uvScale;

        MeshData data = MetaBallBuilder::BuildClustered(scratch_, params, &group.stats);
        group.hasMesh = !data.indices.empty();
        group.obj3d->RebuildDynamicMesh(std::move(data));
    }
}

void MetaBallGroupManager::Draw(const ViewProjection &viewProjection)
{
    for (auto &[name, group] : groups_)
    {
        if (!group.settings.enabled || !group.hasMesh || !group.obj3d)
        {
            continue;
        }
        // メッシュはワールド空間で作ってあるので、変換は単位行列のまま
        group.obj3d->Draw(*group.transform, viewProjection, false, true, true);
    }
}

MetaBallGroupSettings &MetaBallGroupManager::GetSettings(const std::string &groupName)
{
    return GetOrCreateGroup(groupName).settings;
}

const MetaBallBuildStats &MetaBallGroupManager::GetStats(const std::string &groupName)
{
    auto it = groups_.find(groupName);
    if (it == groups_.end())
    {
        return emptyStats_;
    }
    return it->second.stats;
}

size_t MetaBallGroupManager::GetMemberCount(const std::string &groupName)
{
    auto it = groups_.find(groupName);
    if (it == groups_.end())
    {
        return 0;
    }
    return it->second.members.size();
}

void MetaBallGroupManager::MarkDirty(const std::string &groupName)
{
    auto it = groups_.find(groupName);
    if (it != groups_.end())
    {
        it->second.forceRebuild = true;
    }
}

} // namespace Hagine
