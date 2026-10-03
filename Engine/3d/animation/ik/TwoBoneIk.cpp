#define NOMINMAX
#include "TwoBoneIk.h"
#include "MyMath.h"
#include <algorithm>
#include <cmath>

namespace Hagine::TwoBoneIk {

namespace {
/// <summary>行列の平行移動成分（＝そのジョイントの位置）を取り出す</summary>
Vector3 GetTranslation(const Matrix4x4 &matrix)
{
    return {matrix.m[3][0], matrix.m[3][1], matrix.m[3][2]};
}

/// <summary>行列の平行移動成分を書き換える</summary>
void SetTranslation(Matrix4x4 &matrix, const Vector3 &translation)
{
    matrix.m[3][0] = translation.x;
    matrix.m[3][1] = translation.y;
    matrix.m[3][2] = translation.z;
}
} // namespace

int32_t FindJoint(const Skeleton &skeleton, const std::string &name)
{
    if (name.empty())
    {
        return -1;
    }
    auto it = skeleton.jointMap.find(name);
    return (it == skeleton.jointMap.end()) ? -1 : it->second;
}

void Solve(Skeleton &skeleton, int32_t upperIndex, int32_t lowerIndex, int32_t footIndex, const Vector3 &targetSkeletonSpace)
{
    const Vector3 upperPosition = GetTranslation(skeleton.joints[upperIndex].skeletonSpaceMatrix);
    const Vector3 lowerPosition = GetTranslation(skeleton.joints[lowerIndex].skeletonSpaceMatrix);
    const Vector3 footPosition = GetTranslation(skeleton.joints[footIndex].skeletonSpaceMatrix);

    const float upperLength = (lowerPosition - upperPosition).Length();
    const float lowerLength = (footPosition - lowerPosition).Length();
    if (upperLength <= 1e-5f || lowerLength <= 1e-5f)
    {
        return;
    }

    // 目標までの距離。脚を伸ばし切る／畳み切る手前で止める
    // （きっかり伸ばし切ると膝の回転軸が定まらず、次のフレームで暴れる）
    const float reachMax = (upperLength + lowerLength) * 0.999f;
    const float reachMin = std::abs(upperLength - lowerLength) * 1.001f + 1e-4f;
    Vector3 toTarget = targetSkeletonSpace - upperPosition;
    float reach = toTarget.Length();
    if (reach <= 1e-5f)
    {
        return;
    }
    reach = (std::max)((std::min)(reach, reachMax), reachMin);

    // ---- 膝の曲げ角を作り直す ----
    // 余弦定理: cos(膝) = (腿^2 + すね^2 - 目標距離^2) / (2 * 腿 * すね)
    const float cosDesired =
        (upperLength * upperLength + lowerLength * lowerLength - reach * reach) / (2.0f * upperLength * lowerLength);
    const float desiredKneeAngle = std::acos((std::max)(-1.0f, (std::min)(1.0f, cosDesired)));

    const Vector3 kneeToUpper = (upperPosition - lowerPosition).Normalize();
    const Vector3 kneeToFoot = (footPosition - lowerPosition).Normalize();
    const float currentKneeAngle =
        std::acos((std::max)(-1.0f, (std::min)(1.0f, kneeToUpper.Dot(kneeToFoot))));

    Vector3 bendAxis = kneeToUpper.Cross(kneeToFoot);
    if (bendAxis.LengthSq() < 1e-8f)
    {
        // 脚が一直線で曲げる面が決まらない。腿のローカルX軸を蝶番に見立てる
        // （人型リグでは膝はおおむねこの軸まわりに曲がる）
        const Matrix4x4 &upperMatrix = skeleton.joints[upperIndex].skeletonSpaceMatrix;
        bendAxis = Vector3{upperMatrix.m[0][0], upperMatrix.m[0][1], upperMatrix.m[0][2]};
        if (bendAxis.LengthSq() < 1e-8f)
        {
            return;
        }
    }
    bendAxis = bendAxis.Normalize();

    // この軸まわりに正の角度で回すと、腿とすねのなす角は開く（＝膝が伸びる）
    const float kneeDelta = desiredKneeAngle - currentKneeAngle;
    if (std::abs(kneeDelta) > 1e-5f)
    {
        RotateJoint(skeleton, lowerIndex, Quaternion::FromAxisAngle(bendAxis, kneeDelta));
    }

    // ---- 腿を回して足首を目標の方向へ向ける ----
    // 膝を作り直したので、腿から足首までの距離は目標までの距離と一致している。
    // あとは向きを合わせれば足首が目標へ乗る
    const Vector3 movedFoot = GetTranslation(skeleton.joints[footIndex].skeletonSpaceMatrix);
    const Vector3 fromDirection = movedFoot - upperPosition;
    if (fromDirection.LengthSq() < 1e-8f || toTarget.LengthSq() < 1e-8f)
    {
        return;
    }

    Quaternion swing;
    swing.SetFromTo(fromDirection, toTarget);
    RotateJoint(skeleton, upperIndex, swing);
}

void RotateJoint(Skeleton &skeleton, int32_t jointIndex, const Quaternion &rotation)
{
    Matrix4x4 &matrix = skeleton.joints[jointIndex].skeletonSpaceMatrix;
    const Vector3 pivot = GetTranslation(matrix);

    // 行ベクトル規約なので、行0〜2 がこのジョイントのローカル軸（スケール込み）そのもの。
    // それを回して書き戻せば「自分の位置を中心に回す」ことになる
    for (int row = 0; row < 3; ++row)
    {
        const Vector3 axis = {matrix.m[row][0], matrix.m[row][1], matrix.m[row][2]};
        const Vector3 rotated = RotateVector(axis, rotation);
        matrix.m[row][0] = rotated.x;
        matrix.m[row][1] = rotated.y;
        matrix.m[row][2] = rotated.z;
    }
    SetTranslation(matrix, pivot);

    WriteBackLocalMatrix(skeleton, jointIndex);
    RefreshDescendants(skeleton, jointIndex);
}

void TranslateJoint(Skeleton &skeleton, int32_t jointIndex, const Vector3 &offset)
{
    Matrix4x4 &matrix = skeleton.joints[jointIndex].skeletonSpaceMatrix;
    SetTranslation(matrix, GetTranslation(matrix) + offset);

    WriteBackLocalMatrix(skeleton, jointIndex);
    RefreshDescendants(skeleton, jointIndex);
}

void WriteBackLocalMatrix(Skeleton &skeleton, int32_t jointIndex)
{
    // 書き換えたのはスケルトン空間行列だけなので、このままだと localMatrix が食い違う。
    // 後から祖先を動かすと RefreshDescendants が古い localMatrix から組み直してしまい、
    // 直前に入れた曲げが消える（膝を曲げてから腿を振ると膝が伸びる、という壊れ方をする）。
    // ここで localMatrix を今の姿勢に合わせておけば、祖先の変更にもそのまま付いてくる
    Joint &joint = skeleton.joints[jointIndex];
    if (joint.parent)
    {
        joint.localMatrix = joint.skeletonSpaceMatrix * Inverse(skeleton.joints[*joint.parent].skeletonSpaceMatrix);
    }
    else
    {
        joint.localMatrix = joint.skeletonSpaceMatrix;
    }
}

void RefreshDescendants(Skeleton &skeleton, int32_t jointIndex)
{
    // 子のローカル行列は動かしていないので、親が変わったぶんだけ掛け直せばよい。
    // Bone::CalculateMatrices と同じ式で、対象を部分木に絞ったもの
    std::vector<int32_t> stack = skeleton.joints[jointIndex].children;
    while (!stack.empty())
    {
        const int32_t index = stack.back();
        stack.pop_back();

        Joint &joint = skeleton.joints[index];
        joint.skeletonSpaceMatrix = joint.localMatrix * skeleton.joints[*joint.parent].skeletonSpaceMatrix;

        for (int32_t child : joint.children)
        {
            stack.push_back(child);
        }
    }
}

} // namespace Hagine::TwoBoneIk
