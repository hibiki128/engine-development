#pragma once
#include "Model/ModelStructs.h"

/// <summary>
/// ボーン管理クラス
/// スケルトン構造の構築、アニメーション適用、ジョイント情報の取得を行う
/// </summary>
namespace Hagine {
class Bone {
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
    /// <param name="animtaionTime">アニメーション時間</param>
    void Update(const Animation &animation, float animtaionTime);

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

  private:
    /// ===================================================
    /// private variants
    /// ===================================================

    Skeleton skeleton_; // スケルトンデータ
};
} // namespace Hagine
