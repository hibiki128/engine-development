#include "DrumMachineInstrument.h"

#include <algorithm>
#include <cmath>
#include <iterator>

namespace Hagine {
namespace {

constexpr float kPi = 3.14159265358979323846f;

/// ノイズ用の擬似乱数。生成はミキサースレッドのみなので thread_local で持つ
float NextNoise()
{
    static thread_local uint32_t state = 0x5BD1E995u;
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return (static_cast<float>(state) / 2147483648.0f) - 1.0f;
}

/// 定位から左右の音量を求める
void PanGains(float pan, float &outLeft, float &outRight)
{
    const float angle = (std::clamp(pan, -1.0f, 1.0f) + 1.0f) * 0.25f * kPi;
    outLeft = std::cos(angle);
    outRight = std::sin(angle);
}

/// <summary>
/// 秒で指定した減衰時間から、1サンプルあたりの減衰係数を作る。
/// 「指定した秒数でほぼ消えきる(-60dB)」意味にそろえてあるので、
/// 画面の数値と実際の長さが一致する
/// </summary>
/// <param name="seconds">消えきるまでの時間[秒]</param>
/// <returns>float: 1サンプルあたりに掛ける係数</returns>
float DecayCoef(float seconds)
{
    constexpr float kDecayTo60dB = 6.907755f; // ln(1000)
    return std::exp(-kDecayTo60dB / (std::max(0.005f, seconds) * static_cast<float>(MusicConst::kSampleRate)));
}

} // namespace

DrumMachineInstrument::DrumMachineInstrument()
{
    ResetToDefaultKit();
}

void DrumMachineInstrument::ResetToDefaultKit()
{
    pads_.clear();
    // ノート番号は一般的なドラムマップに合わせてある。
    // ピアノロールでドラム譜を打つとき、他のソフトと同じ並びで読めるようにするため
    pads_.push_back({"キック", static_cast<int>(DrumVoiceType::Kick), 36, 50.0f, 0.40f, 0.35f, 1.00f, 0.0f, 0});
    pads_.push_back({"スネア", static_cast<int>(DrumVoiceType::Snare), 38, 190.0f, 0.22f, 0.55f, 0.85f, 0.0f, 0});
    pads_.push_back({"クローズハット", static_cast<int>(DrumVoiceType::HiHat), 42, 8000.0f, 0.06f, 0.80f, 0.55f, 0.15f, 1});
    pads_.push_back({"オープンハット", static_cast<int>(DrumVoiceType::HiHat), 46, 7500.0f, 0.45f, 0.75f, 0.50f, 0.15f, 1});
    pads_.push_back({"ロータム", static_cast<int>(DrumVoiceType::Tom), 41, 110.0f, 0.45f, 0.30f, 0.75f, -0.30f, 0});
    pads_.push_back({"ハイタム", static_cast<int>(DrumVoiceType::Tom), 45, 170.0f, 0.38f, 0.30f, 0.75f, 0.30f, 0});
    pads_.push_back({"クラップ", static_cast<int>(DrumVoiceType::Clap), 39, 1200.0f, 0.24f, 0.65f, 0.70f, -0.20f, 0});
    pads_.push_back({"クラッシュ", static_cast<int>(DrumVoiceType::Cymbal), 49, 6000.0f, 1.20f, 0.70f, 0.45f, 0.25f, 0});
    AllNotesOff();
}

void DrumMachineInstrument::AddPad(const DrumPad &pad)
{
    pads_.push_back(pad);
}

void DrumMachineInstrument::RemovePad(int index)
{
    if (index < 0 || index >= static_cast<int>(pads_.size()))
        return;
    AllNotesOff();
    pads_.erase(pads_.begin() + index);
}

std::string DrumMachineInstrument::GetNoteLabel(int note) const
{
    for (const DrumPad &pad : pads_)
    {
        if (pad.note == note)
            return pad.name;
    }
    return std::string();
}

DrumMachineInstrument::Voice &DrumMachineInstrument::AllocateVoice()
{
    for (Voice &voice : voices_)
    {
        if (!voice.active)
            return voice;
    }
    // 空きが無いときは一番小さくなっている音を差し替える（消えても気付かれにくい）
    Voice *quietest = &voices_[0];
    for (Voice &voice : voices_)
    {
        if (voice.amp < quietest->amp)
            quietest = &voice;
    }
    return *quietest;
}

void DrumMachineInstrument::NoteOn(int note, float velocity)
{
    int padIndex = -1;
    for (size_t i = 0; i < pads_.size(); ++i)
    {
        if (pads_[i].note == note)
        {
            padIndex = static_cast<int>(i);
            break;
        }
    }
    if (padIndex < 0)
        return; // 割り当ての無いノートは鳴らさない

    const DrumPad &pad = pads_[padIndex];

    // ハイハットのように、開いた音と閉じた音が同時に鳴るとおかしいものを止める
    if (pad.chokeGroup != 0)
    {
        for (Voice &voice : voices_)
        {
            if (voice.active && voice.chokeGroup == pad.chokeGroup)
                voice.ampCoef = std::min(voice.ampCoef, DecayCoef(0.01f));
        }
    }

    Voice &voice = AllocateVoice();
    voice.active = true;
    voice.padIndex = padIndex;
    voice.chokeGroup = pad.chokeGroup;
    voice.velocity = std::clamp(velocity, 0.0f, 1.0f);
    voice.type = pad.type;
    voice.tone = std::clamp(pad.tone, 0.0f, 1.0f);
    voice.gain = pad.gain;
    voice.phase = 0.0f;
    voice.phase2 = 0.0f;
    voice.amp = 1.0f;
    voice.noiseAmp = 1.0f;
    voice.lowpassState = 0.0f;
    voice.elapsed = 0.0;
    voice.frequency = pad.tune;
    voice.targetFrequency = pad.tune;
    voice.sweepCoef = 0.0f;
    voice.ampCoef = DecayCoef(pad.decay);
    voice.noiseCoef = DecayCoef(pad.decay);
    PanGains(pad.pan, voice.panLeft, voice.panRight);

    // 種類ごとの立ち上げ。打楽器らしさはほぼここで決まる
    switch (static_cast<DrumVoiceType>(pad.type))
    {
    case DrumVoiceType::Kick:
        // 高いところから一気に落とすと「ドッ」という頭ができる
        voice.frequency = pad.tune * 6.0f;
        voice.targetFrequency = pad.tune;
        voice.sweepCoef = DecayCoef(0.03f + 0.05f * voice.tone);
        break;
    case DrumVoiceType::Tom:
        voice.frequency = pad.tune * 2.2f;
        voice.targetFrequency = pad.tune;
        voice.sweepCoef = DecayCoef(0.08f);
        break;
    case DrumVoiceType::Snare:
        voice.noiseCoef = DecayCoef(pad.decay * 1.25f);
        voice.ampCoef = DecayCoef(pad.decay * 0.6f);
        break;
    case DrumVoiceType::Clap:
        voice.noiseCoef = DecayCoef(pad.decay);
        break;
    case DrumVoiceType::Rim:
        voice.ampCoef = DecayCoef(std::min(pad.decay, 0.08f));
        break;
    default:
        break;
    }
}

void DrumMachineInstrument::NoteOff(int note)
{
    // 打楽器は押している長さでは変わらないので、ノートオフでは何もしない。
    // 長さはパッドの減衰時間だけで決まる
    (void)note;
}

void DrumMachineInstrument::AllNotesOff()
{
    for (Voice &voice : voices_)
    {
        voice.active = false;
        voice.amp = 0.0f;
    }
}

int DrumMachineInstrument::GetActiveVoiceCount() const
{
    int count = 0;
    for (const Voice &voice : voices_)
    {
        if (voice.active)
            ++count;
    }
    return count;
}

void DrumMachineInstrument::Render(float *out, uint32_t frames)
{
    const float sampleRate = static_cast<float>(MusicConst::kSampleRate);
    const double sampleDuration = 1.0 / static_cast<double>(MusicConst::kSampleRate);

    for (Voice &voice : voices_)
    {
        if (!voice.active)
            continue;

        const float velocityGain = 0.25f + 0.75f * voice.velocity;
        const float totalGain = voice.gain * velocityGain;

        for (uint32_t i = 0; i < frames; ++i)
        {
            voice.amp *= voice.ampCoef;
            voice.noiseAmp *= voice.noiseCoef;
            voice.elapsed += sampleDuration;
            if (voice.amp <= 0.0003f && voice.noiseAmp <= 0.0003f)
            {
                voice.active = false;
                break;
            }

            // 音程を下げていく種類はここで追い込む
            if (voice.sweepCoef > 0.0f)
            {
                voice.frequency = voice.targetFrequency +
                                  (voice.frequency - voice.targetFrequency) * voice.sweepCoef;
            }

            float sample = 0.0f;
            switch (static_cast<DrumVoiceType>(voice.type))
            {
            case DrumVoiceType::Kick: {
                voice.phase += voice.frequency / sampleRate;
                if (voice.phase >= 1.0f)
                    voice.phase -= 1.0f;
                sample = std::sin(2.0f * kPi * voice.phase) * voice.amp;
                // 頭の「パチッ」を少しだけ足す
                sample += NextNoise() * voice.noiseAmp * voice.noiseAmp * voice.tone * 0.25f;
                break;
            }
            case DrumVoiceType::Tom: {
                voice.phase += voice.frequency / sampleRate;
                if (voice.phase >= 1.0f)
                    voice.phase -= 1.0f;
                sample = std::sin(2.0f * kPi * voice.phase) * voice.amp;
                sample += NextNoise() * voice.noiseAmp * voice.tone * 0.15f;
                break;
            }
            case DrumVoiceType::Snare: {
                voice.phase += voice.frequency / sampleRate;
                if (voice.phase >= 1.0f)
                    voice.phase -= 1.0f;
                voice.phase2 += voice.frequency * 1.48f / sampleRate;
                if (voice.phase2 >= 1.0f)
                    voice.phase2 -= 1.0f;
                const float body = (std::sin(2.0f * kPi * voice.phase) +
                                    std::sin(2.0f * kPi * voice.phase2) * 0.7f) *
                                   voice.amp * (1.0f - voice.tone * 0.6f);
                // スナッピー（響き線）はノイズを高域寄りにして作る
                const float noise = NextNoise();
                voice.lowpassState += 0.45f * (noise - voice.lowpassState);
                const float high = noise - voice.lowpassState;
                sample = body + high * voice.noiseAmp * voice.tone * 1.1f;
                break;
            }
            case DrumVoiceType::HiHat:
            case DrumVoiceType::Cymbal: {
                const float noise = NextNoise();
                // 低い成分を引いて金物らしいシャリつきだけ残す
                const float cut = (voice.type == static_cast<int>(DrumVoiceType::HiHat)) ? 0.62f : 0.40f;
                voice.lowpassState += cut * (noise - voice.lowpassState);
                sample = (noise - voice.lowpassState) * voice.noiseAmp * (0.5f + voice.tone * 0.8f);
                break;
            }
            case DrumVoiceType::Clap: {
                // 3回ぶんの短い連打を重ねて、拍手らしい「パチパチ」にする
                float burst = 0.0f;
                const double spacing = 0.009;
                for (int b = 0; b < 3; ++b)
                {
                    const double start = spacing * b;
                    if (voice.elapsed >= start)
                    {
                        const float age = static_cast<float>(voice.elapsed - start);
                        burst += std::exp(-age * 90.0f);
                    }
                }
                const float noise = NextNoise();
                voice.lowpassState += 0.55f * (noise - voice.lowpassState);
                const float high = noise - voice.lowpassState;
                sample = high * (burst * 0.6f + voice.noiseAmp * 0.5f) * (0.6f + voice.tone * 0.8f);
                break;
            }
            case DrumVoiceType::Rim: {
                voice.phase += voice.frequency / sampleRate;
                if (voice.phase >= 1.0f)
                    voice.phase -= 1.0f;
                sample = (std::sin(2.0f * kPi * voice.phase) * 0.5f + NextNoise() * 0.5f) * voice.amp;
                break;
            }
            case DrumVoiceType::Cowbell: {
                voice.phase += voice.frequency / sampleRate;
                if (voice.phase >= 1.0f)
                    voice.phase -= 1.0f;
                voice.phase2 += voice.frequency * 1.48f / sampleRate;
                if (voice.phase2 >= 1.0f)
                    voice.phase2 -= 1.0f;
                const float squareA = (voice.phase < 0.5f) ? 1.0f : -1.0f;
                const float squareB = (voice.phase2 < 0.5f) ? 1.0f : -1.0f;
                sample = (squareA + squareB) * 0.35f * voice.amp;
                break;
            }
            default:
                break;
            }

            const float value = sample * totalGain;
            out[i * 2 + 0] += value * voice.panLeft;
            out[i * 2 + 1] += value * voice.panRight;
        }
    }
}

void DrumMachineInstrument::SaveTo(nlohmann::json &outJson) const
{
    nlohmann::json padArray = nlohmann::json::array();
    for (const DrumPad &pad : pads_)
    {
        nlohmann::json item;
        item["name"] = pad.name;
        item["type"] = pad.type;
        item["note"] = pad.note;
        item["tune"] = pad.tune;
        item["decay"] = pad.decay;
        item["tone"] = pad.tone;
        item["gain"] = pad.gain;
        item["pan"] = pad.pan;
        item["chokeGroup"] = pad.chokeGroup;
        padArray.push_back(item);
    }
    outJson["pads"] = padArray;
}

void DrumMachineInstrument::LoadFrom(const nlohmann::json &json)
{
    if (!json.contains("pads") || !json.at("pads").is_array())
        return;

    AllNotesOff();
    pads_.clear();
    for (const auto &item : json.at("pads"))
    {
        DrumPad pad;
        auto read = [&item](const char *key, auto &target) {
            if (item.contains(key))
                target = item.at(key).get<std::decay_t<decltype(target)>>();
        };
        read("name", pad.name);
        read("type", pad.type);
        read("note", pad.note);
        read("tune", pad.tune);
        read("decay", pad.decay);
        read("tone", pad.tone);
        read("gain", pad.gain);
        read("pan", pad.pan);
        read("chokeGroup", pad.chokeGroup);
        pads_.push_back(pad);
    }
}

const char *DrumMachineInstrument::GetVoiceTypeName(int type)
{
    static const char *kNames[] = {"キック", "スネア", "ハイハット", "タム", "クラップ", "シンバル", "リム", "カウベル"};
    if (type < 0 || type >= static_cast<int>(std::size(kNames)))
        return "";
    return kNames[type];
}

} // namespace Hagine
