#include "SamplerInstrument.h"

#include <algorithm>
#include <cmath>
#include <filesystem>

namespace Hagine {
namespace {

constexpr float kPi = 3.14159265358979323846f;

/// 定位から左右の音量を求める
void PanGains(float pan, float &outLeft, float &outRight)
{
    const float angle = (std::clamp(pan, -1.0f, 1.0f) + 1.0f) * 0.25f * kPi;
    outLeft = std::cos(angle);
    outRight = std::sin(angle);
}

} // namespace

int SamplerInstrument::LoadSample(const std::string &path, std::string *outError)
{
    AudioClip clip;
    if (!WavFile::Load(path, clip, outError))
        return -1;
    WavFile::ConvertToEngineFormat(clip);

    SamplerSlot slot;
    slot.name = std::filesystem::path(path).stem().string();
    slot.filePath = path;
    slot.clip = std::move(clip);
    // 追加順にノートを割り当てる。すでに使われている番号は飛ばす
    int note = 36;
    while (FindSlotForNote(note) >= 0 && note < MusicConst::kHighestNote)
        ++note;
    slot.note = note;

    slots_.push_back(std::move(slot));
    return static_cast<int>(slots_.size()) - 1;
}

bool SamplerInstrument::ReplaceSample(int slotIndex, const std::string &path, std::string *outError)
{
    if (slotIndex < 0 || slotIndex >= static_cast<int>(slots_.size()))
        return false;

    AudioClip clip;
    if (!WavFile::Load(path, clip, outError))
        return false;
    WavFile::ConvertToEngineFormat(clip);

    AllNotesOff();
    slots_[slotIndex].clip = std::move(clip);
    slots_[slotIndex].filePath = path;
    slots_[slotIndex].name = std::filesystem::path(path).stem().string();
    return true;
}

void SamplerInstrument::RemoveSlot(int slotIndex)
{
    if (slotIndex < 0 || slotIndex >= static_cast<int>(slots_.size()))
        return;
    AllNotesOff();
    slots_.erase(slots_.begin() + slotIndex);
}

int SamplerInstrument::FindSlotForNote(int note) const
{
    if (slots_.empty())
        return -1;

    if (mode_ == SamplerMode::DrumKit)
    {
        for (size_t i = 0; i < slots_.size(); ++i)
        {
            if (slots_[i].note == note)
                return static_cast<int>(i);
        }
        return -1;
    }

    // 音程つきのときは、元の音程が一番近いサンプルを選ぶ。
    // 複数読み込んでおけば、伸ばしすぎて不自然になるのを避けられる
    int best = 0;
    int bestDistance = std::abs(slots_[0].note - note);
    for (size_t i = 1; i < slots_.size(); ++i)
    {
        const int distance = std::abs(slots_[i].note - note);
        if (distance < bestDistance)
        {
            bestDistance = distance;
            best = static_cast<int>(i);
        }
    }
    return best;
}

std::string SamplerInstrument::GetNoteLabel(int note) const
{
    if (mode_ != SamplerMode::DrumKit)
        return std::string();
    const int slotIndex = FindSlotForNote(note);
    return (slotIndex >= 0) ? slots_[slotIndex].name : std::string();
}

SamplerInstrument::Voice &SamplerInstrument::AllocateVoice()
{
    for (size_t i = 0; i < voices_.size(); ++i)
    {
        if (!voices_[i].active)
        {
            voiceOrder_[i] = ++voiceCounter_;
            return voices_[i];
        }
    }
    size_t oldest = 0;
    for (size_t i = 1; i < voices_.size(); ++i)
    {
        if (voiceOrder_[i] < voiceOrder_[oldest])
            oldest = i;
    }
    voiceOrder_[oldest] = ++voiceCounter_;
    return voices_[oldest];
}

void SamplerInstrument::NoteOn(int note, float velocity)
{
    const int slotIndex = FindSlotForNote(note);
    if (slotIndex < 0)
        return;
    const SamplerSlot &slot = slots_[slotIndex];
    if (slot.clip.Empty())
        return;

    if (slot.chokeGroup != 0)
    {
        for (Voice &voice : voices_)
        {
            if (voice.active && voice.chokeGroup == slot.chokeGroup)
                voice.releaseCoef = std::exp(-1.0f / (0.01f * static_cast<float>(MusicConst::kSampleRate)));
        }
    }

    Voice &voice = AllocateVoice();
    voice.active = true;
    voice.slotIndex = slotIndex;
    voice.note = note;
    voice.chokeGroup = slot.chokeGroup;
    voice.position = 0.0;
    voice.loop = slot.loop;
    voice.amp = 1.0f;
    voice.releaseCoef = 0.0f;
    voice.gain = slot.gain * (0.25f + 0.75f * std::clamp(velocity, 0.0f, 1.0f));
    PanGains(slot.pan, voice.panLeft, voice.panRight);

    // ドラムキットのときは原音のまま、音程つきのときは鍵盤に合わせて伸び縮みさせる
    voice.step = (mode_ == SamplerMode::Pitched)
                     ? std::pow(2.0, static_cast<double>(note - slot.note) / 12.0)
                     : 1.0;
}

void SamplerInstrument::NoteOff(int note)
{
    for (Voice &voice : voices_)
    {
        if (!voice.active || voice.note != note)
            continue;
        if (voice.slotIndex < 0 || voice.slotIndex >= static_cast<int>(slots_.size()))
            continue;
        const SamplerSlot &slot = slots_[voice.slotIndex];
        // ドラムの一撃ものは離しても最後まで鳴らす。伸ばす音だけ余韻をつけて止める
        if (mode_ == SamplerMode::Pitched || slot.loop)
        {
            voice.releaseCoef = std::exp(-1.0f / (std::max(0.005f, slot.release) *
                                                  static_cast<float>(MusicConst::kSampleRate)));
        }
    }
}

void SamplerInstrument::AllNotesOff()
{
    for (Voice &voice : voices_)
    {
        voice.active = false;
        voice.slotIndex = -1;
    }
}

int SamplerInstrument::GetActiveVoiceCount() const
{
    int count = 0;
    for (const Voice &voice : voices_)
    {
        if (voice.active)
            ++count;
    }
    return count;
}

void SamplerInstrument::Render(float *out, uint32_t frames)
{
    for (Voice &voice : voices_)
    {
        if (!voice.active)
            continue;
        if (voice.slotIndex < 0 || voice.slotIndex >= static_cast<int>(slots_.size()))
        {
            voice.active = false;
            continue;
        }

        const AudioClip &clip = slots_[voice.slotIndex].clip;
        const uint32_t clipFrames = clip.FrameCount();
        if (clipFrames == 0)
        {
            voice.active = false;
            continue;
        }
        const uint32_t channels = clip.channels;

        for (uint32_t i = 0; i < frames; ++i)
        {
            if (voice.position >= static_cast<double>(clipFrames))
            {
                if (!voice.loop)
                {
                    voice.active = false;
                    break;
                }
                voice.position -= static_cast<double>(clipFrames);
            }

            if (voice.releaseCoef > 0.0f)
            {
                voice.amp *= voice.releaseCoef;
                if (voice.amp <= 0.0003f)
                {
                    voice.active = false;
                    break;
                }
            }

            // 位置が小数になるので、前後のサンプルを混ぜて読む
            const uint32_t index0 = static_cast<uint32_t>(voice.position);
            const uint32_t index1 = (index0 + 1 < clipFrames) ? (index0 + 1) : (voice.loop ? 0u : index0);
            const float t = static_cast<float>(voice.position - static_cast<double>(index0));

            const float left0 = clip.samples[static_cast<size_t>(index0) * channels];
            const float left1 = clip.samples[static_cast<size_t>(index1) * channels];
            const float right0 = (channels > 1) ? clip.samples[static_cast<size_t>(index0) * channels + 1] : left0;
            const float right1 = (channels > 1) ? clip.samples[static_cast<size_t>(index1) * channels + 1] : left1;

            const float left = (left0 + (left1 - left0) * t) * voice.gain * voice.amp;
            const float right = (right0 + (right1 - right0) * t) * voice.gain * voice.amp;

            out[i * 2 + 0] += left * voice.panLeft;
            out[i * 2 + 1] += right * voice.panRight;

            voice.position += voice.step;
        }
    }
}

void SamplerInstrument::SaveTo(nlohmann::json &outJson) const
{
    outJson["mode"] = static_cast<int>(mode_);

    nlohmann::json slotArray = nlohmann::json::array();
    for (const SamplerSlot &slot : slots_)
    {
        nlohmann::json item;
        item["name"] = slot.name;
        // 波形そのものは保存せず、読み込み元のパスだけを残す。
        // JSON にサンプルを埋めるとプロジェクトが巨大になるため
        item["filePath"] = slot.filePath;
        item["note"] = slot.note;
        item["gain"] = slot.gain;
        item["pan"] = slot.pan;
        item["release"] = slot.release;
        item["loop"] = slot.loop;
        item["chokeGroup"] = slot.chokeGroup;
        slotArray.push_back(item);
    }
    outJson["slots"] = slotArray;
}

void SamplerInstrument::LoadFrom(const nlohmann::json &json)
{
    AllNotesOff();
    if (json.contains("mode"))
        mode_ = static_cast<SamplerMode>(json.at("mode").get<int>());

    slots_.clear();
    if (!json.contains("slots") || !json.at("slots").is_array())
        return;

    for (const auto &item : json.at("slots"))
    {
        SamplerSlot slot;
        auto read = [&item](const char *key, auto &target) {
            if (item.contains(key))
                target = item.at(key).get<std::decay_t<decltype(target)>>();
        };
        read("name", slot.name);
        read("filePath", slot.filePath);
        read("note", slot.note);
        read("gain", slot.gain);
        read("pan", slot.pan);
        read("release", slot.release);
        read("loop", slot.loop);
        read("chokeGroup", slot.chokeGroup);

        // 波形は保存していないので、パスから読み直す。
        // 見つからなくてもスロット自体は残し、エディタ側で差し替えられるようにする
        if (!slot.filePath.empty() && WavFile::Load(slot.filePath, slot.clip))
            WavFile::ConvertToEngineFormat(slot.clip);

        slots_.push_back(std::move(slot));
    }
}

} // namespace Hagine
