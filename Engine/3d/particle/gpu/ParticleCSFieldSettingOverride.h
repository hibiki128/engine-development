#pragma once
#include <cstdint>

/// =====================================================================
/// フィールドの「一度きり設定上書き」ビット定数
/// フィールド側の overrideMask / パーティクル側の settingsOverrideFlags.x
/// で共通使用する。
///
/// 【設計メモ】
/// 旧実装は ParticleCSSettings の45項目を上書き対象にしていたが、
/// enableXxx 系などグループ全体の ConstantBuffer 設定は粒子単体では
/// 適用不可能で、実際に効くのは一部だけだった。
/// 粒子単体へ確実に適用できる8項目に絞って再設計している。
/// =====================================================================
namespace Hagine {
namespace FieldOverrideBits {
static constexpr uint32_t LifeTime = 1u << 0;       // 寿命を Min/Max 乱数で上書き
static constexpr uint32_t Scale = 1u << 1;          // スケールを Min/Max 乱数で上書き
static constexpr uint32_t Velocity = 1u << 2;       // 速度を Min/Max 乱数で置換
static constexpr uint32_t VelocityMul = 1u << 3;    // 速度に倍率を一度だけ乗算
static constexpr uint32_t AccelImpulse = 1u << 4;   // 速度に加速度を一度だけ加算
static constexpr uint32_t Color = 1u << 5;          // 色(RGB)を上書き（以後この色で固定）
static constexpr uint32_t TrailDistance = 1u << 6;  // トレイル生成間隔を上書き
static constexpr uint32_t GatherRedirect = 1u << 7; // 速度をターゲット方向へ向け替え
} // namespace FieldOverrideBits
} // namespace Hagine
