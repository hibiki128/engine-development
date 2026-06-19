#pragma once
// ============================================================
//  DebugUI_Improved.h  (revision 2)
//  ImGui 1.92.4 + ImGuizmo + ImPlot
//  * ASCII only  -- no environment-dependent characters
// ============================================================
#ifdef _DEBUG


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
} // namespace DebugTheme

// ------------------------------------------------------------
// 左サイドバー付きセクション見出し (ASCII ラベルのみ)
// ------------------------------------------------------------
static void SectionHeader(const char *label, ImVec4 color) {
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
static void ReadOnlyRow(const char *label, const char *fmt, ...) {
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
static void StatusBadge(const char *text, ImVec4 color) {
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImVec2 ts = ImGui::CalcTextSize(text);
    float pad = 4.0f;
    ImVec2 br = {p.x + ts.x + pad * 2.f, p.y + ts.y + pad};
    ImDrawList *dl = ImGui::GetWindowDrawList();
    ImVec4 bg = color;
    bg.w = 0.20f;
    dl->AddRectFilled(p, br, ImGui::ColorConvertFloat4ToU32(bg), 3.0f);
    dl->AddRect(p, br, ImGui::ColorConvertFloat4ToU32(color), 3.0f, 0, 1.0f);
    ImGui::SetCursorScreenPos({p.x + pad, p.y + pad * 0.5f});
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    ImGui::TextUnformatted(text);
    ImGui::PopStyleColor();
    ImGui::SetCursorScreenPos({p.x, br.y + 2.0f});
}

// ------------------------------------------------------------
// 目立たないリセットボタン
// ------------------------------------------------------------
static bool SmallResetButton(const char *id) {
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
                         ImVec4 frameBgColor) {
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
                          ImVec4 accentColor) {
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
} // namespace Hagine
#endif // _DEBUG
