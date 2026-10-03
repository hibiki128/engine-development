#include "LineRenderer.h"
#ifdef USE_IMGUI
#include <algorithm>
#include <format>
#include <string>
#include <string_view>
#include <icon/IconsFontAwesome5.h>
#include <imgui.h>
// DebugUIHelper.h は ImVec4 / ImGui:: を使うので imgui.h の後に include する
#include "utility/debug/imgui/DebugUIHelper.h"

// =======================================================================
// LineRenderer: 「デバッグ線」窓（種類ごとの本数とオン/オフ）
//
// 線を出す所が増えて、どこで出している線なのか・どこで消すのかが分からなくなっていたので、
// 線を積む側が LineCategoryScope で種類を名乗り、ここで一覧にして切り替えられるようにした。
// =======================================================================

namespace Hagine {
namespace {
/// <summary>種類の表示情報（LineCategory と同じ並び。添字がそのまま LineCategory の値）</summary>
struct CategoryInfo
{
    const char *icon;
    const char *label;
    const char *group;
    const char *tip;
};

constexpr CategoryInfo kCategoryInfos[] = {
    {ICON_FA_ELLIPSIS_H, "その他", "その他", "種類を名乗っていない線（ゲーム側のデバッグ描画など）"},
    {ICON_FA_BORDER_ALL, "床のグリッド", "エディタ", "シーンの床のマス目（表示メニュー →「グリッド設定」で大きさ・色）"},
    {ICON_FA_VECTOR_SQUARE, "選択中の物の枠", "エディタ", "シーンで選んでいる物の枠と、マウスを乗せた物の枠"},
    {ICON_FA_CUBE, "ギズモの補助表示", "エディタ", "トランスフォームマネージャの「デバッグ表示」（AABB・外接球・レイ）"},
    {ICON_FA_TH, "スナップの刻み", "エディタ", "移動スナップ中に、掴んでいる間だけ足元に出るマス目"},
    {ICON_FA_SHAPES, "コライダー", "当たり判定・ライト・描画", "当たり判定の形（コライダー窓で1つずつの表示・色も変えられる）"},
    {ICON_FA_LIGHTBULB, "ライト", "当たり判定・ライト・描画", "ライトの向き・届く範囲（ライト窓の「可視化」）"},
    {ICON_FA_EYE, "アイコンで選んだ物の範囲", "エディタ", "シーンのアイコンで選んだライト・カメラなどの範囲"},
    {ICON_FA_TH_LARGE, "配置ツールの下描き", "エディタ", "配置ツールの窓を開いている間の、置く場所の印"},
    {ICON_FA_DRAW_POLYGON, "ワイヤーフレーム", "モデル・アニメーション", "オブジェクトのワイヤーフレーム表示"},
    {ICON_FA_BONE, "骨（スケルトン）", "モデル・アニメーション", "アニメーションするモデルの骨の表示"},
    {ICON_FA_HAND_PAPER, "IK・揺れ物", "モデル・アニメーション", "足のIK・手のIK・揺れ物の確認用の線（インスペクタの物理タブで個別に出す）"},
    {ICON_FA_STAR, "パーティクル", "パーティクル", "エミッターの形・フィールド・集まる点や渦の中心"},
    {ICON_FA_ROUTE, "モーションの経路", "モデル・アニメーション", "モーションエディターの制御点と曲線"},
    {ICON_FA_FILTER, "錐台カリングの確認", "当たり判定・ライト・描画", "メインカメラの視界と、描画から省いた物の箱（統計窓の「錐台カリング」）"},
};
// 表の行数が種類の数と合っていないとコンパイルで止める（並びは LineCategory と同じにすること）
static_assert(sizeof(kCategoryInfos) / sizeof(kCategoryInfos[0]) == kLineCategoryCount);

constexpr const char *kGroupOrder[] = {"エディタ", "当たり判定・ライト・描画", "モデル・アニメーション", "パーティクル", "その他"};
} // namespace

void LineRenderer::DrawCategoryImGui(bool compact)
{
    // ---- 全体 ----
    uint32_t total = 0;
    for (int i = 0; i < kLineCategoryCount; ++i)
    {
        total += lastCategoryLines_[i];
    }

    bool all = allLinesEnabled_;
    if (ThemedToggle("デバッグ線を表示##allLines", &all, DebugTheme::kAccentGreen))
    {
        SetAllLinesEnabled(all);
    }
    ImGui::SetItemTooltip("切るとシーンのデバッグ線を全部隠します（下の種類ごとの設定は残ります）");
    ImGui::SameLine();
    ImGui::TextDisabled("%u 本", total);

    if (NeutralButton("全部オン##lineAllOn"))
    {
        SetAllLinesEnabled(true);
        for (int i = 0; i < kLineCategoryCount; ++i)
        {
            SetCategoryEnabled(static_cast<LineCategory>(i), true);
            if (categoryFlags_[i])
            {
                *categoryFlags_[i] = true;
            }
        }
    }
    ImGui::SetItemTooltip("全部の種類を出します（グリッドやコライダーなど、元の窓で切っていた物も入れます）");
    ImGui::SameLine();
    if (NeutralButton("全部オフ##lineAllOff"))
    {
        for (int i = 0; i < kLineCategoryCount; ++i)
        {
            SetCategoryEnabled(static_cast<LineCategory>(i), false);
        }
    }
    ImGui::SetItemTooltip("全部の種類を隠します（元の窓の設定はそのまま）");
    if (!compact)
    {
        ImGui::SameLine();
        DimText("右クリック: この種類だけ出す");
    }

    ImGui::BeginDisabled(!allLinesEnabled_);
    for (const char *group : kGroupOrder)
    {
        ImGui::SeparatorText(group);
        for (int i = 0; i < kLineCategoryCount; ++i)
        {
            const CategoryInfo &info = kCategoryInfos[i];
            if (std::string_view(info.group) != group)
            {
                continue;
            }
            ImGui::PushID(i);
            const LineCategory category = static_cast<LineCategory>(i);
            bool *flag = categoryFlags_[i];

            // チェックは「種類のスイッチ && 持ち主のスイッチ」。入れるときは両方入れる
            bool shown = categoryEnabled_[i] && (!flag || *flag);
            if (ImGui::Checkbox("##shown", &shown))
            {
                SetCategoryEnabled(category, shown);
                if (shown && flag)
                {
                    *flag = true;
                }
            }
            if (ImGui::BeginPopupContextItem("##lineCategoryContext"))
            {
                if (ImGui::MenuItem("この種類だけ出す"))
                {
                    for (int other = 0; other < kLineCategoryCount; ++other)
                    {
                        SetCategoryEnabled(static_cast<LineCategory>(other), other == i);
                    }
                    if (flag)
                    {
                        *flag = true;
                    }
                }
                if (ImGui::MenuItem("全部の種類を出す"))
                {
                    for (int other = 0; other < kLineCategoryCount; ++other)
                    {
                        SetCategoryEnabled(static_cast<LineCategory>(other), true);
                    }
                }
                ImGui::EndPopup();
            }
            ImGui::SameLine();

            const uint32_t lines = lastCategoryLines_[i];
            ImGui::PushStyleColor(ImGuiCol_Text, shown ? DebugTheme::kAccentCyan : DebugTheme::kTextDim);
            ImGui::TextUnformatted(info.icon);
            ImGui::PopStyleColor();
            ImGui::SameLine();
            if (shown)
            {
                ImGui::TextUnformatted(info.label);
            }
            else
            {
                ImGui::TextDisabled("%s", info.label);
            }
            if (flag && !*flag && categoryEnabled_[i])
            {
                // 種類は出す設定なのに、持ち主の窓で切られている
                ImGui::SameLine();
                ImGui::TextDisabled("（元の窓で非表示）");
            }
            ImGui::SetItemTooltip("%s", info.tip);

            // 本数は右寄せ（0本は薄く）
            const std::string count = lines > 0 ? std::format("{} 本", lines) : std::string("-");
            const float countWidth = ImGui::CalcTextSize(count.c_str()).x;
            ImGui::SameLine((std::max)(ImGui::GetContentRegionMax().x - countWidth, ImGui::GetCursorPosX() + 8.0f));
            if (lines > 0 && shown)
            {
                ImGui::TextUnformatted(count.c_str());
            }
            else
            {
                ImGui::TextDisabled("%s", count.c_str());
            }
            ImGui::PopID();
        }
    }
    ImGui::EndDisabled();

    if (!compact)
    {
        ImGui::Spacing();
        DimText("本数は直前のフレームに積まれた線の数です（画面の外で省いた線は数えません）");
    }
}

} // namespace Hagine
#endif // USE_IMGUI
