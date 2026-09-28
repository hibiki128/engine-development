#include "SynthInstrument.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <vector>

namespace Hagine {
namespace {

constexpr int kSineTableSize = 2048;
constexpr float kPi = 3.14159265358979323846f;

// 減衰の係数を「指定した秒数でほぼ消えきる(-60dB)」意味にそろえるための定数。
// これが無いと、減衰は時定数（63%減るまでの時間）になり、
// 画面上の「1.6秒」に対して実際は5秒近く鳴り続けて発音数を食い潰す
constexpr float kDecayTo60dB = 6.907755f; // ln(1000)

/// 秒で指定した減衰時間から、1サンプルあたりの減衰係数を作る
float DecayCoefficient(float seconds)
{
    return std::exp(-kDecayTo60dB / (std::max(0.001f, seconds) * static_cast<float>(MusicConst::kSampleRate)));
}

/// サイン波のテーブル。倍音を重ねる波形では1サンプルあたり何度も引くので、
/// std::sin を毎回呼ぶより table 参照のほうがはるかに軽い。
const float *SineTable()
{
    static const std::vector<float> table = [] {
        std::vector<float> t(kSineTableSize + 1);
        for (int i = 0; i <= kSineTableSize; ++i)
        {
            t[i] = std::sin(2.0f * kPi * static_cast<float>(i) / static_cast<float>(kSineTableSize));
        }
        return t;
    }();
    return table.data();
}

/// テーブル引き + 線形補間のサイン波。phase は 0〜1 の位相
float TableSin(float phase)
{
    phase -= std::floor(phase);
    const float x = phase * static_cast<float>(kSineTableSize);
    const int index = static_cast<int>(x);
    const float frac = x - static_cast<float>(index);
    const float *table = SineTable();
    return table[index] + (table[index + 1] - table[index]) * frac;
}

/// ノイズ用の擬似乱数。生成はミキサースレッドのみなので thread_local で持つ
float NextNoise()
{
    static thread_local uint32_t state = 0x9E3779B9u;
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return (static_cast<float>(state) / 2147483648.0f) - 1.0f;
}

/// <summary>
/// 不連続点をなめらかにして折り返し歪みを抑える補正項（PolyBLEP）。
/// のこぎり波・矩形波の段差にこれを足すと、高い音でもジリジリした雑味が出にくくなる。
/// </summary>
/// <param name="t">位相 (0〜1)</param>
/// <param name="dt">1サンプルあたりの位相の進み</param>
/// <returns>float: 補正項</returns>
float PolyBlep(float t, float dt)
{
    if (dt <= 0.0f)
        return 0.0f;
    if (t < dt)
    {
        t /= dt;
        return t + t - t * t - 1.0f;
    }
    if (t > 1.0f - dt)
    {
        t = (t - 1.0f) / dt;
        return t * t + t + t + 1.0f;
    }
    return 0.0f;
}

/// 倍音の重ね方。{倍音次数, 振幅} の並び
struct Harmonic
{
    float multiple;
    float amplitude;
};

constexpr Harmonic kPianoHarmonics[] = {
    {1.0f, 1.00f}, {2.0f, 0.42f}, {3.0f, 0.22f}, {4.0f, 0.11f}, {5.0f, 0.06f}, {6.0f, 0.03f}};
constexpr Harmonic kOrganHarmonics[] = {
    {1.0f, 1.00f}, {2.0f, 0.70f}, {3.0f, 0.40f}, {4.0f, 0.50f}, {6.0f, 0.20f}, {8.0f, 0.25f}};

/// 倍音を重ねた波形を作る
float RenderHarmonics(const Harmonic *harmonics, int count, float phase)
{
    float sum = 0.0f;
    float norm = 0.0f;
    for (int i = 0; i < count; ++i)
    {
        sum += TableSin(phase * harmonics[i].multiple) * harmonics[i].amplitude;
        norm += harmonics[i].amplitude;
    }
    return (norm > 0.0f) ? (sum / norm) : 0.0f;
}

/// 定位から左右の音量を求める（合計エネルギーが一定になるようにする）
void PanGains(float pan, float &outLeft, float &outRight)
{
    const float angle = (std::clamp(pan, -1.0f, 1.0f) + 1.0f) * 0.25f * kPi;
    outLeft = std::cos(angle);
    outRight = std::sin(angle);
}

} // namespace

SynthInstrument::SynthInstrument()
{
    // 既定はピアノ。「まず鍵盤を鳴らす」が最初の用途なので、生成した時点で弾ける状態にしておく
    ApplyPreset(0);
}

void SynthInstrument::NoteOn(int note, float velocity)
{
    Voice &voice = AllocateVoice();
    voice.active = true;
    voice.note = note;
    voice.velocity = std::clamp(velocity, 0.0f, 1.0f);
    voice.frequency = NoteToFrequency(static_cast<float>(note));
    voice.phase1 = 0.0f;
    voice.phase2 = 0.0f;
    voice.stage = EnvStage::Attack;
    voice.ampLevel = 0.0f;
    voice.filterEnv = 1.0f;
    voice.ic1 = 0.0f;
    voice.ic2 = 0.0f;
    voice.startOrder = ++noteCounter_;
}

void SynthInstrument::NoteOff(int note)
{
    for (Voice &voice : voices_)
    {
        if (voice.active && voice.note == note && voice.stage != EnvStage::Release)
        {
            voice.stage = EnvStage::Release;
        }
    }
}

void SynthInstrument::AllNotesOff()
{
    for (Voice &voice : voices_)
    {
        voice.active = false;
        voice.stage = EnvStage::Idle;
        voice.ampLevel = 0.0f;
    }
}

int SynthInstrument::GetActiveVoiceCount() const
{
    int count = 0;
    for (const Voice &voice : voices_)
    {
        if (voice.active)
            ++count;
    }
    return count;
}

SynthInstrument::Voice &SynthInstrument::AllocateVoice()
{
    // 空きがあればそこへ
    for (Voice &voice : voices_)
    {
        if (!voice.active)
            return voice;
    }

    // 空きが無いときは、まず余韻に入っている中で一番古いものを奪う。
    // それも無ければ全体で一番古いものを奪う（新しい打鍵を鳴らせないほうが不自然なので）
    Voice *oldestReleased = nullptr;
    Voice *oldest = &voices_[0];
    for (Voice &voice : voices_)
    {
        if (voice.startOrder < oldest->startOrder)
            oldest = &voice;
        if (voice.stage == EnvStage::Release &&
            (oldestReleased == nullptr || voice.startOrder < oldestReleased->startOrder))
        {
            oldestReleased = &voice;
        }
    }
    return oldestReleased ? *oldestReleased : *oldest;
}

float SynthInstrument::Oscillator(int waveform, float phase, float phaseStep) const
{
    switch (static_cast<SynthWaveform>(waveform))
    {
    case SynthWaveform::Sine:
        return TableSin(phase);

    case SynthWaveform::Triangle:
        return 1.0f - 4.0f * std::fabs(phase - 0.5f);

    case SynthWaveform::Saw:
        return (2.0f * phase - 1.0f) - PolyBlep(phase, phaseStep);

    case SynthWaveform::Square:
    case SynthWaveform::Pulse: {
        const float width = (static_cast<SynthWaveform>(waveform) == SynthWaveform::Square)
                                ? 0.5f
                                : std::clamp(params_.pulseWidth, 0.05f, 0.95f);
        float value = (phase < width) ? 1.0f : -1.0f;
        value += PolyBlep(phase, phaseStep);
        float shifted = phase - width;
        if (shifted < 0.0f)
            shifted += 1.0f;
        value -= PolyBlep(shifted, phaseStep);
        return value;
    }

    case SynthWaveform::Piano:
        return RenderHarmonics(kPianoHarmonics, static_cast<int>(std::size(kPianoHarmonics)), phase);

    case SynthWaveform::Organ:
        return RenderHarmonics(kOrganHarmonics, static_cast<int>(std::size(kOrganHarmonics)), phase);

    case SynthWaveform::Noise:
        return NextNoise();

    default:
        return TableSin(phase);
    }
}

void SynthInstrument::Render(float *out, uint32_t frames)
{
    const float sampleRate = static_cast<float>(MusicConst::kSampleRate);

    // エンベロープの係数はブロック内で変わらないので、ここで一度だけ求める
    const float attackRate = 1.0f / std::max(0.0005f, params_.attack) / sampleRate;
    const float decayCoef = DecayCoefficient(params_.decay);
    const float releaseCoef = DecayCoefficient(params_.release);
    const float filterDecayCoef = DecayCoefficient(params_.filterDecay);
    const float sustain = std::clamp(params_.sustain, 0.0f, 1.0f);

    float panLeft = 0.0f;
    float panRight = 0.0f;
    PanGains(params_.pan, panLeft, panRight);

    const float detuneRatio = std::pow(2.0f, params_.osc2Detune / 1200.0f);
    const float osc2Ratio = std::pow(2.0f, static_cast<float>(params_.osc2Semitone) / 12.0f) * detuneRatio;
    const float mix2 = std::clamp(params_.osc2Mix, 0.0f, 1.0f);
    const float mix1 = 1.0f - mix2 * 0.5f; // 2つ混ぜても音量が跳ねないよう軽く抑える

    for (Voice &voice : voices_)
    {
        if (!voice.active)
            continue;

        const float step1 = voice.frequency / sampleRate;
        const float step2 = voice.frequency * osc2Ratio / sampleRate;

        // フィルタ係数はエンベロープで動くが、1ブロック(数ミリ秒)ごとの更新で十分なめらか。
        // 毎サンプル tan を呼ぶと音を出すだけでCPUを食い潰す
        const float cutoffHz = std::clamp(
            params_.cutoff * std::pow(2.0f, params_.filterEnvAmount * voice.filterEnv * 4.0f),
            30.0f, sampleRate * 0.45f);
        const float g = std::tan(kPi * cutoffHz / sampleRate);
        const float k = 2.0f - 2.0f * std::clamp(params_.resonance, 0.0f, 0.95f);
        const float a1 = 1.0f / (1.0f + g * (g + k));
        const float a2 = g * a1;
        const float a3 = g * a2;

        const float velocityGain = params_.velocityToVolume ? (0.25f + 0.75f * voice.velocity) : 1.0f;
        const float voiceGain = params_.gain * velocityGain;

        for (uint32_t i = 0; i < frames; ++i)
        {
            // --- 音量エンベロープ ---
            switch (voice.stage)
            {
            case EnvStage::Attack:
                voice.ampLevel += attackRate;
                if (voice.ampLevel >= 1.0f)
                {
                    voice.ampLevel = 1.0f;
                    voice.stage = EnvStage::Decay;
                }
                break;
            case EnvStage::Decay:
                voice.ampLevel = sustain + (voice.ampLevel - sustain) * decayCoef;
                if (sustain <= 0.0005f && voice.ampLevel <= 0.0002f)
                {
                    voice.active = false;
                }
                else if (std::fabs(voice.ampLevel - sustain) <= 0.0005f)
                {
                    voice.stage = EnvStage::Sustain;
                }
                break;
            case EnvStage::Sustain:
                break;
            case EnvStage::Release:
                voice.ampLevel *= releaseCoef;
                if (voice.ampLevel <= 0.0002f)
                    voice.active = false;
                break;
            default:
                voice.active = false;
                break;
            }
            if (!voice.active)
                break;

            voice.filterEnv *= filterDecayCoef;

            // --- オシレータ ---
            float sample = Oscillator(params_.waveform, voice.phase1, step1) * mix1;
            if (mix2 > 0.0f)
                sample += Oscillator(params_.waveform, voice.phase2, step2) * mix2;

            voice.phase1 += step1;
            if (voice.phase1 >= 1.0f)
                voice.phase1 -= 1.0f;
            voice.phase2 += step2;
            if (voice.phase2 >= 1.0f)
                voice.phase2 -= 1.0f;

            // --- ローパスフィルタ（ゼロディレイフィードバック型） ---
            const float v3 = sample - voice.ic2;
            const float v1 = a1 * voice.ic1 + a2 * v3;
            const float v2 = voice.ic2 + a2 * voice.ic1 + a3 * v3;
            voice.ic1 = 2.0f * v1 - voice.ic1;
            voice.ic2 = 2.0f * v2 - voice.ic2;
            sample = v2;

            const float value = sample * voice.ampLevel * voiceGain;
            out[i * 2 + 0] += value * panLeft;
            out[i * 2 + 1] += value * panRight;
        }
    }
}

void SynthInstrument::SaveTo(nlohmann::json &outJson) const
{
    outJson["waveform"] = params_.waveform;
    outJson["pulseWidth"] = params_.pulseWidth;
    outJson["osc2Semitone"] = params_.osc2Semitone;
    outJson["osc2Detune"] = params_.osc2Detune;
    outJson["osc2Mix"] = params_.osc2Mix;
    outJson["attack"] = params_.attack;
    outJson["decay"] = params_.decay;
    outJson["sustain"] = params_.sustain;
    outJson["release"] = params_.release;
    outJson["cutoff"] = params_.cutoff;
    outJson["resonance"] = params_.resonance;
    outJson["filterEnvAmount"] = params_.filterEnvAmount;
    outJson["filterDecay"] = params_.filterDecay;
    outJson["gain"] = params_.gain;
    outJson["pan"] = params_.pan;
    outJson["velocityToVolume"] = params_.velocityToVolume;
}

void SynthInstrument::LoadFrom(const nlohmann::json &json)
{
    // 項目が欠けていても既定値のまま残るようにする（保存形式を後から足せるように）
    auto read = [&json](const char *key, auto &target) {
        if (json.contains(key))
            target = json.at(key).get<std::decay_t<decltype(target)>>();
    };
    read("waveform", params_.waveform);
    read("pulseWidth", params_.pulseWidth);
    read("osc2Semitone", params_.osc2Semitone);
    read("osc2Detune", params_.osc2Detune);
    read("osc2Mix", params_.osc2Mix);
    read("attack", params_.attack);
    read("decay", params_.decay);
    read("sustain", params_.sustain);
    read("release", params_.release);
    read("cutoff", params_.cutoff);
    read("resonance", params_.resonance);
    read("filterEnvAmount", params_.filterEnvAmount);
    read("filterDecay", params_.filterDecay);
    read("gain", params_.gain);
    read("pan", params_.pan);
    read("velocityToVolume", params_.velocityToVolume);
}

namespace {

/// プリセットの中身。ApplyPreset / GetPresetName の両方から引く
struct SynthPreset
{
    const char *name;
    SynthParams params;
};

const SynthPreset kPresets[] = {
    {"ピアノ",
     {static_cast<int>(SynthWaveform::Piano), 0.5f, 0, 0.0f, 0.0f,
      0.002f, 1.8f, 0.0f, 0.35f,
      5500.0f, 0.10f, 0.80f, 0.9f,
      0.75f, 0.0f, true}},
    {"エレピ",
     {static_cast<int>(SynthWaveform::Sine), 0.5f, 12, 4.0f, 0.30f,
      0.003f, 1.3f, 0.04f, 0.30f,
      4800.0f, 0.12f, 0.60f, 0.55f,
      0.60f, 0.0f, true}},
    {"ベース",
     {static_cast<int>(SynthWaveform::Saw), 0.5f, 0, 7.0f, 0.35f,
      0.004f, 0.55f, 0.55f, 0.12f,
      750.0f, 0.35f, 0.55f, 0.25f,
      0.62f, 0.0f, true}},
    {"リード",
     {static_cast<int>(SynthWaveform::Pulse), 0.35f, 0, 6.0f, 0.25f,
      0.008f, 0.35f, 0.70f, 0.15f,
      4200.0f, 0.25f, 0.40f, 0.30f,
      0.52f, 0.0f, true}},
    {"パッド",
     {static_cast<int>(SynthWaveform::Saw), 0.5f, 0, 12.0f, 0.45f,
      0.60f, 1.5f, 0.70f, 1.20f,
      2400.0f, 0.20f, 0.35f, 1.20f,
      0.55f, 0.0f, true}},
    {"オルガン",
     {static_cast<int>(SynthWaveform::Organ), 0.5f, 0, 0.0f, 0.0f,
      0.008f, 0.10f, 1.00f, 0.08f,
      12000.0f, 0.05f, 0.10f, 0.20f,
      0.60f, 0.0f, false}},
    {"プラック",
     {static_cast<int>(SynthWaveform::Saw), 0.5f, 0, 0.0f, 0.0f,
      0.001f, 0.35f, 0.0f, 0.20f,
      3200.0f, 0.40f, 0.85f, 0.18f,
      0.58f, 0.0f, true}},
};

} // namespace

void SynthInstrument::ApplyPreset(int presetIndex)
{
    if (presetIndex < 0 || presetIndex >= GetPresetCount())
        return;
    params_ = kPresets[presetIndex].params;
    AllNotesOff();
}

int SynthInstrument::GetPresetCount()
{
    return static_cast<int>(std::size(kPresets));
}

const char *SynthInstrument::GetPresetName(int presetIndex)
{
    if (presetIndex < 0 || presetIndex >= GetPresetCount())
        return "";
    return kPresets[presetIndex].name;
}

const char *SynthInstrument::GetWaveformName(int waveform)
{
    static const char *kNames[] = {"サイン", "三角", "のこぎり", "矩形", "パルス", "ピアノ", "オルガン", "ノイズ"};
    if (waveform < 0 || waveform >= static_cast<int>(std::size(kNames)))
        return "";
    return kNames[waveform];
}

} // namespace Hagine
