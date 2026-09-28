#pragma once
#ifdef USE_IMGUI
#include <ai/behavior/BehaviorTreeRegistry.h>
#include <imgui.h>

/// <summary>
/// ビヘイビアツリーエディタの .cpp が共有する色と定数。
/// ノードの状態（実行中・成功・失敗）と種類（コンポジット・判定・アクション）の色はここに集める。
/// </summary>
namespace Hagine::BTEditorStyle {

// 状態の光が消えるまでの時間（秒）と減る速さ
inline constexpr float kStatusGlowTime = 1.2f;
inline constexpr float kStatusGlowDecay = 1.5f;

// 種類ごとのアクセント（待機中の枠・タイトル）
inline constexpr ImVec4 kAccentComposite = {0.56f, 0.70f, 0.88f, 1.0f};
inline constexpr ImVec4 kAccentCondition = {0.80f, 0.72f, 0.92f, 1.0f};
inline constexpr ImVec4 kAccentAction = {0.62f, 0.83f, 0.66f, 1.0f};
inline constexpr ImVec4 kAccentUnknown = {0.72f, 0.72f, 0.74f, 1.0f};

// ノードの地
inline constexpr ImVec4 kNodeBgIdle = {0.13f, 0.14f, 0.16f, 1.0f};
inline constexpr ImVec4 kNodeBgRunning = {0.20f, 0.18f, 0.09f, 1.0f};
inline constexpr ImVec4 kNodeBgSuccess = {0.11f, 0.18f, 0.12f, 1.0f};
inline constexpr ImVec4 kNodeBgFailure = {0.20f, 0.12f, 0.12f, 1.0f};

// 状態の枠（アルファは光の強さで上書きする）
inline constexpr ImVec4 kBorderRunning = {0.95f, 0.75f, 0.25f, 1.0f};
inline constexpr ImVec4 kBorderSuccess = {0.30f, 0.78f, 0.42f, 1.0f};
inline constexpr ImVec4 kBorderFailure = {0.82f, 0.34f, 0.34f, 1.0f};

// 状態の文字
inline constexpr ImVec4 kTextRunning = {1.0f, 0.86f, 0.35f, 1.0f};
inline constexpr ImVec4 kTextSuccess = {0.45f, 0.90f, 0.55f, 1.0f};
inline constexpr ImVec4 kTextFailure = {0.95f, 0.45f, 0.45f, 1.0f};

// ピン
inline constexpr ImVec4 kPinInput = {0.65f, 0.85f, 1.0f, 0.85f};
inline constexpr ImVec4 kPinSuccess = {0.28f, 1.0f, 0.40f, 1.0f};
inline constexpr ImVec4 kPinFailure = {1.0f, 0.30f, 0.30f, 1.0f};
inline constexpr ImVec4 kPinOutput = {0.90f, 0.90f, 0.90f, 0.85f};

// リンク
inline constexpr ImVec4 kLinkIdle = {0.50f, 0.50f, 0.50f, 0.80f};
inline constexpr ImVec4 kLinkFlow = {1.0f, 0.90f, 0.10f, 1.0f};
inline constexpr ImVec4 kLinkFlowMarker = {1.0f, 1.00f, 0.40f, 1.0f};
inline constexpr ImVec4 kLinkReject = {0.90f, 0.35f, 0.35f, 1.0f};
inline constexpr ImVec4 kLinkAccept = {0.45f, 0.85f, 0.55f, 1.0f};

// 根・未接続の印
inline constexpr ImVec4 kBadgeRoot = {0.95f, 0.80f, 0.40f, 1.0f};
inline constexpr ImVec4 kBadgeOrphan = {0.70f, 0.55f, 0.45f, 1.0f};

/// <summary>種類のアクセント色</summary>
inline ImVec4 AccentOf(const BTNodeTypeDesc *desc)
{
    if (!desc)
        return kAccentUnknown;
    switch (desc->kind)
    {
    case BTNodeKind::Composite:
    case BTNodeKind::WeightedRandom:
        return kAccentComposite;
    case BTNodeKind::Condition:
        return kAccentCondition;
    case BTNodeKind::Action:
        return kAccentAction;
    }
    return kAccentUnknown;
}

/// <summary>種類の呼び名</summary>
inline const char *KindLabel(const BTNodeTypeDesc *desc)
{
    if (!desc)
        return "未登録";
    switch (desc->kind)
    {
    case BTNodeKind::Composite:
        return "コンポジット";
    case BTNodeKind::WeightedRandom:
        return "デコレータ";
    case BTNodeKind::Condition:
        return "条件ノード";
    case BTNodeKind::Action:
        return "アクションノード";
    }
    return "未登録";
}

/// <summary>色のアルファだけ変える</summary>
inline ImVec4 WithAlpha(const ImVec4 &color, float alpha)
{
    return {color.x, color.y, color.z, alpha};
}

} // namespace Hagine::BTEditorStyle

#endif // USE_IMGUI
