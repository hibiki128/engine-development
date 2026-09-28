#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include "AssetPath.h"

#include <Windows.h>
#include <filesystem>
#include <initializer_list>
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
/// 1) exe と同じ場所に bundledDirName フォルダがあれば配布構成とみなし、
///    カレントディレクトリを exe の場所へ移したうえでフォルダ名だけの相対パスを返す。
/// 2) 無ければ開発時とみなし、sourceTreeCandidates を先頭から順に存在チェックして
///    最初に見つかったものを返す (リポジトリごとのフォルダ構成の違いを吸収する)。
/// 3) どれも見つからなければ先頭の候補を返す (呼び出し側で読み込み失敗として扱われる)。
///
/// 絶対パスを返さないのは、配布先フォルダ名に日本語が含まれていると起動できなくなるため。
/// エンジンの narrow (char) 文字列は UTF-8 だが、std::ifstream や Assimp (fopen) は
/// パスを ANSI (CP932) として解釈するので、UTF-8 の絶対パスは開けずに落ちる。
/// 相対パスなら ASCII のみで済み、日本語部分の解決は
/// ワイド版の SetCurrentDirectoryW とカレントディレクトリが受け持つので
/// 文字コードの問題が起きない (候補は必ず ASCII のみで書くこと)。
/// </summary>
/// <param name="bundledDirName">exe 隣に置く配布用フォルダ名 (例: L"EngineAssets")</param>
/// <param name="sourceTreeCandidates">開発時に使うソースツリー相対パスの候補 (末尾スラッシュ付き / ASCII のみ)</param>
std::string ResolveRoot(const wchar_t *bundledDirName, std::initializer_list<const char *> sourceTreeCandidates)
{
    const std::filesystem::path &exeDir = CachedExecutableDir();
    if (!exeDir.empty())
    {
        std::error_code ec;
        const std::filesystem::path bundled = exeDir / bundledDirName;
        if (std::filesystem::exists(bundled, ec))
        {
            // 相対パスの基準を exe の場所に固定する
            // (エクスプローラ以外から起動された場合はカレントが別の場所になっているため)
            SetCurrentDirectoryW(exeDir.c_str());
            return ToUtf8RootString(std::filesystem::path(bundledDirName));
        }
    }

    // 開発時: カレントディレクトリ (Visual Studio のデバッグ実行では vcxproj のあるフォルダ)
    // から見える候補を順に探す。
    for (const char *candidate : sourceTreeCandidates)
    {
        std::error_code ec;
        if (std::filesystem::exists(std::filesystem::path(candidate), ec))
        {
            return std::string(candidate);
        }
    }

    return std::string(*sourceTreeCandidates.begin());
}

} // namespace

const std::string &EngineRoot()
{
    // 配布時: EngineAssets/ (カレント = exe の場所)
    // 開発時: 以下の候補をカレントディレクトリから順に探す
    //   Engine/EngineAssets/                  : エンジン単体のリポジトリを開いている場合
    //   ../module/Hagine/Engine/EngineAssets/ : アプリ側リポジトリ (カレント = app/)
    //   module/Hagine/Engine/EngineAssets/    : アプリ側リポジトリのルートがカレントの場合
    //   EngineAssets/                         : カレント直下へコピー済みの場合
    static const std::string kRoot = ResolveRoot(L"EngineAssets",
                                                 {"Engine/EngineAssets/",
                                                  "../module/Hagine/Engine/EngineAssets/",
                                                  "module/Hagine/Engine/EngineAssets/",
                                                  "EngineAssets/"});
    return kRoot;
}

const std::string &AppRoot()
{
    // 配布時: Assets/ (カレント = exe の場所)
    // 開発時: 以下の候補をカレントディレクトリから順に探す
    //   Assets/             : アプリ側リポジトリ (カレント = app/)
    //   Application/Assets/ : アセットを Application/ 配下に置く構成
    //   app/Assets/         : アプリ側リポジトリのルートがカレントの場合
    static const std::string kRoot = ResolveRoot(L"Assets",
                                                 {"Assets/",
                                                  "Application/Assets/",
                                                  "app/Assets/"});
    return kRoot;
}

} // namespace AssetPath
} // namespace Hagine
