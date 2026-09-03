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

void SphereCollider::SaveShapeToJson(DataHandler &json)
{
    json.Save("radius", radius_);
    json.Save("offset", offset_);
}

void SphereCollider::LoadShapeFromJson(DataHandler &json)
{
    radius_ = json.Load<float>("radius", radius_);
    offset_ = json.Load<Vector3>("offset", offset_);
}
} // namespace Hagine
