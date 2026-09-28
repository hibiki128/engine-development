#include "MusicEffects.h"

#include <algorithm>
#include <cmath>

namespace Hagine {
namespace {

/// ディレイ用に確保しておく最大の遅れ（秒）
constexpr float kMaxDelaySeconds = 2.0f;

/// Freeverb で使われている遅延長（44.1kHz 基準）。
/// 互いに素に近い長さを並べることで、響きが金属的にならない
constexpr int kCombTuning[8] = {1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617};
constexpr int kAllPassTuning[4] = {556, 441, 341, 225};
/// 左右で長さをずらす量。これがないと真ん中でしか鳴っていないように聞こえる
constexpr int kStereoSpread = 23;

/// 44.1kHz 前提の長さを、エンジンのサンプリングレートへ合わせる
int ScaleTuning(int tuning)
{
    const double ratio = static_cast<double>(MusicConst::kSampleRate) / 44100.0;
    return std::max(8, static_cast<int>(tuning * ratio));
}

/// 音符の分割から、遅れの長さ（秒）を求める
float DelaySeconds(int noteDivision, float bpm)
{
    const float beatSeconds = 60.0f / std::max(20.0f, bpm);
    switch (noteDivision)
    {
    case 0:
        return beatSeconds;             // 4分
    case 1:
        return beatSeconds * 0.75f;     // 付点8分
    case 2:
        return beatSeconds * 0.5f;      // 8分
    case 3:
    default:
        return beatSeconds * 0.25f;     // 16分
    }
}

} // namespace

//==============================================================================
// ディレイ
//==============================================================================

void MusicDelay::Initialize()
{
    const size_t maxSamples = static_cast<size_t>(kMaxDelaySeconds * MusicConst::kSampleRate) + 4;
    lineLeft_.assign(maxSamples, 0.0f);
    lineRight_.assign(maxSamples, 0.0f);
    writeIndex_ = 0;
}

void MusicDelay::Reset()
{
    std::fill(lineLeft_.begin(), lineLeft_.end(), 0.0f);
    std::fill(lineRight_.begin(), lineRight_.end(), 0.0f);
    writeIndex_ = 0;
}

void MusicDelay::Process(float *buffer, uint32_t frames, const DelayParams &params, float bpm)
{
    if (!params.enabled || lineLeft_.empty())
        return;

    const size_t lineSize = lineLeft_.size();
    const float seconds = std::clamp(DelaySeconds(params.noteDivision, bpm), 0.01f, kMaxDelaySeconds);
    const size_t delaySamples = std::clamp<size_t>(
        static_cast<size_t>(seconds * MusicConst::kSampleRate), 1, lineSize - 1);

    const float feedback = std::clamp(params.feedback, 0.0f, 0.95f);
    const float mix = std::clamp(params.mix, 0.0f, 1.0f);
    const float pingPong = std::clamp(params.pingPong, 0.0f, 1.0f);

    for (uint32_t i = 0; i < frames; ++i)
    {
        const size_t readIndex = (writeIndex_ + lineSize - delaySamples) % lineSize;
        const float delayedLeft = lineLeft_[readIndex];
        const float delayedRight = lineRight_[readIndex];

        const float inputLeft = buffer[i * 2 + 0];
        const float inputRight = buffer[i * 2 + 1];

        // 返す音を左右で入れ替えるほど、やまびこが左右を行き来する
        const float feedLeft = delayedLeft * (1.0f - pingPong) + delayedRight * pingPong;
        const float feedRight = delayedRight * (1.0f - pingPong) + delayedLeft * pingPong;

        lineLeft_[writeIndex_] = inputLeft + feedLeft * feedback;
        lineRight_[writeIndex_] = inputRight + feedRight * feedback;

        buffer[i * 2 + 0] = inputLeft + delayedLeft * mix;
        buffer[i * 2 + 1] = inputRight + delayedRight * mix;

        writeIndex_ = (writeIndex_ + 1) % lineSize;
    }
}

//==============================================================================
// リバーブ
//==============================================================================

void MusicReverb::Initialize()
{
    for (int i = 0; i < kCombCount; ++i)
    {
        combsLeft_[i].buffer.assign(ScaleTuning(kCombTuning[i]), 0.0f);
        combsRight_[i].buffer.assign(ScaleTuning(kCombTuning[i] + kStereoSpread), 0.0f);
        combsLeft_[i].index = 0;
        combsRight_[i].index = 0;
    }
    for (int i = 0; i < kAllPassCount; ++i)
    {
        allPassLeft_[i].buffer.assign(ScaleTuning(kAllPassTuning[i]), 0.0f);
        allPassRight_[i].buffer.assign(ScaleTuning(kAllPassTuning[i] + kStereoSpread), 0.0f);
        allPassLeft_[i].index = 0;
        allPassRight_[i].index = 0;
    }
}

void MusicReverb::Reset()
{
    for (int i = 0; i < kCombCount; ++i)
    {
        std::fill(combsLeft_[i].buffer.begin(), combsLeft_[i].buffer.end(), 0.0f);
        std::fill(combsRight_[i].buffer.begin(), combsRight_[i].buffer.end(), 0.0f);
        combsLeft_[i].filterStore = 0.0f;
        combsRight_[i].filterStore = 0.0f;
    }
    for (int i = 0; i < kAllPassCount; ++i)
    {
        std::fill(allPassLeft_[i].buffer.begin(), allPassLeft_[i].buffer.end(), 0.0f);
        std::fill(allPassRight_[i].buffer.begin(), allPassRight_[i].buffer.end(), 0.0f);
    }
}

void MusicReverb::Process(float *buffer, uint32_t frames, const ReverbParams &params)
{
    if (!params.enabled || combsLeft_[0].buffer.empty())
        return;

    // 元の Freeverb と同じ係数の置き方。roomSize をそのまま使うと響きすぎる
    const float feedback = std::clamp(params.roomSize, 0.0f, 1.0f) * 0.28f + 0.70f;
    const float damping = std::clamp(params.damping, 0.0f, 1.0f) * 0.4f;
    const float damping2 = 1.0f - damping;
    const float mix = std::clamp(params.mix, 0.0f, 1.0f);
    const float width = std::clamp(params.width, 0.0f, 1.0f);
    const float inputGain = 0.015f;

    for (uint32_t i = 0; i < frames; ++i)
    {
        const float dryLeft = buffer[i * 2 + 0];
        const float dryRight = buffer[i * 2 + 1];
        const float input = (dryLeft + dryRight) * inputGain;

        float wetLeft = 0.0f;
        float wetRight = 0.0f;

        // くし形フィルタを並列に通して、減衰の異なる反射をいっぺんに作る
        for (int c = 0; c < kCombCount; ++c)
        {
            {
                Comb &comb = combsLeft_[c];
                const float output = comb.buffer[comb.index];
                comb.filterStore = output * damping2 + comb.filterStore * damping;
                comb.buffer[comb.index] = input + comb.filterStore * feedback;
                comb.index = (comb.index + 1) % comb.buffer.size();
                wetLeft += output;
            }
            {
                Comb &comb = combsRight_[c];
                const float output = comb.buffer[comb.index];
                comb.filterStore = output * damping2 + comb.filterStore * damping;
                comb.buffer[comb.index] = input + comb.filterStore * feedback;
                comb.index = (comb.index + 1) % comb.buffer.size();
                wetRight += output;
            }
        }

        // 全域通過フィルタを直列に通して、反射を細かくばらけさせる
        for (int a = 0; a < kAllPassCount; ++a)
        {
            {
                AllPass &allPass = allPassLeft_[a];
                const float stored = allPass.buffer[allPass.index];
                allPass.buffer[allPass.index] = wetLeft + stored * 0.5f;
                allPass.index = (allPass.index + 1) % allPass.buffer.size();
                wetLeft = stored - wetLeft;
            }
            {
                AllPass &allPass = allPassRight_[a];
                const float stored = allPass.buffer[allPass.index];
                allPass.buffer[allPass.index] = wetRight + stored * 0.5f;
                allPass.index = (allPass.index + 1) % allPass.buffer.size();
                wetRight = stored - wetRight;
            }
        }

        // 左右をどれだけ混ぜるかで広がりを決める
        const float outLeft = wetLeft * width + wetRight * (1.0f - width);
        const float outRight = wetRight * width + wetLeft * (1.0f - width);

        buffer[i * 2 + 0] = dryLeft + outLeft * mix;
        buffer[i * 2 + 1] = dryRight + outRight * mix;
    }
}

//==============================================================================
// 保存・読み込み
//==============================================================================

void SaveEffectsTo(nlohmann::json &outJson, const DelayParams &delay, const ReverbParams &reverb)
{
    outJson["delay"] = {{"enabled", delay.enabled},
                        {"noteDivision", delay.noteDivision},
                        {"feedback", delay.feedback},
                        {"mix", delay.mix},
                        {"pingPong", delay.pingPong}};
    outJson["reverb"] = {{"enabled", reverb.enabled},
                         {"roomSize", reverb.roomSize},
                         {"damping", reverb.damping},
                         {"width", reverb.width},
                         {"mix", reverb.mix}};
}

void LoadEffectsFrom(const nlohmann::json &json, DelayParams &delay, ReverbParams &reverb)
{
    auto read = [](const nlohmann::json &source, const char *key, auto &target) {
        if (source.contains(key))
            target = source.at(key).get<std::decay_t<decltype(target)>>();
    };
    if (json.contains("delay"))
    {
        const auto &source = json.at("delay");
        read(source, "enabled", delay.enabled);
        read(source, "noteDivision", delay.noteDivision);
        read(source, "feedback", delay.feedback);
        read(source, "mix", delay.mix);
        read(source, "pingPong", delay.pingPong);
    }
    if (json.contains("reverb"))
    {
        const auto &source = json.at("reverb");
        read(source, "enabled", reverb.enabled);
        read(source, "roomSize", reverb.roomSize);
        read(source, "damping", reverb.damping);
        read(source, "width", reverb.width);
        read(source, "mix", reverb.mix);
    }
}

} // namespace Hagine
