#pragma once
#include <string>
namespace Hagine {

/// <summary>
/// ログ出力ユーティリティ
/// デバッグ出力（VS出力ウィンドウ）と、実行ファイルと同じ場所の
/// ログファイル（GameLog.txt）の両方へ書き出す。
/// ログファイルは起動ごとに毎回上書きされる。
/// </summary>
namespace Logger {

/// <summary>
/// ログの重要度
/// </summary>
enum class LogLevel {
    Info,    // 情報
    Warning, // 警告（処理は継続できるが注意が必要）
    Error,   // エラー（読み込み失敗など）
};

/// <summary>
/// メッセージを情報ログとして書き出す（既存互換）
/// </summary>
/// <param name="message">出力するメッセージ</param>
void Log(const std::string &message);

/// <summary>
/// 重要度を指定してメッセージを書き出す
/// </summary>
/// <param name="level">ログの重要度</param>
/// <param name="message">出力するメッセージ</param>
void Log(LogLevel level, const std::string &message);

/// <summary>
/// 情報ログを書き出す
/// </summary>
/// <param name="message">出力するメッセージ</param>
void Info(const std::string &message);

/// <summary>
/// 警告ログを書き出す
/// </summary>
/// <param name="message">出力するメッセージ</param>
void Warn(const std::string &message);

/// <summary>
/// エラーログを書き出す
/// </summary>
/// <param name="message">出力するメッセージ</param>
void Error(const std::string &message);

} // namespace Logger
} // namespace Hagine
