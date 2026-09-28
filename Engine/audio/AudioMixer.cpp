// =============================================================
//  AudioMixer.cpp
//  Audio クラスのうち、用途別ミキサー・3D 定位・残響・フェードを担当する部分。
//  読み込みと従来 API は Audio.cpp 側にある。
// =============================================================
#include "Audio.h"
#include <debug/log/Logger.h>

#include <algorithm>
#include <cmath>
#include <iterator>
#include <random>

#ifdef USE_IMGUI
#include "imgui.h"
#include "utility/debug/imgui/DebugUIHelper.h"
#endif // USE_IMGUI

// CreateAudioReverb（残響）はインライン実装ではないのでインポートライブラリが要る。
// XAUDIO2_9.dll は Windows 10 以降に標準で入っており、本エンジンは D3D12 必須なので前提を満たす
#pragma comment(lib, "xaudio2.lib")

namespace Hagine {
namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kSpeedOfSound = 343.0f; // 音速[m/s]

/// 乱数。ピッチのばらつかせにだけ使う
float RandomRange(float range)
{
    if (range <= 0.0f)
        return 0.0f;
    static thread_local std::mt19937 engine{std::random_device{}()};
    std::uniform_real_distribution<float> distribution(-range, range);
    return distribution(engine);
}

/// 定位から左右の音量を求める（合計エネルギーが一定になるようにする）
void PanGains(float pan, float &outLeft, float &outRight)
{
    const float angle = (std::clamp(pan, -1.0f, 1.0f) + 1.0f) * 0.25f * kPi;
    outLeft = std::cos(angle);
    outRight = std::sin(angle);
}

/// 残響プリセットの表。XAudio2 の I3DL2 プリセットをそのまま並べてある
struct ReverbPresetEntry
{
    const char *name;
    XAUDIO2FX_REVERB_I3DL2_PARAMETERS params;
};

const ReverbPresetEntry kReverbPresets[] = {
    {"標準", XAUDIO2FX_I3DL2_PRESET_GENERIC},
    {"小部屋", XAUDIO2FX_I3DL2_PRESET_SMALLROOM},
    {"石の部屋", XAUDIO2FX_I3DL2_PRESET_STONEROOM},
    {"通路", XAUDIO2FX_I3DL2_PRESET_STONECORRIDOR},
    {"ホール", XAUDIO2FX_I3DL2_PRESET_CONCERTHALL},
    {"大ホール", XAUDIO2FX_I3DL2_PRESET_LARGEHALL},
    {"洞窟", XAUDIO2FX_I3DL2_PRESET_CAVE},
    {"闘技場", XAUDIO2FX_I3DL2_PRESET_ARENA},
    {"格納庫", XAUDIO2FX_I3DL2_PRESET_HANGAR},
    {"街", XAUDIO2FX_I3DL2_PRESET_CITY},
    {"森", XAUDIO2FX_I3DL2_PRESET_FOREST},
    {"平原", XAUDIO2FX_I3DL2_PRESET_PLAIN},
    {"山", XAUDIO2FX_I3DL2_PRESET_MOUNTAINS},
    {"水中", XAUDIO2FX_I3DL2_PRESET_UNDERWATER},
};

} // namespace

//==============================================================================
// 区分ごとのサブミックスと残響
//==============================================================================

void Audio::CreateBuses()
{
    if (!xAudio2_ || !pMasterVoice_)
        return;

    // 用途ごとの束ね先。ここの音量を動かすと、その区分の音がまとめて上下する
    for (size_t i = 0; i < busVoices_.size(); ++i)
    {
        HRESULT hr = xAudio2_->CreateSubmixVoice(&busVoices_[i], outputChannels_, outputSampleRate_, 0, 0, nullptr, nullptr);
        if (FAILED(hr))
        {
            Logger::Error("サブミックスボイスの作成に失敗しました。用途別の音量調整は効きません。");
            busVoices_[i] = nullptr;
            continue;
        }
        busVoices_[i]->SetVolume(busVolumes_[i]);
    }

    // 残響。ソースボイスから「送る量」を指定して届ける作りにするので、
    // 残響そのものは常に1本だけ用意しておく
    IUnknown *reverbApo = nullptr;
    if (SUCCEEDED(XAudio2CreateReverb(&reverbApo)))
    {
        XAUDIO2_EFFECT_DESCRIPTOR effect = {};
        effect.InitialState = TRUE;
        effect.OutputChannels = outputChannels_;
        effect.pEffect = reverbApo;

        XAUDIO2_EFFECT_CHAIN chain = {};
        chain.EffectCount = 1;
        chain.pEffectDescriptors = &effect;

        // 残響は処理順が最後になるよう、他より低い ProcessingStage を避けて 1 段後ろに置く
        HRESULT hr = xAudio2_->CreateSubmixVoice(&reverbVoice_, outputChannels_, outputSampleRate_, 0, 1, nullptr, &chain);
        if (FAILED(hr))
        {
            Logger::Error("残響用サブミックスの作成に失敗しました。残響は使えません。");
            reverbVoice_ = nullptr;
        }
        reverbApo->Release();
    }

    if (reverbVoice_)
    {
        SetReverbPreset(reverbPreset_);
        // 既定では切っておく（無条件に掛けると近接の効果音まで遠く聞こえるため）
        reverbVoice_->SetVolume(reverbEnabled_ ? 1.0f : 0.0f);
    }
}

void Audio::DestroyBuses()
{
    if (reverbVoice_)
    {
        reverbVoice_->DestroyVoice();
        reverbVoice_ = nullptr;
    }
    for (auto &bus : busVoices_)
    {
        if (bus)
        {
            bus->DestroyVoice();
            bus = nullptr;
        }
    }
}

void Audio::SetBusVolume(SoundBus bus, float volume)
{
    const size_t index = static_cast<size_t>(bus);
    if (index >= busVolumes_.size())
        return;
    busVolumes_[index] = std::clamp(volume, 0.0f, 1.0f);
    if (busVoices_[index])
        busVoices_[index]->SetVolume(busVolumes_[index]);
}

float Audio::GetBusVolume(SoundBus bus) const
{
    const size_t index = static_cast<size_t>(bus);
    return (index < busVolumes_.size()) ? busVolumes_[index] : 0.0f;
}

void Audio::SetReverbEnabled(bool enabled)
{
    reverbEnabled_ = enabled;
    if (reverbVoice_)
        reverbVoice_->SetVolume(enabled ? 1.0f : 0.0f);
}

void Audio::SetReverbPreset(int presetIndex)
{
    reverbPreset_ = std::clamp(presetIndex, 0, GetReverbPresetCount() - 1);
    if (!reverbVoice_)
        return;

    // I3DL2 のパラメータは XAudio2 の内部表現へ変換してから渡す決まりになっている
    XAUDIO2FX_REVERB_PARAMETERS native = {};
    ReverbConvertI3DL2ToNative(&kReverbPresets[reverbPreset_].params, &native);
    reverbVoice_->SetEffectParameters(0, &native, sizeof(native));
}

int Audio::GetReverbPresetCount()
{
    return static_cast<int>(std::size(kReverbPresets));
}

const char *Audio::GetReverbPresetName(int presetIndex)
{
    if (presetIndex < 0 || presetIndex >= GetReverbPresetCount())
        return "";
    return kReverbPresets[presetIndex].name;
}

//==============================================================================
// 再生
//==============================================================================

uint32_t Audio::GetOrLoad(const std::string &filename)
{
    // LoadWave 自身が読み込み済みファイルの使い回しを見てくれるので、そのまま通せばよい
    return LoadWave(filename);
}

Audio::Voice *Audio::FindVoice(SoundHandle handle) const
{
    if (!handle.IsValid())
        return nullptr;
    for (const auto &voice : voices_)
    {
        if (voice->id == handle.value)
            return voice.get();
    }
    return nullptr;
}

SoundHandle Audio::StartVoice(uint32_t soundIndex, const SoundPlayParams &params, const SoundEmitter3D *emitter)
{
    if (!xAudio2_ || soundIndex == UINT32_MAX || soundIndex >= soundDatas_.size())
        return {};

    const SoundData &soundData = soundDatas_[soundIndex];
    if (soundData.buffer.empty())
        return {};

    auto voice = std::make_unique<Voice>();
    voice->soundIndex = soundIndex;
    voice->id = nextVoiceId_++;
    voice->bus = params.bus;
    voice->sourceChannels = soundData.wfex.nChannels;
    voice->callback = std::make_unique<VoiceCallback>();
    voice->is3D = (emitter != nullptr);
    if (emitter)
        voice->emitter = *emitter;

    // フェードイン指定があれば 0 から立ち上げる
    voice->targetVolume = params.volume;
    voice->volume = (params.fadeInSeconds > 0.0f) ? 0.0f : params.volume;
    voice->fadeSpeed = (params.fadeInSeconds > 0.0f) ? (params.volume / params.fadeInSeconds) : 0.0f;
    voice->stopOnFadeEnd = false;

    // 同じ効果音を連打したときに機械的に聞こえないよう、毎回わずかに高さを散らす
    voice->basePitch = std::clamp(params.pitch + RandomRange(params.pitchRandomRange), 0.25f, 4.0f);

    // 送り先。区分のサブミックスへ、残響を使うならそちらへも同時に送る
    const size_t busIndex = static_cast<size_t>(params.bus);
    IXAudio2SubmixVoice *busVoice = (busIndex < busVoices_.size()) ? busVoices_[busIndex] : nullptr;
    const bool useReverb = reverbEnabled_ && reverbVoice_ != nullptr && params.reverbSend > 0.0f;

    XAUDIO2_SEND_DESCRIPTOR sends[2] = {};
    uint32_t sendCount = 0;
    if (busVoice)
    {
        sends[sendCount].Flags = 0;
        sends[sendCount].pOutputVoice = busVoice;
        ++sendCount;
    }
    if (useReverb)
    {
        sends[sendCount].Flags = 0;
        sends[sendCount].pOutputVoice = reverbVoice_;
        ++sendCount;
    }
    XAUDIO2_VOICE_SENDS sendList = {};
    sendList.SendCount = sendCount;
    sendList.pSends = sends;

    // 距離でこもらせるためにフィルタを使えるようにしておく
    const UINT32 flags = voice->is3D ? XAUDIO2_VOICE_USEFILTER : 0u;
    HRESULT hr = xAudio2_->CreateSourceVoice(&voice->sourceVoice, &soundData.wfex, flags,
                                             XAUDIO2_DEFAULT_FREQ_RATIO, voice->callback.get(),
                                             (sendCount > 0) ? &sendList : nullptr, nullptr);
    if (FAILED(hr) || voice->sourceVoice == nullptr)
    {
        Logger::Error("ソースボイスの作成に失敗しました: " + soundData.name_);
        return {};
    }

    XAUDIO2_BUFFER buffer = {};
    buffer.pAudioData = soundData.buffer.data();
    buffer.AudioBytes = static_cast<uint32_t>(soundData.buffer.size());
    buffer.Flags = XAUDIO2_END_OF_STREAM;
    buffer.pContext = voice.get();
    buffer.LoopCount = params.loop ? XAUDIO2_LOOP_INFINITE : 0;
    voice->sourceVoice->SubmitSourceBuffer(&buffer);

    voice->sourceVoice->SetVolume(voice->volume);
    voice->sourceVoice->SetFrequencyRatio(voice->basePitch);

    if (useReverb)
    {
        // 残響への送り量は「残響行きの経路だけに掛ける行列」で決める。
        // ここを 0 にしても本線（区分のサブミックス）の音は変わらない
        constexpr uint32_t kMaxChannels = 8;
        const uint32_t sourceChannels = std::min<uint32_t>(voice->sourceChannels, kMaxChannels);
        const uint32_t destChannels = std::min<uint32_t>(outputChannels_, kMaxChannels);
        const float send = std::clamp(params.reverbSend, 0.0f, 1.0f) / static_cast<float>(sourceChannels);
        float reverbMatrix[kMaxChannels * kMaxChannels] = {};
        for (uint32_t destination = 0; destination < destChannels; ++destination)
        {
            for (uint32_t source = 0; source < sourceChannels; ++source)
                reverbMatrix[sourceChannels * destination + source] = (destination < 2) ? send : 0.0f;
        }
        voice->sourceVoice->SetOutputMatrix(reverbVoice_, sourceChannels, destChannels, reverbMatrix);
    }

    const SoundHandle handle{voice->id};
    Voice *raw = voice.get();
    voices_.insert(std::move(voice));

    if (raw->is3D)
        ApplySpatialization(*raw);

    raw->sourceVoice->Start();
    return handle;
}

SoundHandle Audio::Play(uint32_t soundIndex, const SoundPlayParams &params)
{
    return StartVoice(soundIndex, params, nullptr);
}

SoundHandle Audio::PlayOneShot(const std::string &filename, const SoundPlayParams &params)
{
    return Play(GetOrLoad(filename), params);
}

SoundHandle Audio::Play3D(uint32_t soundIndex, const SoundEmitter3D &emitter, const SoundPlayParams &params)
{
    return StartVoice(soundIndex, params, &emitter);
}

SoundHandle Audio::PlayOneShot3D(const std::string &filename, const Vector3 &position, const SoundPlayParams &params)
{
    SoundEmitter3D emitter;
    emitter.position = position;
    return Play3D(GetOrLoad(filename), emitter, params);
}

void Audio::Stop(SoundHandle handle, float fadeOutSeconds)
{
    Voice *voice = FindVoice(handle);
    if (!voice || !voice->sourceVoice)
        return;

    if (fadeOutSeconds <= 0.0f)
    {
        voice->sourceVoice->Stop(0);
        voice->sourceVoice->DestroyVoice();
        voice->sourceVoice = nullptr; // 回収は Update / CleanupFinishedVoices が行う
        return;
    }
    voice->targetVolume = 0.0f;
    voice->fadeSpeed = voice->volume / fadeOutSeconds;
    voice->stopOnFadeEnd = true;
}

void Audio::SetVoiceVolume(SoundHandle handle, float volume, float fadeSeconds)
{
    Voice *voice = FindVoice(handle);
    if (!voice || !voice->sourceVoice)
        return;

    voice->targetVolume = std::max(0.0f, volume);
    voice->stopOnFadeEnd = false;
    if (fadeSeconds <= 0.0f)
    {
        voice->volume = voice->targetVolume;
        voice->fadeSpeed = 0.0f;
        voice->sourceVoice->SetVolume(voice->volume);
        return;
    }
    voice->fadeSpeed = std::fabs(voice->targetVolume - voice->volume) / fadeSeconds;
}

void Audio::SetVoicePitch(SoundHandle handle, float pitch)
{
    Voice *voice = FindVoice(handle);
    if (!voice || !voice->sourceVoice)
        return;
    voice->basePitch = std::clamp(pitch, 0.25f, 4.0f);
    voice->sourceVoice->SetFrequencyRatio(voice->basePitch);
}

void Audio::UpdateEmitter(SoundHandle handle, const SoundEmitter3D &emitter)
{
    Voice *voice = FindVoice(handle);
    if (!voice || !voice->sourceVoice)
        return;
    voice->emitter = emitter;
    voice->is3D = true;
    ApplySpatialization(*voice);
}

bool Audio::IsPlaying(SoundHandle handle) const
{
    const Voice *voice = FindVoice(handle);
    return voice != nullptr && voice->sourceVoice != nullptr;
}

void Audio::StopBus(SoundBus bus, float fadeOutSeconds)
{
    for (auto &voice : voices_)
    {
        if (voice->bus != bus || voice->sourceVoice == nullptr)
            continue;
        Stop(SoundHandle{voice->id}, fadeOutSeconds);
    }
}

SoundHandle Audio::CrossFadeBgm(const std::string &filename, float fadeSeconds, float volume)
{
    StopBus(SoundBus::BGM, fadeSeconds);

    SoundPlayParams params;
    params.volume = volume;
    params.loop = true;
    params.bus = SoundBus::BGM;
    params.fadeInSeconds = fadeSeconds;
    return PlayOneShot(filename, params);
}

//==============================================================================
// 3D 定位
//==============================================================================

void Audio::ApplySpatialization(Voice &voice) const
{
    if (!voice.sourceVoice || !voice.is3D)
        return;

    const size_t busIndex = static_cast<size_t>(voice.bus);
    IXAudio2SubmixVoice *busVoice = (busIndex < busVoices_.size()) ? busVoices_[busIndex] : nullptr;
    if (!busVoice)
        return;

    const SoundEmitter3D &emitter = voice.emitter;
    const Vector3 toEmitter = emitter.position - listener_.position;
    const float distance = toEmitter.Length();

    const float minDistance = std::max(0.01f, emitter.minDistance);
    const float maxDistance = std::max(minDistance + 0.01f, emitter.maxDistance);

    // --- 距離減衰 ---
    // 近くでは 1/距離（実際の音の減り方）、遠くでは maxDistance でちょうど 0 になるよう畳む
    const float normalized = std::clamp((distance - minDistance) / (maxDistance - minDistance), 0.0f, 1.0f);
    const float inverse = minDistance / std::max(distance, minDistance);
    const float attenuation = inverse * (1.0f - normalized);

    // --- 左右の振り分け ---
    float pan = 0.0f;
    if (distance > 0.0001f)
    {
        const Vector3 direction = toEmitter * (1.0f / distance);
        const float side = direction.Dot(listener_.right);
        // 耳元まで近づいたときに左右へ強く振れると不自然なので、近いほど中央へ寄せる
        const float focus = std::clamp(distance / minDistance, 0.0f, 1.0f);
        pan = side * focus;
    }
    float gainLeft = 0.0f;
    float gainRight = 0.0f;
    PanGains(pan, gainLeft, gainRight);

    // --- 出力行列 ---
    // XAudio2 の並びは [出力ch][入力ch]。2ch より多い出力でも前方 2ch へ入れれば破綻しない
    constexpr uint32_t kMaxChannels = 8;
    const uint32_t sourceChannels = std::min<uint32_t>(voice.sourceChannels, kMaxChannels);
    const uint32_t destChannels = std::min<uint32_t>(outputChannels_, kMaxChannels);
    float matrix[kMaxChannels * kMaxChannels] = {};
    for (uint32_t destination = 0; destination < destChannels; ++destination)
    {
        const float channelGain = (destination == 0) ? gainLeft : ((destination == 1) ? gainRight : 0.0f);
        for (uint32_t source = 0; source < sourceChannels; ++source)
        {
            matrix[sourceChannels * destination + source] = channelGain * attenuation / static_cast<float>(sourceChannels);
        }
    }
    voice.sourceVoice->SetOutputMatrix(busVoice, sourceChannels, destChannels, matrix);

    // --- 遠いほど高音が減る ---
    // XAudio2 のフィルタ係数は 2*sin(pi*遮断周波数/サンプリングレート)。
    // 上限 1.0 を超えると設定できないので、実質の上限は レート/6 になる
    const float cutoffHz = 20000.0f + (700.0f - 20000.0f) * normalized;
    XAUDIO2_FILTER_PARAMETERS filter = {};
    filter.Type = LowPassFilter;
    filter.Frequency = std::clamp(2.0f * std::sin(kPi * cutoffHz / static_cast<float>(outputSampleRate_)),
                                  0.01f, XAUDIO2_MAX_FILTER_FREQUENCY);
    filter.OneOverQ = 1.0f;
    voice.sourceVoice->SetFilterParameters(&filter);

    // --- ドップラー効果 ---
    float frequencyRatio = voice.basePitch;
    if (dopplerScale_ > 0.0f && distance > 0.0001f)
    {
        const Vector3 direction = toEmitter * (1.0f / distance);
        const float listenerSpeed = listener_.velocity.Dot(direction);
        const float emitterSpeed = emitter.velocity.Dot(direction);
        const float denominator = kSpeedOfSound + emitterSpeed;
        if (std::fabs(denominator) > 1.0f)
        {
            const float shift = (kSpeedOfSound + listenerSpeed) / denominator;
            // 行き過ぎると音程が壊れるので、効き具合を混ぜたうえで範囲を絞る
            frequencyRatio *= std::clamp(1.0f + (shift - 1.0f) * dopplerScale_, 0.5f, 2.0f);
        }
    }
    voice.sourceVoice->SetFrequencyRatio(std::clamp(frequencyRatio, 0.25f, 4.0f));
}

//==============================================================================
// 毎フレーム更新
//==============================================================================

void Audio::Update(float deltaTime)
{
    for (auto it = voices_.begin(); it != voices_.end();)
    {
        Voice *voice = it->get();

        // 再生が終わったものはコールバックが sourceVoice を nullptr にしている
        if (voice->sourceVoice == nullptr)
        {
            it = voices_.erase(it);
            continue;
        }

        // フェードを進める
        if (voice->fadeSpeed > 0.0f && voice->volume != voice->targetVolume)
        {
            const float step = voice->fadeSpeed * deltaTime;
            if (std::fabs(voice->targetVolume - voice->volume) <= step)
            {
                voice->volume = voice->targetVolume;
                voice->fadeSpeed = 0.0f;
            }
            else
            {
                voice->volume += (voice->targetVolume > voice->volume) ? step : -step;
            }
            voice->sourceVoice->SetVolume(voice->volume);

            if (voice->stopOnFadeEnd && voice->volume <= 0.0001f)
            {
                voice->sourceVoice->Stop(0);
                voice->sourceVoice->DestroyVoice();
                voice->sourceVoice = nullptr;
                it = voices_.erase(it);
                continue;
            }
        }

        // 音源が動いていなくても聞き手が動くので、3D は毎フレーム引き直す
        if (voice->is3D)
            ApplySpatialization(*voice);

        ++it;
    }
}

//==============================================================================
// デバッグUI
//==============================================================================

void Audio::DebugDrawMixer()
{
#ifdef USE_IMGUI
    SectionHeader("[ 用途別ミキサー ]", DebugTheme::kAccentPurple);
    {
        static const char *kBusNames[] = {"BGM", "効果音", "ボイス"};
        static const ImVec4 kBusColors[] = {DebugTheme::kAccentBlue, DebugTheme::kAccentOrange,
                                            DebugTheme::kAccentGreen};
        const float knobSize = std::max(46.0f, (ImGui::GetContentRegionAvail().x - 30.0f) / 3.0f);
        for (int i = 0; i < static_cast<int>(SoundBus::Count); ++i)
        {
            if (i > 0)
                ImGui::SameLine();
            float volume = busVolumes_[i];
            if (ThemedKnob(kBusNames[i], &volume, 0.0f, 1.0f, "%.2f", kBusColors[i], knobSize))
                SetBusVolume(static_cast<SoundBus>(i), volume);
        }
        DimText("ゲームの設定画面の「BGM音量」「効果音音量」はここに対応します");
    }

    ImGui::Spacing();
    SectionHeader("[ 残響 (リバーブ) ]", DebugTheme::kAccentCyan);
    {
        bool enabled = reverbEnabled_;
        if (ToggleRow("残響をかける", "##reverbEnabled", &enabled, DebugTheme::kAccentCyan))
            SetReverbEnabled(enabled);

        if (reverbVoice_ == nullptr)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kAccentRed);
            ImGui::TextUnformatted("残響を作成できませんでした（この環境では使えません）");
            ImGui::PopStyleColor();
        }
        else
        {
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::BeginCombo("##reverbPreset", GetReverbPresetName(reverbPreset_)))
            {
                for (int i = 0; i < GetReverbPresetCount(); ++i)
                {
                    if (ImGui::Selectable(GetReverbPresetName(i), reverbPreset_ == i))
                        SetReverbPreset(i);
                }
                ImGui::EndCombo();
            }
            DimText("鳴らすときに reverbSend を 0 より大きくした音だけが響きます");
        }
    }

    ImGui::Spacing();
    SectionHeader("[ 3D オーディオ ]", DebugTheme::kAccentYellow);
    {
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::SliderFloat("##doppler", &dopplerScale_, 0.0f, 2.0f, "ドップラー効果 %.2f");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("音源や聞き手が動いているとき、近づくと高く・遠ざかると低く聞こえる強さ。0 で無効");

        ReadOnlyRow("聞き手の位置", "%.1f, %.1f, %.1f", listener_.position.x, listener_.position.y,
                    listener_.position.z);
        ReadOnlyRow("出力チャンネル", "%u ch / %u Hz", outputChannels_, outputSampleRate_);
    }

    ImGui::Spacing();
    DebugDrawActiveVoices();
#endif // USE_IMGUI
}

void Audio::DebugDrawActiveVoices()
{
#ifdef USE_IMGUI
    const std::string header = "鳴っている音 (" + std::to_string(voices_.size()) + ")##activeVoices";
    if (!ThemedHeader(header.c_str(), DebugTheme::kAccentRed, false))
    {
        return;
    }

    if (voices_.empty())
    {
        DimText("今は何も鳴っていません");
        return;
    }

    static const char *kBusNames[] = {"BGM", "SE", "Voice"};
    if (ImGui::BeginTable("##voiceTable", 5,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
    {
        ImGui::TableSetupColumn("ファイル");
        ImGui::TableSetupColumn("区分");
        ImGui::TableSetupColumn("音量");
        ImGui::TableSetupColumn("3D");
        ImGui::TableSetupColumn("");
        ImGui::TableHeadersRow();

        // 走査中に止めると集合が変わるので、押されたものは覚えておいて後で止める
        SoundHandle pendingStop{};
        for (const auto &voice : voices_)
        {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(soundDatas_[voice->soundIndex].name_.c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(kBusNames[static_cast<size_t>(voice->bus)]);
            ImGui::TableNextColumn();
            ImGui::Text("%.2f", voice->volume);
            ImGui::TableNextColumn();
            if (voice->is3D)
            {
                const float distance = (voice->emitter.position - listener_.position).Length();
                ImGui::Text("%.1f m", distance);
            }
            else
            {
                ImGui::TextUnformatted("-");
            }
            ImGui::TableNextColumn();
            ImGui::PushID(static_cast<int>(voice->id));
            {
                ScopedButtonColors colors(DebugTheme::kButtonDanger, DebugTheme::kButtonDangerHover);
                if (ImGui::SmallButton("停止"))
                    pendingStop = SoundHandle{voice->id};
            }
            ImGui::PopID();
        }
        ImGui::EndTable();

        if (pendingStop.IsValid())
            Stop(pendingStop, 0.1f);
    }
#endif // USE_IMGUI
}

} // namespace Hagine
