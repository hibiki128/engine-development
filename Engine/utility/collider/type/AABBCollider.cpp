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

void AABBCollider::SaveShapeToJson(DataHandler &json)
{
    json.Save("size", size_);
    json.Save("offset", offset_);
}

void AABBCollider::LoadShapeFromJson(DataHandler &json)
{
    size_ = json.Load<Vector3>("size", size_);
    offset_ = json.Load<Vector3>("offset", offset_);
}
} // namespace Hagine
