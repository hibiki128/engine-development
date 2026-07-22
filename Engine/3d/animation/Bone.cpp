#include "Bone.h"
#include <MyMath.h>
#include "Animator.h"

namespace Hagine {
void Bone::Initialize(ModelData modelData)
{
    // モデルデータのルートノードからスケルトン構造を生成
    skeleton_ = CreateSkeleton(modelData.rootNode);
}

void Bone::Update(const Animation &animation, float animationTime)
{
    // 指定された時間のアニメーションデータをボーンのトランスフォームに適用
    ApplyAnimation(animation, animationTime);

    CalculateMatrices();
}

void Bone::UpdateLayered(const Animation &baseAnimation, float baseTime,
                         const Animation &layerAnimation, float layerTime,
                         const std::vector<uint8_t> &mask, float weight)
{
    // まず全身へ基準アニメーション（下半身側＝移動モーション）を適用し、
    // そのあとマスク対象のジョイントだけレイヤー側で上書きする。
    // 階層行列の計算は上書き後に一度だけ行えばよい
    ApplyAnimation(baseAnimation, baseTime);
    ApplyLayer(layerAnimation, layerTime, mask, weight);

    CalculateMatrices();
}

std::vector<uint8_t> Bone::MakeSubtreeMask(const std::string &rootJointName) const
{
    std::vector<uint8_t> mask(skeleton_.joints.size(), 0);

    auto it = skeleton_.jointMap.find(rootJointName);
    if (it == skeleton_.jointMap.end())
    {
        // 該当ジョイントが無いモデルではマスクが全て0になり、レイヤーは実質無効になる
        return mask;
    }

    // 根から子をたどって部分木を立てる
    std::vector<int32_t> stack{it->second};
    while (!stack.empty())
    {
        const int32_t index = stack.back();
        stack.pop_back();
        mask[index] = 1;
        for (int32_t child : skeleton_.joints[index].children)
        {
            stack.push_back(child);
        }
    }

    return mask;
}

void Bone::CalculateMatrices()
{
    // すべてのJointを更新。
    // 親ジョイントのインデックスが必ず子より小さいため、
    // 配列の順序通りに更新処理を行うことで、依存関係を壊さず正しい階層行列が計算できる
    for (Joint &joint : skeleton_.joints)
    {
        // SRTからローカル変換行列を作成
        joint.localMatrix = MakeBoneMatrix(joint.transform.scale, joint.transform.rotate, joint.transform.translate);

        if (joint.parent)
        {
            // 親ジョイントが存在する場合：ローカル行列 * 親のスケルトン空間行列
            joint.skeletonSpaceMatrix = joint.localMatrix * skeleton_.joints[*joint.parent].skeletonSpaceMatrix;
        }
        else
        {
            // ルートジョイント（親なし）の場合：ローカル行列がそのままスケルトン空間行列となる
            joint.skeletonSpaceMatrix = joint.localMatrix;
        }
    }
}

std::optional<Vector3> Bone::GetJointWorldPosition(const std::string &jointName, const Matrix4x4 &worldMatrix) const
{
    auto it = skeleton_.jointMap.find(jointName);
    if (it != skeleton_.jointMap.end())
    {
        const Joint &joint = skeleton_.joints[it->second];

        // スケルトン空間行列とモデルのワールド行列を掛け合わせる
        Matrix4x4 worldJointMatrix = joint.skeletonSpaceMatrix * worldMatrix;

        // ワールド行列の平行移動成分を抽出して位置とする
        Vector3 worldPosition = {
            worldJointMatrix.m[3][0],
            worldJointMatrix.m[3][1],
            worldJointMatrix.m[3][2]};

        return worldPosition;
    }
    // ジョイントが見つからなかった場合はnulloptを返す
    return std::nullopt;
}

std::optional<Matrix4x4> Bone::GetJointSkeletonSpaceMatrix(const std::string &jointName) const
{
    auto it = skeleton_.jointMap.find(jointName);
    if (it != skeleton_.jointMap.end())
    {
        return skeleton_.joints[it->second].skeletonSpaceMatrix;
    }
    return std::nullopt;
}

std::optional<Matrix4x4> Bone::GetJointWorldMatrix(const std::string &jointName, const Matrix4x4 &worldMatrix) const
{
    auto it = skeleton_.jointMap.find(jointName);
    if (it != skeleton_.jointMap.end())
    {
        const Joint &joint = skeleton_.joints[it->second];

        // スケルトン空間行列にワールド行列を適用し、ワールド空間での行列を算出
        Matrix4x4 worldJointMatrix = joint.skeletonSpaceMatrix * worldMatrix;

        return worldJointMatrix;
    }
    return std::nullopt;
}

int32_t Bone::CreateJoint(const Node &node, const std::optional<int32_t> &parent, std::vector<Joint> &joints)
{
    // ノードからジョイント情報を生成し、配列に追加
    Joint joint;
    joint.name = node.name;
    joint.localMatrix = node.localMatrix;
    joint.skeletonSpaceMatrix = MakeIdentity4x4();
    joint.transform = node.transform;
    joint.index = static_cast<int32_t>(joints.size());
    joint.parent = parent;
    joints.push_back(joint);

    // 子ノードに対して再帰的にジョイント作成処理を行う
    for (const Node &child : node.children)
    {
        int32_t childIndex = CreateJoint(child, joint.index, joints);
        joints[joint.index].children.push_back(childIndex);
    }

    return joint.index;
}

Skeleton Bone::CreateSkeleton(const Node &rootNode)
{
    Skeleton skeleton;
    // ルートノードから階層構造を構築
    skeleton.root = CreateJoint(rootNode, {}, skeleton.joints);

    // 名前からインデックスを素早く検索するためのマップを作成
    for (const Joint &joint : skeleton.joints)
    {
        skeleton.jointMap.emplace(joint.name, joint.index);
    }

    return skeleton;
}

void Bone::ApplyLayer(const Animation &animation, float animationTime,
                      const std::vector<uint8_t> &mask, float weight)
{
    if (weight <= 0.0f)
    {
        return;
    }
    const float t = (weight >= 1.0f) ? 1.0f : weight;

    for (Joint &joint : skeleton_.joints)
    {
        // マスク外（下半身側）のジョイントは基準アニメーションのまま残す
        if (static_cast<size_t>(joint.index) >= mask.size() || !mask[joint.index])
        {
            continue;
        }

        auto it = animation.nodeAnimations.find(joint.name);
        if (it == animation.nodeAnimations.end())
        {
            continue;
        }

        const NodeAnimation &nodeAnimation = (*it).second;
        const Vector3 translate = Animator::CalculateValue(nodeAnimation.translate, animationTime);
        const Quaternion rotate = Animator::CalculateValue(nodeAnimation.rotate, animationTime);
        const Vector3 scale = Animator::CalculateValue(nodeAnimation.scale, animationTime);

        if (t >= 1.0f)
        {
            joint.transform.translate = translate;
            joint.transform.rotate = rotate;
            joint.transform.scale = scale;
        }
        else
        {
            // レイヤーの出入りで上半身がパキッと切り替わらないよう、重みで基準側と混ぜる
            joint.transform.translate = Lerp(joint.transform.translate, translate, t);
            joint.transform.rotate = Quaternion::Slerp(joint.transform.rotate, rotate, t);
            joint.transform.scale = Lerp(joint.transform.scale, scale, t);
        }
    }
}

void Bone::ApplyAnimation(const Animation &animation, float animationTime)
{
    // 各ジョイントのアニメーション設定を検索し、補間値を計算して適用
    for (Joint &joint : skeleton_.joints)
    {
        if (auto it = animation.nodeAnimations.find(joint.name); it != animation.nodeAnimations.end())
        {
            const NodeAnimation &rootNodeAnimation = (*it).second;
            joint.transform.translate = Animator::CalculateValue(rootNodeAnimation.translate, animationTime);
            joint.transform.rotate = Animator::CalculateValue(rootNodeAnimation.rotate, animationTime);
            joint.transform.scale = Animator::CalculateValue(rootNodeAnimation.scale, animationTime);
        }
    }
}
} // namespace Hagine
