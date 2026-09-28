#include "MusicEditor.h"
#ifdef USE_IMGUI

#include <debug/imgui/DebugUIHelper.h>

#include <algorithm>
#include <cmath>

namespace Hagine {
namespace {

constexpr float kKeyColumnWidth = 64.0f;
constexpr float kRulerHeight = 22.0f;

/// ピアノロールの地の色
constexpr ImU32 kColorWhiteRow = IM_COL32(46, 48, 54, 255);
constexpr ImU32 kColorBlackRow = IM_COL32(37, 39, 44, 255);
constexpr ImU32 kColorGridFine = IM_COL32(58, 60, 68, 255);
constexpr ImU32 kColorGridBeat = IM_COL32(74, 78, 88, 255);
constexpr ImU32 kColorGridBar = IM_COL32(108, 114, 128, 255);
constexpr ImU32 kColorPlayhead = IM_COL32(240, 200, 100, 255);
constexpr ImU32 kColorLoopArea = IM_COL32(90, 150, 170, 26);

// 作曲アシストの色分け。キーに合う行だけを明るく、外れる行は暗く沈める
constexpr ImU32 kColorScaleRow = IM_COL32(52, 58, 68, 255);
constexpr ImU32 kColorScaleRootRow = IM_COL32(62, 72, 86, 255);
constexpr ImU32 kColorOutOfScaleRow = IM_COL32(31, 32, 37, 255);

} // namespace

int MusicEditor::GetGridTicks() const
{
    return std::max(1, MusicConst::kTicksPerBeat / std::max(1, gridDivision_));
}

int MusicEditor::SnapToGrid(double tick) const
{
    if (!snapEnabled_)
        return static_cast<int>(std::floor(tick));
    const int grid = GetGridTicks();
    return static_cast<int>(std::floor(tick / grid)) * grid;
}

void MusicEditor::DrawPianoRoll()
{
    MusicEngine *engine = MusicEngine::GetInstance();

    const int beatsPerBar = std::max(1, engine->GetBeatsPerBar());
    const int barTicks = beatsPerBar * MusicConst::kTicksPerBeat;
    int totalTicks = std::max(engine->GetSongEndTick(), engine->GetLoopEndTick());
    totalTicks = std::max(totalTicks, barTicks * 8);
    totalTicks = (totalTicks / barTicks + 4) * barTicks; // 後ろに4小節ぶん余白を作る

    const int noteRange = MusicConst::kHighestNote - MusicConst::kLowestNote + 1;

    ImGui::BeginChild("##pianoRoll", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders,
                      ImGuiWindowFlags_HorizontalScrollbar);

    RollLayout layout;
    layout.contentOrigin = ImGui::GetCursorScreenPos();
    layout.windowPos = ImGui::GetWindowPos();
    layout.windowSize = ImGui::GetWindowSize();
    layout.keyColumnWidth = kKeyColumnWidth;
    layout.rulerHeight = kRulerHeight;
    layout.pixelsPerTick = pixelsPerTick_;
    layout.rowHeight = rowHeight_;

    const ImVec2 contentSize(kKeyColumnWidth + static_cast<float>(totalTicks) * pixelsPerTick_,
                             kRulerHeight + static_cast<float>(noteRange) * rowHeight_);

    // 内容の大きさをここで申告しつつ、マウス操作もこのアイテムで受ける
    ImGui::InvisibleButton("##rollCanvas", contentSize,
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    const bool canvasHovered = ImGui::IsItemHovered();

    // 初回だけ、よく使う音域（C4 あたり）が真ん中に来るようにしておく
    if (!rollInitialized_)
    {
        const float target = static_cast<float>(MusicConst::kHighestNote - 72) * rowHeight_;
        ImGui::SetScrollY(std::max(0.0f, target));
        rollInitialized_ = true;
    }

    ImDrawList *drawList = ImGui::GetWindowDrawList();
    const ImVec2 gridClipMin(layout.windowPos.x + kKeyColumnWidth, layout.windowPos.y + kRulerHeight);
    const ImVec2 gridClipMax(layout.windowPos.x + layout.windowSize.x,
                             layout.windowPos.y + layout.windowSize.y);

    drawList->PushClipRect(gridClipMin, gridClipMax, true);
    DrawRollGrid(drawList, layout, totalTicks);
    DrawRollNotes(drawList, layout);

    // 再生位置
    const float playheadX = layout.TickToX(engine->GetPositionTicks());
    drawList->AddLine(ImVec2(playheadX, gridClipMin.y), ImVec2(playheadX, gridClipMax.y), kColorPlayhead, 1.5f);

    // 範囲選択中の枠
    if (dragMode_ == DragMode::BoxSelect)
    {
        const ImVec2 boxMin(std::min(boxSelectStart_.x, boxSelectEnd_.x),
                            std::min(boxSelectStart_.y, boxSelectEnd_.y));
        const ImVec2 boxMax(std::max(boxSelectStart_.x, boxSelectEnd_.x),
                            std::max(boxSelectStart_.y, boxSelectEnd_.y));
        drawList->AddRectFilled(boxMin, boxMax, IM_COL32(120, 170, 230, 40));
        drawList->AddRect(boxMin, boxMax, IM_COL32(150, 195, 245, 200));
    }
    drawList->PopClipRect();

    DrawRollRuler(drawList, layout, totalTicks);
    DrawRollKeyColumn(drawList, layout);

    HandleRollInput(layout, canvasHovered);

    // 再生位置を追いかける（画面から出そうになったら左寄せで送る）
    if (followPlayhead_ && engine->IsPlaying() && dragMode_ == DragMode::None)
    {
        const float viewLeft = layout.windowPos.x + kKeyColumnWidth;
        const float viewRight = layout.windowPos.x + layout.windowSize.x;
        if (playheadX < viewLeft || playheadX > viewRight - 40.0f)
        {
            const float contentLeft = layout.contentOrigin.x + ImGui::GetScrollX();
            const float wanted = contentLeft + kKeyColumnWidth +
                                 static_cast<float>(engine->GetPositionTicks()) * pixelsPerTick_ -
                                 (viewLeft + 60.0f);
            ImGui::SetScrollX(std::max(0.0f, wanted));
        }
    }

    ImGui::EndChild();
}

void MusicEditor::DrawRollGrid(ImDrawList *drawList, const RollLayout &layout, int totalTicks)
{
    MusicEngine *engine = MusicEngine::GetInstance();
    const int beatsPerBar = std::max(1, engine->GetBeatsPerBar());

    const float left = layout.TickToX(0.0);
    const float right = layout.TickToX(static_cast<double>(totalTicks));
    const float viewTop = layout.windowPos.y + layout.rulerHeight;
    const float viewBottom = layout.windowPos.y + layout.windowSize.y;
    const float viewLeft = layout.windowPos.x + layout.keyColumnWidth;
    const float viewRight = layout.windowPos.x + layout.windowSize.x;

    // 行（鍵盤の白黒に合わせて地の色を変える）
    // 作曲アシストが有効なときは、キーに合う音の行だけを明るくして「ここを使えば外れない」と分かるようにする
    const MusicTrack *selected = GetSelectedTrack();
    const bool pitched = !selected || !selected->GetInstrument() || selected->GetInstrument()->IsPitched();
    const bool useScaleColor = highlightScale_ && pitched;

    for (int note = MusicConst::kLowestNote; note <= MusicConst::kHighestNote; ++note)
    {
        const float y0 = layout.NoteToY(note);
        const float y1 = y0 + layout.rowHeight;
        if (y1 < viewTop || y0 > viewBottom)
            continue;

        ImU32 rowColor = IsBlackKey(note) ? kColorBlackRow : kColorWhiteRow;
        if (useScaleColor)
        {
            if (MusicTheory::IsInScale(note, scaleRoot_, scaleType_))
            {
                // 主音の行だけさらに強調して、キーの位置を見失わないようにする
                const bool isRoot = (((note - scaleRoot_) % 12) + 12) % 12 == 0;
                rowColor = isRoot ? kColorScaleRootRow : kColorScaleRow;
            }
            else
            {
                rowColor = kColorOutOfScaleRow;
            }
        }
        drawList->AddRectFilled(ImVec2(left, y0), ImVec2(right, y1), rowColor);
        // オクターブの境目（シ→ド）だけ線を入れて位置を見失わないようにする
        if ((note % 12) == 0)
            drawList->AddLine(ImVec2(left, y0 + layout.rowHeight), ImVec2(right, y0 + layout.rowHeight),
                              kColorGridBeat);
    }

    // ループ範囲
    if (engine->IsLoopEnabled())
    {
        const float loopLeft = layout.TickToX(engine->GetLoopStartTick());
        const float loopRight = layout.TickToX(engine->GetLoopEndTick());
        drawList->AddRectFilled(ImVec2(loopLeft, viewTop), ImVec2(loopRight, viewBottom), kColorLoopArea);
    }

    // 縦線。画面に入っている範囲だけ引く
    const int gridTicks = GetGridTicks();
    const int barTicks = beatsPerBar * MusicConst::kTicksPerBeat;
    const int firstTick = std::max(0, static_cast<int>(layout.XToTick(viewLeft) / gridTicks) * gridTicks);
    const int lastTick = std::min(totalTicks, static_cast<int>(layout.XToTick(viewRight)) + gridTicks);

    for (int tick = firstTick; tick <= lastTick; tick += gridTicks)
    {
        const float x = layout.TickToX(tick);
        ImU32 color = kColorGridFine;
        float thickness = 1.0f;
        if (tick % barTicks == 0)
        {
            color = kColorGridBar;
            thickness = 1.5f;
        }
        else if (tick % MusicConst::kTicksPerBeat == 0)
        {
            color = kColorGridBeat;
        }
        drawList->AddLine(ImVec2(x, viewTop), ImVec2(x, viewBottom), color, thickness);
    }
}

void MusicEditor::DrawRollNotes(ImDrawList *drawList, const RollLayout &layout)
{
    MusicEngine *engine = MusicEngine::GetInstance();

    const float viewTop = layout.windowPos.y + layout.rulerHeight;
    const float viewBottom = layout.windowPos.y + layout.windowSize.y;
    const float viewLeft = layout.windowPos.x + layout.keyColumnWidth;
    const float viewRight = layout.windowPos.x + layout.windowSize.x;

    // 他トラック → 選択中トラック の順に描いて、編集中のものが手前に来るようにする
    for (int pass = 0; pass < 2; ++pass)
    {
        for (int trackIndex = 0; trackIndex < engine->GetTrackCount(); ++trackIndex)
        {
            const bool isSelected = (trackIndex == selectedTrack_);
            if (pass == 0 && isSelected)
                continue;
            if (pass == 1 && !isSelected)
                continue;
            if (pass == 0 && !showOtherTracks_)
                continue;

            MusicTrack *track = engine->GetTrack(trackIndex);
            if (!track)
                continue;

            const float *color = track->GetColor();
            for (const MusicNote &note : track->GetNotes())
            {
                const float x0 = layout.TickToX(note.startTick);
                const float x1 = layout.TickToX(note.startTick + std::max(1, note.lengthTick));
                const float y0 = layout.NoteToY(note.note);
                const float y1 = y0 + layout.rowHeight - 1.0f;
                if (x1 < viewLeft || x0 > viewRight || y1 < viewTop || y0 > viewBottom)
                    continue;

                const float alpha = isSelected ? (0.45f + 0.55f * std::clamp(note.velocity, 0.0f, 1.0f)) : 0.22f;
                const ImU32 fill = ImGui::ColorConvertFloat4ToU32(ImVec4(color[0], color[1], color[2], alpha));
                drawList->AddRectFilled(ImVec2(x0, y0), ImVec2(std::max(x1, x0 + 2.0f), y1), fill, 2.0f);

                if (isSelected)
                {
                    const bool picked = selectedNotes_.count(note.id) > 0;
                    drawList->AddRect(ImVec2(x0, y0), ImVec2(std::max(x1, x0 + 2.0f), y1),
                                      picked ? IM_COL32(255, 255, 255, 230) : IM_COL32(20, 20, 24, 180),
                                      2.0f, 0, picked ? 2.0f : 1.0f);
                }
            }
        }
    }
}

void MusicEditor::DrawRollKeyColumn(ImDrawList *drawList, const RollLayout &layout)
{
    MusicTrack *track = GetSelectedTrack();
    const Instrument *instrument = track ? track->GetInstrument() : nullptr;

    const ImVec2 clipMin(layout.windowPos.x, layout.windowPos.y + layout.rulerHeight);
    const ImVec2 clipMax(layout.windowPos.x + layout.keyColumnWidth,
                         layout.windowPos.y + layout.windowSize.y);
    drawList->PushClipRect(clipMin, clipMax, true);
    drawList->AddRectFilled(clipMin, clipMax, IM_COL32(28, 29, 34, 255));

    for (int note = MusicConst::kLowestNote; note <= MusicConst::kHighestNote; ++note)
    {
        const float y0 = layout.NoteToY(note);
        const float y1 = y0 + layout.rowHeight - 1.0f;
        if (y1 < clipMin.y || y0 > clipMax.y)
            continue;

        const bool black = IsBlackKey(note);
        const bool sounding = (note >= 0 && note < 128) && noteRefCount_[note] > 0;
        ImU32 fill = black ? IM_COL32(38, 38, 44, 255) : IM_COL32(226, 228, 232, 255);
        if (sounding)
            fill = IM_COL32(110, 170, 235, 255);
        else if (note == hoveredNote_)
            fill = black ? IM_COL32(70, 78, 92, 255) : IM_COL32(198, 212, 232, 255);

        drawList->AddRectFilled(ImVec2(clipMin.x, y0), ImVec2(clipMax.x - 1.0f, y1), fill);
        drawList->AddLine(ImVec2(clipMin.x, y1), ImVec2(clipMax.x, y1), IM_COL32(20, 20, 24, 120));

        // 打楽器のトラックではパッド名を、それ以外はドの位置に音名を出す
        std::string label;
        if (instrument && !instrument->IsPitched())
            label = instrument->GetNoteLabel(note);
        else if ((note % 12) == 0)
            label = NoteName(note);

        if (!label.empty() && layout.rowHeight >= 10.0f)
        {
            drawList->AddText(ImVec2(clipMin.x + 4.0f, y0 + (layout.rowHeight - ImGui::GetTextLineHeight()) * 0.5f),
                              black ? IM_COL32(200, 200, 210, 255) : IM_COL32(60, 60, 70, 255), label.c_str());
        }
    }
    drawList->PopClipRect();
}

void MusicEditor::DrawRollRuler(ImDrawList *drawList, const RollLayout &layout, int totalTicks)
{
    MusicEngine *engine = MusicEngine::GetInstance();
    const int beatsPerBar = std::max(1, engine->GetBeatsPerBar());
    const int barTicks = beatsPerBar * MusicConst::kTicksPerBeat;

    const ImVec2 clipMin(layout.windowPos.x + layout.keyColumnWidth, layout.windowPos.y);
    const ImVec2 clipMax(layout.windowPos.x + layout.windowSize.x, layout.windowPos.y + layout.rulerHeight);
    drawList->PushClipRect(clipMin, clipMax, true);
    drawList->AddRectFilled(clipMin, clipMax, IM_COL32(32, 33, 38, 255));

    // ループ範囲の帯
    if (engine->IsLoopEnabled())
    {
        const float loopLeft = layout.TickToX(engine->GetLoopStartTick());
        const float loopRight = layout.TickToX(engine->GetLoopEndTick());
        drawList->AddRectFilled(ImVec2(loopLeft, clipMin.y), ImVec2(loopRight, clipMin.y + 4.0f),
                                IM_COL32(100, 175, 200, 220));
    }

    const int firstBar = std::max(0, static_cast<int>(layout.XToTick(clipMin.x) / barTicks));
    const int lastBar = std::min(totalTicks / barTicks, static_cast<int>(layout.XToTick(clipMax.x) / barTicks) + 1);

    for (int bar = firstBar; bar <= lastBar; ++bar)
    {
        const float x = layout.TickToX(bar * barTicks);
        drawList->AddLine(ImVec2(x, clipMin.y + 6.0f), ImVec2(x, clipMax.y), kColorGridBar);
        char label[16];
        snprintf(label, sizeof(label), "%d", bar + 1);
        drawList->AddText(ImVec2(x + 4.0f, clipMin.y + 4.0f), IM_COL32(180, 185, 195, 255), label);
    }

    // 再生位置の印
    const float playheadX = layout.TickToX(engine->GetPositionTicks());
    drawList->AddTriangleFilled(ImVec2(playheadX - 5.0f, clipMin.y + 4.0f),
                                ImVec2(playheadX + 5.0f, clipMin.y + 4.0f),
                                ImVec2(playheadX, clipMax.y), kColorPlayhead);
    drawList->PopClipRect();

    // 左上の角（鍵盤列とルーラーの交点）を塗って、下の内容が透けないようにする
    drawList->AddRectFilled(layout.windowPos,
                            ImVec2(layout.windowPos.x + layout.keyColumnWidth,
                                   layout.windowPos.y + layout.rulerHeight),
                            IM_COL32(28, 29, 34, 255));
}

void MusicEditor::HandleRollInput(const RollLayout &layout, bool canvasHovered)
{
    MusicEngine *engine = MusicEngine::GetInstance();
    MusicTrack *track = GetSelectedTrack();
    if (!track)
        return;

    const ImGuiIO &io = ImGui::GetIO();
    const ImVec2 mouse = ImGui::GetMousePos();

    const bool inRuler = canvasHovered && mouse.y < layout.windowPos.y + layout.rulerHeight &&
                         mouse.x >= layout.windowPos.x + layout.keyColumnWidth;
    const bool inKeys = canvasHovered && mouse.x < layout.windowPos.x + layout.keyColumnWidth &&
                        mouse.y >= layout.windowPos.y + layout.rulerHeight;
    const bool inGrid = canvasHovered && !inRuler && !inKeys;

    hoveredNote_ = inKeys ? layout.YToNote(mouse.y) : -1;

    // Ctrl + ホイールで横方向の拡大縮小。マウスの下の位置がずれないようスクロールも合わせる
    if (canvasHovered && io.KeyCtrl && io.MouseWheel != 0.0f)
    {
        const double tickAtMouse = layout.XToTick(mouse.x);
        const float contentLeft = layout.contentOrigin.x + ImGui::GetScrollX();
        pixelsPerTick_ = std::clamp(pixelsPerTick_ * (1.0f + io.MouseWheel * 0.15f), 0.08f, 4.0f);
        const float wanted = contentLeft + layout.keyColumnWidth +
                             static_cast<float>(tickAtMouse) * pixelsPerTick_ - mouse.x;
        ImGui::SetScrollX(std::max(0.0f, wanted));
    }

    // --- 鍵盤列: 音を確かめる ---
    if (inKeys && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        PreviewNote(layout.YToNote(mouse.y), playVelocity_);

    // --- 目盛り: 再生位置とループ範囲 ---
    if (inRuler && dragMode_ == DragMode::None)
    {
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            dragMode_ = DragMode::Playhead;
        else if (ImGui::IsMouseClicked(ImGuiMouseButton_Right))
        {
            dragMode_ = DragMode::LoopRange;
            dragStartTick_ = std::max(0, SnapToGrid(layout.XToTick(mouse.x)));
        }
    }
    if (dragMode_ == DragMode::Playhead)
    {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
            engine->SetPositionTicks(std::max(0, SnapToGrid(layout.XToTick(mouse.x))));
        else
            dragMode_ = DragMode::None;
        return;
    }
    if (dragMode_ == DragMode::LoopRange)
    {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Right))
        {
            const int current = std::max(0, SnapToGrid(layout.XToTick(mouse.x)));
            engine->SetLoopRange(std::min(dragStartTick_, current), std::max(dragStartTick_, current));
        }
        else
        {
            dragMode_ = DragMode::None;
        }
        return;
    }

    // --- ノートの当たり判定 ---
    const double tickAtMouse = layout.XToTick(mouse.x);
    const int noteAtMouse = layout.YToNote(mouse.y);

    bool hitFound = false;
    uint32_t hitId = 0;
    bool nearRightEdge = false;
    if (inGrid)
    {
        for (const MusicNote &note : track->GetNotes())
        {
            if (note.note != noteAtMouse)
                continue;
            const double end = note.startTick + std::max(1, note.lengthTick);
            if (tickAtMouse < note.startTick || tickAtMouse > end)
                continue;
            hitFound = true;
            hitId = note.id;
            nearRightEdge = (layout.TickToX(end) - mouse.x) <= 6.0f;
            break;
        }
        if (hitFound && nearRightEdge)
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    }

    // --- 押した瞬間 ---
    if (inGrid && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
        if (hitFound)
        {
            if (io.KeyCtrl)
            {
                if (selectedNotes_.count(hitId))
                    selectedNotes_.erase(hitId);
                else
                    selectedNotes_.insert(hitId);
            }
            else if (!selectedNotes_.count(hitId))
            {
                selectedNotes_.clear();
                selectedNotes_.insert(hitId);
            }

            if (nearRightEdge)
            {
                dragMode_ = DragMode::ResizeNote;
                dragNoteId_ = hitId;
            }
            else
            {
                dragMode_ = DragMode::MoveNotes;
                dragStartTick_ = SnapToGrid(tickAtMouse);
                dragStartNote_ = noteAtMouse;
                dragLastTickDelta_ = 0;
                dragLastNoteDelta_ = 0;
                PreviewNote(noteAtMouse, noteVelocity_);
            }
        }
        else
        {
            // 何もない所を押したらノートを置く。そのままドラッグすれば長さを決められる
            MusicEngine::ScopedLock lock;
            const int baseNote = std::clamp(noteAtMouse, MusicConst::kLowestNote, MusicConst::kHighestNote);
            const int startTick = std::max(0, SnapToGrid(tickAtMouse));
            const int length = std::max(1, defaultNoteLength_);

            // コードモードのときは、押した音を最低音にした和音をまとめて置く。
            // スケールの中だけで積むので、どこを押しても音楽的に成立する
            const bool pitched = !track->GetInstrument() || track->GetInstrument()->IsPitched();
            std::vector<int> noteNumbers;
            if (chordMode_ && pitched)
                noteNumbers = MusicTheory::BuildDiatonicChord(baseNote, scaleRoot_, scaleType_, chordNoteCount_);
            if (noteNumbers.empty())
                noteNumbers.push_back(baseNote);

            selectedNotes_.clear();
            for (int noteNumber : noteNumbers)
            {
                MusicNote note;
                note.note = noteNumber;
                note.startTick = startTick;
                note.lengthTick = length;
                note.velocity = noteVelocity_;
                const uint32_t id = track->AddNote(note);
                selectedNotes_.insert(id);
                // 長さ変更の対象は最低音にしておく（掴んだ位置と一致するので分かりやすい）
                if (noteNumber == noteNumbers.front())
                    dragNoteId_ = id;
            }

            dragMode_ = DragMode::ResizeNote;
            draggingNewNote_ = true;
            for (int noteNumber : noteNumbers)
                PreviewNote(noteNumber, noteVelocity_);
        }
    }
    else if (inGrid && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
    {
        if (hitFound)
        {
            MusicEngine::ScopedLock lock;
            track->RemoveNote(hitId);
            selectedNotes_.erase(hitId);
        }
        else
        {
            dragMode_ = DragMode::BoxSelect;
            boxSelectStart_ = mouse;
            boxSelectEnd_ = mouse;
        }
    }

    // --- ドラッグ中 ---
    if (dragMode_ == DragMode::MoveNotes)
    {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            const int tickDelta = SnapToGrid(tickAtMouse) - dragStartTick_;
            const int noteDelta = noteAtMouse - dragStartNote_;
            if (tickDelta != dragLastTickDelta_ || noteDelta != dragLastNoteDelta_)
            {
                const int applyTick = tickDelta - dragLastTickDelta_;
                const int applyNote = noteDelta - dragLastNoteDelta_;

                MusicEngine::ScopedLock lock;
                // 端に当たった時に選択がばらけないよう、動かせる量をまとめて詰める
                int allowedTick = applyTick;
                int allowedNote = applyNote;
                for (uint32_t id : selectedNotes_)
                {
                    const MusicNote *note = track->FindNote(id);
                    if (!note)
                        continue;
                    allowedTick = std::max(allowedTick, -note->startTick);
                    allowedNote = std::clamp(allowedNote, MusicConst::kLowestNote - note->note,
                                             MusicConst::kHighestNote - note->note);
                }
                for (uint32_t id : selectedNotes_)
                {
                    if (MusicNote *note = track->FindNote(id))
                    {
                        note->startTick = std::max(0, note->startTick + allowedTick);
                        note->note = std::clamp(note->note + allowedNote, MusicConst::kLowestNote,
                                                MusicConst::kHighestNote);
                    }
                }
                dragLastTickDelta_ += allowedTick;
                dragLastNoteDelta_ += allowedNote;
                if (allowedNote != 0)
                    PreviewNote(noteAtMouse, noteVelocity_);
            }
        }
        else
        {
            dragMode_ = DragMode::None;
        }
    }
    else if (dragMode_ == DragMode::ResizeNote)
    {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            MusicEngine::ScopedLock lock;
            if (MusicNote *note = track->FindNote(dragNoteId_))
            {
                const int grid = GetGridTicks();
                const int end = SnapToGrid(tickAtMouse) + grid;
                const int startTick = note->startTick;
                const int newLength = std::max(grid, end - startTick);
                note->lengthTick = newLength;

                // 和音として置いたときは、同じ位置から始まる音をまとめて伸ばす
                for (uint32_t id : selectedNotes_)
                {
                    if (id == dragNoteId_)
                        continue;
                    if (MusicNote *other = track->FindNote(id))
                    {
                        if (other->startTick == startTick)
                            other->lengthTick = newLength;
                    }
                }
            }
        }
        else
        {
            // ドラッグせずに置いただけなら、次からも同じ長さで置けるよう覚えておく
            if (draggingNewNote_)
            {
                if (const MusicNote *note = track->FindNote(dragNoteId_))
                    defaultNoteLength_ = std::max(1, note->lengthTick);
            }
            draggingNewNote_ = false;
            dragMode_ = DragMode::None;
        }
    }
    else if (dragMode_ == DragMode::BoxSelect)
    {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Right))
        {
            boxSelectEnd_ = mouse;
        }
        else
        {
            const float minX = std::min(boxSelectStart_.x, boxSelectEnd_.x);
            const float maxX = std::max(boxSelectStart_.x, boxSelectEnd_.x);
            const float minY = std::min(boxSelectStart_.y, boxSelectEnd_.y);
            const float maxY = std::max(boxSelectStart_.y, boxSelectEnd_.y);
            if (!io.KeyCtrl)
                selectedNotes_.clear();
            for (const MusicNote &note : track->GetNotes())
            {
                const float x0 = layout.TickToX(note.startTick);
                const float x1 = layout.TickToX(note.startTick + std::max(1, note.lengthTick));
                const float y0 = layout.NoteToY(note.note);
                const float y1 = y0 + layout.rowHeight;
                if (x1 >= minX && x0 <= maxX && y1 >= minY && y0 <= maxY)
                    selectedNotes_.insert(note.id);
            }
            dragMode_ = DragMode::None;
        }
    }
}

void MusicEditor::DeleteSelectedNotes()
{
    MusicTrack *track = GetSelectedTrack();
    if (!track || selectedNotes_.empty())
        return;

    MusicEngine::ScopedLock lock;
    for (uint32_t id : selectedNotes_)
        track->RemoveNote(id);
    selectedNotes_.clear();
}

} // namespace Hagine
#endif // USE_IMGUI
