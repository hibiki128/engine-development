#pragma once
#include "model/ModelStructs.h"
#include "nlohmann/json.hpp"
#include "type/Matrix4x4.h"
#include "type/Vector3.h"
#include <string>
#include <vector>

namespace Hagine {

/// <summary>
/// 注視で回すジョイント1つ。回す角度を share の割合で分け合う
/// （背骨で少し・首で少し・頭で残り、と分けると自然に見える）
/// </summary>
struct LookAtJoint
{
    std::string name;   //!< ジョイント名
    float share = 1.0f; //!< 回す角度の取り分（全ジョイントの合計に対する割合）
};

/// <summary>
/// 注視IKの設定。オブジェクトごとに持たせてJSONへ保存する
/// </summary>
struct LookAtSettings
{
    bool enabled = false;               //!< 注視を効かせるか
    float weight = 1.0f;                //!< 効き具合（0で素のアニメーションのまま）
    std::vector<LookAtJoint> joints;    //!< 回すジョイント（体の根元側から順に）
    float maxYawDegrees = 70.0f;        //!< 左右へ向ける上限[度]
    float maxPitchDegrees = 35.0f;      //!< 上下へ向ける上限[度]
    float giveUpYawDegrees = 130.0f;    //!< 相手がこれより後ろに回ったら注視をやめる[度]
    float followSpeed = 8.0f;           //!< 向きの追従の速さ[1/秒]
    float blendSpeed = 4.0f;            //!< 注視の出入りの速さ[1/秒]
};

/// <summary>
/// 注視IK（ルックアット）。
/// アニメーションを適用した後のポーズに、「体の正面から目標への向き」の分だけ
/// 背骨〜首〜頭を回す角度を足す。扱うのは Joint::skeletonSpaceMatrix だけで、
/// 足IK（FootIkSolver）と同じく SRT には触れない。
/// 呼ぶ場所は「アニメーション適用後・スキニングのパレット更新前」。
/// </summary>
class LookAtSolver
{
  public:
    /// <summary>
    /// ポーズへ注視を適用する
    /// </summary>
    /// <param name="skeleton">アニメーション適用済みのスケルトン（書き換える）</param>
    /// <param name="worldMatrix">モデルのワールド行列（絵と同じ位置のもの）</param>
    /// <param name="faceWorld">体の正面の向き（ワールド）</param>
    /// <param name="deltaTime">経過時間[秒]</param>
    /// <returns>bool: ポーズを書き換えたら true（呼ぶ側はスキンのパレットを作り直す）</returns>
    bool Solve(Skeleton &skeleton, const Matrix4x4 &worldMatrix, const Vector3 &faceWorld, float deltaTime);

    /// <summary>
    /// 見る先（ワールド座標）を設定する。毎フレーム呼んでよい
    /// </summary>
    /// <param name="targetWorld">見る先</param>
    void SetTarget(const Vector3 &targetWorld);

    /// <summary>
    /// 見る先を外す（なめらかに正面へ戻る）
    /// </summary>
    void ClearTarget() { hasTarget_ = false; }

    /// <summary>
    /// スケルトンのジョイント名から、背骨・首・頭を自動で拾う（Mixamo などの命名）
    /// </summary>
    /// <param name="skeleton">対象のスケルトン</param>
    /// <returns>bool: 頭が見つかれば true</returns>
    bool AutoDetect(const Skeleton &skeleton);

    /// <summary>設定を取得（UIから直接いじる用）</summary>
    LookAtSettings &GetSettings() { return settings_; }
    const LookAtSettings &GetSettings() const { return settings_; }

    /// <summary>見る先があるか</summary>
    bool HasTarget() const { return hasTarget_; }

    /// <summary>直近の向け具合（UIの確認用。度）</summary>
    float GetYawDegrees() const;
    float GetPitchDegrees() const;

    /// <summary>直近の効き（出入りのフェード込み。UIの確認用）</summary>
    float GetBlend() const { return blend_; }

    /// <summary>設定をJSONへ書き出す</summary>
    nlohmann::json ToJson() const;

    /// <summary>設定をJSONから読み込む</summary>
    void FromJson(const nlohmann::json &json);

  private:
    LookAtSettings settings_;
    bool hasTarget_ = false;              //!< 見る先があるか
    Vector3 target_ = {0.0f, 0.0f, 0.0f}; //!< 見る先（ワールド）
    float yaw_ = 0.0f;                    //!< 均した後の左右の角度[ラジアン]
    float pitch_ = 0.0f;                  //!< 均した後の上下の角度[ラジアン]
    float blend_ = 0.0f;                  //!< 出入りのフェード（0〜1）
};

} // namespace Hagine
