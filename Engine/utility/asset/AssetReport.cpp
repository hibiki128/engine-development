#include "AssetReport.h"
#include "AssetPath.h"
#include "utility/debug/imgui/ImGuiNotification.h"
#include <debug/log/Logger.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <mutex>
#include <system_error>
#include <unordered_map>

namespace Hagine {
namespace AssetReport {

namespace {

// 同じパスを続けて知らせない間隔。毎フレーム読みに来る呼び出しでも通知が積もらないようにする
constexpr std::chrono::seconds kRepeatInterval{10};

// 通知の色と表示フレーム数（警告なので通常の通知より長めに出す）
const Vector4 kColor = {0.95f, 0.45f, 0.35f, 1.0f};
constexpr int kDurationFrames = 600;

std::mutex gMutex;
std::unordered_map<std::string, std::chrono::steady_clock::time_point> gLastReported;

/// <summary>
/// エンジンの文字列 (UTF-8) をパスにする。std::string のまま渡すと ANSI として解釈され、
/// 日本語のファイル名が化けて存在チェックを誤る
/// </summary>
std::filesystem::path ToPath(const std::string &utf8)
{
    return std::filesystem::path(std::u8string(utf8.begin(), utf8.end()));
}

bool Exists(const std::string &utf8)
{
    std::error_code ec;
    return std::filesystem::exists(ToPath(utf8), ec);
}

/// <summary>
/// 前回の知らせから間が空いていれば true を返し、時刻を控える
/// </summary>
bool ShouldReport(const std::string &path)
{
    std::lock_guard<std::mutex> lock(gMutex);
    const auto now = std::chrono::steady_clock::now();
    auto it = gLastReported.find(path);
    if (it != gLastReported.end() && now - it->second < kRepeatInterval)
    {
        return false;
    }
    gLastReported[path] = now;
    return true;
}

} // namespace

void Failed(const std::string &kind, const std::string &rawPath, const std::string &reason, const std::string &hint)
{
    // ルート (末尾 '/') とファイル名を '/' でつなぐ呼び出しが多く "models//xxx" になるので整える。
    // 同じファイルを別の書き方で探しても 1 回の知らせにまとまるようにする意味もある
    std::string path = rawPath;
    std::replace(path.begin(), path.end(), '\\', '/');
    for (size_t pos = path.find("//"); pos != std::string::npos; pos = path.find("//", pos))
    {
        path.erase(pos, 1);
    }

    if (!ShouldReport(path))
    {
        return;
    }

    // 理由はファイルがあるのに読めなかったときだけ添える。無いときの理由（「開けません」等）は見出しの繰り返しになる
    const bool exists = Exists(path);
    std::string message = exists ? kind + "を読み込めませんでした: " + path : kind + "が見つかりません: " + path;
    if (exists && !reason.empty())
    {
        message += "\n  " + reason;
    }
    if (!hint.empty())
    {
        message += "\n  " + hint;
    }

    Logger::Error(message);
    ImGuiNotification::PostError(message, kColor, kDurationFrames);
}

std::string SuggestOtherRoot(const std::string &category, const std::string &rel)
{
    const std::string folder = category + "/";
    if (AssetPath::IsEngineRelative(rel))
    {
        // "debug/xxx" でエンジン側を見て無かった → アプリ側の "xxx" にあるか
        const std::string appRel = rel.size() > 6 ? rel.substr(6) : std::string();
        if (!appRel.empty() && Exists(AssetPath::AppRoot() + folder + appRel))
        {
            return "アプリ側 (" + AssetPath::AppRoot() + folder + ") に " + appRel + " があります。先頭の debug/ を外してください";
        }
        return {};
    }

    // "xxx" でアプリ側を見て無かった → エンジン側の "debug/xxx" にあるか
    const std::string engineRel = "debug/" + rel;
    if (Exists(AssetPath::EngineRoot() + folder + engineRel))
    {
        return "エンジン側 (" + AssetPath::EngineRoot() + folder + ") に " + engineRel + " があります。先頭に debug/ を付けてください";
    }
    return {};
}

} // namespace AssetReport
} // namespace Hagine
