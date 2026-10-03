#define NOMINMAX
#include "SpringBoneSolver.h"
#include <MyMath.h>
#include <algorithm>
#include <cmath>
#include <line/LineRenderer.h>

namespace Hagine {

namespace {
constexpr float kMaxStep = 1.0f / 30.0f;   // 1回に進める時間の上限（止まった後の大きな dt で暴れないように）
constexpr float kForceScale = 10.0f;      // 戻る力・重力の単位（骨の長さ × この値 × 強さ / 秒）
constexpr float kResetStretch = 4.0f;     // 先端が骨の長さのこの倍より離れたら、瞬間移動とみなして置き直す
constexpr float kMinAngle = 1.0e-5f;      // これより小さい回転は掛けない

// 自動検出で揺れ物とみなす名前の部品（小文字化した名前に含まれるか）
constexpr const char *kSpringTokens[] = {"hair", "skirt", "tail", "cape", "cloak", "ribbon", "scarf", "coat", "sleeve", "spring"};

/// <summary>軸まわりの回転行列（行ベクトル規約: v * M で回す）。ロドリゲスの回転公式</summary>
Matrix4x4 MakeAxisRotation(const Vector3 &axis, float angle)
{
    const float c = std::cos(angle);
    const float s = std::sin(angle);
    const float t = 1.0f - c;
    const float x = axis.x;
    const float y = axis.y;
    const float z = axis.z;
    Matrix4x4 m = MakeIdentity4x4();
    m.m[0][0] = c + t * x * x;
    m.m[0][1] = t * x * y + s * z;
    m.m[0][2] = t * x * z - s * y;
    m.m[1][0] = t * x * y - s * z;
    m.m[1][1] = c + t * y * y;
    m.m[1][2] = t * y * z + s * x;
    m.m[2][0] = t * x * z + s * y;
    m.m[2][1] = t * y * z - s * x;
    m.m[2][2] = c + t * z * z;
    return m;
}

Vector3 GetTranslation(const Matrix4x4 &m)
{
    return {m.m[3][0], m.m[3][1], m.m[3][2]};
}

/// <summary>ジョイントとその子孫すべての行列に、後ろから変換を掛ける</summary>
void ApplyToSubtree(Skeleton &skeleton, int32_t root, const Matrix4x4 &transform)
{
    std::vector<int32_t> stack{root};
    while (!stack.empty())
    {
        const int32_t index = stack.back();
        stack.pop_back();
        Joint &joint = skeleton.joints[index];
        joint.skeletonSpaceMatrix = joint.skeletonSpaceMatrix * transform;
        for (int32_t child : joint.children)
        {
            stack.push_back(child);
        }
    }
}

std::string ToLower(const std::string &name)
{
    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return lower;
}

bool IsSpringName(const std::string &name)
{
    const std::string lower = ToLower(name);
    for (const char *token : kSpringTokens)
    {
        if (lower.find(token) != std::string::npos)
        {
            return true;
        }
    }
    return false;
}

/// <summary>ベクトルを長さ1にする。短すぎるときは fallback を返す</summary>
Vector3 SafeNormalize(const Vector3 &v, const Vector3 &fallback)
{
    const float length = v.Length();
    return length > 1.0e-6f ? v / length : fallback;
}
} // namespace

void SpringBoneSolver::BuildChainState(const Skeleton &skeleton, int32_t rootIndex, ChainState &outState)
{
    outState.tails.clear();
    // 深さ優先で、親を先に積む（親の回転を反映した位置から子を解くため）
    std::vector<int32_t> stack{rootIndex};
    while (!stack.empty())
    {
        const int32_t index = stack.back();
        stack.pop_back();
        const Joint &joint = skeleton.joints[index];
        TailState tail;
        tail.joint = index;
        tail.tailJoint = joint.children.empty() ? -1 : joint.children.front();
        outState.tails.push_back(tail);
        // 子を逆順に積むと、取り出す順が元の並びになる
        for (auto it = joint.children.rbegin(); it != joint.children.rend(); ++it)
        {
            stack.push_back(*it);
        }
    }
}

bool SpringBoneSolver::Solve(Skeleton &skeleton, const Matrix4x4 &worldMatrix, float deltaTime)
{
    simulatedJointCount_ = 0;
    if (!settings_.enabled || settings_.chains.empty() || skeleton.joints.empty())
    {
        return false;
    }
    const float weight = std::clamp(settings_.weight, 0.0f, 1.0f);
    const float step = std::clamp(deltaTime, 0.0f, kMaxStep);

    const Matrix4x4 inverseWorld = Inverse(worldMatrix);
    // モデル空間の長さ（当たりの半径など）をワールドへ直す倍率
    const float worldScale = (TransformNormal({1.0f, 0.0f, 0.0f}, worldMatrix).Length() +
                              TransformNormal({0.0f, 1.0f, 0.0f}, worldMatrix).Length() +
                              TransformNormal({0.0f, 0.0f, 1.0f}, worldMatrix).Length()) /
                             3.0f;

    // ---- 当たり球（ワールド）。揺れ物で骨を回す前のポーズで作る ----
    struct WorldSphere
    {
        Vector3 center;
        float radius;
    };
    std::vector<WorldSphere> spheres;
    spheres.reserve(settings_.colliders.size());
    for (const SpringBoneCollider &collider : settings_.colliders)
    {
        auto it = skeleton.jointMap.find(collider.joint);
        if (it == skeleton.jointMap.end())
        {
            continue;
        }
        const Vector3 centerModel = Transformation(collider.offset, skeleton.joints[it->second].skeletonSpaceMatrix);
        spheres.push_back({Transformation(centerModel, worldMatrix), collider.radius * worldScale});
    }

    LineRenderer *pLine = settings_.drawDebug ? LineRenderer::GetInstance() : nullptr;
    if (pLine)
    {
        for (const WorldSphere &sphere : spheres)
        {
            pLine->AddSphere(sphere.center, sphere.radius, {0.4f, 0.8f, 1.0f, 1.0f}, 12);
        }
    }

    // まとまりの数・根元が変わったら状態を作り直す
    states_.resize(settings_.chains.size());

    bool changed = false;
    for (size_t chainIndex = 0; chainIndex < settings_.chains.size(); ++chainIndex)
    {
        const SpringBoneChain &chain = settings_.chains[chainIndex];
        ChainState &state = states_[chainIndex];
        auto rootIt = skeleton.jointMap.find(chain.rootJoint);
        if (!chain.enabled || rootIt == skeleton.jointMap.end())
        {
            state.tails.clear();
            state.rootJoint.clear();
            continue;
        }
        if (state.rootJoint != chain.rootJoint || state.tails.empty() || state.tails.front().joint != rootIt->second)
        {
            state.rootJoint = chain.rootJoint;
            BuildChainState(skeleton, rootIt->second, state);
        }

        const Vector3 gravityDirection = SafeNormalize(chain.gravityDirection, {0.0f, -1.0f, 0.0f});
        const float dragFactor = std::pow(std::clamp(1.0f - chain.drag, 0.0f, 1.0f), step * 60.0f);

        for (TailState &tail : state.tails)
        {
            if (tail.joint < 0 || tail.joint >= static_cast<int32_t>(skeleton.joints.size()))
            {
                continue;
            }
            const Joint &joint = skeleton.joints[tail.joint];
            const Vector3 jointModel = GetTranslation(joint.skeletonSpaceMatrix);

            // アニメーションが置きたい先端（親の揺れを反映した今のポーズでの位置）
            Vector3 restTailModel{};
            if (tail.tailJoint >= 0)
            {
                restTailModel = GetTranslation(skeleton.joints[tail.tailJoint].skeletonSpaceMatrix);
            }
            else if (joint.parent)
            {
                // 末端は子が無いので、親からの骨の向きを伸ばした仮の先端を使う
                const Vector3 parentModel = GetTranslation(skeleton.joints[*joint.parent].skeletonSpaceMatrix);
                restTailModel = jointModel + (jointModel - parentModel) * settings_.tipLengthRate;
            }
            else
            {
                continue;
            }

            const Vector3 jointWorld = Transformation(jointModel, worldMatrix);
            const Vector3 restTailWorld = Transformation(restTailModel, worldMatrix);
            const Vector3 restVector = restTailWorld - jointWorld;
            const float length = restVector.Length();
            if (length < 1.0e-6f)
            {
                continue;
            }
            const Vector3 restDirection = restVector / length;

            // 初回と、瞬間移動などで先端が置いていかれたときはアニメーションどおりに置き直す
            if (!tail.initialized || (tail.currentTail - jointWorld).Length() > length * kResetStretch)
            {
                tail.currentTail = restTailWorld;
                tail.previousTail = restTailWorld;
                tail.initialized = true;
            }

            Vector3 next = tail.currentTail;
            if (step > 0.0f)
            {
                // 慣性（減衰つき）＋元のポーズへ戻る力＋重力
                const Vector3 velocity = (tail.currentTail - tail.previousTail) * dragFactor;
                next = tail.currentTail + velocity + restDirection * (chain.stiffness * kForceScale * length * step) +
                       gravityDirection * (chain.gravityPower * kForceScale * length * step);
            }
            // 骨の長さは変えない
            next = jointWorld + SafeNormalize(next - jointWorld, restDirection) * length;

            // 体へめり込んだら球の表面へ押し出す
            const float hitRadius = chain.hitRadius * worldScale;
            for (const WorldSphere &sphere : spheres)
            {
                const Vector3 offset = next - sphere.center;
                const float minDistance = sphere.radius + hitRadius;
                if (offset.Length() < minDistance)
                {
                    next = sphere.center + SafeNormalize(offset, restDirection) * minDistance;
                    next = jointWorld + SafeNormalize(next - jointWorld, restDirection) * length;
                }
            }

            if (step > 0.0f)
            {
                tail.previousTail = tail.currentTail;
            }
            tail.currentTail = next;

            if (pLine)
            {
                pLine->AddLine(jointWorld, next, {1.0f, 0.75f, 0.3f, 1.0f});
            }

            // アニメーションの向き → 揺れた向き の回転を、モデル空間で骨に掛ける
            const Vector3 from = SafeNormalize(TransformNormal(restDirection, inverseWorld), {0.0f, 1.0f, 0.0f});
            const Vector3 to = SafeNormalize(TransformNormal(next - jointWorld, inverseWorld), from);
            Vector3 axis = from.Cross(to);
            const float axisLength = axis.Length();
            const float angle = std::acos(std::clamp(from.Dot(to), -1.0f, 1.0f)) * weight;
            if (axisLength < 1.0e-6f || angle < kMinAngle)
            {
                continue;
            }
            axis = axis / axisLength;
            const Matrix4x4 rotate = MakeTranslateMatrix(jointModel * -1.0f) * MakeAxisRotation(axis, angle) * MakeTranslateMatrix(jointModel);
            ApplyToSubtree(skeleton, tail.joint, rotate);
            ++simulatedJointCount_;
            changed = true;
        }
    }
    return changed;
}

size_t SpringBoneSolver::AutoDetect(const Skeleton &skeleton)
{
    settings_.chains.clear();
    settings_.colliders.clear();

    // 揺れ物らしい名前で、親は揺れ物らしくないジョイントを「まとまりの根元」とする
    for (const Joint &joint : skeleton.joints)
    {
        if (!IsSpringName(joint.name))
        {
            continue;
        }
        if (joint.parent && IsSpringName(skeleton.joints[*joint.parent].name))
        {
            continue;
        }
        SpringBoneChain chain;
        chain.rootJoint = joint.name;
        // 当たりの太さは骨の長さに合わせる（モデルの単位が m でも cm でも同じ見た目になるように）
        if (!joint.children.empty())
        {
            const float boneLength = (GetTranslation(skeleton.joints[joint.children.front()].skeletonSpaceMatrix) -
                                      GetTranslation(joint.skeletonSpaceMatrix))
                                         .Length();
            chain.hitRadius = boneLength * 0.2f;
        }
        settings_.chains.push_back(chain);
    }

    // 頭に当たり球を1つ（頭の先のジョイントがあれば、その中間に頭の大きさで置く）
    for (const Joint &joint : skeleton.joints)
    {
        const std::string lower = ToLower(joint.name);
        if (lower.size() < 4 || lower.compare(lower.size() - 4, 4, "head") != 0)
        {
            continue;
        }
        SpringBoneCollider collider;
        collider.joint = joint.name;
        if (!joint.children.empty())
        {
            const Vector3 toTop = skeleton.joints[joint.children.front()].transform.translate;
            collider.offset = toTop * 0.5f;
            const float headLength = (GetTranslation(skeleton.joints[joint.children.front()].skeletonSpaceMatrix) -
                                      GetTranslation(joint.skeletonSpaceMatrix))
                                         .Length();
            collider.radius = headLength * 0.55f;
        }
        settings_.colliders.push_back(collider);
        break;
    }

    states_.clear();
    return settings_.chains.size();
}

nlohmann::json SpringBoneSolver::ToJson() const
{
    nlohmann::json json;
    json["enabled"] = settings_.enabled;
    json["weight"] = settings_.weight;
    json["tipLengthRate"] = settings_.tipLengthRate;
    nlohmann::json chains = nlohmann::json::array();
    for (const SpringBoneChain &chain : settings_.chains)
    {
        chains.push_back({{"root", chain.rootJoint},
                          {"enabled", chain.enabled},
                          {"stiffness", chain.stiffness},
                          {"drag", chain.drag},
                          {"gravityPower", chain.gravityPower},
                          {"gravityDir", {chain.gravityDirection.x, chain.gravityDirection.y, chain.gravityDirection.z}},
                          {"hitRadius", chain.hitRadius}});
    }
    json["chains"] = chains;
    nlohmann::json colliders = nlohmann::json::array();
    for (const SpringBoneCollider &collider : settings_.colliders)
    {
        colliders.push_back({{"joint", collider.joint},
                             {"offset", {collider.offset.x, collider.offset.y, collider.offset.z}},
                             {"radius", collider.radius}});
    }
    json["colliders"] = colliders;
    return json;
}

void SpringBoneSolver::FromJson(const nlohmann::json &json)
{
    if (!json.is_object())
    {
        return;
    }
    auto readVector = [](const nlohmann::json &value, const Vector3 &fallback) {
        if (!value.is_array() || value.size() < 3)
        {
            return fallback;
        }
        return Vector3{value[0].get<float>(), value[1].get<float>(), value[2].get<float>()};
    };

    settings_.enabled = json.value("enabled", settings_.enabled);
    settings_.weight = json.value("weight", settings_.weight);
    settings_.tipLengthRate = json.value("tipLengthRate", settings_.tipLengthRate);
    if (json.contains("chains") && json["chains"].is_array())
    {
        settings_.chains.clear();
        for (const nlohmann::json &value : json["chains"])
        {
            SpringBoneChain chain;
            chain.rootJoint = value.value("root", std::string());
            chain.enabled = value.value("enabled", chain.enabled);
            chain.stiffness = value.value("stiffness", chain.stiffness);
            chain.drag = value.value("drag", chain.drag);
            chain.gravityPower = value.value("gravityPower", chain.gravityPower);
            chain.gravityDirection = readVector(value.value("gravityDir", nlohmann::json()), chain.gravityDirection);
            chain.hitRadius = value.value("hitRadius", chain.hitRadius);
            settings_.chains.push_back(chain);
        }
    }
    if (json.contains("colliders") && json["colliders"].is_array())
    {
        settings_.colliders.clear();
        for (const nlohmann::json &value : json["colliders"])
        {
            SpringBoneCollider collider;
            collider.joint = value.value("joint", std::string());
            collider.offset = readVector(value.value("offset", nlohmann::json()), collider.offset);
            collider.radius = value.value("radius", collider.radius);
            settings_.colliders.push_back(collider);
        }
    }
    states_.clear();
}

} // namespace Hagine
