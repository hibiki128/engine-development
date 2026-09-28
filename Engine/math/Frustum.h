#pragma once
#include "MyMath.h"
#include "type/Matrix4x4.h"
#include "type/Vector3.h"
#include "type/Vector4.h"

namespace Hagine {

/// <summary>
/// 視錐台。ビュー射影行列から6枚の平面を取り出し、境界ボリュームが画面に入るかを判定する
///
/// 平面は「内側が正」になるよう向きを揃えて正規化してあるので、
/// 点と平面の距離がそのまま「視錐台の内側へどれだけ入っているか」になる。
/// 行列が未初期化などで平面を作れなかったときは IsValid() が false になり、
/// 判定は常に true（＝カリングしない）を返す。切り捨てすぎるより描いた方が安全なため
/// </summary>
class Frustum
{
  public:
    /// ===================================================
    /// public method
    /// ===================================================

    /// <summary>
    /// ビュー射影行列から6平面を抽出する
    /// </summary>
    /// <param name="viewProjection">ビュー行列 × 射影行列</param>
    void ExtractFromViewProjection(const Matrix4x4 &viewProjection);

    /// <summary>
    /// 平面を抽出できているか
    /// </summary>
    /// <returns>bool: 抽出できていれば true</returns>
    bool IsValid() const { return isValid_; }

    /// <summary>
    /// 球が視錐台と交差するか
    /// </summary>
    /// <param name="center">球の中心（ワールド空間）</param>
    /// <param name="radius">球の半径</param>
    /// <returns>bool: 完全に外側なら false</returns>
    bool IsSphereVisible(const Vector3 &center, float radius) const;

    /// <summary>
    /// ワールド空間の境界ボックスが視錐台と交差するか
    /// </summary>
    /// <param name="worldBounds">ワールド空間のAABB</param>
    /// <returns>bool: 完全に外側なら false</returns>
    bool IsAabbVisible(const AABB &worldBounds) const;

    /// <summary>
    /// ローカル空間のAABBにワールド行列を掛け、それを包む軸平行なAABBを求める
    /// </summary>
    /// <param name="localBounds">ローカル空間のAABB</param>
    /// <param name="worldMatrix">ワールド行列</param>
    /// <returns>AABB: ワールド空間の境界ボックス</returns>
    static AABB TransformAabb(const AABB &localBounds, const Matrix4x4 &worldMatrix);

  private:
    /// ===================================================
    /// private variables
    /// ===================================================

    Vector4 planes_[6]{};  // 視錐台平面（xyz=法線, w=距離）
    bool isValid_ = false; // 平面を抽出できたか
};
} // namespace Hagine
