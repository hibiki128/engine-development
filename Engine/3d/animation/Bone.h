#pragma once
#include "model/ModelStructs.h"

/// <summary>
/// ボーン管理クラス
/// スケルトン構造の構築、アニメーション適用、ジョイント情報の取得を行う
/// </summary>
namespace Hagine {
class Bone
{
  public:
    /// ===================================================
    /// public method
    /// ===================================================

    /// <summary>
    /// 初期化
    /// </summary>
    /// <param name="modelData">モデルデータ</param>
    void Initialize(ModelData modelData);

    /// <summary>
    /// 更新処理
    /// </summary>
    /// <param name="animation">アニメーションデータ</param>
    /// <param name="animationTime">アニメーション時間</param>
    void Update(const Animation &animation, float animationTime);

    /// <summary>
    /// レイヤー合成付きの更新処理
    /// 全身へ基準アニメーションを適用したあと、マスクで指定したジョイントだけ
    /// レイヤーアニメーションで上書きする（上半身だけ別モーションにする用途）
    /// </summary>
    /// <param name="baseAnimation">全身に適用する基準アニメーション</param>
    /// <param name="baseTime">基準アニメーションの再生時間</param>
    /// <param name="layerAnimation">一部のジョイントへ上書きするアニメーション</param>
    /// <param name="layerTime">レイヤーアニメーションの再生時間</param>
    /// <param name="mask">ジョイントインデックスごとの適用フラグ（1で上書き対象）</param>
    /// <param name="weight">上書きの強さ（0で基準のみ・1で完全にレイヤー）</param>
    void UpdateLayered(const Animation &baseAnimation, float baseTime,
                       const Animation &layerAnimation, float layerTime,
                       const std::vector<uint8_t> &mask, float weight);

    /// <summary>
    /// 指定ジョイントとその子孫だけを立てたマスクを生成する
    /// </summary>
    /// <param name="rootJointName">部分木の根になるジョイント名</param>
    /// <returns>std::vector&lt;uint8_t&gt;: ジョイント数ぶんのマスク（見つからなければ全て0）</returns>
    std::vector<uint8_t> MakeSubtreeMask(const std::string &rootJointName) const;

    /// <summary>
    /// ジョイントのワールド座標を取得
    /// </summary>
    /// <param name="jointName">ジョイント名</param>
    /// <param name="worldMatrix">ワールド行列</param>
    /// <returns>std::optional<Vector3>: ジョイントのワールド座標</returns>
    std::optional<Vector3> GetJointWorldPosition(const std::string &jointName, const Matrix4x4 &worldMatrix) const;

    /// <summary>
    /// ジョイントのスケルトン空間行列を取得
    /// </summary>
    /// <param name="jointName">ジョイント名</param>
    /// <returns>std::optional<Matrix4x4>: スケルトン空間行列</returns>
    std::optional<Matrix4x4> GetJointSkeletonSpaceMatrix(const std::string &jointName) const;

    /// <summary>
    /// ジョイントのワールド行列を取得
    /// </summary>
    /// <param name="jointName">ジョイント名</param>
    /// <param name="worldMatrix">ワールド行列</param>
    /// <returns>std::optional<Matrix4x4>: ジョイントのワールド行列</returns>
    std::optional<Matrix4x4> GetJointWorldMatrix(const std::string &jointName, const Matrix4x4 &worldMatrix) const;

    /// <summary>
    /// Getter
    /// </summary>
    Skeleton GetSkeleton() { return skeleton_; }

    /// <summary>
    /// Setter
    /// </summary>
    void SetSkeleton(Skeleton &skeleton) { skeleton_ = skeleton; }

  private:
    /// ===================================================
    /// private method
    /// ===================================================

    /// <summary>
    /// Joint作成
    /// </summary>
    /// <param name="node">ノードデータ</param>
    /// <param name="parent">親ジョイントのインデックス</param>
    /// <param name="joints">ジョイント配列</param>
    /// <returns>int32_t: 作成したジョイントのインデックス</returns>
    int32_t CreateJoint(const Node &node, const std::optional<int32_t> &parent, std::vector<Joint> &joints);

    /// <summary>
    /// 骨作成
    /// </summary>
    /// <param name="rootNode">ルートノード</param>
    /// <returns>Skeleton: 作成したスケルトン</returns>
    Skeleton CreateSkeleton(const Node &rootNode);

    /// <summary>
    /// アニメーションの適用
    /// </summary>
    /// <param name="animation">アニメーションデータ</param>
    /// <param name="animationTime">アニメーション時間</param>
    void ApplyAnimation(const Animation &animation, float animationTime);

    /// <summary>
    /// マスクで指定したジョイントへアニメーションを重ね書きする
    /// </summary>
    /// <param name="animation">アニメーションデータ</param>
    /// <param name="animationTime">アニメーション時間</param>
    /// <param name="mask">ジョイントインデックスごとの適用フラグ</param>
    /// <param name="weight">上書きの強さ（0〜1）</param>
    void ApplyLayer(const Animation &animation, float animationTime,
                    const std::vector<uint8_t> &mask, float weight);

    /// <summary>
    /// 現在のジョイントのSRTから階層行列を計算する
    /// </summary>
    void CalculateMatrices();

  private:
    /// ===================================================
    /// private variables
    /// ===================================================

    Skeleton skeleton_; // スケルトンデータ
};
} // namespace Hagine
