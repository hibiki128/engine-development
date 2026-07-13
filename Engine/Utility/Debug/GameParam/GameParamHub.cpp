#include "GameParamHub.h"
#include <Utility/Data/DataHandler.h>
#include <Utility/Debug/ImGui/ImGuiNotification.h>
#include <algorithm>
#ifdef USE_IMGUI
#include <imgui.h>
#endif // USE_IMGUI

namespace Hagine {

namespace {
/// ドラッグ＆ドロップで使うペイロード識別子
constexpr const char *kDndPayloadType = "GAMEPARAM_KEY";
/// レイアウト保存先
constexpr const char *kLayoutFolder = "Debug";
constexpr const char *kLayoutFile = "GameParamHubLayout";
} // namespace

GameParamHub *GameParamHub::GetInstance()
{
    static GameParamHub instance;
    return &instance;
}

std::string GameParamHub::MakeKey(const std::string &owner, const std::string &name)
{
    return owner + "/" + name;
}

void GameParamHub::Register(const std::string &owner, const std::string &name, ParamPtr ptr, const Options &opts)
{
    const std::string key = MakeKey(owner, name);
    // 同一キーは上書き（シーン再初期化での再登録を想定）
    for (auto &e : entries_)
    {
        if (MakeKey(e.owner, e.name) == key)
        {
            e.ptr = ptr;
            e.opts = opts;
            return;
        }
    }
    entries_.push_back({owner, name, ptr, opts});
}

void GameParamHub::Unregister(const std::string &owner)
{
    entries_.erase(
        std::remove_if(entries_.begin(), entries_.end(),
                       [&](const Entry &e) { return e.owner == owner; }),
        entries_.end());
}

void GameParamHub::SaveLayout()
{
    DataHandler data(kLayoutFolder, kLayoutFile);

    json windowsJson = json::array();
    for (const auto &w : windows_)
    {
        json tabsJson = json::array();
        for (const auto &t : w.tabs)
        {
            json sectionsJson = json::array();
            for (const auto &s : t.sections)
            {
                sectionsJson.push_back(s.name);
            }
            tabsJson.push_back({{"name", t.name}, {"sections", sectionsJson}});
        }
        windowsJson.push_back({{"name", w.name}, {"tabs", tabsJson}});
    }

    json assignJson = json::object();
    for (const auto &[key, a] : assignments_)
    {
        assignJson[key] = {{"window", a.window}, {"tab", a.tab}, {"section", a.section}};
    }

    data.Save("windows", windowsJson);
    data.Save("assignments", assignJson);
    data.Flush();
    layoutDirty_ = false;
#ifdef USE_IMGUI
    ImGuiNotification::Post("パラメータ仕分けを保存しました", {0.2f, 0.8f, 0.2f, 1.0f});
#endif
}

void GameParamHub::LoadLayout()
{
    DataHandler data(kLayoutFolder, kLayoutFile);
    windows_.clear();
    assignments_.clear();

    json windowsJson = data.Load<json>("windows", json::array());
    for (const auto &wj : windowsJson)
    {
        WindowDef w;
        w.name = wj.value("name", "");
        if (w.name.empty())
        {
            continue;
        }
        for (const auto &tj : wj.value("tabs", json::array()))
        {
            TabDef t;
            t.name = tj.value("name", "");
            if (t.name.empty())
            {
                continue;
            }
            for (const auto &sj : tj.value("sections", json::array()))
            {
                t.sections.push_back({sj.get<std::string>()});
            }
            w.tabs.push_back(std::move(t));
        }
        windows_.push_back(std::move(w));
    }

    json assignJson = data.Load<json>("assignments", json::object());
    for (auto it = assignJson.begin(); it != assignJson.end(); ++it)
    {
        Assignment a;
        a.window = it.value().value("window", "");
        a.tab = it.value().value("tab", "");
        a.section = it.value().value("section", "");
        assignments_[it.key()] = a;
    }
    layoutDirty_ = false;
}

GameParamHub::WindowDef *GameParamHub::FindWindowDef(const std::string &name)
{
    for (auto &w : windows_)
    {
        if (w.name == name)
        {
            return &w;
        }
    }
    return nullptr;
}

std::vector<std::string> GameParamHub::CollectAssigned(const std::string &window, const std::string &tab, const std::string &section)
{
    std::vector<std::string> result;
    for (const auto &e : entries_)
    {
        const std::string key = MakeKey(e.owner, e.name);
        auto it = assignments_.find(key);
        if (it == assignments_.end())
        {
            continue;
        }
        if (it->second.window == window && it->second.tab == tab && it->second.section == section)
        {
            result.push_back(key);
        }
    }
    return result;
}

void GameParamHub::DrawImGui(bool *open)
{
#ifdef USE_IMGUI
    // 初回のみレイアウトを復元する
    if (!layoutLoaded_)
    {
        LoadLayout();
        layoutLoaded_ = true;
    }

    DrawHubWindow(open);
    DrawUserWindows();
#else
    (void)open;
#endif
}

#ifdef USE_IMGUI

void GameParamHub::DrawParamWidget(Entry &entry)
{
    const std::string label = entry.name + "##" + entry.owner;
    bool changed = false;

    std::visit([&](auto *p) {
        using T = std::remove_pointer_t<std::remove_cv_t<std::remove_reference_t<decltype(p)>>>;
        using Ptr = std::remove_reference_t<decltype(p)>;
        const Options &o = entry.opts;

        if constexpr (std::is_same_v<Ptr, float *>)
        {
            changed = ImGui::DragFloat(label.c_str(), p, o.speed, o.min, o.max);
        }
        else if constexpr (std::is_same_v<Ptr, int *>)
        {
            changed = ImGui::DragInt(label.c_str(), p, o.speed, static_cast<int>(o.min), static_cast<int>(o.max));
        }
        else if constexpr (std::is_same_v<Ptr, bool *>)
        {
            changed = ImGui::Checkbox(label.c_str(), p);
        }
        else if constexpr (std::is_same_v<Ptr, Vector2 *>)
        {
            changed = ImGui::DragFloat2(label.c_str(), &p->x, o.speed, o.min, o.max);
        }
        else if constexpr (std::is_same_v<Ptr, Vector3 *>)
        {
            changed = ImGui::DragFloat3(label.c_str(), &p->x, o.speed, o.min, o.max);
        }
        else if constexpr (std::is_same_v<Ptr, Vector4 *>)
        {
            if (o.isColor)
            {
                changed = ImGui::ColorEdit4(label.c_str(), &p->x);
            }
            else
            {
                changed = ImGui::DragFloat4(label.c_str(), &p->x, o.speed, o.min, o.max);
            }
        }
        else if constexpr (std::is_same_v<Ptr, const float *>)
        {
            ImGui::Text("%s: %.3f", entry.name.c_str(), *p);
        }
        else if constexpr (std::is_same_v<Ptr, const int *>)
        {
            ImGui::Text("%s: %d", entry.name.c_str(), *p);
        }
        else if constexpr (std::is_same_v<Ptr, const bool *>)
        {
            ImGui::Text("%s: %s", entry.name.c_str(), *p ? "True" : "False");
        }
        else
        {
            (void)sizeof(T); // 未対応型（コンパイル時に列挙済みのため到達しない）
        }
    },
               entry.ptr);

    if (changed && entry.opts.onChange)
    {
        entry.opts.onChange();
    }
}

void GameParamHub::DrawDragSource(const std::string &key)
{
    // 「≡」を掴んでレイアウトツリーへドロップして仕分ける
    ImGui::Selectable(("≡##drag_" + key).c_str(), false, 0, ImVec2(18.0f, 0.0f));
    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None))
    {
        ImGui::SetDragDropPayload(kDndPayloadType, key.c_str(), key.size() + 1);
        ImGui::Text("%s", key.c_str());
        ImGui::EndDragDropSource();
    }
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("ドラッグして仕分け / 右クリックで移動メニュー");
    }
    DrawMoveContextMenu(key);
    ImGui::SameLine();
}

void GameParamHub::HandleDropTarget(const std::string &window, const std::string &tab, const std::string &section)
{
    if (ImGui::BeginDragDropTarget())
    {
        if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload(kDndPayloadType))
        {
            const std::string key = static_cast<const char *>(payload->Data);
            assignments_[key] = {window, tab, section};
            layoutDirty_ = true;
        }
        ImGui::EndDragDropTarget();
    }
}

void GameParamHub::DrawMoveContextMenu(const std::string &key)
{
    if (ImGui::BeginPopupContextItem(("move_" + key).c_str()))
    {
        if (ImGui::MenuItem("未分類へ戻す"))
        {
            assignments_.erase(key);
            layoutDirty_ = true;
        }
        ImGui::Separator();
        for (const auto &w : windows_)
        {
            if (ImGui::BeginMenu(w.name.c_str()))
            {
                if (ImGui::MenuItem("(ウィンドウ直下)"))
                {
                    assignments_[key] = {w.name, "", ""};
                    layoutDirty_ = true;
                }
                for (const auto &t : w.tabs)
                {
                    if (ImGui::BeginMenu(t.name.c_str()))
                    {
                        if (ImGui::MenuItem("(タブ直下)"))
                        {
                            assignments_[key] = {w.name, t.name, ""};
                            layoutDirty_ = true;
                        }
                        for (const auto &s : t.sections)
                        {
                            if (ImGui::MenuItem(s.name.c_str()))
                            {
                                assignments_[key] = {w.name, t.name, s.name};
                                layoutDirty_ = true;
                            }
                        }
                        ImGui::EndMenu();
                    }
                }
                ImGui::EndMenu();
            }
        }
        ImGui::EndPopup();
    }
}

void GameParamHub::DrawParamRows(const std::vector<std::string> &keys)
{
    for (const auto &key : keys)
    {
        for (auto &e : entries_)
        {
            if (MakeKey(e.owner, e.name) != key)
            {
                continue;
            }
            ImGui::PushID(key.c_str());
            DrawDragSource(key);
            DrawParamWidget(e);
            ImGui::PopID();
            break;
        }
    }
}

void GameParamHub::DrawLayoutTree()
{
    // ── 新規ウィンドウ作成 ──
    static char newWindowName[64] = {};
    ImGui::SetNextItemWidth(180.0f);
    ImGui::InputTextWithHint("##newWindow", "新しいウィンドウ名", newWindowName, sizeof(newWindowName));
    ImGui::SameLine();
    if (ImGui::Button("ウィンドウ作成") && newWindowName[0] != '\0')
    {
        if (!FindWindowDef(newWindowName))
        {
            windows_.push_back({newWindowName, {}});
            layoutDirty_ = true;
        }
        newWindowName[0] = '\0';
    }

    ImGui::Separator();

    // ── ウィンドウ → タブ → セクションのツリー（各ノードがドロップ先） ──
    int windowIndexToDelete = -1;
    for (int wi = 0; wi < static_cast<int>(windows_.size()); ++wi)
    {
        WindowDef &w = windows_[wi];
        ImGui::PushID(wi);

        bool windowOpen = ImGui::TreeNodeEx(w.name.c_str(), ImGuiTreeNodeFlags_DefaultOpen);
        HandleDropTarget(w.name, "", "");

        // ウィンドウ操作メニュー
        if (ImGui::BeginPopupContextItem("windowMenu"))
        {
            if (ImGui::MenuItem("ウィンドウを削除（中身は未分類へ）"))
            {
                windowIndexToDelete = wi;
            }
            ImGui::EndPopup();
        }

        if (windowOpen)
        {
            // タブ追加
            static char newTabName[64] = {};
            ImGui::SetNextItemWidth(140.0f);
            ImGui::InputTextWithHint("##newTab", "新しいタブ名", newTabName, sizeof(newTabName));
            ImGui::SameLine();
            if (ImGui::Button("タブ追加") && newTabName[0] != '\0')
            {
                bool exists = false;
                for (const auto &t : w.tabs)
                {
                    exists |= (t.name == newTabName);
                }
                if (!exists)
                {
                    w.tabs.push_back({newTabName, {}});
                    layoutDirty_ = true;
                }
                newTabName[0] = '\0';
            }

            // ウィンドウ直下のパラメータ
            DrawParamRows(CollectAssigned(w.name, "", ""));

            int tabIndexToDelete = -1;
            for (int ti = 0; ti < static_cast<int>(w.tabs.size()); ++ti)
            {
                TabDef &t = w.tabs[ti];
                ImGui::PushID(1000 + ti);

                bool tabOpen = ImGui::TreeNodeEx(("[タブ] " + t.name).c_str(), ImGuiTreeNodeFlags_DefaultOpen);
                HandleDropTarget(w.name, t.name, "");

                if (ImGui::BeginPopupContextItem("tabMenu"))
                {
                    if (ImGui::MenuItem("タブを削除（中身は未分類へ）"))
                    {
                        tabIndexToDelete = ti;
                    }
                    ImGui::EndPopup();
                }

                if (tabOpen)
                {
                    // セクション追加
                    static char newSectionName[64] = {};
                    ImGui::SetNextItemWidth(140.0f);
                    ImGui::InputTextWithHint("##newSection", "新しいセクション名", newSectionName, sizeof(newSectionName));
                    ImGui::SameLine();
                    if (ImGui::Button("セクション追加") && newSectionName[0] != '\0')
                    {
                        bool exists = false;
                        for (const auto &s : t.sections)
                        {
                            exists |= (s.name == newSectionName);
                        }
                        if (!exists)
                        {
                            t.sections.push_back({newSectionName});
                            layoutDirty_ = true;
                        }
                        newSectionName[0] = '\0';
                    }

                    // タブ直下のパラメータ
                    DrawParamRows(CollectAssigned(w.name, t.name, ""));

                    int sectionIndexToDelete = -1;
                    for (int si = 0; si < static_cast<int>(t.sections.size()); ++si)
                    {
                        SectionDef &s = t.sections[si];
                        ImGui::PushID(2000 + si);

                        bool sectionOpen = ImGui::TreeNodeEx(("[区切] " + s.name).c_str(), ImGuiTreeNodeFlags_DefaultOpen);
                        HandleDropTarget(w.name, t.name, s.name);

                        if (ImGui::BeginPopupContextItem("sectionMenu"))
                        {
                            if (ImGui::MenuItem("セクションを削除（中身は未分類へ）"))
                            {
                                sectionIndexToDelete = si;
                            }
                            ImGui::EndPopup();
                        }

                        if (sectionOpen)
                        {
                            DrawParamRows(CollectAssigned(w.name, t.name, s.name));
                            ImGui::TreePop();
                        }
                        ImGui::PopID();
                    }

                    if (sectionIndexToDelete >= 0)
                    {
                        // 割り当てを未分類へ戻してからセクション定義を消す
                        for (auto &[key, a] : assignments_)
                        {
                            (void)key;
                            if (a.window == w.name && a.tab == t.name && a.section == t.sections[sectionIndexToDelete].name)
                            {
                                a = {};
                            }
                        }
                        std::erase_if(assignments_, [](const auto &pair) { return pair.second.window.empty(); });
                        t.sections.erase(t.sections.begin() + sectionIndexToDelete);
                        layoutDirty_ = true;
                    }

                    ImGui::TreePop();
                }
                ImGui::PopID();
            }

            if (tabIndexToDelete >= 0)
            {
                for (auto &[key, a] : assignments_)
                {
                    (void)key;
                    if (a.window == w.name && a.tab == w.tabs[tabIndexToDelete].name)
                    {
                        a = {};
                    }
                }
                std::erase_if(assignments_, [](const auto &pair) { return pair.second.window.empty(); });
                w.tabs.erase(w.tabs.begin() + tabIndexToDelete);
                layoutDirty_ = true;
            }

            ImGui::TreePop();
        }
        ImGui::PopID();
    }

    if (windowIndexToDelete >= 0)
    {
        for (auto &[key, a] : assignments_)
        {
            (void)key;
            if (a.window == windows_[windowIndexToDelete].name)
            {
                a = {};
            }
        }
        std::erase_if(assignments_, [](const auto &pair) { return pair.second.window.empty(); });
        windows_.erase(windows_.begin() + windowIndexToDelete);
        layoutDirty_ = true;
    }
}

void GameParamHub::DrawHubWindow(bool *open)
{
    if (open && !*open)
    {
        return;
    }

    ImGui::Begin("ゲームパラメータHub", open, ImGuiWindowFlags_NoFocusOnAppearing);

    // ── ツールバー ──
    if (ImGui::Button("仕分けを保存"))
    {
        SaveLayout();
    }
    ImGui::SameLine();
    if (ImGui::Button("再読込"))
    {
        LoadLayout();
    }
    if (layoutDirty_)
    {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "※未保存の変更あり");
    }
    ImGui::TextDisabled("「≡」をドラッグ、または右クリックで仕分けできます");

    if (ImGui::BeginTabBar("hubTabs"))
    {
        // ── 未分類パラメータ（owner別にまとめて表示） ──
        if (ImGui::BeginTabItem("未分類"))
        {
            static ImGuiTextFilter filter;
            filter.Draw("検索", 180.0f);

            // owner の登場順を保ちながらグループ化する
            std::vector<std::string> owners;
            for (const auto &e : entries_)
            {
                if (std::find(owners.begin(), owners.end(), e.owner) == owners.end())
                {
                    owners.push_back(e.owner);
                }
            }

            for (const auto &owner : owners)
            {
                // 未分類のみ集める
                std::vector<std::string> keys;
                for (const auto &e : entries_)
                {
                    if (e.owner != owner)
                    {
                        continue;
                    }
                    const std::string key = MakeKey(e.owner, e.name);
                    if (assignments_.count(key))
                    {
                        continue;
                    }
                    if (!filter.PassFilter(key.c_str()))
                    {
                        continue;
                    }
                    keys.push_back(key);
                }
                if (keys.empty())
                {
                    continue;
                }
                if (ImGui::TreeNodeEx(owner.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
                {
                    DrawParamRows(keys);
                    ImGui::TreePop();
                }
            }
            ImGui::EndTabItem();
        }

        // ── レイアウト編集（ウィンドウ/タブ/セクション作成＋仕分け先ツリー） ──
        if (ImGui::BeginTabItem("レイアウト"))
        {
            DrawLayoutTree();
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    ImGui::End();
}

void GameParamHub::DrawUserWindows()
{
    for (const auto &w : windows_)
    {
        ImGui::Begin(w.name.c_str(), nullptr, ImGuiWindowFlags_NoFocusOnAppearing);

        // ウィンドウ直下
        DrawParamRows(CollectAssigned(w.name, "", ""));

        if (!w.tabs.empty())
        {
            if (ImGui::BeginTabBar(("tabs_" + w.name).c_str()))
            {
                for (const auto &t : w.tabs)
                {
                    if (ImGui::BeginTabItem(t.name.c_str()))
                    {
                        // タブ直下
                        DrawParamRows(CollectAssigned(w.name, t.name, ""));

                        for (const auto &s : t.sections)
                        {
                            if (ImGui::CollapsingHeader(s.name.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
                            {
                                DrawParamRows(CollectAssigned(w.name, t.name, s.name));
                            }
                        }
                        ImGui::EndTabItem();
                    }
                }
                ImGui::EndTabBar();
            }
        }

        ImGui::End();
    }
}

#endif // USE_IMGUI

} // namespace Hagine
