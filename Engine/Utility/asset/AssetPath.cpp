#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include "AssetPath.h"

#include <Windows.h>
#include <filesystem>
#include <system_error>

namespace Hagine {
namespace AssetPath {

namespace {

/// <summary>
/// exe が置かれているディレクトリを取得する (末尾スラッシュ無し)。
/// wide 文字列で取得するため、パスに日本語等が含まれていても欠落しない。
/// </summary>
/// <returns>exe のディレクトリ。取得失敗時は空。</returns>
std::filesystem::path GetExecutableDir()
{
    std::wstring buffer(MAX_PATH, L'\0');

    // バッファに収まらない長いパスにも対応できるよう、足りなければ拡張する
    for (;;)
    {
        const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0)
        {
            return {}; // 取得失敗
        }
        if (length < buffer.size())
        {
            buffer.resize(length);
            break;
        }
        buffer.resize(buffer.size() * 2);
    }

    return std::filesystem::path(buffer).parent_path();
}

/// <summary>
/// exe のディレクトリ (取得は一度きり)。
/// </summary>
const std::filesystem::path &CachedExecutableDir()
{
    static const std::filesystem::path kDir = GetExecutableDir();
    return kDir;
}

/// <summary>
/// path を UTF-8 の std::string へ変換し、末尾にスラッシュ ('/') を付けて返す。
/// エンジンの文字列は UTF-8 前提 (StringUtility::ConvertString が CP_UTF8) のため UTF-8 で揃える。
/// </summary>
std::string ToUtf8RootString(const std::filesystem::path &path)
{
    const std::u8string utf8 = path.generic_u8string();
    std::string result(utf8.begin(), utf8.end());
    if (!result.empty() && result.back() != '/')
    {
        result += '/';
    }
    return result;
}

/// <summary>
/// アセットルートを解決する。
/// exe と同じ場所に bundledDirName フォルダがあればその絶対パス (配布時) を、
/// 無ければ sourceTreeFallback (開発時) を返す。
/// 存在判定とパス構築は wide/filesystem で行い、文字コードの取り違えを避ける。
/// </summary>
/// <param name="bundledDirName">exe 隣に置く配布用フォルダ名 (例: L"EngineAssets")</param>
/// <param name="sourceTreeFallback">開発時に使うソースツリー相対パス (末尾スラッシュ付き)</param>
std::string ResolveRoot(const wchar_t *bundledDirName, const char *sourceTreeFallback)
{
    const std::filesystem::path &exeDir = CachedExecutableDir();
    if (!exeDir.empty())
    {
        std::error_code ec;
        const std::filesystem::path bundled = exeDir / bundledDirName;
        if (std::filesystem::exists(bundled, ec))
        {
            return ToUtf8RootString(bundled);
        }
    }
    return std::string(sourceTreeFallback);
}

} // namespace

const std::string &EngineRoot()
{
    // 配布時: <exe>/EngineAssets/ ／ 開発時: ../module/Hagine/Engine/EngineAssets/
    static const std::string kRoot = ResolveRoot(L"EngineAssets", "../module/Hagine/Engine/EngineAssets/");
    return kRoot;
}

const std::string &AppRoot()
{
    // 配布時: <exe>/Assets/ ／ 開発時: ../app/Assets/
    static const std::string kRoot = ResolveRoot(L"Assets", "../app/Assets/");
    return kRoot;
}

} // namespace AssetPath
} // namespace Hagine
