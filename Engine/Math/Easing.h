#pragma once
#include "type/Vector2.h"
#include "type/Vector3.h"

namespace Hagine {

/// <summary>
/// イージングの種類
/// </summary>
enum class EasingType {
    Linear,
    InSine, OutSine, InOutSine,
    InQuad, OutQuad, InOutQuad,
    InCubic, OutCubic, InOutCubic,
    InQuart, OutQuart, InOutQuart,
    InQuint, OutQuint, InOutQuint,
    InCirc, OutCirc, InOutCirc,
    InExpo, OutExpo, InOutExpo,
    InBack, OutBack, InOutBack,
    InElastic, OutElastic, InOutElastic,
    InBounce, OutBounce, InOutBounce
};

/// <summary>
/// イージングの状態（開始値・終了値・経過時間など）を保持するデータ構造
/// </summary>
/// <typeparam name="T">補間する値の型</typeparam>
template <typename T>
struct EasingData {
    T start;          // 開始値
    T end;            // 終了値
    T current;        // 現在値
    float time;       // 経過時間
    float maxTime;    // 補間にかける総時間
    EasingType type;  // イージングの種類
    bool isActive;    // イージング進行中フラグ

    /// <summary>
    /// コンストラクタ（非アクティブ状態で初期化）
    /// </summary>
    EasingData()
        : time(0.0f), maxTime(0.0f), type(EasingType::Linear), isActive(false) {}

    /// <summary>
    /// コンストラクタ（開始値・終了値・時間を指定してアクティブ状態で初期化）
    /// </summary>
    /// <param name="startVal">開始値</param>
    /// <param name="endVal">終了値</param>
    /// <param name="duration">補間にかける総時間</param>
    /// <param name="easingType">イージングの種類</param>
    EasingData(const T &startVal, const T &endVal, float duration, EasingType easingType = EasingType::Linear)
        : start(startVal), end(endVal), current(startVal),
          time(0.0f), maxTime(duration), type(easingType), isActive(true) {}

    /// <summary>
    /// イージングを更新して現在値を返す
    /// </summary>
    /// <param name="deltaTime">経過時間</param>
    /// <returns>T: 更新後の現在値</returns>
    T Update(float deltaTime) {
        if (!isActive)
            return current;

        time += deltaTime;
        if (time >= maxTime) {
            time = maxTime;
            isActive = false;
        }

        current = ApplyEasing(type, start, end, time, maxTime);
        return current;
    }

    /// <summary>
    /// イージングを新しい値で再設定して再開
    /// </summary>
    /// <param name="newStart">新しい開始値</param>
    /// <param name="newEnd">新しい終了値</param>
    /// <param name="duration">補間にかける総時間</param>
    /// <param name="easingType">イージングの種類</param>
    void Reset(const T &newStart, const T &newEnd, float duration, EasingType easingType) {
        start = newStart;
        end = newEnd;
        current = newStart;
        time = 0.0f;
        maxTime = duration;
        type = easingType;
        isActive = true;
    }

    /// <summary>
    /// イージングが完了しているか
    /// </summary>
    /// <returns>bool: 完了していれば true</returns>
    bool IsFinished() const { return !isActive; }
};

class Quaternion;

/// ===================================================
/// 線形補間・球面補間
/// ===================================================
float LerpE(const float& start, const float& end, float t);

Vector3 LerpE(const Vector3& start, const Vector3& end, float t);

Vector2 LerpE(const Vector2& start, const Vector2& end, float t);

Quaternion LerpE(const Quaternion &start, const Quaternion &end, float t);

Vector3 SLerp(const Vector3& start, const Vector3& end, float t);

/// ===================================================
/// 振幅付きElasticイージング
/// ===================================================
float EaseInElasticAmplitude(float t, const float& totaltime, const float& amplitude, const float& period);

float EaseOutElasticAmplitude(float t, float totaltime, float amplitude, float period);

float EaseInOutElasticAmplitude(float t, float totaltime, float amplitude, float period);

Vector3 EaseInElasticAmplitude(float t, const float &totaltime, const Vector3 &amplitude, const float &period);

Vector3 EaseOutElasticAmplitude(float t, float totaltime, const Vector3 &amplitude, float period);

Vector3 EaseInOutElasticAmplitude(float t, float totaltime, const Vector3 &amplitude, float period);

/// ===================================================
/// 各種イージング関数（種類は EasingType に対応）
/// ApplyEasing で type に応じた関数へ振り分ける
/// ===================================================
template <typename T> T ApplyEasing(EasingType type, const T &start, const T &end, float x, float totalX);

template<typename T> T EaseAmplitudeScale(const T& initScale, const float& easeT, const float& totalTime, const float& amplitude, const float& period);

template<typename T> T EaseInSine(const T& start, const T& end, float x, float totalX);

template<typename T> T EaseOutSine(const T& start, const T& end, float x, float totalX);

template<typename T> T EaseInOutSine(const T& start, const T& end, float x, float totalX);
template<typename T> T EaseInBack(const T& start, const T& end, float x, float totalX);

template<typename T> T EaseOutBack(const T& start, const T& end, float x, float totalX);

template<typename T> T EaseInOutBack(const T& start, const T& end, float x, float totalX);

template<typename T> T EaseInQuint(const T& start, const T& end, float x, float totalX);

template<typename T> T EaseOutQuint(const T& start, const T& end, float x, float totalX);

template<typename T> T EaseInOutQuint(const T& start, const T& end, float x, float totalX);

template<typename T> T EaseInCirc(const T& start, const T& end, float x, float totalX);

template<typename T> T EaseOutCirc(const T& start, const T& end, float x, float totalX);

template<typename T> T EaseInOutCirc(const T& start, const T& end, float x, float totalX);

template<typename T> T EaseInExpo(const T& start, const T& end, float x, float totalX);

template<typename T> T EaseOutExpo(const T& start, const T& end, float x, float totalX);

template<typename T> T EaseInOutExpo(const T& start, const T& end, float x, float totalX);

template<typename T> T EaseOutCubic(const T& start, const T& end, float x, float totalx);

template<typename T> T EaseInCubic(const T& start, const T& end, float x, float totalx);

template<typename T> T EaseInOutCubic(const T& start, const T& end, float x, float totalx);

template<typename T> T EaseInQuad(const T& start, const T& end, float x, float totalx);

template<typename T> T EaseOutQuad(const T& start, const T& end, float x, float totalx);

template<typename T> T EaseInOutQuad(const T& start, const T& end, float x, float totalx);

template<typename T> T EaseInQuart(const T& start, const T& end, float x, float totalx);

template<typename T> T EaseOutQuart(const T& start, const T& end, float x, float totalx);

float BounceEaseOut(float x);
template<typename T> T EaseInBounce(const T& start, const T& end, float x, float totalX);
template<typename T> T EaseOutBounce(const T& start, const T& end, float x, float totalX);

template<typename T> T EaseInOutBounce(const T& start, const T& end, float x, float totalX);
template<typename T>
T EaseInElastic(const T& start, const T& end, float x, float totalX);
template<typename T>
T EaseOutElastic(const T& start, const T& end, float x, float totalX);

template<typename T>
T EaseInOutElastic(const T& start, const T& end, float x, float totalX);
} // namespace Hagine
