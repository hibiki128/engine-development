#include "TimelineEditor.h"
#ifdef USE_IMGUI

#include <Frame.h>
#include <camera/Camera.h>
#include <camera/CameraManager.h>
#include <debug/imgui/DebugUIHelper.h>
#include <debug/imgui/ImGuiNotification.h>
#include <icon/IconsFontAwesome5.h>
#include <object/base/BaseObject.h>
#include <object/base/BaseObjectManager.h>
#include <particle/gpu/ParticleCSSpawner.h>

#include <algorithm>
#include <cmath>
#include <debug/imgui/AssetDragDrop.h>
#include <format>
#include <numbers>

namespace Hagine {
namespace {

constexpr float kTrackNameWidth = 150.0f;
constexpr float kRulerHeight = 22.0f;

/// トラック種類の表示名
const char *TrackTypeName(int type)
{
    static const char *kNames[] = {"オブジェクト", "カメラ", "音", "パーティクル", "合図"};
    if (type < 0 || type >= IM_ARRAYSIZE(kNames))
        return "";
    return kNames[type];
}

/// トラック種類のアイコン
const char *TrackTypeIcon(int type)
{
    switch (static_cast<TimelineTrackType>(type))
    {
    case TimelineTrackType::Transform:
        return ICON_FA_CUBE;
    case TimelineTrackType::Camera:
        return ICON_FA_VIDEO;
    case TimelineTrackType::Sound:
        return ICON_FA_VOLUME_UP;
    case TimelineTrackType::Particle:
        return ICON_FA_STAR;
    case TimelineTrackType::Event:
        return ICON_FA_FLAG;
    default:
        return ICON_FA_CIRCLE;
    }
}

/// トラック種類ごとの色。グリッド上で見分けやすくする
ImVec4 TrackTypeColor(int type)
{
    switch (static_cast<TimelineTrackType>(type))
    {
    case TimelineTrackType::Transform:
        return DebugTheme::kAccentBlue;
    case TimelineTrackType::Camera:
        return DebugTheme::kAccentPurple;
    case TimelineTrackType::Sound:
        return DebugTheme::kAccentGreen;
    case TimelineTrackType::Particle:
        return DebugTheme::kAccentOrange;
    case TimelineTrackType::Event:
        return DebugTheme::kAccentYellow;
    default:
        return DebugTheme::kAccentBlue;
    }
}

/// そのトラックがキーフレームで動かす種類か（音などは「点」しか持たない）
bool IsKeyframeTrack(int type)
{
    return type == static_cast<int>(TimelineTrackType::Transform) ||
           type == static_cast<int>(TimelineTrackType::Camera);
}

/// イージングの表示名
const char *EasingName(int easing)
{
    static const char *kNames[] = {
        "リニア", "InSine", "OutSine", "InOutSine", "InQuad", "OutQuad", "InOutQuad",
        "InCubic", "OutCubic", "InOutCubic", "InQuart", "OutQuart", "InOutQuart",
        "InQuint", "OutQuint", "InOutQuint", "InCirc", "OutCirc", "InOutCirc",
        "InExpo", "OutExpo", "InOutExpo", "InBack", "OutBack", "InOutBack",
        "InElastic", "OutElastic", "InOutElastic", "InBounce", "OutBounce", "InOutBounce"};
    if (easing < 0 || easing >= IM_ARRAYSIZE(kNames))
        return "";
    return kNames[easing];
}

/// キーを時間順に並べ直す（移動した直後に呼ぶ）
void SortTrack(TimelineTrack &track)
{
    std::sort(track.keys.begin(), track.keys.end(),
              [](const TimelineKey &a, const TimelineKey &b) { return a.time < b.time; });
    std::sort(track.events.begin(), track.events.end(),
              [](const TimelineEvent &a, const TimelineEvent &b) { return a.time < b.time; });
}

} // namespace

TimelineEditor *TimelineEditor::GetInstance()
{
    static TimelineEditor instance;
    return &instance;
}

void TimelineEditor::Draw(bool *open)
{
    if (open && !*open)
        return;

    TimelineManager *manager = TimelineManager::GetInstance();
    if (!initialized_)
    {
        sequenceNameBuffer_ = manager->GetSequence().GetName();
        initialized_ = true;
    }

    ImGui::SetNextWindowSize(ImVec2(1000.0f, 560.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(ICON_FA_FILM " タイムライン", open,
                      ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoFocusOnAppearing))
    {
        ImGui::End();
        return;
    }

    DrawMenuBar();
    DrawTransport();
    HandleShortcuts();
    ImGui::Separator();

    const float inspectorHeight = 190.0f;
    const float upperHeight = std::max(140.0f, ImGui::GetContentRegionAvail().y - inspectorHeight);

    ImGui::BeginChild("##timelineUpper", ImVec2(0.0f, upperHeight));
    {
        ImGui::BeginChild("##trackList", ImVec2(kTrackNameWidth + 60.0f, 0.0f), ImGuiChildFlags_Borders);
        DrawTrackList();
        ImGui::EndChild();

        ImGui::SameLine();

        // Ctrl+ホイールは拡大に使うので、その間はホイールでスクロールさせない
        ImGui::BeginChild("##timelineGrid", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders,
                          ImGuiWindowFlags_HorizontalScrollbar | (ImGui::GetIO().KeyCtrl ? ImGuiWindowFlags_NoScrollWithMouse : 0));
        DrawTimelineGrid();
        ImGui::EndChild();
    }
    ImGui::EndChild();

    ImGui::BeginChild("##timelineInspector", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders);
    DrawInspector();
    ImGui::EndChild();

    ImGui::End();
}

void TimelineEditor::DrawMenuBar()
{
    TimelineManager *manager = TimelineManager::GetInstance();

    if (!ImGui::BeginMenuBar())
        return;

    if (ImGui::BeginMenu(ICON_FA_FILE " ファイル"))
    {
        if (ImGui::MenuItem(ICON_FA_FILE " 新規作成"))
        {
            manager->Stop();
            manager->GetSequence().Clear();
            manager->GetSequence().SetName("NewTimeline");
            sequenceNameBuffer_ = "NewTimeline";
            selectedTrack_ = -1;
            selectedKey_ = -1;
            selectedEvent_ = -1;
        }
        if (ImGui::MenuItem(ICON_FA_SAVE " 保存"))
        {
            manager->GetSequence().SetName(sequenceNameBuffer_);
            std::string error;
            if (manager->SaveSequence(&error))
            {
                statusMessage_ = "保存しました: " + sequenceNameBuffer_;
                statusTimer_ = 3.0f;
                ImGuiNotification::Post("演出を保存しました: " + sequenceNameBuffer_);
            }
            else
            {
                ImGuiNotification::Post(error, {0.9f, 0.4f, 0.4f, 1.0f});
            }
        }
        ImGui::Separator();
        if (manager->GetSequenceNames().empty())
        {
            ImGui::MenuItem("保存済みの演出はありません", nullptr, false, false);
        }
        else
        {
            for (const std::string &name : manager->GetSequenceNames())
            {
                if (ImGui::MenuItem((ICON_FA_FOLDER_OPEN " " + name).c_str()))
                {
                    std::string error;
                    if (manager->LoadSequence(name, &error))
                    {
                        sequenceNameBuffer_ = name;
                        selectedTrack_ = -1;
                        selectedKey_ = -1;
                        selectedEvent_ = -1;
                        ImGuiNotification::Post("演出を読み込みました: " + name);
                    }
                    else
                    {
                        ImGuiNotification::Post(error, {0.9f, 0.4f, 0.4f, 1.0f});
                    }
                }
            }
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu(ICON_FA_QUESTION_CIRCLE " 使い方"))
    {
        ImGui::BulletText("左の「＋」でトラックを足し、対象（オブジェクト／カメラ）を選ぶ");
        ImGui::BulletText("時間つまみを動かし、対象をギズモで置いてから");
        ImGui::BulletText("「今の位置をキーにする」を押す → これを繰り返すだけで動きになる");
        ImGui::Separator();
        ImGui::BulletText("グリッド上でキーを左ドラッグ: 時間を動かす");
        ImGui::BulletText("何もない所を右クリック: その位置にキー／イベントを追加");
        ImGui::BulletText("上の目盛りをドラッグ: 再生位置を動かす");
        ImGui::BulletText("Delete: 選択中のキー／イベントを消す");
        ImGui::EndMenu();
    }

    if (statusTimer_ > 0.0f)
    {
        const float textWidth = ImGui::CalcTextSize(statusMessage_.c_str()).x;
        ImGui::SameLine(ImGui::GetContentRegionMax().x - textWidth - 12.0f);
        ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kAccentGreen);
        ImGui::TextUnformatted(statusMessage_.c_str());
        ImGui::PopStyleColor();
        statusTimer_ -= Frame::UnscaledDeltaTime();
    }

    ImGui::EndMenuBar();
}

void TimelineEditor::DrawTransport()
{
    TimelineManager *manager = TimelineManager::GetInstance();
    TimelineSequence &sequence = manager->GetSequence();

    const bool playing = manager->IsPlaying();
    if (RoleButton(playing ? (ICON_FA_PAUSE " 一時停止") : (ICON_FA_PLAY " 再生"),
                   playing ? DebugTheme::kButtonConfirm : DebugTheme::kButtonPrimary,
                   playing ? DebugTheme::kButtonConfirmHover : DebugTheme::kButtonPrimaryHover,
                   ImVec2(110.0f, 0.0f)))
    {
        if (playing)
            manager->Pause();
        else
            manager->PlayCurrent();
    }
    ImGui::SameLine();
    if (NeutralButton(ICON_FA_STOP " 停止", ImVec2(90.0f, 0.0f)))
        manager->Stop();

    ImGui::SameLine();
    float time = manager->GetTime();
    ImGui::SetNextItemWidth(220.0f);
    if (ImGui::SliderFloat("##time", &time, 0.0f, sequence.GetDuration(), "%.2f 秒"))
        manager->SetTime(time, false);

    ImGui::SameLine();
    float duration = sequence.GetDuration();
    ImGui::SetNextItemWidth(110.0f);
    if (ImGui::DragFloat("長さ", &duration, 0.1f, 0.1f, 300.0f, "%.1f 秒"))
        sequence.SetDuration(duration);

    ImGui::SameLine();
    bool loop = sequence.IsLooping();
    if (AccentCheckbox("ループ", &loop, DebugTheme::kAccentCyan))
        sequence.SetLooping(loop);

    ImGui::SameLine();
    bool autoCamera = manager->IsAutoActivateCamera();
    if (AccentCheckbox("再生時にカメラを切り替える", &autoCamera, DebugTheme::kAccentPurple))
        manager->SetAutoActivateCamera(autoCamera);

    ImGui::SetNextItemWidth(200.0f);
    ImGui::InputText("演出名", &sequenceNameBuffer_);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(160.0f);
    ImGui::SliderFloat("横の拡大", &pixelsPerSecond_, 20.0f, 800.0f, "%.0f px/秒", ImGuiSliderFlags_Logarithmic);
    ImGui::SetItemTooltip("グリッドの上で Ctrl+ホイールでも拡大・縮小できます");
    ImGui::SameLine();
    AccentCheckbox(ICON_FA_MAGNET " スナップ", &snapEnabled_, DebugTheme::kAccentGreen);
    ImGui::SetItemTooltip("キーと再生位置を刻みに吸着させます（Shift を押している間は反転）");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(90.0f);
    static const float kSnapSteps[] = {0.05f, 0.1f, 0.25f, 0.5f, 1.0f};
    if (ImGui::BeginCombo("##snapStep", std::format("{:.2f} 秒", snapStep_).c_str()))
    {
        for (float step : kSnapSteps)
        {
            if (ImGui::Selectable(std::format("{:.2f} 秒", step).c_str(), snapStep_ == step))
                snapStep_ = step;
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    ImGui::SetItemTooltip("Space: 再生/一時停止  Home/End: 先頭/末尾  ←→: 刻みぶん進める/戻す\n"
                          "キーをダブルクリック: 再生位置をそこへ  右クリック: メニュー");
}

float TimelineEditor::SnapTime(float time) const
{
    const bool snap = snapEnabled_ != ImGui::GetIO().KeyShift;
    if (!snap || snapStep_ <= 0.0f)
        return time;
    return std::round(time / snapStep_) * snapStep_;
}

void TimelineEditor::HandleShortcuts()
{
    // タイムラインの窓（子も含む）にフォーカスがあるときだけ。文字入力中は奪わない
    if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) || ImGui::GetIO().WantTextInput)
        return;
    TimelineManager *manager = TimelineManager::GetInstance();
    const float duration = manager->GetSequence().GetDuration();
    if (ImGui::IsKeyPressed(ImGuiKey_Space, false))
    {
        if (manager->IsPlaying())
            manager->Pause();
        else
            manager->PlayCurrent();
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Home, false))
        manager->SetTime(0.0f, false);
    if (ImGui::IsKeyPressed(ImGuiKey_End, false))
        manager->SetTime(duration, false);
    const float step = (snapStep_ > 0.0f) ? snapStep_ : (1.0f / 30.0f);
    if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow))
        manager->SetTime(std::clamp(manager->GetTime() - step, 0.0f, duration), false);
    if (ImGui::IsKeyPressed(ImGuiKey_RightArrow))
        manager->SetTime(std::clamp(manager->GetTime() + step, 0.0f, duration), false);
}

void TimelineEditor::DrawTrackList()
{
    TimelineManager *manager = TimelineManager::GetInstance();
    TimelineSequence &sequence = manager->GetSequence();
    std::vector<TimelineTrack> &tracks = sequence.GetTracks();

    // グリッド側と行の高さをそろえるため、目盛りぶんの空白を先に入れる
    ImGui::Dummy(ImVec2(0.0f, kRulerHeight - ImGui::GetStyle().ItemSpacing.y));

    for (int i = 0; i < static_cast<int>(tracks.size()); ++i)
    {
        TimelineTrack &track = tracks[i];
        ImGui::PushID(i);

        const ImVec4 color = TrackTypeColor(track.type);
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(color.x, color.y, color.z, 0.30f));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(color.x, color.y, color.z, 0.20f));
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(color.x, color.y, color.z, 0.40f));

        const std::string label = std::string(TrackTypeIcon(track.type)) + " " + track.name + "##track";
        if (ImGui::Selectable(label.c_str(), selectedTrack_ == i, ImGuiSelectableFlags_AllowOverlap,
                              ImVec2(kTrackNameWidth, trackRowHeight_ - 2.0f)))
        {
            selectedTrack_ = i;
            selectedKey_ = -1;
            selectedEvent_ = -1;
        }
        ImGui::PopStyleColor(3);

        // 有効・無効のチェックを右端に置く
        ImGui::SameLine(kTrackNameWidth + 6.0f);
        ImGui::Checkbox("##enabled", &track.enabled);

        ImGui::PopID();
    }

    ImGui::Spacing();
    if (ImGui::Button(ICON_FA_PLUS " トラック追加", ImVec2(-1.0f, 0.0f)))
        ImGui::OpenPopup("##addTrack");
    if (ImGui::BeginPopup("##addTrack"))
    {
        for (int type = 0; type < static_cast<int>(TimelineTrackType::Count); ++type)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, TrackTypeColor(type));
            const bool picked = ImGui::Selectable(std::format("{} {}", TrackTypeIcon(type), TrackTypeName(type)).c_str());
            ImGui::PopStyleColor();
            if (picked)
            {
                selectedTrack_ = sequence.AddTrack(static_cast<TimelineTrackType>(type), TrackTypeName(type));
                selectedKey_ = -1;
                selectedEvent_ = -1;
            }
        }
        ImGui::EndPopup();
    }

    if (selectedTrack_ >= 0 && selectedTrack_ < static_cast<int>(tracks.size()))
    {
        if (DangerButton(ICON_FA_TRASH_ALT " トラック削除", ImVec2(-1.0f, 0.0f)))
        {
            sequence.RemoveTrack(selectedTrack_);
            selectedTrack_ = -1;
            selectedKey_ = -1;
            selectedEvent_ = -1;
        }
    }
}

void TimelineEditor::DrawTimelineGrid()
{
    TimelineManager *manager = TimelineManager::GetInstance();
    TimelineSequence &sequence = manager->GetSequence();
    std::vector<TimelineTrack> &tracks = sequence.GetTracks();

    const float duration = sequence.GetDuration();
    const float contentWidth = duration * pixelsPerSecond_ + 40.0f;
    const float contentHeight = kRulerHeight + tracks.size() * trackRowHeight_ + 20.0f;

    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 windowPos = ImGui::GetWindowPos();
    const ImVec2 windowSize = ImGui::GetWindowSize();

    ImGui::InvisibleButton("##gridCanvas", ImVec2(contentWidth, std::max(contentHeight, windowSize.y)),
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    const bool hovered = ImGui::IsItemHovered();

    ImDrawList *drawList = ImGui::GetWindowDrawList();
    const ImVec2 mouse = ImGui::GetMousePos();

    auto timeToX = [&](float time) { return origin.x + time * pixelsPerSecond_; };
    auto xToTime = [&](float x) { return std::clamp((x - origin.x) / pixelsPerSecond_, 0.0f, duration); };
    auto rowTop = [&](int index) { return origin.y + kRulerHeight + index * trackRowHeight_; };

    // --- 目盛り ---
    const float rulerTop = windowPos.y;
    drawList->PushClipRect(ImVec2(windowPos.x, rulerTop),
                           ImVec2(windowPos.x + windowSize.x, rulerTop + kRulerHeight), true);
    drawList->AddRectFilled(ImVec2(windowPos.x, rulerTop),
                            ImVec2(windowPos.x + windowSize.x, rulerTop + kRulerHeight),
                            IM_COL32(32, 33, 38, 255));
    // 1秒ごとに線と数字を入れる。拡大率が低いときは間引き、高いときは細かい刻みも出す
    const int secondStep = (pixelsPerSecond_ < 40.0f) ? 5 : 1;
    const float minorStep = (pixelsPerSecond_ >= 300.0f) ? 0.1f : ((pixelsPerSecond_ >= 90.0f) ? 0.5f : 0.0f);
    if (minorStep > 0.0f)
    {
        const int minorCount = static_cast<int>(std::ceil(duration / minorStep)) + 1;
        for (int m = 0; m <= minorCount; ++m)
        {
            const float x = timeToX(m * minorStep);
            drawList->AddLine(ImVec2(x, rulerTop + kRulerHeight - 5.0f), ImVec2(x, rulerTop + kRulerHeight),
                              IM_COL32(80, 84, 96, 255));
        }
    }
    for (int second = 0; second <= static_cast<int>(duration) + 1; second += secondStep)
    {
        const float x = timeToX(static_cast<float>(second));
        drawList->AddLine(ImVec2(x, rulerTop + 6.0f), ImVec2(x, rulerTop + kRulerHeight),
                          IM_COL32(110, 116, 130, 255));
        char label[16];
        snprintf(label, sizeof(label), "%ds", second);
        drawList->AddText(ImVec2(x + 3.0f, rulerTop + 3.0f), IM_COL32(180, 185, 195, 255), label);
    }
    drawList->PopClipRect();

    // --- 行の地と縦線 ---
    const float gridTop = windowPos.y + kRulerHeight;
    const float gridBottom = windowPos.y + windowSize.y;
    drawList->PushClipRect(ImVec2(windowPos.x, gridTop), ImVec2(windowPos.x + windowSize.x, gridBottom), true);

    for (int i = 0; i < static_cast<int>(tracks.size()); ++i)
    {
        const float top = rowTop(i);
        const ImU32 background = (i == selectedTrack_) ? IM_COL32(52, 56, 66, 255) : IM_COL32(40, 42, 48, 255);
        drawList->AddRectFilled(ImVec2(origin.x, top), ImVec2(origin.x + contentWidth, top + trackRowHeight_ - 1.0f),
                                background);
    }
    for (int second = 0; second <= static_cast<int>(duration) + 1; second += secondStep)
    {
        const float x = timeToX(static_cast<float>(second));
        drawList->AddLine(ImVec2(x, gridTop), ImVec2(x, gridBottom), IM_COL32(58, 60, 68, 255));
    }
    // 終端（演出の長さ）を示す線
    drawList->AddLine(ImVec2(timeToX(duration), gridTop), ImVec2(timeToX(duration), gridBottom),
                      IM_COL32(150, 110, 110, 220), 2.0f);

    // --- キーとイベント ---
    for (int i = 0; i < static_cast<int>(tracks.size()); ++i)
    {
        const TimelineTrack &track = tracks[i];
        const ImVec4 color = TrackTypeColor(track.type);
        const ImU32 fill = ImGui::ColorConvertFloat4ToU32(color);
        const float centerY = rowTop(i) + trackRowHeight_ * 0.5f;

        for (int k = 0; k < static_cast<int>(track.keys.size()); ++k)
        {
            const float x = timeToX(track.keys[k].time);
            const bool picked = (selectedTrack_ == i && selectedKey_ == k);
            const float radius = picked ? 7.0f : 5.0f;
            // キーはひし形、イベントは四角。形で種類が分かるようにする
            const ImVec2 points[4] = {{x, centerY - radius}, {x + radius, centerY}, {x, centerY + radius},
                                      {x - radius, centerY}};
            drawList->AddConvexPolyFilled(points, 4, fill);
            if (picked)
                drawList->AddPolyline(points, 4, IM_COL32(255, 255, 255, 230), ImDrawFlags_Closed, 2.0f);
        }

        for (int e = 0; e < static_cast<int>(track.events.size()); ++e)
        {
            const float x = timeToX(track.events[e].time);
            const bool picked = (selectedTrack_ == i && selectedEvent_ == e);
            const float half = picked ? 6.0f : 4.5f;
            drawList->AddRectFilled(ImVec2(x - half, centerY - half), ImVec2(x + half, centerY + half), fill, 2.0f);
            if (picked)
                drawList->AddRect(ImVec2(x - half, centerY - half), ImVec2(x + half, centerY + half),
                                  IM_COL32(255, 255, 255, 230), 2.0f, 0, 2.0f);
        }
    }

    // --- 再生位置 ---
    const float playheadX = timeToX(manager->GetTime());
    drawList->AddLine(ImVec2(playheadX, gridTop), ImVec2(playheadX, gridBottom), IM_COL32(240, 200, 100, 255), 1.5f);
    drawList->PopClipRect();

    drawList->AddTriangleFilled(ImVec2(playheadX - 5.0f, rulerTop + 4.0f), ImVec2(playheadX + 5.0f, rulerTop + 4.0f),
                                ImVec2(playheadX, rulerTop + kRulerHeight), IM_COL32(240, 200, 100, 255));

    // --- 操作 ---
    const bool inRuler = hovered && mouse.y < windowPos.y + kRulerHeight;

    if (dragTarget_ == DragTarget::None && inRuler && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        dragTarget_ = DragTarget::Playhead;

    if (dragTarget_ == DragTarget::Playhead)
    {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
            manager->SetTime(std::clamp(SnapTime(xToTime(mouse.x)), 0.0f, duration), false);
        else
            dragTarget_ = DragTarget::None;
        return;
    }

    // Ctrl+ホイール: マウスの下の時刻を動かさずに拡大・縮小する
    if (hovered && ImGui::GetIO().KeyCtrl && ImGui::GetIO().MouseWheel != 0.0f)
    {
        const float timeUnderMouse = (mouse.x - origin.x) / pixelsPerSecond_;
        const float oldPixels = pixelsPerSecond_;
        pixelsPerSecond_ = std::clamp(pixelsPerSecond_ * ((ImGui::GetIO().MouseWheel > 0.0f) ? 1.15f : 1.0f / 1.15f), 20.0f, 800.0f);
        ImGui::SetScrollX(ImGui::GetScrollX() + timeUnderMouse * (pixelsPerSecond_ - oldPixels));
    }

    // 再生中は再生位置が見えるようにスクロールを追従させる
    if (manager->IsPlaying())
    {
        const float visibleLeft = windowPos.x;
        const float visibleRight = windowPos.x + windowSize.x - 20.0f;
        if (playheadX < visibleLeft || playheadX > visibleRight)
        {
            ImGui::SetScrollX((std::max)(0.0f, manager->GetTime() * pixelsPerSecond_ - windowSize.x * 0.2f));
        }
    }

    // 掴めるものを探す（キー・イベントとも同じ判定でよい）
    int hitTrack = -1;
    int hitKey = -1;
    int hitEvent = -1;
    if (hovered && !inRuler)
    {
        for (int i = 0; i < static_cast<int>(tracks.size()) && hitTrack < 0; ++i)
        {
            const float centerY = rowTop(i) + trackRowHeight_ * 0.5f;
            if (std::fabs(mouse.y - centerY) > trackRowHeight_ * 0.5f)
                continue;
            const TimelineTrack &track = tracks[i];
            for (int k = 0; k < static_cast<int>(track.keys.size()); ++k)
            {
                if (std::fabs(mouse.x - timeToX(track.keys[k].time)) <= 7.0f)
                {
                    hitTrack = i;
                    hitKey = k;
                    break;
                }
            }
            if (hitTrack < 0)
            {
                for (int e = 0; e < static_cast<int>(track.events.size()); ++e)
                {
                    if (std::fabs(mouse.x - timeToX(track.events[e].time)) <= 7.0f)
                    {
                        hitTrack = i;
                        hitEvent = e;
                        break;
                    }
                }
            }
            if (hitKey >= 0 || hitEvent >= 0)
                hitTrack = i;
        }
    }

    // マウスが乗っているキー・イベントの中身をすぐ見せる
    if (hitTrack >= 0 && dragTarget_ == DragTarget::None)
    {
        const TimelineTrack &hoverTrack = tracks[hitTrack];
        if (hitKey >= 0)
            ImGui::SetTooltip("キー %.2f 秒\n次への繋ぎ方: %s", hoverTrack.keys[hitKey].time, EasingName(hoverTrack.keys[hitKey].easing));
        else if (hitEvent >= 0)
            ImGui::SetTooltip("%.2f 秒\n%s", hoverTrack.events[hitEvent].time,
                              hoverTrack.events[hitEvent].name.empty() ? "(未設定)" : hoverTrack.events[hitEvent].name.c_str());
    }
    // ダブルクリック: 再生位置をそのキーへ
    if (hitTrack >= 0 && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
    {
        const TimelineTrack &hitTrackRef = tracks[hitTrack];
        manager->SetTime(hitKey >= 0 ? hitTrackRef.keys[hitKey].time : hitTrackRef.events[hitEvent].time, false);
    }
    // 右クリック（キー・イベントの上）: メニュー
    if (hitTrack >= 0 && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
    {
        menuTrack_ = hitTrack;
        menuKey_ = hitKey;
        menuEvent_ = hitEvent;
        selectedTrack_ = hitTrack;
        selectedKey_ = hitKey;
        selectedEvent_ = hitEvent;
        ImGui::OpenPopup("##timelineKeyMenu");
    }
    if (ImGui::BeginPopup("##timelineKeyMenu"))
    {
        if (menuTrack_ >= 0 && menuTrack_ < static_cast<int>(tracks.size()))
        {
            TimelineTrack &menuTrack = tracks[menuTrack_];
            const bool isKey = menuKey_ >= 0 && menuKey_ < static_cast<int>(menuTrack.keys.size());
            const bool isEvent = menuEvent_ >= 0 && menuEvent_ < static_cast<int>(menuTrack.events.size());
            const float itemTime = isKey ? menuTrack.keys[menuKey_].time : (isEvent ? menuTrack.events[menuEvent_].time : 0.0f);
            ImGui::TextDisabled("%.2f 秒", itemTime);
            if (ImGui::MenuItem(ICON_FA_MAP_MARKER_ALT " 再生位置をここへ"))
                manager->SetTime(itemTime, false);
            if (isKey && ImGui::BeginMenu(ICON_FA_BEZIER_CURVE " 次への繋ぎ方"))
            {
                for (int i = 0; i <= static_cast<int>(EasingType::InOutBounce); ++i)
                {
                    if (ImGui::MenuItem(EasingName(i), nullptr, menuTrack.keys[menuKey_].easing == i))
                        menuTrack.keys[menuKey_].easing = i;
                }
                ImGui::EndMenu();
            }
            if (ImGui::MenuItem(ICON_FA_CLONE " 複製（再生位置に置く）"))
            {
                const float targetTime = SnapTime(manager->GetTime());
                if (isKey)
                {
                    TimelineKey copy = menuTrack.keys[menuKey_];
                    copy.time = targetTime;
                    menuTrack.keys.push_back(copy);
                }
                else if (isEvent)
                {
                    TimelineEvent copy = menuTrack.events[menuEvent_];
                    copy.time = targetTime;
                    menuTrack.events.push_back(copy);
                }
                SortTrack(menuTrack);
                selectedKey_ = -1;
                selectedEvent_ = -1;
            }
            ImGui::Separator();
            if (ImGui::MenuItem(ICON_FA_TRASH_ALT " 削除"))
                DeleteSelection();
        }
        ImGui::EndPopup();
    }

    if (hovered && !inRuler && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
        if (hitTrack >= 0)
        {
            selectedTrack_ = hitTrack;
            selectedKey_ = hitKey;
            selectedEvent_ = hitEvent;
            dragTarget_ = (hitKey >= 0) ? DragTarget::Key : DragTarget::Event;
            dragTrack_ = hitTrack;
            dragIndex_ = (hitKey >= 0) ? hitKey : hitEvent;
        }
        else
        {
            // 空いている行を押したら、そのトラックを選ぶだけにする
            const int row = static_cast<int>((mouse.y - (origin.y + kRulerHeight)) / trackRowHeight_);
            if (row >= 0 && row < static_cast<int>(tracks.size()))
            {
                selectedTrack_ = row;
                selectedKey_ = -1;
                selectedEvent_ = -1;
            }
        }
    }

    // 右クリックでその位置に追加する
    if (hovered && !inRuler && ImGui::IsMouseClicked(ImGuiMouseButton_Right) && hitTrack < 0)
    {
        const int row = static_cast<int>((mouse.y - (origin.y + kRulerHeight)) / trackRowHeight_);
        if (row >= 0 && row < static_cast<int>(tracks.size()))
        {
            TimelineTrack &track = tracks[row];
            const float time = std::clamp(SnapTime(xToTime(mouse.x)), 0.0f, duration);
            if (IsKeyframeTrack(track.type))
            {
                CaptureKeyFromTarget(row, time);
            }
            else
            {
                TimelineEvent event;
                event.time = time;
                track.events.push_back(event);
                SortTrack(track);
                selectedTrack_ = row;
                selectedKey_ = -1;
                selectedEvent_ = static_cast<int>(track.events.size()) - 1;
                for (int e = 0; e < static_cast<int>(track.events.size()); ++e)
                {
                    if (track.events[e].time == time)
                        selectedEvent_ = e;
                }
            }
        }
    }

    // ドラッグで時間を動かす
    if (dragTarget_ == DragTarget::Key || dragTarget_ == DragTarget::Event)
    {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            if (dragTrack_ >= 0 && dragTrack_ < static_cast<int>(tracks.size()))
            {
                TimelineTrack &track = tracks[dragTrack_];
                const float time = std::clamp(SnapTime(xToTime(mouse.x)), 0.0f, duration);
                if (dragTarget_ == DragTarget::Key && dragIndex_ < static_cast<int>(track.keys.size()))
                    track.keys[dragIndex_].time = time;
                else if (dragTarget_ == DragTarget::Event && dragIndex_ < static_cast<int>(track.events.size()))
                    track.events[dragIndex_].time = time;
            }
        }
        else
        {
            // 離した時点で並べ直し、選択が別のものへズレないよう番号を取り直す
            if (dragTrack_ >= 0 && dragTrack_ < static_cast<int>(tracks.size()))
            {
                TimelineTrack &track = tracks[dragTrack_];
                float draggedTime = 0.0f;
                if (dragTarget_ == DragTarget::Key && dragIndex_ < static_cast<int>(track.keys.size()))
                    draggedTime = track.keys[dragIndex_].time;
                else if (dragTarget_ == DragTarget::Event && dragIndex_ < static_cast<int>(track.events.size()))
                    draggedTime = track.events[dragIndex_].time;

                SortTrack(track);

                if (dragTarget_ == DragTarget::Key)
                {
                    for (int k = 0; k < static_cast<int>(track.keys.size()); ++k)
                    {
                        if (track.keys[k].time == draggedTime)
                            selectedKey_ = k;
                    }
                }
                else
                {
                    for (int e = 0; e < static_cast<int>(track.events.size()); ++e)
                    {
                        if (track.events[e].time == draggedTime)
                            selectedEvent_ = e;
                    }
                }
            }
            dragTarget_ = DragTarget::None;
        }
    }

    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
        ImGui::IsKeyPressed(ImGuiKey_Delete, false))
    {
        DeleteSelection();
    }
}

void TimelineEditor::CaptureKeyFromTarget(int trackIndex, float time)
{
    TimelineSequence &sequence = TimelineManager::GetInstance()->GetSequence();
    std::vector<TimelineTrack> &tracks = sequence.GetTracks();
    if (trackIndex < 0 || trackIndex >= static_cast<int>(tracks.size()))
        return;

    TimelineTrack &track = tracks[trackIndex];
    TimelineKey key;
    key.time = time;

    // 対象の今の姿勢をそのまま取り込む。数値を打たずに動きを作れるようにするのが狙い
    if (track.type == static_cast<int>(TimelineTrackType::Transform))
    {
        BaseObject *object = BaseObjectManager::GetInstance()->GetObjectByName(track.targetName);
        if (object && object->GetWorldTransform())
        {
            const WorldTransform *transform = object->GetWorldTransform();
            key.position = transform->translation_;
            key.scale = transform->scale_;
            key.rotation = transform->eulerRotation_;
        }
    }
    else if (track.type == static_cast<int>(TimelineTrackType::Camera))
    {
        if (Camera *camera = CameraManager::GetInstance()->Find(track.targetName))
        {
            const ViewProjection &viewProjection = camera->GetViewProjection();
            key.position = viewProjection.translation_;
            key.rotation = viewProjection.eulerRotation_;
            key.fovDegrees = camera->GetFovYDegrees();
        }
    }

    // 同じ時刻にすでにキーがあれば差し替える（押し間違いで増殖しないように）
    for (TimelineKey &existing : track.keys)
    {
        if (std::fabs(existing.time - time) < 0.001f)
        {
            const int easing = existing.easing;
            existing = key;
            existing.easing = easing;
            return;
        }
    }

    track.keys.push_back(key);
    SortTrack(track);
    selectedTrack_ = trackIndex;
    selectedEvent_ = -1;
    for (int k = 0; k < static_cast<int>(track.keys.size()); ++k)
    {
        if (std::fabs(track.keys[k].time - time) < 0.001f)
            selectedKey_ = k;
    }
}

void TimelineEditor::DeleteSelection()
{
    TimelineSequence &sequence = TimelineManager::GetInstance()->GetSequence();
    std::vector<TimelineTrack> &tracks = sequence.GetTracks();
    if (selectedTrack_ < 0 || selectedTrack_ >= static_cast<int>(tracks.size()))
        return;

    TimelineTrack &track = tracks[selectedTrack_];
    if (selectedKey_ >= 0 && selectedKey_ < static_cast<int>(track.keys.size()))
    {
        track.keys.erase(track.keys.begin() + selectedKey_);
        selectedKey_ = -1;
    }
    else if (selectedEvent_ >= 0 && selectedEvent_ < static_cast<int>(track.events.size()))
    {
        track.events.erase(track.events.begin() + selectedEvent_);
        selectedEvent_ = -1;
    }
}

void TimelineEditor::DrawTargetSelector(TimelineTrack &track)
{
    if (track.type == static_cast<int>(TimelineTrackType::Camera))
    {
        const std::vector<std::string> names = CameraManager::GetInstance()->GetCameraNames();
        ImGui::SetNextItemWidth(220.0f);
        if (ImGui::BeginCombo("対象カメラ", track.targetName.empty() ? "(未設定)" : track.targetName.c_str()))
        {
            for (const std::string &name : names)
            {
                if (ImGui::Selectable(name.c_str(), track.targetName == name))
                    track.targetName = name;
            }
            ImGui::EndCombo();
        }
        return;
    }

    const std::vector<std::string> names = BaseObjectManager::GetInstance()->GetSortedObjectNames();
    ImGui::SetNextItemWidth(220.0f);
    if (ImGui::BeginCombo("対象オブジェクト", track.targetName.empty() ? "(未設定)" : track.targetName.c_str()))
    {
        for (const std::string &name : names)
        {
            if (ImGui::Selectable(name.c_str(), track.targetName == name))
                track.targetName = name;
        }
        ImGui::EndCombo();
    }
}

void TimelineEditor::DrawInspector()
{
    TimelineManager *manager = TimelineManager::GetInstance();
    TimelineSequence &sequence = manager->GetSequence();
    std::vector<TimelineTrack> &tracks = sequence.GetTracks();

    if (selectedTrack_ < 0 || selectedTrack_ >= static_cast<int>(tracks.size()))
    {
        DimText("トラックを選ぶと、ここで中身を編集できます");
        return;
    }

    TimelineTrack &track = tracks[selectedTrack_];
    SectionHeader((std::string("[ ") + TrackTypeName(track.type) + " ]").c_str(), TrackTypeColor(track.type));

    ImGui::SetNextItemWidth(220.0f);
    ImGui::InputText("トラック名", &track.name);
    ImGui::SameLine();
    DrawTargetSelector(track);

    ImGui::Separator();

    if (IsKeyframeTrack(track.type))
    {
        if (ConfirmButton(ICON_FA_PLUS " 今の位置をキーにする", ImVec2(240.0f, 0.0f)))
            CaptureKeyFromTarget(selectedTrack_, manager->GetTime());
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("時間つまみを動かして、対象をギズモで置いてから押してください。\n"
                              "これを何度か繰り返すだけで動きができあがります");
        }
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
        ImGui::Text("キー %d 個", static_cast<int>(track.keys.size()));
        ImGui::PopStyleColor();

        if (selectedKey_ >= 0 && selectedKey_ < static_cast<int>(track.keys.size()))
        {
            TimelineKey &key = track.keys[selectedKey_];
            ImGui::Separator();
            ImGui::SetNextItemWidth(160.0f);
            if (ImGui::DragFloat("時間##key", &key.time, 0.01f, 0.0f, sequence.GetDuration(), "%.2f 秒"))
                SortTrack(track);

            ImGui::SetNextItemWidth(260.0f);
            ImGui::DragFloat3("位置", &key.position.x, 0.05f);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(260.0f);
            {
                constexpr float kToDeg = 180.0f / std::numbers::pi_v<float>;
                float degrees[3] = {key.rotation.x * kToDeg, key.rotation.y * kToDeg, key.rotation.z * kToDeg};
                if (ImGui::DragFloat3("回転(度)", degrees, 0.5f, -360.0f, 360.0f, "%.1f°"))
                    key.rotation = {degrees[0] / kToDeg, degrees[1] / kToDeg, degrees[2] / kToDeg};
            }

            if (track.type == static_cast<int>(TimelineTrackType::Transform))
            {
                ImGui::SetNextItemWidth(260.0f);
                ImGui::DragFloat3("大きさ", &key.scale.x, 0.01f, 0.001f, 100.0f);
            }
            else
            {
                ImGui::SetNextItemWidth(160.0f);
                ImGui::DragFloat("画角", &key.fovDegrees, 0.2f, 5.0f, 150.0f, "%.1f 度");
            }

            ImGui::SetNextItemWidth(200.0f);
            if (ImGui::BeginCombo("次への繋ぎ方", EasingName(key.easing)))
            {
                for (int i = 0; i <= static_cast<int>(EasingType::InOutBounce); ++i)
                {
                    if (ImGui::Selectable(EasingName(i), key.easing == i))
                        key.easing = i;
                }
                ImGui::EndCombo();
            }

            if (DangerButton(ICON_FA_TRASH_ALT " このキーを削除", ImVec2(200.0f, 0.0f)))
                DeleteSelection();
        }
        else
        {
            DimText("グリッド上のひし形をクリックすると、そのキーを編集できます");
        }
        return;
    }

    // --- 音・パーティクル・合図 ---
    if (ConfirmButton(ICON_FA_PLUS " 今の時間にイベントを追加", ImVec2(240.0f, 0.0f)))
    {
        TimelineEvent event;
        event.time = manager->GetTime();
        track.events.push_back(event);
        SortTrack(track);
        selectedEvent_ = static_cast<int>(track.events.size()) - 1;
    }
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
    ImGui::Text("イベント %d 個", static_cast<int>(track.events.size()));
    ImGui::PopStyleColor();

    if (selectedEvent_ < 0 || selectedEvent_ >= static_cast<int>(track.events.size()))
    {
        DimText("グリッド上の四角をクリックすると、その中身を編集できます");
        return;
    }

    TimelineEvent &event = track.events[selectedEvent_];
    ImGui::Separator();
    ImGui::SetNextItemWidth(160.0f);
    if (ImGui::DragFloat("時間##event", &event.time, 0.01f, 0.0f, sequence.GetDuration(), "%.2f 秒"))
        SortTrack(track);

    switch (static_cast<TimelineTrackType>(track.type))
    {
    case TimelineTrackType::Sound:
        ImGui::SetNextItemWidth(320.0f);
        ImGui::InputTextWithHint("音のパス", "player/playerJump.wav", &event.name);
        AssetDragDrop::SoundTarget(event.name);
        ImGui::SetItemTooltip("アセットブラウザから音をドラッグして入れられます");
        ImGui::SetNextItemWidth(200.0f);
        ImGui::SliderFloat("音量", &event.value, 0.0f, 1.0f, "%.2f");
        break;

    case TimelineTrackType::Particle: {
        const std::vector<std::string> templates = ParticleCSSpawner::GetTemplateNames();
        ImGui::SetNextItemWidth(320.0f);
        if (ImGui::BeginCombo("テンプレート", event.name.empty() ? "(未設定)" : event.name.c_str()))
        {
            for (const std::string &name : templates)
            {
                if (ImGui::Selectable(name.c_str(), event.name == name))
                    event.name = name;
            }
            ImGui::EndCombo();
        }
        AccentCheckbox("対象オブジェクトの位置に出す", &event.useTargetPosition, DebugTheme::kAccentOrange);
        if (!event.useTargetPosition)
        {
            ImGui::SetNextItemWidth(260.0f);
            ImGui::DragFloat3("出す位置", &event.position.x, 0.05f);
        }
        break;
    }

    case TimelineTrackType::Event:
        ImGui::SetNextItemWidth(320.0f);
        ImGui::InputTextWithHint("合図の名前", "damage / flash など", &event.name);
        ImGui::SetNextItemWidth(200.0f);
        ImGui::DragFloat("値", &event.value, 0.1f);
        DimText("TimelineManager::SetEventCallback で受け取れます");
        break;

    default:
        break;
    }

    if (DangerButton(ICON_FA_TRASH_ALT " このイベントを削除", ImVec2(200.0f, 0.0f)))
        DeleteSelection();
}

} // namespace Hagine
#endif // USE_IMGUI
