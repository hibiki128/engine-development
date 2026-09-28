#include "LookAtSolver.h"
#include <MyMath.h>
#include <algorithm>
#include <cmath>
#include <numbers>

namespace Hagine {

namespace {
constexpr float kDegToRad = std::numbers::pi_v<float> / 180.0f;
constexpr float kRadToDeg = 180.0f / std::numbers::pi_v<float>;
constexpr float kMinBlend = 1.0e-3f; // これより効きが小さければポーズに触らない

/// <summary>
/// 軸まわりの回転行列（行ベクトル規約: v * M で回す）。ロドリゲスの回転公式
/// </summary>
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

/// <summary>
/// ジョイントとその子孫すべての行列に、後ろから変換を掛ける
/// </summary>
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
} // namespace

void LookAtSolver::SetTarget(const Vector3 &targetWorld)
{
    target_ = targetWorld;
    hasTarget_ = true;
}

float LookAtSolver::GetYawDegrees() const { return yaw_ * kRadToDeg; }
float LookAtSolver::GetPitchDegrees() const { return pitch_ * kRadToDeg; }

bool LookAtSolver::Solve(Skeleton &skeleton, const Matrix4x4 &worldMatrix, const Vector3 &faceWorld, float deltaTime)
{
    if (settings_.joints.empty())
    {
        return false;
    }

    // 回すジョイントを体の根元側から順に並べる（親の回転を子が受け継ぐので順番が大事）
    struct Entry
    {
        int32_t index;
        float share;
    };
    std::vector<Entry> entries;
    float totalShare = 0.0f;
    for (const LookAtJoint &joint : settings_.joints)
    {
        auto it = skeleton.jointMap.find(joint.name);
        if (it == skeleton.jointMap.end() || joint.share <= 0.0f)
        {
            continue;
        }
        entries.push_back({it->second, joint.share});
        totalShare += joint.share;
    }
    if (entries.empty() || totalShare <= 0.0f)
    {
        return false;
    }
    std::sort(entries.begin(), entries.end(), [](const Entry &a, const Entry &b) { return a.index < b.index; });

    // ---- 体の正面と目標の向きを、モデル空間で比べる ----
    const Matrix4x4 inverseWorld = Inverse(worldMatrix);
    const Vector3 up = {0.0f, 1.0f, 0.0f}; // 体の上（体が傾いていても体基準で見る）
    Vector3 face = TransformNormal(faceWorld, inverseWorld);
    face = face - up * face.Dot(up);
    if (face.Length() < 1.0e-5f)
    {
        return false;
    }
    face = face.Normalize();
    const Vector3 right = up.Cross(face).Normalize();

    float targetYaw = 0.0f;
    float targetPitch = 0.0f;
    float targetBlend = 0.0f;
    if (hasTarget_ && settings_.enabled)
    {
        // 頭（最後のジョイント）から目標への向き
        const Vector3 headModel = GetTranslation(skeleton.joints[entries.back().index].skeletonSpaceMatrix);
        const Vector3 headWorld = Transformation(headModel, worldMatrix);
        Vector3 toTarget = TransformNormal(target_ - headWorld, inverseWorld);
        if (toTarget.Length() > 1.0e-5f)
        {
            toTarget = toTarget.Normalize();
            const float yaw = std::atan2(toTarget.Dot(right), toTarget.Dot(face));
            const float pitch = std::asin(std::clamp(toTarget.Dot(up), -1.0f, 1.0f));
            // 真後ろ近くまで回られたら首をひねり切らずに正面へ戻す
            if (std::abs(yaw) <= settings_.giveUpYawDegrees * kDegToRad)
            {
                targetYaw = std::clamp(yaw, -settings_.maxYawDegrees * kDegToRad, settings_.maxYawDegrees * kDegToRad);
                targetPitch = std::clamp(pitch, -settings_.maxPitchDegrees * kDegToRad, settings_.maxPitchDegrees * kDegToRad);
                targetBlend = 1.0f;
            }
        }
    }

    // ---- 均す（目標が飛んでも首がパキッと回らないように）----
    const float follow = 1.0f - std::exp(-(std::max)(settings_.followSpeed, 0.0f) * deltaTime);
    const float fade = 1.0f - std::exp(-(std::max)(settings_.blendSpeed, 0.0f) * deltaTime);
    yaw_ += (targetYaw - yaw_) * follow;
    pitch_ += (targetPitch - pitch_) * follow;
    blend_ += (targetBlend - blend_) * fade;

    const float effective = std::clamp(settings_.weight, 0.0f, 1.0f) * blend_;
    if (effective < kMinBlend)
    {
        return false;
    }

    // ---- 正面から「向けたい向き」への回転を、ジョイントで分け合って足す ----
    const Vector3 look = (face * (std::cos(yaw_) * std::cos(pitch_)) + right * (std::sin(yaw_) * std::cos(pitch_)) + up * std::sin(pitch_)).Normalize();
    Vector3 axis = face.Cross(look);
    const float axisLength = axis.Length();
    if (axisLength < 1.0e-6f)
    {
        return false; // 正面を見ている
    }
    axis = axis / axisLength;
    const float angle = std::acos(std::clamp(face.Dot(look), -1.0f, 1.0f)) * effective;

    for (const Entry &entry : entries)
    {
        // ジョイントの位置を中心に回す（子孫も一緒に回る）
        const Vector3 pivot = GetTranslation(skeleton.joints[entry.index].skeletonSpaceMatrix);
        const Matrix4x4 rotate = MakeTranslateMatrix(pivot * -1.0f) * MakeAxisRotation(axis, angle * entry.share / totalShare) * MakeTranslateMatrix(pivot);
        ApplyToSubtree(skeleton, entry.index, rotate);
    }
    return true;
}

bool LookAtSolver::AutoDetect(const Skeleton &skeleton)
{
    // 名前の末尾で探す（"mixamorig:Head" / "Head" / "head" など）
    auto find = [&](std::initializer_list<const char *> candidates) -> std::string {
        for (const char *candidate : candidates)
        {
            for (const Joint &joint : skeleton.joints)
            {
                const std::string &name = joint.name;
                const std::string key = candidate;
                if (name.size() >= key.size() && name.compare(name.size() - key.size(), key.size(), key) == 0)
                {
                    return name;
                }
            }
        }
        return std::string();
    };

    const std::string head = find({"Head", "head"});
    if (head.empty())
    {
        return false;
    }
    const std::string neck = find({"Neck", "neck"});
    const std::string spine = find({"Spine2", "spine2", "Spine1", "spine1", "Chest", "chest"});

    settings_.joints.clear();
    if (!spine.empty())
    {
        settings_.joints.push_back({spine, 0.25f});
    }
    if (!neck.empty())
    {
        settings_.joints.push_back({neck, 0.3f});
    }
    settings_.joints.push_back({head, 0.45f});
    return true;
}

nlohmann::json LookAtSolver::ToJson() const
{
    nlohmann::json json;
    json["enabled"] = settings_.enabled;
    json["weight"] = settings_.weight;
    json["maxYaw"] = settings_.maxYawDegrees;
    json["maxPitch"] = settings_.maxPitchDegrees;
    json["giveUpYaw"] = settings_.giveUpYawDegrees;
    json["followSpeed"] = settings_.followSpeed;
    json["blendSpeed"] = settings_.blendSpeed;
    nlohmann::json joints = nlohmann::json::array();
    for (const LookAtJoint &joint : settings_.joints)
    {
        joints.push_back({{"name", joint.name}, {"share", joint.share}});
    }
    json["joints"] = joints;
    return json;
}

void LookAtSolver::FromJson(const nlohmann::json &json)
{
    if (!json.is_object())
    {
        return;
    }
    settings_.enabled = json.value("enabled", settings_.enabled);
    settings_.weight = json.value("weight", settings_.weight);
    settings_.maxYawDegrees = json.value("maxYaw", settings_.maxYawDegrees);
    settings_.maxPitchDegrees = json.value("maxPitch", settings_.maxPitchDegrees);
    settings_.giveUpYawDegrees = json.value("giveUpYaw", settings_.giveUpYawDegrees);
    settings_.followSpeed = json.value("followSpeed", settings_.followSpeed);
    settings_.blendSpeed = json.value("blendSpeed", settings_.blendSpeed);
    if (json.contains("joints") && json["joints"].is_array())
    {
        settings_.joints.clear();
        for (const nlohmann::json &joint : json["joints"])
        {
            settings_.joints.push_back({joint.value("name", std::string()), joint.value("share", 1.0f)});
        }
    }
}

} // namespace Hagine
