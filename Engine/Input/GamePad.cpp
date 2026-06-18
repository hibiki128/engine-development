#include "GamePad.h"
#include <algorithm>

namespace Hagine {
void GamePad::Init(int32_t playerIndex) {
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
    if (XInputGetState(playerIndex_, &testState) == ERROR_SUCCESS) {
        isConnected_ = true;
        state_ = testState;
        statePre_ = testState;
    }
}

void GamePad::Update() {
    // 前フレームの状態を保存
    statePre_ = state_;

    // 現在の状態を取得
    DWORD result = XInputGetState(playerIndex_, &state_);

    // 接続状態を更新
    isConnected_ = (result == ERROR_SUCCESS);
}

// ===== ボタン入力 =====

bool GamePad::IsPress(WORD button) const {
    if (!isConnected_)
        return false;
    return (state_.Gamepad.wButtons & button) != 0;
}

bool GamePad::IsTrigger(WORD button) const {
    if (!isConnected_)
        return false;
    bool currentPress = (state_.Gamepad.wButtons & button) != 0;
    bool previousPress = (statePre_.Gamepad.wButtons & button) != 0;
    return currentPress && !previousPress;
}

bool GamePad::IsRelease(WORD button) const {
    if (!isConnected_)
        return false;
    bool currentPress = (state_.Gamepad.wButtons & button) != 0;
    bool previousPress = (statePre_.Gamepad.wButtons & button) != 0;
    return !currentPress && previousPress;
}

// ===== スティック入力 =====

float GamePad::GetLeftStickX() const {
    if (!isConnected_)
        return 0.0f;
    return ApplyDeadZone(state_.Gamepad.sThumbLX, leftStickDeadZone_);
}

float GamePad::GetLeftStickY() const {
    if (!isConnected_)
        return 0.0f;
    return ApplyDeadZone(state_.Gamepad.sThumbLY, leftStickDeadZone_);
}

float GamePad::GetRightStickX() const {
    if (!isConnected_)
        return 0.0f;
    return ApplyDeadZone(state_.Gamepad.sThumbRX, rightStickDeadZone_);
}

float GamePad::GetRightStickY() const {
    if (!isConnected_)
        return 0.0f;
    return ApplyDeadZone(state_.Gamepad.sThumbRY, rightStickDeadZone_);
}

// ===== トリガー入力 =====

float GamePad::GetLeftTrigger() const {
    if (!isConnected_)
        return 0.0f;
    // トリガーは 0-255 の範囲
    BYTE trigger = state_.Gamepad.bLeftTrigger;
    // デッドゾーン適用 (XInputの標準値: 30)
    if (trigger < XINPUT_GAMEPAD_TRIGGER_THRESHOLD) {
        return 0.0f;
    }
    return (trigger - XINPUT_GAMEPAD_TRIGGER_THRESHOLD) /
           static_cast<float>(255 - XINPUT_GAMEPAD_TRIGGER_THRESHOLD);
}

float GamePad::GetRightTrigger() const {
    if (!isConnected_)
        return 0.0f;
    BYTE trigger = state_.Gamepad.bRightTrigger;
    if (trigger < XINPUT_GAMEPAD_TRIGGER_THRESHOLD) {
        return 0.0f;
    }
    return (trigger - XINPUT_GAMEPAD_TRIGGER_THRESHOLD) /
           static_cast<float>(255 - XINPUT_GAMEPAD_TRIGGER_THRESHOLD);
}

bool GamePad::IsLeftTriggerTriggered(float threshold) const {
    if (!isConnected_)
        return false;

    // 現在のトリガー値を取得
    float currentTrigger = GetLeftTrigger();

    // 前フレームのトリガー値を計算
    BYTE triggerPre = statePre_.Gamepad.bLeftTrigger;
    float prevTrigger = 0.0f;
    if (triggerPre >= XINPUT_GAMEPAD_TRIGGER_THRESHOLD) {
        prevTrigger = (triggerPre - XINPUT_GAMEPAD_TRIGGER_THRESHOLD) /
                      static_cast<float>(255 - XINPUT_GAMEPAD_TRIGGER_THRESHOLD);
    }

    // 前フレームは閾値未満、現在フレームは閾値以上
    return currentTrigger >= threshold && prevTrigger < threshold;
}

bool GamePad::IsRightTriggerTriggered(float threshold) const {
    if (!isConnected_)
        return false;

    // 現在のトリガー値を取得
    float currentTrigger = GetRightTrigger();

    // 前フレームのトリガー値を計算
    BYTE triggerPre = statePre_.Gamepad.bRightTrigger;
    float prevTrigger = 0.0f;
    if (triggerPre >= XINPUT_GAMEPAD_TRIGGER_THRESHOLD) {
        prevTrigger = (triggerPre - XINPUT_GAMEPAD_TRIGGER_THRESHOLD) /
                      static_cast<float>(255 - XINPUT_GAMEPAD_TRIGGER_THRESHOLD);
    }

    // 前フレームは閾値未満、現在フレームは閾値以上
    return currentTrigger >= threshold && prevTrigger < threshold;
}

// ===== 振動 =====

void GamePad::SetVibration(WORD leftMotor, WORD rightMotor) {
    if (!isConnected_)
        return;

    XINPUT_VIBRATION vibration;
    vibration.wLeftMotorSpeed = leftMotor;
    vibration.wRightMotorSpeed = rightMotor;
    XInputSetState(playerIndex_, &vibration);
}

void GamePad::StopVibration() {
    SetVibration(0, 0);
}

// ===== デッドゾーン設定 =====

void GamePad::SetLeftStickDeadZone(float deadZone) {
    leftStickDeadZone_ = std::clamp(deadZone, 0.0f, 1.0f);
}

void GamePad::SetRightStickDeadZone(float deadZone) {
    rightStickDeadZone_ = std::clamp(deadZone, 0.0f, 1.0f);
}

// ===== プライベート関数 =====

float GamePad::ApplyDeadZone(SHORT value, float deadZone) const {
    // -32768 ~ 32767 の範囲を -1.0f ~ 1.0f に正規化
    float normalizedValue = value / 32767.0f;

    // デッドゾーン適用
    float absValue = std::abs(normalizedValue);
    if (absValue < deadZone) {
        return 0.0f;
    }

    // デッドゾーンを超えた部分を0-1の範囲に再マッピング
    float sign = (normalizedValue > 0.0f) ? 1.0f : -1.0f;
    float remappedValue = (absValue - deadZone) / (1.0f - deadZone);

    return sign * std::clamp(remappedValue, 0.0f, 1.0f);
}
} // namespace Hagine
