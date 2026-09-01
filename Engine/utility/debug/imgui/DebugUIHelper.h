#pragma once
// ============================================================
//  DebugUIHelper.h  (revision 2)
//  ImGui 1.92.4 + ImGuizmo + ImPlot
//  * ASCII only  -- no environment-dependent characters
// ============================================================
#ifdef USE_IMGUI

// 回転ノブ（imgui-knobs）。imgui.h を使うため、本ヘッダは imgui.h の後に include する前提。
#include <imgui-knobs.h>

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
// 左サイドバー付きセクション見出し (ASCII ラベルのみ)
// ------------------------------------------------------------
static void SectionHeader(const char *label, ImVec4 color)
{
    ImVec2 p = ImGui::GetCursorScreenPos();
    float lh = ImGui::GetTextLineHeightWithSpacing();
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
    ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
    ImGui::Text("%-12s", label);
    ImGui::PopStyleColor();
    ImGui::SameLine(120.0f);
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
    // 以前は枠の高さを「文字高 + 4px」しか取らず、さらに ImGui::TextUnformatted で
    // 文字を描いていたため、行のベースライン調整が入ると文字が枠から下へはみ出していた。
    // ここでは上下に同じだけ余白を取り、文字は描画リストへ直接置いてズレを無くしている。
    const ImVec2 textSize = ImGui::CalcTextSize(text);
    const float padX = 6.0f;
    const float padY = 3.0f;
    const ImVec2 badgeSize = {textSize.x + padX * 2.0f, textSize.y + padY * 2.0f};

    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const ImVec2 bottomRight = {pos.x + badgeSize.x, pos.y + badgeSize.y};

    ImDrawList *dl = ImGui::GetWindowDrawList();
    ImVec4 bg = color;
    bg.w = 0.20f;
    dl->AddRectFilled(pos, bottomRight, ImGui::ColorConvertFloat4ToU32(bg), 3.0f);
    dl->AddRect(pos, bottomRight, ImGui::ColorConvertFloat4ToU32(color), 3.0f, 0, 1.0f);
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
