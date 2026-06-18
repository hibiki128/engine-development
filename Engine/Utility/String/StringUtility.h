#pragma once
#include <string>
namespace Hagine {

/// <summary>
/// 文字列変換ユーティリティ
/// </summary>
namespace StringUtility {

/// <summary>
/// string を wstring に変換する
/// </summary>
/// <param name="str">変換元の string</param>
/// <returns>std::wstring: 変換後の文字列</returns>
std::wstring ConvertString(const std::string& str);

/// <summary>
/// wstring を string に変換する
/// </summary>
/// <param name="str">変換元の wstring</param>
/// <returns>std::string: 変換後の文字列</returns>
std::string ConvertString(const std::wstring& str);
}
} // namespace Hagine
