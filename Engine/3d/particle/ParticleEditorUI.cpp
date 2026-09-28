#include "ParticleEditorUI.h"
#ifdef USE_IMGUI
#include "utility/debug/imgui/ImGuizmoManager.h"
// DebugUIHelper.h は ImGui:: を使うので imgui.h（ImGuizmoManager.h 経由）の後に include する
#include "utility/debug/imgui/DebugUIHelper.h"
#include <icon/IconsFontAwesome5.h>
#include <utility/debug/imgui/ImGuiNotification.h>
#endif // USE_IMGUI
#include <algorithm>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>

namespace Hagine {
namespace ParticleEditorUI {

namespace {
// ファイル名に使えない文字
constexpr const char *kBadChars = "\\/:*?\"<>|";

// 大文字小文字を区別しない比較用
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

std::string JsonPathOf(const std::string &jsonDirectory, const std::string &name)
{
    return jsonDirectory + "/" + name + ".json";
}

bool ReadFileBytes(const std::string &path, std::string &outBytes)
{
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open())
        return false;
    outBytes.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    return true;
}

bool WriteFileBytes(const std::string &path, const std::string &bytes)
{
    std::filesystem::create_directories(std::filesystem::path(path).parent_path());
    std::ofstream file(path, std::ios::binary);
    if (!file.is_open())
        return false;
    file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    return true;
}

NameCheck CheckEmitterName(const std::string &name, const std::string &jsonDirectory, const std::function<bool(const std::string &)> &isLoaded)
{
    NameCheck result;
    if (name.empty())
    {
        return result;
    }
    if (name.find_first_of(kBadChars) != std::string::npos || name.front() == ' ' || name.back() == ' ')
    {
        result.message = "ファイル名に使えない文字（\\ / : * ? \" < > |・前後の空白）が入っています";
        return result;
    }
    if (isLoaded && isLoaded(name))
    {
        result.message = "同じ名前のエミッターがもうあります";
        return result;
    }
    if (std::filesystem::exists(JsonPathOf(jsonDirectory, name)))
    {
        result.message = "同じ名前の保存ファイルがあります（使うなら「保存済みから読み込む」で）";
        return result;
    }
    result.ok = true;
    return result;
}

std::string MakeUniqueEmitterName(const std::string &base, const std::string &jsonDirectory, const std::function<bool(const std::string &)> &isLoaded)
{
    if (CheckEmitterName(base, jsonDirectory, isLoaded).ok)
        return base;
    for (int i = 2; i < 1000; ++i)
    {
        const std::string candidate = std::format("{}_{}", base, i);
        if (CheckEmitterName(candidate, jsonDirectory, isLoaded).ok)
            return candidate;
    }
    return base + "_new";
}

NameCheck DrawNameField(const char *id, std::string &name, const char *hint, const std::string &jsonDirectory,
                        const std::function<bool(const std::string &)> &isLoaded)
{
    const NameCheck check = CheckEmitterName(name, jsonDirectory, isLoaded);
#ifdef USE_IMGUI
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint(id, hint, &name);
    if (!name.empty() && !check.ok)
    {
        ImGui::TextColored(DebugTheme::kAccentRed, ICON_FA_EXCLAMATION_TRIANGLE " %s", check.message.c_str());
    }
#else
    (void)id;
    (void)hint;
#endif // USE_IMGUI
    return check;
}

void DrawSavedFileLoader(const char *id, std::string &search, const std::string &jsonDirectory,
                         const std::function<bool(const std::string &)> &isLoaded, const std::function<void(const std::string &)> &load)
{
#ifdef USE_IMGUI
    ImGui::PushID(id);
    std::vector<std::string> names;
    if (std::filesystem::exists(jsonDirectory) && std::filesystem::is_directory(jsonDirectory))
    {
        for (const auto &entry : std::filesystem::directory_iterator(jsonDirectory))
        {
            if (entry.is_regular_file() && entry.path().extension() == ".json")
            {
                const std::string stem = entry.path().stem().string();
                if (!isLoaded || !isLoaded(stem))
                    names.push_back(stem);
            }
        }
    }
    std::sort(names.begin(), names.end());

    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##loadSearch", ICON_FA_SEARCH " 保存済みを名前で絞り込み", &search);
    const std::string query = Lower(search);

    ImGui::BeginChild("##loadList", ImVec2(0.0f, ImGui::GetTextLineHeightWithSpacing() * 5.5f), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeY);
    int shown = 0;
    std::string toLoad;
    for (const std::string &name : names)
    {
        if (!query.empty() && Lower(name).find(query) == std::string::npos)
            continue;
        ++shown;
        ImGui::PushID(name.c_str());
        if (ImGui::Selectable((std::string(ICON_FA_FILE_IMPORT " ") + name).c_str(), false, ImGuiSelectableFlags_AllowDoubleClick))
        {
            toLoad = name;
        }
        ImGui::SetItemTooltip("押すとエディタに読み込みます");
        ImGui::PopID();
    }
    if (shown == 0)
    {
        ImGui::TextDisabled(names.empty() ? "読み込んでいない保存ファイルはありません" : "一致する保存ファイルがありません");
    }
    ImGui::EndChild();
    if (!toLoad.empty() && load)
    {
        load(toLoad);
    }
    ImGui::PopID();
#else
    (void)id;
    (void)search;
    (void)jsonDirectory;
    (void)isLoaded;
    (void)load;
#endif // USE_IMGUI
}

void DrawEmitterDeleteList(const char *id, DeleteState &state, std::vector<std::string> names, const std::string &jsonDirectory,
                           const std::function<void(const std::string &)> &remove, const std::function<void(const std::string &)> &reload,
                           const std::function<std::string(const std::string &)> &describe)
{
#ifdef USE_IMGUI
    ImGui::PushID(id);
    std::sort(names.begin(), names.end());
    if (std::find(names.begin(), names.end(), state.selected) == names.end())
    {
        state.selected.clear();
    }

    if (names.empty())
    {
        DimText("エミッターがありません");
        ImGui::PopID();
        return;
    }

    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##deleteSearch", ICON_FA_SEARCH " エミッターを名前で絞り込み", &state.search);
    const std::string query = Lower(state.search);

    ImGui::BeginChild("##deleteList", ImVec2(0.0f, ImGui::GetTextLineHeightWithSpacing() * 7.5f), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeY);
    int shown = 0;
    for (const std::string &name : names)
    {
        if (!query.empty() && Lower(name).find(query) == std::string::npos)
            continue;
        ++shown;
        ImGui::PushID(name.c_str());
        const bool saved = std::filesystem::exists(JsonPathOf(jsonDirectory, name));
        if (ImGui::Selectable(name.c_str(), state.selected == name))
        {
            state.selected = name;
        }
        if (!saved)
        {
            ImGui::SameLine();
            ImGui::TextColored(DebugTheme::kAccentOrange, "(未保存)");
        }
        ImGui::PopID();
    }
    if (shown == 0)
    {
        ImGui::TextDisabled("一致するエミッターがありません");
    }
    ImGui::EndChild();

    const bool hasSelection = !state.selected.empty();
    if (hasSelection)
    {
        ImGui::TextColored(DebugTheme::kAccentCyan, ICON_FA_CHECK " %s", state.selected.c_str());
        if (describe)
        {
            const std::string info = describe(state.selected);
            if (!info.empty())
            {
                ImGui::SameLine();
                ImGui::TextDisabled("%s", info.c_str());
            }
        }
    }
    else
    {
        DimText("消すエミッターを一覧から選んでください");
    }

    ImGui::Checkbox("保存ファイルも消す", &state.deleteFile);
    ImGui::SetItemTooltip("切っておくと、エディタの一覧から外すだけで保存ファイルは残ります（あとで読み込み直せます）");

    ImGui::BeginDisabled(!hasSelection);
    if (DangerButton(ICON_FA_TRASH " 削除する...", ImVec2(-1.0f, 0.0f)))
    {
        state.pending = state.selected;
        ImGui::OpenPopup("##confirmDelete");
    }
    ImGui::EndDisabled();

    // ---- 確認窓 ----
    if (ImGui::BeginPopupModal("##confirmDelete", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        const std::string path = JsonPathOf(jsonDirectory, state.pending);
        const bool saved = std::filesystem::exists(path);
        ImGui::Text("エミッター「%s」を削除します。", state.pending.c_str());
        DimText("保存していない変更は失われます。");
        if (state.deleteFile && saved)
        {
            ImGui::TextColored(DebugTheme::kAccentOrange, ICON_FA_EXCLAMATION_TRIANGLE " 保存ファイルも消します: %s.json", state.pending.c_str());
        }
        else if (saved)
        {
            DimText("保存ファイルは残ります。");
        }
        ImGui::Spacing();
        if (DangerButton(ICON_FA_TRASH " 削除", ImVec2(120.0f, 0.0f)))
        {
            const std::string name = state.pending;
            // 元に戻せるよう、消すファイルの中身を覚えておく
            std::string bytes;
            const bool removeFile = state.deleteFile && saved && ReadFileBytes(path, bytes);
            if (remove)
                remove(name);
            if (removeFile)
            {
                std::error_code ec;
                std::filesystem::remove(path, ec);
            }
            state.selected.clear();
            state.pending.clear();

            // 保存ファイルがあれば読み込み直して戻せる（消したファイルは書き戻す）
            if (saved)
            {
                ImGuiNotification::PostWithAction("エミッターを削除しました: " + name, {0.82f, 0.58f, 0.36f, 1.0f}, "元に戻す",
                                                  [name, path, bytes, removeFile, reload] {
                                                      if (removeFile && !std::filesystem::exists(path))
                                                          WriteFileBytes(path, bytes);
                                                      if (reload)
                                                          reload(name);
                                                  });
            }
            else
            {
                ImGuiNotification::Post("エミッターを削除しました（未保存だったので元に戻せません）: " + name, {0.82f, 0.58f, 0.36f, 1.0f});
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (NeutralButton("やめる", ImVec2(120.0f, 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Escape))
        {
            state.pending.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    ImGui::PopID();
#else
    (void)id;
    (void)state;
    (void)names;
    (void)jsonDirectory;
    (void)remove;
    (void)reload;
    (void)describe;
#endif // USE_IMGUI
}

} // namespace ParticleEditorUI
} // namespace Hagine
