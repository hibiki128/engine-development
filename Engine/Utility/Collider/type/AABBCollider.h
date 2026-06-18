#pragma once
#include "../ColliderBase.h"

namespace Hagine {

/// <summary>
/// 軸平行境界ボックス（AABB）のコライダー
/// </summary>
class AABBCollider : public ColliderBase {
  public:
    /// ===================================================
    /// public method
    /// ===================================================

    /// <summary>
    /// コンストラクタ
    /// </summary>
    AABBCollider() = default;

    /// <summary>
    /// デストラクタ
    /// </summary>
    ~AABBCollider() override = default;

    /// <summary>
    /// サイズを設定
    /// </summary>
    /// <param name="size">設定するサイズ</param>
    void SetSize(const Vector3 &size) { size_ = size; }

    /// <summary>
    /// サイズを取得
    /// </summary>
    /// <returns>const Vector3&: 現在のサイズ</returns>
    const Vector3 &GetSize() const { return size_; }

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
    /// 現在のワールド情報からAABBの形状データを取得
    /// </summary>
    /// <returns>AABB: 最小点と最大点を持つボックスデータ</returns>
    AABB GetAABB() const {
        Vector3 center = GetCenterPosition() + offset_;
        Vector3 halfSize = size_ * 0.5f;
        return AABB{center - halfSize, center + halfSize};
    }

    /// <summary>
    /// コライダーの形状種別を取得
    /// </summary>
    /// <returns>ColliderType: AABB</returns>
    ColliderType GetType() const override { return ColliderType::AABB; }

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

    Vector3 size_ = {1.0f, 1.0f, 1.0f};   // サイズ
    Vector3 offset_ = {0.0f, 0.0f, 0.0f}; // 中心からのオフセット
    AABB cachedAABB_;                     // 計算済みのAABB形状データ
};
} // namespace Hagine
