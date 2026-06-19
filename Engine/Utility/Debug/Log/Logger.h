#pragma once
#include <string>
namespace Hagine {

/// <summary>
/// ログ出力ユーティリティ
/// </summary>
namespace Logger
{
/// <summary>
/// メッセージをデバッグ出力に書き出す
/// </summary>
/// <param name="message">出力するメッセージ</param>
void Log(const std::string& message);
}
} // namespace Hagine
