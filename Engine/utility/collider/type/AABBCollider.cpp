#include "AABBCollider.h"
#include "line/LineRenderer.h"

namespace Hagine {
void AABBCollider::UpdateWorldTransform()
{
    Vector3 center = GetCenterPosition() + offset_;
    Vector3 halfSize = size_ * 0.5f;
    cachedAABB_.min = center - halfSize;
    cachedAABB_.max = center + halfSize;
}

void AABBCollider::DebugDraw(const ViewProjection &viewProjection)
{
    if (!isVisible_ || !isEnabled_)
    {
        return;
    }

    LineRenderer *pLine = LineRenderer::GetInstance();

    // 画面外なら線を積まない
    const Vector3 center = (cachedAABB_.min + cachedAABB_.max) * 0.5f;
    const Vector3 extent = (cachedAABB_.max - cachedAABB_.min) * 0.5f;
    const float boundingRadius = extent.Length();
    if (!pLine->IsSphereVisible(center, boundingRadius))
    {
        return;
    }

    pLine->AddBox(cachedAABB_.min, cachedAABB_.max, color_);
}

void AABBCollider::SaveToJson()
{
    ColliderBase::SaveToJson();

    dataHandler_->Save("size", size_);
    dataHandler_->Save("offset", offset_);
}

void AABBCollider::LoadFromJson()
{
    ColliderBase::LoadFromJson();

    size_ = dataHandler_->Load<Vector3>("size", {1.0f, 1.0f, 1.0f});
    offset_ = dataHandler_->Load<Vector3>("offset", {0.0f, 0.0f, 0.0f});
}
} // namespace Hagine
