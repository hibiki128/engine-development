#define NOMINMAX
#include "GamePad.h"
#include <Frame.h>
#include <algorithm>

namespace {
float gVibrationScale = 1.0f; // 全体の振動の強さ（オプション）
} // namespace

namespace Hagine {
void GamePad::Init(int32_t playerIndex)
{
    playerIndex_ = playerIndex;
    isConnected_ = false;

    // デフォルトのデッドゾーン設定 (XInputの標準値を0-1の範囲に正規化)
    leftStickDeadZone_ = XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE / 32767.0f;
    rightStickDeadZone_ = XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE / 32767.0f;

    // 状態の初期化
    ZeroMemory(&state_, sizeof(XINPUT_STATE));
    ZeroMemory(&statePre_, sizeof(XINPUT_STATE));

    // 接続チェック
    XINPUT_STATE testState;
    if (XInputGetState(playerIndex_, &testState) == ERROR_SUCCESS)
    {
        isConnected_ = true;
        state_ = testState;
        statePre_ = testState;
    }
}

void GamePad::Update()
{
    // 前フレームの状態を保存
    statePre_ = state_;

    // 現在の状態を取得
    DWORD result = XInputGetState(playerIndex_, &state_);

    // 接続状態を更新
    isConnected_ = (result == ERROR_SUCCESS);

    // 時間つき振動を進める（ヒットストップで止まらないよう実時間）
    if (rumbleTimer_ > 0.0f)
    {
        rumbleTimer_ -= Frame::UnscaledDeltaTime();
        if (rumbleTimer_ <= 0.0f)
        {
            rumbleTimer_ = 0.0f;
            rumbleLow_ = 0.0f;
            rumbleHigh_ = 0.0f;
            StopVibration();
        }
    }
}

GamePad::~GamePad()
{
    if (rumbleTimer_ > 0.0f)
    {
        StopVibration();
    }
}

void GamePad::Rumble(float lowStrength, float highStrength, float seconds)
{
    if (!isConnected_ || seconds <= 0.0f)
        return;
    const float scale = std::clamp(gVibrationScale, 0.0f, 2.0f);
    const float low = std::clamp(lowStrength * scale, 0.0f, 1.0f);
    const float high = std::clamp(highStrength * scale, 0.0f, 1.0f);
    if (low <= 0.0f && high <= 0.0f)
        return;
    // 強い方を残す（弱い振動で強い振動を消さない）
    if (rumbleTimer_ > 0.0f && low + high < rumbleLow_ + rumbleHigh_)
    {
        rumbleTimer_ = (std::max)(rumbleTimer_, seconds);
        return;
    }
    rumbleLow_ = low;
    rumbleHigh_ = high;
    rumbleTimer_ = seconds;
    SetVibration(static_cast<WORD>(low * 65535.0f), static_cast<WORD>(high * 65535.0f));
}

void GamePad::SetVibrationScale(float scale)
{
    gVibrationScale = (std::max)(scale, 0.0f);
}

float GamePad::GetVibrationScale()
{
    return gVibrationScale;
}

// ===== ボタン入力 =====

bool GamePad::IsPress(WORD button) const
{
    if (!isConnected_)
        return false;
    return (state_.Gamepad.wButtons & button) != 0;
}

bool GamePad::IsTrigger(WORD button) const
{
    if (!isConnected_)
        return false;
    bool currentPress = (state_.Gamepad.wButtons & button) != 0;
    bool previousPress = (statePre_.Gamepad.wButtons & button) != 0;
    return currentPress && !previousPress;
}

bool GamePad::IsRelease(WORD button) const
{
    if (!isConnected_)
        return false;
    bool currentPress = (state_.Gamepad.wButtons & button) != 0;
    bool previousPress = (statePre_.Gamepad.wButtons & button) != 0;
    return !currentPress && previousPress;
}

// ===== スティック入力 =====

float GamePad::GetLeftStickX() const
{
    if (!isConnected_)
        return 0.0f;
    return ApplyDeadZone(state_.Gamepad.sThumbLX, leftStickDeadZone_);
}

float GamePad::GetLeftStickY() const
{
    if (!isConnected_)
        return 0.0f;
    return ApplyDeadZone(state_.Gamepad.sThumbLY, leftStickDeadZone_);
}

float GamePad::GetRightStickX() const
{
    if (!isConnected_)
        return 0.0f;
    return ApplyDeadZone(state_.Gamepad.sThumbRX, rightStickDeadZone_);
}

float GamePad::GetRightStickY() const
{
    if (!isConnected_)
        return 0.0f;
    return ApplyDeadZone(state_.Gamepad.sThumbRY, rightStickDeadZone_);
}

// ===== トリガー入力 =====

float GamePad::GetLeftTrigger() const
{
    if (!isConnected_)
        return 0.0f;
    // トリガーは 0-255 の範囲
    BYTE trigger = state_.Gamepad.bLeftTrigger;
    // デッドゾーン適用 (XInputの標準値: 30)
    if (trigger < XINPUT_GAMEPAD_TRIGGER_THRESHOLD)
    {
        return 0.0f;
    }
    return (trigger - XINPUT_GAMEPAD_TRIGGER_THRESHOLD) /
           static_cast<float>(255 - XINPUT_GAMEPAD_TRIGGER_THRESHOLD);
}

float GamePad::GetRightTrigger() const
{
    if (!isConnected_)
        return 0.0f;
    BYTE trigger = state_.Gamepad.bRightTrigger;
    if (trigger < XINPUT_GAMEPAD_TRIGGER_THRESHOLD)
    {
        return 0.0f;
    }
    return (trigger - XINPUT_GAMEPAD_TRIGGER_THRESHOLD) /
           static_cast<float>(255 - XINPUT_GAMEPAD_TRIGGER_THRESHOLD);
}

bool GamePad::IsLeftTriggerTriggered(float threshold) const
{
    if (!isConnected_)
        return false;

    // 現在のトリガー値を取得
    float currentTrigger = GetLeftTrigger();

    // 前フレームのトリガー値を計算
    BYTE triggerPre = statePre_.Gamepad.bLeftTrigger;
    float prevTrigger = 0.0f;
    if (triggerPre >= XINPUT_GAMEPAD_TRIGGER_THRESHOLD)
    {
        prevTrigger = (triggerPre - XINPUT_GAMEPAD_TRIGGER_THRESHOLD) /
                      static_cast<float>(255 - XINPUT_GAMEPAD_TRIGGER_THRESHOLD);
    }

    // 前フレームは閾値未満、現在フレームは閾値以上
    return currentTrigger >= threshold && prevTrigger < threshold;
}

bool GamePad::IsRightTriggerTriggered(float threshold) const
{
    if (!isConnected_)
        return false;

    // 現在のトリガー値を取得
    float currentTrigger = GetRightTrigger();

    // 前フレームのトリガー値を計算
    BYTE triggerPre = statePre_.Gamepad.bRightTrigger;
    float prevTrigger = 0.0f;
    if (triggerPre >= XINPUT_GAMEPAD_TRIGGER_THRESHOLD)
    {
        prevTrigger = (triggerPre - XINPUT_GAMEPAD_TRIGGER_THRESHOLD) /
                      static_cast<float>(255 - XINPUT_GAMEPAD_TRIGGER_THRESHOLD);
    }

    // 前フレームは閾値未満、現在フレームは閾値以上
    return currentTrigger >= threshold && prevTrigger < threshold;
}

// ===== 振動 =====

void GamePad::SetVibration(WORD leftMotor, WORD rightMotor)
{
    if (!isConnected_)
        return;
    // 振動オフのときは鳴らさない。停止（0,0）だけは通す
    if (!isVibrationEnabled_ && (leftMotor != 0 || rightMotor != 0))
        return;

    XINPUT_VIBRATION vibration;
    vibration.wLeftMotorSpeed = leftMotor;
    vibration.wRightMotorSpeed = rightMotor;
    XInputSetState(playerIndex_, &vibration);
}

void GamePad::StopVibration()
{
    SetVibration(0, 0);
}

void GamePad::SetVibrationEnabled(bool enabled)
{
    isVibrationEnabled_ = enabled;
    if (!isVibrationEnabled_)
    {
        // 設定を切った瞬間に鳴りっぱなしにならないよう止める
        StopVibration();
    }
}

// ===== デッドゾーン設定 =====

void GamePad::SetLeftStickDeadZone(float deadZone)
{
    leftStickDeadZone_ = std::clamp(deadZone, 0.0f, 1.0f);
}

void GamePad::SetRightStickDeadZone(float deadZone)
{
    rightStickDeadZone_ = std::clamp(deadZone, 0.0f, 1.0f);
}

void GamePad::SetStickSensitivity(float sensitivity)
{
    stickSensitivity_ = std::max(sensitivity, 0.01f);
}

// ===== プライベート関数 =====

float GamePad::ApplyDeadZone(SHORT value, float deadZone) const
{
    // -32768 ~ 32767 の範囲を -1.0f ~ 1.0f に正規化
    float normalizedValue = value / 32767.0f;

    // デッドゾーン適用
    float absValue = std::abs(normalizedValue);
    if (absValue < deadZone)
    {
        return 0.0f;
    }

    // デッドゾーンを超えた部分を0-1の範囲に再マッピングし、感度の倍率を掛ける
    float sign = (normalizedValue > 0.0f) ? 1.0f : -1.0f;
    float remappedValue = (absValue - deadZone) / (1.0f - deadZone);
    remappedValue *= stickSensitivity_;

    return sign * std::clamp(remappedValue, 0.0f, 1.0f);
}
} // namespace Hagine
