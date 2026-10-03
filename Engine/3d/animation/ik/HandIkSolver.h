#pragma once
#include "model/ModelStructs.h"
#include "nlohmann/json.hpp"
#include "type/Matrix4x4.h"
#include "type/Vector3.h"
#include <string>
#include <vector>

namespace Hagine {

/// <summary>
/// 腕1本ぶんのジョイント名（上腕 → 前腕 → 手首）
/// </summary>
struct HandIkLimb
{
    std::string label;      //!< 呼び名（"左手" など。ゲーム側はこの名前で目標を渡せる）
    std::string upperJoint; //!< 上腕（肩側）
    std::string lowerJoint; //!< 前腕（肘）
    std::string handJoint;  //!< 手首
    bool enabled = true;    //!< この腕を解くか
};

/// <summary>
/// 手のIKの設定。オブジェクトごとに持たせてJSONへ保存する
/// </summary>
struct HandIkSettings
{
    bool enabled = false;          //!< 手のIKを効かせるか
    float weight = 1.0f;           //!< 効き具合（0で素のアニメーションのまま）
    std::vector<HandIkLimb> limbs; //!< 解く腕
    float blendSpeed = 8.0f;       //!< 目標ができた / 外れたときに効かせ始める・抜く速さ[1/秒]
    bool drawDebug = false;        //!< 目標と腕を線で描く
};

/// <summary>
/// 手のIK（手首を目標の位置へ伸ばす）。
/// アニメーションのポーズのまま、上腕と前腕を2ボーンIKで曲げ直して手首を目標へ運ぶ。
/// 肘は今のポーズの曲がる向きのまま曲げ伸ばしするので、腕がねじれない。
/// 武器を両手で握る・壁に手をつく・物を拾う、などに使う。
/// 足IKと同じく Joint::skeletonSpaceMatrix だけを書き換え、SRT には触れない。
/// 呼ぶ場所は「アニメーション適用後・スキニングのパレット更新前」（注視IKの後・揺れ物の前）。
/// </summary>
class HandIkSolver
{
  public:
    /// <summary>
    /// ポーズへ手のIKを適用する
    /// </summary>
    /// <param name="skeleton">アニメーション適用済みのスケルトン（書き換える）</param>
    /// <param name="worldMatrix">モデルのワールド行列（絵と同じ位置のもの）</param>
    /// <param name="deltaTime">経過時間[秒]</param>
    /// <returns>bool: ポーズを書き換えたら true（呼ぶ側はスキンのパレットを作り直す）</returns>
    bool Solve(Skeleton &skeleton, const Matrix4x4 &worldMatrix, float deltaTime);

    /// <summary>手首の目標（ワールド）を設定する。毎フレーム呼んでよい</summary>
    /// <param name="limbIndex">腕の番号（設定の並び順）</param>
    /// <param name="targetWorld">手首を運ぶ先</param>
    void SetTarget(size_t limbIndex, const Vector3 &targetWorld);

    /// <summary>呼び名で腕を選んで目標を設定する</summary>
    /// <returns>bool: その名前の腕があれば true</returns>
    bool SetTarget(const std::string &label, const Vector3 &targetWorld);

    /// <summary>目標を外す（なめらかにアニメーションどおりへ戻る）</summary>
    void ClearTarget(size_t limbIndex);

    /// <summary>すべての腕の目標を外す</summary>
    void ClearAllTargets();

    /// <summary>
    /// スケルトンのジョイント名から、左右の腕（上腕・前腕・手首）を拾う（Mixamo などの命名）
    /// </summary>
    /// <returns>size_t: 拾った腕の数</returns>
    size_t AutoDetect(const Skeleton &skeleton);

    /// <summary>設定を取得（UIから直接いじる用）</summary>
    HandIkSettings &GetSettings() { return settings_; }
    const HandIkSettings &GetSettings() const { return settings_; }

    /// <summary>腕ごとの今の様子（UIの確認・エディタの試しの目標用）</summary>
    struct LimbState
    {
        bool hasTarget = false;                     //!< ゲームから目標をもらっているか
        Vector3 target = {0.0f, 0.0f, 0.0f};        //!< ゲームからの目標（ワールド）
        bool useTestTarget = false;                 //!< エディタの試しの目標を使うか（ゲームの目標より優先）
        Vector3 testTarget = {0.0f, 0.0f, 0.0f};    //!< 試しの目標（ワールド）
        float blend = 0.0f;                         //!< 出入りのフェード（0〜1）
        Vector3 animatedHandWorld = {0.0f, 0.0f, 0.0f}; //!< IK を掛ける前の手首の位置（ワールド）
        float reachRate = 0.0f;                     //!< 目標までの距離 / 腕の長さ（1を超えると届いていない）
    };

    /// <summary>腕ごとの今の様子を取得（設定の腕の数にそろえてある）</summary>
    std::vector<LimbState> &GetLimbStates() { return states_; }

    /// <summary>設定をJSONへ書き出す</summary>
    nlohmann::json ToJson() const;

    /// <summary>設定をJSONから読み込む</summary>
    void FromJson(const nlohmann::json &json);

  private:
    HandIkSettings settings_;
    std::vector<LimbState> states_;
};

} // namespace Hagine
