#pragma once
#include "model/ModelStructs.h"
#include "nlohmann/json.hpp"
#include "type/Matrix4x4.h"
#include "type/Vector3.h"
#include <string>
#include <vector>

namespace Hagine {

/// <summary>
/// 片脚ぶんのジョイント名。
/// 腿 → すね → 足首 の3つが揃っていないと解けない（つま先は任意）
/// </summary>
struct FootIkLeg
{
    std::string upperJoint; //!< 腿（股関節側）
    std::string lowerJoint; //!< すね（膝）
    std::string footJoint;  //!< 足首
    std::string toeJoint;   //!< つま先（あれば接地の基準を足首より前に取れる。無くてもよい）
};

/// <summary>
/// 足IKの設定。オブジェクトごとに持たせてJSONへ保存する
/// </summary>
struct FootIkSettings
{
    bool enabled = false;      //!< 足IKを効かせるか
    float weight = 1.0f;       //!< 効き具合（0で素のアニメーションのまま）
    std::string hipJoint;      //!< 腰のジョイント名。両足が届かないときここを沈める
    std::vector<FootIkLeg> legs; //!< 脚の一覧（通常は左右2本）

    float rayUpOffset = 0.8f;  //!< 足首から上へどれだけ遡ってレイを撃ち始めるか
    float rayLength = 2.0f;    //!< レイの長さ（開始点からの距離）
    float footHeight = 0.1f;   //!< 足首を地面からどれだけ浮かせるか（靴の厚み）
    float maxHipDrop = 0.6f;   //!< 腰を下げられる上限（これ以上は下げない）

    float positionLerpSpeed = 12.0f; //!< 接地位置に追従する速さ[1/秒]
    float rotationLerpSpeed = 12.0f; //!< 足の傾きに追従する速さ[1/秒]
    float maxFootAngleDegrees = 45.0f; //!< 足を傾けられる上限[度]

    bool alignFootToGround = true; //!< 足裏を地面の傾きに合わせるか
    bool drawDebug = false;        //!< レイと接地点を線で描くか

    std::vector<std::string> groundTags; //!< レイの対象タグ（空なら全タグ）

    // 自分の体のコライダーに当たると、足が胴体の高さで止まってしまう。
    // オブジェクト名で持ち主を見て外すので、呼ぶ側が自分の名前を入れておくこと
    std::string ignoreOwnerName;
};

/// <summary>
/// 足の接地IK（フットIK）。
///
/// アニメーションを適用した後のポーズに対して、次の順で手を入れる:
///   1. 各足の下へレイを撃ち、地面の高さと傾きを調べる
///   2. 両足が届かないぶんだけ腰を沈める
///   3. 脚ごとに2ボーンIK（腿・すね・足首）で足首を接地位置へ運ぶ
///   4. 足裏を地面の傾きへ向ける
///
/// 扱うのは Joint::skeletonSpaceMatrix だけで、SRT（Joint::transform）には触れない。
/// SRT は毎フレーム Bone::ApplyAnimation が丸ごと書き直すので、
/// こちらが書き戻しても次のフレームには消える。スキニングが読むのも
/// skeletonSpaceMatrix なので、そこだけ正しくしておけば絵は合う。
///
/// 呼ぶ場所は「アニメーション適用後・スキニングのパレット更新前」。
/// ワールド行列が要るので、ワールド行列が確定した後に呼ぶこと。
/// </summary>
class FootIkSolver
{
  public:
    /// <summary>
    /// ポーズへ足IKを適用する
    /// </summary>
    /// <param name="skeleton">アニメーション適用済みのスケルトン（書き換える）</param>
    /// <param name="worldMatrix">モデルのワールド行列（絵と同じ位置のもの）</param>
    /// <param name="deltaTime">経過時間[秒]</param>
    void Solve(Skeleton &skeleton, const Matrix4x4 &worldMatrix, float deltaTime);

    /// <summary>
    /// 足首を指定したワールド座標へ1回だけ運ぶ。
    /// 地面探しも均しも行わないので、段差の縁へ足を置く演出のように
    /// 「どこへ置くか」を呼ぶ側が決めている場面で使う
    /// </summary>
    /// <param name="skeleton">アニメーション適用済みのスケルトン（書き換える）</param>
    /// <param name="legIndex">設定に登録した脚の添字</param>
    /// <param name="worldMatrix">モデルのワールド行列</param>
    /// <param name="targetWorld">足首を置きたいワールド座標</param>
    /// <returns>bool: 解けたら true（脚が未登録・ジョイントが無い場合は false）</returns>
    bool SolveLegToWorldPosition(Skeleton &skeleton, size_t legIndex, const Matrix4x4 &worldMatrix,
                                 const Vector3 &targetWorld);

    /// <summary>
    /// スケルトンのジョイント名から、それらしい脚と腰を自動で拾う。
    /// Mixamo（mixamorig:LeftUpLeg など）や一般的な命名に対応する
    /// </summary>
    /// <param name="skeleton">対象のスケルトン</param>
    /// <returns>bool: 左右どちらかでも脚を組めたら true</returns>
    bool AutoDetect(const Skeleton &skeleton);

    /// <summary>
    /// 追従を打ち切り、次のフレームは現在の接地位置から始める。
    /// 瞬間移動やシーンの作り直しなど、足が一気に飛ぶ場面で呼ぶ
    /// </summary>
    void ResetSmoothing();

    /// <summary>設定を取得（UIから直接いじる用）</summary>
    FootIkSettings &GetSettings() { return settings_; }
    const FootIkSettings &GetSettings() const { return settings_; }

    /// <summary>設定を差し替える</summary>
    /// <param name="settings">新しい設定</param>
    void SetSettings(const FootIkSettings &settings);

    /// <summary>直近のフレームで腰を沈めた量[m]（UIの確認用）</summary>
    float GetHipOffset() const { return hipOffset_; }

    /// <summary>脚ごとの接地状態（UIの確認用）</summary>
    /// <param name="legIndex">脚の添字</param>
    /// <returns>bool: 地面を捉えていれば true</returns>
    bool IsLegGrounded(size_t legIndex) const;

    /// <summary>設定をJSONへ書き出す</summary>
    /// <returns>nlohmann::json: 設定JSON</returns>
    nlohmann::json ToJson() const;

    /// <summary>設定をJSONから読み込む</summary>
    /// <param name="json">読み込み元</param>
    void FromJson(const nlohmann::json &json);

  private:
    /// <summary>
    /// 脚1本ぶんの追従状態。目標がフレームごとに飛ばないよう均すために持つ
    /// </summary>
    struct LegState
    {
        bool grounded = false;   //!< 直近のレイが地面を捉えたか
        bool hasPrevious = false; //!< 均しの前回値があるか
        float targetY = 0.0f;    //!< 均した後の接地Y（ワールド）
        Vector3 normal = {0.0f, 1.0f, 0.0f}; //!< 均した後の地面法線（ワールド）
        float blend = 0.0f;      //!< 効き具合（空中では0へ落とす）
    };

    /// <summary>
    /// 脚1本を解く
    /// </summary>
    /// <param name="skeleton">対象のスケルトン</param>
    /// <param name="leg">脚のジョイント名</param>
    /// <param name="state">この脚の追従状態</param>
    /// <param name="worldMatrix">モデルのワールド行列</param>
    /// <param name="inverseWorld">ワールド行列の逆行列</param>
    void SolveLeg(Skeleton &skeleton, const FootIkLeg &leg, const LegState &state,
                  const Matrix4x4 &worldMatrix, const Matrix4x4 &inverseWorld);

    /// <summary>
    /// 腿・すね・足首の3点を、足首が目標へ届くように曲げ直す（2ボーンIK）
    /// </summary>
    /// <param name="skeleton">対象のスケルトン</param>
    /// <param name="upperIndex">腿のジョイント添字</param>
    /// <param name="lowerIndex">すねのジョイント添字</param>
    /// <param name="footIndex">足首のジョイント添字</param>
    /// <param name="targetSkeletonSpace">足首の目標位置（スケルトン空間）</param>
    void SolveTwoBone(Skeleton &skeleton, int32_t upperIndex, int32_t lowerIndex, int32_t footIndex,
                      const Vector3 &targetSkeletonSpace);

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
    static void WriteBackLocalMatrix(Skeleton &skeleton, int32_t jointIndex);

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
    static int32_t FindJoint(const Skeleton &skeleton, const std::string &name);

    FootIkSettings settings_{};    //!< 設定
    std::vector<LegState> states_; //!< 脚ごとの追従状態（脚数に合わせて伸縮する）
    float hipOffset_ = 0.0f;       //!< 均した後の腰の沈み量（0以下）
};
} // namespace Hagine
