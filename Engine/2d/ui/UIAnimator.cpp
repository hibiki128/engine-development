#include "UIAnimator.h"
#include "SpriteManager.h"
#include <algorithm>
#include <data/DataHandler.h>
#ifdef USE_IMGUI
#include <imgui.h>
#include <utility/debug/imgui/ImGuiNotification.h>
#include "utility/debug/imgui/ImGuizmoManager.h"
// DebugUIHelper.h は ImGui:: を使うので imgui.h の後に include する
#include "utility/debug/imgui/DebugUIHelper.h"
#include <cstdio>
#include <format>
#include <icon/IconsFontAwesome5.h>
#endif // USE_IMGUI
#include <string>

namespace Hagine {

#ifdef USE_IMGUI
namespace {
// EasingType の並びに対応する表示名（31種）
const char *kEasingNames[] = {
    "Linear",
    "InSine", "OutSine", "InOutSine",
    "InQuad", "OutQuad", "InOutQuad",
    "InCubic", "OutCubic", "InOutCubic",
    "InQuart", "OutQuart", "InOutQuart",
    "InQuint", "OutQuint", "InOutQuint",
    "InCirc", "OutCirc", "InOutCirc",
    "InExpo", "OutExpo", "InOutExpo",
    "InBack", "OutBack", "InOutBack",
    "InElastic", "OutElastic", "InOutElastic",
    "InBounce", "OutBounce", "InOutBounce"};
constexpr int kEasingCount = static_cast<int>(sizeof(kEasingNames) / sizeof(kEasingNames[0]));

// UIChannel の並びに対応する表示名（Count は含めない）
const char *kChannelNames[] = {"位置X", "位置Y", "スケールX", "スケールY", "回転Z", "不透明度"};
constexpr int kChannelCount = static_cast<int>(UIChannel::Count);

// UITargetKind の並びに対応する表示名
const char *kKindNames[] = {"スプライト", "グループ"};
} // namespace
#endif // USE_IMGUI

UIAnimator *UIAnimator::GetInstance()
{
    static UIAnimator instance;
    return &instance;
}

void UIAnimator::Initialize()
{
    Load();
}

void UIAnimator::EnsureLoaded()
{
    if (!loaded_)
    {
        Load();
    }
}

UIClip *UIAnimator::FindClip(const std::string &name)
{
    for (auto &c : clips_)
    {
        if (c.name == name)
            return &c;
    }
    return nullptr;
}

UIGroup *UIAnimator::FindGroup(const std::string &name)
{
    for (auto &g : groups_)
    {
        if (g.name == name)
            return &g;
    }
    return nullptr;
}

// ===================================================
// 再生API
// ===================================================

void UIAnimator::Play(const std::string &clipName)
{
    EnsureLoaded();
    UIClip *clip = FindClip(clipName);
    if (!clip)
    {
#ifdef USE_IMGUI
        ImGuiNotification::Post("UIクリップが見つかりません: " + clipName, {0.9f, 0.5f, 0.3f, 1.0f});
#endif // USE_IMGUI
        return;
    }
    RewindClip(*clip);
    clip->playing_ = true;
}

void UIAnimator::Stop(const std::string &clipName)
{
    UIClip *clip = FindClip(clipName);
    if (clip)
    {
        clip->playing_ = false;
    }
}

void UIAnimator::StopAll()
{
    for (auto &c : clips_)
    {
        c.playing_ = false;
    }
}

bool UIAnimator::IsPlaying(const std::string &clipName) const
{
    for (const auto &c : clips_)
    {
        if (c.name == clipName)
            return c.playing_;
    }
    return false;
}

// ===================================================
// 更新
// ===================================================

void UIAnimator::Update(float deltaTime)
{
    EnsureLoaded();

    for (auto &clip : clips_)
    {
        if (!clip.playing_)
            continue;

        bool allFinished = true;
        for (auto &tween : clip.tweens)
        {
            UpdateTween(tween, deltaTime);
            if (!tween.finished_)
                allFinished = false;
        }

        if (allFinished)
        {
            if (clip.loop)
                RewindClip(clip);
            else
                clip.playing_ = false;
        }
    }

    // グループの相対位置をメンバーへ反映（束としてまとまって動く）
    ApplyGroups();
}

void UIAnimator::RewindClip(UIClip &clip)
{
    for (auto &tween : clip.tweens)
    {
        tween.elapsed_ = 0.0f;
        tween.finished_ = false;
        tween.resolvedStart_ = tween.fromCurrent
                                   ? GetChannelValue(tween.targetKind, tween.targetName, tween.channel)
                                   : tween.startValue;
    }
}

void UIAnimator::UpdateTween(UITween &tween, float deltaTime)
{
    if (tween.finished_)
        return;

    tween.elapsed_ += deltaTime;
    const float t = tween.elapsed_ - tween.delay;

    // 遅延中は初期値を当てておく（開始前のちらつき防止）
    if (t < 0.0f)
    {
        ApplyChannel(tween.targetKind, tween.targetName, tween.channel, tween.resolvedStart_);
        return;
    }

    float value;
    if (tween.duration <= 0.0f)
    {
        // 秒数0は即時到達
        value = tween.endValue;
        tween.finished_ = true;
    }
    else
    {
        // ApplyEasing は total を超えると外挿するため、必ず [0,duration] にクランプしてから渡す
        const float clampedT = std::clamp(t, 0.0f, tween.duration);
        value = ApplyEasing(tween.easing, tween.resolvedStart_, tween.endValue, clampedT, tween.duration);
        if (t >= tween.duration)
            tween.finished_ = true;
    }

    ApplyChannel(tween.targetKind, tween.targetName, tween.channel, value);
}

// ===================================================
// チャンネル値の取得・適用
// ===================================================

float UIAnimator::GetChannelValue(UITargetKind kind, const std::string &target, UIChannel ch)
{
    SpriteManager *sm = SpriteManager::GetInstance();

    if (kind == UITargetKind::Group)
    {
        UIGroup *g = FindGroup(target);
        if (!g)
            return 0.0f;
        if (ch == UIChannel::PositionX)
            return g->origin.x;
        if (ch == UIChannel::PositionY)
            return g->origin.y;
        // その他は先頭メンバーの値を代表値として返す
        if (!g->members.empty())
            return GetChannelValue(UITargetKind::Sprite, g->members[0].spriteName, ch);
        return 0.0f;
    }

    // スプライト
    switch (ch)
    {
    case UIChannel::PositionX:
    case UIChannel::PositionY:
    case UIChannel::ScaleX:
    case UIChannel::ScaleY:
    case UIChannel::RotationZ:
    {
        InstanceSRT *s = sm->GetInstanceSRT(target, 0);
        if (!s)
            return (ch == UIChannel::ScaleX || ch == UIChannel::ScaleY) ? 1.0f : 0.0f;
        switch (ch)
        {
        case UIChannel::PositionX: return s->translation.x;
        case UIChannel::PositionY: return s->translation.y;
        case UIChannel::ScaleX:    return s->scale.x;
        case UIChannel::ScaleY:    return s->scale.y;
        case UIChannel::RotationZ: return s->rotation.z;
        default: return 0.0f;
        }
    }
    case UIChannel::Alpha:
    {
        SpriteData *d = sm->GetSprite(target);
        return (d && d->sprite) ? d->sprite->GetColor().w : 1.0f;
    }
    default:
        return 0.0f;
    }
}

void UIAnimator::ApplyChannel(UITargetKind kind, const std::string &target, UIChannel ch, float value)
{
    SpriteManager *sm = SpriteManager::GetInstance();

    if (kind == UITargetKind::Group)
    {
        UIGroup *g = FindGroup(target);
        if (!g)
            return;
        if (ch == UIChannel::PositionX)
        {
            g->origin.x = value; // ApplyGroups がメンバーへ反映する
        }
        else if (ch == UIChannel::PositionY)
        {
            g->origin.y = value;
        }
        else
        {
            // スケール・回転・不透明度は各メンバーへ同じ値を適用する
            for (auto &m : g->members)
                ApplyChannel(UITargetKind::Sprite, m.spriteName, ch, value);
        }
        return;
    }

    // スプライト
    switch (ch)
    {
    case UIChannel::PositionX:
    {
        InstanceSRT *s = sm->GetInstanceSRT(target, 0);
        if (s)
            sm->SetSpritePosition(target, {value, s->translation.y});
        break;
    }
    case UIChannel::PositionY:
    {
        InstanceSRT *s = sm->GetInstanceSRT(target, 0);
        if (s)
            sm->SetSpritePosition(target, {s->translation.x, value});
        break;
    }
    case UIChannel::ScaleX:
    {
        InstanceSRT *s = sm->GetInstanceSRT(target, 0);
        if (s)
        {
            Vector3 sc = s->scale;
            sc.x = value;
            sm->SetInstanceScale(target, 0, sc);
        }
        break;
    }
    case UIChannel::ScaleY:
    {
        InstanceSRT *s = sm->GetInstanceSRT(target, 0);
        if (s)
        {
            Vector3 sc = s->scale;
            sc.y = value;
            sm->SetInstanceScale(target, 0, sc);
        }
        break;
    }
    case UIChannel::RotationZ:
    {
        InstanceSRT *s = sm->GetInstanceSRT(target, 0);
        if (s)
        {
            Vector3 r = s->rotation;
            r.z = value;
            sm->SetInstanceRotation(target, 0, r);
        }
        break;
    }
    case UIChannel::Alpha:
    {
        SpriteData *d = sm->GetSprite(target);
        if (d && d->sprite)
        {
            Vector4 c = d->sprite->GetColor();
            c.w = value;
            sm->SetSpriteColor(target, c);
        }
        break;
    }
    default:
        break;
    }
}

void UIAnimator::ApplyGroups()
{
    SpriteManager *sm = SpriteManager::GetInstance();
    for (auto &g : groups_)
    {
        for (auto &m : g.members)
        {
            const InstanceSRT *current = sm->GetInstanceSRT(m.spriteName, 0);
            if (!current)
            {
                m.hasApplied_ = false; // 今のシーンに居ない。戻ってきたら置き直す
                continue;
            }
            // 前のフレームに置いた所から動いていたら、ギズモやトゥイーンで外から動かされた。
            // 毎フレーム原点+相対へ引き戻すと掴んでも動かせないので、動いた先を新しい相対位置にする
            if (m.hasApplied_ &&
                (current->translation.x != m.lastApplied_.x || current->translation.y != m.lastApplied_.y))
            {
                m.offset = {current->translation.x - g.origin.x, current->translation.y - g.origin.y};
            }
            const Vector2 pos = {g.origin.x + m.offset.x, g.origin.y + m.offset.y};
            sm->SetSpritePosition(m.spriteName, pos);
            m.lastApplied_ = pos;
            m.hasApplied_ = true;
        }
    }
}

// ===================================================
// 保存・読み込み
// ===================================================

void UIAnimator::Save()
{
    DataHandler data("UI", "UIAnimation");
    const json all = ToJson();
    data.Save("groups", all["groups"]);
    data.Save("clips", all["clips"]);
    data.Flush();

#ifdef USE_IMGUI
    savedState_ = all;
    ImGuiNotification::Post("UIアニメーションを保存しました", {0.2f, 0.8f, 0.2f, 1.0f});
#endif // USE_IMGUI
}

nlohmann::json UIAnimator::ToJson() const
{
    json groupsJson = json::array();
    for (const auto &g : groups_)
    {
        json gj;
        gj["name"] = g.name;
        gj["origin"] = g.origin;
        json members = json::array();
        for (const auto &m : g.members)
        {
            json mj;
            mj["sprite"] = m.spriteName;
            mj["offset"] = m.offset;
            members.push_back(mj);
        }
        gj["members"] = members;
        groupsJson.push_back(gj);
    }

    json clipsJson = json::array();
    for (const auto &c : clips_)
    {
        json cj;
        cj["name"] = c.name;
        cj["loop"] = c.loop;
        json tweens = json::array();
        for (const auto &t : c.tweens)
        {
            json tj;
            tj["targetKind"] = static_cast<int>(t.targetKind);
            tj["targetName"] = t.targetName;
            tj["channel"] = static_cast<int>(t.channel);
            tj["fromCurrent"] = t.fromCurrent;
            tj["start"] = t.startValue;
            tj["end"] = t.endValue;
            tj["duration"] = t.duration;
            tj["delay"] = t.delay;
            tj["easing"] = static_cast<int>(t.easing);
            tweens.push_back(tj);
        }
        cj["tweens"] = tweens;
        clipsJson.push_back(cj);
    }

    json all = json::object();
    all["groups"] = std::move(groupsJson);
    all["clips"] = std::move(clipsJson);
    return all;
}

void UIAnimator::Load()
{
    loaded_ = true; // 再入防止（EnsureLoaded から）

    DataHandler data("UI", "UIAnimation");

    groups_.clear();
    clips_.clear();

    json groupsJson = data.Load<json>("groups", json::array());
    if (groupsJson.is_array())
    {
        for (const auto &gj : groupsJson)
        {
            UIGroup g;
            g.name = gj.value("name", std::string());
            if (gj.contains("origin"))
                g.origin = gj.at("origin").get<Vector2>();
            if (gj.contains("members") && gj.at("members").is_array())
            {
                for (const auto &mj : gj.at("members"))
                {
                    UIGroupMember m;
                    m.spriteName = mj.value("sprite", std::string());
                    if (mj.contains("offset"))
                        m.offset = mj.at("offset").get<Vector2>();
                    g.members.push_back(m);
                }
            }
            groups_.push_back(g);
        }
    }

    json clipsJson = data.Load<json>("clips", json::array());
    if (clipsJson.is_array())
    {
        for (const auto &cj : clipsJson)
        {
            UIClip c;
            c.name = cj.value("name", std::string());
            c.loop = cj.value("loop", false);
            if (cj.contains("tweens") && cj.at("tweens").is_array())
            {
                for (const auto &tj : cj.at("tweens"))
                {
                    UITween t;
                    t.targetKind = static_cast<UITargetKind>(tj.value("targetKind", 0));
                    t.targetName = tj.value("targetName", std::string());
                    t.channel = static_cast<UIChannel>(tj.value("channel", 0));
                    t.fromCurrent = tj.value("fromCurrent", false);
                    t.startValue = tj.value("start", 0.0f);
                    t.endValue = tj.value("end", 0.0f);
                    t.duration = tj.value("duration", 0.5f);
                    t.delay = tj.value("delay", 0.0f);
                    t.easing = static_cast<EasingType>(tj.value("easing", 0));
                    c.tweens.push_back(t);
                }
            }
            clips_.push_back(c);
        }
    }
#ifdef USE_IMGUI
    savedState_ = ToJson();
#endif // USE_IMGUI
}

#ifdef USE_IMGUI
// ===================================================
// エディタUI
// ===================================================

namespace {
constexpr float kRadToDeg = 180.0f / 3.14159265358979f;

/// <summary>チャンネルごとの見せ方（回転は度で見せ、内部はラジアンのまま持つ）</summary>
struct ChannelDisplay
{
    float scale;        // 内部値 → 表示値の倍率
    float speed;        // ドラッグの速さ（表示値で）
    const char *format; // 表示の書式
    float min;          // 下限（min == max なら制限なし）
    float max;          // 上限
};

ChannelDisplay GetChannelDisplay(UIChannel channel)
{
    switch (channel)
    {
    case UIChannel::PositionX:
    case UIChannel::PositionY:
        return {1.0f, 1.0f, "%.0f px", 0.0f, 0.0f};
    case UIChannel::ScaleX:
    case UIChannel::ScaleY:
        return {1.0f, 0.01f, "%.2f 倍", 0.0f, 0.0f};
    case UIChannel::RotationZ:
        return {kRadToDeg, 1.0f, "%.0f 度", 0.0f, 0.0f};
    case UIChannel::Alpha:
    default:
        return {1.0f, 0.01f, "%.2f", 0.0f, 1.0f};
    }
}

/// <summary>チャンネルの単位で値を文字にする（一覧の要約用）</summary>
std::string FormatChannelValue(UIChannel channel, float value)
{
    const ChannelDisplay display = GetChannelDisplay(channel);
    char buffer[64]{};
    std::snprintf(buffer, sizeof(buffer), display.format, value * display.scale);
    return buffer;
}

/// <summary>チャンネルの単位で値を編集する欄</summary>
bool ChannelValueField(const char *id, UIChannel channel, float &value)
{
    const ChannelDisplay display = GetChannelDisplay(channel);
    float shown = value * display.scale;
    const ImGuiSliderFlags flags = (display.max > display.min) ? ImGuiSliderFlags_AlwaysClamp : ImGuiSliderFlags_None;
    if (ImGui::DragFloat(id, &shown, display.speed, display.min, display.max, display.format, flags))
    {
        value = shown / display.scale;
        return true;
    }
    return false;
}

/// <summary>左にラベル、右に入力欄を並べる行の頭（入力欄の幅は残り全部）</summary>
void RowLabel(const char *label)
{
    const float labelX = ImGui::GetCursorPosX();
    ImGui::AlignTextToFramePadding();
    ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
    ImGui::TextUnformatted(label);
    ImGui::PopStyleColor();
    ImGui::SameLine(labelX + LabelColumnWidth());
    ImGui::SetNextItemWidth(-FLT_MIN);
}

/// <summary>イージングの形を小さなグラフで描く（横が時間、縦が進み具合）</summary>
void EasingCurve(const char *id, EasingType easing, const ImVec2 &size)
{
    const ImVec2 min = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton(id, size);
    ImDrawList *drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(min, ImVec2(min.x + size.x, min.y + size.y), ImGui::GetColorU32(ImGuiCol_FrameBg), 3.0f);

    // Back や Elastic は 0〜1 をはみ出すので、はみ出した分も入るよう縦の範囲を広げる
    constexpr int kSamples = 48;
    float values[kSamples + 1];
    float low = 0.0f;
    float high = 1.0f;
    for (int i = 0; i <= kSamples; ++i)
    {
        const float t = static_cast<float>(i) / kSamples;
        values[i] = ApplyEasing(easing, 0.0f, 1.0f, t, 1.0f);
        low = std::min(low, values[i]);
        high = std::max(high, values[i]);
    }
    const float padding = 3.0f;
    auto toPoint = [&](int i) {
        const float x = min.x + padding + (size.x - padding * 2.0f) * (static_cast<float>(i) / kSamples);
        const float y = min.y + size.y - padding - (size.y - padding * 2.0f) * ((values[i] - low) / (high - low));
        return ImVec2(x, y);
    };
    // 0 と 1 の目安線
    for (float guide : {0.0f, 1.0f})
    {
        const float y = min.y + size.y - padding - (size.y - padding * 2.0f) * ((guide - low) / (high - low));
        drawList->AddLine(ImVec2(min.x + padding, y), ImVec2(min.x + size.x - padding, y), ImGui::GetColorU32(ImGuiCol_TextDisabled, 0.4f));
    }
    for (int i = 0; i < kSamples; ++i)
    {
        drawList->AddLine(toPoint(i), toPoint(i + 1), ImGui::GetColorU32(DebugTheme::kAccentBlue), 1.5f);
    }
}

// 対象名のコンボ（スプライト名一覧またはグループ名一覧から選ぶ）
bool TargetNameCombo(const char *label, UITargetKind kind, std::string &target,
                     const std::vector<UIGroup> &groups)
{
    bool changed = false;
    const char *preview = target.empty() ? "(選んでください)" : target.c_str();
    if (ImGui::BeginCombo(label, preview, ImGuiComboFlags_HeightLarge))
    {
        if (kind == UITargetKind::Group)
        {
            if (groups.empty())
            {
                ImGui::TextDisabled("グループがありません（「グループ」タブで作れます）");
            }
            for (const auto &g : groups)
            {
                const bool sel = (g.name == target);
                if (ImGui::Selectable(g.name.c_str(), sel))
                {
                    target = g.name;
                    changed = true;
                }
            }
        }
        else
        {
            for (SpriteData *sd : SpriteManager::GetInstance()->GetAllSprites())
            {
                if (!sd)
                    continue;
                const bool sel = (sd->name == target);
                if (ImGui::Selectable(sd->name.c_str(), sel))
                {
                    target = sd->name;
                    changed = true;
                }
            }
        }
        ImGui::EndCombo();
    }
    return changed;
}

/// <summary>名前が既に使われていたら _2, _3 … を付けて重ならない名前にする</summary>
template <typename Exists>
std::string MakeUniqueName(const std::string &base, Exists exists)
{
    if (!exists(base))
    {
        return base;
    }
    for (int index = 2;; ++index)
    {
        const std::string candidate = base + "_" + std::to_string(index);
        if (!exists(candidate))
        {
            return candidate;
        }
    }
}

/// <summary>直前のアイテムの右へ、窓の右端に寄せて次のアイテムを置く（重なるときは直後に置く）</summary>
void SameLineRightAligned(float width)
{
    const float lastEnd = ImGui::GetItemRectMax().x - ImGui::GetWindowPos().x + ImGui::GetStyle().ItemSpacing.x;
    ImGui::SameLine(std::max(lastEnd, ImGui::GetContentRegionMax().x - width));
}

/// <summary>シーンで今選んでいるスプライトの名前（無ければ空）</summary>
std::vector<std::string> PickedSpriteNames()
{
    std::vector<std::string> names;
    SpriteManager *sm = SpriteManager::GetInstance();
    for (const std::string &name : ImGuizmoManager::GetInstance()->GetSelectedNames())
    {
        if (sm->GetSprite(name))
        {
            names.push_back(name);
        }
    }
    std::sort(names.begin(), names.end());
    return names;
}
} // namespace

std::string UIAnimator::SuggestTweenTarget(const UIClip &clip) const
{
    // シーンで掴んでいるスプライトが最優先（「これを動かしたい」と思って選んでいるはず）
    const std::vector<std::string> picked = PickedSpriteNames();
    if (!picked.empty())
    {
        return picked.front();
    }
    // 次は直前のトゥイーンと同じ相手（同じ物の X と Y を続けて足すことが多い）
    if (!clip.tweens.empty())
    {
        return clip.tweens.back().targetName;
    }
    return std::string();
}

void UIAnimator::DrawImGui(bool *open)
{
    EnsureLoaded();

    if (!ImGui::Begin("UIエディタ", open, ImGuiWindowFlags_NoFocusOnAppearing))
    {
        ImGui::End();
        return;
    }

    // ---- ツールバー: 保存・保存した状態へ戻す・未保存の印 ----
    const bool dirty = (ToJson() != savedState_);
    const bool savePressed = dirty ? ConfirmButton(ICON_FA_SAVE " 保存") : NeutralButton(ICON_FA_SAVE " 保存");
    if (savePressed)
    {
        Save();
    }
    ImGui::SetItemTooltip("jsons/UI/UIAnimation.json へ保存（全シーン共通）");
    ImGui::SameLine();
    ImGui::BeginDisabled(!dirty);
    if (NeutralButton(ICON_FA_UNDO " 保存した状態へ戻す"))
    {
        ImGui::OpenPopup("保存した状態へ戻す##uiRevert");
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (dirty)
    {
        StatusBadge("未保存の変更あり", DebugTheme::kAccentOrange);
    }
    else
    {
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("保存済み");
    }

    if (ImGui::BeginPopupModal("保存した状態へ戻す##uiRevert", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::TextUnformatted("保存していない変更を捨てて、保存した内容を読み直しますか？");
        ImGui::Separator();
        if (DangerButton("戻す", ImVec2(120.0f, 0.0f)))
        {
            StopAll();
            selectedGroup_ = -1;
            selectedClip_ = -1;
            selectedTween_ = -1;
            Load();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (NeutralButton("キャンセル", ImVec2(120.0f, 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Escape))
        {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    // このエディタで扱えるのは SpriteManager に登録済み(所有)のスプライトだけ。
    // 一覧が空になる主因（未登録・外部所有）を明示して迷わせない
    const int spriteCount = static_cast<int>(SpriteManager::GetInstance()->GetAllSprites().size());
    if (spriteCount == 0)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kAccentOrange);
        ImGui::TextWrapped("動かせるスプライトがまだありません。スプライトマネージャの「新規作成」で作るか、"
                           "アセットブラウザから画像をシーンへドラッグしてください。");
        ImGui::PopStyleColor();
    }

    ImGui::Separator();
    if (ImGui::BeginTabBar("##UIEditorTabs"))
    {
        if (ImGui::BeginTabItem(ICON_FA_PLAY_CIRCLE " クリップ"))
        {
            DrawClipTab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem(ICON_FA_LAYER_GROUP " グループ"))
        {
            DrawGroupTab();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    ImGui::End();
}

// ============================================================
// グループ
// ============================================================

void UIAnimator::DrawGroupTab()
{
    DimText("いくつかのスプライトを束ねて、相対位置を保ったまま一緒に動かします");

    // ---- 作る ----
    ImGui::SetNextItemWidth(220.0f);
    const bool enter = ImGui::InputTextWithHint("##newGroup", "新しいグループ名", &newGroupName_,
                                                ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    const bool nameTaken = FindGroup(newGroupName_) != nullptr;
    const bool canCreate = !newGroupName_.empty() && !nameTaken;
    const bool pressed = canCreate ? ConfirmButton(ICON_FA_PLUS " グループを作る") : NeutralButton(ICON_FA_PLUS " グループを作る");
    if (canCreate && (pressed || enter))
    {
        UIGroup group;
        group.name = newGroupName_;
        // シーンでスプライトを選んでいたら、それをそのままメンバーにする（選ぶ → 作る の流れで済むように）。
        // 既に他のグループに入っている物は入れない（2つのグループが位置を取り合ってしまう）
        for (const std::string &name : PickedSpriteNames())
        {
            bool grouped = false;
            for (const UIGroup &other : groups_)
            {
                for (const UIGroupMember &member : other.members)
                    grouped = grouped || (member.spriteName == name);
            }
            if (grouped)
                continue;
            if (InstanceSRT *s = SpriteManager::GetInstance()->GetInstanceSRT(name, 0))
            {
                UIGroupMember member;
                member.spriteName = name;
                member.offset = {s->translation.x, s->translation.y};
                group.members.push_back(member);
            }
        }
        groups_.push_back(group);
        selectedGroup_ = static_cast<int>(groups_.size()) - 1;
        newGroupName_.clear();
    }
    ImGui::SetItemTooltip("シーンでスプライトを選んでから作ると、それがそのままメンバーになります");
    if (nameTaken)
    {
        ImGui::TextColored(DebugTheme::kAccentOrange, "同じ名前のグループがあります");
    }

    // ---- 一覧（左）と中身（右）----
    ImGui::BeginChild("##GroupList", ImVec2(230.0f, 0.0f), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX);
    if (groups_.empty())
    {
        ImGui::TextDisabled("グループがありません");
    }
    DrawGroupTree();
    ImGui::EndChild();

    ImGui::SameLine();

    ImGui::BeginChild("##GroupDetail", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders);
    if (selectedGroup_ >= 0 && selectedGroup_ < static_cast<int>(groups_.size()))
    {
        DrawGroupDetail(groups_[selectedGroup_]);
    }
    else
    {
        ImGui::TextDisabled("左の一覧からグループを選ぶか、上で作ってください");
    }
    ImGui::EndChild();

    // ---- 削除の確認 ----
    // 削除ボタンは子ウィンドウの中にあって ID の階層が違うので、開くのはここで一度だけ行う
    if (deleteGroupRequest_ >= 0 && !ImGui::IsPopupOpen("グループを消す##uiGroupDelete"))
    {
        ImGui::OpenPopup("グループを消す##uiGroupDelete");
    }
    if (ImGui::BeginPopupModal("グループを消す##uiGroupDelete", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        const bool valid = deleteGroupRequest_ >= 0 && deleteGroupRequest_ < static_cast<int>(groups_.size());
        if (valid)
        {
            const std::string &name = groups_[deleteGroupRequest_].name;
            ImGui::Text("グループ「%s」を消しますか？（メンバーのスプライトは消えません）", name.c_str());
            // このグループを動かしているトゥイーンがあると、消した後は空振りになる
            int users = 0;
            for (const UIClip &clip : clips_)
            {
                for (const UITween &tween : clip.tweens)
                {
                    if (tween.targetKind == UITargetKind::Group && tween.targetName == name)
                        ++users;
                }
            }
            if (users > 0)
            {
                ImGui::TextColored(DebugTheme::kAccentOrange, "このグループを動かすトゥイーンが %d 本あります", users);
            }
        }
        ImGui::Separator();
        if (DangerButton("消す", ImVec2(120.0f, 0.0f)))
        {
            if (valid)
            {
                groups_.erase(groups_.begin() + deleteGroupRequest_);
                selectedGroup_ = -1;
            }
            deleteGroupRequest_ = -1;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (NeutralButton("キャンセル", ImVec2(120.0f, 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Escape))
        {
            deleteGroupRequest_ = -1;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    else
    {
        deleteGroupRequest_ = -1;
    }
}

void UIAnimator::DrawGroupDetail(UIGroup &group)
{
    SpriteManager *sm = SpriteManager::GetInstance();

    ImGui::AlignTextToFramePadding();
    ImGui::Text(ICON_FA_LAYER_GROUP " %s", group.name.c_str());
    SameLineRightAligned(80.0f);
    if (DangerButton(ICON_FA_TRASH_ALT " 削除"))
    {
        deleteGroupRequest_ = selectedGroup_;
    }

    RowLabel("原点");
    ImGui::DragFloat2("##origin", &group.origin.x, 1.0f, 0.0f, 0.0f, "%.0f");
    ImGui::SetItemTooltip("動かすとメンバー全員が相対位置を保ったまま一緒に動きます");

    RowLabel("");
    if (ImGui::SmallButton("原点をメンバーの中心へ") && !group.members.empty())
    {
        // 見た目は動かさず、原点だけをメンバーの真ん中へ移す（トゥイーンで拡大・移動させるときの基準にしやすい）
        Vector2 low = {FLT_MAX, FLT_MAX};
        Vector2 high = {-FLT_MAX, -FLT_MAX};
        for (const UIGroupMember &member : group.members)
        {
            const Vector2 position = {group.origin.x + member.offset.x, group.origin.y + member.offset.y};
            low = {std::min(low.x, position.x), std::min(low.y, position.y)};
            high = {std::max(high.x, position.x), std::max(high.y, position.y)};
        }
        const Vector2 center = {(low.x + high.x) * 0.5f, (low.y + high.y) * 0.5f};
        for (UIGroupMember &member : group.members)
        {
            member.offset = {group.origin.x + member.offset.x - center.x, group.origin.y + member.offset.y - center.y};
        }
        group.origin = center;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("今の位置を相対に取り込む"))
    {
        for (UIGroupMember &member : group.members)
        {
            if (InstanceSRT *s = sm->GetInstanceSRT(member.spriteName, 0))
                member.offset = {s->translation.x - group.origin.x, s->translation.y - group.origin.y};
        }
    }

    ImGui::Spacing();
    SectionHeader(std::format("メンバー（{} 個）", group.members.size()).c_str(), DebugTheme::kAccentBlue);

    int removeIndex = -1;
    if (!group.members.empty() &&
        ImGui::BeginTable("##members", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH))
    {
        ImGui::TableSetupColumn("名前", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("相対位置", ImGuiTableColumnFlags_WidthFixed, 150.0f);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFrameHeight());
        for (int i = 0; i < static_cast<int>(group.members.size()); ++i)
        {
            UIGroupMember &member = group.members[i];
            ImGui::PushID(i);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            const bool exists = sm->GetSprite(member.spriteName) != nullptr;
            if (!exists)
                ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kAccentRed);
            if (ImGui::Selectable(member.spriteName.c_str(), false))
                ImGuizmoManager::GetInstance()->SelectOnly(member.spriteName);
            if (!exists)
                ImGui::PopStyleColor();
            ImGui::SetItemTooltip(exists ? "クリックでシーンでも掴む" : "このスプライトは今のシーンにありません");
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::DragFloat2("##offset", &member.offset.x, 1.0f, 0.0f, 0.0f, "%.0f");
            ImGui::TableNextColumn();
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
            if (ImGui::Button(ICON_FA_TIMES "##remove", ImVec2(ImGui::GetFrameHeight(), 0.0f)))
                removeIndex = i;
            ImGui::PopStyleColor();
            ImGui::SetItemTooltip("グループから外す（スプライトは消えません）");
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (removeIndex >= 0)
    {
        group.members.erase(group.members.begin() + removeIndex);
    }

    // ---- メンバーを足す ----
    // 2つのグループに同じスプライトが入ると、毎フレーム取り合って位置が決まらない。
    // どのグループにも入っていないスプライトだけを候補に出す
    std::vector<std::string> candidates;
    for (SpriteData *sprite : sm->GetAllSprites())
    {
        bool grouped = false;
        for (const UIGroup &other : groups_)
        {
            for (const UIGroupMember &member : other.members)
                grouped = grouped || (member.spriteName == sprite->name);
        }
        if (!grouped)
            candidates.push_back(sprite->name);
    }
    auto addMember = [&](const std::string &name) {
        if (std::find(candidates.begin(), candidates.end(), name) == candidates.end())
            return; // 他のグループに入っている
        UIGroupMember member;
        member.spriteName = name;
        if (InstanceSRT *s = sm->GetInstanceSRT(name, 0))
            member.offset = {s->translation.x - group.origin.x, s->translation.y - group.origin.y};
        group.members.push_back(member);
    };

    ImGui::Spacing();
    ImGui::SetNextItemWidth(200.0f);
    if (ImGui::BeginCombo("##addMember", addMemberTarget_.empty() ? "(足すスプライト)" : addMemberTarget_.c_str()))
    {
        if (candidates.empty())
            ImGui::TextDisabled("どのグループにも入っていないスプライトがありません");
        for (const std::string &name : candidates)
        {
            if (ImGui::Selectable(name.c_str(), name == addMemberTarget_))
                addMemberTarget_ = name;
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (PrimaryButton(ICON_FA_PLUS " 足す") && !addMemberTarget_.empty())
    {
        addMember(addMemberTarget_);
        addMemberTarget_.clear();
    }
    ImGui::SameLine();
    const std::vector<std::string> picked = PickedSpriteNames();
    ImGui::BeginDisabled(picked.empty());
    if (NeutralButton(std::format(ICON_FA_MOUSE_POINTER " シーンで選択中を足す（{}）", picked.size()).c_str()))
    {
        for (const std::string &name : picked)
            addMember(name);
    }
    ImGui::EndDisabled();
    DimText("メンバーはシーンのギズモでも動かせます（相対位置が自動で変わります）");
}

// ============================================================
// クリップ
// ============================================================

void UIAnimator::DrawClipTab()
{
    DimText("トゥイーン（値を A → B へなめらかに変える指示）をまとめた物。コードから名前で再生します");

    // ---- 作る ----
    ImGui::SetNextItemWidth(220.0f);
    const bool enter = ImGui::InputTextWithHint("##newClip", "新しいクリップ名", &newClipName_,
                                                ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    const bool nameTaken = FindClip(newClipName_) != nullptr;
    const bool canCreate = !newClipName_.empty() && !nameTaken;
    const bool pressed = canCreate ? ConfirmButton(ICON_FA_PLUS " クリップを作る") : NeutralButton(ICON_FA_PLUS " クリップを作る");
    if (canCreate && (pressed || enter))
    {
        UIClip clip;
        clip.name = newClipName_;
        clips_.push_back(clip);
        selectedClip_ = static_cast<int>(clips_.size()) - 1;
        selectedTween_ = -1;
        newClipName_.clear();
    }
    if (nameTaken)
    {
        ImGui::TextColored(DebugTheme::kAccentOrange, "同じ名前のクリップがあります");
    }

    // ---- 一覧（左）----
    int duplicateRequest = -1;
    ImGui::BeginChild("##ClipList", ImVec2(200.0f, 0.0f), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX);
    if (clips_.empty())
    {
        ImGui::TextDisabled("クリップがありません");
    }
    for (int i = 0; i < static_cast<int>(clips_.size()); ++i)
    {
        const UIClip &clip = clips_[i];
        ImGui::PushID(i);
        const std::string label = std::format("{} {}  ({})", clip.playing_ ? ICON_FA_PLAY : ICON_FA_FILM, clip.name, clip.tweens.size());
        if (ImGui::Selectable(label.c_str(), selectedClip_ == i))
        {
            if (selectedClip_ != i)
                selectedTween_ = -1;
            selectedClip_ = i;
        }
        ImGui::SetItemTooltip("（ ）はトゥイーンの本数。右クリックで複製・削除");
        if (ImGui::BeginPopupContextItem("##clipMenu"))
        {
            if (ImGui::MenuItem(ICON_FA_CLONE " 複製"))
                duplicateRequest = i;
            if (ImGui::MenuItem(ICON_FA_COPY " 名前をコピー"))
                ImGui::SetClipboardText(clip.name.c_str());
            ImGui::Separator();
            ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kAccentRed);
            if (ImGui::MenuItem(ICON_FA_TRASH_ALT " 削除"))
                deleteClipRequest_ = i;
            ImGui::PopStyleColor();
            ImGui::EndPopup();
        }
        ImGui::PopID();
    }
    ImGui::EndChild();
    if (duplicateRequest >= 0)
    {
        UIClip copy = clips_[duplicateRequest];
        copy.playing_ = false;
        copy.name = MakeUniqueName(copy.name, [this](const std::string &n) { return FindClip(n) != nullptr; });
        clips_.insert(clips_.begin() + duplicateRequest + 1, copy);
        selectedClip_ = duplicateRequest + 1;
        selectedTween_ = -1;
    }

    ImGui::SameLine();

    // ---- 中身（右）----
    ImGui::BeginChild("##ClipDetail", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders);
    if (selectedClip_ >= 0 && selectedClip_ < static_cast<int>(clips_.size()))
    {
        UIClip &clip = clips_[selectedClip_];

        ImGui::AlignTextToFramePadding();
        ImGui::Text(ICON_FA_FILM " %s", clip.name.c_str());
        ImGui::SameLine();
        ImGui::Checkbox("ループ", &clip.loop);
        ImGui::SameLine();
        if (ImGui::SmallButton(ICON_FA_COPY " 再生コードをコピー"))
        {
            const std::string code = std::format("UIAnimator::GetInstance()->Play(\"{}\");", clip.name);
            ImGui::SetClipboardText(code.c_str());
            ImGuiNotification::Post("コピーしました: " + code, {0.45f, 0.60f, 0.78f, 1.0f});
        }
        SameLineRightAligned(80.0f);
        if (DangerButton(ICON_FA_TRASH_ALT " 削除"))
        {
            deleteClipRequest_ = selectedClip_;
        }

        // タイムライン（再生・停止・途中の確認・プレビュー前に戻す）
        DrawClipTimeline(clip);
        ImGui::Spacing();

        // ---- トゥイーンの一覧（1行の要約。押すと下に設定が出る）----
        SectionHeader(std::format("トゥイーン（{} 本）", clip.tweens.size()).c_str(), DebugTheme::kAccentBlue);
        int moveFrom = -1;
        int moveTo = -1;
        int duplicateTween = -1;
        int removeTween = -1;
        for (int i = 0; i < static_cast<int>(clip.tweens.size()); ++i)
        {
            const UITween &tween = clip.tweens[i];
            ImGui::PushID(i);
            const std::string start = tween.fromCurrent ? std::string("今の値") : FormatChannelValue(tween.channel, tween.startValue);
            const std::string summary = std::format(
                "{}. {}  {}   {} → {}   {:.2f}〜{:.2f} 秒", i + 1,
                tween.targetName.empty() ? "(対象なし)" : tween.targetName.c_str(),
                kChannelNames[std::clamp(static_cast<int>(tween.channel), 0, kChannelCount - 1)], start,
                FormatChannelValue(tween.channel, tween.endValue), tween.delay, tween.delay + tween.duration);
            if (tween.targetName.empty())
                ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kAccentOrange);
            if (ImGui::Selectable(summary.c_str(), selectedTween_ == i))
                selectedTween_ = (selectedTween_ == i) ? -1 : i; // もう一度押すと閉じる
            if (tween.targetName.empty())
                ImGui::PopStyleColor();
            if (ImGui::BeginPopupContextItem("##tweenMenu"))
            {
                if (ImGui::MenuItem(ICON_FA_CLONE " 複製"))
                    duplicateTween = i;
                if (ImGui::MenuItem(ICON_FA_ARROW_UP " 上へ", nullptr, false, i > 0))
                {
                    moveFrom = i;
                    moveTo = i - 1;
                }
                if (ImGui::MenuItem(ICON_FA_ARROW_DOWN " 下へ", nullptr, false, i + 1 < static_cast<int>(clip.tweens.size())))
                {
                    moveFrom = i;
                    moveTo = i + 1;
                }
                ImGui::Separator();
                ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kAccentRed);
                if (ImGui::MenuItem(ICON_FA_TRASH_ALT " 削除"))
                    removeTween = i;
                ImGui::PopStyleColor();
                ImGui::EndPopup();
            }
            ImGui::PopID();
        }

        if (PrimaryButton(ICON_FA_PLUS " トゥイーンを足す"))
        {
            UITween tween;
            // 直前の物と同じ相手・同じ値から始めると、続けて足すときに選び直さずに済む
            if (!clip.tweens.empty())
            {
                const UITween &previous = clip.tweens.back();
                tween.targetKind = previous.targetKind;
                tween.channel = previous.channel;
                tween.easing = previous.easing;
                tween.delay = previous.delay;
                tween.duration = previous.duration;
            }
            const std::string target = SuggestTweenTarget(clip);
            if (!PickedSpriteNames().empty())
                tween.targetKind = UITargetKind::Sprite;
            tween.targetName = target;
            // 何もしない動き（今の値 → 今の値）から始め、目標だけ書き換えれば済むようにする
            if (!target.empty())
            {
                tween.startValue = GetChannelValue(tween.targetKind, target, tween.channel);
                tween.endValue = tween.startValue;
            }
            clip.tweens.push_back(tween);
            selectedTween_ = static_cast<int>(clip.tweens.size()) - 1;
        }
        ImGui::SetItemTooltip("シーンでスプライトを選んでいれば、それが対象になります");

        // 一覧の操作は描き終えてから行う
        if (duplicateTween >= 0)
        {
            UITween copy = clip.tweens[duplicateTween];
            clip.tweens.insert(clip.tweens.begin() + duplicateTween + 1, copy);
            selectedTween_ = duplicateTween + 1;
        }
        if (moveFrom >= 0)
        {
            std::swap(clip.tweens[moveFrom], clip.tweens[moveTo]);
            selectedTween_ = moveTo;
        }
        if (removeTween >= 0)
        {
            clip.tweens.erase(clip.tweens.begin() + removeTween);
            selectedTween_ = -1;
        }

        // ---- 選んだトゥイーンの設定 ----
        if (selectedTween_ >= 0 && selectedTween_ < static_cast<int>(clip.tweens.size()))
        {
            ImGui::Spacing();
            ImGui::SeparatorText(std::format("{}. の設定", selectedTween_ + 1).c_str());
            ImGui::PushID(selectedTween_);
            DrawTweenEditor(clip.tweens[selectedTween_]);
            ImGui::PopID();
        }
        else if (!clip.tweens.empty())
        {
            DimText("一覧かタイムラインの行を押すと、そのトゥイーンの設定が開きます");
        }
    }
    else
    {
        ImGui::TextDisabled("左の一覧からクリップを選ぶか、上で作ってください");
    }
    ImGui::EndChild();

    // ---- 削除の確認 ----
    // 削除の要求は子ウィンドウの中から来るので、開くのはここで一度だけ行う
    if (deleteClipRequest_ >= 0 && !ImGui::IsPopupOpen("クリップを消す##uiClipDelete"))
    {
        ImGui::OpenPopup("クリップを消す##uiClipDelete");
    }
    if (ImGui::BeginPopupModal("クリップを消す##uiClipDelete", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        const bool valid = deleteClipRequest_ >= 0 && deleteClipRequest_ < static_cast<int>(clips_.size());
        if (valid)
        {
            ImGui::Text("クリップ「%s」を消しますか？", clips_[deleteClipRequest_].name.c_str());
            ImGui::TextDisabled("コードから Play(\"%s\") している所は再生されなくなります", clips_[deleteClipRequest_].name.c_str());
        }
        ImGui::Separator();
        if (DangerButton("消す", ImVec2(120.0f, 0.0f)))
        {
            if (valid)
            {
                clips_.erase(clips_.begin() + deleteClipRequest_);
                selectedClip_ = -1;
                selectedTween_ = -1;
            }
            deleteClipRequest_ = -1;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (NeutralButton("キャンセル", ImVec2(120.0f, 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Escape))
        {
            deleteClipRequest_ = -1;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    else
    {
        deleteClipRequest_ = -1;
    }
}

void UIAnimator::DrawTweenEditor(UITween &tween)
{
    const float spacing = ImGui::GetStyle().ItemSpacing.x;

    // ---- 対象 ----
    RowLabel("対象");
    ImGui::SetNextItemWidth(110.0f);
    int kind = static_cast<int>(tween.targetKind);
    if (ImGui::Combo("##kind", &kind, kKindNames, 2))
    {
        tween.targetKind = static_cast<UITargetKind>(kind);
        tween.targetName.clear();
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-FLT_MIN);
    TargetNameCombo("##target", tween.targetKind, tween.targetName, groups_);
    if (!tween.targetName.empty() && tween.targetKind == UITargetKind::Sprite &&
        !SpriteManager::GetInstance()->GetSprite(tween.targetName))
    {
        ImGui::TextColored(DebugTheme::kAccentOrange, "このスプライトは今のシーンにありません");
    }

    RowLabel("動かす値");
    int channel = static_cast<int>(tween.channel);
    if (ImGui::Combo("##channel", &channel, kChannelNames, kChannelCount))
    {
        tween.channel = static_cast<UIChannel>(channel);
    }

    // ---- 開始・目標（チャンネルの単位で見せる。「今の値」で対象の今の値を取り込める）----
    const bool hasTarget = !tween.targetName.empty();
    const float captureWidth = ImGui::CalcTextSize("今の値").x + ImGui::GetStyle().FramePadding.x * 2.0f;

    RowLabel("開始");
    ImGui::Checkbox("今の値から##fromCurrent", &tween.fromCurrent);
    ImGui::SetItemTooltip("再生した瞬間の値から動かす（途中から再生しても飛ばない）");
    if (!tween.fromCurrent)
    {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - captureWidth - spacing);
        ChannelValueField("##start", tween.channel, tween.startValue);
        ImGui::SameLine();
        ImGui::BeginDisabled(!hasTarget);
        if (ImGui::Button("今の値##captureStart"))
            tween.startValue = GetChannelValue(tween.targetKind, tween.targetName, tween.channel);
        ImGui::EndDisabled();
        ImGui::SetItemTooltip("対象の今の値を開始値にする（シーンで置いてから取り込むと楽）");
    }

    RowLabel("目標");
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - captureWidth - spacing);
    ChannelValueField("##end", tween.channel, tween.endValue);
    ImGui::SameLine();
    ImGui::BeginDisabled(!hasTarget);
    if (ImGui::Button("今の値##captureEnd"))
        tween.endValue = GetChannelValue(tween.targetKind, tween.targetName, tween.channel);
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("対象の今の値を目標値にする（シーンで動かしてから取り込むと楽）");

    // ---- 時間 ----
    RowLabel("時間");
    const float half = (ImGui::GetContentRegionAvail().x - spacing) * 0.5f;
    ImGui::SetNextItemWidth(half);
    ImGui::DragFloat("##delay", &tween.delay, 0.01f, 0.0f, 60.0f, "待ち %.2f 秒", ImGuiSliderFlags_AlwaysClamp);
    ImGui::SetItemTooltip("再生してから動き始めるまでの秒数");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::DragFloat("##duration", &tween.duration, 0.01f, 0.0f, 60.0f, "長さ %.2f 秒", ImGuiSliderFlags_AlwaysClamp);
    ImGui::SetItemTooltip("動いている秒数（0 で一瞬で目標値）");

    // ---- イージング（形をグラフで見せる）----
    RowLabel("イージング");
    const ImVec2 curveSize(72.0f, ImGui::GetFrameHeight());
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - curveSize.x - spacing);
    int easing = static_cast<int>(tween.easing);
    if (ImGui::BeginCombo("##easing", kEasingNames[std::clamp(easing, 0, kEasingCount - 1)], ImGuiComboFlags_HeightLarge))
    {
        for (int i = 0; i < kEasingCount; ++i)
        {
            ImGui::PushID(i);
            if (ImGui::Selectable(kEasingNames[i], i == easing, ImGuiSelectableFlags_None,
                                  ImVec2(ImGui::GetContentRegionAvail().x - curveSize.x - spacing, 0.0f)))
            {
                tween.easing = static_cast<EasingType>(i);
            }
            // 候補を選ぶ前に形が分かるよう、横に小さく描いておく
            ImGui::SameLine();
            EasingCurve("##option", static_cast<EasingType>(i), ImVec2(curveSize.x, ImGui::GetTextLineHeight()));
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    EasingCurve("##curve", tween.easing, curveSize);
    ImGui::SetItemTooltip("横が時間、縦が進み具合。線が 0〜1 の枠をはみ出す所は行き過ぎて戻る動き");
}
#endif // USE_IMGUI

} // namespace Hagine
