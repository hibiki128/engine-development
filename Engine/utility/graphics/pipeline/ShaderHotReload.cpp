#include "ShaderHotReload.h"
#include "ComputeEffectPipeline.h"
#include "ComputePipelineManager.h"
#include "PipelineManager.h"
#include <DirectXCommon.h>
#include <asset/AssetPath.h>
#include <chrono>
#include <debug/log/Logger.h>
#include <filesystem>
#include <frame/Frame.h>
#include <string/stringUtility.h>
#include <unordered_map>
#ifdef USE_IMGUI
#include "imgui.h"
#include "utility/debug/imgui/DebugUIHelper.h"
#include "utility/debug/imgui/ImGuiNotification.h"
#endif

namespace Hagine {
namespace {

/// 更新時刻を見にいく間隔（秒）。毎フレーム走査すると数十ファイル分のstatで無駄に重い
constexpr float kPollIntervalSeconds = 0.5f;

/// 相対パス → 最後に見た更新時刻
std::unordered_map<std::string, std::filesystem::file_time_type> &TimestampTable()
{
    static std::unordered_map<std::string, std::filesystem::file_time_type> table;
    return table;
}

/// パス区切りを "/" に揃える（一覧の見た目と辞書のキーを一致させるため）
std::string NormalizeSeparators(std::string path)
{
    for (char &c : path)
    {
        if (c == '\\')
        {
            c = '/';
        }
    }
    return path;
}

/// ファイル名の "○○.VS.hlsl" という書き方からシェーダープロファイルを決める。
/// このエンジンのシェーダーはすべてこの命名になっている
const wchar_t *ResolveProfile(const std::string &relativePath)
{
    if (relativePath.find(".VS.hlsl") != std::string::npos)
    {
        return L"vs_6_0";
    }
    if (relativePath.find(".PS.hlsl") != std::string::npos)
    {
        return L"ps_6_0";
    }
    if (relativePath.find(".CS.hlsl") != std::string::npos)
    {
        return L"cs_6_0";
    }
    return nullptr;
}
} // namespace

ShaderHotReload *ShaderHotReload::GetInstance()
{
    static ShaderHotReload instance;
    return &instance;
}

void ShaderHotReload::Initialize(DirectXCommon *pDxCommon)
{
    pDxCommon_ = pDxCommon;

    // 起動時の更新時刻を基準にする。ここで拾った変更は「起動前のもの」なので作り直さない
    CollectChanges(nullptr, nullptr);
    lastMessage_ = "まだ作り直していません";
    lastReloadSucceeded_ = true;
}

void ShaderHotReload::Update()
{
#ifdef USE_IMGUI
    if (!pDxCommon_ || !autoReloadEnabled_)
    {
        return;
    }

    pollTimer_ -= Frame::UnscaledDeltaTime();
    if (pollTimer_ > 0.0f)
    {
        return;
    }
    pollTimer_ = kPollIntervalSeconds;

    bool changedAnyInclude = false;
    std::string changedShaders;
    if (!CollectChanges(&changedAnyInclude, &changedShaders))
    {
        return;
    }

    // .hlsli は単体でコンパイルできないので、変わったときだけ全ての .hlsl を確かめる。
    // 1本の .hlsl を直しただけなら、その1本を見れば足りる
    RunReload(changedAnyInclude, changedShaders);
#endif
}

bool ShaderHotReload::ReloadNow()
{
    if (!pDxCommon_)
    {
        return false;
    }

    // 控えを取り直しておく。ここで取らないと、直後の Update がもう一度作り直してしまう
    CollectChanges(nullptr, nullptr);

    // 手で押したときは「全部を作り直したい」という意思なので、全ての .hlsl を確かめる
    return RunReload(true, {});
}

bool ShaderHotReload::RunReload(bool verifyAll, const std::string &changedShaders)
{
    if (!pDxCommon_)
    {
        return false;
    }

    std::string failedPath;
    std::string compileError;

    const std::string root = AssetPath::Shader("");
    std::error_code ec;
    if (!std::filesystem::exists(root, ec))
    {
        lastReloadSucceeded_ = false;
        lastMessage_ = "シェーダーフォルダが見つかりません: " + root;
        return false;
    }

    auto verify = [&](const std::string &relativePath) -> bool {
        std::string error;
        if (VerifyShader(relativePath, &error))
        {
            return true;
        }
        failedPath = relativePath;
        compileError = error;
        return false;
    };

    bool verified = true;
    if (verifyAll)
    {
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
            const std::filesystem::path rel = std::filesystem::relative(entry.path(), root, ec);
            if (ec)
            {
                continue;
            }
            if (!verify(NormalizeSeparators(rel.string())))
            {
                verified = false;
                break;
            }
        }
    }
    else
    {
        // 変わった .hlsl だけを見る（改行区切りで積んである）
        size_t begin = 0;
        while (begin < changedShaders.size() && verified)
        {
            const size_t end = changedShaders.find('\n', begin);
            const std::string relativePath = changedShaders.substr(
                begin, (end == std::string::npos) ? std::string::npos : end - begin);
            if (!relativePath.empty() && !verify(relativePath))
            {
                verified = false;
            }
            if (end == std::string::npos)
            {
                break;
            }
            begin = end + 1;
        }
    }

    if (!verified)
    {
        lastReloadSucceeded_ = false;
        lastMessage_ = failedPath + "\n" + compileError;
        Logger::Error("シェーダーの作り直しを中止しました: " + failedPath + " / " + compileError);
#ifdef USE_IMGUI
        ImGuiNotification::Post("シェーダーにエラーがあります: " + failedPath, {0.85f, 0.35f, 0.35f, 1.0f});
#endif
        return false;
    }

    // ── ここから実際に作り直す ──
    // GPUがまだ前のフレームを描いている最中にPSOを捨てると、
    // デバッグレイヤーが「使用中のリソースを解放した」と言って止まる
    const auto startTime = std::chrono::steady_clock::now();
    pDxCommon_->WaitForGPU();

    // 1本直しただけでも全PSOを作り直している。どのシェーダーがどのPSOに使われているかの
    // 対応表を持っていないため。数秒かかるが、再起動してシーンを開き直すよりはるかに速い
    PipelineManager::GetInstance()->Reload();
    ComputePipelineManager::GetInstance()->Reload();
    // ポストエフェクトのCSは必要になった時点で作り直される
    ComputeEffectPipeline::GetInstance()->ClearCache();

    const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                               std::chrono::steady_clock::now() - startTime)
                               .count();

    ++reloadCount_;
    lastReloadSucceeded_ = true;
    lastMessage_ = "作り直しました (" + std::to_string(elapsedMs) + " ms)";
    Logger::Info("シェーダーを作り直しました (" + std::to_string(elapsedMs) + " ms)");
#ifdef USE_IMGUI
    ImGuiNotification::Post(lastMessage_, {0.35f, 0.80f, 0.45f, 1.0f});
#endif
    return true;
}

bool ShaderHotReload::CollectChanges(bool *outChangedAnyInclude, std::string *outChangedShaders)
{
    const std::string root = AssetPath::Shader("");
    std::error_code ec;
    if (!std::filesystem::exists(root, ec))
    {
        return false;
    }

    auto &table = TimestampTable();
    bool changed = false;

    for (const auto &entry : std::filesystem::recursive_directory_iterator(root, ec))
    {
        if (ec)
        {
            break;
        }
        if (!entry.is_regular_file())
        {
            continue;
        }
        const std::filesystem::path &path = entry.path();
        const bool isShader = (path.extension() == ".hlsl");
        const bool isInclude = (path.extension() == ".hlsli");
        if (!isShader && !isInclude)
        {
            continue;
        }

        const std::filesystem::path rel = std::filesystem::relative(path, root, ec);
        if (ec)
        {
            continue;
        }
        const std::string key = NormalizeSeparators(rel.string());

        const auto writeTime = std::filesystem::last_write_time(path, ec);
        if (ec)
        {
            continue;
        }

        auto it = table.find(key);
        if (it == table.end())
        {
            // 初回に見たファイル。基準として控えるだけで、変更扱いにはしない
            table.emplace(key, writeTime);
            continue;
        }
        if (it->second == writeTime)
        {
            continue;
        }

        it->second = writeTime;
        changed = true;
        if (isInclude && outChangedAnyInclude)
        {
            *outChangedAnyInclude = true;
        }
        if (isShader && outChangedShaders)
        {
            *outChangedShaders += key;
            *outChangedShaders += '\n';
        }
    }
    return changed;
}

bool ShaderHotReload::VerifyShader(const std::string &relativePath, std::string *outError)
{
    const wchar_t *profile = ResolveProfile(relativePath);
    if (!profile)
    {
        // 命名からプロファイルを決められないものは確かめようがないので通す
        return true;
    }
    const std::wstring fullPath = StringUtility::ConvertString(AssetPath::Shader(relativePath));
    return pDxCommon_->TryCompileShader(fullPath, profile, outError);
}

void ShaderHotReload::DrawImGui()
{
#ifdef USE_IMGUI
    ImGui::Checkbox("保存を検知して自動で作り直す", &autoReloadEnabled_);
    ImGui::SetItemTooltip("shaders/ 配下のHLSLが更新されたら、その場でパイプラインを作り直します。\n"
                          "エラーがあるシェーダーは作り直しを中止するので、書きかけを保存しても落ちません");

    if (ConfirmButton("今すぐ作り直す"))
    {
        ReloadNow();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("作り直した回数: %d", reloadCount_);

    if (!lastMessage_.empty())
    {
        const ImVec4 color = lastReloadSucceeded_ ? DebugTheme::kAccentGreen : DebugTheme::kAccentRed;
        ImGui::PushStyleColor(ImGuiCol_Text, color);
        ImGui::TextWrapped("%s", lastMessage_.c_str());
        ImGui::PopStyleColor();
    }
#endif
}
} // namespace Hagine
