#pragma once
#include "chrono"
#include <cstdint>

namespace Hagine {

/// <summary>
/// FPS固定クラス
/// 目標FPSに合わせてフレーム末尾の余剰時間を待機する
/// </summary>
class FrameRateLimiter
{
  public:
    FrameRateLimiter() = default;
    ~FrameRateLimiter() = default;
    FrameRateLimiter(const FrameRateLimiter &) = delete;
    FrameRateLimiter &operator=(const FrameRateLimiter &) = delete;

    /// <summary>
    /// 初期化（基準時間の記録）
    /// </summary>
    void Initialize();

    /// <summary>
    /// 目標FPSに合わせて余剰時間を待機する
    /// </summary>
    void Wait();

  private:
    // 前フレームの基準時間
    std::chrono::steady_clock::time_point reference_;
    // 目標FPS
    const double targetFPS_ = 60.0;
    // 1フレームの目標時間
    const std::chrono::microseconds frameTime_{static_cast<uint64_t>(1000000.0 / targetFPS_)};
};
} // namespace Hagine
