#pragma once
#include "MusicTypes.h"
#include <nlohmann/json.hpp>
#include <vector>

namespace Hagine {

/// <summary>
/// やまびこ（ディレイ）。テンポに合わせて遅れて返ってくる音を足す
/// </summary>
struct DelayParams
{
    bool enabled = false;
    int noteDivision = 3;   // 遅れの長さ。0=4分 1=付点8分 2=8分 3=16分
    float feedback = 0.35f; // 返ってきた音をもう一度送り返す量 (0〜0.95)
    float mix = 0.25f;      // 元の音に足す量 (0〜1)
    float pingPong = 0.5f;  // 左右で交互に返す度合い (0〜1)
};

/// <summary>
/// 残響（リバーブ）。部屋の広さと響きの吸われ方で決まる
/// </summary>
struct ReverbParams
{
    bool enabled = false;
    float roomSize = 0.62f; // 空間の広さ (0〜1)
    float damping = 0.45f;  // 高い音から先に消える度合い (0〜1)
    float width = 1.0f;     // 左右の広がり (0〜1)
    float mix = 0.22f;      // 元の音に足す量 (0〜1)
};

/// <summary>
/// テンポ同期のディレイ。ミキサースレッドからだけ触ること
/// </summary>
class MusicDelay
{
  public:
    /// <summary>遅延用のバッファを確保する（最大2秒ぶん）</summary>
    void Initialize();

    /// <summary>内部に残っている音を消す</summary>
    void Reset();

    /// <summary>
    /// ステレオのミックスへその場で効果を掛ける
    /// </summary>
    /// <param name="buffer">ステレオインターリーブの音声</param>
    /// <param name="frames">フレーム数</param>
    /// <param name="params">設定</param>
    /// <param name="bpm">テンポ（遅れの長さを決めるのに使う）</param>
    void Process(float *buffer, uint32_t frames, const DelayParams &params, float bpm);

  private:
    std::vector<float> lineLeft_;
    std::vector<float> lineRight_;
    size_t writeIndex_ = 0;
};

/// <summary>
/// くし形フィルタと全域通過フィルタを重ねた残響。
/// いわゆる Freeverb 型で、少ない計算量のわりに自然に響く
/// </summary>
class MusicReverb
{
  public:
    /// <summary>各フィルタの遅延バッファを確保する</summary>
    void Initialize();

    /// <summary>内部に残っている音を消す</summary>
    void Reset();

    /// <summary>
    /// ステレオのミックスへその場で効果を掛ける
    /// </summary>
    /// <param name="buffer">ステレオインターリーブの音声</param>
    /// <param name="frames">フレーム数</param>
    /// <param name="params">設定</param>
    void Process(float *buffer, uint32_t frames, const ReverbParams &params);

  private:
    /// くし形フィルタ1本
    struct Comb
    {
        std::vector<float> buffer;
        size_t index = 0;
        float filterStore = 0.0f;
    };

    /// 全域通過フィルタ1本
    struct AllPass
    {
        std::vector<float> buffer;
        size_t index = 0;
    };

    static constexpr int kCombCount = 8;
    static constexpr int kAllPassCount = 4;

    Comb combsLeft_[kCombCount];
    Comb combsRight_[kCombCount];
    AllPass allPassLeft_[kAllPassCount];
    AllPass allPassRight_[kAllPassCount];
};

/// <summary>エフェクト設定を JSON へ書き出す</summary>
void SaveEffectsTo(nlohmann::json &outJson, const DelayParams &delay, const ReverbParams &reverb);

/// <summary>エフェクト設定を JSON から読み込む</summary>
void LoadEffectsFrom(const nlohmann::json &json, DelayParams &delay, ReverbParams &reverb);

} // namespace Hagine
