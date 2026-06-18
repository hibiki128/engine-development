#include "Audio.h"
#include "Engine/Utility/Debug/ImGui/ImGuiNotification.h"
#include <cassert>
#include <fstream>

#ifdef _DEBUG
#include "imgui.h"
#include "Engine/Utility/Debug/ImGui/Debugui_improved.h"
#endif // _DEBUG

namespace Hagine {
void Audio::Initialize(const std::string &directoryPath) {
    HRESULT hr;

    directoryPath_ = directoryPath;

    hr = XAudio2Create(&xAudio2_, 0, XAUDIO2_DEFAULT_PROCESSOR);
    hr = xAudio2_->CreateMasteringVoice(&masterVoice_);
}

uint32_t Audio::LoadWave(const std::string &filename) {

    if (loadedFiles_.find(filename) != loadedFiles_.end()) {
        for (size_t i = 0; i < kMaxSoundData; ++i) {
            if (soundDatas_[i].name_ == filename) {
                return static_cast<uint32_t>(i);
            }
        }
    }

    std::string fullPath = directoryPath_ + "/" + filename;

    std::ifstream file;
    file.open(fullPath, std::ios_base::binary);
    assert(file.is_open());

    RiffHeader riff;
    file.read((char *)&riff, sizeof(riff));

    if (strncmp(riff.chunk.id, "RIFF", 4) != 0) {
        assert(0);
    }
    if (strncmp(riff.type, "WAVE", 4) != 0) {
        assert(0);
    }

    ChunkHeader chunkHeader;
    FormatChunk format = {};

    while (file.read((char *)&chunkHeader, sizeof(chunkHeader))) {
        if (strncmp(chunkHeader.id, "fmt ", 4) == 0) {
            assert(chunkHeader.size <= sizeof(format.fmt));
            format.chunk = chunkHeader;
            file.read((char *)&format.fmt, chunkHeader.size);
            break;
        } else {
            file.seekg(chunkHeader.size, std::ios_base::cur);
        }
    }

    if (strncmp(format.chunk.id, "fmt ", 4) != 0) {
        assert(0);
    }

    ChunkHeader data;
    while (file.read((char *)&data, sizeof(data))) {
        if (strncmp(data.id, "data", 4) == 0) {
            break;
        } else {
            file.seekg(data.size, std::ios_base::cur);
        }
    }

    if (strncmp(data.id, "data", 4) != 0) {
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

void Audio::Unload(uint32_t soundIndex) {
    SoundData &soundData = soundDatas_[soundIndex];
    std::string filename = soundData.name_;
    soundData.buffer.clear();
    soundData.wfex = {};
    soundData.name_.clear();
    ImGuiNotification::Post("音声ファイルをアンロードしました: " + filename, {0.9f, 0.7f, 0.2f, 1.0f});
}

void Audio::PlayWave(uint32_t soundIndex, float volume, bool loop) {
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

void Audio::StopWave(uint32_t soundIndex) {
    for (auto it = voices_.begin(); it != voices_.end();) {
        if ((*it)->handle == soundIndex) {
            if ((*it)->sourceVoice != nullptr) {
                (*it)->sourceVoice->Stop(0);
                (*it)->sourceVoice->DestroyVoice();
            }
            it = voices_.erase(it);
        } else {
            ++it;
        }
    }
}

void Audio::SetVolume(uint32_t soundIndex, float volume) {
    for (auto &voice : voices_) {
        if (voice->handle == soundIndex) {
            voice->volume = volume;
            voice->sourceVoice->SetVolume(volume);
            break;
        }
    }
}

void Audio::CleanupFinishedVoices() {
    for (auto it = voices_.begin(); it != voices_.end();) {
        if ((*it)->sourceVoice == nullptr) {
            it = voices_.erase(it);
        } else {
            ++it;
        }
    }
}

void Audio::Finalize() {
    if (masterVoice_) {
        masterVoice_->DestroyVoice();
        masterVoice_ = nullptr;
    }

    for (auto &voice : voices_) {
        if (voice->sourceVoice) {
            voice->sourceVoice->DestroyVoice();
        }
    }

    if (xAudio2_) {
        xAudio2_.Reset();
    }

    voices_.clear();
}

//==============================================================================
// デバッグ補助関数
//==============================================================================

void Audio::DebugScanWavFiles() {
    debugWavFileList_.clear();

    // ソリューション直下の Resources\sounds\ を走査
    // 実行ファイルがソリューション直下に置かれている前提
    std::filesystem::path dir("Resources/sounds");

    if (!std::filesystem::exists(dir)) {
        return;
    }

    for (auto &entry : std::filesystem::recursive_directory_iterator(dir)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        if (entry.path().extension() != ".wav") {
            continue;
        }

        // directoryPath_ からの相対パスをファイル名として登録
        auto rel = std::filesystem::relative(entry.path(), dir);
        debugWavFileList_.push_back(rel.string());
    }
}

float Audio::DebugGetDurationSec(uint32_t index) const {
    const SoundData &sd = soundDatas_[index];
    if (sd.wfex.nAvgBytesPerSec == 0) {
        return 0.0f;
    }
    return static_cast<float>(sd.buffer.size()) /
           static_cast<float>(sd.wfex.nAvgBytesPerSec);
}

float Audio::DebugGetPositionSec(uint32_t index) const {
    for (auto &voice : voices_) {
        if (voice->handle != index) {
            continue;
        }
        if (!voice->sourceVoice) {
            continue;
        }

        XAUDIO2_VOICE_STATE state{};
        voice->sourceVoice->GetState(&state);

        const SoundData &sd = soundDatas_[index];
        if (sd.wfex.nAvgBytesPerSec == 0) {
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

bool Audio::DebugIsPlaying(uint32_t index) const {
    for (auto &voice : voices_) {
        if (voice->handle == index) {
            return true;
        }
    }
    return false;
}

uint32_t Audio::DebugResolveIndex(const std::string &filename) const {
    auto it = debugLoadedMap_.find(filename);
    if (it != debugLoadedMap_.end()) {
        return it->second;
    }
    return UINT32_MAX;
}

//==============================================================================
// Debug() 本体
//==============================================================================

void Audio::Debug() {
#ifdef _DEBUG
    // ── マスター音量 ──
    SectionHeader("[ マスター ]", DebugTheme::kAccentBlue);
    ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
    ImGui::TextUnformatted("マスター音量");
    ImGui::PopStyleColor();
    ImGui::SetNextItemWidth(-1);
    ImGui::PushStyleColor(ImGuiCol_SliderGrab, DebugTheme::kAccentBlue);
    ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, DebugTheme::kAccentBlue);
    if (ImGui::SliderFloat("##master", &debugMasterVolume_, 0.0f, 1.0f, "%.2f")) {
        if (masterVoice_) {
            masterVoice_->SetVolume(debugMasterVolume_);
        }
    }
    ImGui::PopStyleColor(2);

    ImGui::Spacing();

    // ── ファイルブラウザ ──
    SectionHeader("[ ファイルブラウザ  Resources\\sounds\\ ]", DebugTheme::kAccentGreen);
    ImGui::PushStyleColor(ImGuiCol_Button, DebugTheme::kBgGreen);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.45f, 0.68f, 0.52f, 0.40f));
    if (ImGui::Button("WAVファイルをスキャン")) {
        DebugScanWavFiles();
        debugSelectedFile_ = -1;
        ImGuiNotification::Post("WAVを " + std::to_string(debugWavFileList_.size()) + " 件検出しました",
                                {0.45f, 0.68f, 0.52f, 1.0f});
    }
    ImGui::PopStyleColor(2);
    ImGui::SameLine();
    ImGui::TextDisabled("(%zu ファイル)", debugWavFileList_.size());

    ImGui::BeginChild("##filelist", ImVec2(0, 150), ImGuiChildFlags_Borders);
    for (int i = 0; i < static_cast<int>(debugWavFileList_.size()); ++i) {
        const bool selected = (debugSelectedFile_ == i);
        if (ImGui::Selectable(debugWavFileList_[i].c_str(), selected)) {
            debugSelectedFile_ = i;
        }
    }
    ImGui::EndChild();

    ImGui::Spacing();

    // ── 再生コントロール ──
    SectionHeader("[ 再生コントロール ]", DebugTheme::kAccentOrange);

    const bool hasSelection = (debugSelectedFile_ >= 0 &&
                               debugSelectedFile_ < static_cast<int>(debugWavFileList_.size()));

    if (!hasSelection) {
        ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
        ImGui::TextUnformatted("上のリストからファイルを選択してください");
        ImGui::PopStyleColor();
    } else {
        const std::string &selectedName = debugWavFileList_[debugSelectedFile_];

        uint32_t idx = DebugResolveIndex(selectedName);
        const bool loaded = (idx != UINT32_MAX);
        const bool playing = loaded && DebugIsPlaying(idx);

        // 名前 + 状態バッジ
        ImGui::AlignTextToFramePadding();
        ImGui::Text("%s", selectedName.c_str());
        ImGui::SameLine();
        if (loaded) {
            StatusBadge(playing ? "再生中" : "ロード済み",
                        playing ? DebugTheme::kAccentGreen : DebugTheme::kAccentCyan);
        } else {
            StatusBadge("未ロード", DebugTheme::kAccentOrange);
        }

        // 再生パラメータ
        ImGui::SetNextItemWidth(-1);
        ImGui::SliderFloat("##vol", &debugVolume_, 0.0f, 1.0f, "音量 %.2f");
        ImGui::Checkbox("ループ再生", &debugLoop_);

        ImGui::Spacing();

        if (!loaded) {
            //-- Load ボタン
            ImGui::PushStyleColor(ImGuiCol_Button, DebugTheme::kBgBlue);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.45f, 0.60f, 0.78f, 0.40f));
            if (ImGui::Button("ロード", ImVec2(120, 0))) {
                // デバッグ用に Resources/sounds から一時的にロードする
                std::string savedDir = directoryPath_;
                directoryPath_ = "Resources/sounds";
                uint32_t newIdx = LoadWave(selectedName);
                directoryPath_ = savedDir;
                debugLoadedMap_[selectedName] = newIdx;
                ImGuiNotification::Post("ロードしました: " + selectedName, {0.42f, 0.66f, 0.68f, 1.0f});
            }
            ImGui::PopStyleColor(2);
        } else {
            //-- Play ボタン
            ImGui::BeginDisabled(playing);
            if (ImGui::Button("再生")) {
                PlayWave(idx, debugVolume_, debugLoop_);
            }
            ImGui::EndDisabled();

            ImGui::SameLine();

            //-- Stop ボタン
            ImGui::BeginDisabled(!playing);
            if (ImGui::Button("停止")) {
                StopWave(idx);
            }
            ImGui::EndDisabled();

            ImGui::SameLine();

            //-- 再生中のみ音量を即時反映
            if (playing) {
                if (ImGui::Button("音量を適用")) {
                    SetVolume(idx, debugVolume_);
                }
                ImGui::SameLine();
            }

            //-- Unload ボタン
            ImGui::BeginDisabled(playing);
            ImGui::PushStyleColor(ImGuiCol_Button, DebugTheme::kBgRed);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.80f, 0.46f, 0.46f, 0.40f));
            if (ImGui::Button("アンロード")) {
                StopWave(idx);
                Unload(idx);
                debugLoadedMap_.erase(selectedName);
                loadedFiles_.erase(selectedName);
                ImGuiNotification::Post("アンロードしました: " + selectedName, {0.82f, 0.58f, 0.36f, 1.0f});
            }
            ImGui::PopStyleColor(2);
            ImGui::EndDisabled();

            //-- 再生時間バー
            if (loaded) {
                float duration = DebugGetDurationSec(idx);
                float position = playing ? DebugGetPositionSec(idx) : 0.0f;

                // ループ時は position が duration を超えることがあるのでクランプ
                if (duration > 0.0f) {
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
                if (duration > 0.0f) {
                    const SoundData &sd = soundDatas_[idx];
                    ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
                    ImGui::Text("ch:%u   %u Hz   %u bit   %u B/s",
                                sd.wfex.nChannels,
                                sd.wfex.nSamplesPerSec,
                                sd.wfex.wBitsPerSample,
                                sd.wfex.nAvgBytesPerSec);
                    ImGui::PopStyleColor();
                }
            }
        }
    }

    ImGui::Spacing();

    // ── ロード済みファイル一覧 ──
    SectionHeader("[ ロード済みファイル ]", DebugTheme::kAccentPurple);
    ImGui::BeginChild("##loadedlist", ImVec2(0, 120), ImGuiChildFlags_Borders);
    if (debugLoadedMap_.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
        ImGui::TextUnformatted("（ロード済みファイルはありません）");
        ImGui::PopStyleColor();
    }
    for (auto &[name, soundIdx] : debugLoadedMap_) {
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
    if (ImGui::Button("すべて停止", ImVec2(-1.0f, 0.0f))) {
        for (auto &[name, soundIdx] : debugLoadedMap_) {
            StopWave(soundIdx);
        }
        ImGuiNotification::Post("すべての再生を停止しました", {0.82f, 0.58f, 0.36f, 1.0f});
    }
    ImGui::PopStyleColor(2);
#endif // _DEBUG
}
} // namespace Hagine
