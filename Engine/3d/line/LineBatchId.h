#pragma once
#include <cstdint>

namespace Hagine {

/// <summary>
/// 線描画の静的バッチの識別子（0は無効値）。
/// バッチの ID を持つだけのクラス（コライダーなど）が LineRenderer.h 全体を読まなくて済むよう、ここに分けてある
/// </summary>
using LineBatchId = uint32_t;
inline constexpr LineBatchId kInvalidLineBatch = 0;

} // namespace Hagine
