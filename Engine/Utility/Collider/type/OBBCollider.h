#pragma once
#include "../ColliderBase.h"

namespace Hagine {

/// <summary>
/// 有向境界ボックス（OBB）のコライダー
/// 回転を考慮したボックス判定を行う
/// </summary>
class OBBCollider : public ColliderBase {
  public:
    /// ===================================================
    /// public method
    /// ===================================================

    /// <summary>
    /// コンストラクタ
    /// </summary>
    OBBCollider() = default;

    /// <summary>
    /// デストラクタ
    /// </summary>
    ~OBBCollider() override = default;

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
    /// 回転オフセットを設定
    /// </summary>
    /// <param name="offset">設定する回転オフセット</param>
    void SetRotationOffset(const Vector3 &offset) { rotationOffset_ = offset; }

    /// <summary>
    /// 回転オフセットを取得
    /// </summary>
    /// <returns>const Vector3&: 現在の回転オフセット</returns>
    const Vector3 &GetRotationOffset() const { return rotationOffset_; }

    /// <summary>
    /// 位置オフセットを設定
    /// </summary>
    /// <param name="offset">設定する位置オフセット</param>
    void SetPositionOffSet(const Vector3 &offset) { positionOffset_ = offset; }

    /// <summary>
    /// 位置オフセットを取得
    /// </summary>
    /// <returns>const Vector3&: 現在の位置オフセット</returns>
    const Vector3 &GetPositionOffset() const { return positionOffset_; }

    /// <summary>
    /// 計算済みのOBB形状データを取得
    /// </summary>
    /// <returns>OBB: 中心・向き・サイズを持つボックスデータ</returns>
    OBB GetOBB() const { return cachedOBB_; }

    /// <summary>
    /// コライダーの形状種別を取得
    /// </summary>
    /// <returns>ColliderType: OBB</returns>
    ColliderType GetType() const override { return ColliderType::OBB; }

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

    /// <summary>
    /// 回転の基準となるアンカーポイントを設定
    /// </summary>
    /// <param name="anchor">設定するアンカーポイント</param>
    void SetAnchorPoint(const Vector3 &anchor) { anchorPoint_ = anchor; }

    /// <summary>
    /// 回転の基準となるアンカーポイントを取得
    /// </summary>
    /// <returns>const Vector3&: 現在のアンカーポイント</returns>
    const Vector3 &GetAnchorPoint() const { return anchorPoint_; }

  private:
    /// ===================================================
    /// private method
    /// ===================================================

    /// <summary>
    /// 回転からOBBの各軸の向きを計算
    /// </summary>
    /// <param name="rotation">基準となる回転</param>
    void MakeOBBOrientations(const Quaternion &rotation);

    /// <summary>
    /// OBBのスケールと中心を更新
    /// </summary>
    void UpdateOBBScaleCenter();

    /// <summary>
    /// 回転中心をデバッグ描画
    /// </summary>
    /// <param name="viewProjection">ビュープロジェクション</param>
    void DrawRotationCenter(const ViewProjection &viewProjection);

    /// ===================================================
    /// private variants
    /// ===================================================

    Vector3 size_ = {1.0f, 1.0f, 1.0f};           // サイズ
    Vector3 rotationOffset_ = {0.0f, 0.0f, 0.0f};  // 回転オフセット
    Vector3 positionOffset_ = {0.0f, 0.0f, 0.0f};  // 位置オフセット
    OBB cachedOBB_;                                // 計算済みのOBB形状データ
    Vector3 anchorPoint_ = {0.5f, 0.5f, 0.5f};     // 回転基準のアンカーポイント
};
} // namespace Hagine
