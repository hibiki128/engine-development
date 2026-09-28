#include "MusicEditor.h"
#ifdef USE_IMGUI

#include <debug/imgui/DebugUIHelper.h>
#include <debug/imgui/ImGuiNotification.h>
#include <icon/IconsFontAwesome5.h>

#include <algorithm>

namespace Hagine {

void MusicEditor::DrawCompositionAssist()
{
    SectionHeader(ICON_FA_MAGIC " 作曲アシスト", DebugTheme::kAccentYellow);

    // --- キーとスケール ---
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::BeginCombo("##scaleRoot", (std::string("キー: ") + MusicTheory::GetRootName(scaleRoot_)).c_str()))
    {
        for (int i = 0; i < 12; ++i)
        {
            if (ImGui::Selectable(MusicTheory::GetRootName(i), scaleRoot_ == i))
                scaleRoot_ = i;
        }
        ImGui::EndCombo();
    }
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::BeginCombo("##scaleType", MusicTheory::GetScaleName(scaleType_)))
    {
        for (int i = 0; i < MusicTheory::GetScaleCount(); ++i)
        {
            if (ImGui::Selectable(MusicTheory::GetScaleName(i), scaleType_ == i))
                scaleType_ = i;
        }
        ImGui::EndCombo();
    }
    AccentCheckbox("使える音を色分けする", &highlightScale_, DebugTheme::kAccentYellow);
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("ピアノロールで、そのキーに合う音の行を明るく表示します。\n"
                          "明るい行だけを使えば、音楽の知識が無くても外れません");
    }

    ImGui::Spacing();

    // --- コード入力 ---
    AccentCheckbox("クリックで和音を置く", &chordMode_, DebugTheme::kAccentPurple);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("ONにすると、ピアノロールを1回クリックするだけで和音が3〜4音まとめて置かれます");
    if (chordMode_)
    {
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::SliderInt("##chordNotes", &chordNoteCount_, 2, 5, "重ねる音 %d つ");
        DimText("3 で普通の和音、4 でおしゃれな響き（セブンス）");
    }

    ImGui::Spacing();

    // --- コード進行の流し込み ---
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::BeginCombo("##progression", MusicTheory::GetProgressionName(progressionIndex_)))
    {
        for (int i = 0; i < MusicTheory::GetProgressionCount(); ++i)
        {
            if (ImGui::Selectable(MusicTheory::GetProgressionName(i), progressionIndex_ == i))
                progressionIndex_ = i;
        }
        ImGui::EndCombo();
    }
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::SliderInt("##progOctave", &progressionOctave_, 1, 6, "高さ（オクターブ）%d");

    // 選んだ進行がどんな和音の並びになるかを、書き込む前に見せる
    {
        const std::vector<int> &degrees = MusicTheory::GetProgressionDegrees(progressionIndex_);
        std::string preview;
        for (size_t i = 0; i < degrees.size() && i < 8; ++i)
        {
            const std::vector<int> chord =
                MusicTheory::BuildChordFromDegree(degrees[i], progressionOctave_, scaleRoot_, scaleType_, 3);
            preview += MusicTheory::GetChordName(chord);
            if (i + 1 < degrees.size())
                preview += " - ";
        }
        if (degrees.size() > 8)
            preview += "...";
        DimText(preview.c_str());
    }

    if (ConfirmButton(ICON_FA_PLUS " 再生位置へ書き込む", ImVec2(-1.0f, 0.0f)))
        ApplyProgression(SnapToGrid(MusicEngine::GetInstance()->GetPositionTicks()));
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("選択中のトラックへ、1小節ずつ和音を並べます");
}

void MusicEditor::ApplyProgression(int startTick)
{
    MusicTrack *track = GetSelectedTrack();
    if (!track)
        return;

    MusicEngine *engine = MusicEngine::GetInstance();
    const std::vector<int> &degrees = MusicTheory::GetProgressionDegrees(progressionIndex_);
    if (degrees.empty())
        return;

    const int barTicks = std::max(1, engine->GetBeatsPerBar()) * MusicConst::kTicksPerBeat;
    startTick = std::max(0, startTick);

    MusicEngine::ScopedLock lock;
    selectedNotes_.clear();
    for (size_t i = 0; i < degrees.size(); ++i)
    {
        const std::vector<int> chord =
            MusicTheory::BuildChordFromDegree(degrees[i], progressionOctave_, scaleRoot_, scaleType_, chordNoteCount_);
        for (int noteNumber : chord)
        {
            MusicNote note;
            note.note = noteNumber;
            note.startTick = startTick + static_cast<int>(i) * barTicks;
            // 次の和音と重ならないよう、ほんの少し手前で切る
            note.lengthTick = barTicks - 6;
            note.velocity = noteVelocity_;
            selectedNotes_.insert(track->AddNote(note));
        }
    }

    ImGuiNotification::Post(std::string("コード進行を書き込みました: ") +
                            MusicTheory::GetProgressionName(progressionIndex_));
}

void MusicEditor::DrawEffectsPanel()
{
    MusicEngine *engine = MusicEngine::GetInstance();

    SectionHeader(ICON_FA_WAVE_SQUARE " エフェクト", DebugTheme::kAccentCyan);

    // --- やまびこ ---
    DelayParams &delay = engine->GetDelayParams();
    if (ToggleRow("やまびこ (ディレイ)", "##delayEnabled", &delay.enabled, DebugTheme::kAccentCyan))
    {
    }
    if (delay.enabled)
    {
        const char *kDivisionNames[] = {"4分", "付点8分", "8分", "16分"};
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::Combo("##delayDivision", &delay.noteDivision, kDivisionNames, IM_ARRAYSIZE(kDivisionNames));
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::SliderFloat("##delayFeedback", &delay.feedback, 0.0f, 0.95f, "繰り返し %.2f");
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::SliderFloat("##delayMix", &delay.mix, 0.0f, 1.0f, "混ぜる量 %.2f");
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::SliderFloat("##delayPingPong", &delay.pingPong, 0.0f, 1.0f, "左右に振る %.2f");
    }

    ImGui::Spacing();

    // --- 残響 ---
    ReverbParams &reverb = engine->GetReverbParams();
    ToggleRow("残響 (リバーブ)", "##reverbEnabled", &reverb.enabled, DebugTheme::kAccentBlue);
    if (reverb.enabled)
    {
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::SliderFloat("##reverbRoom", &reverb.roomSize, 0.0f, 1.0f, "空間の広さ %.2f");
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::SliderFloat("##reverbDamping", &reverb.damping, 0.0f, 1.0f, "吸われ方 %.2f");
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::SliderFloat("##reverbWidth", &reverb.width, 0.0f, 1.0f, "広がり %.2f");
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::SliderFloat("##reverbMix", &reverb.mix, 0.0f, 1.0f, "混ぜる量 %.2f");
    }

    ImGui::Spacing();

    // --- アルペジエータ ---
    SectionHeader(ICON_FA_SORT " アルペジオ", DebugTheme::kAccentGreen);
    ArpeggiatorParams &arp = engine->GetArpeggiatorParams();
    if (ToggleRow("和音を1音ずつ刻む", "##arpEnabled", &arp.enabled, DebugTheme::kAccentGreen))
    {
        // 切り替えた瞬間に音が残らないよう、いったん全部止める
        engine->AllNotesOff();
        ReleaseAllHeldNotes();
    }
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("ONのあいだ、鍵盤で和音を押さえると自動で1音ずつ順番に鳴ります。\n"
                          "押さえたままにするだけで伴奏らしい動きが作れます");
    }
    if (arp.enabled)
    {
        const char *kModeNames[] = {"上へ", "下へ", "上下", "ばらばら"};
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::Combo("##arpMode", &arp.mode, kModeNames, IM_ARRAYSIZE(kModeNames));

        const char *kRateNames[] = {"4分", "8分", "3連8分", "16分", "32分"};
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::Combo("##arpRate", &arp.rateDivision, kRateNames, IM_ARRAYSIZE(kRateNames));

        ImGui::SetNextItemWidth(-1.0f);
        ImGui::SliderInt("##arpOctave", &arp.octaveRange, 1, 3, "%d オクターブ繰り返す");
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::SliderFloat("##arpGate", &arp.gateRatio, 0.1f, 1.0f, "1音の長さ %.2f");

        ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
        ImGui::Text("押さえている音: %d", engine->GetArpeggiatorHeldCount());
        ImGui::PopStyleColor();
    }
}

} // namespace Hagine
#endif // USE_IMGUI
