#pragma once
#include "../ColliderBase.h"
#include <cmath>

/// <summary>
/// 円柱コライダー
/// 内側に押し戻す用途にも、外側に押し出す用途にも使える汎用円柱判定
/// </summary>
namespace Hagine {
class CylinderCollider : public ColliderBase {
  public:
    CylinderCollider() = default;
    ~CylinderCollider() override = default;

    void SetRadius(float radius) { radius_ = radius; }
    float GetRadius() const { return radius_; }

    void SetHeight(float height) { height_ = height; }
    float GetHeight() const { return height_; }

    /// <summary>
    /// 内側制約か外側制約かを設定
    /// true  = キャラを内側に閉じ込める（フィールド壁）
    /// false = キャラを外側に押し出す（障害物）
    /// </summary>
    void SetInward(bool inward) { inward_ = inward; }
    bool IsInward() const { return inward_; }

    ColliderType GetType() const override { return ColliderType::Cylinder; }
    void UpdateWorldTransform() override { /* 中心位置はgetPositionFunc_から取得 */ }
    void DebugDraw(const ViewProjection &viewProjection) override;

    /// <summary>
    /// 対象座標・速度を円柱に対して押し戻す
    /// inward=true  → 外に出たら内側へ
    /// inward=false → 内側に入ったら外側へ
    /// </summary>
    void Clamp(Vector3 &position, Vector3 &velocity) const {
        Vector3 center = GetCenterPosition();

        float dx = position.x - center.x;
        float dz = position.z - center.z;
        float distXZ = std::sqrt(dx * dx + dz * dz);

        bool outside = distXZ > radius_;

        // inward=true  のとき: 外にいれば押し戻す
        // inward=false のとき: 内にいれば押し出す
        if (inward_ != outside) {
            return; // 押し戻し不要
        }

        if (distXZ < 0.0001f) {
            // 中心と完全に重なっている場合はZ方向に逃がす
            position.z = center.z + (inward_ ? -radius_ : radius_);
            return;
        }

        float nx = dx / distXZ;
        float nz = dz / distXZ;

        // 境界面上に補正
        position.x = center.x + nx * radius_;
        position.z = center.z + nz * radius_;

        // 境界を突き抜けようとする速度成分を除去
        // inward=true  → 外向き(dot>0)の速度を除去
        // inward=false → 内向き(dot<0)の速度を除去
        float dot = velocity.x * nx + velocity.z * nz;
        if (inward_ && dot > 0.0f) {
            velocity.x -= nx * dot;
            velocity.z -= nz * dot;
        } else if (!inward_ && dot < 0.0f) {
            velocity.x -= nx * dot;
            velocity.z -= nz * dot;
        }
    }

  private:
    float radius_ = 30.0f;
    float height_ = 100.0f; // Y方向は現状チェックなし（必要なら追加可）
    bool inward_ = true;    // デフォルトはフィールド壁（内側に閉じ込める）
};
} // namespace Hagine
