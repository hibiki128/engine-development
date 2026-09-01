#include "Audio.h"
#include "utility/debug/imgui/ImGuiNotification.h"
#include <debug/log/Logger.h>
#include <cassert>
#include <cmath>
#include <cstring>
#include <fstream>

#ifdef USE_IMGUI
#include "imgui.h"
#include "utility/debug/imgui/DebugUIHelper.h"
#include <implot.h>
#endif // USE_IMGUI

namespace Hagine {

namespace {
// PCM の 1 サンプル（1 チャンネル分）を [-1,1] の float へ正規化する。
//   bits      : ビット深度（8 / 16 / 32）
//   formatTag : WAVEFORMATEX.wFormatTag（32bit が float か整数かの判定に使う）
float NormalizeSample(const uint8_t *p, uint16_t bits, uint16_t formatTag)
{
    switch (bits)
    {
    case 8:
        // 8bit PCM は符号なし（128 が無音）
        return (static_cast<float>(*p) - 128.0f) / 128.0f;
    case 16: {
        int16_t v = 0;
        std::memcpy(&v, p, sizeof(v));
        return static_cast<float>(v) / 32768.0f;
    }
    case 32: {
        if (formatTag == WAVE_FORMAT_IEEE_FLOAT)
        {
            float v = 0.0f;
            std::memcpy(&v, p, sizeof(v));
            return v;
        }
        int32_t v = 0;
        std::memcpy(&v, p, sizeof(v));
        return static_cast<float>(v) / 2147483648.0f;
    }
    default:
        return 0.0f;
    }
}
} // namespace
void Audio::Initialize(const std::string &directoryPath)
{
    HRESULT hr;

    directoryPath_ = directoryPath;

    hr = XAudio2Create(&xAudio2_, 0, XAUDIO2_DEFAULT_PROCESSOR);
    hr = xAudio2_->CreateMasteringVoice(&pMasterVoice_);
}

uint32_t Audio::LoadWave(const std::string &filename)
{

    if (loadedFiles_.find(filename) != loadedFiles_.end())
    {
        for (size_t i = 0; i < kMaxSoundData; ++i)
        {
            if (soundDatas_[i].name_ == filename)
            {
                return static_cast<uint32_t>(i);
            }
        }
    }

    std::string fullPath = directoryPath_ + "/" + filename;

    std::ifstream file;
    file.open(fullPath, std::ios_base::binary);
    if (!file.is_open())
    {
        Logger::Error("Failed to open audio file: \"" + fullPath + "\". The file was not found.");
        assert(file.is_open());
        return UINT32_MAX;
    }

    RiffHeader riff;
    file.read(reinterpret_cast<char *>(&riff), sizeof(riff));

    if (strncmp(riff.chunk.id, "RIFF", 4) != 0)
    {
        Logger::Error("Invalid audio file (missing RIFF header): \"" + fullPath + "\".");
        assert(0);
    }
    if (strncmp(riff.type, "WAVE", 4) != 0)
    {
        Logger::Error("Invalid audio file (not WAVE format): \"" + fullPath + "\".");
        assert(0);
    }

    ChunkHeader chunkHeader;
    FormatChunk format = {};

    while (file.read(reinterpret_cast<char *>(&chunkHeader), sizeof(chunkHeader)))
    {
        if (strncmp(chunkHeader.id, "fmt ", 4) == 0)
        {
            assert(chunkHeader.size <= sizeof(format.fmt));
            format.chunk = chunkHeader;
            file.read(reinterpret_cast<char *>(&format.fmt), chunkHeader.size);
            break;
        }
        else
        {
            file.seekg(chunkHeader.size, std::ios_base::cur);
        }
    }

    if (strncmp(format.chunk.id, "fmt ", 4) != 0)
    {
        Logger::Error("Invalid audio file (missing fmt chunk): \"" + fullPath + "\".");
        assert(0);
    }

    ChunkHeader data;
    while (file.read(reinterpret_cast<char *>(&data), sizeof(data)))
    {
        if (strncmp(data.id, "data", 4) == 0)
        {
            break;
        }
        else
        {
            file.seekg(data.size, std::ios_base::cur);
        }
    }

    if (strncmp(data.id, "data", 4) != 0)
    {
        Logger::Error("Invalid audio file (missing data chunk): \"" + fullPath + "\".");
        assert(0);
    }

    std::vector<uint8_t> buffer(data.size);
    file.read(reinterpret_cast<char *>(buffer.data()), data.size);
    file.close();

    SoundData &soundData = soundDatas_[soundDataIndex_];
    soundData.wfex = format.fmt;
    soundData.buffer = std::move(buffer);
    soundData.name_ = filename;

    loadedFiles_.insert(filename);

    uint32_t currentIndex = static_cast<uint32_t>(soundDataIndex_);
    soundDataIndex_ = (soundDataIndex_ + 1) % kMaxSoundData;

    ImGuiNotification::Post("音声ファイルを読み込みました: " + filename, {0.2f, 0.8f, 0.8f, 1.0f});
    return currentIndex;
}

void Audio::Unload(uint32_t soundIndex)
{
    SoundData &soundData = soundDatas_[soundIndex];
    std::string filename = soundData.name_;
    soundData.buffer.clear();
    soundData.wfex = {};
    soundData.name_.clear();
    ImGuiNotification::Post("音声ファイルをアンロードしました: " + filename, {0.9f, 0.7f, 0.2f, 1.0f});
}

void Audio::PlayWave(uint32_t soundIndex, float volume, bool loop)
{
    HRESULT result;

    const SoundData &soundData = soundDatas_[soundIndex];

    auto voice = std::make_unique<Voice>();
    voice->handle = soundIndex;
    voice->volume = volume;
    voice->callback = std::make_unique<VoiceCallback>();

    result = xAudio2_->CreateSourceVoice(&voice->sourceVoice, &soundData.wfex, 0, XAUDIO2_DEFAULT_FREQ_RATIO, voice->callback.get());
    assert(SUCCEEDED(result));

    XAUDIO2_BUFFER buf{};
    buf.pAudioData = soundData.buffer.data();
    buf.AudioBytes = static_cast<uint32_t>(soundData.buffer.size());
    buf.Flags = XAUDIO2_END_OF_STREAM;
    buf.pContext = voice.get();
    buf.LoopCount = loop ? XAUDIO2_LOOP_INFINITE : 0;

    result = voice->sourceVoice->SubmitSourceBuffer(&buf);
    assert(SUCCEEDED(result));

    result = voice->sourceVoice->Start();
    assert(SUCCEEDED(result));

    voice->sourceVoice->SetVolume(voice->volume);

    voices_.insert(std::move(voice));
}

void Audio::StopWave(uint32_t soundIndex)
{
    for (auto it = voices_.begin(); it != voices_.end();)
    {
        if ((*it)->handle == soundIndex)
        {
            if ((*it)->sourceVoice != nullptr)
            {
                (*it)->sourceVoice->Stop(0);
                (*it)->sourceVoice->DestroyVoice();
            }
            it = voices_.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

void Audio::SetVolume(uint32_t soundIndex, float volume)
{
    for (auto &voice : voices_)
    {
        if (voice->handle == soundIndex)
        {
            voice->volume = volume;
            voice->sourceVoice->SetVolume(volume);
            break;
        }
    }
}

void Audio::CleanupFinishedVoices()
{
    for (auto it = voices_.begin(); it != voices_.end();)
    {
        if ((*it)->sourceVoice == nullptr)
        {
            it = voices_.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

void Audio::Finalize()
{
    if (pMasterVoice_)
    {
        pMasterVoice_->DestroyVoice();
        pMasterVoice_ = nullptr;
    }

    for (auto &voice : voices_)
    {
        if (voice->sourceVoice)
        {
            voice->sourceVoice->DestroyVoice();
        }
    }

    if (xAudio2_)
    {
        xAudio2_.Reset();
    }

    voices_.clear();
}

//==============================================================================
// デバッグ補助関数
//==============================================================================

void Audio::DebugScanWavFiles()
{
    debugWavFileList_.clear();

    // 現在のサウンドルート配下を走査（Initialize で差し替えられている場合もそれに追従する）
    std::filesystem::path dir(directoryPath_);

    if (!std::filesystem::exists(dir))
    {
        return;
    }

    for (auto &entry : std::filesystem::recursive_directory_iterator(dir))
    {
        if (!entry.is_regular_file())
        {
            continue;
        }
        if (entry.path().extension() != ".wav")
        {
            continue;
        }

        // directoryPath_ からの相対パスをファイル名として登録
        auto rel = std::filesystem::relative(entry.path(), dir);
        debugWavFileList_.push_back(rel.string());
    }
}

float Audio::DebugGetDurationSec(uint32_t index) const
{
    const SoundData &sd = soundDatas_[index];
    if (sd.wfex.nAvgBytesPerSec == 0)
    {
        return 0.0f;
    }
    return static_cast<float>(sd.buffer.size()) /
           static_cast<float>(sd.wfex.nAvgBytesPerSec);
}

float Audio::DebugGetPositionSec(uint32_t index) const
{
    for (auto &voice : voices_)
    {
        if (voice->handle != index)
        {
            continue;
        }
        if (!voice->sourceVoice)
        {
            continue;
        }

        XAUDIO2_VOICE_STATE state{};
        voice->sourceVoice->GetState(&state);

        const SoundData &sd = soundDatas_[index];
        if (sd.wfex.nAvgBytesPerSec == 0)
        {
            return 0.0f;
        }

        // SamplesPlayed をバイト換算して秒に変換
        uint64_t bytes = state.SamplesPlayed *
                         sd.wfex.nBlockAlign;
        return static_cast<float>(bytes) /
               static_cast<float>(sd.wfex.nAvgBytesPerSec);
    }
    return 0.0f;
}

bool Audio::DebugIsPlaying(uint32_t index) const
{
    for (auto &voice : voices_)
    {
        if (voice->handle == index)
        {
            return true;
        }
    }
    return false;
}

uint32_t Audio::DebugResolveIndex(const std::string &filename) const
{
    auto it = debugLoadedMap_.find(filename);
    if (it != debugLoadedMap_.end())
    {
        return it->second;
    }
    return UINT32_MAX;
}

float Audio::GetCurrentAmplitude() const
{
    float maxAmp = 0.0f;

    for (auto &voice : voices_)
    {
        if (!voice || !voice->sourceVoice)
        {
            continue;
        }
        uint32_t index = voice->handle;
        if (index >= soundDatas_.size())
        {
            continue;
        }
        const SoundData &sd = soundDatas_[index];
        const WAVEFORMATEX &fmt = sd.wfex;
        const uint16_t channels = fmt.nChannels ? fmt.nChannels : 1;
        const uint16_t bits = fmt.wBitsPerSample;
        const uint8_t *data = sd.buffer.data();
        const size_t bytes = sd.buffer.size();
        if (!data || bytes == 0 || bits == 0)
        {
            continue;
        }
        const size_t bytesPerSample = bits / 8u;
        const size_t frameBytes = bytesPerSample * channels;
        if (frameBytes == 0)
        {
            continue;
        }
        const size_t totalFrames = bytes / frameBytes;
        if (totalFrames == 0)
        {
            continue;
        }

        XAUDIO2_VOICE_STATE state{};
        voice->sourceVoice->GetState(&state);
        // 再生位置（ループ再生で SamplesPlayed が総数を超えるためモジュロで巻き戻す）
        const size_t curFrame = static_cast<size_t>(state.SamplesPlayed % totalFrames);

        // 現在位置からの短い窓（約20ms）でピーク（最大絶対値）を取る。
        // ピークは RMS よりビート/打撃音に反応が良く、振動が音に合って見える。
        // 窓内は最大256点に間引いて一定コストにする。
        size_t window = (fmt.nSamplesPerSec ? fmt.nSamplesPerSec : 44100u) / 50u; // 20ms 相当
        if (window < 64)
        {
            window = 64;
        }
        const size_t step = (window / 256u) + 1u;

        float peak = 0.0f;
        for (size_t k = 0; k < window; k += step)
        {
            const size_t fr = (curFrame + k) % totalFrames;
            float s = 0.0f;
            for (uint16_t ch = 0; ch < channels; ++ch)
            {
                s += NormalizeSample(data + fr * frameBytes + ch * bytesPerSample, bits, fmt.wFormatTag);
            }
            s /= static_cast<float>(channels);
            const float a = s < 0.0f ? -s : s;
            if (a > peak)
            {
                peak = a;
            }
        }

        if (peak > maxAmp)
        {
            maxAmp = peak;
        }
    }

    return maxAmp;
}

#ifdef USE_IMGUI
void Audio::DebugBuildWaveform(uint32_t index)
{
    debugWaveformIndex_ = static_cast<int>(index);
    debugWaveformX_.clear();
    debugWaveformMin_.clear();
    debugWaveformMax_.clear();

    if (index >= soundDatas_.size())
    {
        return;
    }
    const SoundData &sd = soundDatas_[index];
    const WAVEFORMATEX &fmt = sd.wfex;
    const uint16_t channels = fmt.nChannels ? fmt.nChannels : 1;
    const uint16_t bits = fmt.wBitsPerSample;
    const uint8_t *data = sd.buffer.data();
    const size_t bytes = sd.buffer.size();
    if (!data || bytes == 0 || bits == 0)
    {
        return;
    }

    const size_t bytesPerSample = bits / 8u;
    const size_t frameBytes = bytesPerSample * channels;
    if (frameBytes == 0)
    {
        return;
    }
    const size_t totalFrames = bytes / frameBytes;
    if (totalFrames == 0)
    {
        return;
    }

    // 1 フレーム分のサンプルを [-1,1] の float に正規化して取り出す
    auto sampleAt = [&](size_t frame, uint16_t ch) -> float {
        const uint8_t *p = data + frame * frameBytes + ch * bytesPerSample;
        return NormalizeSample(p, bits, fmt.wFormatTag);
    };

    const int buckets = 600;
    debugWaveformX_.resize(buckets);
    debugWaveformMin_.assign(buckets, 0.0f);
    debugWaveformMax_.assign(buckets, 0.0f);

    for (int b = 0; b < buckets; ++b)
    {
        debugWaveformX_[b] = static_cast<float>(b);

        size_t start = static_cast<size_t>((static_cast<double>(b) / buckets) * totalFrames);
        size_t end = static_cast<size_t>((static_cast<double>(b + 1) / buckets) * totalFrames);
        if (end <= start)
        {
            end = start + 1;
        }
        if (end > totalFrames)
        {
            end = totalFrames;
        }

        // 長尺でも一定コストに収めるためバケット内を間引いて走査する
        size_t step = ((end - start) / 256u) + 1u;
        float mn = 1.0f;
        float mx = -1.0f;
        for (size_t fr = start; fr < end; fr += step)
        {
            float s = 0.0f;
            for (uint16_t ch = 0; ch < channels; ++ch)
            {
                s += sampleAt(fr, ch);
            }
            s /= static_cast<float>(channels);
            mn = (s < mn) ? s : mn;
            mx = (s > mx) ? s : mx;
        }
        debugWaveformMin_[b] = mn;
        debugWaveformMax_[b] = mx;
    }
}
#endif // USE_IMGUI

//==============================================================================
// Debug() 本体
//==============================================================================

void Audio::Debug()
{
#ifdef USE_IMGUI
    // ── マスター音量 ──
    SectionHeader("[ マスター ]", DebugTheme::kAccentBlue);
    {
        // 回転ノブで中央寄せ表示する
        const float knobSize = 64.0f;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() +
                             (ImGui::GetContentRegionAvail().x - knobSize) * 0.5f);
        if (ThemedKnob("音量##master", &debugMasterVolume_, 0.0f, 1.0f, "%.2f",
                       DebugTheme::kAccentBlue, knobSize))
        {
            if (pMasterVoice_)
            {
                pMasterVoice_->SetVolume(debugMasterVolume_);
            }
        }
    }

    ImGui::Spacing();

    // ── ファイルブラウザ ──
    SectionHeader(("[ ファイルブラウザ  " + directoryPath_ + "/ ]").c_str(), DebugTheme::kAccentGreen);
    ImGui::PushStyleColor(ImGuiCol_Button, DebugTheme::kBgGreen);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.45f, 0.68f, 0.52f, 0.40f));
    if (ImGui::Button("WAVファイルをスキャン"))
    {
        DebugScanWavFiles();
        debugSelectedFile_ = -1;
        ImGuiNotification::Post("WAVを " + std::to_string(debugWavFileList_.size()) + " 件検出しました",
                                {0.45f, 0.68f, 0.52f, 1.0f});
    }
    ImGui::PopStyleColor(2);
    ImGui::SameLine();
    ImGui::TextDisabled("(%zu ファイル)", debugWavFileList_.size());

    ImGui::BeginChild("##filelist", ImVec2(0, 150), ImGuiChildFlags_Borders);
    for (int i = 0; i < static_cast<int>(debugWavFileList_.size()); ++i)
    {
        const bool selected = (debugSelectedFile_ == i);
        if (ImGui::Selectable(debugWavFileList_[i].c_str(), selected))
        {
            debugSelectedFile_ = i;
        }
    }
    ImGui::EndChild();

    ImGui::Spacing();

    // ── 再生コントロール ──
    SectionHeader("[ 再生コントロール ]", DebugTheme::kAccentOrange);

    const bool hasSelection = (debugSelectedFile_ >= 0 &&
                               debugSelectedFile_ < static_cast<int>(debugWavFileList_.size()));

    if (!hasSelection)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
        ImGui::TextUnformatted("上のリストからファイルを選択してください");
        ImGui::PopStyleColor();
    }
    else
    {
        const std::string &selectedName = debugWavFileList_[debugSelectedFile_];

        uint32_t idx = DebugResolveIndex(selectedName);
        const bool loaded = (idx != UINT32_MAX);
        const bool playing = loaded && DebugIsPlaying(idx);

        // 名前 + 状態バッジ
        ImGui::AlignTextToFramePadding();
        ImGui::Text("%s", selectedName.c_str());
        ImGui::SameLine();
        if (loaded)
        {
            StatusBadge(playing ? "再生中" : "ロード済み",
                        playing ? DebugTheme::kAccentGreen : DebugTheme::kAccentCyan);
        }
        else
        {
            StatusBadge("未ロード", DebugTheme::kAccentOrange);
        }

        // 再生パラメータ（音量は回転ノブ）
        ThemedKnob("再生音量##vol", &debugVolume_, 0.0f, 1.0f, "%.2f",
                   DebugTheme::kAccentCyan, 52.0f);
        ImGui::Checkbox("ループ再生", &debugLoop_);

        ImGui::Spacing();

        if (!loaded)
        {
            //-- Load ボタン
            ImGui::PushStyleColor(ImGuiCol_Button, DebugTheme::kBgBlue);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.45f, 0.60f, 0.78f, 0.40f));
            if (ImGui::Button("ロード", ImVec2(120, 0)))
            {
                // selectedName は DebugScanWavFiles が directoryPath_ 基準で列挙した相対パスなので、
                // LoadWave にそのまま渡せる
                uint32_t newIdx = LoadWave(selectedName);
                debugLoadedMap_[selectedName] = newIdx;
                ImGuiNotification::Post("ロードしました: " + selectedName, {0.42f, 0.66f, 0.68f, 1.0f});
            }
            ImGui::PopStyleColor(2);
        }
        else
        {
            //-- Play ボタン
            ImGui::BeginDisabled(playing);
            if (ImGui::Button("再生"))
            {
                PlayWave(idx, debugVolume_, debugLoop_);
            }
            ImGui::EndDisabled();

            ImGui::SameLine();

            //-- Stop ボタン
            ImGui::BeginDisabled(!playing);
            if (ImGui::Button("停止"))
            {
                StopWave(idx);
            }
            ImGui::EndDisabled();

            ImGui::SameLine();

            //-- 再生中のみ音量を即時反映
            if (playing)
            {
                if (ImGui::Button("音量を適用"))
                {
                    SetVolume(idx, debugVolume_);
                }
                ImGui::SameLine();
            }

            //-- Unload ボタン
            ImGui::BeginDisabled(playing);
            ImGui::PushStyleColor(ImGuiCol_Button, DebugTheme::kBgRed);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.80f, 0.46f, 0.46f, 0.40f));
            if (ImGui::Button("アンロード"))
            {
                StopWave(idx);
                Unload(idx);
                debugLoadedMap_.erase(selectedName);
                loadedFiles_.erase(selectedName);
                ImGuiNotification::Post("アンロードしました: " + selectedName, {0.82f, 0.58f, 0.36f, 1.0f});
            }
            ImGui::PopStyleColor(2);
            ImGui::EndDisabled();

            //-- 再生時間バー
            if (loaded)
            {
                float duration = DebugGetDurationSec(idx);
                float position = playing ? DebugGetPositionSec(idx) : 0.0f;

                // ループ時は position が duration を超えることがあるのでクランプ
                if (duration > 0.0f)
                {
                    position = (position > duration) ? std::fmod(position, duration) : position;
                }

                ImGui::Spacing();

                // プログレスバー
                float fraction = (duration > 0.0f) ? (position / duration) : 0.0f;
                char overlay[64];
                snprintf(overlay, sizeof(overlay), "%.2f s / %.2f s", position, duration);
                ImGui::PushStyleColor(ImGuiCol_PlotHistogram, DebugTheme::kAccentGreen);
                ImGui::ProgressBar(fraction, ImVec2(-1.0f, 0.0f), overlay);
                ImGui::PopStyleColor();

                // 詳細情報
                if (duration > 0.0f)
                {
                    const SoundData &sd = soundDatas_[idx];
                    ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
                    ImGui::Text("ch:%u   %u Hz   %u bit   %u B/s",
                                sd.wfex.nChannels,
                                sd.wfex.nSamplesPerSec,
                                sd.wfex.wBitsPerSample,
                                sd.wfex.nAvgBytesPerSec);
                    ImGui::PopStyleColor();
                }

                // 波形プレビュー（PCM の min/max エンベロープを ImPlot で描画）
                // 選択音が変わったときだけ再構築する
                if (debugWaveformIndex_ != static_cast<int>(idx))
                {
                    DebugBuildWaveform(idx);
                }
                if (!debugWaveformMax_.empty())
                {
                    ImGui::Spacing();
                    if (ImPlot::BeginPlot("##waveform", ImVec2(-1.0f, 90.0f), ImPlotFlags_CanvasOnly))
                    {
                        ImPlot::SetupAxes(nullptr, nullptr,
                                          ImPlotAxisFlags_NoDecorations,
                                          ImPlotAxisFlags_NoDecorations);
                        const double bucketCount = static_cast<double>(debugWaveformMax_.size());
                        ImPlot::SetupAxesLimits(0.0, bucketCount, -1.05, 1.05, ImPlotCond_Always);

                        const int count = static_cast<int>(debugWaveformMax_.size());
                        // 振幅エンベロープ（min〜max を塗り）
                        ImPlot::SetNextFillStyle(DebugTheme::kAccentCyan, 0.55f);
                        ImPlot::PlotShaded("##env", debugWaveformX_.data(),
                                           debugWaveformMin_.data(),
                                           debugWaveformMax_.data(), count);

                        // 再生ヘッド（再生中のみ縦線で表示）
                        if (playing && duration > 0.0f)
                        {
                            double head = (position / duration) * bucketCount;
                            ImPlot::SetNextLineStyle(DebugTheme::kAccentOrange, 2.0f);
                            ImPlot::PlotInfLines("##playhead", &head, 1);
                        }
                        ImPlot::EndPlot();
                    }
                }
            }
        }
    }

    ImGui::Spacing();

    // ── ロード済みファイル一覧 ──
    SectionHeader("[ ロード済みファイル ]", DebugTheme::kAccentPurple);
    ImGui::BeginChild("##loadedlist", ImVec2(0, 120), ImGuiChildFlags_Borders);
    if (debugLoadedMap_.empty())
    {
        ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
        ImGui::TextUnformatted("（ロード済みファイルはありません）");
        ImGui::PopStyleColor();
    }
    for (auto &[name, soundIdx] : debugLoadedMap_)
    {
        bool isPlaying = DebugIsPlaying(soundIdx);
        float dur = DebugGetDurationSec(soundIdx);
        float pos = isPlaying ? DebugGetPositionSec(soundIdx) : 0.0f;

        ImGui::PushStyleColor(ImGuiCol_Text, isPlaying ? DebugTheme::kAccentGreen : DebugTheme::kTextDim);
        ImGui::TextUnformatted(isPlaying ? "[再生中]" : "[停止]");
        ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::Text("idx=%-3u  %.2f/%.2fs  %s", soundIdx, pos, dur, name.c_str());
    }
    ImGui::EndChild();

    // ── 全停止 ──
    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Button, DebugTheme::kBgRed);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.80f, 0.46f, 0.46f, 0.40f));
    if (ImGui::Button("すべて停止", ImVec2(-1.0f, 0.0f)))
    {
        for (auto &[name, soundIdx] : debugLoadedMap_)
        {
            StopWave(soundIdx);
        }
        ImGuiNotification::Post("すべての再生を停止しました", {0.82f, 0.58f, 0.36f, 1.0f});
    }
    ImGui::PopStyleColor(2);
#endif // USE_IMGUI
}
} // namespace Hagine
