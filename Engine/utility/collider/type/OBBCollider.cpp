#include "OBBCollider.h"
#include "line/LineRenderer.h"
#include "MyMath.h"

namespace Hagine {
void OBBCollider::UpdateWorldTransform()
{
    cachedOBB_.rotationCenter = GetCenterPosition() + rotationOffset_;

    Vector3 anchorOffset = Vector3(
        (anchorPoint_.x - 0.5f) * size_.x * 2.0f,
        (anchorPoint_.y - 0.5f) * size_.y * 2.0f,
        (anchorPoint_.z - 0.5f) * size_.z * 2.0f);

    cachedOBB_.scaleCenter = GetCenterPosition() + positionOffset_ + anchorOffset;
    cachedOBB_.size = size_;

    MakeOBBOrientations(GetCenterRotation());
    UpdateOBBScaleCenter();
}

void OBBCollider::MakeOBBOrientations(const Quaternion &rotation)
{
    Matrix4x4 rotateMatrix = QuaternionToMatrix4x4(rotation);

    cachedOBB_.orientations[0] = Vector3(rotateMatrix.m[0][0], rotateMatrix.m[0][1], rotateMatrix.m[0][2]);
    cachedOBB_.orientations[1] = Vector3(rotateMatrix.m[1][0], rotateMatrix.m[1][1], rotateMatrix.m[1][2]);
    cachedOBB_.orientations[2] = Vector3(rotateMatrix.m[2][0], rotateMatrix.m[2][1], rotateMatrix.m[2][2]);
}

void OBBCollider::UpdateOBBScaleCenter()
{
    cachedOBB_.scaleCenterRotated =
        cachedOBB_.orientations[0] * (cachedOBB_.scaleCenter.x - cachedOBB_.rotationCenter.x) +
        cachedOBB_.orientations[1] * (cachedOBB_.scaleCenter.y - cachedOBB_.rotationCenter.y) +
        cachedOBB_.orientations[2] * (cachedOBB_.scaleCenter.z - cachedOBB_.rotationCenter.z) +
        cachedOBB_.rotationCenter;
}

void OBBCollider::DebugDraw(const ViewProjection &viewProjection)
{
    if (!isVisible_ || !isEnabled_)
    {
        return;
    }

    LineRenderer *pLine = LineRenderer::GetInstance();

    // 画面外なら線を積まない
    const Vector3 halfSize = cachedOBB_.size;
    const float boundingRadius = halfSize.Length() + (cachedOBB_.scaleCenter - cachedOBB_.rotationCenter).Length();
    if (!pLine->IsSphereVisible(cachedOBB_.rotationCenter, boundingRadius))
    {
        return;
    }

    // AddBoxCorners は 0-3 が手前面、4-7 が対応する奥面という並びを期待するので、
    // ビット順（x:1, y:2, z:4）で作った頂点をその並びへ差し替える
    Vector3 bits[8];
    const Vector3 scaleOffset = cachedOBB_.scaleCenter - cachedOBB_.rotationCenter;
    for (int i = 0; i < 8; i++)
    {
        const Vector3 localPosition = Vector3(
            (i & 1) ? halfSize.x : -halfSize.x,
            (i & 2) ? halfSize.y : -halfSize.y,
            (i & 4) ? halfSize.z : -halfSize.z);

        const Vector3 scaledPosition = localPosition + scaleOffset;

        const Vector3 rotatedPosition =
            cachedOBB_.orientations[0] * scaledPosition.x +
            cachedOBB_.orientations[1] * scaledPosition.y +
            cachedOBB_.orientations[2] * scaledPosition.z;

        bits[i] = cachedOBB_.rotationCenter + rotatedPosition;
    }

    const Vector3 corners[8] = {bits[0], bits[1], bits[3], bits[2], bits[4], bits[5], bits[7], bits[6]};
    pLine->AddBoxCorners(corners, color_);

    DrawRotationCenter(viewProjection);
}

void OBBCollider::DrawRotationCenter(const ViewProjection &viewProjection)
{
    // 回転中心の目印。3つの大円で描くので旧実装（緯度経度メッシュ200本）より大幅に軽い
    LineRenderer::GetInstance()->AddSphere(cachedOBB_.rotationCenter, 0.1f, color_, 12);
}

void OBBCollider::SaveToJson()
{
    ColliderBase::SaveToJson();

    dataHandler_->Save("size", size_);
    dataHandler_->Save("rotationOffset", rotationOffset_);
    dataHandler_->Save("scaleOffset", positionOffset_);
    dataHandler_->Save("anchorPoint", anchorPoint_);
}

void OBBCollider::LoadFromJson()
{
    ColliderBase::LoadFromJson();

    size_ = dataHandler_->Load<Vector3>("size", {1.0f, 1.0f, 1.0f});
    rotationOffset_ = dataHandler_->Load<Vector3>("rotationOffset", {0.0f, 0.0f, 0.0f});
    positionOffset_ = dataHandler_->Load<Vector3>("scaleOffset", {0.0f, 0.0f, 0.0f});
    anchorPoint_ = dataHandler_->Load<Vector3>("anchorPoint", {0.5f, 0.5f, 0.5f});
}
} // namespace Hagine
