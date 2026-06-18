
#include "Easing.h"
#include<numbers>
#include <cmath>
#include"type/Quaternion.h"

namespace Hagine {
float LerpE(const float& start, const float& end, float t) { return (1.0f - t) * start + end * t; }

Vector3 LerpE(const Vector3& start, const Vector3& end, float t) {
	Vector3 result;
	result.x = (1.0f - t) * start.x + end.x * t;
	result.y = (1.0f - t) * start.y + end.y * t;
	result.z = (1.0f - t) * start.z + end.z * t;
	return result;
}

Vector2 LerpE(const Vector2& start, const Vector2& end, float t) {
	Vector2 result;
	result.x = (1.0f - t) * start.x + end.x * t;
	result.y = (1.0f - t) * start.y + end.y * t;

	return result;
}

Quaternion LerpE(const Quaternion &start, const Quaternion &end, float t) {
    // クォータニオンの場合はSlerpを使用（球面線形補間）
    return Quaternion::Slerp(start, end, t);
}

Vector3 SLerp(const Vector3 &start, const Vector3 &end, float t) {
	Vector3 Nstart = (start).Normalize();
	Vector3 Nend = (end).Normalize();
	// 内積を求める
	float dot = (Nstart).Dot(Nend);
	// 誤差により1.0fを超えるのを防ぐ
	if (dot > 1.0f) {
		dot = 1.0f;
	}
	// アークコサインでθの角度を求める
	float theta = std::acos(dot);
	// θの角度からsinθを求める
	float sinTheta = std::sin(theta);
	// サイン(θ(1-t))を求める
	float sinThetaFrom = std::sin((1 - t) * theta);
	// サインθtを求める
	float sinThetaTo = std::sin(t * theta);
	Vector3 NormalizeVector;
	if (sinTheta < 1.0e-5) {
		NormalizeVector = Nstart;
	}
	else {
		// 球面線形補間したベクトル(単位ベクトル)
		NormalizeVector.x = (sinThetaFrom * Nstart.x + sinThetaTo * Nend.x) / sinTheta;
		NormalizeVector.y = (sinThetaFrom * Nstart.y + sinThetaTo * Nend.y) / sinTheta;
		NormalizeVector.z = (sinThetaFrom * Nstart.z + sinThetaTo * Nend.z) / sinTheta;
	}
	// ベクトルの長さはstartとendの長さを線形補間
	float length1 = (start).Length();
	float length2 = (end).Length();
	// LerpEで補間ベクトルの長さを求める
	float length = LerpE(length1, length2, t);
	// 長さを反映
	return NormalizeVector * length;
}


float EaseInElasticAmplitude(float t, const float& totaltime, const float& amplitude, const float& period) {

	if (t <= 0.0f) {
		return 0.0f;
	}
	if (t >= totaltime) {
		return 0.0f;
	}
	float s = period / (2.0f * std::numbers::pi_v<float>) * std::asinf(1.0f);
	t /= totaltime;

	return -amplitude * std::powf(2.0f, 10.0f * (t - 1.0f)) * std::sinf((t - 1.0f - s) * (2.0f * std::numbers::pi_v<float>) / period);
}

float EaseOutElasticAmplitude(float t, float totaltime, float amplitude, float period) {
	if (t <= 0.0f)
		return 0.0f;
	if (t >= totaltime)
		return 0.0f;

	float s = period / (2.0f * std::numbers::pi_v<float>) * std::asin(1.0f);
	t /= totaltime;

	return amplitude * std::pow(2.0f, -10.0f * t) * std::sin((t - s) * (2.0f * std::numbers::pi_v<float>) / period);
}

float EaseInOutElasticAmplitude(float t, float totaltime, float amplitude, float period) {
	if (t <= 0.0f)
		return 0.0f;
	if (t >= totaltime)
		return 0.0f;

	float backPoint = 0.5f;
	t /= totaltime;

	if (t < backPoint) {
		return EaseOutElasticAmplitude(t, totaltime, amplitude, period);
	}
	else {

		return EaseInElasticAmplitude(t - backPoint, totaltime - backPoint, amplitude, period);
	}
}

// 各軸のamplitudeに対してEaseInElasticを適用
Vector3 EaseInElasticAmplitude(float t, const float &totaltime, const Vector3 &amplitude, const float &period) {
    if (t <= 0.0f) {
        return Vector3(0.0f, 0.0f, 0.0f);
    }
    if (t >= totaltime) {
        return Vector3(0.0f, 0.0f, 0.0f);
    }

    float s = period / (2.0f * std::numbers::pi_v<float>)*std::asinf(1.0f);
    t /= totaltime;

    // スカラー部分を計算してamplitudeに乗算
    float scale = -std::powf(2.0f, 10.0f * (t - 1.0f)) * std::sinf((t - 1.0f - s) * (2.0f * std::numbers::pi_v<float>) / period);
    return amplitude * scale;
}

// 各軸のamplitudeに対してEaseOutElasticを適用
Vector3 EaseOutElasticAmplitude(float t, float totaltime, const Vector3 &amplitude, float period) {
    if (t <= 0.0f) {
        return Vector3(0.0f, 0.0f, 0.0f);
    }
    if (t >= totaltime) {
        return Vector3(0.0f, 0.0f, 0.0f);
    }

    float s = period / (2.0f * std::numbers::pi_v<float>)*std::asin(1.0f);
    t /= totaltime;

    float scale = std::pow(2.0f, -10.0f * t) * std::sin((t - s) * (2.0f * std::numbers::pi_v<float>) / period);
    return amplitude * scale;
}

// 各軸のamplitudeに対してEaseInOutElasticを適用
Vector3 EaseInOutElasticAmplitude(float t, float totaltime, const Vector3 &amplitude, float period) {
    if (t <= 0.0f) {
        return Vector3(0.0f, 0.0f, 0.0f);
    }
    if (t >= totaltime) {
        return Vector3(0.0f, 0.0f, 0.0f);
    }

    float backPoint = 0.5f;
    t /= totaltime;

    // 前半はEaseOut、後半はEaseInに委譲
    if (t < backPoint) {
        return EaseOutElasticAmplitude(t, totaltime, amplitude, period);
    } else {
        return EaseInElasticAmplitude(t - backPoint, totaltime - backPoint, amplitude, period);
    }
}

template <typename T>
T ApplyEasing(EasingType type, const T &start, const T &end, float x, float totalX) {
    switch (type) {
    case EasingType::Linear:
        return LerpE(start, end, x / totalX);
    case EasingType::InSine:
        return EaseInSine(start, end, x, totalX);
    case EasingType::OutSine:
        return EaseOutSine(start, end, x, totalX);
    case EasingType::InOutSine:
        return EaseInOutSine(start, end, x, totalX);
    case EasingType::InQuad:
        return EaseInQuad(start, end, x, totalX);
    case EasingType::OutQuad:
        return EaseOutQuad(start, end, x, totalX);
    case EasingType::InOutQuad:
        return EaseInOutQuad(start, end, x, totalX);
    case EasingType::InCubic:
        return EaseInCubic(start, end, x, totalX);
    case EasingType::OutCubic:
        return EaseOutCubic(start, end, x, totalX);
    case EasingType::InOutCubic:
        return EaseInOutCubic(start, end, x, totalX);
    case EasingType::InQuart:
        return EaseInQuart(start, end, x, totalX);
    case EasingType::OutQuart:
        return EaseOutQuart(start, end, x, totalX);
    case EasingType::InQuint:
        return EaseInQuint(start, end, x, totalX);
    case EasingType::OutQuint:
        return EaseOutQuint(start, end, x, totalX);
    case EasingType::InOutQuint:
        return EaseInOutQuint(start, end, x, totalX);
    case EasingType::InCirc:
        return EaseInCirc(start, end, x, totalX);
    case EasingType::OutCirc:
        return EaseOutCirc(start, end, x, totalX);
    case EasingType::InOutCirc:
        return EaseInOutCirc(start, end, x, totalX);
    case EasingType::InExpo:
        return EaseInExpo(start, end, x, totalX);
    case EasingType::OutExpo:
        return EaseOutExpo(start, end, x, totalX);
    case EasingType::InOutExpo:
        return EaseInOutExpo(start, end, x, totalX);
    case EasingType::InBack:
        return EaseInBack(start, end, x, totalX);
    case EasingType::OutBack:
        return EaseOutBack(start, end, x, totalX);
    case EasingType::InOutBack:
        return EaseInOutBack(start, end, x, totalX);
    case EasingType::InElastic:
        return EaseInElastic(start, end, x, totalX);
    case EasingType::OutElastic:
        return EaseOutElastic(start, end, x, totalX);
    case EasingType::InOutElastic:
        return EaseInOutElastic(start, end, x, totalX);
    case EasingType::InBounce:
        return EaseInBounce(start, end, x, totalX);
    case EasingType::OutBounce:
        return EaseOutBounce(start, end, x, totalX);
    case EasingType::InOutBounce:
        return EaseInOutBounce(start, end, x, totalX);
    default:
        return LerpE(start, end, x / totalX);
    }
}


template <typename T>
T EaseAmplitudeScale(const T &initScale, const float &easeT, const float &totalTime, const float &amplitude, const float &period) {
	T newScale = initScale; // T型のnewScaleを宣言

	if constexpr (std::is_same<T, float>::value) {
		newScale = initScale + -EaseOutElasticAmplitude(easeT, totalTime, amplitude, period);
	}
	else if constexpr (std::is_same<T, Vector2>::value) {
		newScale.x = initScale.x + -EaseOutElasticAmplitude(easeT, totalTime, amplitude, period);
		newScale.y = initScale.y + EaseOutElasticAmplitude(easeT, totalTime, amplitude, period);
	}
	else if constexpr (std::is_same<T, Vector3>::value) {
		newScale.x = initScale.x + -EaseOutElasticAmplitude(easeT, totalTime, amplitude, period);
		newScale.y = initScale.y + EaseOutElasticAmplitude(easeT, totalTime, amplitude, period);
		newScale.z = initScale.z + -EaseOutElasticAmplitude(easeT, totalTime, amplitude, period);
	}

	return newScale;
}

// EaseInSine 関数
template<typename T> T EaseInSine(const T& start, const T& end, float x, float totalX) {
	float t = x / totalX;
	float easeT = 1.0f - std::cosf((t * std::numbers::pi_v<float>) / 2.0f);
	return LerpE(start, end, easeT);
}

// EaseOutSine 関数
template<typename T> T EaseOutSine(const T& start, const T& end, float x, float totalX) {
	float t = x / totalX;
	float easeT = std::sinf((t * std::numbers::pi_v<float>) / 2.0f);
	return LerpE(start, end, easeT);
}

// EaseInOutSine 関数
template<typename T> T EaseInOutSine(const T& start, const T& end, float x, float totalX) {
	float t = x / (totalX / 2.0f);
	float easeT = 0.5f * (1.0f - std::cosf(t * std::numbers::pi_v<float>));
	return LerpE(start, end, easeT);
}

/// EaseInBack
template<typename T>
T EaseInBack(const T& start, const T& end, float x, float totalX) {
	const float s = 1.70158f; // オーバーシュートの量 (調整可能)
	float t = x / totalX;
	float easeT = t * t * ((s + 1) * t - s);
	return LerpE(start, end, easeT);
}

/// EaseOutBack
template<typename T>
T EaseOutBack(const T& start, const T& end, float x, float totalX) {
	const float s = 1.70158f; // オーバーシュートの量 (調整可能)
	float t = x / totalX - 1;
	float easeT = (t * t * ((s + 1) * t + s)) + 1;
	return LerpE(start, end, easeT);
}


/// EaseInOutBack
template<typename T>
T EaseInOutBack(const T& start, const T& end, float x, float totalX) {
	const float s = 1.70158f * 1.525f; // オーバーシュートの量 (調整可能)
	float t = x / (totalX / 2.0f);
	float easeT;

	if (t < 1) {
		easeT = 0.5f * (t * t * ((s + 1) * t - s));
	}
	else {
		t -= 2;
		easeT = 0.5f * ((t * t * ((s + 1) * t + s)) + 2);
	}

	return LerpE(start, end, easeT);
}

// EaseInQuint 関数
template<typename T> T EaseInQuint(const T& start, const T& end, float x, float totalX) {
	float t = x / totalX;
	float easeT = t * t * t * t * t;
	return LerpE(start, end, easeT);
}

// EaseOutQuint 関数
template<typename T> T EaseOutQuint(const T& start, const T& end, float x, float totalX) {
	float t = x / totalX;
	float easeT = 1.0f - std::powf(1.0f - t, 5);
	return LerpE(start, end, easeT);
}

// EaseInOutQuint 関数
template<typename T> T EaseInOutQuint(const T& start, const T& end, float x, float totalX) {
	float t = x / (totalX / 2.0f);
	float easeT;
	if (t < 1.0f) {
		easeT = 0.5f * t * t * t * t * t;
	}
	else {
		t -= 2.0f;
		easeT = 0.5f * (std::powf(t, 5) + 2.0f);
	}
	return LerpE(start, end, easeT);
}

// EaseInCirc 関数
template<typename T> T EaseInCirc(const T& start, const T& end, float x, float totalX) {
	float t = x / totalX;
	float easeT = 1.0f - std::sqrtf(1.0f - std::powf(t, 2));
	return LerpE(start, end, easeT);
}

// EaseOutCirc 関数
template<typename T> T EaseOutCirc(const T& start, const T& end, float x, float totalX) {
	float t = x / totalX;
	float easeT = std::sqrtf(1.0f - std::powf(t - 1.0f, 2));
	return LerpE(start, end, easeT);
}

// EaseInOutCirc 関数
template<typename T> T EaseInOutCirc(const T& start, const T& end, float x, float totalX) {
	float t = x / (totalX / 2.0f);
	float easeT;
	if (t < 1.0f) {
		easeT = -0.5f * (std::sqrtf(1.0f - std::powf(t, 2)) - 1.0f);
	}
	else {
		t -= 2.0f;
		easeT = 0.5f * (std::sqrtf(1.0f - std::powf(t, 2)) + 1.0f);
	}
	return LerpE(start, end, easeT);
}

// EaseInExpo 関数
template<typename T> T EaseInExpo(const T& start, const T& end, float x, float totalX) {
	float t = x / totalX;
	float easeT = (t == 0.0f) ? 0.0f : std::powf(2.0f, 10.0f * (t - 1.0f));
	return LerpE(start, end, easeT);
}

// EaseOutExpo 関数
template<typename T> T EaseOutExpo(const T& start, const T& end, float x, float totalX) {
	float t = x / totalX;
	float easeT = (t == 1.0f) ? 1.0f : 1.0f - std::powf(2.0f, -10.0f * t);
	return LerpE(start, end, easeT);
}

// EaseInOutExpo 関数
template<typename T> T EaseInOutExpo(const T& start, const T& end, float x, float totalX) {
	float t = x / (totalX / 2.0f);
	float easeT;
	if (t < 1.0f) {
		easeT = 0.5f * std::powf(2.0f, 10.0f * (t - 1.0f));
	}
	else {
		t -= 1.0f;
		easeT = 0.5f * (2.0f - std::powf(2.0f, -10.0f * t));
	}
	return LerpE(start, end, easeT);
}

// EaseInCubic 関数
template<typename T> T EaseInCubic(const T& start, const T& end, float x, float totalX) {
	float t = x / totalX;
	float easeT = t * t * t;
	return LerpE(start, end, easeT);
}

// EaseOutCubic 関数
template<typename T> T EaseOutCubic(const T& start, const T& end, float x, float totalX) {
	float t = x / totalX;
	float easeT = 1.0f - std::powf(1.0f - t, 3);
	return LerpE(start, end, easeT);
}

// EaseInOutCubic 関数
template<typename T> T EaseInOutCubic(const T& start, const T& end, float x, float totalX) {
	float t = x / (totalX / 2.0f);
	float easeT;
	if (t < 1.0f) {
		easeT = 0.5f * t * t * t;
	}
	else {
		t -= 2.0f;
		easeT = 0.5f * (std::powf(t, 3) + 2.0f);
	}
	return LerpE(start, end, easeT);
}

// EaseInQuad 関数
template<typename T> T EaseInQuad(const T& start, const T& end, float x, float totalX) {
	float t = x / totalX;
	float easeT = t * t;
	return LerpE(start, end, easeT);
}

// EaseOutQuad 関数
template<typename T> T EaseOutQuad(const T& start, const T& end, float x, float totalX) {
	float t = x / totalX;
	float easeT = 1.0f - (1.0f - t) * (1.0f - t);
	return LerpE(start, end, easeT);
}

// EaseInOutQuad 関数
template<typename T> T EaseInOutQuad(const T& start, const T& end, float x, float totalX) {
	float t = x / (totalX / 2.0f);
	float easeT;
	if (t < 1.0f) {
		easeT = 0.5f * t * t;
	}
	else {
		t -= 1.0f;
		easeT = -0.5f * (t * (t - 2.0f) - 1.0f);
	}
	return LerpE(start, end, easeT);
}

// EaseInQuart 関数
template<typename T> T EaseInQuart(const T& start, const T& end, float x, float totalX) {
	float t = x / totalX;
	float easeT = t * t * t * t;
	return LerpE(start, end, easeT);
}

// EaseOutQuart 関数
template<typename T> T EaseOutQuart(const T& start, const T& end, float x, float totalX) {
	float t = x / totalX;
	float easeT = 1.0f - std::powf(1.0f - t, 4);
	return LerpE(start, end, easeT);
}

// EaseInOutQuart 関数
template<typename T> T EaseInOutQuart(const T& start, const T& end, float x, float totalX) {
	float t = x / (totalX / 2.0f);
	float easeT;
	if (t < 1.0f) {
		easeT = 0.5f * t * t * t * t;
	}
	else {
		t -= 2.0f;
		easeT = -0.5f * (std::powf(t, 4) - 2.0f);
	}
	return LerpE(start, end, easeT);
}

//バウンス補助関数
float BounceEaseOut(float x) {

	const float n1 = 7.5625f;
	const float d1 = 2.75f;
	float easeT = 0.0f;

	if (x < 1.0f / d1) {
		easeT = n1 * x * x;
	}
	else if (x < 2.0f / d1) {
		x -= 1.5f / d1;
		easeT = n1 * x * x + 0.75f;
	}
	else if (x < 2.5f / d1) {
		x -= 2.25f / d1;
		easeT = n1 * x * x + 0.9375f;
	}
	else {
		x -= 2.625f / d1;
		easeT = n1 * x * x + 0.984375f;
	}
	return easeT;
}

// EaseInBounce 関数
template<typename T> T EaseInBounce(const T& start, const T& end, float x, float totalX) {
	float t = 1.0f - (x / totalX);
	float easeT = 1.0f - BounceEaseOut(t);
	return LerpE(start, end, easeT);
}

// EaseOutBounce 関数
template<typename T> T EaseOutBounce(const T& start, const T& end, float x, float totalX) {
	float t = x / totalX;
	float easeT = BounceEaseOut(t);
	return LerpE(start, end, easeT);
}

// EaseInOutBounce 関数
template<typename T> T EaseInOutBounce(const T& start, const T& end, float x, float totalX) {
	float t = x / (totalX / 2.0f);
	float easeT;
	if (t < 1.0f) {
		easeT = 0.5f * (1.0f - BounceEaseOut(1.0f - t));
	}
	else {
		t -= 1.0f;
		easeT = 0.5f * BounceEaseOut(t) + 0.5f;
	}
	return LerpE(start, end, easeT);
}

/// EaseInElastic
template<typename T>
T EaseInElastic(const T& start, const T& end, float x, float totalX) {
	if (x == 0) return start;
	if (x == totalX) return end;

	float t = x / totalX;
	float p = 0.3f; // period
	float s = p / 4.0f; // calculated shift
	float easeT = -std::pow(2.0f, 10.0f * (t - 1)) * std::sin((t - 1 - s) * (2 * std::numbers::pi_v<float>) / p);

	return LerpE(start, end, easeT);
}

/// EaseOutElastic
template<typename T>
T EaseOutElastic(const T& start, const T& end, float x, float totalX) {
	if (x == 0) return start;
	if (x == totalX) return end;

	float t = x / totalX;
	float p = 0.3f; // period
	float s = p / 4.0f; // calculated shift
	float easeT = std::pow(2.0f, -10.0f * t) * std::sin((t - s) * (2 * std::numbers::pi_v<float>) / p) + 1.0f;

	return LerpE(start, end, easeT);
}


/// EaseInOutElastic
template<typename T>
T EaseInOutElastic(const T& start, const T& end, float x, float totalX) {
	if (x == 0) return start;
	if (x == totalX) return end;

	float t = x / (totalX / 2.0f);
	float p = 0.45f; // period
	float s = p / 4.0f; // calculated shift
	float easeT;

	if (t < 1) {
		easeT = -0.5f * std::pow(2.0f, 10.0f * (t - 1)) * std::sin((t - 1 - s) * (2 * std::numbers::pi_v<float>) / p);
	}
	else {
		t -= 1;
		easeT = std::pow(2.0f, -10.0f * t) * std::sin((t - s) * (2 * std::numbers::pi_v<float>) / p) * 0.5f + 1.0f;
	}

	return LerpE(start, end, easeT);
}

//// イージングタイムコントール
//template void EaseTimeControl(float& t, const float& totalTime, const Vector3& start, const Vector3& end);
//template void EaseTimeControl(float& t, const float& totalTime, const Vector2& start, const Vector2& end);
//template void EaseTimeControl(float& t, const float& totalTime, const float& start, const float& end);
// ぷにぷに
template Vector3 EaseAmplitudeScale<Vector3>(const Vector3& initScale, const float& easeT, const float& easeTime, const float& amplitude, const float& period);
template Vector2 EaseAmplitudeScale<Vector2>(const Vector2& initScale, const float& easeT, const float& easeTime, const float& amplitude, const float& period);
template float EaseAmplitudeScale<float>(const float& initScale, const float& easeT, const float& easeTime, const float& amplitude, const float& period);
//*******************************************************************************************************************************************************************
// Sine**************************************************************************************************************************************************************
//*******************************************************************************************************************************************************************

template Vector3 ApplyEasing<Vector3>(EasingType type, const Vector3 &start, const Vector3 &end, float x, float totalX);
template Vector2 ApplyEasing<Vector2>(EasingType type, const Vector2 &start, const Vector2 &end, float x, float totalX);
template float ApplyEasing<float>(EasingType type, const float &start, const float &end, float x, float totalX);
template Quaternion ApplyEasing<Quaternion>(EasingType type, const Quaternion &start, const Quaternion &end, float x, float totalX);

template Vector3 EaseInSine<Vector3>(const Vector3& start, const Vector3& end, float x, float totalX);
template Vector2 EaseInSine<Vector2>(const Vector2& start, const Vector2& end, float x, float totalX);
template float EaseInSine<float>(const float& start, const float& end, float x, float totalX);

template Vector3 EaseOutSine<Vector3>(const Vector3& start, const Vector3& end, float x, float totalX);
template Vector2 EaseOutSine<Vector2>(const Vector2& start, const Vector2& end, float x, float totalX);
template float EaseOutSine<float>(const float& start, const float& end, float x, float totalX);

template Vector3 EaseInOutSine<Vector3>(const Vector3& start, const Vector3& end, float x, float totalX);
template Vector2 EaseInOutSine<Vector2>(const Vector2& start, const Vector2& end, float x, float totalX);
template float EaseInOutSine<float>(const float& start, const float& end, float x, float totalX);

template Vector3 EaseInBack(const Vector3& start, const Vector3& end, float x, float totalX);
template Vector2 EaseInBack(const Vector2& start, const Vector2& end, float x, float totalX);
template float EaseInBack(const float& start, const float& end, float x, float totalX);

template Vector3 EaseOutBack(const Vector3& start, const Vector3& end, float x, float totalX);
template Vector2 EaseOutBack(const Vector2& start, const Vector2& end, float x, float totalX);
template float EaseOutBack(const float& start, const float& end, float x, float totalX);

template Vector3 EaseInOutBack(const Vector3& start, const Vector3& end, float x, float totalX);
template Vector2 EaseInOutBack(const Vector2& start, const Vector2& end, float x, float totalX);
template float EaseInOutBack(const float& start, const float& end, float x, float totalX);

template Vector3 EaseInQuint<Vector3>(const Vector3& start, const Vector3& end, float x, float totalX);
template Vector2 EaseInQuint<Vector2>(const Vector2& start, const Vector2& end, float x, float totalX);
template float EaseInQuint<float>(const float& start, const float& end, float x, float totalX);

template Vector3 EaseOutQuint<Vector3>(const Vector3& start, const Vector3& end, float x, float totalX);
template Vector2 EaseOutQuint<Vector2>(const Vector2& start, const Vector2& end, float x, float totalX);
template float EaseOutQuint<float>(const float& start, const float& end, float x, float totalX);

template Vector3 EaseInOutQuint<Vector3>(const Vector3& start, const Vector3& end, float x, float totalX);
template Vector2 EaseInOutQuint<Vector2>(const Vector2& start, const Vector2& end, float x, float totalX);
template float EaseInOutQuint<float>(const float& start, const float& end, float x, float totalX);

template Vector3 EaseInCirc<Vector3>(const Vector3& start, const Vector3& end, float x, float totalX);
template Vector2 EaseInCirc<Vector2>(const Vector2& start, const Vector2& end, float x, float totalX);
template float EaseInCirc<float>(const float& start, const float& end, float x, float totalX);

template Vector3 EaseOutCirc<Vector3>(const Vector3& start, const Vector3& end, float x, float totalX);
template Vector2 EaseOutCirc<Vector2>(const Vector2& start, const Vector2& end, float x, float totalX);
template float EaseOutCirc<float>(const float& start, const float& end, float x, float totalX);

template Vector3 EaseInOutCirc<Vector3>(const Vector3& start, const Vector3& end, float x, float totalX);
template Vector2 EaseInOutCirc<Vector2>(const Vector2& start, const Vector2& end, float x, float totalX);
template float EaseInOutCirc<float>(const float& start, const float& end, float x, float totalX);

template Vector3 EaseInExpo<Vector3>(const Vector3& start, const Vector3& end, float x, float totalX);
template Vector2 EaseInExpo<Vector2>(const Vector2& start, const Vector2& end, float x, float totalX);
template float EaseInExpo<float>(const float& start, const float& end, float x, float totalX);

template Vector3 EaseOutExpo<Vector3>(const Vector3& start, const Vector3& end, float x, float totalX);
template Vector2 EaseOutExpo<Vector2>(const Vector2& start, const Vector2& end, float x, float totalX);
template float EaseOutExpo<float>(const float& start, const float& end, float x, float totalX);

template Vector3 EaseInOutExpo<Vector3>(const Vector3& start, const Vector3& end, float x, float totalX);
template Vector2 EaseInOutExpo<Vector2>(const Vector2& start, const Vector2& end, float x, float totalX);
template float EaseInOutExpo<float>(const float& start, const float& end, float x, float totalX);

template Vector3 EaseOutCubic<Vector3>(const Vector3& start, const Vector3& end, float x, float totalx);
template Vector2 EaseOutCubic<Vector2>(const Vector2& start, const Vector2& end, float x, float totalx);
template float EaseOutCubic<float>(const float& start, const float& end, float x, float totalx);

template Vector3 EaseInCubic<Vector3>(const Vector3& start, const Vector3& end, float x, float totalx);
template Vector2 EaseInCubic<Vector2>(const Vector2& start, const Vector2& end, float x, float totalx);
template float EaseInCubic<float>(const float& start, const float& end, float x, float totalx);

template Vector3 EaseInOutCubic<Vector3>(const Vector3& start, const Vector3& end, float x, float totalx);
template Vector2 EaseInOutCubic<Vector2>(const Vector2& start, const Vector2& end, float x, float totalx);
template float EaseInOutCubic<float>(const float& start, const float& end, float x, float totalx);

template Vector3 EaseInQuad<Vector3>(const Vector3& start, const Vector3& end, float x, float totalx);
template Vector2 EaseInQuad<Vector2>(const Vector2& start, const Vector2& end, float x, float totalx);
template float EaseInQuad<float>(const float& start, const float& end, float x, float totalx);

template Vector3 EaseOutQuad<Vector3>(const Vector3& start, const Vector3& end, float x, float totalx);
template Vector2 EaseOutQuad<Vector2>(const Vector2& start, const Vector2& end, float x, float totalx);
template float EaseOutQuad<float>(const float& start, const float& end, float x, float totalx);

template Vector3 EaseInOutQuad<Vector3>(const Vector3& start, const Vector3& end, float x, float totalx);
template Vector2 EaseInOutQuad<Vector2>(const Vector2& start, const Vector2& end, float x, float totalx);
template float EaseInOutQuad<float>(const float& start, const float& end, float x, float totalx);

template Vector3 EaseInQuart<Vector3>(const Vector3& start, const Vector3& end, float x, float totalx);
template Vector2 EaseInQuart<Vector2>(const Vector2& start, const Vector2& end, float x, float totalx);
template float EaseInQuart<float>(const float& start, const float& end, float x, float totalx);

template Vector3 EaseOutQuart<Vector3>(const Vector3& start, const Vector3& end, float x, float totalx);
template Vector2 EaseOutQuart<Vector2>(const Vector2& start, const Vector2& end, float x, float totalx);
template float EaseOutQuart<float>(const float& start, const float& end, float x, float totalx);

// バウンス補助関数
float BounceEaseOut(float x);

template Vector3 EaseInBounce<Vector3>(const Vector3& start, const Vector3& end, float x, float totalX);
template Vector2 EaseInBounce<Vector2>(const Vector2& start, const Vector2& end, float x, float totalX);
template float EaseInBounce<float>(const float& start, const float& end, float x, float totalX);

template Vector3 EaseOutBounce<Vector3>(const Vector3& start, const Vector3& end, float x, float totalX);
template Vector2 EaseOutBounce<Vector2>(const Vector2& start, const Vector2& end, float x, float totalX);
template float EaseOutBounce<float>(const float& start, const float& end, float x, float totalX);

template Vector3 EaseInOutBounce<Vector3>(const Vector3& start, const Vector3& end, float x, float totalX);
template Vector2 EaseInOutBounce<Vector2>(const Vector2& start, const Vector2& end, float x, float totalX);
template float EaseInOutBounce<float>(const float& start, const float& end, float x, float totalX);

template Vector3 EaseInElastic<Vector3>(const Vector3& start, const Vector3& end, float x, float totalX);
template Vector2 EaseInElastic<Vector2>(const Vector2& start, const Vector2& end, float x, float totalX);
template float EaseInElastic<float>(const float& start, const float& end, float x, float totalX);

template Vector3 EaseOutElastic<Vector3>(const Vector3& start, const Vector3& end, float x, float totalX);
template Vector2 EaseOutElastic<Vector2>(const Vector2& start, const Vector2& end, float x, float totalX);
template float EaseOutElastic<float>(const float& start, const float& end, float x, float totalX);

template Vector3 EaseInOutElastic<Vector3>(const Vector3& start, const Vector3& end, float x, float totalX);
template Vector2 EaseInOutElastic<Vector2>(const Vector2& start, const Vector2& end, float x, float totalX);
template float EaseInOutElastic<float>(const float& start, const float& end, float x, float totalX);

template Quaternion EaseInSine<Quaternion>(const Quaternion &start, const Quaternion &end, float x, float totalX);
template Quaternion EaseOutSine<Quaternion>(const Quaternion &start, const Quaternion &end, float x, float totalX);
template Quaternion EaseInOutSine<Quaternion>(const Quaternion &start, const Quaternion &end, float x, float totalX);

template Quaternion EaseInQuad<Quaternion>(const Quaternion &start, const Quaternion &end, float x, float totalX);
template Quaternion EaseOutQuad<Quaternion>(const Quaternion &start, const Quaternion &end, float x, float totalX);
template Quaternion EaseInOutQuad<Quaternion>(const Quaternion &start, const Quaternion &end, float x, float totalX);

template Quaternion EaseInCubic<Quaternion>(const Quaternion &start, const Quaternion &end, float x, float totalX);
template Quaternion EaseOutCubic<Quaternion>(const Quaternion &start, const Quaternion &end, float x, float totalX);
template Quaternion EaseInOutCubic<Quaternion>(const Quaternion &start, const Quaternion &end, float x, float totalX);

template Quaternion EaseInQuart<Quaternion>(const Quaternion &start, const Quaternion &end, float x, float totalX);
template Quaternion EaseOutQuart<Quaternion>(const Quaternion &start, const Quaternion &end, float x, float totalX);
template Quaternion EaseInOutQuart<Quaternion>(const Quaternion &start, const Quaternion &end, float x, float totalX);

template Quaternion EaseInQuint<Quaternion>(const Quaternion &start, const Quaternion &end, float x, float totalX);
template Quaternion EaseOutQuint<Quaternion>(const Quaternion &start, const Quaternion &end, float x, float totalX);
template Quaternion EaseInOutQuint<Quaternion>(const Quaternion &start, const Quaternion &end, float x, float totalX);

template Quaternion EaseInCirc<Quaternion>(const Quaternion &start, const Quaternion &end, float x, float totalX);
template Quaternion EaseOutCirc<Quaternion>(const Quaternion &start, const Quaternion &end, float x, float totalX);
template Quaternion EaseInOutCirc<Quaternion>(const Quaternion &start, const Quaternion &end, float x, float totalX);

template Quaternion EaseInExpo<Quaternion>(const Quaternion &start, const Quaternion &end, float x, float totalX);
template Quaternion EaseOutExpo<Quaternion>(const Quaternion &start, const Quaternion &end, float x, float totalX);
template Quaternion EaseInOutExpo<Quaternion>(const Quaternion &start, const Quaternion &end, float x, float totalX);

template Quaternion EaseInBack<Quaternion>(const Quaternion &start, const Quaternion &end, float x, float totalX);
template Quaternion EaseOutBack<Quaternion>(const Quaternion &start, const Quaternion &end, float x, float totalX);
template Quaternion EaseInOutBack<Quaternion>(const Quaternion &start, const Quaternion &end, float x, float totalX);

template Quaternion EaseInElastic<Quaternion>(const Quaternion &start, const Quaternion &end, float x, float totalX);
template Quaternion EaseOutElastic<Quaternion>(const Quaternion &start, const Quaternion &end, float x, float totalX);
template Quaternion EaseInOutElastic<Quaternion>(const Quaternion &start, const Quaternion &end, float x, float totalX);

template Quaternion EaseInBounce<Quaternion>(const Quaternion &start, const Quaternion &end, float x, float totalX);
template Quaternion EaseOutBounce<Quaternion>(const Quaternion &start, const Quaternion &end, float x, float totalX);
template Quaternion EaseInOutBounce<Quaternion>(const Quaternion &start, const Quaternion &end, float x, float totalX);
} // namespace Hagine
