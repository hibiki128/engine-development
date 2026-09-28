#ifdef USE_IMGUI
#include "SettingsPanel.h"
#include "DebugUIHelper.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <icon/IconsFontAwesome5.h>
#include <nlohmann/json.hpp>
#include <set>
#include <utility/asset/AssetPath.h>

namespace Hagine {

namespace {
constexpr const char *kPinFile = "settings_panel.json";

// 固定した見出し（"窓の名前/見出し"）。全パネルで1つのファイルに保存する
std::set<std::string> &Pins()
{
    static std::set<std::string> pins;
    static bool loaded = false;
    if (!loaded)
    {
        loaded = true;
        std::ifstream file(AssetPath::Config(kPinFile));
        if (file.is_open())
        {
            try
            {
                nlohmann::json root;
                file >> root;
                for (const auto &item : root.value("pinned", nlohmann::json::array()))
                {
                    if (item.is_string())
                        pins.insert(item.get<std::string>());
                }
            }
            catch (...)
            {
            }
        }
    }
    return pins;
}

void SavePins()
{
    nlohmann::json root;
    root["pinned"] = nlohmann::json::array();
    for (const std::string &pin : Pins())
        root["pinned"].push_back(pin);
    std::filesystem::create_directories(AssetPath::ConfigRoot());
    std::ofstream file(AssetPath::Config(kPinFile));
    if (file.is_open())
        file << root.dump(2);
}

std::string Lower(std::string text)
{
    for (char &c : text)
    {
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c - 'A' + 'a');
    }
    return text;
}
} // namespace

void SettingsPanel::Add(const std::string &title, const char *icon, const ImVec4 &accent, std::function<void()> draw, const std::string &keywords)
{
    sections_.push_back({title, icon ? icon : "", accent, std::move(draw), keywords});
}

bool SettingsPanel::IsPinned(const std::string &title) const
{
    return Pins().contains(id_ + "/" + title);
}

void SettingsPanel::TogglePin(const std::string &title)
{
    const std::string key = id_ + "/" + title;
    if (!Pins().erase(key))
        Pins().insert(key);
    SavePins();
}

void SettingsPanel::DrawPinMenu(const std::string &title)
{
    if (ImGui::BeginPopupContextItem(("##pin_" + title).c_str()))
    {
        const bool pinned = IsPinned(title);
        if (ImGui::MenuItem(pinned ? ICON_FA_THUMBTACK " よく使うから外す" : ICON_FA_THUMBTACK " よく使う（先頭に固定）"))
        {
            TogglePin(title);
        }
        ImGui::EndPopup();
    }
}

void SettingsPanel::Draw()
{
    ImGui::PushID(id_.c_str());

    // ---- 上の操作列: 検索・表示の切り替え ----
    const float toggleWidth = ImGui::CalcTextSize(ICON_FA_LIST " 一覧").x + ImGui::GetStyle().FramePadding.x * 2.0f;
    ImGui::SetNextItemWidth(-toggleWidth - ImGui::GetStyle().ItemSpacing.x);
    const bool searchEdited = ImGui::InputTextWithHint("##search", ICON_FA_SEARCH " 見出しで絞り込み", &search_);
    if (ImGui::IsItemFocused() && ImGui::IsKeyPressed(ImGuiKey_Escape))
        search_.clear();
    ImGui::SameLine();
    if (NeutralButton(listMode_ ? ICON_FA_FOLDER " タブ" : ICON_FA_LIST " 一覧", ImVec2(toggleWidth, 0.0f)))
        listMode_ = !listMode_;
    ImGui::SetItemTooltip(listMode_ ? "タブで1つずつ表示する" : "折りたたみで縦に並べて表示する");

    // ---- 並べる順: 固定した物を先に（それぞれの中は登録順）----
    std::vector<const Section *> ordered;
    for (int pass = 0; pass < 2; ++pass)
    {
        for (const Section &section : sections_)
        {
            if (IsPinned(section.title) == (pass == 0))
                ordered.push_back(&section);
        }
    }
    const std::string query = Lower(search_);
    if (!query.empty())
    {
        std::erase_if(ordered, [&](const Section *s) {
            return Lower(s->title).find(query) == std::string::npos && Lower(s->keywords).find(query) == std::string::npos;
        });
    }

    if (sections_.empty())
    {
        DimText("このシーンには、ここで調整できる物がありません");
    }
    else if (ordered.empty())
    {
        DimText("一致する見出しがありません");
    }
    else if (listMode_ || !query.empty())
    {
        // 一覧表示（検索中はこちら。見つかった物は開いて見せる）
        for (const Section *section : ordered)
        {
            const bool pinned = IsPinned(section->title);
            const std::string label = (pinned ? std::string(ICON_FA_THUMBTACK " ") : std::string()) + section->icon + " " + section->title +
                                      "###" + section->title;
            if (!query.empty() && searchEdited)
                ImGui::SetNextItemOpen(true);
            const bool open = ThemedHeader(label.c_str(), section->accent, pinned);
            DrawPinMenu(section->title);
            if (open)
            {
                ImGui::PushID(section->title.c_str());
                ImGui::Indent(4.0f);
                section->draw();
                ImGui::Unindent(4.0f);
                ImGui::PopID();
                ImGui::Spacing();
            }
        }
    }
    else if (ImGui::BeginTabBar("##sections", ImGuiTabBarFlags_FittingPolicyScroll))
    {
        for (const Section *section : ordered)
        {
            const bool pinned = IsPinned(section->title);
            const std::string label = (pinned ? std::string(ICON_FA_THUMBTACK " ") : std::string()) + section->icon + " " + section->title +
                                      "###" + section->title;
            ImGui::PushStyleColor(ImGuiCol_TabSelectedOverline, section->accent);
            const bool open = ImGui::BeginTabItem(label.c_str());
            ImGui::PopStyleColor();
            DrawPinMenu(section->title);
            if (open)
            {
                ImGui::PushID(section->title.c_str());
                section->draw();
                ImGui::PopID();
                ImGui::EndTabItem();
            }
        }
        ImGui::EndTabBar();
    }

    sections_.clear();
    ImGui::PopID();
}

} // namespace Hagine
#endif // USE_IMGUI
