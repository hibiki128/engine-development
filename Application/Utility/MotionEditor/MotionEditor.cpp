#include "MotionEditor.h"
#include "Engine/Utility/Debug/ImGui/ImGuiNotification.h"
#ifdef _DEBUG
#include "imgui.h"
#endif // _DEBUG
#include "myMath.h"
#include <Line/DrawLine3D.h>

using namespace Hagine;
const float MotionEditor::ATTACK_END_INTERVAL = 0.1f;

// イージング適用関数
float ApplyMotionEasing(MotionEasingType type, float t, float total) {
    switch (type) {
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

void MotionEditor::Finalize() {
    motions_.clear();
    comboStartPositions_.clear();
    comboStartRotations_.clear();
    comboStartScales_.clear();
    attackEndIntervals_.clear();
}

void MotionEditor::Register(BaseObject *object) {
    if (!object)
        return;

    std::string name = object->GetName();

    // 既に登録済みかチェック
    if (motions_.find(name) != motions_.end()) {
        return;
    }

    // 新しくモーションを登録
    motions_[name] = Motion();
    motions_[name].target = object;
    motions_[name].objectName = name;
    ImGuiNotification::Post("モーションエディタに登録しました: " + name, {0.4f, 0.8f, 1.0f, 1.0f});
}

void MotionEditor::CleanupFinishedTemporaryMotions() {
    auto it = motions_.begin();
    while (it != motions_.end()) {
        // 終了した一時モーションを削除
        if (it->second.isTemporary && it->second.status == MotionStatus::Finished) {
            it = motions_.erase(it);
        } else {
            ++it;
        }
    }
}

Vector3 MotionEditor::TransformLocalToWorld(const Vector3 &localOffset, const Matrix4x4 &worldMatrix) {
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

MotionStatus MotionEditor::GetMotionStatus(const std::string &objectName) {
    auto it = motions_.find(objectName);
    if (it == motions_.end()) {
        return MotionStatus::Stopped;
    }
    return it->second.status;
}

bool MotionEditor::IsPlaying(const std::string &objectName) {
    return GetMotionStatus(objectName) == MotionStatus::Playing;
}

bool MotionEditor::IsFinished(const std::string &objectName) {
    return GetMotionStatus(objectName) == MotionStatus::Finished;
}

std::string MotionEditor::GetTemporaryMotionName(BaseObject *target, const std::string &fileName) {
    return "temp_" + target->GetName() + "_" + fileName;
}

void MotionEditor::ResetInitialPosition(const std::string &objectName) {
    auto it = motions_.find(objectName);
    if (it != motions_.end() && it->second.target) {
        Motion &motion = it->second;
        motion.initialPos = motion.target->GetLocalPosition();
        motion.initialRot = motion.target->GetLocalRotation().ToEulerAngles();
        motion.initialScale = motion.target->GetLocalScale();
        motion.hasInitialTransform = true;
    }
}

Vector3 MotionEditor::CatmullRomInterpolation(const std::vector<Vector3> &points, float t) {
    if (points.size() < 2)
        return Vector3{0, 0, 0};

    if (points.size() == 2) {
        return Lerp(points[0], points[1], t);
    }
    if (points.size() == 3) {
        if (t < 0.5f) {
            return Lerp(points[0], points[1], t * 2.0f);
        } else {
            return Lerp(points[1], points[2], (t - 0.5f) * 2.0f);
        }
    }

    int numSegments = static_cast<int>(points.size() - 1);
    float segmentT = t * (numSegments - 2);
    int segment = (int)segmentT;
    float localT = segmentT - segment;

    // 範囲チェック
    if (segment < 0) {
        segment = 0;
        localT = 0.0f;
    }
    if (segment >= numSegments - 2) {
        segment = numSegments - 3;
        localT = 1.0f;
    }

    int i0 = segment;
    int i1 = segment + 1;
    int i2 = segment + 2;
    int i3 = segment + 3;

    if (i0 < 0)
        i0 = 0;
    if (i3 >= (int)points.size())
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

Matrix4x4 MotionEditor::GetParentInverseWorldMatrix(BaseObject *object) {
    if (!object || !object->GetParent()) {
        return MakeIdentity4x4();
    }

    // 親のワールド変換行列の逆行列を返す
    Matrix4x4 parentWorldMatrix = object->GetParent()->GetWorldTransform()->matWorld_;
    return Inverse(parentWorldMatrix);
}

Vector3 MotionEditor::GetLocalControlPointPosition(BaseObject *object, const Vector3 &worldPos) {
    if (!object || !object->GetParent()) {
        return worldPos;
    }

    Matrix4x4 parentInverseMatrix = GetParentInverseWorldMatrix(object);
    Vector4 worldPos4 = {worldPos.x, worldPos.y, worldPos.z, 1.0f};
    Vector4 localPos4 = Transformation(worldPos4, parentInverseMatrix);

    return {localPos4.x, localPos4.y, localPos4.z};
}

Vector3 MotionEditor::TransformLocalControlPointToWorld(BaseObject *object, const Vector3 &localPos) {
    if (!object || !object->GetParent()) {
        return localPos;
    }

    // 親のワールド行列で変換
    Matrix4x4 parentWorldMatrix = object->GetParent()->GetWorldTransform()->matWorld_;
    Vector4 localPos4 = {localPos.x, localPos.y, localPos.z, 1.0f};
    Vector4 worldPos4 = Transformation(localPos4, parentWorldMatrix);

    return {worldPos4.x, worldPos4.y, worldPos4.z};
}

void MotionEditor::Update(float deltaTime) {
    // 攻撃終了後のインターバルタイマー更新
    for (auto it = attackEndIntervals_.begin(); it != attackEndIntervals_.end();) {
        it->second -= deltaTime;
        if (it->second <= 0.0f) {
            // インターバル終了、コライダー無効化
            if (it->first) {
                auto &colliders = it->first->GetColliders();
                for (auto &collider : colliders) {
                    if (collider) {
                        collider->SetEnabled(false);
                    }
                }
            }
            it = attackEndIntervals_.erase(it);
        } else {
            ++it;
        }
    }

    // モーションの更新
    for (auto &[name, motion] : motions_) {
        if (!motion.target || motion.status != MotionStatus::Playing) {
            continue;
        }

        // 初回のみ初期状態を記録
        if (!motion.hasInitialTransform) {
            motion.initialPos = motion.target->GetLocalPosition();
            motion.initialRot = motion.target->GetLocalRotation().ToEulerAngles();
            motion.initialScale = motion.target->GetLocalScale();
            motion.hasInitialTransform = true;
        }

        motion.currentTime += deltaTime;

        // 終了判定
        if (motion.currentTime >= motion.totalTime) {
            motion.currentTime = motion.totalTime;
            motion.status = MotionStatus::Finished;

            if (motion.isTemporary) {
                SetAttackEndInterval(motion.target, ATTACK_END_INTERVAL);
                if (motion.returnToOriginal) {
                    ReturnToComboStart(motion.target);
                }
            }
            continue;
        }

        // 補間計算
        float t = motion.currentTime / motion.totalTime;
        if (motion.useCatmullRom && motion.controlPoints.size() >= 4) {
            Vector3 localOffset = CatmullRomInterpolation(motion.controlPoints, t);
            motion.target->GetLocalPosition() = motion.basePos + localOffset;
        } else {
            float easedT = ApplyMotionEasing(motion.easingType, t, 1.0f);
            Vector3 actualStartPos = motion.basePos + motion.startPosOffset;
            Vector3 actualEndPos = motion.basePos + motion.endPosOffset;
            motion.target->GetLocalPosition() = Lerp(actualStartPos, actualEndPos, easedT);
        }

        float easedT = ApplyMotionEasing(motion.easingType, t, 1.0f);
        Quaternion interpolatedRot = Slerp(motion.actualStartRot, motion.actualEndRot, easedT);
        motion.target->GetWorldTransform()->quateRotation_ = interpolatedRot;
        motion.target->GetLocalScale() = Lerp(motion.actualStartScale, motion.actualEndScale, easedT);

        // コライダー制御
        bool enable = motion.currentTime >= motion.colliderOnTime && motion.currentTime <= motion.colliderOffTime;
        auto &colliders = motion.target->GetColliders();
        for (auto &collider : colliders) {
            if (collider) {
                collider->SetEnabled(enable);
            }
        }
    }

    CleanupFinishedTemporaryMotions();
    DrawControlPoints();
    DrawCatmullRomCurve();
}

void MotionEditor::Play(const std::string &jsonName) {
    auto it = motions_.find(jsonName);
    if (it == motions_.end() || !it->second.target) {
        return;
    }

    Motion &motion = it->second;
    if (!motion.hasInitialTransform) {
        motion.initialPos = motion.target->GetLocalPosition();
        motion.initialRot = motion.target->GetLocalRotation().ToEulerAngles();
        motion.initialScale = motion.target->GetLocalScale();
        motion.hasInitialTransform = true;
    }

    motion.basePos = motion.target->GetLocalPosition();
    motion.baseRot = motion.target->GetLocalRotation().ToEulerAngles();
    motion.baseScale = motion.target->GetLocalScale();

    // 補間用の回転・スケール値を計算
    motion.actualStartRot = Quaternion::FromEulerAngles(motion.baseRot + motion.startRotOffset);
    motion.actualEndRot = Quaternion::FromEulerAngles(motion.baseRot + motion.endRotOffset);
    motion.actualStartScale = motion.baseScale + motion.startScaleOffset;
    motion.actualEndScale = motion.baseScale + motion.endScaleOffset;

    motion.currentTime = 0.0f;
    motion.status = MotionStatus::Playing;
}

bool MotionEditor::PlayFromFile(BaseObject *target, const std::string &fileName, bool returnToOriginal) {
    if (!target) {
        return false;
    }

    std::string tempName = GetTemporaryMotionName(target, fileName);
    DataHandler data("AttackData", fileName);
    Motion &motion = motions_[tempName];

    motion.target = target;
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
    for (int i = 0; i < pointCount; ++i) {
        Vector3 point = data.Load<Vector3>("controlPoint" + std::to_string(i), {0, 0, 0});
        motion.controlPoints.push_back(point);
    }

    motion.initialPos = target->GetLocalPosition();
    motion.initialRot = target->GetLocalRotation().ToEulerAngles();
    motion.initialScale = target->GetLocalScale();
    motion.hasInitialTransform = true;

    motion.basePos = target->GetLocalPosition();
    motion.baseRot = target->GetLocalRotation().ToEulerAngles();
    motion.baseScale = target->GetLocalScale();

    motion.actualStartRot = Quaternion::FromEulerAngles(motion.baseRot + motion.startRotOffset);
    motion.actualEndRot = Quaternion::FromEulerAngles(motion.baseRot + motion.endRotOffset);
    motion.actualStartScale = motion.baseScale + motion.startScaleOffset;
    motion.actualEndScale = motion.baseScale + motion.endScaleOffset;

    motion.currentTime = 0.0f;
    motion.status = MotionStatus::Playing;

    return true;
}

void MotionEditor::SetComboStartPosition(BaseObject *target) {
    if (!target)
        return;

    // コンボ開始位置・回転・スケールを保存
    comboStartPositions_[target] = target->GetLocalPosition();
    comboStartRotations_[target] = target->GetLocalRotation().ToEulerAngles();
    comboStartScales_[target] = target->GetLocalScale();
}

void MotionEditor::ReturnToComboStart(BaseObject *target) {
    if (!target)
        return;

    auto posIt = comboStartPositions_.find(target);
    auto rotIt = comboStartRotations_.find(target);
    auto scaleIt = comboStartScales_.find(target);

    if (posIt != comboStartPositions_.end() &&
        rotIt != comboStartRotations_.end() &&
        scaleIt != comboStartScales_.end()) {

        target->GetLocalPosition() = posIt->second;
        target->GetLocalRotation() = Quaternion::FromEulerAngles(rotIt->second);
        target->GetLocalScale() = scaleIt->second;
    }
}

void MotionEditor::ClearComboStartPosition(BaseObject *target) {
    if (!target)
        return;

    comboStartPositions_.erase(target);
    comboStartRotations_.erase(target);
    comboStartScales_.erase(target);
}

void MotionEditor::ClearAllComboStartPositions() {
    comboStartPositions_.clear();
    comboStartRotations_.clear();
    comboStartScales_.clear();
}

void MotionEditor::Stop(const std::string &objectName) {
    auto it = motions_.find(objectName);
    if (it != motions_.end()) {
        Motion &motion = it->second;
        motion.status = MotionStatus::Stopped;
        motion.currentTime = 0.0f;

        // 初期状態に戻す
        if (motion.hasInitialTransform && motion.target) {
            motion.target->GetLocalPosition() = motion.initialPos;
            motion.target->GetLocalRotation() = Quaternion::FromEulerAngles(motion.initialRot);
            motion.target->GetLocalScale() = motion.initialScale;
        }
        if (motion.isTemporary) {
            motions_.erase(it);
        }
    }
}

void MotionEditor::StopAll() {
    auto it = motions_.begin();
    while (it != motions_.end()) {
        Motion &motion = it->second;
        motion.status = MotionStatus::Stopped;
        motion.currentTime = 0.0f;

        if (motion.hasInitialTransform && motion.target) {
            motion.target->GetLocalPosition() = motion.initialPos;
            motion.target->GetLocalRotation() = Quaternion::FromEulerAngles(motion.initialRot);
            motion.target->GetLocalScale() = motion.initialScale;
        }

        if (motion.isTemporary) {
            it = motions_.erase(it);
        } else {
            ++it;
        }
    }
}

bool MotionEditor::IsAttackFinished(BaseObject *target) {
    if (!target)
        return false;

    for (const auto &[name, motion] : motions_) {
        if (motion.target == target && motion.isTemporary) {
            return motion.status == MotionStatus::Finished;
        }
    }
    return true;
}

bool MotionEditor::IsAttackFinishedWithInterval(BaseObject *target) {
    if (!target)
        return false;

    if (!IsAttackFinished(target)) {
        return false;
    }

    auto it = attackEndIntervals_.find(target);
    if (it != attackEndIntervals_.end()) {
        return it->second <= 0.0f;
    }
    return true;
}

void MotionEditor::SetAttackEndInterval(BaseObject *target, float interval) {
    if (!target)
        return;
    attackEndIntervals_[target] = interval;
}

void MotionEditor::ClearAttackEndInterval(BaseObject *target) {
    if (!target)
        return;
    attackEndIntervals_.erase(target);
}

bool MotionEditor::IsTemporaryMotionFinished(BaseObject *target, const std::string &fileName) {
    if (!target)
        return false;

    std::string tempName = GetTemporaryMotionName(target, fileName);
    auto it = motions_.find(tempName);
    if (it != motions_.end()) {
        return it->second.status == MotionStatus::Finished;
    }
    return true;
}

void MotionEditor::DrawControlPoints() {
    if (selectedName_.empty())
        return;

    Motion &motion = motions_[selectedName_];
    if (!motion.target || !motion.useCatmullRom || motion.controlPoints.empty())
        return;

    DrawLine3D *drawLine = DrawLine3D::GetInstance();
    const float sphereSize = 0.4f;

    Vector3 basePos = (motion.currentTime == 0.0f) ? motion.target->GetLocalPosition() : motion.basePos;

    for (size_t i = 0; i < motion.controlPoints.size(); ++i) {
        Vector3 localPos = basePos + motion.controlPoints[i];
        Vector3 worldPos = TransformLocalControlPointToWorld(motion.target, localPos);

        Vector4 color = {1.0f, 0.0f, 0.0f, 1.0f};
        if (i == 0) color = {0.0f, 1.0f, 0.0f, 1.0f};
        else if (i == motion.controlPoints.size() - 1) color = {0.0f, 0.0f, 1.0f, 1.0f};

        if ((int)i == selectedControlPoint_) {
            color.x = std::min(color.x + 0.5f, 1.0f);
            color.y = std::min(color.y + 0.5f, 1.0f);
            color.z = std::min(color.z + 0.5f, 1.0f);
        }

        drawLine->DrawSphere(worldPos, color, sphereSize, 8);
    }
}

void MotionEditor::DrawCatmullRomCurve() {
    if (selectedName_.empty())
        return;

    Motion &motion = motions_[selectedName_];
    if (!motion.target || !motion.useCatmullRom || motion.controlPoints.size() < 2)
        return;

    DrawLine3D *drawLine = DrawLine3D::GetInstance();
    const Vector4 curveColor = {1.0f, 0.5f, 0.0f, 1.0f};
    const int resolution = 100;

    Vector3 basePos = (motion.currentTime == 0.0f) ? motion.target->GetLocalPosition() : motion.basePos;

    Vector3 prevWorldPoint = TransformLocalControlPointToWorld(motion.target, basePos + CatmullRomInterpolation(motion.controlPoints, 0.0f));

    for (int i = 1; i <= resolution; ++i) {
        float t = (float)i / resolution;
        Vector3 currentWorldPoint = TransformLocalControlPointToWorld(motion.target, basePos + CatmullRomInterpolation(motion.controlPoints, t));
        drawLine->SetPoints(prevWorldPoint, currentWorldPoint, curveColor);
        prevWorldPoint = currentWorldPoint;
    }
}

void MotionEditor::DrawImGui() {
#ifdef USE_IMGUI
    if (!motions_.empty()) {
        std::vector<const char *> names;
        for (auto &[name, _] : motions_)
            names.push_back(name.c_str());
        int index = 0;
        for (size_t i = 0; i < names.size(); ++i) {
            if (names[i] == selectedName_)
                index = (int)i;
        }
        if (ImGui::Combo("対象オブジェクト", &index, names.data(), (int)names.size())) {
            selectedName_ = names[index];
        }
    }

    if (!selectedName_.empty()) {
        Motion &m = motions_[selectedName_];

        ImGui::SliderFloat("経過時間", &m.currentTime, 0.0f, m.totalTime);
        ImGui::DragFloat("トータル時間", &m.totalTime, 0.1f, 0.1f, 30.0f);
        ImGui::DragFloat("コライダーON", &m.colliderOnTime, 0.01f);
        ImGui::DragFloat("コライダーOFF", &m.colliderOffTime, 0.01f);

        if (ImGui::Button("再生")) Play(selectedName_);
        ImGui::SameLine();
        if (ImGui::Button("停止")) Stop(selectedName_);
        ImGui::SameLine();
        if (ImGui::Button("全停止")) StopAll();

        ImGui::Separator();
        if (ImGui::Checkbox("Catmull-Rom曲線", &m.useCatmullRom)) {}

        if (m.useCatmullRom) {
            if (ImGui::Button("制御点追加")) m.controlPoints.push_back({0, 0, 0});
            ImGui::SameLine();
            if (ImGui::Button("制御点削除") && selectedControlPoint_ >= 0) {
                m.controlPoints.erase(m.controlPoints.begin() + selectedControlPoint_);
                selectedControlPoint_ = -1;
            }
            
            for (int i = 0; i < (int)m.controlPoints.size(); ++i) {
                if (ImGui::Selectable(("制御点 " + std::to_string(i)).c_str(), selectedControlPoint_ == i)) selectedControlPoint_ = i;
            }

            if (selectedControlPoint_ >= 0) {
                ImGui::DragFloat3("位置", &m.controlPoints[selectedControlPoint_].x, 0.1f);
            }
        } else {
            ImGui::DragFloat3("開始PosOff", &m.startPosOffset.x, 0.1f);
            ImGui::DragFloat3("終了PosOff", &m.endPosOffset.x, 0.1f);
            ImGui::DragFloat3("開始RotOff", &m.startRotOffset.x, 0.1f);
            ImGui::DragFloat3("終了RotOff", &m.endRotOffset.x, 0.1f);
        }

        static char nameBuffer[256] = "";
        strcpy_s(nameBuffer, sizeof(nameBuffer), jsonName_.c_str());
        if (ImGui::InputText("セーブ名", nameBuffer, sizeof(nameBuffer))) jsonName_ = nameBuffer;
        if (ImGui::Button("セーブ")) { Save(jsonName_); jsonName_.clear(); }
    }
#endif
}

void MotionEditor::Save(const std::string &fileName) {
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
    data.Save("controlPointCount", (int)m.controlPoints.size());
    for (int i = 0; i < (int)m.controlPoints.size(); ++i) {
        data.Save("controlPoint" + std::to_string(i), m.controlPoints[i]);
    }
    ImGuiNotification::Post("モーションデータを保存しました: " + fileName, {0.2f, 0.8f, 0.2f, 1.0f});
}

Motion MotionEditor::Load(const std::string &fileName) {
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
    for (int i = 0; i < pointCount; ++i) {
        Vector3 point = data.Load<Vector3>("controlPoint" + std::to_string(i), {0, 0, 0});
        m.controlPoints.push_back(point);
    }
    ImGuiNotification::Post("モーションデータを読み込みました: " + fileName, {0.2f, 0.8f, 0.8f, 1.0f});
    return m;
}