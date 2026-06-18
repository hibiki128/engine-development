#pragma once
#include <chrono>

/// <summary>
/// フレームクラス
/// </summary>
namespace Hagine {
class Frame {
  private:
    /// ========================================================
    /// 静的メンバ変数
    /// ========================================================
    static std::chrono::high_resolution_clock::time_point lastTime_; ///< 最後の更新時刻
    static std::chrono::high_resolution_clock::time_point fpsCalcTime_;
    static std::chrono::high_resolution_clock::time_point startTime_;
    static int frameCount_;  ///< フレームカウント
    static float deltaTime_; ///< 前回のフレームからの経過時間
    static float fps_;       ///< FPS
  
  public:
    /// ========================================================
    /// 静的メンバ関数
    /// ========================================================
    static void Init();       ///< フレームの初期化処理
    static void Update();     ///< フレームの更新処理
    static float DeltaTime(); ///< 前回の更新からの経過時間を取得
    static float GetFPS();    ///< 現在のFPSを取得
    static float Time();      ///< 起動からの経過時間を秒単位で返す
};
} // namespace Hagine
