#include "SphereCollider.h"
#include "line/LineRenderer.h"

namespace Hagine {
void SphereCollider::UpdateWorldTransform()
{
    cachedSphere_.center = GetCenterPosition() + offset_;
    cachedSphere_.radius = radius_;
}

void SphereCollider::DebugDraw(const ViewProjection &viewProjection)
{
    if (!isVisible_ || !isEnabled_)
    {
        return;
    }

    // 3つの大円で表現する（旧実装の緯度経度メッシュ200本に対して48本）。
    // 視錐台カリングと三角関数テーブルは LineRenderer 側で行われる。
    LineRenderer::GetInstance()->AddSphere(cachedSphere_.center, cachedSphere_.radius, color_, 16);
}

void SphereCollider::SaveToJson()
{
    ColliderBase::SaveToJson();

    dataHandler_->Save("radius", radius_);
    dataHandler_->Save("offset", offset_);
}

void SphereCollider::LoadFromJson()
{
    ColliderBase::LoadFromJson();

    radius_ = dataHandler_->Load<float>("radius", 1.0f);
    offset_ = dataHandler_->Load<Vector3>("offset", {0.0f, 0.0f, 0.0f});
}
} // namespace Hagine
