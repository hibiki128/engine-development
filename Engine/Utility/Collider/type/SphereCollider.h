#pragma once
#include "../ColliderBase.h"

namespace Hagine {

/// <summary>
/// 球形のコライダー
/// </summary>
class SphereCollider : public ColliderBase {
  public:
    /// ===================================================
    /// public method
    /// ===================================================

    /// <summary>
    /// コンストラクタ
    /// </summary>
    SphereCollider() = default;

    /// <summary>
    /// デストラクタ
    /// </summary>
    ~SphereCollider() override = default;

    /// <summary>
    /// 半径を設定
    /// </summary>
    /// <param name="radius">設定する半径</param>
    void SetRadius(float radius) { radius_ = radius; }

    /// <summary>
    /// 半径を取得
    /// </summary>
    /// <returns>float: 現在の半径</returns>
    float GetRadius() const { return radius_; }

    /// <summary>
    /// 中心からのオフセットを設定
    /// </summary>
    /// <param name="offset">設定するオフセット</param>
    void SetOffset(const Vector3 &offset) { offset_ = offset; }

    /// <summary>
    /// 中心からのオフセットを取得
    /// </summary>
    /// <returns>const Vector3&: 現在のオフセット</returns>
    const Vector3 &GetOffset() const { return offset_; }

    /// <summary>
    /// 現在のワールド情報から球の形状データを取得
    /// </summary>
    /// <returns>Sphere: 中心と半径を持つ球データ</returns>
    Sphere GetSphere() const {
        Sphere sphere;
        sphere.center = GetCenterPosition() + offset_;
        sphere.radius = radius_;
        return sphere;
    }

    /// <summary>
    /// コライダーの形状種別を取得
    /// </summary>
    /// <returns>ColliderType: Sphere</returns>
    ColliderType GetType() const override { return ColliderType::Sphere; }

    /// <summary>
    /// ワールド変換を更新
    /// </summary>
    void UpdateWorldTransform() override;

    /// <summary>
    /// デバッグ描画
    /// </summary>
    /// <param name="viewProjection">ビュープロジェクション</param>
    void DebugDraw(const ViewProjection &viewProjection) override;

    /// <summary>
    /// 設定をJsonへ保存
    /// </summary>
    void SaveToJson() override;

    /// <summary>
    /// 設定をJsonから読み込み
    /// </summary>
    void LoadFromJson() override;

  private:
    /// ===================================================
    /// private variants
    /// ===================================================

    float radius_ = 1.0f;                 // 半径
    Vector3 offset_ = {0.0f, 0.0f, 0.0f}; // 中心からのオフセット
    Sphere cachedSphere_;                 // 計算済みの球形状データ
};
} // namespace Hagine
