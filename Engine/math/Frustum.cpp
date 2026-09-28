#include "Frustum.h"
#include <cmath>

namespace Hagine {

void Frustum::ExtractFromViewProjection(const Matrix4x4 &viewProjection)
{
    // 行ベクトル規約（clip = pos * M）なので、平面は列の和差で得られる
    auto column = [&viewProjection](int index) -> Vector4 {
        return Vector4(viewProjection.m[0][index], viewProjection.m[1][index],
                       viewProjection.m[2][index], viewProjection.m[3][index]);
    };

    const Vector4 c0 = column(0);
    const Vector4 c1 = column(1);
    const Vector4 c2 = column(2);
    const Vector4 c3 = column(3);

    const Vector4 raw[6] = {
        {c3.x + c0.x, c3.y + c0.y, c3.z + c0.z, c3.w + c0.w}, // 左
        {c3.x - c0.x, c3.y - c0.y, c3.z - c0.z, c3.w - c0.w}, // 右
        {c3.x + c1.x, c3.y + c1.y, c3.z + c1.z, c3.w + c1.w}, // 下
        {c3.x - c1.x, c3.y - c1.y, c3.z - c1.z, c3.w - c1.w}, // 上
        {c2.x, c2.y, c2.z, c2.w},                             // 近
        {c3.x - c2.x, c3.y - c2.y, c3.z - c2.z, c3.w - c2.w}, // 遠
    };

    for (int i = 0; i < 6; ++i)
    {
        const float length = std::sqrt(raw[i].x * raw[i].x + raw[i].y * raw[i].y + raw[i].z * raw[i].z);
        if (length <= 1e-6f)
        {
            // 行列が未初期化などで平面を作れない場合はカリングを無効化する
            isValid_ = false;
            return;
        }
        const float inv = 1.0f / length;
        planes_[i] = {raw[i].x * inv, raw[i].y * inv, raw[i].z * inv, raw[i].w * inv};
    }
    isValid_ = true;
}

bool Frustum::IsSphereVisible(const Vector3 &center, float radius) const
{
    if (!isValid_)
    {
        return true;
    }
    for (const Vector4 &plane : planes_)
    {
        const float distance = plane.x * center.x + plane.y * center.y + plane.z * center.z + plane.w;
        if (distance < -radius)
        {
            return false;
        }
    }
    return true;
}

bool Frustum::IsAabbVisible(const AABB &worldBounds) const
{
    if (!isValid_)
    {
        return true;
    }

    for (const Vector4 &plane : planes_)
    {
        // 平面の法線側にいちばん寄っている角（正の頂点）だけを調べれば足りる。
        // そこが平面の裏にあるなら、残り7つの角も必ず裏にある
        const Vector3 positiveVertex = {
            plane.x >= 0.0f ? worldBounds.max.x : worldBounds.min.x,
            plane.y >= 0.0f ? worldBounds.max.y : worldBounds.min.y,
            plane.z >= 0.0f ? worldBounds.max.z : worldBounds.min.z,
        };

        const float distance = plane.x * positiveVertex.x + plane.y * positiveVertex.y +
                               plane.z * positiveVertex.z + plane.w;
        if (distance < 0.0f)
        {
            return false;
        }
    }
    return true;
}

AABB Frustum::TransformAabb(const AABB &localBounds, const Matrix4x4 &worldMatrix)
{
    // 8頂点を個別に変換しなくても、中心と半径ベクトルを分けて扱えば同じ結果が得られる。
    // 半径ベクトルには行列の絶対値を掛ける（回転でどちら向きに伸びても取りこぼさないため）
    const Vector3 center = {
        (localBounds.min.x + localBounds.max.x) * 0.5f,
        (localBounds.min.y + localBounds.max.y) * 0.5f,
        (localBounds.min.z + localBounds.max.z) * 0.5f,
    };
    const Vector3 extent = {
        (localBounds.max.x - localBounds.min.x) * 0.5f,
        (localBounds.max.y - localBounds.min.y) * 0.5f,
        (localBounds.max.z - localBounds.min.z) * 0.5f,
    };

    // 行ベクトル規約なので、ワールドX = c.x*m[0][0] + c.y*m[1][0] + c.z*m[2][0] + m[3][0]
    const Vector3 worldCenter = {
        center.x * worldMatrix.m[0][0] + center.y * worldMatrix.m[1][0] + center.z * worldMatrix.m[2][0] + worldMatrix.m[3][0],
        center.x * worldMatrix.m[0][1] + center.y * worldMatrix.m[1][1] + center.z * worldMatrix.m[2][1] + worldMatrix.m[3][1],
        center.x * worldMatrix.m[0][2] + center.y * worldMatrix.m[1][2] + center.z * worldMatrix.m[2][2] + worldMatrix.m[3][2],
    };

    const Vector3 worldExtent = {
        extent.x * std::fabs(worldMatrix.m[0][0]) + extent.y * std::fabs(worldMatrix.m[1][0]) + extent.z * std::fabs(worldMatrix.m[2][0]),
        extent.x * std::fabs(worldMatrix.m[0][1]) + extent.y * std::fabs(worldMatrix.m[1][1]) + extent.z * std::fabs(worldMatrix.m[2][1]),
        extent.x * std::fabs(worldMatrix.m[0][2]) + extent.y * std::fabs(worldMatrix.m[1][2]) + extent.z * std::fabs(worldMatrix.m[2][2]),
    };

    return AABB{
        {worldCenter.x - worldExtent.x, worldCenter.y - worldExtent.y, worldCenter.z - worldExtent.z},
        {worldCenter.x + worldExtent.x, worldCenter.y + worldExtent.y, worldCenter.z + worldExtent.z},
    };
}
} // namespace Hagine
