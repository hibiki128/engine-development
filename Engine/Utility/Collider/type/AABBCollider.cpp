#include "AABBCollider.h"
#include "line/DrawLine3D.h"
#include <array>

namespace Hagine {
void AABBCollider::UpdateWorldTransform() {
    Vector3 center = GetCenterPosition() + offset_;
    Vector3 halfSize = size_ * 0.5f;
    cachedAABB_.min = center - halfSize;
    cachedAABB_.max = center + halfSize;
}

void AABBCollider::DebugDraw(const ViewProjection &viewProjection) {
    if (!isVisible_ || !isEnabled_) {
        return;
    }

    std::array<Vector3, 8> vertices = {
        cachedAABB_.min,
        {cachedAABB_.max.x, cachedAABB_.min.y, cachedAABB_.min.z},
        {cachedAABB_.min.x, cachedAABB_.max.y, cachedAABB_.min.z},
        {cachedAABB_.max.x, cachedAABB_.max.y, cachedAABB_.min.z},
        {cachedAABB_.min.x, cachedAABB_.min.y, cachedAABB_.max.z},
        {cachedAABB_.max.x, cachedAABB_.min.y, cachedAABB_.max.z},
        {cachedAABB_.min.x, cachedAABB_.max.y, cachedAABB_.max.z},
        cachedAABB_.max};

    const std::array<std::pair<int, int>, 12> edges = {
        std::make_pair(0, 1), std::make_pair(1, 3), std::make_pair(3, 2), std::make_pair(2, 0),
        std::make_pair(4, 5), std::make_pair(5, 7), std::make_pair(7, 6), std::make_pair(6, 4),
        std::make_pair(0, 4), std::make_pair(1, 5), std::make_pair(2, 6), std::make_pair(3, 7)};

    for (const auto &edge : edges) {
        DrawLine3D::GetInstance()->SetPoints(vertices[edge.first], vertices[edge.second], color_);
    }
}

void AABBCollider::SaveToJson() {
    ColliderBase::SaveToJson();

    dataHandler_->Save("size", size_);
    dataHandler_->Save("offset", offset_);
}

void AABBCollider::LoadFromJson() {
    ColliderBase::LoadFromJson();

    size_ = dataHandler_->Load<Vector3>("size", {1.0f, 1.0f, 1.0f});
    offset_ = dataHandler_->Load<Vector3>("offset", {0.0f, 0.0f, 0.0f});
}
} // namespace Hagine
