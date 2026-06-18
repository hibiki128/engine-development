#ifdef _DEBUG
#define IMGUI_DEFINE_MATH_OPERATORS
#define NOMINMAX
#include "ShowFolder.h"
#include <Graphics/Texture/TextureManager.h>
#include <algorithm>
#include <externals/icon/IconsFontAwesome5.h>
#include <filesystem>
#include <imgui.h>
#include <string>
#include <unordered_map>
#include <vector>

namespace Hagine {
void ShowTextureFile(std::string &selectedTexturePath) {
    namespace fs = std::filesystem;
    ImGuiStyle &style = ImGui::GetStyle();

    static fs::path baseDirTex = "resources/images/";
    static fs::path currentDirTex = "resources/images";
    static std::string selectedFolderTex;
    static std::string selectedFileTex;
    static std::unordered_map<std::string, TextureCache> texCache;
    static ImGuiTextFilter filter;

    // パンくずリスト
    {
        ImGui::PushStyleColor(ImGuiCol_Button, {0.0f, 0.0f, 0.0f, 0.0f});
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, {0.3f, 0.3f, 0.3f, 0.5f});
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, {0.5f, 0.5f, 0.5f, 0.8f});

        fs::path tmpPath = baseDirTex;
        if (ImGui::Button("home##texhome")) {
            currentDirTex = baseDirTex;
            selectedFolderTex = selectedFileTex = "";
        }
        fs::path rel = currentDirTex.lexically_relative(baseDirTex);
        if (!rel.empty() && rel != ".") {
            for (auto &part : rel) {
                ImGui::SameLine();
                ImGui::TextUnformatted(" > ");
                ImGui::SameLine();
                tmpPath /= part;
                if (ImGui::Button(part.string().c_str())) {
                    currentDirTex = tmpPath;
                    selectedFolderTex = selectedFileTex = "";
                }
            }
        }
        ImGui::PopStyleColor(3);
        ImGui::Separator();
    }

    filter.Draw("##texsearch", ImGui::GetContentRegionAvail().x);
    ImGui::Spacing();

    std::vector<std::string> folders, files;
    try {
        for (const auto &e : fs::directory_iterator(currentDirTex)) {
            if (e.is_directory()) {
                folders.push_back(e.path().filename().string());
            } else {
                auto ext = e.path().extension();
                if (ext == ".png" || ext == ".jpg")
                    files.push_back(e.path().filename().string());
            }
        }
        std::sort(folders.begin(), folders.end());
        std::sort(files.begin(), files.end());
    } catch (std::exception &ex) {
        ImGui::TextColored({1.0f, 0.3f, 0.3f, 1.0f}, "Error: %s", ex.what());
    }

    // 高さ固定の BeginChild → 他の Header が開いていても潰れない
    const float kBrowserH = 300.0f;
    ImGui::BeginChild("TexBrowser##child", ImVec2(0, kBrowserH), true,
                      ImGuiWindowFlags_AlwaysVerticalScrollbar);

    // フォルダ
    if (!folders.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Header, {0.3f, 0.3f, 0.7f, 0.5f});
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, {0.4f, 0.4f, 0.8f, 0.6f});
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, {0.5f, 0.5f, 0.9f, 0.7f});
        if (ImGui::CollapsingHeader("Folders##texfolders", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Indent(10.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,
                                ImVec2(style.ItemSpacing.x, 8.0f));
            for (auto &folder : folders) {
                if (!filter.PassFilter(folder.c_str()))
                    continue;
                ImGui::PushStyleColor(ImGuiCol_Text, {1.0f, 0.9f, 0.4f, 1.0f});
                ImGui::TextUnformatted(ICON_FA_FOLDER);
                ImGui::PopStyleColor();
                ImGui::SameLine();
                if (ImGui::Selectable(folder.c_str(), selectedFolderTex == folder,
                                      ImGuiSelectableFlags_AllowDoubleClick)) {
                    if (ImGui::IsMouseDoubleClicked(0)) {
                        selectedFolderTex = folder;
                        currentDirTex /= folder;
                        selectedFileTex = "";
                    }
                }
                if (ImGui::BeginPopupContextItem(folder.c_str())) {
                    if (ImGui::MenuItem("Open")) {
                        selectedFolderTex = folder;
                        currentDirTex /= folder;
                        selectedFileTex = "";
                    }
                    ImGui::EndPopup();
                }
            }
            ImGui::PopStyleVar();
            ImGui::Unindent(10.0f);
        }
        ImGui::PopStyleColor(3);
    }

    // テクスチャファイル（グリッド）
    if (!files.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Header, {0.3f, 0.7f, 0.3f, 0.5f});
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, {0.4f, 0.8f, 0.4f, 0.6f});
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, {0.5f, 0.9f, 0.5f, 0.7f});
        if (ImGui::CollapsingHeader("Textures##texfiles", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Indent(10.0f);

            const float kCell = 140.0f; // プレビューサイズを拡大
            int cols = std::max(1, (int)(ImGui::GetContentRegionAvail().x / kCell));
            ImGui::Columns(cols, "TexGrid##g", false);

            for (const auto &file : files) {
                if (!filter.PassFilter(file.c_str()))
                    continue;

                fs::path fullPath = currentDirTex / file;
                std::string rel = fullPath.lexically_relative(baseDirTex).string();
                std::replace(rel.begin(), rel.end(), '\\', '/');

                if (!texCache.contains(rel)) {
                    auto *mgr = TextureManager::GetInstance();
                    mgr->LoadTexture(rel);
                    uint32_t idx = mgr->GetTextureIndexByFilePath(rel);
                    auto &meta = mgr->GetMetaData(rel);
                    TextureCache c;
                    c.srvIndex = idx;
                    c.handle = mgr->GetSrvManager()->GetGPUDescriptorHandle(idx);
                    c.width = (int)meta.width;
                    c.height = (int)meta.height;
                    texCache[rel] = c;
                }
                auto &cache = texCache[rel];

                ImGui::PushID(file.c_str());
                bool sel = (selectedTexturePath == rel);

                ImGui::PushStyleColor(ImGuiCol_Button,
                                      sel ? ImVec4{0.4f, 0.6f, 0.8f, 0.5f} : ImVec4{0.2f, 0.2f, 0.2f, 0.3f});
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, {0.5f, 0.7f, 0.9f, 0.6f});
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, {0.6f, 0.8f, 1.0f, 0.7f});

                const float kBtn = kCell - 10.f;
                if (ImGui::Button("##tbtn", ImVec2(kBtn, kBtn)))
                    selectedTexturePath = rel;

                ImGui::PopStyleColor(3);

                ImVec2 bMin = ImGui::GetItemRectMin();
                ImVec2 bMax = ImGui::GetItemRectMax();
                ImVec2 imgMin = {bMin.x + 4.f, bMin.y + 4.f};
                ImVec2 imgMax = {bMax.x - 4.f, bMax.y - 22.f};
                ImGui::GetWindowDrawList()->AddImage(
                    (ImTextureID)cache.handle.ptr, imgMin, imgMax);

                std::string name = file.size() > 16 ? file.substr(0, 13) + "..." : file;
                ImGui::GetWindowDrawList()->AddText(
                    {bMin.x + 4.f, bMax.y - 19.f},
                    IM_COL32(220, 220, 220, 255), name.c_str());

                if (sel) {
                    ImGui::GetWindowDrawList()->AddRect(
                        bMin, bMax, IM_COL32(80, 160, 255, 220), 3.0f, 0, 2.0f);
                }

                ImGui::PopID();
                ImGui::NextColumn();
            }

            ImGui::Columns(1);
            ImGui::Unindent(10.0f);
        }
        ImGui::PopStyleColor(3);
    }

    ImGui::EndChild();

    ImGui::Separator();
    ImGui::PushStyleColor(ImGuiCol_Text, {0.55f, 0.55f, 0.60f, 1.0f});
    ImGui::Text("Path: %s  |  %zu files", currentDirTex.string().c_str(), files.size());
    ImGui::PopStyleColor();
}

// ============================================================
// ShowModelFile  (BeginChild 高さ固定化)
// ============================================================
void ShowModelFile(std::string &selectedModelPath) {
    namespace fs = std::filesystem;
    ImGuiStyle &style = ImGui::GetStyle();

    static fs::path baseDirModel = "resources/models/";
    static fs::path currentDirModel = "resources/models";
    static std::string selectedFolderModel;
    static std::string selectedFileModel;
    static ImGuiTextFilter filter;
    static bool showDetails = true;

    {
        ImGui::PushStyleColor(ImGuiCol_Button, {0.0f, 0.0f, 0.0f, 0.0f});
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, {0.3f, 0.3f, 0.3f, 0.5f});
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, {0.5f, 0.5f, 0.5f, 0.8f});
        fs::path tmpPath = baseDirModel;
        if (ImGui::Button("home##mdlhome")) {
            currentDirModel = baseDirModel;
            selectedFolderModel = selectedFileModel = "";
        }
        fs::path rel = currentDirModel.lexically_relative(baseDirModel);
        if (!rel.empty() && rel != ".") {
            for (auto &part : rel) {
                ImGui::SameLine();
                ImGui::TextUnformatted(" > ");
                ImGui::SameLine();
                tmpPath /= part;
                if (ImGui::Button(part.string().c_str())) {
                    currentDirModel = tmpPath;
                    selectedFolderModel = selectedFileModel = "";
                }
            }
        }
        ImGui::PopStyleColor(3);
        ImGui::Separator();
    }

    filter.Draw("##mdlsearch", ImGui::GetContentRegionAvail().x - 90.f);
    ImGui::SameLine();
    if (ImGui::SmallButton(showDetails ? "Simple##mdlv" : "Detail##mdlv"))
        showDetails = !showDetails;
    ImGui::Spacing();

    std::vector<std::string> folders, files;
    try {
        for (const auto &e : fs::directory_iterator(currentDirModel)) {
            if (e.is_directory()) {
                folders.push_back(e.path().filename().string());
            } else {
                auto ext = e.path().extension().string();
                if (ext == ".obj" || ext == ".fbx" || ext == ".gltf" || ext == ".glb" ||
                    ext == ".dae" || ext == ".3ds" || ext == ".ply" || ext == ".x3d")
                    files.push_back(e.path().filename().string());
            }
        }
        std::sort(folders.begin(), folders.end());
        std::sort(files.begin(), files.end());
    } catch (std::exception &ex) {
        ImGui::TextColored({1.0f, 0.3f, 0.3f, 1.0f}, "Error: %s", ex.what());
    }

    const float kBrowserH = 260.0f;
    ImGui::BeginChild("MdlBrowser##child", ImVec2(0, kBrowserH), true,
                      ImGuiWindowFlags_AlwaysVerticalScrollbar);

    if (!folders.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Header, {0.3f, 0.3f, 0.7f, 0.5f});
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, {0.4f, 0.4f, 0.8f, 0.6f});
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, {0.5f, 0.5f, 0.9f, 0.7f});
        if (ImGui::CollapsingHeader("Folders##mdlfolders", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Indent(10.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(style.ItemSpacing.x, 8.0f));
            for (auto &folder : folders) {
                if (!filter.PassFilter(folder.c_str()))
                    continue;
                ImGui::PushStyleColor(ImGuiCol_Text, {1.0f, 0.9f, 0.4f, 1.0f});
                ImGui::TextUnformatted(ICON_FA_FOLDER);
                ImGui::PopStyleColor();
                ImGui::SameLine();
                if (ImGui::Selectable(folder.c_str(), selectedFolderModel == folder,
                                      ImGuiSelectableFlags_AllowDoubleClick)) {
                    if (ImGui::IsMouseDoubleClicked(0)) {
                        selectedFolderModel = folder;
                        currentDirModel /= folder;
                        selectedFileModel = "";
                    }
                }
                if (ImGui::BeginPopupContextItem(folder.c_str())) {
                    if (ImGui::MenuItem("Open")) {
                        selectedFolderModel = folder;
                        currentDirModel /= folder;
                        selectedFileModel = "";
                    }
                    ImGui::EndPopup();
                }
            }
            ImGui::PopStyleVar();
            ImGui::Unindent(10.0f);
        }
        ImGui::PopStyleColor(3);
    }

    if (!files.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Header, {0.7f, 0.3f, 0.3f, 0.5f});
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, {0.8f, 0.4f, 0.4f, 0.6f});
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, {0.9f, 0.5f, 0.5f, 0.7f});
        if (ImGui::CollapsingHeader("Model Files##mdlfiles", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Indent(10.0f);

            auto getColor = [](const std::string &ext) -> ImVec4 {
                if (ext == ".obj" || ext == ".fbx")
                    return {1.0f, 0.8f, 0.4f, 1.0f};
                if (ext == ".gltf" || ext == ".glb")
                    return {0.4f, 1.0f, 0.8f, 1.0f};
                return {1.0f, 0.4f, 0.4f, 1.0f};
            };
            auto getRelPath = [&](const std::string &f) {
                std::string p = (currentDirModel / f).lexically_relative(baseDirModel).string();
                std::replace(p.begin(), p.end(), '\\', '/');
                return p;
            };

            if (showDetails) {
                ImGui::Columns(2, "MdlList##c", true);
                ImGui::SetColumnWidth(0, ImGui::GetWindowWidth() * 0.7f);
                ImGui::TextUnformatted("Name");
                ImGui::NextColumn();
                ImGui::TextUnformatted("Ext");
                ImGui::NextColumn();
                ImGui::Separator();
                for (const auto &file : files) {
                    if (!filter.PassFilter(file.c_str()))
                        continue;
                    std::string ext = fs::path(file).extension().string();
                    ImGui::PushStyleColor(ImGuiCol_Text, getColor(ext));
                    ImGui::TextUnformatted(ICON_FA_CUBE);
                    ImGui::PopStyleColor();
                    ImGui::SameLine();
                    bool sel = (file == selectedFileModel);
                    if (ImGui::Selectable(file.c_str(), sel, ImGuiSelectableFlags_SpanAllColumns)) {
                        selectedFileModel = file;
                        selectedModelPath = getRelPath(file);
                    }
                    ImGui::NextColumn();
                    ImGui::TextUnformatted(ext.c_str());
                    ImGui::NextColumn();
                }
                ImGui::Columns(1);
            } else {
                const float kCell = 100.0f;
                int cols = std::max(1, (int)(ImGui::GetContentRegionAvail().x / kCell));
                ImGui::Columns(cols, "MdlGrid##g", false);
                for (const auto &file : files) {
                    if (!filter.PassFilter(file.c_str()))
                        continue;
                    bool sel = (file == selectedFileModel);
                    ImGui::PushStyleColor(ImGuiCol_Button,
                                          sel ? ImVec4{0.7f, 0.5f, 0.5f, 0.7f} : ImVec4{0.3f, 0.3f, 0.3f, 0.0f});
                    ImGui::PushID(file.c_str());
                    if (ImGui::Button("##mbtn", ImVec2(kCell - 10, kCell - 10))) {
                        selectedFileModel = file;
                        selectedModelPath = getRelPath(file);
                    }
                    ImGui::PopID();
                    ImGui::PopStyleColor();
                    std::string name = file.size() > 12 ? file.substr(0, 9) + "..." : file;
                    ImGui::TextWrapped("%s", name.c_str());
                    ImGui::NextColumn();
                }
                ImGui::Columns(1);
            }
            ImGui::Unindent(10.0f);
        }
        ImGui::PopStyleColor(3);
    }

    ImGui::EndChild();
    ImGui::Separator();
    ImGui::PushStyleColor(ImGuiCol_Text, {0.55f, 0.55f, 0.60f, 1.0f});
    ImGui::Text("Path: %s  |  %zu files", currentDirModel.string().c_str(), files.size());
    if (!selectedFileModel.empty())
        ImGui::Text("Selected: %s", selectedModelPath.c_str());
    ImGui::PopStyleColor();
}

// ============================================================
// ShowJsonFile  (BeginChild 高さ固定化)
// ============================================================
void ShowJsonFile(std::string &selectedJsonPath, std::string &startPath) {
    namespace fs = std::filesystem;
    ImGuiStyle &style = ImGui::GetStyle();

    static fs::path baseDirJson = "resources/jsons/" + startPath;
    static fs::path currentDirJson = "resources/jsons/" + startPath;
    static std::string selectedFolderJson;
    static std::string selectedFileJson;
    static ImGuiTextFilter filter;
    static bool showDetails = true;

    {
        ImGui::PushStyleColor(ImGuiCol_Button, {0.0f, 0.0f, 0.0f, 0.0f});
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, {0.3f, 0.3f, 0.3f, 0.5f});
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, {0.5f, 0.5f, 0.5f, 0.8f});
        fs::path tmpPath = baseDirJson;
        if (ImGui::Button("home##jsonhome")) {
            currentDirJson = baseDirJson;
            selectedFolderJson = selectedFileJson = "";
        }
        fs::path rel = currentDirJson.lexically_relative(baseDirJson);
        if (!rel.empty() && rel != ".") {
            for (auto &part : rel) {
                ImGui::SameLine();
                ImGui::TextUnformatted(" > ");
                ImGui::SameLine();
                tmpPath /= part;
                if (ImGui::Button(part.string().c_str())) {
                    currentDirJson = tmpPath;
                    selectedFolderJson = selectedFileJson = "";
                }
            }
        }
        ImGui::PopStyleColor(3);
        ImGui::Separator();
    }

    filter.Draw("##jsonsearch", ImGui::GetContentRegionAvail().x - 90.f);
    ImGui::SameLine();
    if (ImGui::SmallButton(showDetails ? "Simple##jsonv" : "Detail##jsonv"))
        showDetails = !showDetails;
    ImGui::Spacing();

    std::vector<std::string> folders, files;
    try {
        for (const auto &e : fs::directory_iterator(currentDirJson)) {
            if (e.is_directory()) {
                folders.push_back(e.path().filename().string());
            } else {
                auto ext = e.path().extension().string();
                if (ext == ".json" || ext == ".jsonl" || ext == ".geojson")
                    files.push_back(e.path().filename().string());
            }
        }
        std::sort(folders.begin(), folders.end());
        std::sort(files.begin(), files.end());
    } catch (std::exception &ex) {
        ImGui::TextColored({1.0f, 0.3f, 0.3f, 1.0f}, "Error: %s", ex.what());
    }

    const float kBrowserH = 260.0f;
    ImGui::BeginChild("JsonBrowser##child", ImVec2(0, kBrowserH), true,
                      ImGuiWindowFlags_AlwaysVerticalScrollbar);

    if (!folders.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Header, {0.3f, 0.3f, 0.7f, 0.5f});
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, {0.4f, 0.4f, 0.8f, 0.6f});
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, {0.5f, 0.5f, 0.9f, 0.7f});
        if (ImGui::CollapsingHeader("Folders##jsonfolders", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Indent(10.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(style.ItemSpacing.x, 8.0f));
            for (auto &folder : folders) {
                if (!filter.PassFilter(folder.c_str()))
                    continue;
                ImGui::PushStyleColor(ImGuiCol_Text, {1.0f, 0.9f, 0.4f, 1.0f});
                ImGui::TextUnformatted(ICON_FA_FOLDER);
                ImGui::PopStyleColor();
                ImGui::SameLine();
                if (ImGui::Selectable(folder.c_str(), selectedFolderJson == folder,
                                      ImGuiSelectableFlags_AllowDoubleClick)) {
                    if (ImGui::IsMouseDoubleClicked(0)) {
                        selectedFolderJson = folder;
                        currentDirJson /= folder;
                        selectedFileJson = "";
                    }
                }
                if (ImGui::BeginPopupContextItem(folder.c_str())) {
                    if (ImGui::MenuItem("Open")) {
                        selectedFolderJson = folder;
                        currentDirJson /= folder;
                        selectedFileJson = "";
                    }
                    ImGui::EndPopup();
                }
            }
            ImGui::PopStyleVar();
            ImGui::Unindent(10.0f);
        }
        ImGui::PopStyleColor(3);
    }

    if (!files.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Header, {0.3f, 0.7f, 0.7f, 0.5f});
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, {0.4f, 0.8f, 0.8f, 0.6f});
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, {0.5f, 0.9f, 0.9f, 0.7f});
        if (ImGui::CollapsingHeader("JSON Files##jsonfiles", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Indent(10.0f);

            auto getIconColor = [](const std::string &ext) -> ImVec4 {
                if (ext == ".json")
                    return {1.0f, 0.9f, 0.4f, 1.0f};
                if (ext == ".jsonl")
                    return {0.4f, 0.8f, 1.0f, 1.0f};
                if (ext == ".geojson")
                    return {0.4f, 1.0f, 0.6f, 1.0f};
                return {0.8f, 0.8f, 0.8f, 1.0f};
            };
            auto getRelPath = [&](const std::string &f) {
                std::string p = (currentDirJson / f).lexically_relative(baseDirJson).string();
                std::replace(p.begin(), p.end(), '\\', '/');
                return p;
            };

            if (showDetails) {
                ImGui::Columns(2, "JsonList##c", true);
                ImGui::SetColumnWidth(0, ImGui::GetWindowWidth() * 0.7f);
                ImGui::TextUnformatted("Name");
                ImGui::NextColumn();
                ImGui::TextUnformatted("Ext");
                ImGui::NextColumn();
                ImGui::Separator();
                for (const auto &file : files) {
                    if (!filter.PassFilter(file.c_str()))
                        continue;
                    std::string ext = fs::path(file).extension().string();
                    ImGui::PushStyleColor(ImGuiCol_Text, getIconColor(ext));
                    ImGui::TextUnformatted(ICON_FA_FILE_CODE);
                    ImGui::PopStyleColor();
                    ImGui::SameLine();
                    bool sel = (file == selectedFileJson);
                    if (ImGui::Selectable(file.c_str(), sel, ImGuiSelectableFlags_SpanAllColumns)) {
                        selectedFileJson = file;
                        selectedJsonPath = getRelPath(file);
                    }
                    ImGui::NextColumn();
                    ImGui::TextUnformatted(ext.c_str());
                    ImGui::NextColumn();
                }
                ImGui::Columns(1);
            } else {
                const float kCell = 100.0f;
                int cols = std::max(1, (int)(ImGui::GetContentRegionAvail().x / kCell));
                ImGui::Columns(cols, "JsonGrid##g", false);
                for (const auto &file : files) {
                    if (!filter.PassFilter(file.c_str()))
                        continue;
                    bool sel = (file == selectedFileJson);
                    ImGui::PushStyleColor(ImGuiCol_Button,
                                          sel ? ImVec4{0.5f, 0.7f, 0.7f, 0.7f} : ImVec4{0.3f, 0.3f, 0.3f, 0.0f});
                    ImGui::PushID(file.c_str());
                    if (ImGui::Button("##jbtn", ImVec2(kCell - 10, kCell - 10))) {
                        selectedFileJson = file;
                        selectedJsonPath = getRelPath(file);
                    }
                    ImGui::PopID();
                    ImGui::PopStyleColor();
                    std::string name = file.size() > 12 ? file.substr(0, 9) + "..." : file;
                    ImGui::TextWrapped("%s", name.c_str());
                    ImGui::NextColumn();
                }
                ImGui::Columns(1);
            }
            ImGui::Unindent(10.0f);
        }
        ImGui::PopStyleColor(3);
    }

    ImGui::EndChild();
    ImGui::Separator();
    ImGui::PushStyleColor(ImGuiCol_Text, {0.55f, 0.55f, 0.60f, 1.0f});
    ImGui::Text("Path: %s  |  %zu files", currentDirJson.string().c_str(), files.size());
    if (!selectedFileJson.empty())
        ImGui::Text("Selected: %s", selectedJsonPath.c_str());
    ImGui::PopStyleColor();
}

// ============================================================
// ShowGltfFile  — resources/models/animation 以下の .gltf / .glb を閲覧する
//                 選択されたパスは resources/models を基点とした相対パスで返す
//                 例: animation/walk.gltf
// ============================================================
void ShowGltfFile(std::string &selectedGltfPath) {
    namespace fs = std::filesystem;
    ImGuiStyle &style = ImGui::GetStyle();

    // animation フォルダをルートとするが、SetAnimation に渡すパスの基点は
    // その一つ上の models/ なので parent_path() を相対パス計算に使う
    static fs::path baseDirGltf = "resources/models/animation";
    static fs::path currentDirGltf = "resources/models/animation";
    static std::string selectedFolderGltf;
    static std::string selectedFileGltf;
    static ImGuiTextFilter filter;
    static bool showDetails = true;

    // パンくずリスト（home → サブフォルダ順に並ぶ）
    {
        ImGui::PushStyleColor(ImGuiCol_Button, {0.0f, 0.0f, 0.0f, 0.0f});
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, {0.3f, 0.3f, 0.3f, 0.5f});
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, {0.5f, 0.5f, 0.5f, 0.8f});

        fs::path tmpPath = baseDirGltf;
        if (ImGui::Button("home##gltfhome")) {
            currentDirGltf = baseDirGltf;
            selectedFolderGltf = selectedFileGltf = "";
        }
        fs::path rel = currentDirGltf.lexically_relative(baseDirGltf);
        if (!rel.empty() && rel != ".") {
            for (auto &part : rel) {
                ImGui::SameLine();
                ImGui::TextUnformatted(" > ");
                ImGui::SameLine();
                tmpPath /= part;
                if (ImGui::Button(part.string().c_str())) {
                    currentDirGltf = tmpPath;
                    selectedFolderGltf = selectedFileGltf = "";
                }
            }
        }
        ImGui::PopStyleColor(3);
        ImGui::Separator();
    }

    // 検索バー + Detail / Simple 切り替えボタン
    filter.Draw("##gltfsearch", ImGui::GetContentRegionAvail().x - 90.f);
    ImGui::SameLine();
    if (ImGui::SmallButton(showDetails ? "Simple##gltfv" : "Detail##gltfv"))
        showDetails = !showDetails;
    ImGui::Spacing();

    // カレントディレクトリのフォルダ・ファイルを列挙
    std::vector<std::string> folders, files;
    try {
        for (const auto &e : fs::directory_iterator(currentDirGltf)) {
            if (e.is_directory()) {
                folders.push_back(e.path().filename().string());
            } else {
                auto ext = e.path().extension().string();
                if (ext == ".gltf" || ext == ".glb")
                    files.push_back(e.path().filename().string());
            }
        }
        std::sort(folders.begin(), folders.end());
        std::sort(files.begin(), files.end());
    } catch (std::exception &ex) {
        ImGui::TextColored({1.0f, 0.3f, 0.3f, 1.0f}, "Error: %s", ex.what());
    }

    const float kBrowserH = 260.0f;
    ImGui::BeginChild("GltfBrowser##child", ImVec2(0, kBrowserH), true,
                      ImGuiWindowFlags_AlwaysVerticalScrollbar);

    // フォルダ一覧セクション
    if (!folders.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Header, {0.3f, 0.3f, 0.7f, 0.5f});
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, {0.4f, 0.4f, 0.8f, 0.6f});
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, {0.5f, 0.5f, 0.9f, 0.7f});
        if (ImGui::CollapsingHeader("Folders##gltffolders", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Indent(10.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(style.ItemSpacing.x, 8.0f));
            for (auto &folder : folders) {
                if (!filter.PassFilter(folder.c_str()))
                    continue;
                ImGui::PushStyleColor(ImGuiCol_Text, {1.0f, 0.9f, 0.4f, 1.0f});
                ImGui::TextUnformatted(ICON_FA_FOLDER);
                ImGui::PopStyleColor();
                ImGui::SameLine();
                if (ImGui::Selectable(folder.c_str(), selectedFolderGltf == folder,
                                      ImGuiSelectableFlags_AllowDoubleClick)) {
                    if (ImGui::IsMouseDoubleClicked(0)) {
                        selectedFolderGltf = folder;
                        currentDirGltf /= folder;
                        selectedFileGltf = "";
                    }
                }
                if (ImGui::BeginPopupContextItem(folder.c_str())) {
                    if (ImGui::MenuItem("Open")) {
                        selectedFolderGltf = folder;
                        currentDirGltf /= folder;
                        selectedFileGltf = "";
                    }
                    ImGui::EndPopup();
                }
            }
            ImGui::PopStyleVar();
            ImGui::Unindent(10.0f);
        }
        ImGui::PopStyleColor(3);
    }

    // アニメーションファイル一覧セクション
    if (!files.empty()) {
        // 紫系：モデル（赤）・テクスチャ（緑）・JSON（青緑）と被らない色
        ImGui::PushStyleColor(ImGuiCol_Header, {0.5f, 0.3f, 0.7f, 0.5f});
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, {0.6f, 0.4f, 0.8f, 0.6f});
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, {0.7f, 0.5f, 0.9f, 0.7f});
        if (ImGui::CollapsingHeader("Animation Files##gltffiles", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Indent(10.0f);

            // 拡張子ごとのアイコン色（.gltf = 水色 / .glb = 薄紫）
            auto getColor = [](const std::string &ext) -> ImVec4 {
                if (ext == ".gltf")
                    return {0.5f, 0.9f, 1.0f, 1.0f};
                if (ext == ".glb")
                    return {0.8f, 0.6f, 1.0f, 1.0f};
                return {0.8f, 0.8f, 0.8f, 1.0f};
            };

            // resources/models を基点とした相対パスを生成（例: animation/walk.gltf）
            auto getRelPath = [&](const std::string &f) {
                std::string p = (currentDirGltf / f)
                                    .lexically_relative(baseDirGltf.parent_path())
                                    .string();
                std::replace(p.begin(), p.end(), '\\', '/');
                return p;
            };

            if (showDetails) {
                // 詳細ビュー：名前 + 拡張子を 2 列表示
                ImGui::Columns(2, "GltfList##c", true);
                ImGui::SetColumnWidth(0, ImGui::GetWindowWidth() * 0.7f);
                ImGui::TextUnformatted("Name");
                ImGui::NextColumn();
                ImGui::TextUnformatted("Ext");
                ImGui::NextColumn();
                ImGui::Separator();

                for (const auto &file : files) {
                    if (!filter.PassFilter(file.c_str()))
                        continue;
                    std::string ext = fs::path(file).extension().string();
                    ImGui::PushStyleColor(ImGuiCol_Text, getColor(ext));
                    ImGui::TextUnformatted(ICON_FA_FILM);
                    ImGui::PopStyleColor();
                    ImGui::SameLine();
                    bool sel = (file == selectedFileGltf);
                    if (ImGui::Selectable(file.c_str(), sel,
                                          ImGuiSelectableFlags_SpanAllColumns)) {
                        selectedFileGltf = file;
                        selectedGltfPath = getRelPath(file);
                    }
                    ImGui::NextColumn();
                    ImGui::TextUnformatted(ext.c_str());
                    ImGui::NextColumn();
                }
                ImGui::Columns(1);
            } else {
                // シンプルビュー：グリッド表示
                const float kCell = 100.0f;
                int cols = std::max(1, (int)(ImGui::GetContentRegionAvail().x / kCell));
                ImGui::Columns(cols, "GltfGrid##g", false);

                for (const auto &file : files) {
                    if (!filter.PassFilter(file.c_str()))
                        continue;
                    bool sel = (file == selectedFileGltf);
                    ImGui::PushStyleColor(ImGuiCol_Button,
                                          sel ? ImVec4{0.5f, 0.4f, 0.7f, 0.7f}
                                              : ImVec4{0.3f, 0.3f, 0.3f, 0.0f});
                    ImGui::PushID(file.c_str());
                    if (ImGui::Button("##gbtn", ImVec2(kCell - 10, kCell - 10))) {
                        selectedFileGltf = file;
                        selectedGltfPath = getRelPath(file);
                    }
                    ImGui::PopID();
                    ImGui::PopStyleColor();
                    std::string name = file.size() > 12 ? file.substr(0, 9) + "..." : file;
                    ImGui::TextWrapped("%s", name.c_str());
                    ImGui::NextColumn();
                }
                ImGui::Columns(1);
            }
            ImGui::Unindent(10.0f);
        }
        ImGui::PopStyleColor(3);
    }

    ImGui::EndChild();

    // ステータスバー（現在のディレクトリ・ファイル数・選択中パス）
    ImGui::Separator();
    ImGui::PushStyleColor(ImGuiCol_Text, {0.55f, 0.55f, 0.60f, 1.0f});
    ImGui::Text("Path: %s  |  %zu files", currentDirGltf.string().c_str(), files.size());
    if (!selectedFileGltf.empty())
        ImGui::Text("Selected: %s", selectedGltfPath.c_str());
    ImGui::PopStyleColor();
}

} // namespace Hagine
#endif // _DEBUG
