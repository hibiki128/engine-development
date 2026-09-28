#include "UIAnimator.h"
#ifdef USE_IMGUI
#include "SpriteManager.h"
#include "utility/debug/imgui/ImGuizmoManager.h"
// DebugUIHelper.h は ImGui:: を使うので imgui.h（ImGuizmoManager.h 経由）の後に include する
#include "utility/debug/imgui/DebugUIHelper.h"
#include <algorithm>
#include <format>
#include <icon/IconsFontAwesome5.h>
#include <cmath>
#include <set>
#include <tuple>

// UIエディタの見やすさの部品: クリップのタイムライン（プレビュー・途中の確認）とグループの階層表示。
// 本体の描画（UIAnimator::DrawImGui）から呼ぶ。

namespace Hagine {

namespace {
constexpr float kTimelineRowHeight = 18.0f;   // タイムラインの1行の高さ
constexpr float kRulerHeight = 20.0f; // 目盛りの高さ
constexpr float kLabelWidth = 150.0f; // 行の名前を書く幅
constexpr float kMinClipLength = 0.1f;

// チャンネルごとの帯の色
ImU32 ChannelColor(UIChannel channel)
{
    switch (channel)
    {
    case UIChannel::PositionX:
    case UIChannel::PositionY:
        return ImGui::GetColorU32(DebugTheme::kAccentBlue);
    case UIChannel::ScaleX:
    case UIChannel::ScaleY:
        return ImGui::GetColorU32(DebugTheme::kAccentGreen);
    case UIChannel::RotationZ:
        return ImGui::GetColorU32(DebugTheme::kAccentPurple);
    case UIChannel::Alpha:
    default:
        return ImGui::GetColorU32(DebugTheme::kAccentOrange);
    }
}

const char *ChannelShortName(UIChannel channel)
{
    static const char *const kNames[] = {"位置X", "位置Y", "拡大X", "拡大Y", "回転", "透明"};
    const int index = static_cast<int>(channel);
    return (index >= 0 && index < static_cast<int>(UIChannel::Count)) ? kNames[index] : "?";
}
} // namespace

float UIAnimator::ClipLength(const UIClip &clip) const
{
    float length = kMinClipLength;
    for (const UITween &tween : clip.tweens)
        length = std::max(length, tween.delay + std::max(0.0f, tween.duration));
    return length;
}

float UIAnimator::ClipTime(const UIClip &clip) const
{
    float time = 0.0f;
    for (const UITween &tween : clip.tweens)
        time = std::max(time, tween.elapsed_);
    return std::min(time, ClipLength(clip));
}

void UIAnimator::CapturePreviewSnapshot(const UIClip &clip)
{
    previewSnapshot_.clear();
    previewClip_ = clip.name;
    std::set<std::tuple<int, std::string, int>> seen;
    for (const UITween &tween : clip.tweens)
    {
        if (tween.targetName.empty())
            continue;
        const auto key = std::make_tuple(static_cast<int>(tween.targetKind), tween.targetName, static_cast<int>(tween.channel));
        if (!seen.insert(key).second)
            continue;
        previewSnapshot_.push_back({tween.targetKind, tween.targetName, tween.channel,
                                    GetChannelValue(tween.targetKind, tween.targetName, tween.channel)});
    }
}

void UIAnimator::RestorePreviewSnapshot()
{
    // 再生中なら止めてから、プレビュー前の値を書き戻す
    if (UIClip *clip = FindClip(previewClip_))
        clip->playing_ = false;
    for (const PreviewValue &value : previewSnapshot_)
        ApplyChannel(value.kind, value.target, value.channel, value.value);
    previewSnapshot_.clear();
    previewClip_.clear();
    ApplyGroups();
}

void UIAnimator::PreviewClipAt(UIClip &clip, float time)
{
    for (UITween &tween : clip.tweens)
    {
        if (tween.targetName.empty())
            continue;
        const float t = time - tween.delay;
        float value = tween.resolvedStart_;
        if (t >= 0.0f)
        {
            if (tween.duration <= 0.0f)
                value = tween.endValue;
            else
                value = ApplyEasing(tween.easing, tween.resolvedStart_, tween.endValue, std::clamp(t, 0.0f, tween.duration), tween.duration);
        }
        ApplyChannel(tween.targetKind, tween.targetName, tween.channel, value);
    }
    ApplyGroups();
}

bool UIAnimator::DrawClipTimeline(UIClip &clip)
{
    bool clicked = false;
    const float length = ClipLength(clip);

    // ---- 操作列: 再生・停止・プレビュー前に戻す ----
    if (PrimaryButton(ICON_FA_PLAY " 再生"))
    {
        if (previewClip_ != clip.name || previewSnapshot_.empty())
            CapturePreviewSnapshot(clip);
        Play(clip.name);
    }
    ImGui::SameLine();
    if (NeutralButton(ICON_FA_STOP " 停止"))
        Stop(clip.name);
    ImGui::SameLine();
    const bool hasSnapshot = previewClip_ == clip.name && !previewSnapshot_.empty();
    ImGui::BeginDisabled(!hasSnapshot);
    if (NeutralButton(ICON_FA_UNDO " プレビュー前に戻す"))
        RestorePreviewSnapshot();
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("再生・途中の確認で動いたスプライトを、プレビューする前の位置・大きさ・色へ戻す");
    ImGui::SameLine();
    ImGui::TextDisabled(clip.playing_ ? "再生中 %.2f / %.2f 秒" : "全体 %.2f 秒", clip.playing_ ? ClipTime(clip) : length, length);

    // ---- タイムライン ----
    const int rowCount = std::max(1, static_cast<int>(clip.tweens.size()));
    const float width = std::max(200.0f, ImGui::GetContentRegionAvail().x);
    const float height = kRulerHeight + kTimelineRowHeight * static_cast<float>(rowCount) + 4.0f;
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##timeline", ImVec2(width, height));
    const bool hovered = ImGui::IsItemHovered();
    const bool active = ImGui::IsItemActive();
    ImDrawList *drawList = ImGui::GetWindowDrawList();

    const float trackLeft = origin.x + kLabelWidth;
    const float trackWidth = std::max(40.0f, width - kLabelWidth - 6.0f);
    auto timeToX = [&](float time) { return trackLeft + (time / length) * trackWidth; };
    auto xToTime = [&](float x) { return std::clamp((x - trackLeft) / trackWidth, 0.0f, 1.0f) * length; };

    drawList->AddRectFilled(origin, ImVec2(origin.x + width, origin.y + height), ImGui::GetColorU32(ImGuiCol_FrameBg), 4.0f);

    // 目盛り（0.1秒刻みの細線・0.5秒ごとに数字）
    const float step = length > 4.0f ? 0.5f : 0.1f;
    for (float t = 0.0f; t <= length + 0.0001f; t += step)
    {
        const float x = timeToX(t);
        const bool major = std::fmod(t + 0.0001f, step * 5.0f) < 0.001f;
        drawList->AddLine(ImVec2(x, origin.y + (major ? 2.0f : 10.0f)), ImVec2(x, origin.y + kRulerHeight),
                          ImGui::GetColorU32(ImGuiCol_TextDisabled));
        if (major)
            drawList->AddText(ImVec2(x + 2.0f, origin.y + 1.0f), ImGui::GetColorU32(ImGuiCol_TextDisabled), std::format("{:.1f}", t).c_str());
    }

    // 各トゥイーンの帯（遅延〜遅延+秒数）
    if (clip.tweens.empty())
    {
        drawList->AddText(ImVec2(origin.x + 8.0f, origin.y + kRulerHeight + 1.0f), ImGui::GetColorU32(ImGuiCol_TextDisabled),
                          "トゥイーンがありません（下の「＋ トゥイーン追加」）");
    }
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    for (int i = 0; i < static_cast<int>(clip.tweens.size()); ++i)
    {
        const UITween &tween = clip.tweens[i];
        const float rowTop = origin.y + kRulerHeight + kTimelineRowHeight * static_cast<float>(i);
        if (i == selectedTween_)
        {
            drawList->AddRectFilled(ImVec2(origin.x, rowTop), ImVec2(origin.x + width, rowTop + kTimelineRowHeight),
                                    ImGui::GetColorU32(ImGuiCol_Header));
        }
        const std::string label = std::format("{}. {} {}", i + 1, tween.targetName.empty() ? "(未選択)" : tween.targetName.c_str(),
                                              ChannelShortName(tween.channel));
        drawList->PushClipRect(ImVec2(origin.x, rowTop), ImVec2(trackLeft - 4.0f, rowTop + kTimelineRowHeight), true);
        drawList->AddText(ImVec2(origin.x + 6.0f, rowTop + 1.0f), ImGui::GetColorU32(ImGuiCol_Text), label.c_str());
        drawList->PopClipRect();

        const float x0 = timeToX(tween.delay);
        const float x1 = std::max(x0 + 3.0f, timeToX(tween.delay + std::max(0.0f, tween.duration)));
        const ImVec2 barMin(x0, rowTop + 3.0f);
        const ImVec2 barMax(x1, rowTop + kTimelineRowHeight - 3.0f);
        drawList->AddRectFilled(barMin, barMax, ChannelColor(tween.channel), 3.0f);
        if (i == selectedTween_)
            drawList->AddRect(barMin, barMax, IM_COL32(255, 255, 255, 220), 3.0f, 0, 1.5f);

        // 行を押したらそのトゥイーンを選ぶ（目盛りの上を押したときは再生位置を動かす）
        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && mouse.y >= rowTop && mouse.y < rowTop + kTimelineRowHeight)
        {
            selectedTween_ = i;
            clicked = true;
        }
    }

    // 目盛りの上をつまんで動かすと、その時刻の姿をプレビューする
    const bool onRuler = mouse.y < origin.y + kRulerHeight;
    if (active && (scrubbing_ || (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && onRuler)))
    {
        if (!scrubbing_)
        {
            scrubbing_ = true;
            clip.playing_ = false;
            if (previewClip_ != clip.name || previewSnapshot_.empty())
                CapturePreviewSnapshot(clip);
            RewindClip(clip); // 「現在値から開始」の初期値を決めておく
        }
        scrubTime_ = xToTime(mouse.x);
        PreviewClipAt(clip, scrubTime_);
    }
    if (!active)
        scrubbing_ = false;

    // 再生位置の線
    const float headTime = clip.playing_ ? ClipTime(clip) : (hasSnapshot ? scrubTime_ : -1.0f);
    if (headTime >= 0.0f)
    {
        const float x = timeToX(headTime);
        drawList->AddLine(ImVec2(x, origin.y), ImVec2(x, origin.y + height), IM_COL32(255, 90, 90, 255), 2.0f);
    }
    if (hovered)
        ImGui::SetItemTooltip("目盛りの上をドラッグ: その時刻の姿を見る / 行を押す: そのトゥイーンを開く");
    return clicked;
}

void UIAnimator::DrawGroupTree()
{
    SpriteManager *sm = SpriteManager::GetInstance();
    std::set<std::string> grouped;
    for (int i = 0; i < static_cast<int>(groups_.size()); ++i)
    {
        UIGroup &g = groups_[i];
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_DefaultOpen;
        if (selectedGroup_ == i)
            flags |= ImGuiTreeNodeFlags_Selected;
        if (g.members.empty())
            flags |= ImGuiTreeNodeFlags_Leaf;
        const bool open = ImGui::TreeNodeEx(std::format("{} {} ({})##group{}", ICON_FA_LAYER_GROUP, g.name, g.members.size(), i).c_str(), flags);
        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
            selectedGroup_ = i;
        if (open)
        {
            for (const UIGroupMember &m : g.members)
            {
                grouped.insert(m.spriteName);
                const bool exists = sm->GetSprite(m.spriteName) != nullptr;
                ImGui::PushStyleColor(ImGuiCol_Text, exists ? ImGui::GetStyleColorVec4(ImGuiCol_Text) : DebugTheme::kAccentRed);
                ImGui::TreeNodeEx(std::format("{} {}##member{}_{}", ICON_FA_IMAGE, m.spriteName, i, m.spriteName).c_str(),
                                  ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_SpanAvailWidth);
                ImGui::PopStyleColor();
                if (ImGui::IsItemClicked())
                {
                    // メンバーを押したら、そのグループを選び、シーンでもそのスプライトを掴む
                    selectedGroup_ = i;
                    ImGuizmoManager::GetInstance()->SelectOnly(m.spriteName);
                }
                ImGui::SetItemTooltip(exists ? "相対位置 (%.0f, %.0f)。押すとシーンで掴めます" : "このスプライトは今のシーンにありません", m.offset.x, m.offset.y);
            }
            ImGui::TreePop();
        }
        else
        {
            for (const UIGroupMember &m : g.members)
                grouped.insert(m.spriteName);
        }
    }

    // どのグループにも入っていないスプライト
    std::vector<std::string> loose;
    for (const auto &sprite : sm->GetAllSprites())
    {
        if (sprite && !grouped.contains(sprite->name))
            loose.push_back(sprite->name);
    }
    if (!loose.empty() && ImGui::TreeNodeEx(std::format("{} グループ外 ({})##loose", ICON_FA_IMAGES, loose.size()).c_str(), ImGuiTreeNodeFlags_SpanAvailWidth))
    {
        for (const std::string &name : loose)
        {
            ImGui::TreeNodeEx(std::format("{}##loose_{}", name, name).c_str(),
                              ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_SpanAvailWidth);
            if (ImGui::IsItemClicked())
                ImGuizmoManager::GetInstance()->SelectOnly(name);
        }
        ImGui::TreePop();
    }
}

} // namespace Hagine
#endif // USE_IMGUI
