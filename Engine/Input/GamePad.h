#pragma once
#include <wrl.h>
#include <XInput.h>
#include <cstdint>

/// <summary>
/// ゲームパッド入力を管理するクラス
/// XInput専用のラッパークラス
/// </summary>
namespace Hagine {
class GamePad {
  private:
    XINPUT_STATE state_;    // 現在の状態
    XINPUT_STATE statePre_; // 前フレームの状態
    int32_t playerIndex_;   // プレイヤー番号 (0-3)
    bool isConnected_;      // 接続されているか

    // デッドゾーン設定
    float leftStickDeadZone_;  // 左スティックのデッドゾーン
    float rightStickDeadZone_; // 右スティックのデッドゾーン

  public:
    /// <summary>
    /// 初期化
    /// </summary>
    /// <param name="playerIndex">プレイヤー番号 (0-3)</param>
    void Init(int32_t playerIndex = 0);

    /// <summary>
    /// 更新（毎フレーム呼び出す）
    /// </summary>
    void Update();

    // ===== ボタン入力 =====

    /// <summary>
    /// ボタンが押されているか
    /// </summary>
    /// <param name="button">ボタン (XINPUT_GAMEPAD_A など)</param>
    /// <returns>押されているか</returns>
    bool IsPress(WORD button) const;

    /// <summary>
    /// ボタンが押された瞬間か
    /// </summary>
    /// <param name="button">ボタン</param>
    /// <returns>押された瞬間か</returns>
    bool IsTrigger(WORD button) const;

    /// <summary>
    /// ボタンが離された瞬間か
    /// </summary>
    /// <param name="button">ボタン</param>
    /// <returns>離された瞬間か</returns>
    bool IsRelease(WORD button) const;

    // ===== スティック入力 =====

    /// <summary>
    /// 左スティックのX軸入力を取得 (-1.0f ~ 1.0f)
    /// </summary>
    /// <returns>正規化された入力値</returns>
    float GetLeftStickX() const;

    /// <summary>
    /// 左スティックのY軸入力を取得 (-1.0f ~ 1.0f)
    /// </summary>
    /// <returns>正規化された入力値</returns>
    float GetLeftStickY() const;

    /// <summary>
    /// 右スティックのX軸入力を取得 (-1.0f ~ 1.0f)
    /// </summary>
    /// <returns>正規化された入力値</returns>
    float GetRightStickX() const;

    /// <summary>
    /// 右スティックのY軸入力を取得 (-1.0f ~ 1.0f)
    /// </summary>
    /// <returns>正規化された入力値</returns>
    float GetRightStickY() const;

    // ===== トリガー入力 =====

    /// <summary>
    /// 左トリガーの入力を取得 (0.0f ~ 1.0f)
    /// </summary>
    /// <returns>正規化された入力値</returns>
    float GetLeftTrigger() const;

    /// <summary>
    /// 右トリガーの入力を取得 (0.0f ~ 1.0f)
    /// </summary>
    /// <returns>正規化された入力値</returns>
    float GetRightTrigger() const;

    /// <summary>
    /// 左トリガーが押された瞬間か
    /// </summary>
    /// <param name="threshold">判定閾値 (0.0f ~ 1.0f)</param>
    /// <returns>押された瞬間か</returns>
    bool IsLeftTriggerTriggered(float threshold = 0.25f) const;

    /// <summary>
    /// 右トリガーが押された瞬間か
    /// </summary>
    /// <param name="threshold">判定閾値 (0.0f ~ 1.0f)</param>
    /// <returns>押された瞬間か</returns>
    bool IsRightTriggerTriggered(float threshold = 0.25f) const;

    // ===== 振動 =====

    /// <summary>
    /// 振動を設定する
    /// </summary>
    /// <param name="leftMotor">左モーターの強度 (0-65535)</param>
    /// <param name="rightMotor">右モーターの強度 (0-65535)</param>
    void SetVibration(WORD leftMotor, WORD rightMotor);

    /// <summary>
    /// 振動を停止する
    /// </summary>
    void StopVibration();

    // ===== デッドゾーン設定 =====

    /// <summary>
    /// 左スティックのデッドゾーンを設定
    /// </summary>
    /// <param name="deadZone">デッドゾーン (0.0f ~ 1.0f)</param>
    void SetLeftStickDeadZone(float deadZone);

    /// <summary>
    /// 右スティックのデッドゾーンを設定
    /// </summary>
    /// <param name="deadZone">デッドゾーン (0.0f ~ 1.0f)</param>
    void SetRightStickDeadZone(float deadZone);

    // ===== 接続状態 =====

    /// <summary>
    /// コントローラーが接続されているか
    /// </summary>
    /// <returns>接続されているか</returns>
    bool IsConnected() const { return isConnected_; }

    /// <summary>
    /// 生のXINPUT_STATEを取得（特殊な処理用）
    /// </summary>
    /// <returns>現在の状態</returns>
    const XINPUT_STATE &GetState() const { return state_; }

    /// <summary>
    /// 前フレームの生のXINPUT_STATEを取得（特殊な処理用）
    /// </summary>
    /// <returns>前フレームの状態</returns>
    const XINPUT_STATE &GetStatePrevious() const { return statePre_; }

  private:
    /// <summary>
    /// スティック入力にデッドゾーンを適用して正規化
    /// </summary>
    /// <param name="value">生の入力値 (-32768 ~ 32767)</param>
    /// <param name="deadZone">デッドゾーン (0.0f ~ 1.0f)</param>
    /// <returns>正規化された値 (-1.0f ~ 1.0f)</returns>
    float ApplyDeadZone(SHORT value, float deadZone) const;
};
} // namespace Hagine
