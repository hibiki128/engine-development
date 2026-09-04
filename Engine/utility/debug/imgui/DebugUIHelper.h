#pragma once
// ============================================================
//  DebugUIHelper.h  (revision 2)
//  ImGui 1.92.8 + ImGuizmo + ImPlot
//  * ASCII only  -- no environment-dependent characters
// ============================================================
#ifdef USE_IMGUI

// 回転ノブ（imgui-knobs）。imgui.h を使うため、本ヘッダは imgui.h の後に include する前提。
#include <imgui-knobs.h>
// トグルスイッチ（imgui_toggle）。ヘッダが軽いのでここで共有する。
#include <imgui_toggle.h>
#include <imgui_toggle_palette.h>
// std::string をそのまま InputText へ渡す（公式 misc/cpp）。
#include <imgui_stdlib.h>
#include <string>

namespace Hagine {
namespace DebugTheme {
// 原色を避け、色相は保ちつつ彩度を落としたシックなトーンで統一する。
// カテゴリの識別性は残しつつ、画面全体が落ち着いた印象になるよう調整。
constexpr ImVec4 kAccentBlue = {0.45f, 0.60f, 0.78f, 1.0f};
constexpr ImVec4 kAccentGreen = {0.45f, 0.68f, 0.52f, 1.0f};
constexpr ImVec4 kAccentOrange = {0.82f, 0.58f, 0.36f, 1.0f};
constexpr ImVec4 kAccentPurple = {0.62f, 0.50f, 0.74f, 1.0f};
constexpr ImVec4 kAccentRed = {0.80f, 0.46f, 0.46f, 1.0f};
constexpr ImVec4 kAccentCyan = {0.42f, 0.66f, 0.68f, 1.0f};
constexpr ImVec4 kAccentYellow = {0.80f, 0.72f, 0.42f, 1.0f};

constexpr ImVec4 kBgBlue = {0.45f, 0.60f, 0.78f, 0.12f};
constexpr ImVec4 kBgGreen = {0.45f, 0.68f, 0.52f, 0.12f};
constexpr ImVec4 kBgOrange = {0.82f, 0.58f, 0.36f, 0.12f};
constexpr ImVec4 kBgPurple = {0.62f, 0.50f, 0.74f, 0.12f};
constexpr ImVec4 kBgRed = {0.80f, 0.46f, 0.46f, 0.12f};
constexpr ImVec4 kBgYellow = {0.80f, 0.72f, 0.42f, 0.12f};

constexpr ImVec4 kHeaderBlue = {0.45f, 0.60f, 0.78f, 0.22f};
constexpr ImVec4 kHeaderGreen = {0.45f, 0.68f, 0.52f, 0.22f};
constexpr ImVec4 kHeaderOrange = {0.82f, 0.58f, 0.36f, 0.22f};
constexpr ImVec4 kHeaderPurple = {0.62f, 0.50f, 0.74f, 0.22f};
constexpr ImVec4 kHeaderYellow = {0.80f, 0.72f, 0.42f, 0.22f};

constexpr ImVec4 kTextDim = {0.55f, 0.55f, 0.60f, 1.0f};
constexpr ImVec4 kTextReadOnly = {0.70f, 0.75f, 0.80f, 1.0f};
// セクション内の小見出し（「発生」「寿命」など、値の並びの上に置く短いラベル）
constexpr ImVec4 kTextCaption = {0.56f, 0.69f, 0.86f, 1.0f};

// ボタンの「役割」色。地・ホバーの2色を1組で持つ（押下色はホバーと同じで足りている）。
// 個々の画面で色を選ばず、意味で選ぶ:
//   Primary = 主操作 / Confirm = 追加・確定 / Danger = 削除 / Neutral = リセットなどの地味な操作
constexpr ImVec4 kButtonPrimary = {0.22f, 0.38f, 0.54f, 0.85f};
constexpr ImVec4 kButtonPrimaryHover = {0.28f, 0.48f, 0.66f, 0.95f};
constexpr ImVec4 kButtonConfirm = {0.21f, 0.44f, 0.35f, 0.85f};
constexpr ImVec4 kButtonConfirmHover = {0.27f, 0.55f, 0.44f, 0.95f};
constexpr ImVec4 kButtonDanger = {0.46f, 0.24f, 0.24f, 0.85f};
constexpr ImVec4 kButtonDangerHover = {0.58f, 0.30f, 0.30f, 0.95f};
constexpr ImVec4 kButtonNeutral = {0.25f, 0.25f, 0.30f, 1.0f};
constexpr ImVec4 kButtonNeutralHover = {0.35f, 0.35f, 0.45f, 1.0f};
// 地を持たないボタン（パンくずリストなど、文字に見えるが押せるもの）
constexpr ImVec4 kButtonGhost = {0.0f, 0.0f, 0.0f, 0.0f};
constexpr ImVec4 kButtonGhostHover = {0.30f, 0.30f, 0.35f, 0.50f};

/// <summary>
/// アクセント色から入力欄（DragFloat 等）の背景色を作る。
/// セクションごとに色を直書きしていると濃さがばらつくので、必ずここを通す。
/// </summary>
/// <param name="accent">そのセクションのアクセント色（kAccentXxx）</param>
/// <returns>ImVec4: ImGuiCol_FrameBg に渡す色</returns>
constexpr ImVec4 FrameBg(const ImVec4 &accent)
{
    return {accent.x * 0.42f, accent.y * 0.42f, accent.z * 0.42f, 0.50f};
}
} // namespace DebugTheme

// ------------------------------------------------------------
// 行内のそろえ方
//
// ImGui::SameLine(120.0f) のような px 直書きは、フォントやラベルを変えた
// 途端に「詰まる／空きすぎる」でズレて見える。ここの2つを通すことで、
// 文字サイズ・窓幅にそのまま追随させる。
// ------------------------------------------------------------

/// <summary>
/// 「ラベル : 値」形式のラベル列の幅。文字サイズに追随する。
/// </summary>
/// <returns>float: ラベル列の幅(px)</returns>
inline float LabelColumnWidth()
{
    return ImGui::GetFontSize() * 7.5f;
}

/// <summary>
/// 1行を等幅の列に割って、チェックボックス等を縦に揃えて並べるための小物。
/// 行の先頭で作り、2列目以降の直前に Next(i) を呼ぶ。
/// 列幅は「その行に残っている幅」から決めるので、窓幅を変えても崩れない。
/// </summary>
/// <example>
/// InlineColumns cols(3);
/// ImGui::Checkbox("A", &a);
/// cols.Next(1); ImGui::Checkbox("B", &b);
/// cols.Next(2); ImGui::Checkbox("C", &c);
/// </example>
struct InlineColumns
{
    float startX = 0.0f;
    float columnWidth = 0.0f;

    /// <param name="count">その行に並べる列数</param>
    explicit InlineColumns(int count)
    {
        startX = ImGui::GetCursorPosX();
        const int n = (count > 0) ? count : 1;
        columnWidth = ImGui::GetContentRegionAvail().x / static_cast<float>(n);
    }

    /// <summary>index 列目（0 始まり）の先頭へ送る。index が 0 以下なら何もしない</summary>
    void Next(int index) const
    {
        if (index <= 0)
            return;
        ImGui::SameLine(startX + columnWidth * static_cast<float>(index));
    }
};

// ------------------------------------------------------------
// 左サイドバー付きセクション見出し (ASCII ラベルのみ)
// ------------------------------------------------------------
static void SectionHeader(const char *label, ImVec4 color)
{
    ImVec2 p = ImGui::GetCursorScreenPos();
    // 以前は GetTextLineHeightWithSpacing() を使っていたが、これは行間ぶん
    // （ItemSpacing.y）まで含む高さなので、色バーだけ文字より下へ伸びていた。
    float lh = ImGui::GetTextLineHeight();
    ImGui::GetWindowDrawList()->AddRectFilled(
        p, {p.x + 3.0f, p.y + lh},
        ImGui::ColorConvertFloat4ToU32(color), 2.0f);
    ImGui::SetCursorScreenPos({p.x + 8.0f, p.y});
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    ImGui::TextUnformatted(label);
    ImGui::PopStyleColor();
    ImGui::Spacing();
}

// ------------------------------------------------------------
// 読み取り専用行  label : value  (## が表示に出ない)
// ------------------------------------------------------------
static void ReadOnlyRow(const char *label, const char *fmt, ...)
{
    // 以前は Text("%-12s") で桁を埋めていたが、これはバイト数での詰めなので
    // 日本語ラベルでは効かず、プロポーショナルフォントでは幅もそろわない。
    // 値の開始位置は「ラベル列の幅」で決める（インデント下でも崩れないよう
    // 現在の x を基準にする）。
    const float labelX = ImGui::GetCursorPosX();
    ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
    ImGui::TextUnformatted(label);
    ImGui::PopStyleColor();
    ImGui::SameLine(labelX + LabelColumnWidth());
    va_list args;
    va_start(args, fmt);
    ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextReadOnly);
    ImGui::TextV(fmt, args);
    ImGui::PopStyleColor();
    va_end(args);
}

// ------------------------------------------------------------
// ステータスバッジ（角丸ボックス）
// ------------------------------------------------------------
static void StatusBadge(const char *text, ImVec4 color)
{
    // 高さはボタンと同じ GetFrameHeight() にそろえる。
    // 以前は「文字高 + 6px」で作っていたため、SameLine でボタンやチェックボックスの
    // 隣に置くとバッジだけ背が低く、行の上端に張り付いて浮いて見えていた
    // （メニューバーの再生状態バッジがまさにこれ）。
    // 幅の余白も style.FramePadding.x に合わせ、隣のボタンと同じ間合いにする。
    const ImVec2 textSize = ImGui::CalcTextSize(text);
    const float padX = ImGui::GetStyle().FramePadding.x;
    const float height = ImGui::GetFrameHeight();
    const ImVec2 badgeSize = {textSize.x + padX * 2.0f, height};
    const float padY = (height - textSize.y) * 0.5f;

    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const ImVec2 bottomRight = {pos.x + badgeSize.x, pos.y + badgeSize.y};

    ImDrawList *dl = ImGui::GetWindowDrawList();
    ImVec4 bg = color;
    bg.w = 0.20f;
    dl->AddRectFilled(pos, bottomRight, ImGui::ColorConvertFloat4ToU32(bg), ImGui::GetStyle().FrameRounding);
    dl->AddRect(pos, bottomRight, ImGui::ColorConvertFloat4ToU32(color), ImGui::GetStyle().FrameRounding, 0, 1.0f);
    dl->AddText({pos.x + padX, pos.y + padY}, ImGui::ColorConvertFloat4ToU32(color), text);

    // カーソルを手で動かさず、バッジの大きさをレイアウトへ申告する。
    // こうすると SameLine や折り返しが他のウィジェットと同じように効く。
    ImGui::Dummy(badgeSize);
}

// ------------------------------------------------------------
// 目立たないリセットボタン
// ------------------------------------------------------------
static bool SmallResetButton(const char *id)
{
    ImGui::PushStyleColor(ImGuiCol_Button, {0.25f, 0.25f, 0.30f, 1.0f});
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, {0.35f, 0.35f, 0.45f, 1.0f});
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, {0.45f, 0.45f, 0.55f, 1.0f});
    bool hit = ImGui::SmallButton(id);
    ImGui::PopStyleColor(3);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Reset");
    return hit;
}

// ------------------------------------------------------------
// ラベルを上に置いてから全幅 DragFloat3 を描く
//   label       : 表示テキスト (ASCII)
//   id          : ImGui ID (## から始める)
//   frameBgColor: フレーム背景色
// ------------------------------------------------------------
static bool LabeledDrag3(const char *label, const char *id,
                         float *v, float speed,
                         float vmin, float vmax,
                         const char *fmt,
                         ImVec4 frameBgColor)
{
    ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
    ImGui::TextUnformatted(label);
    ImGui::PopStyleColor();
    ImGui::SetNextItemWidth(-1);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, frameBgColor);
    bool changed = ImGui::DragFloat3(id, v, speed, vmin, vmax, fmt);
    ImGui::PopStyleColor();
    return changed;
}

// ------------------------------------------------------------
// ラベルを上に置いてから全幅 SliderFloat を描く
// ------------------------------------------------------------
static bool LabeledSlider(const char *label, const char *id,
                          float *v, float vmin, float vmax,
                          const char *fmt,
                          ImVec4 accentColor)
{
    ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
    ImGui::TextUnformatted(label);
    ImGui::PopStyleColor();
    ImGui::SetNextItemWidth(-1);
    ImGui::PushStyleColor(ImGuiCol_SliderGrab, accentColor);
    ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, accentColor);
    bool changed = ImGui::SliderFloat(id, v, vmin, vmax, fmt);
    ImGui::PopStyleColor(2);
    return changed;
}

// ------------------------------------------------------------
// テーマ配色の回転ノブ（imgui-knobs / WiperOnly）
//   label  : ノブ見出し兼 ID（## で表示名と ID を分離可）
//   v      : 対象の値
//   vmin/vmax: 範囲
//   fmt    : 値の書式
//   accent : インジケータ（ワイパー）色
//   size   : ノブ直径 px（0 = 既定の 4 行ぶん）
//   flags  : 追加フラグ（NoTitle / NoInput 等）
// 戻り値: 値が変化したら true
// ------------------------------------------------------------
static bool ThemedKnob(const char *label, float *v,
                       float vmin, float vmax,
                       const char *fmt, ImVec4 accent,
                       float size = 0.0f,
                       ImGuiKnobFlags flags = ImGuiKnobFlags_ValueTooltip)
{
    const ImVec4 track = {accent.x, accent.y, accent.z, 0.22f};
    const ImVec4 active = {accent.x, accent.y, accent.z, 1.0f};
    ImGui::PushStyleColor(ImGuiCol_Button, track);         // ワイパー軌道（地）
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, accent); // インジケータ（ホバー）
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, active);  // インジケータ（操作中）
    bool changed = ImGuiKnobs::Knob(label, v, vmin, vmax, 0.0f, fmt,
                                    ImGuiKnobVariant_WiperOnly, size, flags);
    ImGui::PopStyleColor(3);
    return changed;
}

// ------------------------------------------------------------
// 役割で選ぶボタン
//   色を各画面で決めずここに集約する。テーマを変えるときはこの4組だけ触ればよい。
// ------------------------------------------------------------

/// <summary>地色とホバー色を指定してボタンを描く（役割ボタンの実装本体）</summary>
/// <param name="label">ラベル（## でID付与可）</param>
/// <param name="base">地色</param>
/// <param name="hover">ホバー色。押下時もこの色を使う</param>
/// <param name="size">大きさ。x&lt;0 で全幅、{0,0} で内容に合わせる</param>
/// <returns>bool: 押されたら true</returns>
inline bool RoleButton(const char *label, ImVec4 base, ImVec4 hover, const ImVec2 &size = {0.0f, 0.0f})
{
    ImGui::PushStyleColor(ImGuiCol_Button, base);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hover);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, hover);
    const bool hit = ImGui::Button(label, size);
    ImGui::PopStyleColor(3);
    return hit;
}

/// <summary>主操作のボタン（適用・実行など）</summary>
inline bool PrimaryButton(const char *label, const ImVec2 &size = {0.0f, 0.0f})
{
    return RoleButton(label, DebugTheme::kButtonPrimary, DebugTheme::kButtonPrimaryHover, size);
}

/// <summary>追加・確定のボタン（新規作成・保存など）</summary>
inline bool ConfirmButton(const char *label, const ImVec2 &size = {0.0f, 0.0f})
{
    return RoleButton(label, DebugTheme::kButtonConfirm, DebugTheme::kButtonConfirmHover, size);
}

/// <summary>破壊的操作のボタン（削除・全消しなど）</summary>
inline bool DangerButton(const char *label, const ImVec2 &size = {0.0f, 0.0f})
{
    return RoleButton(label, DebugTheme::kButtonDanger, DebugTheme::kButtonDangerHover, size);
}

/// <summary>目立たせたくない操作のボタン（キャンセル・リセットなど）</summary>
inline bool NeutralButton(const char *label, const ImVec2 &size = {0.0f, 0.0f})
{
    return RoleButton(label, DebugTheme::kButtonNeutral, DebugTheme::kButtonNeutralHover, size);
}

/// <summary>
/// 役割色をボタン系ウィジェットへ適用するスコープ。
/// SmallButton や ArrowButton など、上のヘルパーで包めないものに使う。
/// </summary>
struct ScopedButtonColors
{
    ScopedButtonColors(ImVec4 base, ImVec4 hover)
    {
        ImGui::PushStyleColor(ImGuiCol_Button, base);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hover);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, hover);
    }
    ~ScopedButtonColors() { ImGui::PopStyleColor(3); }
    ScopedButtonColors(const ScopedButtonColors &) = delete;
    ScopedButtonColors &operator=(const ScopedButtonColors &) = delete;
};

/// <summary>アクセント色から地・ホバーを自動で作るボタン（分類色に合わせたいとき用）</summary>
/// <param name="label">ラベル</param>
/// <param name="accent">基準となるアクセント色（DebugTheme::kAccentXxx）</param>
/// <param name="width">横幅。&lt;0 で全幅</param>
inline bool AccentButton(const char *label, ImVec4 accent, float width = -1.0f)
{
    const ImVec4 base = {accent.x * 0.55f, accent.y * 0.55f, accent.z * 0.55f, 0.85f};
    const ImVec4 hover = {accent.x * 0.72f, accent.y * 0.72f, accent.z * 0.72f, 0.95f};
    return RoleButton(label, base, hover, ImVec2(width, 0.0f));
}

// ------------------------------------------------------------
// テーマ配色のウィジェット
// ------------------------------------------------------------

/// <summary>アクセント色から3状態の色を作り、折りたたみヘッダーを描く</summary>
/// <param name="label">ヘッダーラベル（## でID付与可）</param>
/// <param name="accent">基準となるアクセント色</param>
/// <param name="defaultOpen">初期状態で開くか</param>
/// <returns>bool: 開いていれば true</returns>
inline bool ThemedHeader(const char *label, ImVec4 accent, bool defaultOpen = false)
{
    ImVec4 base = accent;
    base.w = 0.22f;
    ImVec4 hov = accent;
    hov.w = 0.38f;
    ImVec4 act = accent;
    act.w = 0.50f;
    ImGui::PushStyleColor(ImGuiCol_Header, base);
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, hov);
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, act);
    const bool open = ImGui::CollapsingHeader(label, defaultOpen ? ImGuiTreeNodeFlags_DefaultOpen : 0);
    ImGui::PopStyleColor(3);
    return open;
}

/// <summary>チェックマーク色だけアクセント色にしたチェックボックス</summary>
inline bool AccentCheckbox(const char *label, bool *v, ImVec4 accent)
{
    ImGui::PushStyleColor(ImGuiCol_CheckMark, accent);
    const bool changed = ImGui::Checkbox(label, v);
    ImGui::PopStyleColor();
    return changed;
}

// ------------------------------------------------------------
// トグルスイッチ（imgui_toggle）
//
// 使い分け:
//   ThemedToggle  … 「その機能が生きているか」を表す入切スイッチ
//                    （描画エントリのON/OFF、コライダーの有効/表示、影の有効 など）
//   AccentCheckbox… 複数から選ぶ・条件を積むタイプの選択肢
//
// 幅はチェックボックスより広いので、行に並べるときは InlineColumns と併用する。
// ------------------------------------------------------------

/// <summary>テーマ配色のトグルスイッチ</summary>
/// <param name="label">ラベル（## でID付与可）</param>
/// <param name="v">対象の値</param>
/// <param name="accent">ON のときの地色（DebugTheme::kAccentXxx）</param>
/// <returns>bool: 値が変化したら true</returns>
inline bool ThemedToggle(const char *label, bool *v, ImVec4 accent = DebugTheme::kAccentBlue)
{
    const ImGuiStyle &style = ImGui::GetStyle();

    // ON/OFF それぞれの配色。ImGui のテーマ色を土台にしてアクセントだけ乗せる
    ImGuiTogglePalette onPalette{};
    onPalette.Frame = {accent.x * 0.62f, accent.y * 0.62f, accent.z * 0.62f, 1.0f};
    onPalette.FrameHover = accent;
    onPalette.Knob = {0.95f, 0.96f, 0.97f, 1.0f};
    onPalette.KnobHover = {1.0f, 1.0f, 1.0f, 1.0f};

    ImGuiTogglePalette offPalette{};
    offPalette.Frame = style.Colors[ImGuiCol_FrameBg];
    offPalette.FrameHover = style.Colors[ImGuiCol_FrameBgHovered];
    offPalette.Knob = {0.45f, 0.46f, 0.50f, 1.0f};
    offPalette.KnobHover = {0.58f, 0.59f, 0.63f, 1.0f};

    ImGuiToggleConfig config;
    config.Flags = ImGuiToggleFlags_Animated;
    config.AnimationDuration = 0.12f; // きびきび動く程度。長いと操作が重く感じる
    config.WidthRatio = 1.70f;
    config.On.Palette = &onPalette;
    config.Off.Palette = &offPalette;

    return ImGui::Toggle(label, v, config);
}

/// <summary>
/// ラベルを左、トグルを右端に置く行。設定項目が縦に並ぶ場所で使うと右端が揃う。
/// </summary>
/// <param name="label">左に出す説明</param>
/// <param name="id">トグルのID（## から始める）</param>
/// <param name="v">対象の値</param>
/// <param name="accent">ON のときの地色</param>
/// <returns>bool: 値が変化したら true</returns>
inline bool ToggleRow(const char *label, const char *id, bool *v, ImVec4 accent = DebugTheme::kAccentBlue)
{
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);

    // トグルの幅は WidthRatio × 行の高さ。右端にそろえるので実寸で下がる
    const float toggleWidth = ImGui::GetFrameHeight() * 1.70f;
    ImGui::SameLine(ImGui::GetContentRegionMax().x - toggleWidth);
    return ThemedToggle(id, v, accent);
}

// ------------------------------------------------------------
// テーマ配色のテキスト
// ------------------------------------------------------------

/// <summary>セクション内の小見出しを描く</summary>
inline void CaptionText(const char *text)
{
    ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextCaption);
    ImGui::TextUnformatted(text);
    ImGui::PopStyleColor();
}

/// <summary>補足説明など、控えめに見せたい文字を描く</summary>
inline void DimText(const char *text)
{
    ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
    ImGui::TextUnformatted(text);
    ImGui::PopStyleColor();
}
} // namespace Hagine
#endif // USE_IMGUI
