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
        // 楕円体は回転しただけでも形が変わる。基底に回転が焼き込まれているので、
        // ここを見ておかないと回してもメッシュが作り直されない
        HashInt(hash, e.isEllipsoid ? 1u : 0u);
        if (e.isEllipsoid)
        {
            for (const Vector3 &axis : {e.unitAxisX, e.unitAxisY, e.unitAxisZ})
            {
                HashFloat(hash, axis.x);
                HashFloat(hash, axis.y);
                HashFloat(hash, axis.z);
            }
        }
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
    // ---- 空になったグループを片付ける ------------------------------------
    // グループは既定でオブジェクト 1 個につき 1 つ作られるので、
    // 消したり名前を変えたりするたびに、誰も居ないグループと
    // その動的メッシュ（GPUバッファ）が残っていく。
    //
    // 消すのは Register / Unregister の中ではなく必ずここ。
    // GetSettings() が返す参照を UI が掴んでいる最中に erase すると、
    // その参照が宙に浮いてしまう（UI の描画中には呼ばれない Update で掃除する）
    for (auto it = groups_.begin(); it != groups_.end();)
    {
        if (!it->second.members.empty())
        {
            ++it;
            continue;
        }
        if (it->second.obj3d && !it->second.obj3d->GetDynamicModelKey().empty())
        {
            ModelManager::GetInstance()->RemoveModel(it->second.obj3d->GetDynamicModelKey());
        }
        it = groups_.erase(it);
    }

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
        // ブレンドモードは Draw のたびに Object3d へ入れ直す必要がある
        // （BaseObject::Update が毎フレーム SetBlendMode しているのと同じ理由）
        group.obj3d->SetBlendMode(group.settings.blendMode);
        // メッシュはワールド空間で作ってあるので、変換は単位行列のまま
        group.obj3d->Draw(*group.transform, viewProjection, false, group.settings.lighting, true);
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

void MetaBallGroupManager::ApplyMaterial(const std::string &groupName)
{
    auto it = groups_.find(groupName);
    if (it == groups_.end() || !it->second.obj3d)
    {
        return;
    }
    const MetaBallGroupSettings &settings = it->second.settings;
    if (!settings.texturePath.empty())
    {
        it->second.obj3d->SetTexture(settings.texturePath, 0);
    }
    it->second.obj3d->SetColor(settings.color, 0);
    it->second.obj3d->SetBlendMode(settings.blendMode);
}

} // namespace Hagine
