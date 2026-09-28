#include "EditorAppearance.h"
#ifdef USE_IMGUI
#include "DebugUIHelper.h"
#include <algorithm>
#include <cmath>
#include <data/DataHandler.h>
#include <icon/IconsFontAwesome5.h>
#include <imgui.h>
#include <memory>

namespace Hagine {

namespace {

/// <summary>テーマ（地の色）の作り方</summary>
struct ThemePreset
{
    const char *name;
    const char *description;
    float lift;    // 暗い色ほど持ち上げる量（0で SetupTheme のまま）
    float tintHue; // 無彩色に乗せる色相 [0,1]
    float tintSat; // 無彩色に乗せる彩度（暗い色ほど強く乗る）
};

const ThemePreset kThemes[EditorAppearance::kThemeCount] = {
    {"ニアブラック", "既定。ほぼ黒の地で、ゲーム画面の色を邪魔しない", 0.0f, 0.0f, 0.0f},
    {"スレート", "一段明るい灰色。長時間の作業で目が疲れにくい", 0.085f, 0.60f, 0.05f},
    {"ミッドナイト", "紺色の地。夜のシーンや青系の演出を作るときに馴染む", 0.05f, 0.64f, 0.40f},
    {"ウォーム", "茶色がかった灰色。暖色の演出を作るときに色を見誤りにくい", 0.07f, 0.07f, 0.14f},
};

/// <summary>アクセント色（HSV）。0番は SetupTheme のスチールブルーそのもの</summary>
struct AccentPreset
{
    const char *name;
    float h;
    float s;
    float v;
};

const AccentPreset kAccents[EditorAppearance::kAccentCount] = {
    {"スチールブルー", 0.0f, 0.0f, 0.0f}, // 既定（変換しない）
    {"ティール", 0.48f, 0.45f, 0.70f},
    {"バイオレット", 0.73f, 0.38f, 0.78f},
    {"アンバー", 0.09f, 0.55f, 0.85f},
    {"ローズ", 0.96f, 0.42f, 0.82f},
    {"グリーン", 0.36f, 0.40f, 0.72f},
    {"カスタム", 0.0f, 0.0f, 0.0f}, // customAccent から決める
};

// SetupTheme のアクセント色（淡いスチールブルー）。これに近い色相の色を「アクセント」とみなして回す
const ImVec4 kDefaultAccent = ImVec4(0.435f, 0.541f, 0.659f, 1.0f);
// これより彩度の低い色は「無彩色の段階」として扱う
constexpr float kNeutralSaturation = 0.2f;
// 既定アクセントの色相からこの範囲の色をアクセントとみなす
constexpr float kAccentHueRange = 0.08f;

constexpr float kMinUiScale = 0.8f;
constexpr float kMaxUiScale = 1.6f;

/// <summary>
/// 1色を設定どおりに変換する
/// </summary>
ImVec4 TransformColor(const ImVec4 &color, const ThemePreset &theme, bool changeAccent, float accentH, float accentS, float accentV)
{
    float defaultH = 0.0f;
    float defaultS = 0.0f;
    float defaultV = 0.0f;
    ImGui::ColorConvertRGBtoHSV(kDefaultAccent.x, kDefaultAccent.y, kDefaultAccent.z, defaultH, defaultS, defaultV);

    float h = 0.0f;
    float s = 0.0f;
    float v = 0.0f;
    ImGui::ColorConvertRGBtoHSV(color.x, color.y, color.z, h, s, v);

    const bool isAccent = (s >= kNeutralSaturation) && (std::fabs(h - defaultH) < kAccentHueRange);
    if (isAccent)
    {
        if (!changeAccent)
        {
            return color;
        }
        // 明るさ・鮮やかさの段階（通常／ホバー／押下）は元の比率のまま保つ
        h = accentH;
        s = std::clamp(s * accentS / std::max(defaultS, 0.01f), 0.0f, 1.0f);
        v = std::clamp(v * accentV / std::max(defaultV, 0.01f), 0.0f, 1.0f);
    }
    else if (s < kNeutralSaturation)
    {
        // 暗い段階ほど持ち上げ、明るい文字色はほぼそのまま（段階の差を潰さない）
        const float dark = std::pow(1.0f - v, 3.0f);
        v = std::clamp(v + theme.lift * dark, 0.0f, 1.0f);
        if (theme.tintSat > 0.0f)
        {
            h = theme.tintHue;
            s = std::clamp(s * 0.4f + theme.tintSat * std::pow(1.0f - v, 1.5f), 0.0f, 1.0f);
        }
    }
    else
    {
        // 黄色の未保存マーカーなど、意味のある色はそのまま
        return color;
    }

    ImVec4 result = color;
    ImGui::ColorConvertHSVtoRGB(h, s, v, result.x, result.y, result.z);
    return result;
}

/// <summary>アクセント色（HSV）を設定から決める</summary>
bool ResolveAccent(const EditorAppearance::Settings &settings, float &h, float &s, float &v)
{
    if (settings.accent <= 0 || settings.accent >= EditorAppearance::kAccentCount)
    {
        return false;
    }
    if (settings.accent == EditorAppearance::kAccentCount - 1)
    {
        ImGui::ColorConvertRGBtoHSV(settings.customAccent.x, settings.customAccent.y, settings.customAccent.z, h, s, v);
        // 彩度0のまま回すと全部灰色になるので、最低限の色味は残す
        s = std::max(s, 0.05f);
        v = std::max(v, 0.2f);
        return true;
    }
    h = kAccents[settings.accent].h;
    s = kAccents[settings.accent].s;
    v = kAccents[settings.accent].v;
    return true;
}

/// <summary>アクセント色の見本（RGB）</summary>
ImVec4 AccentSwatch(const EditorAppearance::Settings &settings, int accent)
{
    if (accent == 0)
    {
        return kDefaultAccent;
    }
    if (accent == EditorAppearance::kAccentCount - 1)
    {
        return ImVec4(settings.customAccent.x, settings.customAccent.y, settings.customAccent.z, 1.0f);
    }
    ImVec4 color(0.0f, 0.0f, 0.0f, 1.0f);
    ImGui::ColorConvertHSVtoRGB(kAccents[accent].h, kAccents[accent].s, kAccents[accent].v, color.x, color.y, color.z);
    return color;
}

} // namespace

const char *EditorAppearance::GetThemeName(int theme)
{
    return kThemes[std::clamp(theme, 0, kThemeCount - 1)].name;
}

const char *EditorAppearance::GetAccentName(int accent)
{
    return kAccents[std::clamp(accent, 0, kAccentCount - 1)].name;
}

void EditorAppearance::SetTheme(int theme)
{
    settings_.theme = std::clamp(theme, 0, kThemeCount - 1);
    Save();
}

void EditorAppearance::SetAccent(int accent)
{
    settings_.accent = std::clamp(accent, 0, kAccentCount - 1);
    Save();
}

void EditorAppearance::Load()
{
    auto data = std::make_unique<DataHandler>("ImGuiSetting", "Appearance");
    settings_.theme = std::clamp(data->Load("theme", 0), 0, kThemeCount - 1);
    settings_.accent = std::clamp(data->Load("accent", 0), 0, kAccentCount - 1);
    settings_.customAccent = data->Load<Vector4>("customAccent", settings_.customAccent);
    settings_.uiScale = std::clamp(data->Load("uiScale", 1.0f), kMinUiScale, kMaxUiScale);
    settings_.rounding = std::clamp(data->Load("rounding", 1.0f), 0.0f, 2.5f);
}

void EditorAppearance::Save() const
{
    auto data = std::make_unique<DataHandler>("ImGuiSetting", "Appearance");
    data->Save("theme", settings_.theme);
    data->Save("accent", settings_.accent);
    data->Save<Vector4>("customAccent", settings_.customAccent);
    data->Save("uiScale", settings_.uiScale);
    data->Save("rounding", settings_.rounding);
}

void EditorAppearance::ApplyTo(ImGuiStyle &style) const
{
    const ThemePreset &theme = kThemes[std::clamp(settings_.theme, 0, kThemeCount - 1)];
    float accentH = 0.0f;
    float accentS = 0.0f;
    float accentV = 0.0f;
    const bool changeAccent = ResolveAccent(settings_, accentH, accentS, accentV);

    if (settings_.theme != 0 || changeAccent)
    {
        for (int i = 0; i < ImGuiCol_COUNT; ++i)
        {
            style.Colors[i] = TransformColor(style.Colors[i], theme, changeAccent, accentH, accentS, accentV);
        }
    }

    // 角の丸み
    style.WindowRounding *= settings_.rounding;
    style.ChildRounding *= settings_.rounding;
    style.FrameRounding *= settings_.rounding;
    style.PopupRounding *= settings_.rounding;
    style.ScrollbarRounding *= settings_.rounding;
    style.GrabRounding *= settings_.rounding;
    style.TabRounding *= settings_.rounding;

    // 大きさ（余白と文字をまとめて）
    if (settings_.uiScale != 1.0f)
    {
        style.ScaleAllSizes(settings_.uiScale);
    }
    style.FontScaleMain = settings_.uiScale;
}

bool EditorAppearance::DrawWindow(bool *open)
{
    bool changed = false;
    ImGui::SetNextWindowSize(ImVec2(460.0f, 0.0f), ImGuiCond_FirstUseEver);
    // 初めて出すときはメイン画面の中央に置く（マルチビューポートで別のOSウィンドウに飛ばない）。
    // 以降は自由に動かせる
    static bool placed = false;
    if (!placed)
    {
        const ImGuiViewport *viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowViewport(viewport->ID);
        ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + viewport->WorkSize.x * 0.5f, viewport->WorkPos.y + viewport->WorkSize.y * 0.35f),
                                ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        placed = true;
    }
    if (!ImGui::Begin(ICON_FA_PALETTE " 外観", open))
    {
        ImGui::End();
        return false;
    }

    const ImGuiStyle &style = ImGui::GetStyle();
    ImDrawList *drawList = ImGui::GetWindowDrawList();

    // ---- テーマ（地の色）: 実際の変換を通した見本のカードを並べる ----
    ImGui::SeparatorText("テーマ（地の色）");
    const float cardWidth = (ImGui::GetContentRegionAvail().x - style.ItemSpacing.x * (kThemeCount - 1)) / kThemeCount;
    const float cardHeight = 70.0f * settings_.uiScale;
    for (int i = 0; i < kThemeCount; ++i)
    {
        if (i > 0)
        {
            ImGui::SameLine();
        }
        ImGui::PushID(i);
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        if (ImGui::InvisibleButton("##theme", ImVec2(cardWidth, cardHeight)))
        {
            settings_.theme = i;
            changed = true;
        }
        const bool hovered = ImGui::IsItemHovered();
        if (hovered)
        {
            ImGui::SetTooltip("%s", kThemes[i].description);
        }

        // 見本は今のアクセント設定で作る（組み合わせの見え方が分かるように）
        Settings preview = settings_;
        preview.theme = i;
        float ah = 0.0f;
        float as = 0.0f;
        float av = 0.0f;
        const bool changeAccent = ResolveAccent(preview, ah, as, av);
        auto sample = [&](const ImVec4 &base) { return ImGui::GetColorU32(TransformColor(base, kThemes[i], changeAccent, ah, as, av)); };
        const ImVec2 end(pos.x + cardWidth, pos.y + cardHeight);
        drawList->AddRectFilled(pos, end, sample(ImVec4(0.047f, 0.047f, 0.055f, 1.0f)), 6.0f);                                             // 窓の地
        drawList->AddRectFilled(ImVec2(pos.x, pos.y), ImVec2(end.x, pos.y + 12.0f), sample(ImVec4(0.086f, 0.098f, 0.118f, 1.0f)), 6.0f, ImDrawFlags_RoundCornersTop); // タイトル
        drawList->AddRectFilled(ImVec2(pos.x + 8.0f, pos.y + 20.0f), ImVec2(end.x - 8.0f, pos.y + 32.0f), sample(ImVec4(0.114f, 0.118f, 0.137f, 1.0f)), 3.0f);    // 入力欄
        drawList->AddRectFilled(ImVec2(pos.x + 8.0f, pos.y + 20.0f), ImVec2(pos.x + 8.0f + (cardWidth - 16.0f) * 0.55f, pos.y + 32.0f),
                                sample(kDefaultAccent), 3.0f); // スライダー
        drawList->AddText(ImVec2(pos.x + 8.0f, end.y - ImGui::GetTextLineHeight() - 6.0f), sample(ImVec4(0.871f, 0.878f, 0.894f, 1.0f)), kThemes[i].name);
        const bool selected = (settings_.theme == i);
        drawList->AddRect(pos, end,
                          selected ? ImGui::GetColorU32(ImGuiCol_CheckMark) : ImGui::GetColorU32(hovered ? ImGuiCol_Text : ImGuiCol_Border),
                          6.0f, 0, selected ? 2.5f : 1.0f);
        ImGui::PopID();
    }

    // ---- アクセント色 ----
    ImGui::SeparatorText("アクセント色");
    for (int i = 0; i < kAccentCount; ++i)
    {
        if (i > 0)
        {
            ImGui::SameLine();
        }
        ImGui::PushID(100 + i);
        const ImVec4 swatch = AccentSwatch(settings_, i);
        const bool selected = (settings_.accent == i);
        const float size = ImGui::GetFrameHeight() * 1.2f;
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        if (ImGui::InvisibleButton("##accent", ImVec2(size, size)))
        {
            settings_.accent = i;
            changed = true;
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("%s", kAccents[i].name);
        }
        const ImVec2 center(pos.x + size * 0.5f, pos.y + size * 0.5f);
        drawList->AddCircleFilled(center, size * 0.38f, ImGui::GetColorU32(swatch));
        if (i == kAccentCount - 1)
        {
            // カスタムは「+」印で区別する
            const ImVec2 textSize = ImGui::CalcTextSize(ICON_FA_EYE_DROPPER);
            drawList->AddText(ImVec2(center.x - textSize.x * 0.5f, center.y - textSize.y * 0.5f), IM_COL32(20, 20, 24, 220), ICON_FA_EYE_DROPPER);
        }
        if (selected)
        {
            drawList->AddCircle(center, size * 0.5f - 1.0f, ImGui::GetColorU32(ImGuiCol_Text), 0, 2.0f);
        }
        ImGui::PopID();
    }
    if (settings_.accent == kAccentCount - 1)
    {
        if (ImGui::ColorEdit3("カスタムの色", &settings_.customAccent.x, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_PickerHueWheel))
        {
            changed = true;
        }
    }

    // ---- 大きさ・丸み ----
    ImGui::SeparatorText("大きさ");
    // 大きさは離したときに反映する（ドラッグ中に窓ごと大きさが変わると、つまみが指から逃げる）
    static float pendingScale = -1.0f;
    if (pendingScale < 0.0f)
    {
        pendingScale = settings_.uiScale;
    }
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.45f);
    ImGui::SliderFloat("UIの大きさ", &pendingScale, kMinUiScale, kMaxUiScale, "%.2f 倍");
    if (ImGui::IsItemDeactivatedAfterEdit())
    {
        settings_.uiScale = pendingScale;
        changed = true;
    }
    // よく使う倍率はボタンで一発（スライダーの下の行に並べる。横に並べると狭い窓ではみ出す）
    static const float kScalePresets[] = {0.9f, 1.0f, 1.15f, 1.3f, 1.5f};
    for (int i = 0; i < IM_ARRAYSIZE(kScalePresets); ++i)
    {
        const float preset = kScalePresets[i];
        if (i > 0)
        {
            ImGui::SameLine();
        }
        char label[16];
        snprintf(label, sizeof(label), "%d%%", static_cast<int>(preset * 100.0f + 0.5f));
        if (ImGui::SmallButton(label))
        {
            settings_.uiScale = preset;
            pendingScale = preset;
            changed = true;
        }
    }

    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.45f);
    if (ImGui::SliderFloat("角の丸み", &settings_.rounding, 0.0f, 2.5f, "%.2f 倍"))
    {
        changed = true;
    }

    ImGui::Spacing();
    ImGui::Separator();
    if (NeutralButton(ICON_FA_UNDO " 既定に戻す"))
    {
        settings_ = Settings{};
        pendingScale = settings_.uiScale;
        changed = true;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("設定は自動で保存され、次回起動時も使われます");

    ImGui::End();

    if (changed)
    {
        Save();
    }
    return changed;
}

} // namespace Hagine
#endif // USE_IMGUI
