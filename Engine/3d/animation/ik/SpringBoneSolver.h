#pragma once
#include "model/ModelStructs.h"
#include "nlohmann/json.hpp"
#include "type/Matrix4x4.h"
#include "type/Vector3.h"
#include <string>
#include <vector>

namespace Hagine {

/// <summary>
/// 揺らすジョイントのまとまり（髪の房・スカートの1枚・しっぽなど）。
/// 根元のジョイントを指定すると、その子孫がまとめて揺れる
/// </summary>
struct SpringBoneChain
{
    std::string rootJoint;                           //!< 根元のジョイント（根元自身も揺れる）
    bool enabled = true;                             //!< このまとまりを揺らすか
    float stiffness = 1.0f;                          //!< 元のポーズへ戻ろうとする強さ
    float drag = 0.4f;                               //!< 揺れの減衰（0=いつまでも揺れる / 1=すぐ止まる）
    float gravityPower = 0.0f;                       //!< 重力の強さ（骨の長さに対する割合/秒）
    Vector3 gravityDirection = {0.0f, -1.0f, 0.0f};  //!< 重力の向き（ワールド）
    float hitRadius = 0.02f;                         //!< 当たり判定の半径（モデル空間の長さ）
};

/// <summary>
/// 揺れ物が体へめり込まないための球（頭・胸などに付ける）
/// </summary>
struct SpringBoneCollider
{
    std::string joint;                     //!< 付けるジョイント
    Vector3 offset = {0.0f, 0.0f, 0.0f};   //!< ジョイントからのずれ（ジョイントの空間）
    float radius = 0.1f;                   //!< 半径（モデル空間の長さ）
};

/// <summary>
/// 揺れ物の設定。オブジェクトごとに持たせてJSONへ保存する
/// </summary>
struct SpringBoneSettings
{
    bool enabled = false;                       //!< 揺れ物を効かせるか
    float weight = 1.0f;                        //!< 効き具合（0で素のアニメーションのまま）
    std::vector<SpringBoneChain> chains;        //!< 揺らすまとまり
    std::vector<SpringBoneCollider> colliders;  //!< めり込み防止の球
    float tipLengthRate = 0.6f;                 //!< 末端の骨の長さ（親の骨の長さに対する割合）
    bool drawDebug = false;                     //!< 揺れの先端と当たり球を線で描く
};

/// <summary>
/// 揺れ物（スプリングボーン）。
/// 骨の先端の位置をワールド空間で慣性・減衰・重力つきで動かし、
/// アニメーションのポーズからの「ずれ」の分だけ骨を回す（VRM のスプリングボーンと同じ考え方）。
/// 体が動くと先端が遅れてついてくるので、髪・布・しっぽが揺れる。
/// 足IK・注視IKと同じく Joint::skeletonSpaceMatrix だけを書き換え、SRT には触れない。
/// 呼ぶ場所は「アニメーション適用後・スキニングのパレット更新前」（注視IKより後）。
/// </summary>
class SpringBoneSolver
{
  public:
    /// <summary>
    /// ポーズへ揺れを適用する
    /// </summary>
    /// <param name="skeleton">アニメーション適用済みのスケルトン（書き換える）</param>
    /// <param name="worldMatrix">モデルのワールド行列（絵と同じ位置のもの）</param>
    /// <param name="deltaTime">経過時間[秒]。0なら揺れを進めずに今の状態だけ掛ける</param>
    /// <returns>bool: ポーズを書き換えたら true（呼ぶ側はスキンのパレットを作り直す）</returns>
    bool Solve(Skeleton &skeleton, const Matrix4x4 &worldMatrix, float deltaTime);

    /// <summary>
    /// 揺れをリセットする（瞬間移動したあとなど。次のフレームはアニメーションどおりから始まる）
    /// </summary>
    void Reset() { states_.clear(); }

    /// <summary>
    /// スケルトンのジョイント名から、揺らせそうな物（髪・スカート・しっぽ等）と頭の当たり球を拾う
    /// </summary>
    /// <param name="skeleton">対象のスケルトン</param>
    /// <returns>size_t: 拾ったまとまりの数</returns>
    size_t AutoDetect(const Skeleton &skeleton);

    /// <summary>設定を取得（UIから直接いじる用）</summary>
    SpringBoneSettings &GetSettings() { return settings_; }
    const SpringBoneSettings &GetSettings() const { return settings_; }

    /// <summary>直近のフレームで揺らした骨の数（UIの確認用）</summary>
    size_t GetSimulatedJointCount() const { return simulatedJointCount_; }

    /// <summary>設定をJSONへ書き出す</summary>
    nlohmann::json ToJson() const;

    /// <summary>設定をJSONから読み込む</summary>
    void FromJson(const nlohmann::json &json);

  private:
    /// <summary>骨1本ぶんの揺れの状態（先端の位置をワールドで持つ）</summary>
    struct TailState
    {
        int32_t joint = -1;                       //!< 揺らすジョイント
        int32_t tailJoint = -1;                   //!< 先端にあたる子ジョイント（-1 なら末端で、仮の先端を使う）
        Vector3 currentTail = {0.0f, 0.0f, 0.0f}; //!< 今の先端（ワールド）
        Vector3 previousTail = {0.0f, 0.0f, 0.0f}; //!< 1つ前の先端（ワールド）
        bool initialized = false;                  //!< 先端の位置を一度でも置いたか
    };

    /// <summary>まとまり1つぶんの状態</summary>
    struct ChainState
    {
        std::string rootJoint;           //!< どのまとまりの状態か（設定が変わったら作り直す）
        std::vector<TailState> tails;    //!< 根元から先へ（親が必ず先）
    };

    /// <summary>まとまりの骨の並びを作る（根元から深さ優先で、親が先）</summary>
    static void BuildChainState(const Skeleton &skeleton, int32_t rootIndex, ChainState &outState);

    SpringBoneSettings settings_;
    std::vector<ChainState> states_; //!< settings_.chains と同じ並び
    size_t simulatedJointCount_ = 0;
};

} // namespace Hagine
