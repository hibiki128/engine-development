#include "MusicEditor.h"
#ifdef USE_IMGUI

#include <asset/AssetPath.h>
#include <debug/imgui/DebugUIHelper.h>
#include <debug/imgui/ImGuiNotification.h>
#include <icon/IconsFontAwesome5.h>

#include <algorithm>
#include <filesystem>

namespace Hagine {
namespace {

/// ノート番号を選ばせる小物（音名も一緒に出す）
bool NoteSlider(const char *label, int *note)
{
    const bool changed = ImGui::SliderInt(label, note, MusicConst::kLowestNote, MusicConst::kHighestNote,
                                          NoteName(*note).c_str());
    return changed;
}

} // namespace

void MusicEditor::DrawInstrumentPanel()
{
    MusicTrack *track = GetSelectedTrack();
    if (!track)
    {
        DimText("トラックがありません");
        return;
    }

    Instrument *instrument = track->GetInstrument();
    SectionHeader(ICON_FA_SLIDERS_H " 音色", DebugTheme::kAccentPurple);

    // 楽器の種類を差し替える（音色は初期状態に戻る）
    const char *kKindNames[] = {"シンセ", "ドラムマシン", "サンプラー (wav)"};
    int kind = static_cast<int>(instrument->GetKind());
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::Combo("##kind", &kind, kKindNames, IM_ARRAYSIZE(kKindNames)))
    {
        MusicEngine::ScopedLock lock;
        track->ReplaceInstrument(static_cast<InstrumentKind>(kind));
        selectedPad_ = 0;
        waveformDirty_ = true;
        instrument = track->GetInstrument();
    }

    switch (instrument->GetKind())
    {
    case InstrumentKind::Synth:
        DrawSynthPanel(static_cast<SynthInstrument *>(instrument));
        break;
    case InstrumentKind::DrumMachine:
        DrawDrumPanel(static_cast<DrumMachineInstrument *>(instrument));
        break;
    case InstrumentKind::Sampler:
        DrawSamplerPanel(static_cast<SamplerInstrument *>(instrument));
        break;
    }
}

void MusicEditor::DrawSynthPanel(SynthInstrument *synth)
{
    SynthParams &params = synth->GetParams();

    // プリセット
    static int presetIndex = 0;
    ImGui::SetNextItemWidth(-90.0f);
    if (ImGui::BeginCombo("##preset", SynthInstrument::GetPresetName(presetIndex)))
    {
        for (int i = 0; i < SynthInstrument::GetPresetCount(); ++i)
        {
            if (ImGui::Selectable(SynthInstrument::GetPresetName(i), presetIndex == i))
                presetIndex = i;
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (PrimaryButton("適用", ImVec2(-1.0f, 0.0f)))
    {
        MusicEngine::ScopedLock lock;
        synth->ApplyPreset(presetIndex);
    }

    if (ThemedHeader("オシレータ", DebugTheme::kAccentBlue, true))
    {
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::BeginCombo("##waveform", SynthInstrument::GetWaveformName(params.waveform)))
        {
            for (int i = 0; i < static_cast<int>(SynthWaveform::Count); ++i)
            {
                if (ImGui::Selectable(SynthInstrument::GetWaveformName(i), params.waveform == i))
                    params.waveform = i;
            }
            ImGui::EndCombo();
        }
        if (params.waveform == static_cast<int>(SynthWaveform::Pulse))
        {
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::SliderFloat("##pulseWidth", &params.pulseWidth, 0.05f, 0.95f, "パルス幅 %.2f");
        }

        ImGui::SetNextItemWidth(-1.0f);
        ImGui::SliderFloat("##osc2Mix", &params.osc2Mix, 0.0f, 1.0f, "2つ目を混ぜる %.2f");
        if (params.osc2Mix > 0.0f)
        {
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::SliderInt("##osc2Semi", &params.osc2Semitone, -24, 24, "音程差 %d 半音");
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::SliderFloat("##osc2Detune", &params.osc2Detune, -50.0f, 50.0f, "ずらし %.1f セント");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("少しずらすと音に厚みが出ます");
        }
    }

    if (ThemedHeader("音の出方 (エンベロープ)", DebugTheme::kAccentGreen, true))
    {
        const float knobSize = std::max(44.0f, (ImGui::GetContentRegionAvail().x - 24.0f) / 4.0f);
        ThemedKnob("A", &params.attack, 0.0005f, 2.0f, "%.3fs", DebugTheme::kAccentGreen, knobSize);
        ImGui::SameLine();
        ThemedKnob("D", &params.decay, 0.01f, 6.0f, "%.2fs", DebugTheme::kAccentGreen, knobSize);
        ImGui::SameLine();
        ThemedKnob("S", &params.sustain, 0.0f, 1.0f, "%.2f", DebugTheme::kAccentGreen, knobSize);
        ImGui::SameLine();
        ThemedKnob("R", &params.release, 0.01f, 4.0f, "%.2fs", DebugTheme::kAccentGreen, knobSize);
        DimText("A=立ち上がり D=減衰 S=保持 R=余韻");
        if (params.sustain <= 0.001f)
            DimText("保持が0なので、押し続けても自然に消えます（ピアノ向き）");
    }

    if (ThemedHeader("音の明るさ (フィルター)", DebugTheme::kAccentOrange, true))
    {
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::SliderFloat("##cutoff", &params.cutoff, 60.0f, 18000.0f, "遮断 %.0f Hz",
                           ImGuiSliderFlags_Logarithmic);
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::SliderFloat("##resonance", &params.resonance, 0.0f, 0.95f, "くせ %.2f");
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::SliderFloat("##filterEnv", &params.filterEnvAmount, 0.0f, 1.0f, "打鍵時の明るさ %.2f");
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::SliderFloat("##filterDecay", &params.filterDecay, 0.02f, 4.0f, "明るさの戻り %.2fs");
    }

    if (ThemedHeader("出力", DebugTheme::kAccentCyan, true))
    {
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::SliderFloat("##gain", &params.gain, 0.0f, 1.5f, "音量 %.2f");
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::SliderFloat("##pan", &params.pan, -1.0f, 1.0f, "定位 %.2f");
        AccentCheckbox("強さを音量へ反映", &params.velocityToVolume, DebugTheme::kAccentCyan);
    }

    if (NeutralButton(ICON_FA_PLAY " 試しに鳴らす", ImVec2(-1.0f, 0.0f)))
        PreviewNote(60, playVelocity_);
}

void MusicEditor::DrawDrumPanel(DrumMachineInstrument *drum)
{
    std::vector<DrumPad> &pads = drum->GetPads();

    if (pads.empty())
        DimText("パッドがありません");

    // パッド一覧。押すとその音が鳴る
    for (int i = 0; i < static_cast<int>(pads.size()); ++i)
    {
        ImGui::PushID(i);
        const bool selected = (selectedPad_ == i);
        const ImVec4 accent = selected ? DebugTheme::kAccentOrange : DebugTheme::kButtonNeutral;
        if (AccentButton((pads[i].name + "##pad").c_str(), accent, -46.0f))
        {
            selectedPad_ = i;
            PreviewNote(pads[i].note, playVelocity_);
        }
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(NoteName(pads[i].note).c_str());
        ImGui::PopStyleColor();
        ImGui::PopID();
    }

    ImGui::Spacing();
    if (ConfirmButton(ICON_FA_PLUS " パッド追加", ImVec2(-1.0f, 0.0f)))
    {
        MusicEngine::ScopedLock lock;
        DrumPad pad;
        pad.name = "新規パッド";
        pad.note = pads.empty() ? 36 : (pads.back().note + 1);
        drum->AddPad(pad);
        selectedPad_ = static_cast<int>(pads.size()) - 1;
    }
    if (NeutralButton(ICON_FA_SYNC_ALT " 標準キットに戻す", ImVec2(-1.0f, 0.0f)))
    {
        MusicEngine::ScopedLock lock;
        drum->ResetToDefaultKit();
        selectedPad_ = 0;
    }

    if (selectedPad_ < 0 || selectedPad_ >= static_cast<int>(pads.size()))
        return;
    DrumPad &pad = pads[selectedPad_];

    ImGui::Separator();
    SectionHeader(pad.name.c_str(), DebugTheme::kAccentOrange);

    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputText("##padName", &pad.name);

    const char *kTypeNames[] = {"キック", "スネア", "ハイハット", "タム", "クラップ", "シンバル", "リム", "カウベル"};
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::Combo("##padType", &pad.type, kTypeNames, IM_ARRAYSIZE(kTypeNames));

    ImGui::SetNextItemWidth(-1.0f);
    NoteSlider("##padNote", &pad.note);
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::SliderFloat("##padTune", &pad.tune, 20.0f, 12000.0f, "高さ %.0f Hz", ImGuiSliderFlags_Logarithmic);
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::SliderFloat("##padDecay", &pad.decay, 0.01f, 3.0f, "長さ %.2fs");
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::SliderFloat("##padTone", &pad.tone, 0.0f, 1.0f, "明るさ %.2f");
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::SliderFloat("##padGain", &pad.gain, 0.0f, 1.5f, "音量 %.2f");
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::SliderFloat("##padPan", &pad.pan, -1.0f, 1.0f, "定位 %.2f");
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::SliderInt("##padChoke", &pad.chokeGroup, 0, 4, "同時に鳴らさない組 %d");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("同じ番号のパッド同士は重ならずに切り替わります（開/閉ハイハットなど）。0 で無効");

    if (NeutralButton(ICON_FA_PLAY " 鳴らす", ImVec2(-1.0f, 0.0f)))
        PreviewNote(pad.note, playVelocity_);
    if (DangerButton(ICON_FA_TRASH_ALT " このパッドを削除", ImVec2(-1.0f, 0.0f)))
    {
        MusicEngine::ScopedLock lock;
        drum->RemovePad(selectedPad_);
        selectedPad_ = std::max(0, selectedPad_ - 1);
    }
}

void MusicEditor::DrawSamplerPanel(SamplerInstrument *sampler)
{
    std::vector<SamplerSlot> &slots = sampler->GetSlots();

    int mode = static_cast<int>(sampler->GetMode());
    const char *kModeNames[] = {"ドラムキット（音ごとに別サンプル）", "音程つき（鍵盤で伸び縮み）"};
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::Combo("##samplerMode", &mode, kModeNames, IM_ARRAYSIZE(kModeNames)))
    {
        MusicEngine::ScopedLock lock;
        sampler->SetMode(static_cast<SamplerMode>(mode));
    }

    // 読み込み済みサンプル
    for (int i = 0; i < static_cast<int>(slots.size()); ++i)
    {
        ImGui::PushID(i);
        const bool selected = (selectedPad_ == i);
        const ImVec4 accent = selected ? DebugTheme::kAccentCyan : DebugTheme::kButtonNeutral;
        if (AccentButton((slots[i].name + "##slot").c_str(), accent, -46.0f))
        {
            selectedPad_ = i;
            waveformDirty_ = true;
            trimStartSeconds_ = 0.0f;
            trimEndSeconds_ = slots[i].clip.DurationSeconds();
            PreviewNote(slots[i].note, playVelocity_);
        }
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(NoteName(slots[i].note).c_str());
        ImGui::PopStyleColor();
        ImGui::PopID();
    }

    // 読み込み
    ImGui::Spacing();
    if (ThemedHeader(ICON_FA_FOLDER_OPEN " wav を読み込む", DebugTheme::kAccentGreen, slots.empty()))
    {
        std::string picked;
        if (DrawWavPicker("##wavPicker", picked))
        {
            MusicEngine::ScopedLock lock;
            std::string error;
            const int index = sampler->LoadSample(picked, &error);
            if (index >= 0)
            {
                selectedPad_ = index;
                waveformDirty_ = true;
                trimStartSeconds_ = 0.0f;
                trimEndSeconds_ = slots[index].clip.DurationSeconds();
                ImGuiNotification::Post("読み込みました: " + slots[index].name);
            }
            else
            {
                ImGuiNotification::Post(error, {0.9f, 0.4f, 0.4f, 1.0f});
            }
        }
    }

    if (selectedPad_ < 0 || selectedPad_ >= static_cast<int>(slots.size()))
    {
        DimText("sounds フォルダの .wav を読み込むとここに並びます");
        return;
    }
    SamplerSlot &slot = slots[selectedPad_];

    ImGui::Separator();
    SectionHeader(slot.name.c_str(), DebugTheme::kAccentCyan);

    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputText("##slotName", &slot.name);

    ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
    ImGui::Text("%.2f 秒 / %u ch / %u Hz", slot.clip.DurationSeconds(), slot.clip.channels,
                slot.clip.sampleRate);
    ImGui::PopStyleColor();

    DrawWaveformPreview("##slotWave", slot.clip, 60.0f);

    ImGui::SetNextItemWidth(-1.0f);
    NoteSlider("##slotNote", &slot.note);
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip(sampler->GetMode() == SamplerMode::DrumKit
                              ? "このサンプルを鳴らす鍵盤の位置"
                              : "このサンプル本来の高さ。ここを基準に鍵盤で伸び縮みします");
    }
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::SliderFloat("##slotGain", &slot.gain, 0.0f, 2.0f, "音量 %.2f");
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::SliderFloat("##slotPan", &slot.pan, -1.0f, 1.0f, "定位 %.2f");
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::SliderFloat("##slotRelease", &slot.release, 0.005f, 2.0f, "余韻 %.3fs");
    AccentCheckbox("繰り返す", &slot.loop, DebugTheme::kAccentCyan);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(110.0f);
    ImGui::SliderInt("##slotChoke", &slot.chokeGroup, 0, 4, "排他 %d");

    if (NeutralButton(ICON_FA_PLAY " 鳴らす", ImVec2(-1.0f, 0.0f)))
        PreviewNote(slot.note, playVelocity_);

    // --- 波形の加工 ---
    if (ThemedHeader(ICON_FA_MAGIC " 波形を加工する", DebugTheme::kAccentYellow, false))
    {
        DimText("加工は読み込んだデータに対して行われます（元ファイルは変わりません）");

        if (PrimaryButton("音量をそろえる（正規化）", ImVec2(-1.0f, 0.0f)))
        {
            MusicEngine::ScopedLock lock;
            WavFile::Normalize(slot.clip);
            waveformDirty_ = true;
        }
        if (PrimaryButton("逆再生にする", ImVec2(-1.0f, 0.0f)))
        {
            MusicEngine::ScopedLock lock;
            WavFile::Reverse(slot.clip);
            waveformDirty_ = true;
        }
        if (PrimaryButton("前後の無音を削る", ImVec2(-1.0f, 0.0f)))
        {
            MusicEngine::ScopedLock lock;
            WavFile::TrimSilence(slot.clip);
            waveformDirty_ = true;
            trimStartSeconds_ = 0.0f;
            trimEndSeconds_ = slot.clip.DurationSeconds();
        }

        ImGui::Separator();
        ImGui::SetNextItemWidth(-70.0f);
        ImGui::SliderFloat("##gainValue", &sampleGain_, 0.1f, 4.0f, "倍率 %.2f");
        ImGui::SameLine();
        if (PrimaryButton("適用##gain", ImVec2(-1.0f, 0.0f)))
        {
            MusicEngine::ScopedLock lock;
            WavFile::ApplyGain(slot.clip, sampleGain_);
            waveformDirty_ = true;
        }

        const float duration = slot.clip.DurationSeconds();
        ImGui::SetNextItemWidth(-70.0f);
        ImGui::SliderFloat("##fadeIn", &fadeInSeconds_, 0.0f, std::max(0.01f, duration), "入り %.3fs");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::SliderFloat("##fadeOut", &fadeOutSeconds_, 0.0f, std::max(0.01f, duration), "出 %.3fs");
        if (PrimaryButton("フェードを掛ける", ImVec2(-1.0f, 0.0f)))
        {
            MusicEngine::ScopedLock lock;
            WavFile::Fade(slot.clip, fadeInSeconds_, fadeOutSeconds_);
            waveformDirty_ = true;
        }

        ImGui::Separator();
        trimEndSeconds_ = std::clamp(trimEndSeconds_, 0.0f, std::max(0.01f, duration));
        trimStartSeconds_ = std::clamp(trimStartSeconds_, 0.0f, trimEndSeconds_);
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::SliderFloat("##trimStart", &trimStartSeconds_, 0.0f, std::max(0.01f, duration), "切り出し開始 %.3fs");
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::SliderFloat("##trimEnd", &trimEndSeconds_, 0.0f, std::max(0.01f, duration), "切り出し終了 %.3fs");
        if (PrimaryButton(ICON_FA_CUT " この範囲だけ残す", ImVec2(-1.0f, 0.0f)))
        {
            MusicEngine::ScopedLock lock;
            const uint32_t rate = slot.clip.sampleRate;
            WavFile::Trim(slot.clip, static_cast<uint32_t>(trimStartSeconds_ * rate),
                          static_cast<uint32_t>(trimEndSeconds_ * rate));
            waveformDirty_ = true;
            trimStartSeconds_ = 0.0f;
            trimEndSeconds_ = slot.clip.DurationSeconds();
        }

        ImGui::Separator();
        if (NeutralButton(ICON_FA_SYNC_ALT " 元のファイルから読み直す", ImVec2(-1.0f, 0.0f)))
        {
            MusicEngine::ScopedLock lock;
            std::string error;
            if (!sampler->ReplaceSample(selectedPad_, slot.filePath, &error))
                ImGuiNotification::Post(error, {0.9f, 0.4f, 0.4f, 1.0f});
            waveformDirty_ = true;
        }
        if (ConfirmButton(ICON_FA_SAVE " 加工結果を wav で保存", ImVec2(-1.0f, 0.0f)))
        {
            const std::string path = AssetPath::SoundRoot() + "/music/" + slot.name + "_edit.wav";
            std::string error;
            if (WavFile::Save(path, slot.clip, 16, &error))
            {
                ImGuiNotification::Post("保存しました: " + path);
                wavScanned_ = false;
            }
            else
            {
                ImGuiNotification::Post(error, {0.9f, 0.4f, 0.4f, 1.0f});
            }
        }
    }

    if (DangerButton(ICON_FA_TRASH_ALT " このサンプルを外す", ImVec2(-1.0f, 0.0f)))
    {
        MusicEngine::ScopedLock lock;
        sampler->RemoveSlot(selectedPad_);
        selectedPad_ = std::max(0, selectedPad_ - 1);
        waveformDirty_ = true;
    }
}

void MusicEditor::DrawWaveformPreview(const char *id, const AudioClip &clip, float height)
{
    const ImVec2 size(ImGui::GetContentRegionAvail().x, height);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton(id, size);

    ImDrawList *drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y),
                            IM_COL32(26, 27, 32, 255), 3.0f);

    if (clip.Empty())
    {
        drawList->AddText(ImVec2(origin.x + 6.0f, origin.y + size.y * 0.5f - 8.0f),
                          IM_COL32(120, 120, 130, 255), "（波形なし）");
        return;
    }

    // 中身が変わったときだけ折れ線を作り直す
    const int bucketCount = std::max(16, static_cast<int>(size.x));
    if (waveformDirty_ || waveformOwner_ != static_cast<const void *>(&clip) ||
        waveformSize_ != clip.samples.size() || static_cast<int>(waveformMin_.size()) != bucketCount)
    {
        WavFile::BuildEnvelope(clip, bucketCount, waveformMin_, waveformMax_);
        waveformOwner_ = static_cast<const void *>(&clip);
        waveformSize_ = clip.samples.size();
        waveformDirty_ = false;
    }

    const float centerY = origin.y + size.y * 0.5f;
    const float scale = size.y * 0.46f;
    drawList->AddLine(ImVec2(origin.x, centerY), ImVec2(origin.x + size.x, centerY),
                      IM_COL32(60, 62, 70, 255));
    for (int i = 0; i < static_cast<int>(waveformMin_.size()); ++i)
    {
        const float x = origin.x + static_cast<float>(i) * size.x / static_cast<float>(waveformMin_.size());
        drawList->AddLine(ImVec2(x, centerY - waveformMax_[i] * scale),
                          ImVec2(x, centerY - waveformMin_[i] * scale), IM_COL32(110, 175, 200, 220));
    }
}

void MusicEditor::RescanWavFiles()
{
    wavFiles_.clear();
    std::error_code ec;
    const std::string root = AssetPath::SoundRoot();
    if (!std::filesystem::exists(root, ec))
    {
        wavScanned_ = true;
        return;
    }
    for (const auto &entry : std::filesystem::recursive_directory_iterator(root, ec))
    {
        if (ec)
            break;
        if (!entry.is_regular_file())
            continue;
        std::string extension = entry.path().extension().string();
        for (char &ch : extension)
            ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        if (extension == ".wav")
            wavFiles_.push_back(entry.path().generic_string());
    }
    std::sort(wavFiles_.begin(), wavFiles_.end());
    wavScanned_ = true;
}

bool MusicEditor::DrawWavPicker(const char *id, std::string &outPath)
{
    if (!wavScanned_)
        RescanWavFiles();

    ImGui::PushID(id);
    ImGui::SetNextItemWidth(-70.0f);
    ImGui::InputTextWithHint("##filter", "絞り込み", &wavFilterText_);
    ImGui::SameLine();
    if (NeutralButton(ICON_FA_SYNC_ALT "##rescan", ImVec2(-1.0f, 0.0f)))
        RescanWavFiles();

    bool picked = false;
    ImGui::BeginChild("##wavList", ImVec2(0.0f, 120.0f), ImGuiChildFlags_Borders);
    const std::string root = AssetPath::SoundRoot() + "/";
    for (const std::string &path : wavFiles_)
    {
        // 一覧は sounds ルートからの相対で見せる（フルパスは長すぎて読めない）
        std::string display = path;
        if (display.rfind(root, 0) == 0)
            display = display.substr(root.size());
        if (!wavFilterText_.empty() && display.find(wavFilterText_) == std::string::npos)
            continue;
        if (ImGui::Selectable(display.c_str()))
        {
            outPath = path;
            picked = true;
        }
    }
    if (wavFiles_.empty())
        DimText("sounds フォルダに .wav が見つかりません");
    ImGui::EndChild();
    ImGui::PopID();
    return picked;
}

} // namespace Hagine
#endif // USE_IMGUI
