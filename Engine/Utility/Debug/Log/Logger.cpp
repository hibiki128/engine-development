#include "Logger.h"
#include <Windows.h>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <mutex>

namespace Hagine {
namespace Logger {
namespace {

std::mutex gLogMutex; // 複数箇所からの出力を直列化する

/// <summary>
/// 実行ファイルと同じ場所のログファイルパスを返す
/// </summary>
std::string GetLogFilePath() {
    char modulePath[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, modulePath, MAX_PATH);
    std::filesystem::path path(modulePath);
    path.replace_filename("GameLog.txt"); // exeと同じフォルダに出力する
    return path.string();
}

/// <summary>
/// 現在時刻を指定フォーマットの文字列にして返す
/// </summary>
/// <param name="format">strftime形式のフォーマット</param>
std::string NowString(const char *format) {
    std::time_t now = std::time(nullptr);
    std::tm local{};
    localtime_s(&local, &now);
    char buffer[64] = {};
    std::strftime(buffer, sizeof(buffer), format, &local);
    return buffer;
}

/// <summary>
/// 起動ごとに一度だけ上書きで開くログファイルストリームを返す
/// </summary>
std::ofstream &LogFile() {
    static std::ofstream file = [] {
        std::ofstream f(GetLogFilePath(), std::ios::out | std::ios::trunc);
        if (f.is_open()) {
            f << "==================================================\n";
            f << "  Game Log  -  " << NowString("%Y-%m-%d %H:%M:%S") << "\n";
            f << "==================================================\n";
            f.flush();
        }
        return f;
    }();
    return file;
}

/// <summary>
/// 重要度に対応するタグ文字列を返す
/// </summary>
const char *LevelTag(LogLevel level) {
    switch (level) {
    case LogLevel::Warning:
        return "WARNING";
    case LogLevel::Error:
        return "ERROR";
    case LogLevel::Info:
    default:
        return "INFO";
    }
}

/// <summary>
/// メッセージ末尾の改行を取り除く（行末で一つだけ付け直すため）
/// </summary>
std::string TrimTrailingNewline(const std::string &message) {
    std::string trimmed = message;
    while (!trimmed.empty() && (trimmed.back() == '\n' || trimmed.back() == '\r')) {
        trimmed.pop_back();
    }
    return trimmed;
}

/// <summary>
/// 1行分のログをデバッグ出力とログファイルへ書き出す
/// </summary>
void WriteLine(LogLevel level, const std::string &message) {
    std::lock_guard<std::mutex> lock(gLogMutex);

    const std::string line =
        "[" + NowString("%H:%M:%S") + "][" + LevelTag(level) + "] " + TrimTrailingNewline(message) + "\n";

    // Visual Studio の出力ウィンドウ
    OutputDebugStringA(line.c_str());

    // 実行ファイルと同じ場所のログファイル
    std::ofstream &file = LogFile();
    if (file.is_open()) {
        file << line;
        file.flush(); // クラッシュしても直前までのログが残るように毎回フラッシュする
    }
}

} // namespace

void Log(const std::string &message) {
    WriteLine(LogLevel::Info, message);
}

void Log(LogLevel level, const std::string &message) {
    WriteLine(level, message);
}

void Info(const std::string &message) {
    WriteLine(LogLevel::Info, message);
}

void Warn(const std::string &message) {
    WriteLine(LogLevel::Warning, message);
}

void Error(const std::string &message) {
    WriteLine(LogLevel::Error, message);
}

} // namespace Logger
} // namespace Hagine
