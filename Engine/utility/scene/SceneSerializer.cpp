#include "SceneSerializer.h"
#include "SceneManager.h"
#include <SpriteManager.h>
#include <2d/ui/UIAnimator.h>
#include <asset/AssetPath.h>
#include <attachment/AttachmentManager.h>
#include <chrono>
#include <ctime>
#include <debug/log/Logger.h>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <light/LightGroup.h>
#include <nlohmann/json.hpp>
#include <object/base/BaseObjectManager.h>
#include <utility/debug/imgui/ImGuiNotification.h>
#ifdef USE_IMGUI
#include <edit/undo/UndoRedoManager.h>
#include <icon/IconsFontAwesome5.h>
#include <imgui_stdlib.h>
#include <utility/debug/imgui/DebugUIHelper.h>
#endif // USE_IMGUI

namespace Hagine {
namespace {
namespace fs = std::filesystem;
using json = nlohmann::json;

/// <summary>ファイルの先頭に書く目印。別の用途の JSON を間違えて読んだときに気づけるようにする</summary>
constexpr const char *kFormatName = "HagineScene";

/// <summary>jsons ルートからのシーンファイル置き場</summary>
constexpr const char *kSceneFolder = "Scenes";

const Vector4 kSuccessColor = {0.45f, 0.68f, 0.52f, 1.0f};
const Vector4 kErrorColor = {0.90f, 0.40f, 0.36f, 1.0f};

/// <summary>UTF-8 の文字列からパスを作る（シーン名に日本語が入っても壊れないように）</summary>
fs::path ToPath(const std::string &utf8)
{
    return fs::path(std::u8string(utf8.begin(), utf8.end()));
}

/// <summary>パスを UTF-8 の文字列にする</summary>
std::string ToUtf8(const fs::path &path)
{
    const std::u8string text = path.u8string();
    return std::string(text.begin(), text.end());
}

/// <summary>ファイル名に使えない文字を _ に置き換え、前後の空白を落とす</summary>
std::string SanitizeSceneName(const std::string &name)
{
    std::string result;
    result.reserve(name.size());
    for (const char c : name)
    {
        const bool forbidden = c == '\\' || c == '/' || c == ':' || c == '*' || c == '?' || c == '"' ||
                               c == '<' || c == '>' || c == '|' || (static_cast<unsigned char>(c) < 0x20);
        result.push_back(forbidden ? '_' : c);
    }
    const size_t first = result.find_first_not_of(' ');
    if (first == std::string::npos)
    {
        return std::string();
    }
    const size_t last = result.find_last_not_of(' ');
    return result.substr(first, last - first + 1);
}

/// <summary>
/// 一時ファイルへ書いてから置き換える。書いている途中で落ちても元のファイルは残る。
/// 置き換える前の内容は .bak に1世代だけ残す
/// </summary>
/// <param name="path">書き込み先</param>
/// <param name="text">中身</param>
/// <param name="outError">失敗したときの理由</param>
/// <returns>bool: 書けたら true</returns>
bool WriteFileAtomically(const fs::path &path, const std::string &text, std::string &outError)
{
    std::error_code error;
    fs::create_directories(path.parent_path(), error);

    fs::path tempPath = path;
    tempPath += ".tmp";
    {
        std::ofstream out(tempPath, std::ios::binary | std::ios::trunc);
        if (!out.is_open())
        {
            outError = "一時ファイルを開けませんでした";
            return false;
        }
        out.write(text.data(), static_cast<std::streamsize>(text.size()));
        out.close();
        if (out.fail())
        {
            outError = "一時ファイルへ書き込めませんでした";
            fs::remove(tempPath, error);
            return false;
        }
    }

    if (fs::exists(path, error))
    {
        // 残せなくても保存自体は続ける（バックアップは保険でしかない）
        fs::path backupPath = path;
        backupPath += ".bak";
        fs::copy_file(path, backupPath, fs::copy_options::overwrite_existing, error);
    }

    fs::rename(tempPath, path, error);
    if (error)
    {
        outError = "ファイルを置き換えられませんでした（他のアプリで開いていませんか？ code " +
                   std::to_string(error.value()) + "）";
        std::error_code ignored;
        fs::remove(tempPath, ignored);
        return false;
    }
    return true;
}

/// <summary>JSON ファイルを読む。読めない・壊れている場合は理由を入れて nullopt</summary>
std::optional<json> ReadJsonFile(const fs::path &path, std::string &outError)
{
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open())
    {
        outError = "ファイルを開けませんでした";
        return std::nullopt;
    }
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

    json document = json::parse(text, nullptr, /*allow_exceptions=*/false, /*ignore_comments=*/true);
    if (document.is_discarded() || !document.is_object())
    {
        outError = "JSON として読めませんでした";
        return std::nullopt;
    }
    if (!document.contains("objects") || !document["objects"].is_array())
    {
        outError = "\"objects\" がありません（シーンファイルではないようです）";
        return std::nullopt;
    }
    return document;
}
} // namespace

SceneSerializer *SceneSerializer::GetInstance()
{
    static SceneSerializer instance;
    return &instance;
}

std::string SceneSerializer::FilePath(const std::string &sceneName)
{
    return AssetPath::Json(std::string(kSceneFolder) + "/" + SanitizeSceneName(sceneName) + ".json");
}

std::string SceneSerializer::CurrentSceneName()
{
    return SceneManager::GetInstance()->GetCurrentSceneName();
}

bool SceneSerializer::Exists(const std::string &sceneName) const
{
    if (SanitizeSceneName(sceneName).empty())
    {
        return false;
    }
    std::error_code error;
    return fs::is_regular_file(ToPath(FilePath(sceneName)), error);
}

std::vector<std::string> SceneSerializer::ListScenes() const
{
    std::vector<std::string> names;
    std::error_code error;
    const fs::path folder = ToPath(AssetPath::Json(kSceneFolder));
    if (!fs::is_directory(folder, error))
    {
        return names;
    }
    for (const fs::directory_entry &entry : fs::directory_iterator(folder, error))
    {
        // .json だけを拾う（.json.tmp / .json.bak は拡張子が違うので入らない）
        if (entry.is_regular_file() && entry.path().extension() == ".json")
        {
            names.push_back(ToUtf8(entry.path().stem()));
        }
    }
    std::sort(names.begin(), names.end());
    return names;
}

bool SceneSerializer::Save(const std::string &sceneName)
{
    const std::string name = SanitizeSceneName(sceneName);
    if (name.empty())
    {
        Logger::Error("[Scene] シーン名が空なので保存できません");
        ImGuiNotification::Post("シーン名が空なので保存できません", kErrorColor);
        return false;
    }

    json document = json::object();
    document["format"] = kFormatName;
    document["version"] = kVersion;
    document["scene"] = name;
    document["objects"] = BaseObjectManager::GetInstance()->SerializeSceneObjects();
    document["lights"] = LightGroup::GetInstance()->ToJson();
    document["attachments"] = AttachmentManager::GetInstance()->ToJson();
    const size_t objectCount = document["objects"].size();

    std::string text;
    try
    {
        text = document.dump(4);
    }
    catch (const json::exception &e)
    {
        // 名前などに壊れた UTF-8 が混ざっていると dump が投げる
        Logger::Error(std::string("[Scene] JSON にできませんでした: ") + e.what());
        ImGuiNotification::Post("シーンを保存できませんでした（ログを確認してください）", kErrorColor);
        return false;
    }

    const std::string filePath = FilePath(name);
    std::string error;
    if (!WriteFileAtomically(ToPath(filePath), text, error))
    {
        Logger::Error("[Scene] 保存に失敗しました: " + filePath + " " + error);
        ImGuiNotification::Post("シーンを保存できませんでした: " + error, kErrorColor);
        return false;
    }

    Logger::Info("[Scene] 保存しました: " + filePath + " (オブジェクト " + std::to_string(objectCount) + " 個)");
    ImGuiNotification::Post(std::format("シーン「{}」を保存しました（オブジェクト {} 個）", name, objectCount), kSuccessColor);
    return true;
}

bool SceneSerializer::Load(const std::string &sceneName)
{
    return LoadInternal(sceneName, true);
}

bool SceneSerializer::LoadIfExists(const std::string &sceneName)
{
    if (!Exists(sceneName))
    {
        return false;
    }
    return LoadInternal(sceneName, false);
}

bool SceneSerializer::LoadInternal(const std::string &sceneName, bool notify)
{
    const std::string name = SanitizeSceneName(sceneName);
    const std::string filePath = FilePath(name);
    if (name.empty() || !Exists(name))
    {
        Logger::Warn("[Scene] シーンファイルがありません: " + filePath);
        if (notify)
        {
            ImGuiNotification::Post("シーンファイルがありません: " + name, kErrorColor);
        }
        return false;
    }

    // 先に全部読んで確かめてから置き換える。壊れたファイルで今の配置を消さないため
    std::string error;
    const std::optional<json> document = ReadJsonFile(ToPath(filePath), error);
    if (!document)
    {
        Logger::Error("[Scene] 読み込みに失敗しました: " + filePath + " " + error);
        if (notify)
        {
            ImGuiNotification::Post("シーンを読み込めませんでした: " + error, kErrorColor);
        }
        return false;
    }

    const int version = document->value("version", 0);
    if (version > kVersion)
    {
        Logger::Warn("[Scene] " + filePath + " は新しい形式 (version " + std::to_string(version) +
                     ") です。読めない項目は飛ばします");
    }

    int createdCount = 0;
    {
        // 1体ごとの「追加しました」は人が押した操作ではないので止める（最後にまとめて1回知らせる）
        ImGuiNotification::ScopedMute mute;
        createdCount = BaseObjectManager::GetInstance()->DeserializeSceneObjects((*document)["objects"]);
        if (document->contains("lights"))
        {
            // 光源はシーンをまたいで残るシングルトンなので、書いてあるときだけ置き換える
            LightGroup::GetInstance()->FromJson((*document)["lights"]);
        }
        if (document->contains("attachments"))
        {
            // リンクは名前で解決されるので、相手（光源・パーティクル）がこの後で作られても構わない
            AttachmentManager::GetInstance()->FromJson((*document)["attachments"]);
        }
    }

    Logger::Info("[Scene] 読み込みました: " + filePath + " (オブジェクト " + std::to_string(createdCount) + " 個)");
    if (notify)
    {
        ImGuiNotification::Post(std::format("シーン「{}」を読み込みました（オブジェクト {} 個）", name, createdCount),
                                kSuccessColor);
    }
    return true;
}

void SceneSerializer::SaveCurrentScene()
{
    if (!Save(CurrentSceneName()))
    {
        return;
    }
    // スプライトと UI アニメーションはシーンごとではなく全シーン共通の保存先を持っている
    if (saveSprites_)
    {
        SpriteManager::GetInstance()->SaveAllSprites();
    }
    if (saveUIAnimation_)
    {
        UIAnimator::GetInstance()->Save();
    }
}

#ifdef USE_IMGUI
namespace {
constexpr const char *kSavePopup = "シーン保存##SceneSerializer";
constexpr const char *kLoadPopup = "シーン読み込み##SceneSerializer";

/// <summary>ファイルの更新日時を「10/02 12:34」の形にする</summary>
std::string FormatModifiedTime(const fs::path &path)
{
    std::error_code error;
    const fs::file_time_type fileTime = fs::last_write_time(path, error);
    if (error)
    {
        return std::string();
    }
    const auto systemTime = std::chrono::clock_cast<std::chrono::system_clock>(fileTime);
    const std::time_t time = std::chrono::system_clock::to_time_t(systemTime);
    std::tm local{};
    if (localtime_s(&local, &time) != 0)
    {
        return std::string();
    }
    char buffer[32]{};
    std::strftime(buffer, sizeof(buffer), "%m/%d %H:%M", &local);
    return buffer;
}
} // namespace

void SceneSerializer::OpenSaveDialog()
{
    requestOpenSave_ = true;
}

void SceneSerializer::OpenLoadDialog()
{
    requestOpenLoad_ = true;
}

void SceneSerializer::DrawImGui()
{
    if (requestOpenSave_)
    {
        requestOpenSave_ = false;
        // 上書き保存が大半なので、今のシーン名を初期値にする
        saveNameBuffer_ = CurrentSceneName();
        ImGui::OpenPopup(kSavePopup);
    }
    if (requestOpenLoad_)
    {
        requestOpenLoad_ = false;
        RefreshLoadList();
        ImGui::OpenPopup(kLoadPopup);
    }

    DrawSaveDialog();
    DrawLoadDialog();
}

void SceneSerializer::DrawSaveDialog()
{
    ImGui::SetNextWindowSizeConstraints(ImVec2(460.0f, 0.0f), ImVec2(FLT_MAX, FLT_MAX));
    if (!ImGui::BeginPopupModal(kSavePopup, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        return;
    }

    const std::string currentScene = CurrentSceneName();
    const std::string targetName = SanitizeSceneName(saveNameBuffer_);

    ImGui::SetNextItemWidth(-1);
    if (ImGui::IsWindowAppearing())
    {
        ImGui::SetKeyboardFocusHere();
    }
    ImGui::InputTextWithHint("##sceneName", "シーン名", &saveNameBuffer_);

    // 保存先と、上書きかどうか
    ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
    ImGui::TextWrapped("保存先: %s", targetName.empty() ? "（シーン名を入れてください）" : FilePath(targetName).c_str());
    ImGui::PopStyleColor();
    if (!targetName.empty() && Exists(targetName))
    {
        ImGui::TextColored(DebugTheme::kAccentYellow, ICON_FA_EXCLAMATION_TRIANGLE " 既にあるファイルを上書きします（前の内容は .bak に残ります）");
    }
    if (!targetName.empty() && targetName != currentScene)
    {
        ImGui::TextColored(DebugTheme::kAccentYellow,
                           ICON_FA_INFO_CIRCLE " 今のシーン「%s」とは別のファイルです。起動時に自動で読まれるのは「%s」の方です",
                           currentScene.c_str(), currentScene.c_str());
    }

    ImGui::Separator();
    DrawSaveTargetList();
    {
        const LightGroup *lights = LightGroup::GetInstance();
        ImGui::TextDisabled(ICON_FA_LIGHTBULB " 光源も保存します（平行光源・点光源 %d 個・スポットライト %d 個）",
                            static_cast<int>(lights->GetPointLights().GetEntries().size()),
                            static_cast<int>(lights->GetSpotLights().GetEntries().size()));
    }

    ImGui::Separator();
    ImGui::TextDisabled("一緒に保存する（どちらも全シーン共通の保存先）");
    ImGui::Checkbox("スプライト", &saveSprites_);
    ImGui::SameLine();
    ImGui::Checkbox("UIアニメーション", &saveUIAnimation_);
    ImGui::Separator();

    const bool canSave = !targetName.empty();
    // 開いた最初のフレームの Enter は無視する（コマンドパレットで Enter を押して開いたとき、
    // その同じ Enter で確認する間もなく保存されてしまうため）
    const bool enterPressed = !ImGui::IsWindowAppearing() &&
                              (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter));
    const bool savePressed = canSave ? ConfirmButton(ICON_FA_SAVE " 保存", ImVec2(140, 0))
                                     : NeutralButton(ICON_FA_SAVE " 保存", ImVec2(140, 0));
    if (canSave && (savePressed || enterPressed))
    {
        if (Save(targetName))
        {
            if (saveSprites_)
            {
                SpriteManager::GetInstance()->SaveAllSprites();
            }
            if (saveUIAnimation_)
            {
                UIAnimator::GetInstance()->Save();
            }
            ImGui::CloseCurrentPopup();
        }
    }
    ImGui::SameLine();
    if (NeutralButton("キャンセル", ImVec2(120, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape))
    {
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void SceneSerializer::DrawSaveTargetList()
{
    BaseObjectManager *manager = BaseObjectManager::GetInstance();

    // 所有オブジェクトを階層どおりに並べる。ゲーム側の物は数だけ出す
    std::vector<BaseObject *> roots;
    std::vector<std::string> gameObjects;
    int saveCount = 0;
    int skipCount = 0;
    for (const std::string &name : manager->GetSortedObjectNames())
    {
        BaseObject *obj = manager->GetObjectByName(name);
        if (!obj)
        {
            continue;
        }
        if (!manager->IsOwned(obj))
        {
            gameObjects.push_back(name);
            continue;
        }
        if (manager->IsSceneSaveTarget(obj))
        {
            ++saveCount;
        }
        else
        {
            ++skipCount;
        }
        if (!manager->IsOwned(obj->GetParent()))
        {
            roots.push_back(obj);
        }
    }

    ImGui::AlignTextToFramePadding();
    ImGui::Text("保存するオブジェクト: %d 個", saveCount);
    if (skipCount > 0)
    {
        ImGui::SameLine();
        ImGui::TextColored(DebugTheme::kAccentOrange, "（保存しない: %d 個）", skipCount);
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("全部保存する"))
    {
        for (const std::string &name : manager->GetObjectNames())
        {
            BaseObject *obj = manager->GetObjectByName(name);
            if (manager->IsOwned(obj))
            {
                obj->SetShouldSave(true);
            }
        }
    }

    // 一覧（チェックを外した物は子ごと保存されない）
    ImGui::BeginChild("##sceneSaveTargets", ImVec2(0.0f, 220.0f), ImGuiChildFlags_Borders);
    if (roots.empty())
    {
        ImGui::TextDisabled("エディタで置いたオブジェクトがありません");
    }
    std::function<void(BaseObject *, bool)> drawNode = [&](BaseObject *obj, bool parentSaved) {
        ImGui::PushID(obj);
        bool shouldSave = obj->GetShouldSave();
        // 親を保存しないなら子は書かれない。チェックは触れるが薄く出して分かるようにする
        if (!parentSaved)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        }
        if (ImGui::Checkbox(obj->GetName().c_str(), &shouldSave))
        {
            obj->SetShouldSave(shouldSave);
        }
        if (!parentSaved)
        {
            ImGui::PopStyleColor();
            ImGui::SetItemTooltip("親を保存しないので、この物も保存されません");
        }

        std::vector<BaseObject *> children;
        for (BaseObject *child : *obj->GetChildren())
        {
            if (manager->IsOwned(child))
            {
                children.push_back(child);
            }
        }
        std::sort(children.begin(), children.end(),
                  [](BaseObject *a, BaseObject *b) { return a->GetName() < b->GetName(); });
        if (!children.empty())
        {
            ImGui::Indent();
            for (BaseObject *child : children)
            {
                drawNode(child, parentSaved && obj->GetShouldSave());
            }
            ImGui::Unindent();
        }
        ImGui::PopID();
    };
    for (BaseObject *root : roots)
    {
        drawNode(root, true);
    }
    ImGui::EndChild();

    if (!gameObjects.empty())
    {
        ImGui::TextDisabled(ICON_FA_GAMEPAD " ゲーム側のオブジェクト %d 個は保存しません", static_cast<int>(gameObjects.size()));
        if (ImGui::BeginItemTooltip())
        {
            ImGui::TextUnformatted("コードで作られる物なので、毎回コードが作り直します:");
            for (const std::string &name : gameObjects)
            {
                ImGui::BulletText("%s", name.c_str());
            }
            ImGui::EndTooltip();
        }
    }
}

void SceneSerializer::RefreshLoadList()
{
    loadList_.clear();
    loadSelected_ = -1;
    const std::string currentScene = CurrentSceneName();
    for (const std::string &name : ListScenes())
    {
        SceneFileInfo info;
        info.sceneName = name;
        const fs::path path = ToPath(FilePath(name));
        info.modified = FormatModifiedTime(path);
        std::string error;
        if (const std::optional<json> document = ReadJsonFile(path, error))
        {
            info.objectCount = static_cast<int>((*document)["objects"].size());
        }
        // 今のシーンのファイルを最初から選んでおく（読み直しが一番多い）
        if (name == currentScene)
        {
            loadSelected_ = static_cast<int>(loadList_.size());
        }
        loadList_.push_back(std::move(info));
    }
}

void SceneSerializer::DrawLoadDialog()
{
    ImGui::SetNextWindowSizeConstraints(ImVec2(460.0f, 0.0f), ImVec2(FLT_MAX, FLT_MAX));
    if (!ImGui::BeginPopupModal(kLoadPopup, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        return;
    }

    const std::string currentScene = CurrentSceneName();
    bool doubleClicked = false;

    ImGui::BeginChild("##sceneFiles", ImVec2(0.0f, 220.0f), ImGuiChildFlags_Borders);
    if (loadList_.empty())
    {
        ImGui::TextDisabled("保存済みのシーンがありません（%s）", AssetPath::Json(kSceneFolder).c_str());
    }
    if (ImGui::BeginTable("##sceneFileTable", 3, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg))
    {
        ImGui::TableSetupColumn("名前", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("数", ImGuiTableColumnFlags_WidthFixed, 70.0f);
        ImGui::TableSetupColumn("更新", ImGuiTableColumnFlags_WidthFixed, 90.0f);
        for (int i = 0; i < static_cast<int>(loadList_.size()); ++i)
        {
            const SceneFileInfo &info = loadList_[i];
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            const std::string label = info.sceneName + (info.sceneName == currentScene ? "  (今のシーン)" : "");
            if (ImGui::Selectable((label + "##" + std::to_string(i)).c_str(), loadSelected_ == i,
                                  ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick))
            {
                loadSelected_ = i;
            }
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            {
                doubleClicked = true; // 行のダブルクリックでそのまま読み込む
            }
            ImGui::TableNextColumn();
            if (info.objectCount >= 0)
            {
                ImGui::Text("%d 個", info.objectCount);
            }
            else
            {
                ImGui::TextColored(DebugTheme::kAccentRed, "読めない");
            }
            ImGui::TableNextColumn();
            ImGui::TextDisabled("%s", info.modified.c_str());
        }
        ImGui::EndTable();
    }
    ImGui::EndChild();

    ImGui::TextDisabled("一緒に読み込む（どちらも全シーン共通の保存先）");
    ImGui::Checkbox("スプライト", &loadSprites_);
    ImGui::SameLine();
    ImGui::Checkbox("UIアニメーション", &loadUIAnimation_);

    ImGui::TextColored(DebugTheme::kAccentYellow,
                       ICON_FA_EXCLAMATION_TRIANGLE " エディタで置いたオブジェクトは、保存していない変更も含めて置き換わります");
    ImGui::Separator();

    const bool canLoad = loadSelected_ >= 0 && loadSelected_ < static_cast<int>(loadList_.size()) &&
                         loadList_[loadSelected_].objectCount >= 0;
    const bool loadPressed = canLoad ? ConfirmButton(ICON_FA_FOLDER_OPEN " 読み込み", ImVec2(140, 0))
                                     : NeutralButton(ICON_FA_FOLDER_OPEN " 読み込み", ImVec2(140, 0));
    if (canLoad && (loadPressed || doubleClicked))
    {
        if (Load(loadList_[loadSelected_].sceneName))
        {
            if (loadSprites_)
            {
                SpriteManager::GetInstance()->LoadAllSprites();
            }
            if (loadUIAnimation_)
            {
                UIAnimator::GetInstance()->Load();
            }
            // 読み込む前の配置を指した Undo 履歴は、置き換えた後では意味を持たない
            UndoRedoManager::GetInstance()->Clear();
            ImGui::CloseCurrentPopup();
        }
    }
    ImGui::SameLine();
    if (NeutralButton("キャンセル", ImVec2(120, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape))
    {
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}
#endif // USE_IMGUI
} // namespace Hagine
