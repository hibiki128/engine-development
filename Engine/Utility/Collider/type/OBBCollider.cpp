#include "OBBCollider.h"
#include "line/DrawLine3D.h"
#include "myMath.h"
#include <array>
#include <numbers>

namespace Hagine {
void OBBCollider::UpdateWorldTransform() {
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

void OBBCollider::MakeOBBOrientations(const Quaternion &rotation) {
    Matrix4x4 rotateMatrix = QuaternionToMatrix4x4(rotation);

    cachedOBB_.orientations[0] = Vector3(rotateMatrix.m[0][0], rotateMatrix.m[0][1], rotateMatrix.m[0][2]);
    cachedOBB_.orientations[1] = Vector3(rotateMatrix.m[1][0], rotateMatrix.m[1][1], rotateMatrix.m[1][2]);
    cachedOBB_.orientations[2] = Vector3(rotateMatrix.m[2][0], rotateMatrix.m[2][1], rotateMatrix.m[2][2]);
}

void OBBCollider::UpdateOBBScaleCenter() {
    cachedOBB_.scaleCenterRotated =
        cachedOBB_.orientations[0] * (cachedOBB_.scaleCenter.x - cachedOBB_.rotationCenter.x) +
        cachedOBB_.orientations[1] * (cachedOBB_.scaleCenter.y - cachedOBB_.rotationCenter.y) +
        cachedOBB_.orientations[2] * (cachedOBB_.scaleCenter.z - cachedOBB_.rotationCenter.z) +
        cachedOBB_.rotationCenter;
}

void OBBCollider::DebugDraw(const ViewProjection &viewProjection) {
    if (!isVisible_ || !isEnabled_) {
        return;
    }

    std::array<Vector3, 8> vertices;
    Vector3 halfSize = cachedOBB_.size;

    for (int i = 0; i < 8; i++) {
        Vector3 localPosition = Vector3(
            (i & 1) ? halfSize.x : -halfSize.x,
            (i & 2) ? halfSize.y : -halfSize.y,
            (i & 4) ? halfSize.z : -halfSize.z);

        Vector3 scaledPosition = localPosition + (cachedOBB_.scaleCenter - cachedOBB_.rotationCenter);

        Vector3 rotatedPosition =
            cachedOBB_.orientations[0] * scaledPosition.x +
            cachedOBB_.orientations[1] * scaledPosition.y +
            cachedOBB_.orientations[2] * scaledPosition.z;

        vertices[i] = cachedOBB_.rotationCenter + rotatedPosition;
    }

    const std::array<std::pair<int, int>, 12> edges = {
        std::make_pair(0, 1), std::make_pair(1, 3), std::make_pair(3, 2), std::make_pair(2, 0),
        std::make_pair(4, 5), std::make_pair(5, 7), std::make_pair(7, 6), std::make_pair(6, 4),
        std::make_pair(0, 4), std::make_pair(1, 5), std::make_pair(2, 6), std::make_pair(3, 7)};

    for (const auto &edge : edges) {
        DrawLine3D::GetInstance()->SetPoints(vertices[edge.first], vertices[edge.second], color_);
    }

    DrawRotationCenter(viewProjection);
}

void OBBCollider::DrawRotationCenter(const ViewProjection &viewProjection) {
    float radius = 0.1f;
    const uint32_t kSubdivision = 10;
    const float kLonEvery = 2.0f * std::numbers::pi_v<float> / kSubdivision;
    const float kLatEvery = std::numbers::pi_v<float> / kSubdivision;

    for (uint32_t latIndex = 0; latIndex < kSubdivision; ++latIndex) {
        float lat = -std::numbers::pi_v<float> / 2.0f + kLatEvery * latIndex;

        for (uint32_t lonIndex = 0; lonIndex < kSubdivision; ++lonIndex) {
            float lon = lonIndex * kLonEvery;

            Vector3 start = {
                cachedOBB_.rotationCenter.x + radius * std::cosf(lat) * std::cosf(lon),
                cachedOBB_.rotationCenter.y + radius * std::sinf(lat),
                cachedOBB_.rotationCenter.z + radius * std::cosf(lat) * std::sinf(lon)};

            Vector3 end1 = {
                cachedOBB_.rotationCenter.x + radius * std::cosf(lat) * std::cosf(lon + kLonEvery),
                cachedOBB_.rotationCenter.y + radius * std::sinf(lat),
                cachedOBB_.rotationCenter.z + radius * std::cosf(lat) * std::sinf(lon + kLonEvery)};

            Vector3 end2 = {
                cachedOBB_.rotationCenter.x + radius * std::cosf(lat + kLatEvery) * std::cosf(lon),
                cachedOBB_.rotationCenter.y + radius * std::sinf(lat + kLatEvery),
                cachedOBB_.rotationCenter.z + radius * std::cosf(lat + kLatEvery) * std::sinf(lon)};

            DrawLine3D::GetInstance()->SetPoints(start, end1, color_);
            DrawLine3D::GetInstance()->SetPoints(start, end2, color_);
        }
    }
}

void OBBCollider::SaveToJson() {
    ColliderBase::SaveToJson();

    dataHandler_->Save("size", size_);
    dataHandler_->Save("rotationOffset", rotationOffset_);
    dataHandler_->Save("scaleOffset", positionOffset_);
    dataHandler_->Save("anchorPoint", anchorPoint_);
}

void OBBCollider::LoadFromJson() {
    ColliderBase::LoadFromJson();

    size_ = dataHandler_->Load<Vector3>("size", {1.0f, 1.0f, 1.0f});
    rotationOffset_ = dataHandler_->Load<Vector3>("rotationOffset", {0.0f, 0.0f, 0.0f});
    positionOffset_ = dataHandler_->Load<Vector3>("scaleOffset", {0.0f, 0.0f, 0.0f});
    anchorPoint_ = dataHandler_->Load<Vector3>("anchorPoint", {0.5f, 0.5f, 0.5f});
}
} // namespace Hagine
