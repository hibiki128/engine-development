#define NOMINMAX
#ifdef USE_IMGUI
#include "ImGuizmoManager.h"
#include "ImGuiNotification.h"
#include "Input.h"
#include "Sprite.h"
#include <line/LineRenderer.h>
#include <object/base/BaseObjectManager.h>
#include <transform/WorldTransform.h>
#include <edit/undo/UndoRedoManager.h>
#include "WinApp.h"
#include <format>
#include <numbers>
#include <imgui.h>
// DebugUIHelper.h は ImVec4 / ImGui:: を使うので imgui.h の後に include する
#include "DebugUIHelper.h"

// =======================================================================
// GizmoTarget メンバ関数実装（操作対象1件ぶんの行列の読み書き）
// =======================================================================

namespace Hagine {
// 各型に対応したワールド行列を返す
// FreeTransform の場合は translate/rotate/scale ポインタから行列を構築する
Matrix4x4 GizmoTarget::GetWorldMatrix() const
{
    switch (type)
    {
    case Type::BaseObject:
        if (baseObject && baseObject->GetWorldTransform())
        {
            return baseObject->GetWorldTransform()->matWorld_;
        }
        break;

    case Type::WorldTransform:
        if (worldTransform)
        {
            return worldTransform->matWorld_;
        }
        break;

    case Type::FreeTransform:
        if (translate)
        {
            Vector3 s = scale ? *scale : Vector3{1.0f, 1.0f, 1.0f};
            Vector3 r = rotate ? *rotate : Vector3{0.0f, 0.0f, 0.0f};
            return MakeAffineMatrix(s, r, *translate);
        }
        break;

    case Type::Sprite2D:
        if (position2D)
        {
            Matrix4x4 m = MakeIdentity4x4();
            m.m[3][0] = position2D->x;
            m.m[3][1] = position2D->y;
            m.m[3][2] = 0.0f;
            return m;
        }
        break;
    }
    return MakeIdentity4x4();
}

// ワールド座標（位置成分）を返す
Vector3 GizmoTarget::GetWorldPosition() const
{
    switch (type)
    {
    case Type::BaseObject:
        if (baseObject)
            return baseObject->GetWorldPosition();
        break;
    case Type::WorldTransform:
        if (worldTransform)
            return worldTransform->GetWorldPosition();
        break;
    case Type::FreeTransform:
        if (translate)
            return *translate;
        break;
    case Type::Sprite2D:
        if (position2D)
            return {position2D->x, position2D->y, 0.0f};
        break;
    }
    return {0.0f, 0.0f, 0.0f};
}

// ギズモ操作によって生じた平行移動デルタを各型に適用する
void GizmoTarget::ApplyTranslationDelta(const Vector3 &delta)
{
    switch (type)
    {
    case Type::BaseObject:
        if (baseObject)
        {
            baseObject->GetLocalPosition() = baseObject->GetLocalPosition() + delta;
            WorldTransform *wt = baseObject->GetWorldTransform();
            if (wt)
            {
                wt->translation_ = baseObject->GetLocalPosition();
                wt->UpdateMatrix();
                baseObject->UpdateWorldTransformHierarchy();
            }
        }
        break;

    case Type::WorldTransform:
        if (worldTransform)
        {
            worldTransform->translation_ = worldTransform->translation_ + delta;
            worldTransform->UpdateMatrix();
        }
        break;

    case Type::FreeTransform:
        if (translate)
        {
            translate->x += delta.x;
            translate->y += delta.y;
            if (!isScreenSpace)
            {
                translate->z += delta.z;
            }
        }
        break;

    case Type::Sprite2D:
        if (position2D)
        {
            position2D->x += delta.x;
            position2D->y += delta.y;
            // Z は無視（スクリーン空間 XY のみ）
        }
        break;
    }
}

namespace {
// アフィン行列をスケール・回転・平行移動へ分解する。
// この行列規約は行ベクトル（v * M）なので、各行が基底ベクトルになる。
void DecomposeAffine(const Matrix4x4 &matrix, Vector3 &scale, Quaternion &rotation, Vector3 &translation)
{
    translation = {matrix.m[3][0], matrix.m[3][1], matrix.m[3][2]};

    Vector3 axis[3] = {
        {matrix.m[0][0], matrix.m[0][1], matrix.m[0][2]},
        {matrix.m[1][0], matrix.m[1][1], matrix.m[1][2]},
        {matrix.m[2][0], matrix.m[2][1], matrix.m[2][2]}};

    float length[3] = {axis[0].Length(), axis[1].Length(), axis[2].Length()};

    // 左手系で行列式が負なら鏡映が入っている。X軸のスケールを負にして回転成分から追い出す。
    const Vector3 cross = axis[0].Cross(axis[1]);
    if (cross.Dot(axis[2]) < 0.0f)
    {
        length[0] = -length[0];
        axis[0] = axis[0] * -1.0f;
    }

    scale = {length[0], length[1], length[2]};

    // スケール0の軸は方向が求まらないので、その軸だけ単位行列の行を使う
    constexpr float kEpsilon = 1e-6f;
    Matrix4x4 rotationMatrix = MakeIdentity4x4();
    for (int row = 0; row < 3; ++row)
    {
        const float absLength = std::abs(length[row]);
        if (absLength <= kEpsilon)
        {
            continue;
        }
        rotationMatrix.m[row][0] = axis[row].x / absLength;
        rotationMatrix.m[row][1] = axis[row].y / absLength;
        rotationMatrix.m[row][2] = axis[row].z / absLength;
    }

    rotation = Quaternion::FromMatrix(rotationMatrix).Normalize();
}

// 親を持つ WorldTransform について、親側のワールド行列（継承フラグ反映済み）を返す。
// UpdateMatrix と同じ組み立てにしないと、ワールド→ローカル変換がずれる。
Matrix4x4 BuildInheritedParentMatrix(const WorldTransform &transform)
{
    const WorldTransform *pParent = transform.pParent_;
    if (!pParent)
    {
        return MakeIdentity4x4();
    }
    if (transform.inheritTranslation_ && transform.inheritRotation_ && transform.inheritScale_)
    {
        return pParent->matWorld_;
    }
    const Vector3 parentScale = transform.inheritScale_ ? pParent->GetWorldScale() : Vector3{1.0f, 1.0f, 1.0f};
    const Quaternion parentRotation = transform.inheritRotation_ ? pParent->GetWorldRotationQuaternion() : Quaternion::IdentityQuaternion();
    const Vector3 parentTranslation = transform.inheritTranslation_ ? pParent->GetWorldPosition() : Vector3{0.0f, 0.0f, 0.0f};
    return MakeAffineMatrix(parentScale, parentRotation, parentTranslation);
}

// ワールド行列を WorldTransform のローカル成分へ書き戻す
void ApplyWorldMatrixToTransform(WorldTransform &transform, const Matrix4x4 &worldMatrix)
{
    // 親がいる場合は親のぶんを打ち消してローカル成分に戻す
    Matrix4x4 localMatrix = worldMatrix;
    if (transform.pParent_)
    {
        localMatrix = worldMatrix * Inverse(BuildInheritedParentMatrix(transform));
    }

    Vector3 scale{};
    Quaternion rotation{};
    Vector3 translation{};
    DecomposeAffine(localMatrix, scale, rotation, translation);

    transform.scale_ = scale;
    transform.translation_ = translation;
    // SetRotationQuaternion はオイラー角モードのときだけ eulerRotation_ を更新するので、
    // クォータニオンモードでは UpdateQuaternion のオイラー再変換に巻き込まれない。
    transform.SetRotationQuaternion(rotation);
    transform.UpdateMatrix();
}
} // namespace

// ギズモ操作後のワールド行列を各型へ反映する
void GizmoTarget::ApplyWorldMatrix(const Matrix4x4 &worldMatrix)
{
    // スクリーン空間は XY 平行移動しか意味を持たないので、従来どおりデルタ適用に落とす
    if (isScreenSpace)
    {
        const Vector3 current = GetWorldPosition();
        ApplyTranslationDelta({worldMatrix.m[3][0] - current.x, worldMatrix.m[3][1] - current.y, 0.0f});
        return;
    }

    switch (type)
    {
    case Type::BaseObject:
        if (baseObject && baseObject->GetWorldTransform())
        {
            ApplyWorldMatrixToTransform(*baseObject->GetWorldTransform(), worldMatrix);
            // 子オブジェクトのワールド行列も追従させる
            baseObject->UpdateWorldTransformHierarchy();
        }
        break;

    case Type::WorldTransform:
        if (worldTransform)
        {
            ApplyWorldMatrixToTransform(*worldTransform, worldMatrix);
        }
        break;

    case Type::FreeTransform:
        if (translate)
        {
            Vector3 decomposedScale{};
            Quaternion decomposedRotation{};
            Vector3 decomposedTranslation{};
            DecomposeAffine(worldMatrix, decomposedScale, decomposedRotation, decomposedTranslation);

            *translate = decomposedTranslation;
            // rotate / scale は持っていない対象もあるので、あるものだけ書き込む
            if (rotate)
            {
                *rotate = decomposedRotation.ToEulerAngles();
            }
            if (scale)
            {
                *scale = decomposedScale;
            }
        }
        break;

    case Type::Sprite2D:
        if (position2D)
        {
            position2D->x = worldMatrix.m[3][0];
            position2D->y = worldMatrix.m[3][1];
        }
        break;
    }
}

// マウス選択・フォーカス用のローカル空間AABBを返す
AABB GizmoTarget::GetLocalBounds() const
{
    if (type == Type::BaseObject && baseObject)
    {
        return baseObject->GetLocalBounds();
    }
    // BaseObject 以外（エミッター・ライト等）は実体の形が無いので、掴める大きさの箱を返す
    return AABB{{-1.3f, -1.3f, -1.3f}, {1.3f, 1.3f, 1.3f}};
}

// ImGui で変換詳細を表示する
// imguiCallback が設定されている場合はそちらを優先する
void GizmoTarget::ShowImGui()
{
    switch (type)
    {
    case Type::BaseObject:
        if (baseObject)
        {
            baseObject->DrawImGui();
        }
        break;

    case Type::WorldTransform:
        if (worldTransform)
        {
            if (imguiCallback)
            {
                imguiCallback();
            }
            else
            {
                // 専用の表示が無い対象の既定の表示（数値欄は右クリックでコピー・貼り付け・既定値に戻す）
                static const float kZero[3] = {0.0f, 0.0f, 0.0f};
                static const float kOne[3] = {1.0f, 1.0f, 1.0f};
                bool changed = LabeledDrag3("位置", "##wtPos", &worldTransform->translation_.x, 0.1f, 0.0f, 0.0f, "%.2f",
                                            DebugTheme::FrameBg(DebugTheme::kAccentBlue));
                changed |= FloatNContextMenu("##wtPosCtx", &worldTransform->translation_.x, 3, kZero);

                const float toDeg = 180.0f / std::numbers::pi_v<float>;
                Vector3 euler = worldTransform->GetRotationEuler();
                float degrees[3] = {euler.x * toDeg, euler.y * toDeg, euler.z * toDeg};
                if (LabeledDrag3("回転 (度)", "##wtRot", degrees, 0.5f, 0.0f, 0.0f, "%.1f°", DebugTheme::FrameBg(DebugTheme::kAccentCyan)) |
                    FloatNContextMenu("##wtRotCtx", degrees, 3, kZero))
                {
                    worldTransform->SetRotationEuler({degrees[0] / toDeg, degrees[1] / toDeg, degrees[2] / toDeg});
                    changed = true;
                }

                changed |= LabeledDrag3("拡縮", "##wtScale", &worldTransform->scale_.x, 0.01f, 0.0f, 0.0f, "%.2f",
                                        DebugTheme::FrameBg(DebugTheme::kAccentGreen));
                changed |= FloatNContextMenu("##wtScaleCtx", &worldTransform->scale_.x, 3, kOne);
                if (changed)
                {
                    worldTransform->UpdateMatrix();
                }
            }
        }
        break;

    case Type::FreeTransform:
        if (imguiCallback)
        {
            imguiCallback();
        }
        else
        {
            static const float kZero[3] = {0.0f, 0.0f, 0.0f};
            static const float kOne[3] = {1.0f, 1.0f, 1.0f};
            if (translate)
            {
                if (isScreenSpace)
                {
                    CaptionText("位置 (px)");
                    ImGui::SetNextItemWidth(-1);
                    ImGui::DragFloat2("##ftPos2", &translate->x, 1.0f);
                    FloatNContextMenu("##ftPos2Ctx", &translate->x, 2, kZero);
                }
                else
                {
                    LabeledDrag3("位置", "##ftPos", &translate->x, 0.1f, 0.0f, 0.0f, "%.2f", DebugTheme::FrameBg(DebugTheme::kAccentBlue));
                    FloatNContextMenu("##ftPosCtx", &translate->x, 3, kZero);
                }
            }
            if (rotate)
            {
                // 中身はラジアンなので、表示と入力だけ度に直す
                const float toDeg = 180.0f / std::numbers::pi_v<float>;
                float degrees[3] = {rotate->x * toDeg, rotate->y * toDeg, rotate->z * toDeg};
                if (LabeledDrag3("回転 (度)", "##ftRot", degrees, 0.5f, 0.0f, 0.0f, "%.1f°", DebugTheme::FrameBg(DebugTheme::kAccentCyan)) |
                    FloatNContextMenu("##ftRotCtx", degrees, 3, kZero))
                {
                    *rotate = {degrees[0] / toDeg, degrees[1] / toDeg, degrees[2] / toDeg};
                }
            }
            if (scale)
            {
                LabeledDrag3("拡縮", "##ftScale", &scale->x, 0.01f, 0.0f, 0.0f, "%.2f", DebugTheme::FrameBg(DebugTheme::kAccentGreen));
                FloatNContextMenu("##ftScaleCtx", &scale->x, 3, kOne);
            }
        }
        break;

    case Type::Sprite2D:
        if (imguiCallback)
        {
            imguiCallback();
        }
        else if (position2D)
        {
            ImGui::DragFloat2("Position (px)", &position2D->x, 1.0f);
        }
        break;
    }
}

} // namespace Hagine
#endif // USE_IMGUI
