#include "SphereCollider.h"
#include "line/DrawLine3D.h"
#include <numbers>

namespace Hagine {
void SphereCollider::UpdateWorldTransform() {
    cachedSphere_.center = GetCenterPosition() + offset_;
    cachedSphere_.radius = radius_;
}

void SphereCollider::DebugDraw(const ViewProjection &viewProjection) {
    if (!isVisible_ || !isEnabled_) {
        return;
    }

    const uint32_t kSubdivision = 10;
    const float kLonEvery = 2.0f * std::numbers::pi_v<float> / kSubdivision;
    const float kLatEvery = std::numbers::pi_v<float> / kSubdivision;

    for (uint32_t latIndex = 0; latIndex < kSubdivision; ++latIndex) {
        float lat = -std::numbers::pi_v<float> / 2.0f + kLatEvery * latIndex;

        for (uint32_t lonIndex = 0; lonIndex < kSubdivision; ++lonIndex) {
            float lon = lonIndex * kLonEvery;

            Vector3 start = {
                cachedSphere_.center.x + cachedSphere_.radius * std::cosf(lat) * std::cosf(lon),
                cachedSphere_.center.y + cachedSphere_.radius * std::sinf(lat),
                cachedSphere_.center.z + cachedSphere_.radius * std::cosf(lat) * std::sinf(lon)};

            Vector3 end1 = {
                cachedSphere_.center.x + cachedSphere_.radius * std::cosf(lat) * std::cosf(lon + kLonEvery),
                cachedSphere_.center.y + cachedSphere_.radius * std::sinf(lat),
                cachedSphere_.center.z + cachedSphere_.radius * std::cosf(lat) * std::sinf(lon + kLonEvery)};

            Vector3 end2 = {
                cachedSphere_.center.x + cachedSphere_.radius * std::cosf(lat + kLatEvery) * std::cosf(lon),
                cachedSphere_.center.y + cachedSphere_.radius * std::sinf(lat + kLatEvery),
                cachedSphere_.center.z + cachedSphere_.radius * std::cosf(lat + kLatEvery) * std::sinf(lon)};

            DrawLine3D::GetInstance()->SetPoints(start, end1, color_);
            DrawLine3D::GetInstance()->SetPoints(start, end2, color_);
        }
    }
}

void SphereCollider::SaveToJson() {
    ColliderBase::SaveToJson();

    dataHandler_->Save("radius", radius_);
    dataHandler_->Save("offset", offset_);
}

void SphereCollider::LoadFromJson() {
    ColliderBase::LoadFromJson();

    radius_ = dataHandler_->Load<float>("radius", 1.0f);
    offset_ = dataHandler_->Load<Vector3>("offset", {0.0f, 0.0f, 0.0f});
}
} // namespace Hagine
