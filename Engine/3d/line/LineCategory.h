#pragma once
#include <cstdint>

namespace Hagine {

/// <summary>
/// デバッグ線の種類。線を積む側が LineCategoryScope で「今からこの種類を積む」と宣言しておくと、
/// 「デバッグ線」窓で種類ごとに本数を確かめたり、まとめて隠したりできる。
/// 何も宣言せずに積んだ線は Other に数える。
/// 並びを変えると保存済みのオン/オフ（ビット）がずれるので、足すときは Count の手前へ足す
/// </summary>
enum class LineCategory : uint8_t
{
    Other = 0,  // 種類を宣言していない線
    Grid,       // 床のグリッド
    Selection,  // 選択中の物の枠（シーン窓に重ねて描く）
    GizmoDebug, // トランスフォームマネージャの補助表示（AABB・外接球・レイ）
    SnapGrid,   // 移動スナップ中の刻みのマス目
    Collider,   // コライダー
    Light,      // ライトの向き・範囲
    SceneIcon,  // アイコンで選んだ物の範囲（ライト・カメラなど）
    Placement,  // 配置ツールの下描き
    Wireframe,  // モデルのワイヤーフレーム
    Skeleton,   // 骨（スケルトン）
    Ik,         // 足・手のIK、揺れ物
    Particle,   // パーティクルのエミッター・フィールド・目標点
    Motion,     // モーションエディターの経路
    Culling,    // 錐台カリングの確認（視錐台・省いた物）
    Count
};

inline constexpr int kLineCategoryCount = static_cast<int>(LineCategory::Count);

} // namespace Hagine
