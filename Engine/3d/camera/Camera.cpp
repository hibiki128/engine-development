#define NOMINMAX
#include "Camera.h"
#include "data/DataHandler.h"
#include "frame/Frame.h"
#include "transform/WorldTransform.h"
#include <MyMath.h>
#include <algorithm>
#include <cmath>
#include <random>
#ifdef USE_IMGUI
#include <imgui.h>
#endif // USE_IMGUI

namespace Hagine {
namespace {
/// <summary>シェイク用の乱数（-1〜1）</summary>
float RandomSigned()
{
    static std::mt19937 engine{std::random_device{}()};
    static std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
    return dist(engine);
}
} // namespace

void Camera::Initialize(const std::string &name)
{
    name_ = name;
    // 定数バッファの生成まで含めて ViewProjection 側で行う（JSON は Load() で明示的に読む）
    viewProjection_.Initialize();
    position_ = viewProjection_.translation_;
    rotation_ = viewProjection_.eulerRotation_;
    quaternion_ = viewProjection_.quaternionRotation_;
    ApplyToViewProjection();
}

void Camera::Update()
{
    const float deltaTime = Frame::DeltaTime();

    UpdateCameraWork(deltaTime);
    UpdateLookAt();
    UpdateShake(deltaTime);
    ApplyToViewProjection();
}

// 位置・向き・レンズの各setterは、設定した内容が同じフレームのうちに行列へ反映されるよう
// その場で ApplyToViewProjection() する（「動かしたのに描画が1フレーム遅れる」を防ぐ）。

void Camera::SetPosition(const Vector3 &position)
{
    position_ = position;
    ApplyToViewProjection();
}

void Camera::SetRotation(const Vector3 &eulerRadians)
{
    // 手で向きを決めたので注視は解除する（設定した向きがすぐ上書きされないように）
    ClearTarget();
    rotation_ = eulerRadians;
    useQuaternion_ = false;
    ApplyToViewProjection();
}

void Camera::SetQuaternion(const Quaternion &rotation)
{
    ClearTarget();
    quaternion_ = rotation;
    useQuaternion_ = true;
    ApplyToViewProjection();
}

void Camera::Translate(const Vector3 &worldDelta)
{
    position_ += worldDelta;
    ApplyToViewProjection();
}

void Camera::MoveLocal(const Vector3 &localDelta)
{
    position_ += GetRight() * localDelta.x;
    position_ += GetUp() * localDelta.y;
    position_ += GetForward() * localDelta.z;
    ApplyToViewProjection();
}

void Camera::LookAt(const Vector3 &target)
{
    rotation_ = CalcLookAtRotation(target);
    useQuaternion_ = false;
    ApplyToViewProjection();
}

void Camera::SetTarget(const Vector3 &target)
{
    target_ = target;
    hasTarget_ = true;
    LookAt(target); // すぐ向きを合わせる（以降は Update が向き続ける）
}

void Camera::ClearTarget()
{
    hasTarget_ = false;
    pTargetTransform_ = nullptr;
}

void Camera::Orbit(const Vector3 &center, float yaw, float pitch, float distance)
{
    // 中心から見て yaw/pitch の方向へ distance だけ離れた位置に立ち、中心を向く
    const float cosPitch = std::cos(pitch);
    Vector3 offset;
    offset.x = -std::sin(yaw) * cosPitch;
    offset.y = std::sin(pitch);
    offset.z = -std::cos(yaw) * cosPitch;
    position_ = center + offset * distance;
    SetTarget(center); // 中心を向く（ここで行列も作り直される）
}

Quaternion Camera::GetRotationQuaternion() const
{
    return useQuaternion_ ? quaternion_ : Quaternion::FromEulerAngles(rotation_);
}

Vector3 Camera::GetForward() const
{
    const Matrix4x4 rotateMatrix = useQuaternion_ ? QuaternionToMatrix4x4(quaternion_)
                                                  : MakeRotateXYZMatrix(rotation_);
    return TransformNormal(kWorldForward, rotateMatrix).Normalize();
}

Vector3 Camera::GetRight() const
{
    const Matrix4x4 rotateMatrix = useQuaternion_ ? QuaternionToMatrix4x4(quaternion_)
                                                  : MakeRotateXYZMatrix(rotation_);
    return TransformNormal(kWorldRight, rotateMatrix).Normalize();
}

Vector3 Camera::GetUp() const
{
    const Matrix4x4 rotateMatrix = useQuaternion_ ? QuaternionToMatrix4x4(quaternion_)
                                                  : MakeRotateXYZMatrix(rotation_);
    return TransformNormal(kWorldUp, rotateMatrix).Normalize();
}

void Camera::SetFovYDegrees(float degrees)
{
    viewProjection_.fovAngleY_ = degreesToRadians(degrees);
    ApplyToViewProjection();
}

void Camera::SetAspectRatio(float aspectRatio)
{
    viewProjection_.aspectRatio = aspectRatio;
    ApplyToViewProjection();
}

float Camera::GetFovYDegrees() const
{
    return radiansToDegrees(viewProjection_.fovAngleY_);
}

void Camera::SetClipRange(float nearZ, float farZ)
{
    viewProjection_.nearZ_ = nearZ;
    viewProjection_.farZ_ = farZ;
    ApplyToViewProjection();
}

void Camera::EaseTo(const Vector3 &position, const Vector3 &eulerRadians,
                    float duration, EasingType easing)
{
    CameraWorkKey key;
    key.position = position;
    key.rotation = eulerRadians;
    key.duration = duration;
    key.easing = easing;
    PlayCameraWork({key}, false);
}

void Camera::PlayCameraWork(const std::vector<CameraWorkKey> &keys, bool loop)
{
    if (keys.empty())
    {
        return;
    }
    workKeys_ = keys;
    workIndex_ = 0;
    workTime_ = 0.0f;
    workPlaying_ = true;
    workLoop_ = loop;
    workWaiting_ = false;
    // 開始点は「今の位置・向き」。途中から再生しても繋がる。
    workStartPosition_ = position_;
    workStartRotation_ = rotation_;
}

void Camera::StopCameraWork()
{
    workPlaying_ = false;
    workWaiting_ = false;
    workKeys_.clear();
    workIndex_ = 0;
    workTime_ = 0.0f;
}

void Camera::Shake(float duration, float strength)
{
    shakeDuration_ = (std::max)(duration, 0.0001f);
    shakeTime_ = shakeDuration_;
    shakeStrength_ = strength;
}

void Camera::StopShake()
{
    shakeTime_ = 0.0f;
    shakeOffset_ = {0.0f, 0.0f, 0.0f};
}

void Camera::SetExternalOffset(const Vector3 &positionOffset, float pitchOffset)
{
    externalOffset_ = positionOffset;
    externalPitch_ = pitchOffset;
}

void Camera::CopyStateFrom(const Camera &other)
{
    position_ = other.position_;
    rotation_ = other.rotation_;
    quaternion_ = other.quaternion_;
    useQuaternion_ = other.useQuaternion_;
    shakeOffset_ = other.shakeOffset_;         // 揺れの見た目もそのまま引き継ぐ
    externalOffset_ = other.externalOffset_;   // 外部演出のずれも引き継ぐ
    externalPitch_ = other.externalPitch_;
    viewProjection_.fovAngleY_ = other.viewProjection_.fovAngleY_;
    viewProjection_.nearZ_ = other.viewProjection_.nearZ_;
    viewProjection_.farZ_ = other.viewProjection_.farZ_;
    viewProjection_.aspectRatio = other.viewProjection_.aspectRatio;
    ApplyToViewProjection();
}

void Camera::BlendFrom(const Camera &from, const Camera &to, float t)
{
    const float ratio = std::clamp(t, 0.0f, 1.0f);
    position_ = Lerp(from.position_, to.position_, ratio);

    // どちらかがクォータニオン運用なら球面補間、そうでなければオイラー角のまま補間する
    if (from.useQuaternion_ || to.useQuaternion_)
    {
        const Quaternion fromQuat = from.useQuaternion_ ? from.quaternion_ : MatrixToQuaternion(MakeRotateXYZMatrix(from.rotation_));
        const Quaternion toQuat = to.useQuaternion_ ? to.quaternion_ : MatrixToQuaternion(MakeRotateXYZMatrix(to.rotation_));
        quaternion_ = Slerp(fromQuat, toQuat, ratio);
        useQuaternion_ = true;
    }
    else
    {
        // 角度は最短方向で回す（+180°/-180°を跨ぐときに1周しないように）
        rotation_.x = LerpShortAngle(from.rotation_.x, to.rotation_.x, ratio);
        rotation_.y = LerpShortAngle(from.rotation_.y, to.rotation_.y, ratio);
        rotation_.z = LerpShortAngle(from.rotation_.z, to.rotation_.z, ratio);
        useQuaternion_ = false;
    }

    shakeOffset_ = Lerp(from.shakeOffset_, to.shakeOffset_, ratio);
    viewProjection_.fovAngleY_ = Lerp(from.viewProjection_.fovAngleY_, to.viewProjection_.fovAngleY_, ratio);
    viewProjection_.nearZ_ = Lerp(from.viewProjection_.nearZ_, to.viewProjection_.nearZ_, ratio);
    viewProjection_.farZ_ = Lerp(from.viewProjection_.farZ_, to.viewProjection_.farZ_, ratio);
    viewProjection_.aspectRatio = to.viewProjection_.aspectRatio;
    ApplyToViewProjection();
}

void Camera::Save(const std::string &jsonName) const
{
    // キー名は従来の ViewProjection の保存形式に合わせる（既存の Camera/*.json をそのまま使えるように）
    const std::string &fileName = jsonName.empty() ? name_ : jsonName;
    std::unique_ptr<DataHandler> data = std::make_unique<DataHandler>("Camera", fileName);
    data->Save("translation", position_);
    data->Save("eulerRotation", rotation_);
    data->Save("quaternionRotation", quaternion_);
    data->Save("isUseQuaternion", useQuaternion_);
    data->Save("fov", viewProjection_.fovAngleY_);
    data->Save("nearZ", viewProjection_.nearZ_);
    data->Save("farZ", viewProjection_.farZ_);
}

void Camera::Load(const std::string &jsonName)
{
    const std::string &fileName = jsonName.empty() ? name_ : jsonName;
    std::unique_ptr<DataHandler> data = std::make_unique<DataHandler>("Camera", fileName);
    position_ = data->Load("translation", position_);
    rotation_ = data->Load("eulerRotation", rotation_);
    quaternion_ = data->Load("quaternionRotation", quaternion_);
    useQuaternion_ = data->Load("isUseQuaternion", useQuaternion_);
    viewProjection_.fovAngleY_ = data->Load("fov", viewProjection_.fovAngleY_);
    viewProjection_.nearZ_ = data->Load("nearZ", viewProjection_.nearZ_);
    viewProjection_.farZ_ = data->Load("farZ", viewProjection_.farZ_);
    ApplyToViewProjection();
}

void Camera::EaseToSaved(const std::string &jsonName, float duration, EasingType easing)
{
    // 保存済みの位置・向きを目標にしてイージング移動する（旧 ViewProjection::EaseCameraMove 相当）
    std::unique_ptr<DataHandler> data = std::make_unique<DataHandler>("Camera", jsonName);
    const Vector3 targetPosition = data->Load("translation", position_);
    const Vector3 targetRotation = data->Load("eulerRotation", rotation_);
    EaseTo(targetPosition, targetRotation, duration, easing);
}

void Camera::UpdateCameraWork(float deltaTime)
{
    if (!workPlaying_ || workKeys_.empty())
    {
        return;
    }

    const CameraWorkKey &key = workKeys_[workIndex_];
    workTime_ += deltaTime;

    // 到着後の待ち時間
    if (workWaiting_)
    {
        if (workTime_ < key.wait)
        {
            return;
        }
        // 次のキーへ
        workWaiting_ = false;
        workTime_ = 0.0f;
        workStartPosition_ = position_;
        workStartRotation_ = rotation_;
        ++workIndex_;
        if (workIndex_ >= workKeys_.size())
        {
            if (!workLoop_)
            {
                StopCameraWork();
                return;
            }
            workIndex_ = 0;
        }
        return;
    }

    const float duration = (std::max)(key.duration, 0.0001f);
    if (workTime_ >= duration)
    {
        // 目標値でぴったり止める（イージングの外挿で行き過ぎないように）
        position_ = key.position;
        if (!key.lookAtTarget)
        {
            rotation_ = key.rotation;
            useQuaternion_ = false;
        }
        workTime_ = 0.0f;
        workWaiting_ = true;
        if (key.wait <= 0.0f)
        {
            // 待ち無しならそのまま次のキーへ進める
            workWaiting_ = false;
            workStartPosition_ = position_;
            workStartRotation_ = rotation_;
            ++workIndex_;
            if (workIndex_ >= workKeys_.size())
            {
                if (!workLoop_)
                {
                    StopCameraWork();
                    return;
                }
                workIndex_ = 0;
            }
        }
        return;
    }

    // 時間はクランプしてから渡す（duration を超えると外挿して飛んでいくため）
    const float time = std::clamp(workTime_, 0.0f, duration);
    position_ = ApplyEasing(key.easing, workStartPosition_, key.position, time, duration);
    if (!key.lookAtTarget)
    {
        rotation_ = ApplyEasing(key.easing, workStartRotation_, key.rotation, time, duration);
        useQuaternion_ = false;
    }
}

void Camera::UpdateShake(float deltaTime)
{
    if (shakeTime_ <= 0.0f)
    {
        shakeOffset_ = {0.0f, 0.0f, 0.0f};
        return;
    }
    shakeTime_ -= deltaTime;
    if (shakeTime_ <= 0.0f)
    {
        StopShake();
        return;
    }
    // 残り時間に比例して弱くしていく
    const float strength = shakeStrength_ * (shakeTime_ / shakeDuration_);
    shakeOffset_ = {RandomSigned() * strength, RandomSigned() * strength, RandomSigned() * strength};
}

void Camera::UpdateLookAt()
{
    if (pTargetTransform_)
    {
        const Matrix4x4 &world = pTargetTransform_->matWorld_;
        rotation_ = CalcLookAtRotation({world.m[3][0], world.m[3][1], world.m[3][2]});
        useQuaternion_ = false;
        return;
    }
    if (hasTarget_)
    {
        rotation_ = CalcLookAtRotation(target_);
        useQuaternion_ = false;
    }
}

Vector3 Camera::CalcLookAtRotation(const Vector3 &target) const
{
    const Vector3 toTarget = target - position_;
    const float horizontal = std::sqrt(toTarget.x * toTarget.x + toTarget.z * toTarget.z);
    if (horizontal < 1e-6f && std::abs(toTarget.y) < 1e-6f)
    {
        return rotation_; // 同じ位置なら向きを変えない
    }
    // Y回りは水平方向の角度、X回りは見下ろし角（+で下向き）
    Vector3 result{};
    result.y = std::atan2(toTarget.x, toTarget.z);
    result.x = std::atan2(-toTarget.y, horizontal);
    result.z = 0.0f;
    return result;
}

void Camera::ApplyToViewProjection()
{
    viewProjection_.translation_ = position_ + shakeOffset_;
    viewProjection_.eulerRotation_ = rotation_;
    viewProjection_.quaternionRotation_ = quaternion_;
    viewProjection_.isUseQuaternion_ = useQuaternion_;
    viewProjection_.UpdateMatrix();

    // 外部演出（画面揺れ）のずれはビュー行列へ直接足す。
    // カメラの位置・向きそのものは動かさないので、演出が終われば元の絵に戻る。
    if (externalOffset_.LengthSq() > 0.0f || externalPitch_ != 0.0f)
    {
        viewProjection_.matView_.m[3][0] += externalOffset_.x;
        viewProjection_.matView_.m[3][1] += externalOffset_.y;
        viewProjection_.matView_.m[3][2] += externalOffset_.z;
        if (externalPitch_ != 0.0f)
        {
            viewProjection_.matView_ = MakeRotateXMatrix(externalPitch_) * viewProjection_.matView_;
        }
        viewProjection_.TransferMatrix();
    }
}

void Camera::ShowDebugWindow()
{
#ifdef USE_IMGUI
    ImGui::Begin("カメラ設定##camerawin");
    ImGui::TextColored(ImVec4(0.45f, 0.60f, 0.78f, 1.0f), "%s", name_.c_str());
    ImGui::Separator();
    DrawImGui();
    ImGui::End();
#endif // USE_IMGUI
}

void Camera::DrawImGui()
{
#ifdef USE_IMGUI
    ImGui::PushID(name_.c_str());

    ImGui::DragFloat3("位置##camerapos", &position_.x, 0.1f);
    Vector3 rotationDegrees = {radiansToDegrees(rotation_.x), radiansToDegrees(rotation_.y), radiansToDegrees(rotation_.z)};
    if (ImGui::DragFloat3("回転(度)##camerarot", &rotationDegrees.x, 0.5f))
    {
        rotation_ = {degreesToRadians(rotationDegrees.x), degreesToRadians(rotationDegrees.y), degreesToRadians(rotationDegrees.z)};
        useQuaternion_ = false;
    }

    float fov = GetFovYDegrees();
    if (ImGui::DragFloat("画角(度)##camerafov", &fov, 0.5f, 1.0f, 179.0f))
    {
        SetFovYDegrees(fov);
    }
    ImGui::DragFloat("近面##cameranear", &viewProjection_.nearZ_, 0.01f, 0.001f, 100.0f);
    ImGui::DragFloat("遠面##camerafar", &viewProjection_.farZ_, 1.0f, 1.0f, 100000.0f);

    if (hasTarget_ || pTargetTransform_)
    {
        ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "注視中");
        ImGui::SameLine();
        if (ImGui::SmallButton("解除##cameracleartarget"))
        {
            ClearTarget();
        }
    }
    if (workPlaying_)
    {
        ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.4f, 1.0f), "カメラワーク再生中 (%zu/%zu)", workIndex_ + 1, workKeys_.size());
        ImGui::SameLine();
        if (ImGui::SmallButton("停止##camerastopwork"))
        {
            StopCameraWork();
        }
    }

    if (ImGui::Button("保存##camerasave"))
    {
        Save();
    }
    ImGui::SameLine();
    if (ImGui::Button("読み込み##cameraload"))
    {
        Load();
    }

    ImGui::PopID();
#endif // USE_IMGUI
}
} // namespace Hagine
