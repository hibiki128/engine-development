#include "EditorAssetBrowser.h"
#ifdef USE_IMGUI
#include "AssetDragDrop.h"
#include "ImGuiNotification.h"
#include "ImGuizmoManager.h"
#include <Audio.h>
#include <algorithm>
#include <asset/AssetPath.h>
#include <filesystem>
#include <format>
#include <fstream>
#include <graphics/texture/TextureManager.h>
#include <icon/IconsFontAwesome5.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <object/base/BaseObjectManager.h>
#include <set>
#include <shellapi.h>
// DebugUIHelper.h は ImVec4 / ImGui:: を使うので imgui.h の後に include する
#include "DebugUIHelper.h"

namespace Hagine {
namespace {
namespace fs = std::filesystem;

constexpr int kKindCount = static_cast<int>(EditorAssetBrowser::Kind::Count);

/// <summary>種類ごとの表示名</summary>
const char *KindLabel(EditorAssetBrowser::Kind kind)
{
    static const char *kLabels[kKindCount] = {"画像", "モデル", "プレハブ", "サウンド", "JSON"};
    return kLabels[static_cast<int>(kind)];
}

/// <summary>種類ごとのアイコン</summary>
const char *KindIcon(EditorAssetBrowser::Kind kind)
{
    static const char *kIcons[kKindCount] = {ICON_FA_IMAGE, ICON_FA_CUBE, ICON_FA_BOX, ICON_FA_MUSIC, ICON_FA_FILE_CODE};
    return kIcons[static_cast<int>(kind)];
}

/// <summary>種類ごとの色（サムネの色帯・アイコン）</summary>
ImVec4 KindColor(EditorAssetBrowser::Kind kind)
{
    static const ImVec4 kColors[kKindCount] = {DebugTheme::kAccentCyan, DebugTheme::kAccentOrange, DebugTheme::kAccentGreen,
                                               DebugTheme::kAccentPurple, DebugTheme::kAccentYellow};
    return kColors[static_cast<int>(kind)];
}

/// <summary>英字だけ小文字にする（日本語はそのまま）</summary>
std::string ToLowerAscii(std::string text)
{
    for (char &c : text)
    {
        if (c >= 'A' && c <= 'Z')
        {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return text;
}

/// <summary>拡張子（小文字・ドット付き）</summary>
std::string LowerExtension(const fs::path &path)
{
    return ToLowerAscii(path.extension().string());
}

/// <summary>バイト数を読みやすい単位で</summary>
std::string FormatSize(uintmax_t bytes)
{
    if (bytes >= 1024ull * 1024ull)
    {
        return std::format("{:.1f} MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
    }
    if (bytes >= 1024ull)
    {
        return std::format("{:.1f} KB", static_cast<double>(bytes) / 1024.0);
    }
    return std::format("{} B", bytes);
}

/// <summary>フォルダのパスの最後の名前（"a/b/c" → "c"）</summary>
std::string LastSegment(const std::string &folder)
{
    const size_t slash = folder.find_last_of('/');
    return slash == std::string::npos ? folder : folder.substr(slash + 1);
}

/// <summary>ツリーの親フォルダ（"a/b/c" → "a/b"、"a" → ""）</summary>
std::string ParentFolder(const std::string &folder)
{
    const size_t slash = folder.find_last_of('/');
    return slash == std::string::npos ? std::string() : folder.substr(0, slash);
}

/// <summary>エクスプローラーでファイルを選択した状態で開く</summary>
void RevealInExplorer(const std::string &path)
{
    std::error_code ec;
    const fs::path absolute = fs::absolute(fs::path(path), ec);
    const std::wstring arguments = L"/select,\"" + absolute.wstring() + L"\"";
    ShellExecuteW(nullptr, L"open", L"explorer.exe", arguments.c_str(), nullptr, SW_SHOWNORMAL);
}

/// <summary>設定ファイルのパス</summary>
std::string SettingsPath()
{
    return AssetPath::Json("ImGuiSetting/AssetBrowser.json");
}
} // namespace

// ---- 初期化・走査・設定 ---------------------------------------------------

void EditorAssetBrowser::Initialize()
{
    LoadSettings();
    Scan();
}

void EditorAssetBrowser::Scan()
{
    entries_.clear();
    for (auto &children : folderChildren_)
    {
        children.clear();
    }
    std::fill(std::begin(countByKind_), std::end(countByKind_), 0);

    // ルートを走査して、指定の拡張子のファイルを種類 kind として積む
    auto scanRoot = [this](Kind kind, const std::string &root, const std::vector<std::string> &extensions) {
        std::error_code ec;
        if (!fs::exists(root, ec))
        {
            return;
        }
        for (auto it = fs::recursive_directory_iterator(root, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec))
        {
            if (!it->is_regular_file(ec))
            {
                continue;
            }
            const std::string extension = LowerExtension(it->path());
            if (std::find(extensions.begin(), extensions.end(), extension) == extensions.end())
            {
                continue;
            }
            Entry entry;
            entry.kind = kind;
            entry.relPath = fs::relative(it->path(), root, ec).generic_string();
            entry.fullPath = it->path().generic_string();
            entry.name = it->path().filename().string();
            entry.folder = ParentFolder(entry.relPath);
            entry.fileSize = it->file_size(ec);
            if (kind == Kind::Prefab)
            {
                // プレハブは名前（拡張子なし）で扱う
                entry.name = it->path().stem().string();
                entry.relPath = entry.name;
                entry.folder.clear();
            }
            entries_.push_back(std::move(entry));
        }
    };

    for (const std::string &root : AssetPath::ImageScanRoots())
    {
        // dds は読み込みが重く、キューブマップなど2D表示できないものを含むので一覧に載せない
        scanRoot(Kind::Image, root, {".png", ".jpg", ".jpeg"});
    }
    for (const std::string &root : AssetPath::ModelScanRoots())
    {
        scanRoot(Kind::Model, root, {".obj", ".gltf", ".glb", ".fbx"});
    }
    scanRoot(Kind::Prefab, AssetPath::Json("Prefab"), {".json"});
    scanRoot(Kind::Sound, AssetPath::SoundRoot(), {".wav"});
    {
        // JSON はプレハブのフォルダを除く（プレハブは専用の欄で扱う）
        const size_t before = entries_.size();
        scanRoot(Kind::Json, AssetPath::JsonRoot(), {".json"});
        entries_.erase(std::remove_if(entries_.begin() + before, entries_.end(),
                                      [](const Entry &e) { return e.relPath.rfind("Prefab/", 0) == 0; }),
                       entries_.end());
    }

    std::sort(entries_.begin(), entries_.end(), [](const Entry &a, const Entry &b) {
        if (a.kind != b.kind)
            return a.kind < b.kind;
        if (a.folder != b.folder)
            return a.folder < b.folder;
        return a.name < b.name;
    });

    // フォルダツリー（途中のフォルダも全部登録する）
    std::set<std::pair<std::string, std::string>> edges[kKindCount];
    for (const Entry &entry : entries_)
    {
        const int kindIndex = static_cast<int>(entry.kind);
        ++countByKind_[kindIndex];
        std::string folder = entry.folder;
        while (!folder.empty())
        {
            edges[kindIndex].insert({ParentFolder(folder), folder});
            folder = ParentFolder(folder);
        }
    }
    for (int k = 0; k < kKindCount; ++k)
    {
        for (const auto &[parent, child] : edges[k])
        {
            folderChildren_[k][parent].push_back(child);
        }
    }

    // 見ていたフォルダが消えていたらルートへ戻す
    const auto &children = folderChildren_[static_cast<int>(currentKind_)];
    if (!currentFolder_.empty() && children.find(ParentFolder(currentFolder_)) == children.end())
    {
        currentFolder_.clear();
    }
    scanned_ = true;
}

void EditorAssetBrowser::LoadSettings()
{
    std::ifstream file(SettingsPath());
    if (!file)
    {
        return;
    }
    const nlohmann::json data = nlohmann::json::parse(file, nullptr, false);
    if (data.is_discarded())
    {
        return;
    }
    thumbSize_ = std::clamp(data.value("thumbSize", thumbSize_), 40.0f, 160.0f);
    autoReloadTextures_ = data.value("autoReloadTextures", autoReloadTextures_);
    listView_ = data.value("listView", listView_);
    sidebarWidth_ = std::clamp(data.value("sidebarWidth", sidebarWidth_), 120.0f, 480.0f);
    currentKind_ = static_cast<Kind>(std::clamp(data.value("kind", 0), 0, kKindCount - 1));
    if (data.contains("favorites") && data["favorites"].is_array())
    {
        favorites_ = data["favorites"].get<std::vector<std::string>>();
    }
    if (data.contains("recent") && data["recent"].is_array())
    {
        for (const auto &key : data["recent"])
        {
            if (key.is_string())
                recent_.push_back(key.get<std::string>());
        }
    }
}

void EditorAssetBrowser::SaveSettings() const
{
    nlohmann::json data;
    data["thumbSize"] = thumbSize_;
    data["autoReloadTextures"] = autoReloadTextures_;
    data["listView"] = listView_;
    data["sidebarWidth"] = sidebarWidth_;
    data["kind"] = static_cast<int>(currentKind_);
    data["favorites"] = favorites_;
    data["recent"] = std::vector<std::string>(recent_.begin(), recent_.end());
    std::error_code ec;
    fs::create_directories(fs::path(SettingsPath()).parent_path(), ec);
    std::ofstream file(SettingsPath());
    if (file)
    {
        file << data.dump(4);
    }
}

// ---- 小物 -----------------------------------------------------------------

std::string EditorAssetBrowser::KeyOf(const Entry &entry)
{
    return std::to_string(static_cast<int>(entry.kind)) + ":" + entry.relPath;
}

const EditorAssetBrowser::Entry *EditorAssetBrowser::FindByKey(const std::string &key) const
{
    for (const Entry &entry : entries_)
    {
        if (KeyOf(entry) == key)
        {
            return &entry;
        }
    }
    return nullptr;
}

bool EditorAssetBrowser::PassSearch(const Entry &entry) const
{
    if (search_.empty())
    {
        return true;
    }
    return ToLowerAscii(entry.relPath).find(ToLowerAscii(search_)) != std::string::npos;
}

bool EditorAssetBrowser::IsFavorite(const Entry &entry) const
{
    return std::find(favorites_.begin(), favorites_.end(), KeyOf(entry)) != favorites_.end();
}

void EditorAssetBrowser::ToggleFavorite(const Entry &entry)
{
    const std::string key = KeyOf(entry);
    auto it = std::find(favorites_.begin(), favorites_.end(), key);
    if (it != favorites_.end())
    {
        favorites_.erase(it);
    }
    else
    {
        favorites_.push_back(key);
    }
    SaveSettings();
}

void EditorAssetBrowser::Touch(const Entry &entry)
{
    const std::string key = KeyOf(entry);
    recent_.erase(std::remove(recent_.begin(), recent_.end(), key), recent_.end());
    recent_.push_front(key);
    while (recent_.size() > kMaxRecent)
    {
        recent_.pop_back();
    }
    SaveSettings();
}

uint64_t EditorAssetBrowser::GetThumbnail(const Entry &entry)
{
    if (entry.kind != Kind::Image)
    {
        return 0;
    }
    TextureManager *textureManager = TextureManager::GetInstance();
    textureManager->LoadTexture(entry.relPath); // 読み込み済みなら何もしない
    // キューブマップは SRV が TEXTURECUBE なので、2D として描くと GPU 検証で落ちる
    if (textureManager->GetMetaData(entry.relPath).IsCubemap())
    {
        return 0;
    }
    return textureManager->GetSrvHandleGPU(AssetPath::Image(entry.relPath)).ptr;
}

void EditorAssetBrowser::MakeDragSource(const Entry &entry, uint64_t textureId)
{
    const bool wasDragging = ImGui::GetDragDropPayload() != nullptr;
    switch (entry.kind)
    {
    case Kind::Image:
        AssetDragDrop::TextureSource(entry.relPath, static_cast<ImTextureID>(textureId));
        break;
    case Kind::Model:
        AssetDragDrop::ModelSource(entry.relPath);
        break;
    case Kind::Prefab:
        AssetDragDrop::PrefabSource(entry.relPath);
        break;
    case Kind::Sound:
        AssetDragDrop::SoundSource(entry.relPath);
        break;
    default:
        return;
    }
    // ドラッグを始めた瞬間に「最近使った」へ積む
    if (!wasDragging && ImGui::GetDragDropPayload() != nullptr)
    {
        Touch(entry);
    }
}

void EditorAssetBrowser::Activate(const Entry &entry)
{
    ImGuizmoManager *gizmo = ImGuizmoManager::GetInstance();
    switch (entry.kind)
    {
    case Kind::Model:
        if (!gizmo->PlaceModel(entry.relPath, gizmo->GetSpawnPosition()).empty())
        {
            ImGuiNotification::Post("モデルを置きました: " + entry.name, {0.45f, 0.68f, 0.52f, 1.0f});
        }
        break;
    case Kind::Prefab:
        gizmo->PlacePrefab(entry.relPath, gizmo->GetSpawnPosition());
        break;
    case Kind::Sound:
    {
        Audio *audio = Audio::GetInstance();
        if (previewSound_.IsValid() && audio->IsPlaying(previewSound_))
        {
            audio->Stop(previewSound_);
            if (previewSoundKey_ == KeyOf(entry))
            {
                previewSound_ = {};
                previewSoundKey_.clear();
                break; // 同じ音をもう一度押したら止めるだけ
            }
        }
        previewSound_ = audio->PlayOneShot(entry.relPath);
        previewSoundKey_ = KeyOf(entry);
        break;
    }
    default:
        ImGui::SetClipboardText(entry.relPath.c_str());
        ImGuiNotification::Post("パスをコピーしました: " + entry.relPath, {0.45f, 0.60f, 0.78f, 1.0f});
        break;
    }
    Touch(entry);
}

// ---- ホットリロード ---------------------------------------------------------

void EditorAssetBrowser::PollFileChanges()
{
    if (!autoReloadTextures_ || !scanned_)
    {
        return;
    }
    // ファイルの更新日時を見るだけだが、数百枚を毎フレーム調べる必要は無いので1秒おき
    const double now = ImGui::GetTime();
    if (now - lastPollTime_ < 1.0)
    {
        return;
    }
    lastPollTime_ = now;

    std::vector<std::string> reloaded;
    for (const Entry &entry : entries_)
    {
        if (entry.kind != Kind::Image)
        {
            continue;
        }
        std::error_code ec;
        const auto writeTime = fs::last_write_time(entry.fullPath, ec);
        if (ec)
        {
            continue;
        }
        const int64_t stamp = static_cast<int64_t>(writeTime.time_since_epoch().count());
        auto [it, inserted] = imageWriteTimes_.try_emplace(entry.relPath, stamp);
        if (inserted)
        {
            continue; // 初めて見たファイルは基準にするだけ
        }
        if (it->second != stamp)
        {
            it->second = stamp;
            // SRV の番号は据え置きで中身だけ差し替わるので、使っているスプライト・マテリアルもそのまま新しい絵になる
            TextureManager::GetInstance()->ReloadTexture(entry.relPath);
            reloaded.push_back(entry.name);
        }
    }
    if (reloaded.size() == 1)
    {
        ImGuiNotification::Post("画像を読み直しました: " + reloaded.front(), {0.45f, 0.68f, 0.52f, 1.0f});
    }
    else if (reloaded.size() > 1)
    {
        ImGuiNotification::Post(std::format("画像を {} 枚読み直しました", reloaded.size()), {0.45f, 0.68f, 0.52f, 1.0f});
    }
}

// ---- 描画 -----------------------------------------------------------------

void EditorAssetBrowser::Draw(bool *open)
{
    if (!open || !*open)
    {
        return;
    }
    if (!scanned_ || rescanRequested_)
    {
        rescanRequested_ = false;
        Scan();
    }

    ImGui::SetNextWindowSize(ImVec2(760.0f, 420.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("アセットブラウザ", open, ImGuiWindowFlags_NoFocusOnAppearing))
    {
        ImGui::End();
        return;
    }

    DrawToolbar();
    ImGui::Separator();

    // 左: ツリー（幅は右端をドラッグして変えられる）／右: 一覧
    ImGui::BeginChild("##assetSidebar", ImVec2(sidebarWidth_, 0.0f), ImGuiChildFlags_ResizeX | ImGuiChildFlags_Borders);
    DrawSidebar();
    const float width = ImGui::GetWindowWidth();
    if (std::abs(width - sidebarWidth_) > 0.5f && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
    {
        sidebarWidth_ = width;
        SaveSettings();
    }
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("##assetContent", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None);
    DrawContent();
    ImGui::EndChild();

    DrawDeletePrefabModal();
    ImGui::End();
}

void EditorAssetBrowser::DrawToolbar()
{
    // 検索（今の場所の下を、サブフォルダまで含めて探す）
    const float rightWidth = 285.0f;
    ImGui::SetNextItemWidth((std::max)(ImGui::GetContentRegionAvail().x - rightWidth, 120.0f));
    ImGui::InputTextWithHint("##assetSearch", ICON_FA_SEARCH " 名前で検索（サブフォルダも含む）", &search_);
    if (ImGui::IsItemFocused() && ImGui::IsKeyPressed(ImGuiKey_Escape))
    {
        search_.clear();
    }

    ImGui::SameLine();
    {
        ScopedButtonColors colors(listView_ ? DebugTheme::kButtonGhost : DebugTheme::kButtonNeutral, DebugTheme::kButtonNeutralHover);
        if (ImGui::Button(ICON_FA_TH_LARGE "##gridView"))
        {
            listView_ = false;
            SaveSettings();
        }
    }
    ImGui::SetItemTooltip("サムネイル表示");
    ImGui::SameLine(0.0f, 2.0f);
    {
        ScopedButtonColors colors(listView_ ? DebugTheme::kButtonNeutral : DebugTheme::kButtonGhost, DebugTheme::kButtonNeutralHover);
        if (ImGui::Button(ICON_FA_LIST "##listView"))
        {
            listView_ = true;
            SaveSettings();
        }
    }
    ImGui::SetItemTooltip("一覧表示");

    ImGui::SameLine();
    ImGui::BeginDisabled(listView_);
    ImGui::SetNextItemWidth(110.0f);
    ImGui::SliderFloat("##thumbSize", &thumbSize_, 40.0f, 160.0f, ICON_FA_SEARCH_PLUS " %.0f");
    // 保存はつまみを離してから
    if (ImGui::IsItemDeactivatedAfterEdit())
    {
        SaveSettings();
    }
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("サムネイルの大きさ（Ctrl+ホイールでも変えられます）");

    ImGui::SameLine();
    {
        ScopedButtonColors colors(autoReloadTextures_ ? DebugTheme::kButtonConfirm : DebugTheme::kButtonGhost,
                                  autoReloadTextures_ ? DebugTheme::kButtonConfirmHover : DebugTheme::kButtonNeutralHover);
        if (ImGui::Button(ICON_FA_BOLT "##hotReload"))
        {
            autoReloadTextures_ = !autoReloadTextures_;
            SaveSettings();
        }
    }
    ImGui::SetItemTooltip(autoReloadTextures_ ? "ホットリロード ON: 画像を上書き保存すると、その場で読み直します（再起動不要）"
                                              : "ホットリロード OFF（押すと ON）");
    ImGui::SameLine();
    if (NeutralButton(ICON_FA_SYNC_ALT "##rescan"))
    {
        Scan();
        ImGuiNotification::Post(std::format("アセットを読み直しました（{} 件）", entries_.size()), {0.45f, 0.60f, 0.78f, 1.0f});
    }
    ImGui::SetItemTooltip("フォルダを読み直す（ファイルを足した・消したとき）");
}

void EditorAssetBrowser::DrawSidebar()
{
    auto locationRow = [this](const char *label, Location location, size_t count) {
        const bool selected = (location_ == location);
        if (ImGui::Selectable(std::format("{}  ({})", label, count).c_str(), selected))
        {
            location_ = location;
        }
    };
    ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kAccentYellow);
    locationRow(ICON_FA_STAR " お気に入り", Location::Favorites, favorites_.size());
    ImGui::PopStyleColor();
    locationRow(ICON_FA_HISTORY " 最近使った", Location::Recent, recent_.size());
    ImGui::Separator();

    for (int k = 0; k < kKindCount; ++k)
    {
        const Kind kind = static_cast<Kind>(k);
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
        const bool isCurrentRoot = (location_ == Location::Folder && currentKind_ == kind && currentFolder_.empty());
        if (isCurrentRoot)
        {
            flags |= ImGuiTreeNodeFlags_Selected;
        }
        const auto &children = folderChildren_[k];
        if (children.find("") == children.end())
        {
            flags |= ImGuiTreeNodeFlags_Leaf;
        }
        const bool nodeOpen =
            ImGui::TreeNodeEx(KindLabel(kind), flags, "%s %s  (%d)", KindIcon(kind), KindLabel(kind), countByKind_[k]);
        // 行（矢印以外）を押したらその種類のルートを開く
        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
        {
            location_ = Location::Folder;
            currentKind_ = kind;
            currentFolder_.clear();
            SaveSettings();
        }
        if (nodeOpen)
        {
            auto rootIt = children.find("");
            if (rootIt != children.end())
            {
                for (const std::string &child : rootIt->second)
                {
                    DrawFolderNode(kind, child);
                }
            }
            ImGui::TreePop();
        }
    }
}

void EditorAssetBrowser::DrawFolderNode(Kind kind, const std::string &folder)
{
    const auto &children = folderChildren_[static_cast<int>(kind)];
    auto it = children.find(folder);
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
    if (it == children.end())
    {
        flags |= ImGuiTreeNodeFlags_Leaf;
    }
    const bool isCurrent = (location_ == Location::Folder && currentKind_ == kind && currentFolder_ == folder);
    if (isCurrent)
    {
        flags |= ImGuiTreeNodeFlags_Selected;
    }
    // 今見ているフォルダまでの枝は開いておく
    if (location_ == Location::Folder && currentKind_ == kind && currentFolder_.rfind(folder + "/", 0) == 0)
    {
        ImGui::SetNextItemOpen(true, ImGuiCond_Always);
    }
    ImGui::PushID(folder.c_str());
    const bool open = ImGui::TreeNodeEx("##folder", flags, ICON_FA_FOLDER " %s", LastSegment(folder).c_str());
    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
    {
        location_ = Location::Folder;
        currentKind_ = kind;
        currentFolder_ = folder;
    }
    if (open)
    {
        if (it != children.end())
        {
            for (const std::string &child : it->second)
            {
                DrawFolderNode(kind, child);
            }
        }
        ImGui::TreePop();
    }
    ImGui::PopID();
}

void EditorAssetBrowser::DrawContent()
{
    // ---- パンくず ----
    std::vector<std::string> subFolders;
    std::vector<const Entry *> items;
    if (location_ == Location::Folder)
    {
        ScopedButtonColors colors(DebugTheme::kButtonGhost, DebugTheme::kButtonGhostHover);
        ImGui::PushStyleColor(ImGuiCol_Text, KindColor(currentKind_));
        if (ImGui::Button(std::format("{} {}", KindIcon(currentKind_), KindLabel(currentKind_)).c_str()))
        {
            currentFolder_.clear();
        }
        ImGui::PopStyleColor();
        if (!currentFolder_.empty())
        {
            std::string accumulated;
            size_t start = 0;
            while (start <= currentFolder_.size())
            {
                const size_t slash = currentFolder_.find('/', start);
                const std::string part = currentFolder_.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
                accumulated = accumulated.empty() ? part : accumulated + "/" + part;
                ImGui::SameLine(0.0f, 2.0f);
                ImGui::TextDisabled(ICON_FA_CHEVRON_RIGHT);
                ImGui::SameLine(0.0f, 2.0f);
                ImGui::PushID(accumulated.c_str());
                if (ImGui::Button(part.c_str()))
                {
                    currentFolder_ = accumulated;
                }
                ImGui::PopID();
                if (slash == std::string::npos)
                    break;
                start = slash + 1;
            }
        }

        // 検索中は今のフォルダの下をすべて、そうでなければ直下だけ
        const std::string prefix = currentFolder_.empty() ? std::string() : currentFolder_ + "/";
        for (const Entry &entry : entries_)
        {
            if (entry.kind != currentKind_)
                continue;
            if (search_.empty())
            {
                if (entry.folder == currentFolder_)
                    items.push_back(&entry);
            }
            else if ((currentFolder_.empty() || entry.folder == currentFolder_ || entry.folder.rfind(prefix, 0) == 0) &&
                     PassSearch(entry))
            {
                items.push_back(&entry);
            }
        }
        if (search_.empty())
        {
            const auto &children = folderChildren_[static_cast<int>(currentKind_)];
            auto it = children.find(currentFolder_);
            if (it != children.end())
                subFolders = it->second;
        }
    }
    else
    {
        const bool favorites = (location_ == Location::Favorites);
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(favorites ? DebugTheme::kAccentYellow : DebugTheme::kTextReadOnly, "%s",
                           favorites ? ICON_FA_STAR " お気に入り" : ICON_FA_HISTORY " 最近使った");
        const std::vector<std::string> keys = favorites ? favorites_ : std::vector<std::string>(recent_.begin(), recent_.end());
        for (const std::string &key : keys)
        {
            if (const Entry *entry = FindByKey(key); entry && PassSearch(*entry))
                items.push_back(entry);
        }
        if (!favorites && !recent_.empty())
        {
            ImGui::SameLine();
            ScopedButtonColors colors(DebugTheme::kButtonGhost, DebugTheme::kButtonGhostHover);
            if (ImGui::SmallButton("履歴を消す"))
            {
                recent_.clear();
                SaveSettings();
            }
        }
    }

    // 種類ごとの使い方を1行で
    {
        const Kind hintKind = (location_ == Location::Folder) ? currentKind_ : Kind::Count;
        const char *hint = "ドラッグで各所へ渡せます。右クリックでお気に入り・パスのコピーなど";
        switch (hintKind)
        {
        case Kind::Image:
            hint = "テクスチャ欄へドラッグで割り当て。ダブルクリックでパスをコピー";
            break;
        case Kind::Model:
            hint = "シーンへドラッグでその場に配置。ダブルクリックでカメラの前に配置";
            break;
        case Kind::Prefab:
            hint = "シーンへドラッグで配置。作るときは階層・インスペクタの「プレハブとして保存」";
            break;
        case Kind::Sound:
            hint = "ダブルクリックで試聴（もう一度で停止）";
            break;
        case Kind::Json:
            hint = "ダブルクリックでパスをコピー。右クリックからエクスプローラーで開けます";
            break;
        default:
            break;
        }
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled(ICON_FA_INFO_CIRCLE);
        ImGui::SetItemTooltip("%s", hint);
    }
    ImGui::Separator();

    // Ctrl+ホイールでサムネの大きさを変える（その間はホイールでスクロールさせない）
    const bool zooming = ImGui::GetIO().KeyCtrl && !listView_;
    ImGui::BeginChild("##assetItems", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None,
                      zooming ? ImGuiWindowFlags_NoScrollWithMouse : ImGuiWindowFlags_None);
    if (zooming && ImGui::IsWindowHovered() && ImGui::GetIO().MouseWheel != 0.0f)
    {
        thumbSize_ = std::clamp(thumbSize_ + ImGui::GetIO().MouseWheel * 8.0f, 40.0f, 160.0f);
        SaveSettings();
    }
    if (items.empty() && subFolders.empty())
    {
        ImGui::Spacing();
        DimText(search_.empty() ? (location_ == Location::Favorites ? "お気に入りはまだありません（項目を右クリック → お気に入り）"
                                                                     : "ここには何もありません")
                                : "検索に一致するものがありません");
    }
    else if (listView_)
    {
        DrawList(subFolders, items);
    }
    else
    {
        DrawGrid(subFolders, items);
    }
    ImGui::EndChild();
}

void EditorAssetBrowser::DrawGrid(const std::vector<std::string> &folders, const std::vector<const Entry *> &items)
{
    const ImGuiStyle &style = ImGui::GetStyle();
    const float labelHeight = ImGui::GetTextLineHeight() + 4.0f;
    const ImVec2 cellSize = ImVec2(thumbSize_ + 8.0f, thumbSize_ + 8.0f + labelHeight);
    const float availWidth = ImGui::GetContentRegionAvail().x;
    const int columns = (std::max)(1, static_cast<int>((availWidth + style.ItemSpacing.x) / (cellSize.x + style.ItemSpacing.x)));
    ImDrawList *drawList = ImGui::GetWindowDrawList();
    const ImU32 selectedColor = ImGui::GetColorU32(ImGuiCol_Header);
    const ImU32 hoveredColor = ImGui::GetColorU32(ImGuiCol_HeaderHovered);

    int index = 0;
    auto nextCell = [&]() {
        if (index % columns != 0)
            ImGui::SameLine();
        ++index;
    };

    // セルの下に名前を中央ぞろえ（はみ出したら …）で描く
    auto drawLabel = [&](const ImVec2 &cellMin, const std::string &text, ImU32 color) {
        const float textWidth = ImGui::CalcTextSize(text.c_str()).x;
        const float maxWidth = cellSize.x - 6.0f;
        const float x = cellMin.x + 3.0f + (std::max)((maxWidth - textWidth) * 0.5f, 0.0f);
        const float y = cellMin.y + thumbSize_ + 8.0f;
        ImGui::PushStyleColor(ImGuiCol_Text, color);
        ImGui::RenderTextEllipsis(drawList, ImVec2(x, y), ImVec2(cellMin.x + cellSize.x - 3.0f, y + labelHeight),
                                  cellMin.x + cellSize.x - 3.0f, text.c_str(), nullptr, nullptr);
        ImGui::PopStyleColor();
    };

    // 大きいアイコン（画像以外のサムネ）
    auto drawBigIcon = [&](const ImVec2 &thumbMin, const char *icon, const ImVec4 &color) {
        ImFont *font = ImGui::GetFont();
        const float iconSize = thumbSize_ * 0.42f;
        const ImVec2 iconExtent = font->CalcTextSizeA(iconSize, FLT_MAX, 0.0f, icon);
        const ImVec2 pos = ImVec2(thumbMin.x + (thumbSize_ - iconExtent.x) * 0.5f, thumbMin.y + (thumbSize_ - iconExtent.y) * 0.5f);
        drawList->AddText(font, iconSize, pos, ImGui::ColorConvertFloat4ToU32(color), icon);
    };

    // ---- フォルダ ----
    std::string enterFolder;
    for (const std::string &folder : folders)
    {
        nextCell();
        ImGui::PushID(folder.c_str());
        const ImVec2 cellMin = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("##folderCell", cellSize);
        const bool hovered = ImGui::IsItemHovered();
        if (hovered)
        {
            drawList->AddRectFilled(cellMin, ImVec2(cellMin.x + cellSize.x, cellMin.y + cellSize.y), hoveredColor, 6.0f);
            ImGui::SetTooltip("%s\nダブルクリックで開く", folder.c_str());
        }
        if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
        {
            enterFolder = folder;
        }
        const ImVec2 thumbMin = ImVec2(cellMin.x + 4.0f, cellMin.y + 4.0f);
        drawBigIcon(thumbMin, ICON_FA_FOLDER, DebugTheme::kAccentYellow);
        drawLabel(cellMin, LastSegment(folder), ImGui::GetColorU32(ImGuiCol_Text));
        ImGui::PopID();
    }
    if (!enterFolder.empty())
    {
        currentFolder_ = enterFolder;
    }

    // ---- ファイル ----
    for (const Entry *entry : items)
    {
        nextCell();
        const std::string key = KeyOf(*entry);
        ImGui::PushID(key.c_str());
        const ImVec2 cellMin = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("##cell", cellSize);
        const bool hovered = ImGui::IsItemHovered();
        const bool visible = ImGui::IsItemVisible();
        const bool selected = (selectedKey_ == key);
        if (ImGui::IsItemClicked())
        {
            selectedKey_ = key;
        }
        if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
        {
            Activate(*entry);
        }

        uint64_t textureId = 0;
        if (visible)
        {
            textureId = GetThumbnail(*entry);
        }
        MakeDragSource(*entry, textureId);
        if (hovered)
        {
            DrawItemTooltip(*entry);
        }
        DrawItemContextMenu(*entry);

        if (visible)
        {
            const ImVec2 cellMax = ImVec2(cellMin.x + cellSize.x, cellMin.y + cellSize.y);
            if (selected || hovered)
            {
                drawList->AddRectFilled(cellMin, cellMax, selected ? selectedColor : hoveredColor, 6.0f);
            }
            const ImVec2 thumbMin = ImVec2(cellMin.x + 4.0f, cellMin.y + 4.0f);
            const ImVec2 thumbMax = ImVec2(thumbMin.x + thumbSize_, thumbMin.y + thumbSize_);
            // サムネの地（市松模様の代わりに暗い地。透過PNGの形が分かる）
            drawList->AddRectFilled(thumbMin, thumbMax, IM_COL32(28, 28, 32, 255), 4.0f);
            if (textureId != 0)
            {
                // 縦横比を保って収める
                const DirectX::TexMetadata &meta = TextureManager::GetInstance()->GetMetaData(entry->relPath);
                const float aspect = (meta.height > 0) ? static_cast<float>(meta.width) / static_cast<float>(meta.height) : 1.0f;
                ImVec2 size = aspect >= 1.0f ? ImVec2(thumbSize_, thumbSize_ / aspect) : ImVec2(thumbSize_ * aspect, thumbSize_);
                const ImVec2 imageMin = ImVec2(thumbMin.x + (thumbSize_ - size.x) * 0.5f, thumbMin.y + (thumbSize_ - size.y) * 0.5f);
                drawList->AddImage(static_cast<ImTextureID>(textureId), imageMin, ImVec2(imageMin.x + size.x, imageMin.y + size.y));
            }
            else
            {
                drawBigIcon(thumbMin, KindIcon(entry->kind), KindColor(entry->kind));
            }
            // 種類の色帯（サムネの下端）
            drawList->AddRectFilled(ImVec2(thumbMin.x, thumbMax.y - 3.0f), thumbMax, ImGui::ColorConvertFloat4ToU32(KindColor(entry->kind)),
                                    2.0f, ImDrawFlags_RoundCornersBottom);
            // お気に入りの星
            if (IsFavorite(*entry))
            {
                drawList->AddText(ImVec2(thumbMax.x - ImGui::GetFontSize() - 2.0f, thumbMin.y + 2.0f),
                                  ImGui::ColorConvertFloat4ToU32(DebugTheme::kAccentYellow), ICON_FA_STAR);
            }
            // 試聴中の印
            if (entry->kind == Kind::Sound && previewSoundKey_ == key && Audio::GetInstance()->IsPlaying(previewSound_))
            {
                drawList->AddText(ImVec2(thumbMin.x + 4.0f, thumbMin.y + 2.0f), ImGui::ColorConvertFloat4ToU32(DebugTheme::kAccentGreen),
                                  ICON_FA_VOLUME_UP);
            }
            drawLabel(cellMin, entry->name, ImGui::GetColorU32(ImGuiCol_Text));
        }
        ImGui::PopID();
    }
}

void EditorAssetBrowser::DrawList(const std::vector<std::string> &folders, const std::vector<const Entry *> &items)
{
    const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable |
                                  ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp;
    if (!ImGui::BeginTable("##assetList", 3, flags))
    {
        return;
    }
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("名前", ImGuiTableColumnFlags_WidthStretch, 3.0f);
    ImGui::TableSetupColumn("場所", ImGuiTableColumnFlags_WidthStretch, 2.0f);
    ImGui::TableSetupColumn("サイズ", ImGuiTableColumnFlags_WidthFixed, 70.0f);
    ImGui::TableHeadersRow();

    std::string enterFolder;
    for (const std::string &folder : folders)
    {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::PushID(folder.c_str());
        ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kAccentYellow);
        ImGui::TextUnformatted(ICON_FA_FOLDER);
        ImGui::PopStyleColor();
        ImGui::SameLine();
        if (ImGui::Selectable(LastSegment(folder).c_str(), false,
                              ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick) &&
            ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
        {
            enterFolder = folder;
        }
        ImGui::PopID();
        ImGui::TableNextColumn();
        ImGui::TextDisabled("フォルダ");
        ImGui::TableNextColumn();
    }
    if (!enterFolder.empty())
    {
        currentFolder_ = enterFolder;
    }

    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(items.size()));
    while (clipper.Step())
    {
        for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
        {
            const Entry &entry = *items[row];
            const std::string key = KeyOf(entry);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::PushID(key.c_str());
            ImGui::PushStyleColor(ImGuiCol_Text, KindColor(entry.kind));
            ImGui::TextUnformatted(KindIcon(entry.kind));
            ImGui::PopStyleColor();
            ImGui::SameLine();
            if (IsFavorite(entry))
            {
                ImGui::TextColored(DebugTheme::kAccentYellow, ICON_FA_STAR);
                ImGui::SameLine();
            }
            if (ImGui::Selectable(entry.name.c_str(), selectedKey_ == key,
                                  ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick))
            {
                selectedKey_ = key;
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                {
                    Activate(entry);
                }
            }
            MakeDragSource(entry, 0);
            if (ImGui::IsItemHovered())
            {
                DrawItemTooltip(entry);
            }
            DrawItemContextMenu(entry);
            ImGui::PopID();
            ImGui::TableNextColumn();
            ImGui::TextDisabled("%s", entry.folder.empty() ? "/" : entry.folder.c_str());
            ImGui::TableNextColumn();
            ImGui::TextDisabled("%s", FormatSize(entry.fileSize).c_str());
        }
    }
    ImGui::EndTable();
}

void EditorAssetBrowser::DrawItemTooltip(const Entry &entry)
{
    if (!ImGui::BeginTooltip())
    {
        return;
    }
    ImGui::TextColored(KindColor(entry.kind), "%s %s", KindIcon(entry.kind), entry.name.c_str());
    ImGui::TextDisabled("%s", entry.fullPath.c_str());
    ImGui::Separator();
    ImGui::Text("%s  ・  %s", KindLabel(entry.kind), FormatSize(entry.fileSize).c_str());
    if (entry.kind == Kind::Image)
    {
        const DirectX::TexMetadata &meta = TextureManager::GetInstance()->GetMetaData(entry.relPath);
        if (meta.width > 0)
        {
            ImGui::Text("%zu × %zu px", meta.width, meta.height);
        }
    }
    else if (entry.kind == Kind::Prefab)
    {
        const BaseObjectManager::PrefabInfo info = BaseObjectManager::GetInstance()->PeekPrefab(entry.relPath);
        ImGui::Text("オブジェクト %d 個", info.objectCount);
        if (!info.rootModel.empty())
        {
            ImGui::TextDisabled("根のモデル: %s", info.rootModel.c_str());
        }
    }
    ImGui::EndTooltip();
}

void EditorAssetBrowser::DrawItemContextMenu(const Entry &entry)
{
    if (!ImGui::BeginPopupContextItem("##assetContext"))
    {
        return;
    }
    ImGui::TextColored(KindColor(entry.kind), "%s %s", KindIcon(entry.kind), entry.name.c_str());
    ImGui::Separator();

    switch (entry.kind)
    {
    case Kind::Model:
        if (ImGui::MenuItem(ICON_FA_PLUS " カメラの前に配置", "ダブルクリック"))
            Activate(entry);
        break;
    case Kind::Prefab:
        if (ImGui::MenuItem(ICON_FA_PLUS " カメラの前に配置", "ダブルクリック"))
            Activate(entry);
        break;
    case Kind::Sound:
        if (ImGui::MenuItem(ICON_FA_PLAY " 試聴 / 停止", "ダブルクリック"))
            Activate(entry);
        break;
    default:
        break;
    }

    const bool favorite = IsFavorite(entry);
    if (ImGui::MenuItem(favorite ? ICON_FA_STAR " お気に入りから外す" : ICON_FA_STAR " お気に入りに追加"))
    {
        ToggleFavorite(entry);
    }
    if (ImGui::MenuItem(ICON_FA_COPY " パスをコピー"))
    {
        ImGui::SetClipboardText(entry.relPath.c_str());
    }
    if (ImGui::MenuItem(ICON_FA_FOLDER_OPEN " エクスプローラーで表示"))
    {
        RevealInExplorer(entry.fullPath);
    }
    if (entry.kind == Kind::Prefab)
    {
        ImGui::Separator();
        ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kAccentRed);
        if (ImGui::MenuItem(ICON_FA_TRASH_ALT " プレハブを削除..."))
        {
            pendingDeletePrefab_ = entry.relPath;
            openDeleteModal_ = true;
        }
        ImGui::PopStyleColor();
    }
    ImGui::EndPopup();
}

void EditorAssetBrowser::DrawDeletePrefabModal()
{
    const char *kPopupName = ICON_FA_TRASH_ALT " プレハブの削除";
    if (openDeleteModal_)
    {
        ImGui::OpenPopup(kPopupName);
        openDeleteModal_ = false;
    }
    const ImGuiViewport *mainViewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowViewport(mainViewport->ID);
    ImGui::SetNextWindowPos(mainViewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal(kPopupName, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::Text("プレハブ「%s」を削除します。元に戻せません。", pendingDeletePrefab_.c_str());
        ImGui::TextDisabled("シーンに置いてある物は消えません");
        ImGui::Spacing();
        if (DangerButton("削除する", ImVec2(120.0f, 0.0f)))
        {
            if (BaseObjectManager::GetInstance()->DeletePrefab(pendingDeletePrefab_))
            {
                ImGuiNotification::Post("プレハブを削除しました: " + pendingDeletePrefab_, {0.82f, 0.58f, 0.36f, 1.0f});
            }
            const std::string removedKey = std::to_string(static_cast<int>(Kind::Prefab)) + ":" + pendingDeletePrefab_;
            favorites_.erase(std::remove(favorites_.begin(), favorites_.end(), removedKey), favorites_.end());
            recent_.erase(std::remove(recent_.begin(), recent_.end(), removedKey), recent_.end());
            SaveSettings();
            pendingDeletePrefab_.clear();
            rescanRequested_ = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (NeutralButton("キャンセル", ImVec2(120.0f, 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Escape))
        {
            pendingDeletePrefab_.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

} // namespace Hagine
#endif // USE_IMGUI
