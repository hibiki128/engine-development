#include "CylinderCollider.h"
#include "line/DrawLine3D.h"
#include <numbers>

namespace Hagine {
void CylinderCollider::DebugDraw(const ViewProjection &viewProjection) {
    if (!isVisible_ || !isEnabled_) {
        return;
    }

    Vector3 center = GetCenterPosition();
    const int kDivision = 64;  // 上下円の分割数を増やす
    const int kVerticals = 16; // 縦線の本数
    const int kRings = 3;      // 中間リングの数（上下除く）
    const float kStep = 2.0f * std::numbers::pi_v<float> / kDivision;
    float halfH = height_ * 0.5f;

    // 上面・下面の円
    for (int i = 0; i < kDivision; ++i) {
        float a0 = kStep * i;
        float a1 = kStep * (i + 1);

        Vector3 top0 = {center.x + radius_ * std::cos(a0), center.y + halfH, center.z + radius_ * std::sin(a0)};
        Vector3 top1 = {center.x + radius_ * std::cos(a1), center.y + halfH, center.z + radius_ * std::sin(a1)};
        Vector3 bot0 = {center.x + radius_ * std::cos(a0), center.y - halfH, center.z + radius_ * std::sin(a0)};
        Vector3 bot1 = {center.x + radius_ * std::cos(a1), center.y - halfH, center.z + radius_ * std::sin(a1)};

        DrawLine3D::GetInstance()->SetPoints(top0, top1, color_);
        DrawLine3D::GetInstance()->SetPoints(bot0, bot1, color_);
    }

    // 縦線
    const float kVertStep = 2.0f * std::numbers::pi_v<float> / kVerticals;
    for (int i = 0; i < kVerticals; ++i) {
        float a = kVertStep * i;
        Vector3 top = {center.x + radius_ * std::cos(a), center.y + halfH, center.z + radius_ * std::sin(a)};
        Vector3 bot = {center.x + radius_ * std::cos(a), center.y - halfH, center.z + radius_ * std::sin(a)};
        DrawLine3D::GetInstance()->SetPoints(top, bot, color_);
    }

    // 中間リング
    for (int r = 1; r <= kRings; ++r) {
        float ringY = center.y - halfH + height_ * (float)r / (float)(kRings + 1);
        for (int i = 0; i < kDivision; ++i) {
            float a0 = kStep * i;
            float a1 = kStep * (i + 1);
            Vector3 p0 = {center.x + radius_ * std::cos(a0), ringY, center.z + radius_ * std::sin(a0)};
            Vector3 p1 = {center.x + radius_ * std::cos(a1), ringY, center.z + radius_ * std::sin(a1)};
            DrawLine3D::GetInstance()->SetPoints(p0, p1, color_);
        }
    }
}
} // namespace Hagine
