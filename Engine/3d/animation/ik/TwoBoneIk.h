#pragma once
#include "model/ModelStructs.h"
#include "type/Quaternion.h"
#include "type/Vector3.h"
#include <cstdint>
#include <string>

/// <summary>
/// 2ボーンIKと、IK で骨を書き換えるときの共通の道具。
/// 足IK（腿・すね・足首）と手のIK（上腕・前腕・手首）の両方が使う。
/// どれも Joint::skeletonSpaceMatrix を書き換え、子孫と localMatrix をそれに合わせる
/// </summary>
namespace Hagine::TwoBoneIk {

/// <summary>
/// 根元・中間・先端の3点を、先端が目標へ届くように曲げ直す（2ボーンIK）。
/// 中間の関節（膝・肘）は今の曲がる面のまま曲げ伸ばしする
/// </summary>
/// <param name="skeleton">対象のスケルトン</param>
/// <param name="upperIndex">根元のジョイント添字（腿・上腕）</param>
/// <param name="lowerIndex">中間のジョイント添字（すね・前腕）</param>
/// <param name="footIndex">先端のジョイント添字（足首・手首）</param>
/// <param name="targetSkeletonSpace">先端の目標位置（スケルトン空間）</param>
void Solve(Skeleton &skeleton, int32_t upperIndex, int32_t lowerIndex, int32_t footIndex, const Vector3 &targetSkeletonSpace);

/// <summary>
/// ジョイントを自分の位置を中心に回し、子孫のスケルトン空間行列を作り直す
/// </summary>
/// <param name="skeleton">対象のスケルトン</param>
/// <param name="jointIndex">回すジョイントの添字</param>
/// <param name="rotation">かける回転（スケルトン空間）</param>
void RotateJoint(Skeleton &skeleton, int32_t jointIndex, const Quaternion &rotation);

/// <summary>
/// ジョイントを平行移動し、子孫のスケルトン空間行列を作り直す
/// </summary>
/// <param name="skeleton">対象のスケルトン</param>
/// <param name="jointIndex">動かすジョイントの添字</param>
/// <param name="offset">移動量（スケルトン空間）</param>
void TranslateJoint(Skeleton &skeleton, int32_t jointIndex, const Vector3 &offset);

/// <summary>
/// 書き換えたスケルトン空間行列に合わせて、そのジョイントのローカル行列も直す。
/// 後から祖先を動かしたときに、入れた曲げが消えないようにするために要る
/// </summary>
/// <param name="skeleton">対象のスケルトン</param>
/// <param name="jointIndex">対象のジョイント添字</param>
void WriteBackLocalMatrix(Skeleton &skeleton, int32_t jointIndex);

/// <summary>
/// 子孫のスケルトン空間行列を親から組み直す（自分自身は対象外）
/// </summary>
/// <param name="skeleton">対象のスケルトン</param>
/// <param name="jointIndex">根になるジョイントの添字</param>
void RefreshDescendants(Skeleton &skeleton, int32_t jointIndex);

/// <summary>
/// ジョイント名から添字を引く（見つからなければ -1）
/// </summary>
/// <param name="skeleton">対象のスケルトン</param>
/// <param name="name">ジョイント名</param>
/// <returns>int32_t: 添字。無ければ -1</returns>
int32_t FindJoint(const Skeleton &skeleton, const std::string &name);

} // namespace Hagine::TwoBoneIk
