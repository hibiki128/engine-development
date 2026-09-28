#pragma once
#ifdef USE_IMGUI
// シーンビューに重ねて描く部品（ツールバー・名前ラベル・アイコン）が共通で使う小物。
// ImGuiManagerSceneOverlay.cpp と ImGuiManagerSceneIcons.cpp からだけ include する。
#include "DebugUIHelper.h"
#include "ImGuizmoManager.h"
#include <imgui.h>

namespace Hagine {
namespace SceneOverlayInternal {
/// <summary>
/// ワールド座標をシーン画像上の位置へ写す。カメラの後ろなら false
/// </summary>
inline bool ProjectToImage(const Matrix4x4 &viewProjection, const Vector3 &p, const ImVec2 &imageMin, const ImVec2 &imageSize, ImVec2 &out)
{
    const auto &m = viewProjection.m;
    const float x = p.x * m[0][0] + p.y * m[1][0] + p.z * m[2][0] + m[3][0];
    const float y = p.x * m[0][1] + p.y * m[1][1] + p.z * m[2][1] + m[3][1];
    const float w = p.x * m[0][3] + p.y * m[1][3] + p.z * m[2][3] + m[3][3];
    if (w <= 1e-4f)
    {
        return false;
    }
    const float ndcX = x / w;
    const float ndcY = y / w;
    out = ImVec2(imageMin.x + (ndcX * 0.5f + 0.5f) * imageSize.x, imageMin.y + (0.5f - ndcY * 0.5f) * imageSize.y);
    // 画面から大きく外れたものは描かない（少しはみ出すぶんは許す）
    return ndcX > -1.2f && ndcX < 1.2f && ndcY > -1.2f && ndcY < 1.2f;
}

/// <summary>種類ごとのラベルの色（インスペクタの色分けとそろえる）</summary>
inline ImVec4 LabelAccent(GizmoCategory category)
{
    switch (category)
    {
    case GizmoCategory::Sprite:
        return DebugTheme::kAccentPurple;
    case GizmoCategory::Particle:
        return DebugTheme::kAccentOrange;
    case GizmoCategory::Light:
        return DebugTheme::kAccentYellow;
    default:
        return DebugTheme::kAccentBlue;
    }
}

/// <summary>角丸の札（文字＋地）を描く。anchor は札の下端中央</summary>
inline void DrawPill(ImDrawList *drawList, const ImVec2 &anchor, const char *text, const ImVec4 &accent, bool emphasized)
{
    const ImVec2 textSize = ImGui::CalcTextSize(text);
    const float padX = 6.0f;
    const float padY = 2.0f;
    const ImVec2 min = ImVec2(anchor.x - textSize.x * 0.5f - padX - 4.0f, anchor.y - textSize.y - padY * 2.0f);
    const ImVec2 max = ImVec2(anchor.x + textSize.x * 0.5f + padX, anchor.y);
    const ImU32 background = emphasized ? IM_COL32(20, 22, 28, 235) : IM_COL32(16, 16, 20, 170);
    drawList->AddRectFilled(min, max, background, 4.0f);
    if (emphasized)
    {
        drawList->AddRect(min, max, ImGui::ColorConvertFloat4ToU32(accent), 4.0f, 0, 1.0f);
    }
    // 種類の色の点
    drawList->AddCircleFilled(ImVec2(min.x + padX, (min.y + max.y) * 0.5f), 2.5f, ImGui::ColorConvertFloat4ToU32(accent), 8);
    drawList->AddText(ImVec2(min.x + padX + 6.0f, min.y + padY), emphasized ? IM_COL32(245, 245, 250, 255) : IM_COL32(200, 202, 210, 255),
                      text);
}
} // namespace SceneOverlayInternal
} // namespace Hagine
#endif // USE_IMGUI
