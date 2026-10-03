#define NOMINMAX
#include "HandIkSolver.h"
#include "TwoBoneIk.h"
#include <MyMath.h>
#include <algorithm>
#include <cmath>
#include <line/LineRenderer.h>

namespace Hagine {

namespace {
constexpr float kMinBlend = 1.0e-3f; // これより効きが小さければポーズに触らない

// 指の骨は手首の候補から外す（"LeftHandIndex1" など、手首の子に hand を含む名前が並ぶため）
constexpr const char *kFingerTokens[] = {"thumb", "index", "middle", "ring", "pinky", "little", "finger"};

Vector3 GetTranslation(const Matrix4x4 &m)
{
    return {m.m[3][0], m.m[3][1], m.m[3][2]};
}

std::string ToLower(const std::string &name)
{
    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return lower;
}

bool EndsWith(const std::string &text, const char *suffix)
{
    const std::string key = suffix;
    return text.size() >= key.size() && text.compare(text.size() - key.size(), key.size(), key) == 0;
}

/// <summary>名前から左右を見分ける（-1=左 / 1=右 / 0=分からない）</summary>
int SideOf(const std::string &lowerName)
{
    if (lowerName.find("left") != std::string::npos)
    {
        return -1;
    }
    if (lowerName.find("right") != std::string::npos)
    {
        return 1;
    }
    if (EndsWith(lowerName, "_l") || EndsWith(lowerName, ".l") || lowerName.find("_l_") != std::string::npos)
    {
        return -1;
    }
    if (EndsWith(lowerName, "_r") || EndsWith(lowerName, ".r") || lowerName.find("_r_") != std::string::npos)
    {
        return 1;
    }
    return 0;
}

bool IsHandName(const std::string &lowerName)
{
    if (lowerName.find("hand") == std::string::npos && lowerName.find("wrist") == std::string::npos)
    {
        return false;
    }
    for (const char *token : kFingerTokens)
    {
        if (lowerName.find(token) != std::string::npos)
        {
            return false;
        }
    }
    return true;
}
} // namespace

void HandIkSolver::SetTarget(size_t limbIndex, const Vector3 &targetWorld)
{
    if (limbIndex >= states_.size())
    {
        states_.resize(settings_.limbs.size());
    }
    if (limbIndex < states_.size())
    {
        states_[limbIndex].hasTarget = true;
        states_[limbIndex].target = targetWorld;
    }
}

bool HandIkSolver::SetTarget(const std::string &label, const Vector3 &targetWorld)
{
    for (size_t i = 0; i < settings_.limbs.size(); ++i)
    {
        if (settings_.limbs[i].label == label)
        {
            SetTarget(i, targetWorld);
            return true;
        }
    }
    return false;
}

void HandIkSolver::ClearTarget(size_t limbIndex)
{
    if (limbIndex < states_.size())
    {
        states_[limbIndex].hasTarget = false;
    }
}

void HandIkSolver::ClearAllTargets()
{
    for (LimbState &state : states_)
    {
        state.hasTarget = false;
    }
}

bool HandIkSolver::Solve(Skeleton &skeleton, const Matrix4x4 &worldMatrix, float deltaTime)
{
    states_.resize(settings_.limbs.size());
    if (!settings_.enabled || settings_.limbs.empty())
    {
        for (LimbState &state : states_)
        {
            state.blend = 0.0f;
        }
        return false;
    }

    const Matrix4x4 inverseWorld = Inverse(worldMatrix);
    const float fade = 1.0f - std::exp(-(std::max)(settings_.blendSpeed, 0.0f) * deltaTime);
    LineRenderer *pLine = settings_.drawDebug ? LineRenderer::GetInstance() : nullptr;

    bool changed = false;
    for (size_t i = 0; i < settings_.limbs.size(); ++i)
    {
        const HandIkLimb &limb = settings_.limbs[i];
        LimbState &state = states_[i];
        const int32_t upperIndex = TwoBoneIk::FindJoint(skeleton, limb.upperJoint);
        const int32_t lowerIndex = TwoBoneIk::FindJoint(skeleton, limb.lowerJoint);
        const int32_t handIndex = TwoBoneIk::FindJoint(skeleton, limb.handJoint);
        if (upperIndex < 0 || lowerIndex < 0 || handIndex < 0)
        {
            state.blend = 0.0f;
            continue;
        }

        const Vector3 upperModel = GetTranslation(skeleton.joints[upperIndex].skeletonSpaceMatrix);
        const Vector3 lowerModel = GetTranslation(skeleton.joints[lowerIndex].skeletonSpaceMatrix);
        const Vector3 handModel = GetTranslation(skeleton.joints[handIndex].skeletonSpaceMatrix);
        state.animatedHandWorld = Transformation(handModel, worldMatrix);

        // 試しの目標（エディタ）はゲームの目標より優先し、一時停止中でもすぐ効かせる
        const bool wanted = limb.enabled && (state.useTestTarget || state.hasTarget);
        const Vector3 targetWorld = state.useTestTarget ? state.testTarget : state.target;
        if (state.useTestTarget && limb.enabled)
        {
            state.blend = 1.0f;
        }
        else
        {
            state.blend += ((wanted ? 1.0f : 0.0f) - state.blend) * fade;
        }

        const float armLength = (lowerModel - upperModel).Length() + (handModel - lowerModel).Length();
        if (armLength > 1.0e-5f)
        {
            state.reachRate = (Transformation(targetWorld, inverseWorld) - upperModel).Length() / armLength;
        }

        const float effective = std::clamp(settings_.weight, 0.0f, 1.0f) * state.blend;
        if (effective < kMinBlend)
        {
            continue;
        }

        // アニメーションの手首の位置から目標へ、効きの分だけ寄せた点へ運ぶ
        const Vector3 goalWorld = Lerp(state.animatedHandWorld, targetWorld, effective);
        TwoBoneIk::Solve(skeleton, upperIndex, lowerIndex, handIndex, Transformation(goalWorld, inverseWorld));
        changed = true;

        if (pLine)
        {
            const Vector3 upperWorld = Transformation(GetTranslation(skeleton.joints[upperIndex].skeletonSpaceMatrix), worldMatrix);
            const Vector3 elbowWorld = Transformation(GetTranslation(skeleton.joints[lowerIndex].skeletonSpaceMatrix), worldMatrix);
            const Vector3 handWorld = Transformation(GetTranslation(skeleton.joints[handIndex].skeletonSpaceMatrix), worldMatrix);
            const Vector4 armColor = {0.4f, 1.0f, 0.6f, 1.0f};
            pLine->AddLine(upperWorld, elbowWorld, armColor);
            pLine->AddLine(elbowWorld, handWorld, armColor);
            const float markerRadius = armLength * 0.06f * TransformNormal({1.0f, 0.0f, 0.0f}, worldMatrix).Length();
            pLine->AddSphere(targetWorld, markerRadius, {1.0f, 0.85f, 0.2f, 1.0f}, 12);
        }
    }
    return changed;
}

size_t HandIkSolver::AutoDetect(const Skeleton &skeleton)
{
    settings_.limbs.clear();
    for (int side : {-1, 1})
    {
        // 手首らしい名前で、親が手首らしくない物（指の付け根の "HandIndex" などを除いた本体）
        for (const Joint &joint : skeleton.joints)
        {
            const std::string lower = ToLower(joint.name);
            if (!IsHandName(lower) || SideOf(lower) != side || !joint.parent)
            {
                continue;
            }
            const Joint &forearm = skeleton.joints[*joint.parent];
            if (IsHandName(ToLower(forearm.name)) || !forearm.parent)
            {
                continue;
            }
            const Joint &upperArm = skeleton.joints[*forearm.parent];
            HandIkLimb limb;
            limb.label = side < 0 ? "左手" : "右手";
            limb.upperJoint = upperArm.name;
            limb.lowerJoint = forearm.name;
            limb.handJoint = joint.name;
            settings_.limbs.push_back(limb);
            break;
        }
    }
    states_.assign(settings_.limbs.size(), LimbState{});
    return settings_.limbs.size();
}

nlohmann::json HandIkSolver::ToJson() const
{
    nlohmann::json json;
    json["enabled"] = settings_.enabled;
    json["weight"] = settings_.weight;
    json["blendSpeed"] = settings_.blendSpeed;
    nlohmann::json limbs = nlohmann::json::array();
    for (const HandIkLimb &limb : settings_.limbs)
    {
        limbs.push_back({{"label", limb.label},
                         {"upper", limb.upperJoint},
                         {"lower", limb.lowerJoint},
                         {"hand", limb.handJoint},
                         {"enabled", limb.enabled}});
    }
    json["limbs"] = limbs;
    return json;
}

void HandIkSolver::FromJson(const nlohmann::json &json)
{
    if (!json.is_object())
    {
        return;
    }
    settings_.enabled = json.value("enabled", settings_.enabled);
    settings_.weight = json.value("weight", settings_.weight);
    settings_.blendSpeed = json.value("blendSpeed", settings_.blendSpeed);
    if (json.contains("limbs") && json["limbs"].is_array())
    {
        settings_.limbs.clear();
        for (const nlohmann::json &value : json["limbs"])
        {
            HandIkLimb limb;
            limb.label = value.value("label", std::string());
            limb.upperJoint = value.value("upper", std::string());
            limb.lowerJoint = value.value("lower", std::string());
            limb.handJoint = value.value("hand", std::string());
            limb.enabled = value.value("enabled", true);
            settings_.limbs.push_back(limb);
        }
    }
    states_.assign(settings_.limbs.size(), LimbState{});
}

} // namespace Hagine
