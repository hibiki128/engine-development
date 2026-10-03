#include "MotionEditor.h"
#include "utility/debug/imgui/ImGuiNotification.h"
#ifdef USE_IMGUI
#include "imgui.h"
#include "utility/debug/imgui/DebugUIHelper.h" // std::string 版 InputText / テーマ配色ヘルパー
#include "utility/debug/imgui/ImGuiExtras.h"   // モーション軌跡の3Dプレビュー (implot3d)
#endif // USE_IMGUI
#include "MyMath.h"
#include <line/LineRenderer.h>
#ifdef USE_IMGUI
#include <algorithm>
#include <asset/AssetPath.h>
#include <edit/undo/ImGuiUndoTracker.h>
#include <filesystem>
#include <format>
#include <icon/IconsFontAwesome5.h>
#include <implot.h>
#include <numbers>
namespace {
// モーション窓の編集ジェスチャを Undo 履歴へ積むトラッカー
Hagine::ImGuiUndoTracker g_motionUndoTracker;
// MotionEasingType と同じ並び
const char *const kMotionEasingNames[] = {
    "リニア", "InSine", "OutSine", "InOutSine", "InBack", "OutBack", "InOutBack", "InQuint", "OutQuint", "InOutQuint",
    "InCirc", "OutCirc", "InOutCirc", "InExpo", "OutExpo", "InOutExpo", "OutCubic", "InCubic", "InOutCubic", "InQuad",
    "OutQuad", "InOutQuad", "InQuart", "OutQuart", "InBounce", "OutBounce", "InOutBounce", "InElastic", "OutElastic", "InOutElastic"};
static_assert(std::size(kMotionEasingNames) == static_cast<size_t>(Hagine::MotionEasingType::EaseInOutElastic) + 1,
              "kMotionEasingNames は MotionEasingType と同数にすること");
} // namespace
#endif // USE_IMGUI

namespace Hagine {
const float MotionEditor::ATTACK_END_INTERVAL = 0.1f;

// イージング適用関数
float ApplyMotionEasing(MotionEasingType type, float t, float total)
{
    switch (type)
    {
    case MotionEasingType::EaseInSine:
        return EaseInSine(0.0f, 1.0f, t, total);
    case MotionEasingType::EaseOutSine:
        return EaseOutSine(0.0f, 1.0f, t, total);
    case MotionEasingType::EaseInOutSine:
        return EaseInOutSine(0.0f, 1.0f, t, total);
    case MotionEasingType::EaseInBack:
        return EaseInBack(0.0f, 1.0f, t, total);
    case MotionEasingType::EaseOutBack:
        return EaseOutBack(0.0f, 1.0f, t, total);
    case MotionEasingType::EaseInOutBack:
        return EaseInOutBack(0.0f, 1.0f, t, total);
    case MotionEasingType::EaseInQuint:
        return EaseInQuint(0.0f, 1.0f, t, total);
    case MotionEasingType::EaseOutQuint:
        return EaseOutQuint(0.0f, 1.0f, t, total);
    case MotionEasingType::EaseInOutQuint:
        return EaseInOutQuint(0.0f, 1.0f, t, total);
    case MotionEasingType::EaseInCirc:
        return EaseInCirc(0.0f, 1.0f, t, total);
    case MotionEasingType::EaseOutCirc:
        return EaseOutCirc(0.0f, 1.0f, t, total);
    case MotionEasingType::EaseInOutCirc:
        return EaseInOutCirc(0.0f, 1.0f, t, total);
    case MotionEasingType::EaseInExpo:
        return EaseInExpo(0.0f, 1.0f, t, total);
    case MotionEasingType::EaseOutExpo:
        return EaseOutExpo(0.0f, 1.0f, t, total);
    case MotionEasingType::EaseInOutExpo:
        return EaseInOutExpo(0.0f, 1.0f, t, total);
    case MotionEasingType::EaseOutCubic:
        return EaseOutCubic(0.0f, 1.0f, t, total);
    case MotionEasingType::EaseInCubic:
        return EaseInCubic(0.0f, 1.0f, t, total);
    case MotionEasingType::EaseInOutCubic:
        return EaseInOutCubic(0.0f, 1.0f, t, total);
    case MotionEasingType::EaseInQuad:
        return EaseInQuad(0.0f, 1.0f, t, total);
    case MotionEasingType::EaseOutQuad:
        return EaseOutQuad(0.0f, 1.0f, t, total);
    case MotionEasingType::EaseInOutQuad:
        return EaseInOutQuad(0.0f, 1.0f, t, total);
    case MotionEasingType::EaseInQuart:
        return EaseInQuart(0.0f, 1.0f, t, total);
    case MotionEasingType::EaseOutQuart:
        return EaseOutQuart(0.0f, 1.0f, t, total);
    case MotionEasingType::EaseInBounce:
        return EaseInBounce(0.0f, 1.0f, t, total);
    case MotionEasingType::EaseOutBounce:
        return EaseOutBounce(0.0f, 1.0f, t, total);
    case MotionEasingType::EaseInOutBounce:
        return EaseInOutBounce(0.0f, 1.0f, t, total);
    case MotionEasingType::EaseInElastic:
        return EaseInElastic(0.0f, 1.0f, t, total);
    case MotionEasingType::EaseOutElastic:
        return EaseOutElastic(0.0f, 1.0f, t, total);
    case MotionEasingType::EaseInOutElastic:
        return EaseInOutElastic(0.0f, 1.0f, t, total);
    default:
        return t;
    }
}

void MotionEditor::Finalize()
{
    motions_.clear();
    comboStartPositions_.clear();
    comboStartRotations_.clear();
    comboStartScales_.clear();
    attackEndIntervals_.clear();
}

void MotionEditor::Register(BaseObject *pObject)
{
    if (!pObject)
        return;

    std::string name = pObject->GetName();

    // 既に登録済みかチェック
    if (motions_.find(name) != motions_.end())
    {
        return;
    }

    // 新しくモーションを登録
    motions_[name] = Motion();
    motions_[name].pTarget = pObject;
    motions_[name].objectName = name;
    ImGuiNotification::Post("モーションエディタに登録しました: " + name, {0.4f, 0.8f, 1.0f, 1.0f});
}

void MotionEditor::Unregister(BaseObject *pObject)
{
    if (!pObject)
        return;

    // 名前ではなく実体で引く。改名されていても取り残しが出ないようにするため。
    for (auto it = motions_.begin(); it != motions_.end();)
    {
        if (it->second.pTarget == pObject)
        {
            if (selectedName_ == it->first)
            {
                selectedName_.clear();
                selectedControlPoint_ = -1;
            }
            it = motions_.erase(it);
        }
        else
        {
            ++it;
        }
    }

    // 攻撃モーションの進行状態もオブジェクト単位で持っているので一緒に落とす
    comboStartPositions_.erase(pObject);
    comboStartRotations_.erase(pObject);
    comboStartScales_.erase(pObject);
    attackEndIntervals_.erase(pObject);
}

void MotionEditor::CleanupFinishedTemporaryMotions()
{
    auto it = motions_.begin();
    while (it != motions_.end())
    {
        // 終了した一時モーションを削除
        if (it->second.isTemporary && it->second.status == MotionStatus::Finished)
        {
            it = motions_.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

Vector3 MotionEditor::TransformLocalToWorld(const Vector3 &localOffset, const Matrix4x4 &worldMatrix)
{
    // 方向ベクトルとして変換（平行移動成分を無視）
    Vector4 localOffset4 = {localOffset.x, localOffset.y, localOffset.z, 0.0f};

    Vector4 worldOffset4;
    worldOffset4.x = localOffset4.x * worldMatrix.m[0][0] + localOffset4.y * worldMatrix.m[1][0] +
                     localOffset4.z * worldMatrix.m[2][0] + localOffset4.w * worldMatrix.m[3][0];
    worldOffset4.y = localOffset4.x * worldMatrix.m[0][1] + localOffset4.y * worldMatrix.m[1][1] +
                     localOffset4.z * worldMatrix.m[2][1] + localOffset4.w * worldMatrix.m[3][1];
    worldOffset4.z = localOffset4.x * worldMatrix.m[0][2] + localOffset4.y * worldMatrix.m[1][2] +
                     localOffset4.z * worldMatrix.m[2][2] + localOffset4.w * worldMatrix.m[3][2];

    return {worldOffset4.x, worldOffset4.y, worldOffset4.z};
}

MotionStatus MotionEditor::GetMotionStatus(const std::string &objectName)
{
    auto it = motions_.find(objectName);
    if (it == motions_.end())
    {
        return MotionStatus::Stopped;
    }
    return it->second.status;
}

bool MotionEditor::IsPlaying(const std::string &objectName)
{
    return GetMotionStatus(objectName) == MotionStatus::Playing;
}

bool MotionEditor::IsFinished(const std::string &objectName)
{
    return GetMotionStatus(objectName) == MotionStatus::Finished;
}

std::string MotionEditor::GetTemporaryMotionName(BaseObject *pTarget, const std::string &fileName)
{
    return "temp_" + pTarget->GetName() + "_" + fileName;
}

void MotionEditor::ResetInitialPosition(const std::string &objectName)
{
    auto it = motions_.find(objectName);
    if (it != motions_.end() && it->second.pTarget)
    {
        Motion &motion = it->second;
        motion.initialPos = motion.pTarget->GetLocalPosition();
        motion.initialRot = motion.pTarget->GetLocalRotation().ToEulerAngles();
        motion.initialScale = motion.pTarget->GetLocalScale();
        motion.hasInitialTransform = true;
    }
}

Vector3 MotionEditor::CatmullRomInterpolation(const std::vector<Vector3> &points, float t)
{
    if (points.size() < 2)
        return Vector3{0, 0, 0};

    if (points.size() == 2)
    {
        return Lerp(points[0], points[1], t);
    }
    if (points.size() == 3)
    {
        if (t < 0.5f)
        {
            return Lerp(points[0], points[1], t * 2.0f);
        }
        else
        {
            return Lerp(points[1], points[2], (t - 0.5f) * 2.0f);
        }
    }

    int numSegments = static_cast<int>(points.size() - 1);
    float segmentT = t * (numSegments - 2);
    int segment = static_cast<int>(segmentT);
    float localT = segmentT - segment;

    // 範囲チェック
    if (segment < 0)
    {
        segment = 0;
        localT = 0.0f;
    }
    if (segment >= numSegments - 2)
    {
        segment = numSegments - 3;
        localT = 1.0f;
    }

    int i0 = segment;
    int i1 = segment + 1;
    int i2 = segment + 2;
    int i3 = segment + 3;

    if (i0 < 0)
        i0 = 0;
    if (i3 >= static_cast<int>(points.size()))
        i3 = static_cast<int>(points.size() - 1);

    Vector3 p0 = points[i0];
    Vector3 p1 = points[i1];
    Vector3 p2 = points[i2];
    Vector3 p3 = points[i3];

    float t2 = localT * localT;
    float t3 = t2 * localT;

    Vector3 result;
    result.x = 0.5f * ((2.0f * p1.x) + (-p0.x + p2.x) * localT +
                       (2.0f * p0.x - 5.0f * p1.x + 4.0f * p2.x - p3.x) * t2 +
                       (-p0.x + 3.0f * p1.x - 3.0f * p2.x + p3.x) * t3);
    result.y = 0.5f * ((2.0f * p1.y) + (-p0.y + p2.y) * localT +
                       (2.0f * p0.y - 5.0f * p1.y + 4.0f * p2.y - p3.y) * t2 +
                       (-p0.y + 3.0f * p1.y - 3.0f * p2.y + p3.y) * t3);
    result.z = 0.5f * ((2.0f * p1.z) + (-p0.z + p2.z) * localT +
                       (2.0f * p0.z - 5.0f * p1.z + 4.0f * p2.z - p3.z) * t2 +
                       (-p0.z + 3.0f * p1.z - 3.0f * p2.z + p3.z) * t3);

    return result;
}

Matrix4x4 MotionEditor::GetParentInverseWorldMatrix(BaseObject *pObject)
{
    if (!pObject || !pObject->GetParent())
    {
        return MakeIdentity4x4();
    }

    // 親のワールド変換行列の逆行列を返す
    Matrix4x4 parentWorldMatrix = pObject->GetParent()->GetWorldTransform()->matWorld_;
    return Inverse(parentWorldMatrix);
}

Vector3 MotionEditor::GetLocalControlPointPosition(BaseObject *pObject, const Vector3 &worldPos)
{
    if (!pObject || !pObject->GetParent())
    {
        return worldPos;
    }

    Matrix4x4 parentInverseMatrix = GetParentInverseWorldMatrix(pObject);
    Vector4 worldPos4 = {worldPos.x, worldPos.y, worldPos.z, 1.0f};
    Vector4 localPos4 = Transformation(worldPos4, parentInverseMatrix);

    return {localPos4.x, localPos4.y, localPos4.z};
}

Vector3 MotionEditor::TransformLocalControlPointToWorld(BaseObject *pObject, const Vector3 &localPos)
{
    if (!pObject || !pObject->GetParent())
    {
        return localPos;
    }

    // 親のワールド行列で変換
    Matrix4x4 parentWorldMatrix = pObject->GetParent()->GetWorldTransform()->matWorld_;
    Vector4 localPos4 = {localPos.x, localPos.y, localPos.z, 1.0f};
    Vector4 worldPos4 = Transformation(localPos4, parentWorldMatrix);

    return {worldPos4.x, worldPos4.y, worldPos4.z};
}

void MotionEditor::Update(float deltaTime)
{
    // 攻撃終了後のインターバルタイマー更新
    for (auto it = attackEndIntervals_.begin(); it != attackEndIntervals_.end();)
    {
        it->second -= deltaTime;
        if (it->second <= 0.0f)
        {
            // インターバル終了、コライダー無効化
            if (it->first)
            {
                auto &colliders = it->first->GetColliders();
                for (auto &collider : colliders)
                {
                    if (collider)
                    {
                        collider->SetEnabled(false);
                    }
                }
            }
            it = attackEndIntervals_.erase(it);
        }
        else
        {
            ++it;
        }
    }

    // モーションの更新
    for (auto &[name, motion] : motions_)
    {
        if (!motion.pTarget || motion.status != MotionStatus::Playing)
        {
            continue;
        }

        // 初回のみ初期状態を記録
        if (!motion.hasInitialTransform)
        {
            motion.initialPos = motion.pTarget->GetLocalPosition();
            motion.initialRot = motion.pTarget->GetLocalRotation().ToEulerAngles();
            motion.initialScale = motion.pTarget->GetLocalScale();
            motion.hasInitialTransform = true;
        }

        motion.currentTime += deltaTime;

        // 終了判定
        if (motion.currentTime >= motion.totalTime)
        {
            motion.currentTime = motion.totalTime;
            motion.status = MotionStatus::Finished;

            if (motion.isTemporary)
            {
                SetAttackEndInterval(motion.pTarget, ATTACK_END_INTERVAL);
                if (motion.returnToOriginal)
                {
                    ReturnToComboStart(motion.pTarget);
                }
            }
            continue;
        }

        // 補間計算
        ApplyPose(motion, motion.currentTime / motion.totalTime);

        // コライダー制御
        bool enable = motion.currentTime >= motion.colliderOnTime && motion.currentTime <= motion.colliderOffTime;
        auto &colliders = motion.pTarget->GetColliders();
        for (auto &collider : colliders)
        {
            if (collider)
            {
                collider->SetEnabled(enable);
            }
        }
    }

    CleanupFinishedTemporaryMotions();
    LineCategoryScope lineScope(LineCategory::Motion);
    DrawControlPoints();
    DrawCatmullRomCurve();
}

void MotionEditor::ApplyPose(Motion &motion, float t)
{
    // t は 0〜1（開始→終了）。位置は曲線か2点の補間、回転と大きさは開始→終了
    if (motion.useCatmullRom && motion.controlPoints.size() >= 4)
    {
        Vector3 localOffset = CatmullRomInterpolation(motion.controlPoints, t);
        motion.pTarget->GetLocalPosition() = motion.basePos + localOffset;
    }
    else
    {
        float easedT = ApplyMotionEasing(motion.easingType, t, 1.0f);
        Vector3 actualStartPos = motion.basePos + motion.startPosOffset;
        Vector3 actualEndPos = motion.basePos + motion.endPosOffset;
        motion.pTarget->GetLocalPosition() = Lerp(actualStartPos, actualEndPos, easedT);
    }

    float easedT = ApplyMotionEasing(motion.easingType, t, 1.0f);
    Quaternion interpolatedRot = Slerp(motion.actualStartRot, motion.actualEndRot, easedT);
    motion.pTarget->GetWorldTransform()->quaternionRotation_ = interpolatedRot;
    motion.pTarget->GetLocalScale() = Lerp(motion.actualStartScale, motion.actualEndScale, easedT);
}

void MotionEditor::PrepareBase(Motion &motion)
{
    // 今の姿勢を基準にして、補間の両端（回転・大きさ）を決める
    motion.basePos = motion.pTarget->GetLocalPosition();
    motion.baseRot = motion.pTarget->GetLocalRotation().ToEulerAngles();
    motion.baseScale = motion.pTarget->GetLocalScale();
    motion.actualStartRot = Quaternion::FromEulerAngles(motion.baseRot + motion.startRotOffset);
    motion.actualEndRot = Quaternion::FromEulerAngles(motion.baseRot + motion.endRotOffset);
    motion.actualStartScale = motion.baseScale + motion.startScaleOffset;
    motion.actualEndScale = motion.baseScale + motion.endScaleOffset;
}

void MotionEditor::Play(const std::string &jsonName)
{
    auto it = motions_.find(jsonName);
    if (it == motions_.end() || !it->second.pTarget)
    {
        return;
    }

    Motion &motion = it->second;
    if (!motion.hasInitialTransform)
    {
        motion.initialPos = motion.pTarget->GetLocalPosition();
        motion.initialRot = motion.pTarget->GetLocalRotation().ToEulerAngles();
        motion.initialScale = motion.pTarget->GetLocalScale();
        motion.hasInitialTransform = true;
    }

    PrepareBase(motion);

    motion.currentTime = 0.0f;
    motion.status = MotionStatus::Playing;
}

bool MotionEditor::PlayFromFile(BaseObject *pTarget, const std::string &fileName, bool returnToOriginal)
{
    if (!pTarget)
    {
        return false;
    }

    std::string tempName = GetTemporaryMotionName(pTarget, fileName);
    DataHandler data("AttackData", fileName);
    Motion &motion = motions_[tempName];

    motion.pTarget = pTarget;
    motion.objectName = tempName;
    motion.isTemporary = true;
    motion.returnToOriginal = returnToOriginal;
    motion.totalTime = data.Load<float>("totalTime", 1.0f);
    motion.colliderOnTime = data.Load("colliderOnTime", 0.3f);
    motion.colliderOffTime = data.Load("colliderOffTime", 0.6f);
    motion.startPosOffset = data.Load<Vector3>("startPosOffset", {});
    motion.endPosOffset = data.Load<Vector3>("endPosOffset", {});
    motion.startRotOffset = data.Load<Vector3>("startRotOffset", {});
    motion.endRotOffset = data.Load<Vector3>("endRotOffset", {});
    motion.startScaleOffset = data.Load<Vector3>("startScaleOffset", {0, 0, 0});
    motion.endScaleOffset = data.Load<Vector3>("endScaleOffset", {0, 0, 0});
    int easingInt = data.Load("easingType", 0);
    motion.easingType = static_cast<MotionEasingType>(easingInt);

    motion.useCatmullRom = data.Load<bool>("useCatmullRom", false);
    int pointCount = data.Load<int>("controlPointCount", 0);
    motion.controlPoints.clear();
    for (int i = 0; i < pointCount; ++i)
    {
        Vector3 point = data.Load<Vector3>("controlPoint" + std::to_string(i), {0, 0, 0});
        motion.controlPoints.push_back(point);
    }

    motion.initialPos = pTarget->GetLocalPosition();
    motion.initialRot = pTarget->GetLocalRotation().ToEulerAngles();
    motion.initialScale = pTarget->GetLocalScale();
    motion.hasInitialTransform = true;

    PrepareBase(motion);

    motion.currentTime = 0.0f;
    motion.status = MotionStatus::Playing;

    return true;
}

void MotionEditor::SetComboStartPosition(BaseObject *pTarget)
{
    if (!pTarget)
        return;

    // コンボ開始位置・回転・スケールを保存
    comboStartPositions_[pTarget] = pTarget->GetLocalPosition();
    comboStartRotations_[pTarget] = pTarget->GetLocalRotation().ToEulerAngles();
    comboStartScales_[pTarget] = pTarget->GetLocalScale();
}

void MotionEditor::ReturnToComboStart(BaseObject *pTarget)
{
    if (!pTarget)
        return;

    auto posIt = comboStartPositions_.find(pTarget);
    auto rotIt = comboStartRotations_.find(pTarget);
    auto scaleIt = comboStartScales_.find(pTarget);

    if (posIt != comboStartPositions_.end() &&
        rotIt != comboStartRotations_.end() &&
        scaleIt != comboStartScales_.end())
    {

        pTarget->GetLocalPosition() = posIt->second;
        pTarget->GetLocalRotation() = Quaternion::FromEulerAngles(rotIt->second);
        pTarget->GetLocalScale() = scaleIt->second;
    }
}

void MotionEditor::ClearComboStartPosition(BaseObject *pTarget)
{
    if (!pTarget)
        return;

    comboStartPositions_.erase(pTarget);
    comboStartRotations_.erase(pTarget);
    comboStartScales_.erase(pTarget);
}

void MotionEditor::ClearAllComboStartPositions()
{
    comboStartPositions_.clear();
    comboStartRotations_.clear();
    comboStartScales_.clear();
}

void MotionEditor::Stop(const std::string &objectName)
{
    auto it = motions_.find(objectName);
    if (it != motions_.end())
    {
        Motion &motion = it->second;
        motion.status = MotionStatus::Stopped;
        motion.currentTime = 0.0f;

        // 初期状態に戻す
        if (motion.hasInitialTransform && motion.pTarget)
        {
            motion.pTarget->GetLocalPosition() = motion.initialPos;
            motion.pTarget->GetLocalRotation() = Quaternion::FromEulerAngles(motion.initialRot);
            motion.pTarget->GetLocalScale() = motion.initialScale;
        }
        if (motion.isTemporary)
        {
            motions_.erase(it);
        }
    }
}

void MotionEditor::StopAll()
{
    auto it = motions_.begin();
    while (it != motions_.end())
    {
        Motion &motion = it->second;
        motion.status = MotionStatus::Stopped;
        motion.currentTime = 0.0f;

        if (motion.hasInitialTransform && motion.pTarget)
        {
            motion.pTarget->GetLocalPosition() = motion.initialPos;
            motion.pTarget->GetLocalRotation() = Quaternion::FromEulerAngles(motion.initialRot);
            motion.pTarget->GetLocalScale() = motion.initialScale;
        }

        if (motion.isTemporary)
        {
            it = motions_.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

bool MotionEditor::IsAttackFinished(BaseObject *pTarget)
{
    if (!pTarget)
        return false;

    for (const auto &[name, motion] : motions_)
    {
        if (motion.pTarget == pTarget && motion.isTemporary)
        {
            return motion.status == MotionStatus::Finished;
        }
    }
    return true;
}

bool MotionEditor::IsAttackFinishedWithInterval(BaseObject *pTarget)
{
    if (!pTarget)
        return false;

    if (!IsAttackFinished(pTarget))
    {
        return false;
    }

    auto it = attackEndIntervals_.find(pTarget);
    if (it != attackEndIntervals_.end())
    {
        return it->second <= 0.0f;
    }
    return true;
}

void MotionEditor::SetAttackEndInterval(BaseObject *pTarget, float interval)
{
    if (!pTarget)
        return;
    attackEndIntervals_[pTarget] = interval;
}

void MotionEditor::ClearAttackEndInterval(BaseObject *pTarget)
{
    if (!pTarget)
        return;
    attackEndIntervals_.erase(pTarget);
}

bool MotionEditor::IsTemporaryMotionFinished(BaseObject *pTarget, const std::string &fileName)
{
    if (!pTarget)
        return false;

    std::string tempName = GetTemporaryMotionName(pTarget, fileName);
    auto it = motions_.find(tempName);
    if (it != motions_.end())
    {
        return it->second.status == MotionStatus::Finished;
    }
    return true;
}

void MotionEditor::DrawControlPoints()
{
    if (selectedName_.empty())
        return;

    Motion &motion = motions_[selectedName_];
    if (!motion.pTarget || !motion.useCatmullRom || motion.controlPoints.empty())
        return;

    LineRenderer *drawLine = LineRenderer::GetInstance();
    const float sphereSize = 0.4f;

    Vector3 basePos = (motion.currentTime == 0.0f) ? motion.pTarget->GetLocalPosition() : motion.basePos;

    for (size_t i = 0; i < motion.controlPoints.size(); ++i)
    {
        Vector3 localPos = basePos + motion.controlPoints[i];
        Vector3 worldPos = TransformLocalControlPointToWorld(motion.pTarget, localPos);

        Vector4 color = {1.0f, 0.0f, 0.0f, 1.0f};
        if (i == 0)
            color = {0.0f, 1.0f, 0.0f, 1.0f};
        else if (i == motion.controlPoints.size() - 1)
            color = {0.0f, 0.0f, 1.0f, 1.0f};

        if (static_cast<int>(i) == selectedControlPoint_)
        {
            color.x = std::min(color.x + 0.5f, 1.0f);
            color.y = std::min(color.y + 0.5f, 1.0f);
            color.z = std::min(color.z + 0.5f, 1.0f);
        }

        drawLine->AddSphere(worldPos, sphereSize, color, 8);
    }
}

void MotionEditor::DrawCatmullRomCurve()
{
    if (selectedName_.empty())
        return;

    Motion &motion = motions_[selectedName_];
    if (!motion.pTarget || !motion.useCatmullRom || motion.controlPoints.size() < 2)
        return;

    LineRenderer *drawLine = LineRenderer::GetInstance();
    const Vector4 curveColor = {1.0f, 0.5f, 0.0f, 1.0f};
    const int resolution = 100;

    Vector3 basePos = (motion.currentTime == 0.0f) ? motion.pTarget->GetLocalPosition() : motion.basePos;

    Vector3 prevWorldPoint = TransformLocalControlPointToWorld(motion.pTarget, basePos + CatmullRomInterpolation(motion.controlPoints, 0.0f));

    for (int i = 1; i <= resolution; ++i)
    {
        float t = static_cast<float>(i) / resolution;
        Vector3 currentWorldPoint = TransformLocalControlPointToWorld(motion.pTarget, basePos + CatmullRomInterpolation(motion.controlPoints, t));
        drawLine->AddLine(prevWorldPoint, currentWorldPoint, curveColor);
        prevWorldPoint = currentWorldPoint;
    }
}

#ifdef USE_IMGUI
void MotionEditor::DrawControlPointPreview(const Motion &motion)
{
    // 制御点が2点未満だと軌跡にならないので出さない
    if (motion.controlPoints.size() < 2)
    {
        return;
    }

    if (!ImGui::CollapsingHeader("軌跡プレビュー##motionpath", ImGuiTreeNodeFlags_DefaultOpen))
    {
        return;
    }
    DimText("ドラッグで視点を回せます（オブジェクトのローカル座標）");

    // 制御点そのもの
    std::vector<float> px, py, pz;
    px.reserve(motion.controlPoints.size());
    py.reserve(motion.controlPoints.size());
    pz.reserve(motion.controlPoints.size());
    for (const Vector3 &p : motion.controlPoints)
    {
        px.push_back(p.x);
        py.push_back(p.y);
        pz.push_back(p.z);
    }

    // 補間後の軌跡。実際に再生されるカーブと同じ関数を通して描く
    // （UI 用に別の近似を書くと、見た目と実挙動がずれる）
    std::vector<float> cx, cy, cz;
    const int kCurveSamples = 96;
    if (motion.useCatmullRom && motion.controlPoints.size() >= 4)
    {
        cx.reserve(kCurveSamples);
        cy.reserve(kCurveSamples);
        cz.reserve(kCurveSamples);
        for (int i = 0; i < kCurveSamples; ++i)
        {
            const float t = static_cast<float>(i) / static_cast<float>(kCurveSamples - 1);
            const Vector3 v = CatmullRomInterpolation(motion.controlPoints, t);
            cx.push_back(v.x);
            cy.push_back(v.y);
            cz.push_back(v.z);
        }
    }

    if (ImPlot3D::BeginPlot("##motionpath3d", ImVec2(-1, 220)))
    {
        ImPlot3D::SetupAxes("X", "Y", "Z");

        if (!cx.empty())
        {
            ImPlot3D::PlotLine("軌跡", cx.data(), cy.data(), cz.data(), static_cast<int>(cx.size()),
                               ImPlot3DSpec(ImPlot3DProp_LineColor, ImVec4(0.53f, 0.64f, 0.75f, 1.0f),
                                            ImPlot3DProp_LineWeight, 2.0f));
        }

        // 制御点は順番が分かるよう線でもつなぐ
        ImPlot3D::PlotLine("制御点の並び", px.data(), py.data(), pz.data(), static_cast<int>(px.size()),
                           ImPlot3DSpec(ImPlot3DProp_LineColor, ImVec4(0.45f, 0.45f, 0.50f, 0.6f),
                                        ImPlot3DProp_LineWeight, 1.0f));
        ImPlot3D::PlotScatter("制御点", px.data(), py.data(), pz.data(), static_cast<int>(px.size()),
                              ImPlot3DSpec(ImPlot3DProp_Marker, ImPlot3DMarker_Circle,
                                           ImPlot3DProp_MarkerSize, 4.0f,
                                           ImPlot3DProp_MarkerFillColor, ImVec4(0.80f, 0.72f, 0.42f, 1.0f)));

        // 選択中の制御点だけ大きく出して、リストとの対応を分かるようにする
        if (selectedControlPoint_ >= 0 && selectedControlPoint_ < static_cast<int>(motion.controlPoints.size()))
        {
            const Vector3 &sel = motion.controlPoints[selectedControlPoint_];
            const float sx[1] = {sel.x};
            const float sy[1] = {sel.y};
            const float sz[1] = {sel.z};
            ImPlot3D::PlotScatter("選択中", sx, sy, sz, 1,
                                  ImPlot3DSpec(ImPlot3DProp_Marker, ImPlot3DMarker_Circle,
                                               ImPlot3DProp_MarkerSize, 7.0f,
                                               ImPlot3DProp_MarkerFillColor, ImVec4(0.80f, 0.46f, 0.46f, 1.0f)));
        }

        ImPlot3D::EndPlot();
    }
}
#endif // USE_IMGUI

void MotionEditor::DrawImGui()
{
#ifdef USE_IMGUI
    // 同じフレームに2か所から呼ばれる（専用窓とシーン設定窓）ことがあるので、2回目は案内だけ出す
    const int frame = ImGui::GetFrameCount();
    if (lastDrawFrame_ == frame)
    {
        DimText("モーションは「モーションエディター」窓で編集します（表示 → モーションエディター）");
        return;
    }
    lastDrawFrame_ = frame;

    g_motionUndoTracker.Begin([this] { return CaptureUndoState(); });

    // ---- 対象（登録されているオブジェクト）----
    SectionHeader("[ 対象 ]", DebugTheme::kAccentBlue);
    if (motions_.empty())
    {
        DimText("登録されているオブジェクトがありません（コードから MotionEditor::Register で登録します）");
        g_motionUndoTracker.End("モーション編集", [this] { return CaptureUndoState(); },
                                [](const nlohmann::json &s) { MotionEditor::GetInstance()->RestoreUndoState(s); });
        return;
    }
    {
        std::vector<std::string> names;
        for (const auto &[name, motion] : motions_)
        {
            if (!motion.isTemporary)
                names.push_back(name);
        }
        std::sort(names.begin(), names.end());
        if (selectedName_.empty() || motions_.find(selectedName_) == motions_.end())
        {
            selectedName_ = names.empty() ? std::string() : names.front();
        }
        ImGui::BeginChild("##motionTargets", ImVec2(0.0f, 4.5f * ImGui::GetTextLineHeightWithSpacing()), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeY);
        for (const std::string &name : names)
        {
            const Motion &motion = motions_[name];
            const bool playing = motion.status == MotionStatus::Playing;
            ImGui::TextColored(playing ? DebugTheme::kAccentGreen : DebugTheme::kTextDim, playing ? ICON_FA_PLAY : ICON_FA_CUBE);
            ImGui::SameLine();
            if (ImGui::Selectable(name.c_str(), selectedName_ == name))
            {
                selectedName_ = name;
                selectedControlPoint_ = -1;
            }
        }
        ImGui::EndChild();
    }
    if (selectedName_.empty())
    {
        g_motionUndoTracker.End("モーション編集", [this] { return CaptureUndoState(); },
                                [](const nlohmann::json &s) { MotionEditor::GetInstance()->RestoreUndoState(s); });
        return;
    }
    Motion &m = motions_[selectedName_];

    // ---- ファイル（AttackData）----
    SectionHeader("[ ファイル ]", DebugTheme::kAccentPurple);
    {
        // 保存済みのモーション一覧（jsons/AttackData/*.json）
        std::vector<std::string> files;
        std::error_code error;
        const std::filesystem::path folder = std::filesystem::path(AssetPath::JsonRoot()) / "AttackData";
        for (const auto &entry : std::filesystem::directory_iterator(folder, error))
        {
            if (entry.is_regular_file() && entry.path().extension() == ".json")
                files.push_back(entry.path().stem().string());
        }
        std::sort(files.begin(), files.end());
        ImGui::SetNextItemWidth(-190.0f);
        if (ImGui::BeginCombo("##motionFile", jsonName_.empty() ? "（読み込むファイルを選ぶ）" : jsonName_.c_str()))
        {
            for (const std::string &file : files)
            {
                if (ImGui::Selectable(file.c_str(), jsonName_ == file))
                    jsonName_ = file;
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(jsonName_.empty() || std::find(files.begin(), files.end(), jsonName_) == files.end());
        if (NeutralButton(ICON_FA_UPLOAD " 読み込む"))
        {
            // 読み込むのは中身だけ。対象・基準の姿勢はそのまま
            Motion loaded = Load(jsonName_);
            loaded.pTarget = m.pTarget;
            loaded.objectName = m.objectName;
            loaded.initialPos = m.initialPos;
            loaded.initialRot = m.initialRot;
            loaded.initialScale = m.initialScale;
            loaded.hasInitialTransform = m.hasInitialTransform;
            m = loaded;
            selectedControlPoint_ = -1;
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(jsonName_.empty());
        if (PrimaryButton(ICON_FA_SAVE " 保存"))
        {
            Save(jsonName_);
        }
        ImGui::EndDisabled();
        ImGui::SetNextItemWidth(-190.0f);
        ImGui::InputTextWithHint("##motionSaveName", "保存する名前（新しい名前なら新規）", &jsonName_);
        ImGui::SameLine();
        ImGui::TextDisabled("コードからは PlayFromFile(対象, \"名前\")");
    }

    // ---- 再生と時間 ----
    SectionHeader("[ 再生 ]", DebugTheme::kAccentGreen);
    {
        const bool playing = m.status == MotionStatus::Playing;
        if (playing ? ConfirmButton(ICON_FA_PAUSE " 再生中") : PrimaryButton(ICON_FA_PLAY " 再生"))
        {
            if (!playing)
            {
                // 前の再生やつまみで動かした姿勢から始めないよう、基準の姿勢に戻してから再生する
                if (m.hasInitialTransform && m.pTarget)
                {
                    m.pTarget->GetLocalPosition() = m.initialPos;
                    m.pTarget->GetLocalRotation() = Quaternion::FromEulerAngles(m.initialRot);
                    m.pTarget->GetLocalScale() = m.initialScale;
                }
                Play(selectedName_);
            }
        }
        ImGui::SameLine();
        if (NeutralButton(ICON_FA_STOP " 止めて戻す"))
            Stop(selectedName_);
        ImGui::SetItemTooltip("再生を止めて、基準の姿勢に戻します");
        ImGui::SameLine();
        if (NeutralButton("全部止める"))
            StopAll();
        ImGui::SameLine();
        ImGui::SetNextItemWidth(90.0f);
        ImGui::DragFloat("長さ", &m.totalTime, 0.01f, 0.05f, 30.0f, "%.2f 秒");
        FloatNContextMenu("##totalMenu", &m.totalTime, 1);

        DrawTimelineStrip(m);
    }

    // ---- 基準の姿勢 ----
    SectionHeader("[ 基準の姿勢 ]", DebugTheme::kAccentCyan);
    DimText("オフセットは基準の姿勢からのずれです。ギズモで対象を動かして「今の姿勢 → 開始/終了」を押すと数値を打たずに作れます");
    if (NeutralButton(ICON_FA_MAP_PIN " 今の姿勢を基準にする"))
    {
        ResetInitialPosition(selectedName_);
        ImGuiNotification::Post("基準の姿勢を今の姿勢にしました", {0.42f, 0.66f, 0.68f, 1.0f});
    }
    ImGui::SetItemTooltip("再生の始まりと「止めて戻す」の戻り先になります");

    // ---- 動き方 ----
    SectionHeader("[ 動き方 ]", DebugTheme::kAccentOrange);
    {
        int mode = m.useCatmullRom ? 1 : 0;
        if (ImGui::RadioButton("2点の補間（開始 → 終了）", &mode, 0))
            m.useCatmullRom = false;
        ImGui::SameLine();
        if (ImGui::RadioButton("曲線（制御点を通る）", &mode, 1))
            m.useCatmullRom = true;
        ImGui::SetItemTooltip("位置だけ制御点を通る曲線（Catmull-Rom）で動かします。回転と大きさは開始 → 終了のまま");
    }

    // 今の姿勢 − 基準 をオフセットとして取り込む
    auto captureOffsets = [&](Vector3 &pos, Vector3 &rot, Vector3 &scale) {
        if (!m.pTarget)
            return;
        if (!m.hasInitialTransform)
            ResetInitialPosition(selectedName_);
        pos = m.pTarget->GetLocalPosition() - m.initialPos;
        rot = m.pTarget->GetLocalRotation().ToEulerAngles() - m.initialRot;
        scale = m.pTarget->GetLocalScale() - m.initialScale;
    };
    constexpr float kToDeg = 180.0f / std::numbers::pi_v<float>;
    static const float kZero3[3] = {0.0f, 0.0f, 0.0f};
    auto drawOffsetRow = [&](const char *title, Vector3 &pos, Vector3 &rot, Vector3 &scale, const char *id, bool showPosition) {
        ImGui::PushID(id);
        CaptionText(title);
        ImGui::SameLine();
        if (ImGui::SmallButton(ICON_FA_CAMERA " 今の姿勢から"))
            captureOffsets(pos, rot, scale);
        ImGui::SetItemTooltip("対象の今の姿勢と基準の差を取り込みます");
        ImGui::PushItemWidth(-90.0f);
        if (showPosition)
        {
            ImGui::DragFloat3("位置", &pos.x, 0.05f);
            FloatNContextMenu("##posMenu", &pos.x, 3, kZero3);
        }
        float degrees[3] = {rot.x * kToDeg, rot.y * kToDeg, rot.z * kToDeg};
        bool rotChanged = ImGui::DragFloat3("回転(度)", degrees, 0.5f);
        rotChanged |= FloatNContextMenu("##rotMenu", degrees, 3, kZero3);
        if (rotChanged)
            rot = {degrees[0] / kToDeg, degrees[1] / kToDeg, degrees[2] / kToDeg};
        ImGui::DragFloat3("大きさ(差)", &scale.x, 0.01f);
        FloatNContextMenu("##sclMenu", &scale.x, 3, kZero3);
        ImGui::SetItemTooltip("基準の大きさに足す量（0 で変えない）");
        ImGui::PopItemWidth();
        ImGui::PopID();
    };

    if (!m.useCatmullRom)
    {
        drawOffsetRow("開始", m.startPosOffset, m.startRotOffset, m.startScaleOffset, "start", true);
        drawOffsetRow("終了", m.endPosOffset, m.endRotOffset, m.endScaleOffset, "end", true);
    }
    else
    {
        // 曲線: 位置は制御点、回転と大きさは開始 → 終了
        CaptionText("制御点（基準の位置からのずれ）");
        if (m.controlPoints.size() < 4)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kAccentOrange);
            ImGui::TextWrapped("曲線にするには制御点が4つ以上いります（今 %d 個。足りない間は開始 → 終了で動きます）",
                               static_cast<int>(m.controlPoints.size()));
            ImGui::PopStyleColor();
        }
        if (PrimaryButton(ICON_FA_PLUS " 点を足す"))
        {
            m.controlPoints.push_back(m.controlPoints.empty() ? Vector3{0.0f, 0.0f, 0.0f} : m.controlPoints.back());
            selectedControlPoint_ = static_cast<int>(m.controlPoints.size()) - 1;
        }
        ImGui::SameLine();
        if (NeutralButton(ICON_FA_CAMERA " 今の位置で足す") && m.pTarget)
        {
            if (!m.hasInitialTransform)
                ResetInitialPosition(selectedName_);
            m.controlPoints.push_back(m.pTarget->GetLocalPosition() - m.initialPos);
            selectedControlPoint_ = static_cast<int>(m.controlPoints.size()) - 1;
        }
        ImGui::SetItemTooltip("ギズモで対象を置いてから押すと、その位置を点にします");

        int pendingRemove = -1;
        int moveFrom = -1;
        int moveTo = -1;
        for (int i = 0; i < static_cast<int>(m.controlPoints.size()); ++i)
        {
            ImGui::PushID(i);
            const char *marker = (i == 0) ? "始点" : ((i == static_cast<int>(m.controlPoints.size()) - 1) ? "終点" : "");
            const std::string label = std::format("{}. ({:.2f}, {:.2f}, {:.2f}) {}", i, m.controlPoints[i].x, m.controlPoints[i].y,
                                                  m.controlPoints[i].z, marker);
            const float buttons = ImGui::GetFrameHeight() * 3.0f + ImGui::GetStyle().ItemSpacing.x * 3.0f;
            if (ImGui::Selectable(label.c_str(), selectedControlPoint_ == i, ImGuiSelectableFlags_AllowOverlap,
                                  ImVec2(ImGui::GetContentRegionAvail().x - buttons, 0.0f)))
                selectedControlPoint_ = i;
            ImGui::SameLine();
            ScopedButtonColors ghost(DebugTheme::kButtonGhost, DebugTheme::kButtonGhostHover);
            const float size = ImGui::GetFrameHeight();
            if (ImGui::Button(ICON_FA_ARROW_UP, ImVec2(size, size)) && i > 0)
            {
                moveFrom = i;
                moveTo = i - 1;
            }
            ImGui::SameLine();
            if (ImGui::Button(ICON_FA_ARROW_DOWN, ImVec2(size, size)) && i + 1 < static_cast<int>(m.controlPoints.size()))
            {
                moveFrom = i;
                moveTo = i + 1;
            }
            ImGui::SameLine();
            if (ImGui::Button(ICON_FA_TIMES, ImVec2(size, size)))
                pendingRemove = i;
            ImGui::PopID();
        }
        if (moveFrom >= 0)
        {
            std::swap(m.controlPoints[moveFrom], m.controlPoints[moveTo]);
            selectedControlPoint_ = moveTo;
        }
        if (pendingRemove >= 0)
        {
            m.controlPoints.erase(m.controlPoints.begin() + pendingRemove);
            selectedControlPoint_ = -1;
        }
        if (selectedControlPoint_ >= 0 && selectedControlPoint_ < static_cast<int>(m.controlPoints.size()))
        {
            ImGui::SetNextItemWidth(-90.0f);
            ImGui::DragFloat3("選択中の点", &m.controlPoints[selectedControlPoint_].x, 0.05f);
            FloatNContextMenu("##cpMenu", &m.controlPoints[selectedControlPoint_].x, 3, kZero3);
        }
        DrawControlPointPreview(m);

        ImGui::Spacing();
        // 曲線のときも回転と大きさは開始 → 終了で動く（位置は制御点が決めるので出さない）
        drawOffsetRow("回転・大きさ（開始）", m.startPosOffset, m.startRotOffset, m.startScaleOffset, "cstart", false);
        drawOffsetRow("回転・大きさ（終了）", m.endPosOffset, m.endRotOffset, m.endScaleOffset, "cend", false);
    }

    // ---- イージング ----
    SectionHeader("[ 動きの緩急（イージング）]", DebugTheme::kAccentYellow);
    DrawEasingPreview(m);

    g_motionUndoTracker.End("モーション編集", [this] { return CaptureUndoState(); },
                            [](const nlohmann::json &s) { MotionEditor::GetInstance()->RestoreUndoState(s); });
#endif
}

#ifdef USE_IMGUI
void MotionEditor::DrawTimelineStrip(Motion &m)
{
    // 横長の帯: 再生位置（つまんで動かすとその時点の姿勢を見られる）と、コライダーが有効な区間（両端をつまんで調整）
    const float width = ImGui::GetContentRegionAvail().x;
    const float height = 40.0f;
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const ImVec2 max = ImVec2(min.x + width, min.y + height);
    ImGui::InvisibleButton("##motionStrip", ImVec2(width, height));
    const bool hovered = ImGui::IsItemHovered();
    const bool active = ImGui::IsItemActive();
    ImDrawList *drawList = ImGui::GetWindowDrawList();

    const float total = (std::max)(m.totalTime, 0.001f);
    auto timeToX = [&](float t) { return min.x + std::clamp(t / total, 0.0f, 1.0f) * width; };
    auto xToTime = [&](float x) { return std::clamp((x - min.x) / width, 0.0f, 1.0f) * total; };

    drawList->AddRectFilled(min, max, ImGui::GetColorU32(ImGuiCol_FrameBg), 4.0f);
    // 0.1 秒ごとの目盛り
    for (float t = 0.0f; t <= total + 1e-4f; t += 0.1f)
    {
        const float x = timeToX(t);
        const bool major = std::fmod(t + 1e-4f, 0.5f) < 2e-4f;
        drawList->AddLine(ImVec2(x, max.y - (major ? 10.0f : 5.0f)), ImVec2(x, max.y), IM_COL32(120, 124, 136, 200));
    }
    // コライダー区間
    const float onX = timeToX(m.colliderOnTime);
    const float offX = timeToX(m.colliderOffTime);
    const ImVec4 hitColor = DebugTheme::kAccentRed;
    drawList->AddRectFilled(ImVec2(onX, min.y + 4.0f), ImVec2(offX, max.y - 12.0f),
                            ImGui::ColorConvertFloat4ToU32(ImVec4(hitColor.x, hitColor.y, hitColor.z, 0.35f)), 3.0f);
    drawList->AddLine(ImVec2(onX, min.y + 2.0f), ImVec2(onX, max.y - 10.0f), ImGui::ColorConvertFloat4ToU32(hitColor), 3.0f);
    drawList->AddLine(ImVec2(offX, min.y + 2.0f), ImVec2(offX, max.y - 10.0f), ImGui::ColorConvertFloat4ToU32(hitColor), 3.0f);
    drawList->AddText(ImVec2(onX + 4.0f, min.y + 6.0f), IM_COL32(240, 220, 220, 230), "当たり判定");
    // 再生位置
    const float playX = timeToX(m.currentTime);
    drawList->AddLine(ImVec2(playX, min.y), ImVec2(playX, max.y), IM_COL32(240, 200, 100, 255), 2.0f);
    drawList->AddTriangleFilled(ImVec2(playX - 5.0f, min.y), ImVec2(playX + 5.0f, min.y), ImVec2(playX, min.y + 7.0f), IM_COL32(240, 200, 100, 255));

    // つまむ物を押した瞬間に決める（コライダーの端を優先）
    const ImVec2 mouse = ImGui::GetMousePos();
    if (ImGui::IsItemActivated())
    {
        if (std::fabs(mouse.x - onX) <= 6.0f)
            stripDrag_ = 1;
        else if (std::fabs(mouse.x - offX) <= 6.0f)
            stripDrag_ = 2;
        else
            stripDrag_ = 3;
    }
    if (hovered && (std::fabs(mouse.x - onX) <= 6.0f || std::fabs(mouse.x - offX) <= 6.0f))
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    if (active)
    {
        const float t = std::round(xToTime(mouse.x) * 100.0f) / 100.0f; // 0.01 秒刻み
        if (stripDrag_ == 1)
            m.colliderOnTime = (std::min)(t, m.colliderOffTime);
        else if (stripDrag_ == 2)
            m.colliderOffTime = (std::max)(t, m.colliderOnTime);
        else if (m.status != MotionStatus::Playing)
            PreviewAt(m, t);
    }
    else
    {
        stripDrag_ = 0;
    }
    if (hovered && !active)
        ImGui::SetTooltip("つまんで動かすと、その時点の姿勢を見られます\n赤い区間の両端をつまむと当たり判定の時間を変えられます");

    ImGui::Text("%.2f / %.2f 秒", m.currentTime, m.totalTime);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(90.0f);
    ImGui::DragFloat("当たり ON", &m.colliderOnTime, 0.01f, 0.0f, m.totalTime, "%.2f 秒");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(90.0f);
    ImGui::DragFloat("OFF", &m.colliderOffTime, 0.01f, 0.0f, m.totalTime, "%.2f 秒");
}

void MotionEditor::DrawEasingPreview(Motion &m)
{
    ImGui::SetNextItemWidth(220.0f);
    if (ImGui::BeginCombo("緩急", kMotionEasingNames[static_cast<int>(m.easingType)]))
    {
        for (int i = 0; i < static_cast<int>(std::size(kMotionEasingNames)); ++i)
        {
            if (ImGui::Selectable(kMotionEasingNames[i], static_cast<int>(m.easingType) == i))
                m.easingType = static_cast<MotionEasingType>(i);
        }
        ImGui::EndCombo();
    }
    ImGui::SetItemTooltip("開始 → 終了の進み方。回転と大きさ、2点の補間の位置に効きます");

    // 進み具合のグラフ（横: 時間 / 縦: 開始→終了の割合）と、今の再生位置
    constexpr int kSamples = 64;
    float xs[kSamples];
    float ys[kSamples];
    for (int i = 0; i < kSamples; ++i)
    {
        const float t = static_cast<float>(i) / static_cast<float>(kSamples - 1);
        xs[i] = t;
        ys[i] = ApplyMotionEasing(m.easingType, t, 1.0f);
    }
    if (ImPlot::BeginPlot("##motionEasing", ImVec2(-1.0f, 120.0f), ImPlotFlags_NoMenus | ImPlotFlags_NoBoxSelect | ImPlotFlags_NoMouseText | ImPlotFlags_NoLegend))
    {
        ImPlot::SetupAxes("時間", "開始→終了", ImPlotAxisFlags_NoHighlight, ImPlotAxisFlags_NoHighlight);
        ImPlot::SetupAxesLimits(0.0, 1.0, -0.3, 1.3, ImPlotCond_Always);
        ImPlot::SetNextLineStyle(DebugTheme::kAccentYellow, 2.0f);
        ImPlot::PlotLine("##curve", xs, ys, kSamples);
        const float now = (m.totalTime > 0.0f) ? std::clamp(m.currentTime / m.totalTime, 0.0f, 1.0f) : 0.0f;
        const float nowY = ApplyMotionEasing(m.easingType, now, 1.0f);
        ImPlot::SetNextMarkerStyle(ImPlotMarker_Circle, 5.0f, DebugTheme::kAccentRed);
        ImPlot::PlotScatter("##now", &now, &nowY, 1);
        ImPlot::EndPlot();
    }
}

void MotionEditor::PreviewAt(Motion &m, float time)
{
    // 基準の姿勢から、指定の時刻の姿勢を作って当てる（再生と同じ式）
    if (!m.pTarget)
        return;
    if (!m.hasInitialTransform)
        ResetInitialPosition(m.objectName);
    m.pTarget->GetLocalPosition() = m.initialPos;
    m.pTarget->GetLocalRotation() = Quaternion::FromEulerAngles(m.initialRot);
    m.pTarget->GetLocalScale() = m.initialScale;
    PrepareBase(m);
    m.currentTime = std::clamp(time, 0.0f, m.totalTime);
    ApplyPose(m, (m.totalTime > 0.0f) ? m.currentTime / m.totalTime : 0.0f);
}

nlohmann::json MotionEditor::CaptureUndoState() const
{
    nlohmann::json state = nlohmann::json::object();
    for (const auto &[name, m] : motions_)
    {
        if (m.isTemporary)
            continue;
        nlohmann::json j;
        j["totalTime"] = m.totalTime;
        j["colliderOn"] = m.colliderOnTime;
        j["colliderOff"] = m.colliderOffTime;
        j["startPos"] = m.startPosOffset;
        j["endPos"] = m.endPosOffset;
        j["startRot"] = m.startRotOffset;
        j["endRot"] = m.endRotOffset;
        j["startScale"] = m.startScaleOffset;
        j["endScale"] = m.endScaleOffset;
        j["easing"] = static_cast<int>(m.easingType);
        j["curve"] = m.useCatmullRom;
        nlohmann::json points = nlohmann::json::array();
        for (const Vector3 &p : m.controlPoints)
            points.push_back(p);
        j["points"] = points;
        state[name] = j;
    }
    return state;
}

void MotionEditor::RestoreUndoState(const nlohmann::json &state)
{
    for (auto it = state.begin(); it != state.end(); ++it)
    {
        auto found = motions_.find(it.key());
        if (found == motions_.end())
            continue;
        Motion &m = found->second;
        const nlohmann::json &j = it.value();
        m.totalTime = j.value("totalTime", m.totalTime);
        m.colliderOnTime = j.value("colliderOn", m.colliderOnTime);
        m.colliderOffTime = j.value("colliderOff", m.colliderOffTime);
        m.startPosOffset = j.value("startPos", m.startPosOffset);
        m.endPosOffset = j.value("endPos", m.endPosOffset);
        m.startRotOffset = j.value("startRot", m.startRotOffset);
        m.endRotOffset = j.value("endRot", m.endRotOffset);
        m.startScaleOffset = j.value("startScale", m.startScaleOffset);
        m.endScaleOffset = j.value("endScale", m.endScaleOffset);
        m.easingType = static_cast<MotionEasingType>(j.value("easing", static_cast<int>(m.easingType)));
        m.useCatmullRom = j.value("curve", m.useCatmullRom);
        m.controlPoints.clear();
        if (j.contains("points"))
        {
            for (const nlohmann::json &p : j["points"])
                m.controlPoints.push_back(p.get<Vector3>());
        }
    }
    selectedControlPoint_ = -1;
}
#endif // USE_IMGUI

void MotionEditor::Save(const std::string &fileName)
{
    DataHandler data("AttackData", fileName);
    Motion &m = motions_[selectedName_];

    data.Save("totalTime", m.totalTime);
    data.Save("colliderOnTime", m.colliderOnTime);
    data.Save("colliderOffTime", m.colliderOffTime);
    data.Save("startPosOffset", m.startPosOffset);
    data.Save("endPosOffset", m.endPosOffset);
    data.Save("startRotOffset", m.startRotOffset);
    data.Save("endRotOffset", m.endRotOffset);
    data.Save("startScaleOffset", m.startScaleOffset);
    data.Save("endScaleOffset", m.endScaleOffset);
    data.Save("easingType", static_cast<int>(m.easingType));
    data.Save("useCatmullRom", m.useCatmullRom);
    data.Save("controlPointCount", static_cast<int>(m.controlPoints.size()));
    for (int i = 0; i < static_cast<int>(m.controlPoints.size()); ++i)
    {
        data.Save("controlPoint" + std::to_string(i), m.controlPoints[i]);
    }
    ImGuiNotification::Post("モーションデータを保存しました: " + fileName, {0.2f, 0.8f, 0.2f, 1.0f});
}

Motion MotionEditor::Load(const std::string &fileName)
{
    DataHandler data("AttackData", fileName);
    Motion m;
    m.totalTime = data.Load("totalTime", m.totalTime);
    m.colliderOnTime = data.Load("colliderOnTime", m.colliderOnTime);
    m.colliderOffTime = data.Load("colliderOffTime", m.colliderOffTime);
    m.startPosOffset = data.Load("startPosOffset", m.startPosOffset);
    m.endPosOffset = data.Load("endPosOffset", m.endPosOffset);
    m.startRotOffset = data.Load("startRotOffset", m.startRotOffset);
    m.endRotOffset = data.Load("endRotOffset", m.endRotOffset);
    m.startScaleOffset = data.Load("startScaleOffset", m.startScaleOffset);
    m.endScaleOffset = data.Load("endScaleOffset", m.endScaleOffset);
    int easingInt = data.Load("easingType", static_cast<int>(m.easingType));
    m.easingType = static_cast<MotionEasingType>(easingInt);
    m.useCatmullRom = data.Load("useCatmullRom", false);
    int pointCount = data.Load("controlPointCount", 0);
    m.controlPoints.clear();
    for (int i = 0; i < pointCount; ++i)
    {
        Vector3 point = data.Load<Vector3>("controlPoint" + std::to_string(i), {0, 0, 0});
        m.controlPoints.push_back(point);
    }
    ImGuiNotification::Post("モーションデータを読み込みました: " + fileName, {0.2f, 0.8f, 0.8f, 1.0f});
    return m;
}
} // namespace Hagine
