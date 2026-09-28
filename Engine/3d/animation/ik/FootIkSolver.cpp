#define NOMINMAX
#include "FootIkSolver.h"
#include "MyMath.h"
#include <algorithm>
#include <cmath>
#include <collider/CollisionManager.h>
#include <line/LineRenderer.h>

namespace Hagine {
namespace {

// 自動検出で使う「それらしい名前」の部品。小文字化した名前に含まれるかで判定する
constexpr const char *kLeftTokens[] = {"left", "_l_", ":l", ".l"};
constexpr const char *kRightTokens[] = {"right", "_r_", ":r", ".r"};
constexpr const char *kUpperTokens[] = {"upleg", "thigh", "upperleg", "hip_l", "leg_l"};
constexpr const char *kLowerTokens[] = {"leg", "calf", "shin", "knee"};
constexpr const char *kFootTokens[] = {"foot", "ankle"};
constexpr const char *kToeTokens[] = {"toe", "ball"};
constexpr const char *kHipTokens[] = {"hips", "pelvis", "root_hip"};

/// <summary>小文字に落とした名前を作る（自動検出の比較用）</summary>
/// <param name="name">元の名前</param>
/// <returns>std::string: 小文字化した名前</returns>
std::string ToLower(const std::string &name)
{
    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return lower;
}

/// <summary>語のどれかを含むか</summary>
/// <param name="lowerName">小文字化した名前</param>
/// <param name="tokens">探す語</param>
/// <param name="count">語の数</param>
/// <returns>bool: 1つでも含めば true</returns>
template <size_t N>
bool ContainsAny(const std::string &lowerName, const char *const (&tokens)[N])
{
    for (const char *token : tokens)
    {
        if (lowerName.find(token) != std::string::npos)
        {
            return true;
        }
    }
    return false;
}

/// <summary>行列の平行移動成分（＝そのジョイントの位置）を取り出す</summary>
/// <param name="matrix">対象の行列</param>
/// <returns>Vector3: 位置</returns>
Vector3 GetTranslation(const Matrix4x4 &matrix)
{
    return {matrix.m[3][0], matrix.m[3][1], matrix.m[3][2]};
}

/// <summary>行列の平行移動成分を書き換える</summary>
/// <param name="matrix">対象の行列</param>
/// <param name="translation">設定する位置</param>
void SetTranslation(Matrix4x4 &matrix, const Vector3 &translation)
{
    matrix.m[3][0] = translation.x;
    matrix.m[3][1] = translation.y;
    matrix.m[3][2] = translation.z;
}

/// <summary>
/// 経過時間ぶんの補間係数。速さ0以下なら即座に目標へ合わせる
/// </summary>
/// <param name="speed">追従の速さ[1/秒]</param>
/// <param name="deltaTime">経過時間[秒]</param>
/// <returns>float: 0〜1の補間係数</returns>
float SmoothRate(float speed, float deltaTime)
{
    if (speed <= 0.0f)
    {
        return 1.0f;
    }
    return (std::min)(speed * deltaTime, 1.0f);
}

} // namespace

void FootIkSolver::SetSettings(const FootIkSettings &settings)
{
    settings_ = settings;
    states_.assign(settings_.legs.size(), LegState{});
    hipOffset_ = 0.0f;
}

void FootIkSolver::ResetSmoothing()
{
    for (LegState &state : states_)
    {
        state = LegState{};
    }
    hipOffset_ = 0.0f;
}

bool FootIkSolver::IsLegGrounded(size_t legIndex) const
{
    return legIndex < states_.size() && states_[legIndex].grounded;
}

int32_t FootIkSolver::FindJoint(const Skeleton &skeleton, const std::string &name)
{
    if (name.empty())
    {
        return -1;
    }
    auto it = skeleton.jointMap.find(name);
    return (it == skeleton.jointMap.end()) ? -1 : it->second;
}

bool FootIkSolver::AutoDetect(const Skeleton &skeleton)
{
    FootIkLeg left{};
    FootIkLeg right{};
    std::string hip;

    for (const Joint &joint : skeleton.joints)
    {
        const std::string lower = ToLower(joint.name);

        if (hip.empty() && ContainsAny(lower, kHipTokens))
        {
            hip = joint.name;
        }

        const bool isLeft = ContainsAny(lower, kLeftTokens);
        const bool isRight = ContainsAny(lower, kRightTokens);
        if (isLeft == isRight)
        {
            continue; // 左右が判らない（両方・どちらでもない）名前は脚に採らない
        }
        FootIkLeg &leg = isLeft ? left : right;

        // つま先は "toe" を含み、足首は "foot"。Mixamo の LeftToeBase は
        // "foot" を含まないので、先につま先を判定しておけば取り違えない
        if (ContainsAny(lower, kToeTokens))
        {
            if (leg.toeJoint.empty())
            {
                leg.toeJoint = joint.name;
            }
            continue;
        }
        if (ContainsAny(lower, kFootTokens))
        {
            if (leg.footJoint.empty())
            {
                leg.footJoint = joint.name;
            }
            continue;
        }
        // "LeftUpLeg" は upper と lower の両方の語を含むので、upper を先に見る
        if (ContainsAny(lower, kUpperTokens))
        {
            if (leg.upperJoint.empty())
            {
                leg.upperJoint = joint.name;
            }
            continue;
        }
        if (ContainsAny(lower, kLowerTokens))
        {
            if (leg.lowerJoint.empty())
            {
                leg.lowerJoint = joint.name;
            }
        }
    }

    FootIkSettings detected = settings_;
    detected.hipJoint = hip;
    detected.legs.clear();

    auto IsComplete = [](const FootIkLeg &leg) {
        return !leg.upperJoint.empty() && !leg.lowerJoint.empty() && !leg.footJoint.empty();
    };
    if (IsComplete(left))
    {
        detected.legs.push_back(left);
    }
    if (IsComplete(right))
    {
        detected.legs.push_back(right);
    }

    SetSettings(detected);
    return !settings_.legs.empty();
}

void FootIkSolver::Solve(Skeleton &skeleton, const Matrix4x4 &worldMatrix, float deltaTime)
{
    if (!settings_.enabled || settings_.weight <= 0.0f || settings_.legs.empty() || skeleton.joints.empty())
    {
        return;
    }
    if (states_.size() != settings_.legs.size())
    {
        states_.assign(settings_.legs.size(), LegState{});
    }

    CollisionManager *pCollision = CollisionManager::GetInstance();
    const Vector3 worldUp = {0.0f, 1.0f, 0.0f};

    // ---- 1. 足の下を調べる ----
    // 目標は「一番深く沈めたい足」で決めたいので、必要な沈み量の最小値（＝一番負の値）を覚える
    float requiredHipDrop = 0.0f;
    bool anyGrounded = false;

    for (size_t i = 0; i < settings_.legs.size(); ++i)
    {
        const FootIkLeg &leg = settings_.legs[i];
        LegState &state = states_[i];

        const int32_t footIndex = FindJoint(skeleton, leg.footJoint);
        if (footIndex < 0)
        {
            state.grounded = false;
            state.blend = 0.0f;
            continue;
        }

        const Vector3 footWorld =
            GetTranslation(skeleton.joints[footIndex].skeletonSpaceMatrix * worldMatrix);
        const Vector3 rayOrigin = footWorld + worldUp * settings_.rayUpOffset;

        RaycastHit hit{};
        const bool grounded = pCollision->RaycastClosest(rayOrigin, -worldUp, settings_.rayLength,
                                                         settings_.groundTags, hit, settings_.ignoreOwnerName);
        state.grounded = grounded;

        if (grounded)
        {
            anyGrounded = true;
            const float desiredY = hit.position.y + settings_.footHeight;

            // 前回と大きく離れていたら瞬間移動か段差の乗り換え。
            // 均すと足が中間の高さを何フレームか通ってしまうので、その場で合わせる
            const bool jumped = std::abs(desiredY - state.targetY) > settings_.rayLength;

            if (!state.hasPrevious || jumped)
            {
                // 初回と、空中から戻ってきた直後は均さずその場に合わせる（足が飛ばないように）
                state.targetY = desiredY;
                state.normal = hit.normal;
                state.hasPrevious = true;
            }
            else
            {
                const float positionRate = SmoothRate(settings_.positionLerpSpeed, deltaTime);
                const float rotationRate = SmoothRate(settings_.rotationLerpSpeed, deltaTime);
                state.targetY = Lerp(state.targetY, desiredY, positionRate);
                state.normal = Lerp(state.normal, hit.normal, rotationRate).Normalize();
            }

            state.blend = (std::min)(state.blend + SmoothRate(settings_.positionLerpSpeed, deltaTime), 1.0f);
            requiredHipDrop = (std::min)(requiredHipDrop, state.targetY - footWorld.y);
        }
        else
        {
            // 地面を見失ったら効きを抜いてアニメーション本来のポーズへ戻す
            state.blend = (std::max)(state.blend - SmoothRate(settings_.positionLerpSpeed, deltaTime), 0.0f);
            if (state.blend <= 0.0f)
            {
                state.hasPrevious = false;
                state.normal = worldUp;
            }
        }

        if (settings_.drawDebug)
        {
            const Vector4 rayColor = grounded ? Vector4{0.3f, 1.0f, 0.4f, 1.0f} : Vector4{1.0f, 0.4f, 0.3f, 1.0f};
            LineRenderer::GetInstance()->AddLine(rayOrigin, rayOrigin - worldUp * settings_.rayLength, rayColor);
            if (grounded)
            {
                LineRenderer::GetInstance()->AddLine(hit.position, hit.position + hit.normal,
                                                     {0.4f, 0.7f, 1.0f, 1.0f});
            }
        }
    }

    // ---- 2. 腰を沈める ----
    // 上げる方向には動かさない。持ち上げると宙に浮いて見えるうえ、
    // 元のアニメーションが意図した高さを壊してしまう
    const float desiredHipOffset =
        anyGrounded ? (std::max)((std::min)(requiredHipDrop, 0.0f), -settings_.maxHipDrop) : 0.0f;
    hipOffset_ = Lerp(hipOffset_, desiredHipOffset, SmoothRate(settings_.positionLerpSpeed, deltaTime));

    const Matrix4x4 inverseWorld = Inverse(worldMatrix);

    const int32_t hipIndex = FindJoint(skeleton, settings_.hipJoint);
    if (hipIndex >= 0 && std::abs(hipOffset_) > 1e-5f)
    {
        // ワールドの真下へ hipOffset_ ぶん動かしたいので、スケルトン空間での向きに直す。
        // ワールド行列に回転・スケールが入っていてもこれで正しく沈む
        const Vector3 dropSkeleton = TransformNormal(worldUp * (hipOffset_ * settings_.weight), inverseWorld);
        TranslateJoint(skeleton, hipIndex, dropSkeleton);
    }

    // ---- 3〜4. 脚ごとに解く ----
    for (size_t i = 0; i < settings_.legs.size(); ++i)
    {
        if (states_[i].blend <= 0.0f)
        {
            continue;
        }
        SolveLeg(skeleton, settings_.legs[i], states_[i], worldMatrix, inverseWorld);
    }
}

bool FootIkSolver::SolveLegToWorldPosition(Skeleton &skeleton, size_t legIndex, const Matrix4x4 &worldMatrix,
                                           const Vector3 &targetWorld)
{
    if (legIndex >= settings_.legs.size())
    {
        return false;
    }
    const FootIkLeg &leg = settings_.legs[legIndex];

    const int32_t upperIndex = FindJoint(skeleton, leg.upperJoint);
    const int32_t lowerIndex = FindJoint(skeleton, leg.lowerJoint);
    const int32_t footIndex = FindJoint(skeleton, leg.footJoint);
    if (upperIndex < 0 || lowerIndex < 0 || footIndex < 0)
    {
        return false;
    }

    SolveTwoBone(skeleton, upperIndex, lowerIndex, footIndex, Transformation(targetWorld, Inverse(worldMatrix)));
    return true;
}

void FootIkSolver::SolveLeg(Skeleton &skeleton, const FootIkLeg &leg, const LegState &state,
                            const Matrix4x4 &worldMatrix, const Matrix4x4 &inverseWorld)
{
    const int32_t upperIndex = FindJoint(skeleton, leg.upperJoint);
    const int32_t lowerIndex = FindJoint(skeleton, leg.lowerJoint);
    const int32_t footIndex = FindJoint(skeleton, leg.footJoint);
    if (upperIndex < 0 || lowerIndex < 0 || footIndex < 0)
    {
        return;
    }

    const float blend = state.blend * settings_.weight;
    const Vector3 worldUp = {0.0f, 1.0f, 0.0f};

    // ---- 3. 足首を接地位置へ運ぶ ----
    // 腰を沈めた後の位置から測り直す（沈めたぶん膝の曲がり方が変わる）
    const Vector3 footWorld = GetTranslation(skeleton.joints[footIndex].skeletonSpaceMatrix * worldMatrix);
    const Vector3 targetWorld = {footWorld.x, Lerp(footWorld.y, state.targetY, blend), footWorld.z};
    const Vector3 targetSkeleton = Transformation(targetWorld, inverseWorld);

    SolveTwoBone(skeleton, upperIndex, lowerIndex, footIndex, targetSkeleton);

    // ---- 4. 足裏を地面の傾きへ向ける ----
    if (!settings_.alignFootToGround)
    {
        return;
    }

    // 「真上 → 地面の法線」の差分をそのまま足へ掛ける。
    // 足のどの軸が裏側かはリグによって違うので、足自身の軸には触れずに
    // 地面の傾きぶんだけ回すこの形にしてある（リグを選ばない）
    const float cosTilt = (std::max)(-1.0f, (std::min)(1.0f, worldUp.Dot(state.normal)));
    float tiltAngle = std::acos(cosTilt) * blend;

    // 傾け過ぎると足がめり込む・ねじれるので角度に上限を設ける
    tiltAngle = (std::min)(tiltAngle, degreesToRadians(settings_.maxFootAngleDegrees));
    if (tiltAngle <= 1e-4f)
    {
        return;
    }

    Vector3 tiltAxis = worldUp.Cross(state.normal);
    if (tiltAxis.LengthSq() < 1e-8f)
    {
        return; // 法線が真上とほぼ同じ（平らな地面）。傾ける必要が無い
    }

    // 回転軸はワールド空間で求めたものなので、スケルトン空間へ移してから掛ける
    tiltAxis = TransformNormal(tiltAxis.Normalize(), inverseWorld);
    if (tiltAxis.LengthSq() < 1e-8f)
    {
        return;
    }
    RotateJoint(skeleton, footIndex, Quaternion::FromAxisAngle(tiltAxis.Normalize(), tiltAngle));
}

void FootIkSolver::SolveTwoBone(Skeleton &skeleton, int32_t upperIndex, int32_t lowerIndex, int32_t footIndex,
                                const Vector3 &targetSkeletonSpace)
{
    const Vector3 upperPosition = GetTranslation(skeleton.joints[upperIndex].skeletonSpaceMatrix);
    const Vector3 lowerPosition = GetTranslation(skeleton.joints[lowerIndex].skeletonSpaceMatrix);
    const Vector3 footPosition = GetTranslation(skeleton.joints[footIndex].skeletonSpaceMatrix);

    const float upperLength = (lowerPosition - upperPosition).Length();
    const float lowerLength = (footPosition - lowerPosition).Length();
    if (upperLength <= 1e-5f || lowerLength <= 1e-5f)
    {
        return;
    }

    // 目標までの距離。脚を伸ばし切る／畳み切る手前で止める
    // （きっかり伸ばし切ると膝の回転軸が定まらず、次のフレームで暴れる）
    const float reachMax = (upperLength + lowerLength) * 0.999f;
    const float reachMin = std::abs(upperLength - lowerLength) * 1.001f + 1e-4f;
    Vector3 toTarget = targetSkeletonSpace - upperPosition;
    float reach = toTarget.Length();
    if (reach <= 1e-5f)
    {
        return;
    }
    reach = (std::max)((std::min)(reach, reachMax), reachMin);

    // ---- 膝の曲げ角を作り直す ----
    // 余弦定理: cos(膝) = (腿^2 + すね^2 - 目標距離^2) / (2 * 腿 * すね)
    const float cosDesired =
        (upperLength * upperLength + lowerLength * lowerLength - reach * reach) / (2.0f * upperLength * lowerLength);
    const float desiredKneeAngle = std::acos((std::max)(-1.0f, (std::min)(1.0f, cosDesired)));

    const Vector3 kneeToUpper = (upperPosition - lowerPosition).Normalize();
    const Vector3 kneeToFoot = (footPosition - lowerPosition).Normalize();
    const float currentKneeAngle =
        std::acos((std::max)(-1.0f, (std::min)(1.0f, kneeToUpper.Dot(kneeToFoot))));

    Vector3 bendAxis = kneeToUpper.Cross(kneeToFoot);
    if (bendAxis.LengthSq() < 1e-8f)
    {
        // 脚が一直線で曲げる面が決まらない。腿のローカルX軸を蝶番に見立てる
        // （人型リグでは膝はおおむねこの軸まわりに曲がる）
        const Matrix4x4 &upperMatrix = skeleton.joints[upperIndex].skeletonSpaceMatrix;
        bendAxis = Vector3{upperMatrix.m[0][0], upperMatrix.m[0][1], upperMatrix.m[0][2]};
        if (bendAxis.LengthSq() < 1e-8f)
        {
            return;
        }
    }
    bendAxis = bendAxis.Normalize();

    // この軸まわりに正の角度で回すと、腿とすねのなす角は開く（＝膝が伸びる）
    const float kneeDelta = desiredKneeAngle - currentKneeAngle;
    if (std::abs(kneeDelta) > 1e-5f)
    {
        RotateJoint(skeleton, lowerIndex, Quaternion::FromAxisAngle(bendAxis, kneeDelta));
    }

    // ---- 腿を回して足首を目標の方向へ向ける ----
    // 膝を作り直したので、腿から足首までの距離は目標までの距離と一致している。
    // あとは向きを合わせれば足首が目標へ乗る
    const Vector3 movedFoot = GetTranslation(skeleton.joints[footIndex].skeletonSpaceMatrix);
    const Vector3 fromDirection = movedFoot - upperPosition;
    if (fromDirection.LengthSq() < 1e-8f || toTarget.LengthSq() < 1e-8f)
    {
        return;
    }

    Quaternion swing;
    swing.SetFromTo(fromDirection, toTarget);
    RotateJoint(skeleton, upperIndex, swing);
}

void FootIkSolver::RotateJoint(Skeleton &skeleton, int32_t jointIndex, const Quaternion &rotation)
{
    Matrix4x4 &matrix = skeleton.joints[jointIndex].skeletonSpaceMatrix;
    const Vector3 pivot = GetTranslation(matrix);

    // 行ベクトル規約なので、行0〜2 がこのジョイントのローカル軸（スケール込み）そのもの。
    // それを回して書き戻せば「自分の位置を中心に回す」ことになる
    for (int row = 0; row < 3; ++row)
    {
        const Vector3 axis = {matrix.m[row][0], matrix.m[row][1], matrix.m[row][2]};
        const Vector3 rotated = RotateVector(axis, rotation);
        matrix.m[row][0] = rotated.x;
        matrix.m[row][1] = rotated.y;
        matrix.m[row][2] = rotated.z;
    }
    SetTranslation(matrix, pivot);

    WriteBackLocalMatrix(skeleton, jointIndex);
    RefreshDescendants(skeleton, jointIndex);
}

void FootIkSolver::TranslateJoint(Skeleton &skeleton, int32_t jointIndex, const Vector3 &offset)
{
    Matrix4x4 &matrix = skeleton.joints[jointIndex].skeletonSpaceMatrix;
    SetTranslation(matrix, GetTranslation(matrix) + offset);

    WriteBackLocalMatrix(skeleton, jointIndex);
    RefreshDescendants(skeleton, jointIndex);
}

void FootIkSolver::WriteBackLocalMatrix(Skeleton &skeleton, int32_t jointIndex)
{
    // 書き換えたのはスケルトン空間行列だけなので、このままだと localMatrix が食い違う。
    // 後から祖先を動かすと RefreshDescendants が古い localMatrix から組み直してしまい、
    // 直前に入れた曲げが消える（膝を曲げてから腿を振ると膝が伸びる、という壊れ方をする）。
    // ここで localMatrix を今の姿勢に合わせておけば、祖先の変更にもそのまま付いてくる
    Joint &joint = skeleton.joints[jointIndex];
    if (joint.parent)
    {
        joint.localMatrix = joint.skeletonSpaceMatrix * Inverse(skeleton.joints[*joint.parent].skeletonSpaceMatrix);
    }
    else
    {
        joint.localMatrix = joint.skeletonSpaceMatrix;
    }
}

void FootIkSolver::RefreshDescendants(Skeleton &skeleton, int32_t jointIndex)
{
    // 子のローカル行列は動かしていないので、親が変わったぶんだけ掛け直せばよい。
    // Bone::CalculateMatrices と同じ式で、対象を部分木に絞ったもの
    std::vector<int32_t> stack = skeleton.joints[jointIndex].children;
    while (!stack.empty())
    {
        const int32_t index = stack.back();
        stack.pop_back();

        Joint &joint = skeleton.joints[index];
        joint.skeletonSpaceMatrix = joint.localMatrix * skeleton.joints[*joint.parent].skeletonSpaceMatrix;

        for (int32_t child : joint.children)
        {
            stack.push_back(child);
        }
    }
}

nlohmann::json FootIkSolver::ToJson() const
{
    nlohmann::json json = nlohmann::json::object();
    json["enabled"] = settings_.enabled;
    json["weight"] = settings_.weight;
    json["hipJoint"] = settings_.hipJoint;
    json["rayUpOffset"] = settings_.rayUpOffset;
    json["rayLength"] = settings_.rayLength;
    json["footHeight"] = settings_.footHeight;
    json["maxHipDrop"] = settings_.maxHipDrop;
    json["positionLerpSpeed"] = settings_.positionLerpSpeed;
    json["rotationLerpSpeed"] = settings_.rotationLerpSpeed;
    json["maxFootAngleDegrees"] = settings_.maxFootAngleDegrees;
    json["alignFootToGround"] = settings_.alignFootToGround;
    json["drawDebug"] = settings_.drawDebug;
    json["groundTags"] = settings_.groundTags;

    nlohmann::json legs = nlohmann::json::array();
    for (const FootIkLeg &leg : settings_.legs)
    {
        nlohmann::json entry = nlohmann::json::object();
        entry["upper"] = leg.upperJoint;
        entry["lower"] = leg.lowerJoint;
        entry["foot"] = leg.footJoint;
        entry["toe"] = leg.toeJoint;
        legs.push_back(std::move(entry));
    }
    json["legs"] = std::move(legs);
    return json;
}

void FootIkSolver::FromJson(const nlohmann::json &json)
{
    if (!json.is_object())
    {
        return;
    }

    FootIkSettings loaded{};
    loaded.enabled = json.value("enabled", false);
    loaded.weight = json.value("weight", 1.0f);
    loaded.hipJoint = json.value("hipJoint", std::string());
    loaded.rayUpOffset = json.value("rayUpOffset", 0.8f);
    loaded.rayLength = json.value("rayLength", 2.0f);
    loaded.footHeight = json.value("footHeight", 0.1f);
    loaded.maxHipDrop = json.value("maxHipDrop", 0.6f);
    loaded.positionLerpSpeed = json.value("positionLerpSpeed", 12.0f);
    loaded.rotationLerpSpeed = json.value("rotationLerpSpeed", 12.0f);
    loaded.maxFootAngleDegrees = json.value("maxFootAngleDegrees", 45.0f);
    loaded.alignFootToGround = json.value("alignFootToGround", true);
    loaded.drawDebug = json.value("drawDebug", false);
    loaded.groundTags = json.value("groundTags", std::vector<std::string>());

    if (json.contains("legs") && json["legs"].is_array())
    {
        for (const nlohmann::json &entry : json["legs"])
        {
            FootIkLeg leg{};
            leg.upperJoint = entry.value("upper", std::string());
            leg.lowerJoint = entry.value("lower", std::string());
            leg.footJoint = entry.value("foot", std::string());
            leg.toeJoint = entry.value("toe", std::string());
            loaded.legs.push_back(std::move(leg));
        }
    }

    SetSettings(loaded);
}
} // namespace Hagine
