#pragma once
#include <functional>
#include <string>
#include <vector>

namespace Hagine {

/// <summary>
/// パーティクルエディタ（GPU / CPU）の「作成」「削除」タブで共通に使う部品。
/// どちらのエディタも「名前で管理するエミッター」と「jsons/(フォルダ)/名前.json の保存ファイル」を持つので、
/// その2つを渡すだけで同じ見た目・同じ手順（名前の確かめ・読み込み・確認付き削除と元に戻す）にそろえる
/// </summary>
namespace ParticleEditorUI {

/// <summary>
/// 名前の確かめ結果
/// </summary>
struct NameCheck
{
    bool ok = false;     ///< この名前で作れるか
    std::string message; ///< だめな理由（作れるときは空）
};

/// <summary>
/// エミッター名として使えるかを確かめる（空・ファイル名に使えない文字・読み込み済み・保存済みファイルあり）
/// </summary>
/// <param name="name">確かめる名前</param>
/// <param name="jsonDirectory">保存ファイルのフォルダ</param>
/// <param name="isLoaded">その名前がエディタに読み込み済みか</param>
/// <returns>NameCheck: 結果</returns>
NameCheck CheckEmitterName(const std::string &name, const std::string &jsonDirectory, const std::function<bool(const std::string &)> &isLoaded);

/// <summary>
/// 使える名前を作る（base がだめなら base_2, base_3 … と番号を付ける）
/// </summary>
std::string MakeUniqueEmitterName(const std::string &base, const std::string &jsonDirectory, const std::function<bool(const std::string &)> &isLoaded);

/// <summary>
/// 名前の入力欄。下に確かめ結果（だめなら赤字の理由）を出す
/// </summary>
/// <param name="id">ImGui の ID</param>
/// <param name="name">入力中の名前</param>
/// <param name="hint">空のときに薄く出す文字</param>
/// <returns>NameCheck: 今の名前の確かめ結果（空なら ok=false・message 空）</returns>
NameCheck DrawNameField(const char *id, std::string &name, const char *hint, const std::string &jsonDirectory,
                        const std::function<bool(const std::string &)> &isLoaded);

/// <summary>
/// 保存済みでまだ読み込んでいないファイルの一覧（検索つき）。押すと load を呼ぶ
/// </summary>
/// <param name="id">ImGui の ID</param>
/// <param name="search">検索語（呼び出し側が持つ）</param>
void DrawSavedFileLoader(const char *id, std::string &search, const std::string &jsonDirectory,
                         const std::function<bool(const std::string &)> &isLoaded, const std::function<void(const std::string &)> &load);

/// <summary>
/// 削除タブの状態（呼び出し側が持つ）
/// </summary>
struct DeleteState
{
    std::string search;       ///< 一覧の検索語
    std::string selected;     ///< 選んでいる名前
    bool deleteFile = false;  ///< 保存ファイルも消すか
    std::string pending;      ///< 確認窓で消そうとしている名前
};

/// <summary>
/// エミッターの削除UI。一覧から選び、確認してから外す。消したあとは「元に戻す」付きの通知を出す。
/// 元に戻すと、消した保存ファイルを書き戻してから reload で読み込み直す
/// </summary>
/// <param name="id">ImGui の ID</param>
/// <param name="state">削除タブの状態</param>
/// <param name="names">今エディタにある名前の一覧</param>
/// <param name="jsonDirectory">保存ファイルのフォルダ</param>
/// <param name="remove">エディタから外す処理</param>
/// <param name="reload">元に戻すときに読み込み直す処理</param>
/// <param name="describe">選んでいる物の説明（1行。空でもよい）</param>
void DrawEmitterDeleteList(const char *id, DeleteState &state, std::vector<std::string> names, const std::string &jsonDirectory,
                           const std::function<void(const std::string &)> &remove, const std::function<void(const std::string &)> &reload,
                           const std::function<std::string(const std::string &)> &describe = {});

/// <summary>保存ファイルのパス（jsonDirectory/name.json）</summary>
std::string JsonPathOf(const std::string &jsonDirectory, const std::string &name);

/// <summary>ファイルの中身をそのまま読む（無ければ false）</summary>
bool ReadFileBytes(const std::string &path, std::string &outBytes);

/// <summary>ファイルへそのまま書く</summary>
bool WriteFileBytes(const std::string &path, const std::string &bytes);

} // namespace ParticleEditorUI
} // namespace Hagine
