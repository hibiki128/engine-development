#include "FrameRateLimiter.h"
#include "thread"

namespace Hagine {

void FrameRateLimiter::Initialize() {
    // 現在時間を記録する
    reference_ = std::chrono::steady_clock::now();
}

void FrameRateLimiter::Wait() {
    // 現在時間を取得する
    std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();

    // 前回記録からの経過時間を取得する
    std::chrono::microseconds elapsed = std::chrono::duration_cast<std::chrono::microseconds>(now - reference_);

    // 次のフレームまでの待機時間を計算
    if (elapsed < frameTime_) {
        // 待機すべき時間
        std::chrono::microseconds sleepTime = frameTime_ - elapsed;

        // より正確なスリープのためのスピンロック
        auto sleepEnd = now + sleepTime;
        // まず大部分の時間をsleep_forで待機
        if (sleepTime > std::chrono::microseconds(1000)) {
            std::this_thread::sleep_for(sleepTime - std::chrono::microseconds(1000));
        }
        // 残りの短い時間はスピンロックで正確に待機
        while (std::chrono::steady_clock::now() < sleepEnd) {
            // スピンロック（何もしない）
        }
    }

    // 次のフレームの基準時間を更新
    // 精確な60FPSを維持するため、理想的なフレーム時間を加算
    reference_ += frameTime_;

    // もし大幅に遅れている場合は現在時刻に調整（フレームスキップ）
    if (std::chrono::steady_clock::now() > reference_ + frameTime_) {
        reference_ = std::chrono::steady_clock::now();
    }
}
} // namespace Hagine
