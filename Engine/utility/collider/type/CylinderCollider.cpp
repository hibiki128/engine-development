#include "CylinderCollider.h"
#include "line/LineRenderer.h"

namespace Hagine {
void CylinderCollider::DebugDraw(const ViewProjection &viewProjection)
{
    if (!isVisible_ || !isEnabled_)
    {
        return;
    }

    // 上下の円＋4本の縦線。旧実装は分割64・中間リング3枚で336本を毎フレーム
    // 三角関数付きで生成していたが、輪郭の把握には24分割の輪郭で足りる。
    // 視錐台カリングと三角関数テーブルは LineRenderer 側で行われる。
    LineRenderer::GetInstance()->AddCylinder(GetCenterPosition(), radius_, height_ * 0.5f, color_, 24);
}
} // namespace Hagine
