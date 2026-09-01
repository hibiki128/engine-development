#include "UIAnimator.h"
#include "SpriteManager.h"
#include <algorithm>
#include <data/DataHandler.h>
#ifdef USE_IMGUI
#include <imgui.h>
#include <utility/debug/imgui/ImGuiNotification.h>
#endif // USE_IMGUI

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
            const Vector2 pos = {g.origin.x + m.offset.x, g.origin.y + m.offset.y};
            sm->SetSpritePosition(m.spriteName, pos);
        }
    }
}

// ===================================================
// 保存・読み込み
// ===================================================

void UIAnimator::Save()
{
    DataHandler data("UI", "UIAnimation");

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

    data.Save("groups", groupsJson);
    data.Save("clips", clipsJson);
    data.Flush();

#ifdef USE_IMGUI
    ImGuiNotification::Post("UIアニメーションを保存しました", {0.2f, 0.8f, 0.2f, 1.0f});
#endif // USE_IMGUI
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
}

#ifdef USE_IMGUI
// ===================================================
// エディタUI
// ===================================================

namespace {
// 対象名のコンボ（スプライト名一覧またはグループ名一覧から選ぶ）
bool TargetNameCombo(const char *label, UITargetKind kind, std::string &target,
                     const std::vector<UIGroup> &groups)
{
    bool changed = false;
    const char *preview = target.empty() ? "(未選択)" : target.c_str();
    if (ImGui::BeginCombo(label, preview))
    {
        if (kind == UITargetKind::Group)
        {
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
} // namespace

void UIAnimator::DrawImGui(bool *open)
{
    EnsureLoaded();

    if (!ImGui::Begin("UIエディタ", open, ImGuiWindowFlags_NoFocusOnAppearing))
    {
        ImGui::End();
        return;
    }

    // 保存・再読み込み
    if (ImGui::Button("保存"))
        Save();
    ImGui::SameLine();
    if (ImGui::Button("再読み込み"))
    {
        loaded_ = false;
        selectedGroup_ = -1;
        selectedClip_ = -1;
        Load();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("コードからは UIAnimator::Play(\"クリップ名\") で再生");

    ImGui::Separator();

    // このエディタで対象にできるのは SpriteManager に登録済み(所有)のスプライトのみ。
    // 一覧が空になる主因（未登録・外部所有）を明示して迷わせないようにする。
    const int spriteCount = static_cast<int>(SpriteManager::GetInstance()->GetAllSprites().size());
    if (spriteCount == 0)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.75f, 0.40f, 1.0f));
        ImGui::TextWrapped("対象にできるスプライトがまだありません。ここで扱えるのは『スプライトマネージャ』に登録済み(所有)のスプライトだけです。"
                           "スプライトマネージャで『スプライト作成』/『文字スプライト作成』を行うか、コードから SpriteManager::RegisterSprite() で登録すると、ここの一覧に出てきます。");
        ImGui::PopStyleColor();
    }
    else
    {
        ImGui::TextDisabled("対象にできるスプライト: %d 個 (SpriteManager登録分)", spriteCount);
    }
    ImGui::Separator();

    if (ImGui::BeginTabBar("##UIEditorTabs"))
    {
        // ============================================================
        // グループ
        // ============================================================
        if (ImGui::BeginTabItem("グループ"))
        {
            // 新規作成
            ImGui::SetNextItemWidth(180);
            ImGui::InputText("##NewGroupName", newNameBuffer_, sizeof(newNameBuffer_));
            ImGui::SameLine();
            if (ImGui::Button("グループ作成") && newNameBuffer_[0] != '\0' && !FindGroup(newNameBuffer_))
            {
                UIGroup g;
                g.name = newNameBuffer_;
                groups_.push_back(g);
                selectedGroup_ = static_cast<int>(groups_.size()) - 1;
                newNameBuffer_[0] = '\0';
            }

            // グループ一覧
            ImGui::BeginChild("##GroupList", ImVec2(180, 160), ImGuiChildFlags_Borders);
            for (int i = 0; i < static_cast<int>(groups_.size()); ++i)
            {
                if (ImGui::Selectable(groups_[i].name.c_str(), selectedGroup_ == i))
                    selectedGroup_ = i;
            }
            ImGui::EndChild();

            ImGui::SameLine();

            // 選択グループの詳細
            ImGui::BeginChild("##GroupDetail", ImVec2(0, 160), ImGuiChildFlags_Borders);
            if (selectedGroup_ >= 0 && selectedGroup_ < static_cast<int>(groups_.size()))
            {
                UIGroup &g = groups_[selectedGroup_];
                ImGui::Text("グループ: %s", g.name.c_str());
                ImGui::DragFloat2("原点", &g.origin.x, 1.0f);
                ImGui::TextDisabled("原点を動かすと全メンバーが相対を保って追従します");

                if (ImGui::SmallButton("現在位置を相対値として一括取り込み"))
                {
                    for (auto &m : g.members)
                    {
                        InstanceSRT *s = SpriteManager::GetInstance()->GetInstanceSRT(m.spriteName, 0);
                        if (s)
                            m.offset = {s->translation.x - g.origin.x, s->translation.y - g.origin.y};
                    }
                }

                ImGui::Separator();
                ImGui::Text("メンバー");
                int removeIndex = -1;
                for (int mi = 0; mi < static_cast<int>(g.members.size()); ++mi)
                {
                    ImGui::PushID(mi);
                    UIGroupMember &m = g.members[mi];
                    ImGui::BulletText("%s", m.spriteName.c_str());
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(140);
                    ImGui::DragFloat2("相対##off", &m.offset.x, 1.0f);
                    ImGui::SameLine();
                    if (ImGui::SmallButton("削除"))
                        removeIndex = mi;
                    ImGui::PopID();
                }
                if (removeIndex >= 0)
                    g.members.erase(g.members.begin() + removeIndex);

                // メンバー追加
                static std::string addTarget;
                ImGui::SetNextItemWidth(180);
                TargetNameCombo("##AddMember", UITargetKind::Sprite, addTarget, groups_);
                ImGui::SameLine();
                if (ImGui::Button("メンバー追加") && !addTarget.empty())
                {
                    // 既に含まれていなければ追加。現在位置から相対オフセットを自動計算する。
                    bool exists = false;
                    for (auto &m : g.members)
                        if (m.spriteName == addTarget)
                            exists = true;
                    if (!exists)
                    {
                        UIGroupMember m;
                        m.spriteName = addTarget;
                        InstanceSRT *s = SpriteManager::GetInstance()->GetInstanceSRT(addTarget, 0);
                        if (s)
                            m.offset = {s->translation.x - g.origin.x, s->translation.y - g.origin.y};
                        g.members.push_back(m);
                    }
                }

                ImGui::Separator();
                if (ImGui::Button("このグループを削除"))
                {
                    groups_.erase(groups_.begin() + selectedGroup_);
                    selectedGroup_ = -1;
                }
            }
            else
            {
                ImGui::TextDisabled("左の一覧からグループを選択、または新規作成してください");
            }
            ImGui::EndChild();

            ImGui::EndTabItem();
        }

        // ============================================================
        // クリップ
        // ============================================================
        if (ImGui::BeginTabItem("クリップ"))
        {
            // 新規作成
            ImGui::SetNextItemWidth(180);
            ImGui::InputText("##NewClipName", newNameBuffer_, sizeof(newNameBuffer_));
            ImGui::SameLine();
            if (ImGui::Button("クリップ作成") && newNameBuffer_[0] != '\0' && !FindClip(newNameBuffer_))
            {
                UIClip c;
                c.name = newNameBuffer_;
                clips_.push_back(c);
                selectedClip_ = static_cast<int>(clips_.size()) - 1;
                newNameBuffer_[0] = '\0';
            }

            // クリップ一覧
            ImGui::BeginChild("##ClipList", ImVec2(180, 0), ImGuiChildFlags_Borders);
            for (int i = 0; i < static_cast<int>(clips_.size()); ++i)
            {
                std::string label = clips_[i].name;
                if (clips_[i].playing_)
                    label += " ▶";
                if (ImGui::Selectable((label + "##clip" + std::to_string(i)).c_str(), selectedClip_ == i))
                    selectedClip_ = i;
            }
            ImGui::EndChild();

            ImGui::SameLine();

            // 選択クリップの詳細
            ImGui::BeginChild("##ClipDetail", ImVec2(0, 0), ImGuiChildFlags_Borders);
            if (selectedClip_ >= 0 && selectedClip_ < static_cast<int>(clips_.size()))
            {
                UIClip &c = clips_[selectedClip_];
                ImGui::Text("クリップ: %s", c.name.c_str());
                ImGui::Checkbox("ループ", &c.loop);
                ImGui::SameLine();
                if (ImGui::Button("▶ 再生"))
                    Play(c.name);
                ImGui::SameLine();
                if (ImGui::Button("■ 停止"))
                    Stop(c.name);
                ImGui::SameLine();
                ImGui::TextDisabled(c.playing_ ? "再生中" : "停止中");

                ImGui::Separator();

                int removeTween = -1;
                for (int ti = 0; ti < static_cast<int>(c.tweens.size()); ++ti)
                {
                    ImGui::PushID(ti);
                    UITween &t = c.tweens[ti];

                    const char *chName = (static_cast<int>(t.channel) < kChannelCount)
                                             ? kChannelNames[static_cast<int>(t.channel)]
                                             : "?";
                    std::string header = std::to_string(ti + 1) + ": " +
                                         (t.targetName.empty() ? "(未選択)" : t.targetName) + " / " + chName;
                    if (ImGui::CollapsingHeader((header + "##tw").c_str(), ImGuiTreeNodeFlags_DefaultOpen))
                    {
                        // 対象種別
                        int kind = static_cast<int>(t.targetKind);
                        if (ImGui::Combo("対象種別", &kind, kKindNames, 2))
                        {
                            t.targetKind = static_cast<UITargetKind>(kind);
                            t.targetName.clear();
                        }
                        // 対象名
                        TargetNameCombo("対象", t.targetKind, t.targetName, groups_);
                        // プロパティ
                        int ch = static_cast<int>(t.channel);
                        if (ImGui::Combo("プロパティ", &ch, kChannelNames, kChannelCount))
                            t.channel = static_cast<UIChannel>(ch);

                        // 初期値・目標値
                        ImGui::Checkbox("現在値から開始", &t.fromCurrent);
                        if (t.fromCurrent)
                            ImGui::BeginDisabled();
                        ImGui::DragFloat("初期値", &t.startValue, 0.5f);
                        if (t.fromCurrent)
                            ImGui::EndDisabled();
                        ImGui::DragFloat("目標値", &t.endValue, 0.5f);

                        // 秒数・遅延
                        ImGui::DragFloat("秒数", &t.duration, 0.01f, 0.0f, 60.0f, "%.2f");
                        ImGui::DragFloat("遅延", &t.delay, 0.01f, 0.0f, 60.0f, "%.2f");

                        // イージング
                        int easing = static_cast<int>(t.easing);
                        if (ImGui::Combo("イージング", &easing, kEasingNames, kEasingCount))
                            t.easing = static_cast<EasingType>(easing);

                        if (ImGui::SmallButton("このトゥイーンを削除"))
                            removeTween = ti;
                    }
                    ImGui::PopID();
                }
                if (removeTween >= 0)
                    c.tweens.erase(c.tweens.begin() + removeTween);

                ImGui::Separator();
                if (ImGui::Button("＋ トゥイーン追加"))
                {
                    UITween t;
                    c.tweens.push_back(t);
                }
                ImGui::SameLine();
                if (ImGui::Button("このクリップを削除"))
                {
                    clips_.erase(clips_.begin() + selectedClip_);
                    selectedClip_ = -1;
                }
            }
            else
            {
                ImGui::TextDisabled("左の一覧からクリップを選択、または新規作成してください");
            }
            ImGui::EndChild();

            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    ImGui::End();
}
#endif // USE_IMGUI

} // namespace Hagine
