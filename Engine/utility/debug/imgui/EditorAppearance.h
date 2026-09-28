#pragma once
#ifdef USE_IMGUI
#include "type/Vector4.h"

struct ImGuiStyle;

namespace Hagine {

/// <summary>
/// エディタの外観（配色・アクセント色・UIの大きさ・角の丸み）。
///
/// 配色の土台は ImGuiManager::SetupTheme の near-black テーマで、ここはその上から
/// 「無彩色の段階を持ち上げる／色味を乗せる」「アクセント色の色相を回す」変換を掛ける。
/// 土台の明度設計（台座 → 窓 → 入力欄 → ホバーの段階）は崩さずに雰囲気だけ変えられる。
/// 既定値（テーマ0・アクセント0・倍率1）では SetupTheme の見た目と完全に同じになる。
///
/// 設定は ImGuiSetting/Appearance.json に保存され、次回起動時に戻る。
/// </summary>
class EditorAppearance
{
  public:
    /// <summary>テーマ（地の色）のプリセット数</summary>
    static constexpr int kThemeCount = 4;
    /// <summary>アクセント色のプリセット数（最後がカスタム）</summary>
    static constexpr int kAccentCount = 7;

    /// <summary>設定値</summary>
    struct Settings
    {
        int theme = 0;                                     // 地の色（0=ニアブラック）
        int accent = 0;                                    // アクセント色（0=スチールブルー、最後=カスタム）
        Vector4 customAccent = {0.435f, 0.541f, 0.659f, 1.0f}; // カスタムのアクセント色
        float uiScale = 1.0f;                              // 文字と余白の大きさ
        float rounding = 1.0f;                             // 角の丸み（0で角ばる）
    };

    /// <summary>保存済みの設定を読む</summary>
    void Load();

    /// <summary>設定を保存する</summary>
    void Save() const;

    /// <summary>
    /// SetupTheme 済みのスタイルへ、設定どおりの色変換と寸法の倍率を掛ける
    /// </summary>
    /// <param name="style">SetupTheme 直後のスタイル</param>
    void ApplyTo(ImGuiStyle &style) const;

    /// <summary>
    /// 設定窓を描く
    /// </summary>
    /// <param name="open">閉じるボタン用のフラグ</param>
    /// <returns>bool: 見た目を作り直す必要がある変更があったら true</returns>
    bool DrawWindow(bool *open);

    /// <summary>テーマを切り替える（コマンドパレットから使う）</summary>
    void SetTheme(int theme);

    /// <summary>アクセント色を切り替える（コマンドパレットから使う）</summary>
    void SetAccent(int accent);

    /// <summary>テーマの表示名</summary>
    static const char *GetThemeName(int theme);

    /// <summary>アクセント色の表示名</summary>
    static const char *GetAccentName(int accent);

    const Settings &GetSettings() const { return settings_; }

  private:
    Settings settings_;
};

} // namespace Hagine
#endif // USE_IMGUI
