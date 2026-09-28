#pragma once
#include <chrono>

/// <summary>
/// フレームクラス
/// </summary>
namespace Hagine {
class Frame
{
  private:
    /// ========================================================
    /// 静的メンバ変数
    /// ========================================================
    static std::chrono::high_resolution_clock::time_point lastTime_; ///< 最後の更新時刻
    static std::chrono::high_resolution_clock::time_point fpsCalcTime_;
    static std::chrono::high_resolution_clock::time_point startTime_;
    static int frameCount_;  ///< フレームカウント
    static float deltaTime_; ///< 前回のフレームからの経過時間（実時間）
    static float fps_;       ///< FPS
    static float timeScale_; ///< ゲーム時間の進む速さ（1=等速 / 0=停止）

  public:
    /// ========================================================
    /// 静的メンバ関数
    /// ========================================================
    static void Init();       ///< フレームの初期化処理
    static void Update();     ///< フレームの更新処理
    static float GetFPS();    ///< 現在のFPSを取得
    static float Time();      ///< 起動からの経過時間を秒単位で返す

    /// <summary>
    /// 前回の更新からの経過時間を取得（時間の倍率を掛けたゲーム時間）。
    /// キャラクターの移動・アニメーション・ゲームのタイマーはこちらを使う
    /// </summary>
    static float DeltaTime();

    /// <summary>
    /// 前回の更新からの実際の経過時間を取得（時間の倍率を掛けない）。
    /// ヒットストップ中も止めたくないもの（パーティクル・画面効果・UI・音）はこちらを使う
    /// </summary>
    static float UnscaledDeltaTime();

    /// <summary>
    /// ゲーム時間の進む速さを設定する。ヒットストップやスローモーションに使う
    /// </summary>
    /// <param name="scale">1で等速、0で停止。負の値は0として扱う</param>
    static void SetTimeScale(float scale);

    /// <summary>
    /// ゲーム時間の進む速さを取得する
    /// </summary>
    static float GetTimeScale() { return timeScale_; }
};
} // namespace Hagine
