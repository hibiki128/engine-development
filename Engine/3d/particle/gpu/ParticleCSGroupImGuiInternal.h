#pragma once
#ifdef USE_IMGUI

// ※ namespace Hagine の外で include すること。ImGradient.h と同じ理由で、
//   imgui の型を namespace の中で見せると Hagine::ImVec4 が生まれて全参照が壊れる。
#include "imgui.h"
// DebugUIHelper.h は ImVec4 / ImGui:: を使うので imgui.h の後に include する
#include "utility/debug/imgui/DebugUIHelper.h"
#include "particle/ParticleStruct.h"
#include <functional>

// ParticleCSGroup のエディタUIを分割した .cpp 群が共有する部品。
// 分割前は DrawImGui() の中のローカルラムダだったが、ファイルを分けると
// 内部リンケージのままでは他の .cpp から見えなくなるのでここへ出した。
// 参照するのは ParticleCSGroupImGui*.cpp のみ。
namespace Hagine {

/// <summary>
/// 「すべて開く／閉じる」の要求（-1=なし 0=閉じる 1=開く）。DrawImGui の先頭のボタンで立て、末尾で下ろす
/// </summary>
inline int g_particleSectionOpenRequest = -1;

/// <summary>
/// セクション見出しの色を積む（PopSectionColor と対で使う）。
/// 見出しの直前に必ず呼ばれるので、「すべて開く／閉じる」の要求もここで次の見出しへ渡す
/// </summary>
inline void PushSectionColor(const ImVec4 &col)
{
    if (g_particleSectionOpenRequest >= 0)
    {
        ImGui::SetNextItemOpen(g_particleSectionOpenRequest == 1);
    }
    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(col.x * 0.45f, col.y * 0.45f, col.z * 0.45f, 0.55f));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(col.x * 0.55f, col.y * 0.55f, col.z * 0.55f, 0.70f));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(col.x * 0.65f, col.y * 0.65f, col.z * 0.65f, 0.85f));
}

/// <summary>PushSectionColor で積んだ色を戻す</summary>
inline void PopSectionColor() { ImGui::PopStyleColor(3); }

/// <summary>
/// エフェクトカードのヘッダ（× 削除ボタン付き）。展開中かどうかを返す。
/// onRemove で enable フラグを 0 にすると、そのエフェクトは非表示になり「＋追加」リストへ戻る。
/// </summary>
inline bool EffectHeader(const char *label, const ImVec4 &col, const std::function<void()> &onRemove)
{
    ImGui::PushID(label);
    PushSectionColor(col);
    // AllowOverlap: 後続の × ボタンをヘッダに重ねてもクリックがボタン側に渡るようにする
    // （これが無いとヘッダが全幅でクリックを奪い、× が押せず開閉だけになる）。
    bool open = ImGui::CollapsingHeader(label, ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);
    PopSectionColor();
    // ヘッダ右端に × 削除ボタン（ヘッダに重ねて配置）
    ImGui::SameLine(ImGui::GetContentRegionMax().x - 28.0f);
    ImGui::PushStyleColor(ImGuiCol_Button, DebugTheme::kButtonDanger);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, DebugTheme::kButtonDangerHover);
    if (ImGui::SmallButton("✕"))
        onRemove();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("このエフェクトを削除");
    ImGui::PopStyleColor(2);
    ImGui::PopID();
    return open;
}

/// <summary>
/// 「演出の基準空間」セレクタ。渦の回転軸と、渦/集束の目標オフセットの解釈をまとめて切り替える。
/// 同じ pSettings->effectSpace を指すので、ギャザー側で変えても渦側に反映される。
/// </summary>
inline void EffectSpaceCombo(const char *id, ParticleCSSettings *pSettings)
{
    static const char *kNames[] = {"ワールド固定", "エミッター基準", "ビルボード（カメラ）"};
    int space = static_cast<int>(pSettings->effectSpace);
    if (space < 0 || space > 2)
        space = 0;
    ImGui::PushID(id);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, DebugTheme::FrameBg(DebugTheme::kAccentBlue));
    if (ImGui::Combo("基準空間", &space, kNames, IM_ARRAYSIZE(kNames)))
        pSettings->effectSpace = static_cast<uint32_t>(space);
    ImGui::PopStyleColor();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("回転軸と目標座標をどの空間の値として扱うか（ギャザーと渦で共通）。\n\n"
                          "ワールド固定     : 従来動作。エミッターやカメラを回しても軸は動かない。\n"
                          "エミッター基準   : エミッターの回転に軸も追従する。\n"
                          "                   エミッター側の「ビルボード」と併用すると\n"
                          "                   発生形状ごとカメラへ正対する。\n"
                          "ビルボード（カメラ）: 軸がカメラの向きに追従する。\n"
                          "                   Z=(0,0,1) なら常に画面と平行に渦が回る。");
    ImGui::PopID();
}

} // namespace Hagine

#endif // USE_IMGUI
