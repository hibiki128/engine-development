#pragma once
#ifdef USE_IMGUI
#include <functional>
#include <imgui.h>
#include <string>
#include <vector>

namespace Hagine {

/// <summary>
/// 「シーン設定」「シーンのオブジェクト設定」の窓の中身をそろえるパネル。
/// シーンは毎フレーム Add で「見出し・アイコン・色・中身を描く関数」を並べ、最後に Draw を呼ぶだけでよい。
/// ・上に検索欄（見出しと、Add で渡したキーワードで絞り込む）
/// ・タブ表示と一覧表示（折りたたみ）を切り替えられる
/// ・右クリックで「よく使う」に固定すると先頭に並ぶ（固定は Application/Config に保存される）
/// </summary>
class SettingsPanel
{
  public:
    /// <param name="id">窓ごとの名前（固定の保存に使う。窓どうしで重ならないもの）</param>
    explicit SettingsPanel(const char *id) : id_(id) {}

    /// <summary>
    /// 見出しを1つ並べる（Draw を呼ぶと消える。毎フレーム呼ぶ）
    /// </summary>
    /// <param name="title">見出し</param>
    /// <param name="icon">アイコン（ICON_FA_*。無ければ ""）</param>
    /// <param name="accent">見出しの色</param>
    /// <param name="draw">中身を描く関数</param>
    /// <param name="keywords">検索で引っかける言葉（空白区切り。中の項目名など）</param>
    void Add(const std::string &title, const char *icon, const ImVec4 &accent, std::function<void()> draw, const std::string &keywords = "");

    /// <summary>並べた見出しを描く（描いたら並びを空にする）</summary>
    void Draw();

  private:
    struct Section
    {
        std::string title;
        std::string icon;
        ImVec4 accent;
        std::function<void()> draw;
        std::string keywords;
    };

    bool IsPinned(const std::string &title) const;
    void TogglePin(const std::string &title);
    void DrawPinMenu(const std::string &title);

    std::string id_;
    std::vector<Section> sections_;
    std::string search_;
    bool listMode_ = false;
};

} // namespace Hagine
#endif // USE_IMGUI
