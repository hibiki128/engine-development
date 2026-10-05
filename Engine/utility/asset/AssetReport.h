#pragma once
#include <string>

namespace Hagine {

/// <summary>
/// アセットが見つからない・読めないときの知らせ方を 1 か所にまとめたもの。
///
/// assert で止めずに処理を続けさせ、代わりに
///   - ログ (VS の出力ウィンドウと GameLog.txt)
///   - 画面右下の通知 (ImGuiNotification。通常の「読み込みました」に押し流されない)
/// の両方で知らせる。
///
/// 毎フレーム読み直そうとする呼び出し (PlayOneShot や描画中の参照など) で
/// 通知が埋め尽くされないよう、同じパスの知らせは一定時間に 1 回にまとめる。
/// </summary>
namespace AssetReport {

/// <summary>
/// アセットを読めなかったことを知らせる。
/// ファイルが無ければ「見つかりません」、あれば「読み込めませんでした」と出し分ける。
/// </summary>
/// <param name="kind">アセットの種類 ("モデル" "テクスチャ" など)</param>
/// <param name="path">探した実パス (AssetPath で解決したもの)</param>
/// <param name="reason">読めなかった理由 (ファイルがあるのに読めなかったときだけ出す。無ければ空)</param>
/// <param name="hint">直し方の案内 (SuggestOtherRoot の結果など。無ければ空)</param>
void Failed(const std::string &kind, const std::string &path, const std::string &reason = "",
            const std::string &hint = "");

/// <summary>
/// images / models は相対パスの先頭が "debug/" かどうかでエンジン側とアプリ側に振り分けられる。
/// 振り分け先に無かったとき、反対側に同じファイルがあればその案内文を返す。
/// (例: "animation/Idle.gltf" を探してアプリ側に無く、エンジン側の
///  "debug/animation/Idle.gltf" にあった場合など)
/// </summary>
/// <param name="category">"images" か "models"</param>
/// <param name="rel">images / models ルートからの相対パス</param>
/// <returns>案内文。反対側にも無ければ空</returns>
std::string SuggestOtherRoot(const std::string &category, const std::string &rel);

} // namespace AssetReport
} // namespace Hagine
