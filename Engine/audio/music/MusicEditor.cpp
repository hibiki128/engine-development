#include "MusicEditor.h"
#ifdef USE_IMGUI

#include <Frame.h>
#include <Input.h>
#include <asset/AssetPath.h>
#include <debug/imgui/DebugUIHelper.h>
#include <debug/imgui/ImGuiNotification.h>
#include <icon/IconsFontAwesome5.h>

#include <algorithm>
#include <filesystem>

namespace Hagine {
namespace {

/// PCキーボードの1キーと、基準音からの音程差の対応
struct KeyNoteMap
{
    ImGuiKey key;
    int semitone;
};

// 下段（Z列）が基準オクターブ、上段（Q列）がその1つ上。
// 一般的な打ち込みソフトと同じ並びにしてあるので、他で慣れていればそのまま弾ける
constexpr KeyNoteMap kKeyMap[] = {
    {ImGuiKey_Z, 0}, {ImGuiKey_S, 1}, {ImGuiKey_X, 2}, {ImGuiKey_D, 3}, {ImGuiKey_C, 4},
    {ImGuiKey_V, 5}, {ImGuiKey_G, 6}, {ImGuiKey_B, 7}, {ImGuiKey_H, 8}, {ImGuiKey_N, 9},
    {ImGuiKey_J, 10}, {ImGuiKey_M, 11}, {ImGuiKey_Comma, 12}, {ImGuiKey_L, 13}, {ImGuiKey_Period, 14},
    {ImGuiKey_Q, 12}, {ImGuiKey_2, 13}, {ImGuiKey_W, 14}, {ImGuiKey_3, 15}, {ImGuiKey_E, 16},
    {ImGuiKey_R, 17}, {ImGuiKey_5, 18}, {ImGuiKey_T, 19}, {ImGuiKey_6, 20}, {ImGuiKey_Y, 21},
    {ImGuiKey_7, 22}, {ImGuiKey_U, 23}, {ImGuiKey_I, 24}, {ImGuiKey_9, 25}, {ImGuiKey_O, 26},
    {ImGuiKey_0, 27}, {ImGuiKey_P, 28},
};

/// プロジェクトJSONの置き場（jsons/music）
std::string ProjectDirectory()
{
    return AssetPath::Json("music");
}

/// 書き出した .wav の置き場（sounds/music）
std::string ExportDirectory()
{
    return AssetPath::SoundRoot() + "/music";
}

/// ティック位置を「小節:拍:ティック」の文字列にする
std::string FormatPosition(double tick, int beatsPerBar)
{
    const int totalTicks = static_cast<int>(std::max(0.0, tick));
    const int beat = totalTicks / MusicConst::kTicksPerBeat;
    const int bar = (beatsPerBar > 0) ? (beat / beatsPerBar) : 0;
    const int beatInBar = (beatsPerBar > 0) ? (beat % beatsPerBar) : 0;
    const int rest = totalTicks % MusicConst::kTicksPerBeat;
    char buffer[64];
    snprintf(buffer, sizeof(buffer), "%d : %d : %03d", bar + 1, beatInBar + 1, rest);
    return buffer;
}

} // namespace

MusicEditor *MusicEditor::GetInstance()
{
    static MusicEditor instance;
    return &instance;
}

MusicTrack *MusicEditor::GetSelectedTrack() const
{
    return MusicEngine::GetInstance()->GetTrack(selectedTrack_);
}

void MusicEditor::Draw(bool *open)
{
    if (open && !*open)
    {
        // 閉じているあいだはゲーム側へキーボードを返す
        if (keyboardCaptured_)
        {
            ReleaseAllHeldNotes();
            Input::GetInstance()->SetKeyboardSuspended(false);
            keyboardCaptured_ = false;
        }
        return;
    }

    MusicEngine *engine = MusicEngine::GetInstance();
    engine->EnsurePlaybackStarted();

    ImGui::SetNextWindowSize(ImVec2(1180.0f, 720.0f), ImGuiCond_FirstUseEver);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoFocusOnAppearing;
    if (!ImGui::Begin(ICON_FA_MUSIC " 音楽エディタ", open, flags))
    {
        ImGui::End();
        if (keyboardCaptured_)
        {
            ReleaseAllHeldNotes();
            Input::GetInstance()->SetKeyboardSuspended(false);
            keyboardCaptured_ = false;
        }
        return;
    }

    // トラック番号がずれていたら直す（トラックを消した直後など）
    if (selectedTrack_ >= engine->GetTrackCount())
        selectedTrack_ = std::max(0, engine->GetTrackCount() - 1);

    DrawMenuBar();
    DrawTransport();
    ImGui::Separator();

    const float keyboardHeight = 92.0f;
    const float leftWidth = 320.0f;

    ImGui::BeginChild("##musicLeft", ImVec2(leftWidth, 0.0f), ImGuiChildFlags_Borders);
    DrawTrackList();
    ImGui::Separator();
    DrawInstrumentPanel();
    ImGui::Separator();
    DrawCompositionAssist();
    ImGui::Separator();
    DrawEffectsPanel();
    ImGui::EndChild();

    ImGui::SameLine();

    ImGui::BeginChild("##musicRight", ImVec2(0.0f, 0.0f));
    {
        const float rollHeight = std::max(160.0f, ImGui::GetContentRegionAvail().y - keyboardHeight -
                                                      ImGui::GetStyle().ItemSpacing.y);
        ImGui::BeginChild("##rollArea", ImVec2(0.0f, rollHeight));
        DrawPianoRoll();
        ImGui::EndChild();
        DrawKeyboardBar();
    }
    ImGui::EndChild();

    // 弾く操作は、このウィンドウにフォーカスがあるときだけ受け付ける
    const bool windowFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    const bool wantKeyboard = pcKeyboardEnabled_ && windowFocused && !ImGui::GetIO().WantTextInput;
    if (wantKeyboard)
        HandlePcKeyboard();
    else
        ReleaseAllHeldNotes();

    if (wantKeyboard != keyboardCaptured_)
    {
        Input::GetInstance()->SetKeyboardSuspended(wantKeyboard);
        keyboardCaptured_ = wantKeyboard;
    }

    UpdatePreviewNotes();
    ImGui::End();
}

void MusicEditor::DrawMenuBar()
{
    MusicEngine *engine = MusicEngine::GetInstance();

    if (!ImGui::BeginMenuBar())
        return;

    if (ImGui::BeginMenu(ICON_FA_FILE " ファイル"))
    {
        if (ImGui::MenuItem(ICON_FA_FILE " 新規作成"))
        {
            engine->NewProject();
            selectedTrack_ = 0;
            selectedNotes_.clear();
            statusMessage_ = "新しい曲を作成しました";
            statusTimer_ = 3.0f;
        }
        ImGui::Separator();
        ImGui::MenuItem("保存 / 読み込みは下の欄から", nullptr, false, false);
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu(ICON_FA_EDIT " 編集"))
    {
        if (ImGui::MenuItem(ICON_FA_TRASH_ALT " 選択ノートを削除", "Delete", false, !selectedNotes_.empty()))
            DeleteSelectedNotes();
        if (ImGui::MenuItem("このトラックのノートを全消し"))
        {
            MusicEngine::ScopedLock lock;
            if (MusicTrack *track = GetSelectedTrack())
                track->ClearNotes();
            selectedNotes_.clear();
        }
        ImGui::Separator();
        if (ImGui::MenuItem(ICON_FA_VOLUME_MUTE " 鳴っている音を全部止める"))
            engine->AllNotesOff();
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu(ICON_FA_EYE " 表示"))
    {
        ImGui::Checkbox("他トラックのノートも薄く表示", &showOtherTracks_);
        ImGui::Checkbox("再生位置に合わせてスクロール", &followPlayhead_);
        ImGui::SetNextItemWidth(150.0f);
        ImGui::SliderFloat("横の拡大", &pixelsPerTick_, 0.12f, 2.5f, "%.2f");
        ImGui::SetNextItemWidth(150.0f);
        ImGui::SliderFloat("鍵盤の高さ", &rowHeight_, 8.0f, 28.0f, "%.0f px");
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu(ICON_FA_QUESTION_CIRCLE " 使い方"))
    {
        ImGui::TextUnformatted("鍵盤を弾く");
        ImGui::BulletText("下の鍵盤をマウスでクリック（押しっぱなしで伸びる）");
        ImGui::BulletText("PCキーボード: Z S X D C V G B H N J M = ド〜シ");
        ImGui::BulletText("            : Q 2 W 3 E R 5 T 6 Y 7 U = 1オクターブ上");
        ImGui::BulletText("オクターブの上げ下げは ↑ / ↓");
        ImGui::BulletText("スペースで再生 / 一時停止");
        ImGui::Separator();
        ImGui::TextUnformatted("ピアノロール");
        ImGui::BulletText("左クリック: ノートを置く / そのままドラッグで長さ指定");
        ImGui::BulletText("ノートを左ドラッグ: 移動（右端をつまむと長さ変更）");
        ImGui::BulletText("右クリック: ノート削除 / 何もない所から右ドラッグで範囲選択");
        ImGui::BulletText("Ctrl+クリック: 選択に追加 / Delete: 選択を削除");
        ImGui::BulletText("上の目盛りをクリック: 再生位置 / 右ドラッグ: ループ範囲");
        ImGui::BulletText("Ctrl+ホイール: 横の拡大縮小");
        ImGui::Separator();
        ImGui::TextUnformatted("録音");
        ImGui::BulletText("録音を押してから再生すると、弾いた音がノートになる");
        ImGui::EndMenu();
    }

    // 右端に状態表示
    if (statusTimer_ > 0.0f)
    {
        const float textWidth = ImGui::CalcTextSize(statusMessage_.c_str()).x;
        ImGui::SameLine(ImGui::GetContentRegionMax().x - textWidth - 12.0f);
        ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kAccentGreen);
        ImGui::TextUnformatted(statusMessage_.c_str());
        ImGui::PopStyleColor();
        statusTimer_ -= Frame::DeltaTime();
    }

    ImGui::EndMenuBar();
}

void MusicEditor::DrawTransport()
{
    MusicEngine *engine = MusicEngine::GetInstance();

    const bool playing = engine->IsPlaying();
    {
        const ImVec4 base = playing ? DebugTheme::kButtonConfirm : DebugTheme::kButtonPrimary;
        const ImVec4 hover = playing ? DebugTheme::kButtonConfirmHover : DebugTheme::kButtonPrimaryHover;
        if (RoleButton(playing ? (ICON_FA_PAUSE " 一時停止") : (ICON_FA_PLAY " 再生"), base, hover,
                       ImVec2(110.0f, 0.0f)))
        {
            if (playing)
                engine->Pause();
            else
                engine->Play();
        }
    }
    ImGui::SameLine();
    if (NeutralButton(ICON_FA_STOP " 停止", ImVec2(90.0f, 0.0f)))
        engine->Stop();

    ImGui::SameLine();
    bool recording = engine->IsRecording();
    if (RoleButton(recording ? (ICON_FA_CIRCLE " 録音中") : (ICON_FA_CIRCLE " 録音"),
                   recording ? DebugTheme::kButtonDangerHover : DebugTheme::kButtonDanger,
                   DebugTheme::kButtonDangerHover, ImVec2(100.0f, 0.0f)))
    {
        engine->SetRecording(!recording);
        engine->SetRecordTrack(selectedTrack_);
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("録音をONにして再生すると、弾いた音が選択中トラックへ書き込まれます");

    ImGui::SameLine();
    ImGui::TextUnformatted("|");
    ImGui::SameLine();

    float tempo = engine->GetTempo();
    ImGui::SetNextItemWidth(110.0f);
    if (ImGui::DragFloat("テンポ", &tempo, 0.5f, 20.0f, 300.0f, "%.1f BPM"))
        engine->SetTempo(tempo);

    ImGui::SameLine();
    int beatsPerBar = engine->GetBeatsPerBar();
    ImGui::SetNextItemWidth(80.0f);
    if (ImGui::DragInt("拍子", &beatsPerBar, 0.1f, 1, 16, "%d / 4"))
        engine->SetBeatsPerBar(beatsPerBar);

    ImGui::SameLine();
    ImGui::TextUnformatted("|");
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextReadOnly);
    ImGui::Text("%s", FormatPosition(engine->GetPositionTicks(), beatsPerBar).c_str());
    ImGui::PopStyleColor();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("小節 : 拍 : ティック");

    ImGui::SameLine();
    bool loop = engine->IsLoopEnabled();
    if (AccentCheckbox("ループ", &loop, DebugTheme::kAccentCyan))
        engine->SetLoopEnabled(loop);

    ImGui::SameLine();
    bool metronome = engine->IsMetronomeEnabled();
    if (AccentCheckbox("メトロノーム", &metronome, DebugTheme::kAccentYellow))
        engine->SetMetronomeEnabled(metronome);

    // 2段目: 打ち込みの設定と音量
    ImGui::SetNextItemWidth(130.0f);
    const char *kGridNames[] = {"4分", "8分", "3連8分", "16分", "3連16分", "32分"};
    const int kGridDivisions[] = {1, 2, 3, 4, 6, 8};
    int gridIndex = 3;
    for (int i = 0; i < IM_ARRAYSIZE(kGridDivisions); ++i)
    {
        if (kGridDivisions[i] == gridDivision_)
            gridIndex = i;
    }
    if (ImGui::Combo("グリッド", &gridIndex, kGridNames, IM_ARRAYSIZE(kGridNames)))
    {
        gridDivision_ = kGridDivisions[gridIndex];
        defaultNoteLength_ = GetGridTicks();
    }
    ImGui::SameLine();
    AccentCheckbox("そろえる", &snapEnabled_, DebugTheme::kAccentBlue);

    ImGui::SameLine();
    ImGui::SetNextItemWidth(140.0f);
    ImGui::SliderFloat("打ち込む強さ", &noteVelocity_, 0.05f, 1.0f, "%.2f");

    // 選択中のノートがあれば、まとめて強さを変えられるようにする
    if (!selectedNotes_.empty())
    {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(140.0f);
        if (ImGui::SliderFloat("選択の強さ", &selectionVelocity_, 0.05f, 1.0f, "%.2f"))
        {
            MusicEngine::ScopedLock lock;
            if (MusicTrack *track = GetSelectedTrack())
            {
                for (uint32_t id : selectedNotes_)
                {
                    if (MusicNote *note = track->FindNote(id))
                        note->velocity = selectionVelocity_;
                }
            }
        }
    }

    ImGui::SameLine();
    ImGui::TextUnformatted("|");
    ImGui::SameLine();
    float masterVolume = engine->GetMasterVolume();
    ImGui::SetNextItemWidth(150.0f);
    if (ImGui::SliderFloat("全体音量", &masterVolume, 0.0f, 1.5f, "%.2f"))
        engine->SetMasterVolume(masterVolume);

    // 出力レベルメーター
    ImGui::SameLine();
    {
        ImDrawList *drawList = ImGui::GetWindowDrawList();
        const ImVec2 position = ImGui::GetCursorScreenPos();
        const float width = 90.0f;
        const float height = ImGui::GetFrameHeight();
        const float barHeight = (height - 3.0f) * 0.5f;
        drawList->AddRectFilled(position, ImVec2(position.x + width, position.y + height),
                                ImGui::ColorConvertFloat4ToU32(ImVec4(0.10f, 0.10f, 0.12f, 1.0f)), 2.0f);
        for (int channel = 0; channel < 2; ++channel)
        {
            const float level = std::clamp(engine->GetMeterLevel(channel), 0.0f, 1.0f);
            const ImVec4 color = (level > 0.98f) ? DebugTheme::kAccentRed : DebugTheme::kAccentGreen;
            const float top = position.y + 1.0f + (barHeight + 1.0f) * channel;
            drawList->AddRectFilled(ImVec2(position.x + 1.0f, top),
                                    ImVec2(position.x + 1.0f + (width - 2.0f) * level, top + barHeight),
                                    ImGui::ColorConvertFloat4ToU32(color), 1.0f);
        }
        ImGui::Dummy(ImVec2(width, height));
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("出力レベル（上=左 / 下=右）\nミキサー負荷: %.0f%%", engine->GetCpuLoad() * 100.0f);
    }

    ImGui::SameLine();
    AccentCheckbox("PCキーボードで弾く", &pcKeyboardEnabled_, DebugTheme::kAccentPurple);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("ONのあいだ、このウィンドウにフォーカスがあるとゲーム側のキー入力は止まります");

    DrawProjectDialogs();
}

void MusicEditor::DrawProjectDialogs()
{
    MusicEngine *engine = MusicEngine::GetInstance();

    if (!ThemedHeader(ICON_FA_SAVE " 保存 / 読み込み / 書き出し", DebugTheme::kAccentGreen, false))
        return;

    ImGui::Indent();

    ImGui::SetNextItemWidth(200.0f);
    ImGui::InputText("曲名##project", &projectFileName_);
    ImGui::SameLine();
    if (ConfirmButton(ICON_FA_SAVE " 保存"))
    {
        const std::string path = ProjectDirectory() + "/" + projectFileName_ + ".json";
        std::string error;
        engine->SetProjectName(projectFileName_);
        if (engine->SaveProject(path, &error))
        {
            statusMessage_ = "保存しました: " + projectFileName_;
            statusTimer_ = 3.0f;
            ImGuiNotification::Post("曲を保存しました: " + projectFileName_);
            projectFiles_.clear();
        }
        else
        {
            ImGuiNotification::Post(error, {0.9f, 0.4f, 0.4f, 1.0f});
        }
    }

    // 保存済みの一覧（初回と保存直後に作り直す）
    if (projectFiles_.empty())
    {
        std::error_code ec;
        if (std::filesystem::exists(ProjectDirectory(), ec))
        {
            for (const auto &entry : std::filesystem::directory_iterator(ProjectDirectory(), ec))
            {
                if (entry.is_regular_file() && entry.path().extension() == ".json")
                    projectFiles_.push_back(entry.path().stem().string());
            }
            std::sort(projectFiles_.begin(), projectFiles_.end());
        }
    }

    if (projectFiles_.empty())
    {
        DimText("保存済みの曲はまだありません");
    }
    else
    {
        ImGui::BeginChild("##projectList", ImVec2(0.0f, 82.0f), ImGuiChildFlags_Borders);
        for (const std::string &name : projectFiles_)
        {
            if (ImGui::Selectable((ICON_FA_MUSIC " " + name).c_str()))
            {
                std::string error;
                if (engine->LoadProject(ProjectDirectory() + "/" + name + ".json", &error))
                {
                    projectFileName_ = name;
                    exportFileName_ = name;
                    selectedTrack_ = 0;
                    selectedNotes_.clear();
                    ImGuiNotification::Post("曲を読み込みました: " + name);
                }
                else
                {
                    ImGuiNotification::Post(error, {0.9f, 0.4f, 0.4f, 1.0f});
                }
            }
        }
        ImGui::EndChild();
    }

    ImGui::Separator();
    ImGui::SetNextItemWidth(200.0f);
    ImGui::InputText("出力名##export", &exportFileName_);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120.0f);
    ImGui::DragFloat("余韻##export", &exportTailSeconds_, 0.1f, 0.0f, 10.0f, "%.1f 秒");
    ImGui::SameLine();
    if (PrimaryButton(ICON_FA_FILE_EXPORT " wav書き出し"))
    {
        const std::string path = ExportDirectory() + "/" + exportFileName_ + ".wav";
        std::string error;
        if (engine->ExportWav(path, exportTailSeconds_, &error))
        {
            statusMessage_ = "書き出しました";
            statusTimer_ = 4.0f;
            ImGuiNotification::Post("wav を書き出しました: " + path);
            wavScanned_ = false;
        }
        else
        {
            ImGuiNotification::Post(error, {0.9f, 0.4f, 0.4f, 1.0f});
        }
    }
    DimText(("出力先: " + ExportDirectory()).c_str());

    ImGui::Unindent();
}

void MusicEditor::DrawTrackList()
{
    MusicEngine *engine = MusicEngine::GetInstance();

    SectionHeader("トラック", DebugTheme::kAccentBlue);

    for (int i = 0; i < engine->GetTrackCount(); ++i)
    {
        MusicTrack *track = engine->GetTrack(i);
        if (!track)
            continue;

        ImGui::PushID(i);

        const float *color = track->GetColor();
        const ImVec4 accent(color[0], color[1], color[2], 1.0f);

        // 選択行はトラック色で塗る
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(accent.x, accent.y, accent.z, 0.30f));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(accent.x, accent.y, accent.z, 0.22f));
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(accent.x, accent.y, accent.z, 0.40f));

        const std::string label = track->GetName() + "##track";
        if (ImGui::Selectable(label.c_str(), selectedTrack_ == i, ImGuiSelectableFlags_AllowOverlap,
                              ImVec2(0.0f, ImGui::GetFrameHeight())))
        {
            selectedTrack_ = i;
            selectedNotes_.clear();
            engine->SetRecordTrack(i);
        }
        ImGui::PopStyleColor(3);

        // ミュート / ソロ を右端に置く
        const float buttonWidth = ImGui::GetFrameHeight() + 4.0f;
        ImGui::SameLine(ImGui::GetContentRegionMax().x - buttonWidth * 2.0f - 6.0f);
        {
            bool muted = track->IsMuted();
            ScopedButtonColors colors(muted ? DebugTheme::kButtonDanger : DebugTheme::kButtonNeutral,
                                      DebugTheme::kButtonDangerHover);
            if (ImGui::Button("M", ImVec2(buttonWidth, 0.0f)))
            {
                MusicEngine::ScopedLock lock;
                track->SetMuted(!muted);
            }
        }
        ImGui::SameLine();
        {
            bool soloed = track->IsSoloed();
            ScopedButtonColors colors(soloed ? DebugTheme::kButtonConfirm : DebugTheme::kButtonNeutral,
                                      DebugTheme::kButtonConfirmHover);
            if (ImGui::Button("S", ImVec2(buttonWidth, 0.0f)))
            {
                MusicEngine::ScopedLock lock;
                track->SetSoloed(!soloed);
            }
        }

        // 選択中のトラックだけ細かい設定を出す
        if (selectedTrack_ == i)
        {
            ImGui::Indent();
            std::string name = track->GetName();
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::InputText("##name", &name))
            {
                MusicEngine::ScopedLock lock;
                track->SetName(name);
            }

            float volume = track->GetVolume();
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::SliderFloat("##volume", &volume, 0.0f, 1.5f, "音量 %.2f"))
                track->SetVolume(volume);

            float pan = track->GetPan();
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::SliderFloat("##pan", &pan, -1.0f, 1.0f, "定位 %.2f"))
                track->SetPan(pan);

            ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
            ImGui::Text("ノート %d / 発音 %d", track->GetNoteCount(),
                        track->GetInstrument()->GetActiveVoiceCount());
            ImGui::PopStyleColor();

            if (DangerButton(ICON_FA_TRASH_ALT " このトラックを削除", ImVec2(-1.0f, 0.0f)))
            {
                engine->RemoveTrack(i);
                selectedNotes_.clear();
                selectedTrack_ = std::max(0, std::min(selectedTrack_, engine->GetTrackCount() - 1));
                ImGui::Unindent();
                ImGui::PopID();
                break;
            }
            ImGui::Unindent();
        }

        ImGui::PopID();
    }

    ImGui::Spacing();
    if (ConfirmButton(ICON_FA_PLUS " シンセ", ImVec2(-1.0f, 0.0f)))
        selectedTrack_ = engine->AddTrack("シンセ", InstrumentKind::Synth);
    if (ConfirmButton(ICON_FA_PLUS " ドラムマシン", ImVec2(-1.0f, 0.0f)))
        selectedTrack_ = engine->AddTrack("ドラム", InstrumentKind::DrumMachine);
    if (ConfirmButton(ICON_FA_PLUS " サンプラー (wav)", ImVec2(-1.0f, 0.0f)))
        selectedTrack_ = engine->AddTrack("サンプラー", InstrumentKind::Sampler);
}

void MusicEditor::DrawKeyboardBar()
{
    MusicTrack *track = GetSelectedTrack();

    ImGui::BeginChild("##keyboardBar", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders);

    // オクターブ操作
    ImGui::AlignTextToFramePadding();
    DimText("オクターブ");
    ImGui::SameLine();
    if (ImGui::ArrowButton("##octaveDown", ImGuiDir_Left))
        keyboardOctave_ = std::max(0, keyboardOctave_ - 1);
    ImGui::SameLine();
    ImGui::Text("C%d", keyboardOctave_);
    ImGui::SameLine();
    if (ImGui::ArrowButton("##octaveUp", ImGuiDir_Right))
        keyboardOctave_ = std::min(8, keyboardOctave_ + 1);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(140.0f);
    ImGui::SliderFloat("弾く強さ", &playVelocity_, 0.05f, 1.0f, "%.2f");
    ImGui::SameLine();
    if (track)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
        ImGui::Text("→ %s", track->GetName().c_str());
        ImGui::PopStyleColor();
    }

    // 鍵盤本体
    const ImVec2 area = ImGui::GetContentRegionAvail();
    const float height = std::max(34.0f, area.y - 2.0f);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##keys", ImVec2(area.x, height));

    const bool hovered = ImGui::IsItemHovered();
    const bool held = ImGui::IsItemActive();

    // 白鍵の数を幅から決める。1オクターブは白鍵7つ
    const int whiteKeyCount = std::clamp(static_cast<int>(area.x / 24.0f), 7, 42);
    const float whiteWidth = area.x / static_cast<float>(whiteKeyCount);
    const float blackWidth = whiteWidth * 0.62f;
    const float blackHeight = height * 0.62f;
    const int baseNote = 12 * (keyboardOctave_ + 1);

    ImDrawList *drawList = ImGui::GetWindowDrawList();
    static const int kWhiteOffsets[7] = {0, 2, 4, 5, 7, 9, 11};

    // 白鍵 → 黒鍵の順に描く（黒鍵が手前になるように）
    int hitNote = -1;
    const ImVec2 mouse = ImGui::GetMousePos();

    for (int i = 0; i < whiteKeyCount; ++i)
    {
        const int note = baseNote + (i / 7) * 12 + kWhiteOffsets[i % 7];
        if (note > MusicConst::kHighestNote)
            break;
        const float x0 = origin.x + whiteWidth * i;
        const float x1 = x0 + whiteWidth - 1.0f;
        const bool sounding = (note >= 0 && note < 128) && noteRefCount_[note] > 0;
        const ImU32 fill = sounding ? IM_COL32(120, 175, 235, 255) : IM_COL32(235, 235, 238, 255);
        drawList->AddRectFilled(ImVec2(x0, origin.y), ImVec2(x1, origin.y + height), fill, 2.0f);
        drawList->AddRect(ImVec2(x0, origin.y), ImVec2(x1, origin.y + height), IM_COL32(40, 40, 45, 255), 2.0f);

        // ドの位置にだけ音名を出す（全部出すとうるさい）
        if ((note % 12) == 0 && whiteWidth > 18.0f)
        {
            drawList->AddText(ImVec2(x0 + 3.0f, origin.y + height - ImGui::GetTextLineHeight() - 2.0f),
                              IM_COL32(90, 90, 100, 255), NoteName(note).c_str());
        }

        if (hovered && mouse.x >= x0 && mouse.x < x1 && mouse.y >= origin.y && mouse.y <= origin.y + height)
            hitNote = note;
    }

    for (int i = 0; i < whiteKeyCount; ++i)
    {
        // 白鍵のあいだに黒鍵がある位置だけ描く（ミ-ファ、シ-ドの間には無い）
        const int step = i % 7;
        if (step == 2 || step == 6)
            continue;
        const int note = baseNote + (i / 7) * 12 + kWhiteOffsets[step] + 1;
        if (note > MusicConst::kHighestNote)
            break;
        const float center = origin.x + whiteWidth * (i + 1);
        const float x0 = center - blackWidth * 0.5f;
        const float x1 = center + blackWidth * 0.5f;
        if (x1 > origin.x + area.x)
            break;
        const bool sounding = (note >= 0 && note < 128) && noteRefCount_[note] > 0;
        const ImU32 fill = sounding ? IM_COL32(80, 140, 210, 255) : IM_COL32(28, 28, 32, 255);
        drawList->AddRectFilled(ImVec2(x0, origin.y), ImVec2(x1, origin.y + blackHeight), fill, 2.0f);
        drawList->AddRect(ImVec2(x0, origin.y), ImVec2(x1, origin.y + blackHeight), IM_COL32(10, 10, 12, 255), 2.0f);

        if (hovered && mouse.x >= x0 && mouse.x < x1 && mouse.y >= origin.y && mouse.y <= origin.y + blackHeight)
            hitNote = note;
    }

    // マウスで押している音を追う。ドラッグで隣の鍵へ移ったら持ち替える
    if (held && hitNote >= 0)
    {
        if (mouseHeldNotes_.empty() || mouseHeldNotes_.front() != hitNote)
        {
            for (int note : mouseHeldNotes_)
                ReleaseNote(note);
            mouseHeldNotes_.clear();
            PressNote(hitNote, playVelocity_);
            mouseHeldNotes_.push_back(hitNote);
        }
    }
    else if (!held && !mouseHeldNotes_.empty())
    {
        for (int note : mouseHeldNotes_)
            ReleaseNote(note);
        mouseHeldNotes_.clear();
    }

    ImGui::EndChild();
}

void MusicEditor::HandlePcKeyboard()
{
    // オクターブの上げ下げ
    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow, false))
        keyboardOctave_ = std::min(8, keyboardOctave_ + 1);
    if (ImGui::IsKeyPressed(ImGuiKey_DownArrow, false))
        keyboardOctave_ = std::max(0, keyboardOctave_ - 1);

    const int baseNote = 12 * (keyboardOctave_ + 1);
    static_assert(IM_ARRAYSIZE(kKeyMap) <= 64, "pcKeyPressed_ の要素数を増やしてください");

    // 同じ音に複数のキーが割り当たっていることがあるので、
    // 鳴らし始め・止めの判断はキー単位、実際の発音は PressNote 側の重なり数で行う
    for (int i = 0; i < IM_ARRAYSIZE(kKeyMap); ++i)
    {
        const int note = baseNote + kKeyMap[i].semitone;
        if (note < 0 || note > MusicConst::kHighestNote)
            continue;

        const bool down = ImGui::IsKeyDown(kKeyMap[i].key);
        if (down && !pcKeyPressed_[i])
        {
            PressNote(note, playVelocity_);
            pcKeyPressed_[i] = true;
            pcKeyNote_[i] = note;
        }
        else if (!down && pcKeyPressed_[i])
        {
            // オクターブを変えた直後でも、押したときの音をそのまま止める
            ReleaseNote(pcKeyNote_[i]);
            pcKeyPressed_[i] = false;
        }
    }

    if (ImGui::IsKeyPressed(ImGuiKey_Delete, false) && !selectedNotes_.empty())
        DeleteSelectedNotes();

    // スペースで再生／一時停止（ボタンやテキスト欄を操作中は邪魔しない）
    if (ImGui::IsKeyPressed(ImGuiKey_Space, false) && !ImGui::IsAnyItemActive())
    {
        MusicEngine *engine = MusicEngine::GetInstance();
        if (engine->IsPlaying())
            engine->Pause();
        else
            engine->Play();
    }
}

void MusicEditor::PressNote(int note, float velocity)
{
    if (note < 0 || note >= 128)
        return;
    if (noteRefCount_[note]++ == 0)
        MusicEngine::GetInstance()->NoteOn(selectedTrack_, note, velocity);
}

void MusicEditor::ReleaseNote(int note)
{
    if (note < 0 || note >= 128)
        return;
    if (noteRefCount_[note] <= 0)
        return;
    if (--noteRefCount_[note] == 0)
        MusicEngine::GetInstance()->NoteOff(selectedTrack_, note);
}

void MusicEditor::PreviewNote(int note, float velocity)
{
    PressNote(note, velocity);
    previewNotes_.emplace_back(note, 0.35f);
}

void MusicEditor::UpdatePreviewNotes()
{
    const float deltaTime = Frame::DeltaTime();
    for (size_t i = 0; i < previewNotes_.size();)
    {
        previewNotes_[i].second -= deltaTime;
        if (previewNotes_[i].second <= 0.0f)
        {
            ReleaseNote(previewNotes_[i].first);
            previewNotes_.erase(previewNotes_.begin() + i);
            continue;
        }
        ++i;
    }
}

void MusicEditor::ReleaseAllHeldNotes()
{
    for (int note = 0; note < 128; ++note)
    {
        while (noteRefCount_[note] > 0)
            ReleaseNote(note);
    }
    pcKeyPressed_.fill(false);
    mouseHeldNotes_.clear();
    previewNotes_.clear();
}

} // namespace Hagine
#endif // USE_IMGUI
