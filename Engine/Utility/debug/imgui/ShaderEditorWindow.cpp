#include "ShaderEditorWindow.h"
#ifdef USE_IMGUI
#include "DebugUIHelper.h"
#include "ImGuiNotification.h"
#include <TextEditor.h>
#include <algorithm>
#include <asset/AssetPath.h>
#include <debug/log/Logger.h>
#include <filesystem>
#include <fstream>
#include <imgui.h>
#include <sstream>

namespace Hagine {
namespace {

// エディタ本体はここに1つだけ持つ。TextEditor は状態（カーソル・Undo履歴）を
// 内部に抱えるので、窓と同じ寿命で使い回す。
TextEditor &Editor()
{
    static TextEditor editor;
    static bool configured = false;
    if (!configured)
    {
        configured = true;
        editor.SetLanguageDefinition(TextEditor::LanguageDefinition::HLSL());
        editor.SetTabSize(4);
        editor.SetShowWhitespaces(false);

        // 既定パレットは明るめなので、near-black のテーマに合わせて地を落とす。
        // 構文色はそのまま活かし、背景と行番号まわりだけ差し替える。
        TextEditor::Palette palette = TextEditor::GetDarkPalette();
        palette[static_cast<size_t>(TextEditor::PaletteIndex::Background)] = IM_COL32(7, 7, 9, 255);
        palette[static_cast<size_t>(TextEditor::PaletteIndex::LineNumber)] = IM_COL32(96, 100, 112, 255);
        palette[static_cast<size_t>(TextEditor::PaletteIndex::CurrentLineFill)] = IM_COL32(24, 26, 32, 255);
        palette[static_cast<size_t>(TextEditor::PaletteIndex::CurrentLineFillInactive)] = IM_COL32(18, 19, 23, 255);
        palette[static_cast<size_t>(TextEditor::PaletteIndex::CurrentLineEdge)] = IM_COL32(40, 43, 52, 255);
        palette[static_cast<size_t>(TextEditor::PaletteIndex::Selection)] = IM_COL32(60, 80, 105, 180);
        editor.SetPalette(palette);
    }
    return editor;
}

/// <summary>パスの区切りを '/' に揃える（表示とキーの一貫性のため）</summary>
std::string NormalizeSeparators(std::string path)
{
    std::replace(path.begin(), path.end(), '\\', '/');
    return path;
}

/// <summary>大文字小文字を無視した部分一致</summary>
bool ContainsNoCase(const std::string &haystack, const std::string &needle)
{
    if (needle.empty())
    {
        return true;
    }
    auto it = std::search(haystack.begin(), haystack.end(), needle.begin(), needle.end(),
                          [](char a, char b) { return std::tolower(static_cast<unsigned char>(a)) ==
                                                      std::tolower(static_cast<unsigned char>(b)); });
    return it != haystack.end();
}

} // namespace

ShaderEditorWindow *ShaderEditorWindow::GetInstance()
{
    static ShaderEditorWindow instance;
    return &instance;
}

void ShaderEditorWindow::RefreshFileList()
{
    files_.clear();

    const std::string root = AssetPath::Shader("");
    std::error_code ec;
    if (!std::filesystem::exists(root, ec))
    {
        Logger::Error("シェーダーフォルダが見つかりません: " + root);
        return;
    }

    for (const auto &entry : std::filesystem::recursive_directory_iterator(root, ec))
    {
        if (ec)
        {
            break;
        }
        if (!entry.is_regular_file() || entry.path().extension() != ".hlsl")
        {
            continue;
        }
        // root からの相対パスで持つ（一覧の表示にも保存先の組み立てにも使う）
        const std::filesystem::path rel = std::filesystem::relative(entry.path(), root, ec);
        if (ec)
        {
            continue;
        }
        files_.push_back(NormalizeSeparators(rel.string()));
    }
    std::sort(files_.begin(), files_.end());
}

void ShaderEditorWindow::LoadFile(const std::string &relativePath)
{
    const std::string full = AssetPath::Shader(relativePath);
    std::ifstream ifs(full, std::ios::binary);
    if (!ifs)
    {
        statusMessage_ = "開けませんでした: " + relativePath;
        Logger::Error("シェーダーを開けませんでした: " + full);
        return;
    }

    std::ostringstream oss;
    oss << ifs.rdbuf();
    Editor().SetText(oss.str());

    currentFile_ = relativePath;
    dirty_ = false;
    statusMessage_.clear();
}

bool ShaderEditorWindow::SaveCurrentFile()
{
    if (currentFile_.empty())
    {
        return false;
    }

    const std::string full = AssetPath::Shader(currentFile_);
    std::ofstream ofs(full, std::ios::binary);
    if (!ofs)
    {
        statusMessage_ = "保存できませんでした: " + currentFile_;
        Logger::Error("シェーダーを保存できませんでした: " + full);
        return false;
    }

    ofs << Editor().GetText();
    ofs.close();

    dirty_ = false;
    statusMessage_ = "保存しました（反映は次回起動から）";
    ImGuiNotification::Post("シェーダーを保存しました: " + currentFile_, {0.45f, 0.68f, 0.52f, 1.0f});
    return true;
}

void ShaderEditorWindow::DrawFileList()
{
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##shaderfilter", "絞り込み", &filter_);

    if (ImGui::BeginChild("##shaderfiles", ImVec2(0, 0), ImGuiChildFlags_Borders))
    {
        std::string currentFolder;
        for (const std::string &file : files_)
        {
            if (!ContainsNoCase(file, filter_))
            {
                continue;
            }

            // フォルダが変わったら見出しを挟む（Deferred / Particle などの塊で読める）
            const size_t slash = file.find_last_of('/');
            const std::string folder = (slash == std::string::npos) ? std::string("(直下)") : file.substr(0, slash);
            if (folder != currentFolder)
            {
                currentFolder = folder;
                ImGui::SeparatorText(currentFolder.c_str());
            }

            const std::string leaf = (slash == std::string::npos) ? file : file.substr(slash + 1);
            const bool selected = (file == currentFile_);
            if (ImGui::Selectable(leaf.c_str(), selected))
            {
                LoadFile(file);
            }
            if (selected && dirty_)
            {
                // 未保存はタイトルだけでなく一覧でも分かるようにする
                ImGui::SameLine();
                ImGui::TextColored(DebugTheme::kAccentYellow, "*");
            }
        }
    }
    ImGui::EndChild();
}

void ShaderEditorWindow::DrawEditor()
{
    if (currentFile_.empty())
    {
        ImGui::Dummy(ImVec2(0.0f, ImGui::GetFrameHeight()));
        DimText("左の一覧からシェーダーを選んでください");
        return;
    }

    // ---- 操作行 ----
    if (ConfirmButton("保存"))
    {
        SaveCurrentFile();
    }
    ImGui::SetItemTooltip("Ctrl+S でも保存できます\nシェーダーは起動時にコンパイルされるので、反映は次回起動からです");

    ImGui::SameLine();
    if (NeutralButton("読み直す"))
    {
        LoadFile(currentFile_);
    }
    ImGui::SetItemTooltip("ディスクの内容へ戻します（編集内容は失われます）");

    ImGui::SameLine();
    StatusBadge(dirty_ ? "未保存" : "保存済み",
                dirty_ ? DebugTheme::kAccentYellow : DebugTheme::kAccentGreen);

    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    DimText(currentFile_.c_str());

    if (!statusMessage_.empty())
    {
        DimText(statusMessage_.c_str());
    }

    // ---- 本体 ----
    TextEditor &editor = Editor();
    editor.Render("##shadertext", ImVec2(0, 0), true);

    // TextEditor は「このフレームで文字が変わったか」を教えてくれるので、
    // それを未保存フラグに積む
    if (editor.IsTextChanged())
    {
        dirty_ = true;
    }

    // 窓にフォーカスがあるときだけ Ctrl+S を拾う
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
        ImGui::IsKeyDown(ImGuiMod_Ctrl) && ImGui::IsKeyPressed(ImGuiKey_S, false))
    {
        SaveCurrentFile();
    }
}

void ShaderEditorWindow::Draw(bool *open)
{
    // 表示名は日本語、ウィンドウIDは英語で固定（保存済みレイアウトとの互換のため）
    if (!ImGui::Begin("シェーダー###ShaderEditor", open, ImGuiWindowFlags_NoFocusOnAppearing))
    {
        ImGui::End();
        return;
    }

    // 初回だけ走査する。毎フレーム掘ると窓を開けている間ずっとディスクを触ることになる
    if (!initialized_)
    {
        initialized_ = true;
        RefreshFileList();
    }

    if (NeutralButton("一覧を更新"))
    {
        RefreshFileList();
    }
    ImGui::SetItemTooltip("shaders/ フォルダを見直します");
    ImGui::SameLine();
    DimText((std::to_string(files_.size()) + " ファイル").c_str());

    ImGui::Separator();

    // 左: 一覧 / 右: エディタ
    const float listWidth = ImGui::GetFontSize() * 14.0f;
    if (ImGui::BeginTable("##shaderlayout", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV))
    {
        ImGui::TableSetupColumn("##list", ImGuiTableColumnFlags_WidthFixed, listWidth);
        ImGui::TableSetupColumn("##edit", ImGuiTableColumnFlags_WidthStretch);

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        DrawFileList();

        ImGui::TableNextColumn();
        DrawEditor();

        ImGui::EndTable();
    }

    ImGui::End();
}

} // namespace Hagine
#endif // USE_IMGUI
