#pragma once
#include "model/ModelStructs.h"
#include <string>
#include <type/Quaternion.h>
#include <type/Vector2.h>
#include <type/Vector3.h>
#include <vector>

namespace Hagine {

/// <summary>
/// ブレンドスペースの重みの決め方
/// </summary>
enum class BlendSpaceMode
{
    Directional2D, ///< 中心（待機）＋周り（前後左右など）。向きで隣り合う2つを混ぜ、長さで中心と混ぜる
    Cartesian2D,   ///< 点を自由に置く（グラデーションバンド補間）
    Linear1D,      ///< X だけを使う一直線（歩き→走りなど）
};

/// <summary>
/// ブレンドスペースに置く1つの点（1つのクリップ）
/// </summary>
struct BlendSpacePoint
{
    std::string clipName;           ///< AnimationController に登録したクリップ名
    Vector2 position = {0.0f, 0.0f}; ///< パラメータ空間での位置
    float speed = 1.0f;             ///< このクリップの再生速度（周期を他と揃える前の倍率）
};

/// <summary>
/// ブレンドスペース（ブレンドツリーの1段）
/// パラメータ（例: 移動方向×速さ）から各クリップの重みを決め、
/// 全クリップの再生位置を「周期に対する割合」で揃えたまま混ぜる。
/// 揃えないと、歩幅の違うモーションを混ぜたときに足がもつれる
/// </summary>
class AnimationBlendSpace
{
  public:
    /// <summary>
    /// 点に対応するアニメーションの実体（ファイルから読んだもの）
    /// </summary>
    struct Source
    {
        std::string filePath; ///< アニメーションファイルパス
        Animation animation;  ///< 読み込んだアニメーション
    };

    /// ===================================================
    /// public method
    /// ===================================================

    /// <summary>
    /// 点の並びを設定し、各点のアニメーションを読み込む
    /// </summary>
    /// <param name="points">点の並び</param>
    /// <param name="filePaths">点ごとのアニメーションファイルパス（points と同じ並び）</param>
    void SetPoints(const std::vector<BlendSpacePoint> &points, const std::vector<std::string> &filePaths);

    /// <summary>
    /// 目標パラメータへ近づけ、再生位置を進める
    /// </summary>
    /// <param name="deltaTime">経過時間（秒）</param>
    void Advance(float deltaTime);

    /// <summary>
    /// パラメータから各点の重みを計算する（合計は1）
    /// </summary>
    /// <param name="parameter">パラメータ</param>
    /// <param name="outWeights">点ごとの重み</param>
    void ComputeWeights(const Vector2 &parameter, std::vector<float> &outWeights) const;

    /// <summary>
    /// 点ごとのサンプル時刻（秒）を取得する
    /// </summary>
    /// <param name="outTimes">点ごとの時刻</param>
    void GetSampleTimes(std::vector<float> &outTimes) const;

    /// <summary>
    /// 再生位置を先頭へ戻す
    /// </summary>
    void ResetPhase() { phase_ = 0.0f; }

    /// <summary>
    /// パラメータを目標値へ即座に合わせる（なめらかにしない）
    /// </summary>
    void SnapParameter() { parameter_ = targetParameter_; }

    /// ===================================================
    /// Getter
    /// ===================================================
    const std::string &GetName() const { return name_; }
    BlendSpaceMode GetMode() const { return mode_; }
    const std::vector<BlendSpacePoint> &GetPoints() const { return points_; }
    std::vector<BlendSpacePoint> &GetMutablePoints() { return points_; }
    const std::vector<Source> &GetSources() const { return sources_; }
    const std::vector<float> &GetWeights() const { return weights_; }
    const Vector2 &GetParameter() const { return parameter_; }
    const Vector2 &GetTargetParameter() const { return targetParameter_; }
    float GetPhase() const { return phase_; }
    float GetSmoothTime() const { return smoothTime_; }
    float GetFadeDuration() const { return fadeDuration_; }
    float GetPlaybackSpeed() const { return playbackSpeed_; }

    /// ===================================================
    /// Setter
    /// ===================================================
    void SetName(const std::string &name) { name_ = name; }
    void SetMode(BlendSpaceMode mode) { mode_ = mode; }
    void SetTargetParameter(const Vector2 &parameter) { targetParameter_ = parameter; }
    void SetSmoothTime(float seconds) { smoothTime_ = seconds; }
    void SetFadeDuration(float seconds) { fadeDuration_ = seconds; }
    void SetPlaybackSpeed(float speed) { playbackSpeed_ = speed; }

  private:
    /// ===================================================
    /// private method
    /// ===================================================

    void ComputeDirectionalWeights(const Vector2 &parameter, std::vector<float> &outWeights) const;
    void ComputeCartesianWeights(const Vector2 &parameter, std::vector<float> &outWeights) const;
    void ComputeLinearWeights(float x, std::vector<float> &outWeights) const;

    /// <summary>
    /// 点ごとの1周の長さ（秒、速度を反映済み）を取得する
    /// </summary>
    /// <param name="index">点の番号</param>
    /// <returns>float: 1周の長さ</returns>
    float GetCycleLength(size_t index) const;

  private:
    /// ===================================================
    /// private variables
    /// ===================================================

    std::string name_;                                      ///< 識別名
    BlendSpaceMode mode_ = BlendSpaceMode::Directional2D;   ///< 重みの決め方
    std::vector<BlendSpacePoint> points_;                   ///< 点の並び
    std::vector<Source> sources_;                           ///< 点ごとのアニメーション
    std::vector<float> weights_;                            ///< 直近の重み
    Vector2 parameter_ = {0.0f, 0.0f};                      ///< 今のパラメータ（なめらかにした後）
    Vector2 targetParameter_ = {0.0f, 0.0f};                ///< 目標のパラメータ
    float phase_ = 0.0f;                                    ///< 周期に対する再生位置（0〜1）
    float smoothTime_ = 0.12f;                              ///< パラメータを目標へ寄せる時定数（秒）
    float fadeDuration_ = 0.25f;                            ///< 他のクリップとの切り替えにかける時間（秒）
    float playbackSpeed_ = 1.0f;                            ///< 全体の再生速度
};

/// <summary>
/// 移動のブレンドスペース用に、ワールドの速度をキャラの向き基準のパラメータへ直す。
/// X = 右へ進む速さ・Y = 前へ進む速さを、基準の速さ（走りの最高速など）で割ったもの（長さは1まで）
/// </summary>
/// <param name="worldVelocity">ワールドの速度（上下成分は無視）</param>
/// <param name="rotation">キャラの回転（+Z を顔の向きとして使う）</param>
/// <param name="referenceSpeed">パラメータの長さが1になる速さ</param>
/// <returns>Vector2: パラメータ</returns>
Vector2 MakeLocomotionParameter(const Vector3 &worldVelocity, const Quaternion &rotation, float referenceSpeed);

/// <summary>
/// 再生中のブレンドスペースを姿勢へ適用するための入力
/// </summary>
struct BlendSpacePose
{
    const AnimationBlendSpace *space = nullptr; ///< 適用するブレンドスペース
    float weight = 0.0f;                        ///< 全体の効き（切り替えのフェード。0〜1）
};

} // namespace Hagine
