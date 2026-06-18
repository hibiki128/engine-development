#include"MyMath.h"
#include <numbers>
#include <WinApp.h>

namespace Hagine {
float Lerp(float _start, float _end, float _t)
{
	return (1.0f - _t) * _start + _end * _t;
}

Vector3 Lerp(const Vector3& _start, const Vector3& _end, float _t)
{
	Vector3 result;
	result.x = (1.0f - _t) * _start.x + _end.x * _t;
	result.y = (1.0f - _t) * _start.y + _end.y * _t;
	result.z = (1.0f - _t) * _start.z + _end.z * _t;
	return result;
}

Vector4 Lerp(const Vector4& _start, const Vector4& _end, float _t)
{
	Vector4 result;
	result.x = (1.0f - _t) * _start.x + _end.x * _t;
	result.y = (1.0f - _t) * _start.y + _end.y * _t;
	result.z = (1.0f - _t) * _start.z + _end.z * _t;
	result.w = (1.0f - _t) * _start.w + _end.w * _t;
	return result;
}
Matrix4x4 MakeTranslateMatrix(const Vector3& translate) { return { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, translate.x, translate.y, translate.z, 1 }; }

Matrix4x4 MakeScaleMatrix(const Vector3& scale) { return { scale.x, 0, 0, 0, 0, scale.y, 0, 0, 0, 0, scale.z, 0, 0, 0, 0, 1 }; }

Matrix4x4 MakeOBBWorldMatrix(const OBB& obb, const Matrix4x4& rotateMatrix) {
	Matrix4x4 translationMatrix = MakeTranslateMatrix(obb.scaleCenterRotated);
	return rotateMatrix * translationMatrix;
}

AABB ConvertOBBToAABB(const OBB& obb) {
	AABB aabb;
	aabb.min = { -obb.size.x, -obb.size.y, -obb.size.z };
	aabb.max = { obb.size.x, obb.size.y, obb.size.z };
	return aabb;
}

// 分離軸に対するOBBの射影範囲を計算
float getProjection(const Vector3& axis, const OBB& obb) {
	return (obb.size.x * abs(axis.Dot(obb.orientations[0])) +
		obb.size.y * abs(axis.Dot(obb.orientations[1])) +
		obb.size.z * abs(axis.Dot(obb.orientations[2])));
}

Vector3 Transformation(const Vector3& vector, const Matrix4x4& matrix) {
	Vector3 result;
	result.x = vector.x * matrix.m[0][0] + vector.y * matrix.m[1][0] + vector.z * matrix.m[2][0] + 1.0f * matrix.m[3][0];
	result.y = vector.x * matrix.m[0][1] + vector.y * matrix.m[1][1] + vector.z * matrix.m[2][1] + 1.0f * matrix.m[3][1];
	result.z = vector.x * matrix.m[0][2] + vector.y * matrix.m[1][2] + vector.z * matrix.m[2][2] + 1.0f * matrix.m[3][2];
	float w = vector.x * matrix.m[0][3] + vector.y * matrix.m[1][3] + vector.z * matrix.m[2][3] + 1.0f * matrix.m[3][3];
	assert(w != 0.0f);
	result.x /= w;
	result.y /= w;
	result.z /= w;
	return result;
}

// Vector4をMatrix4x4で変換する関数
Vector4 Transformation(const Vector4& vector, const Matrix4x4& matrix) {
	Vector4 result;
	result.x = vector.x * matrix.m[0][0] + vector.y * matrix.m[1][0] + vector.z * matrix.m[2][0] + vector.w * matrix.m[3][0];
	result.y = vector.x * matrix.m[0][1] + vector.y * matrix.m[1][1] + vector.z * matrix.m[2][1] + vector.w * matrix.m[3][1];
	result.z = vector.x * matrix.m[0][2] + vector.y * matrix.m[1][2] + vector.z * matrix.m[2][2] + vector.w * matrix.m[3][2];
	result.w = vector.x * matrix.m[0][3] + vector.y * matrix.m[1][3] + vector.z * matrix.m[2][3] + vector.w * matrix.m[3][3];

	// wが0でないことを確認
	assert(result.w != 0.0f);

	// 正規化
	result.x /= result.w;
	result.y /= result.w;
	result.z /= result.w;
	result.w = 1.0f; // 正規化後のwは1.0f

	return result;
}

// Ray生成用の変換関数（正規化しない版）
Vector4 TransformationRaw(const Vector4 &vector, const Matrix4x4 &matrix) {
    Vector4 result;
    result.x = vector.x * matrix.m[0][0] + vector.y * matrix.m[1][0] + vector.z * matrix.m[2][0] + vector.w * matrix.m[3][0];
    result.y = vector.x * matrix.m[0][1] + vector.y * matrix.m[1][1] + vector.z * matrix.m[2][1] + vector.w * matrix.m[3][1];
    result.z = vector.x * matrix.m[0][2] + vector.y * matrix.m[1][2] + vector.z * matrix.m[2][2] + vector.w * matrix.m[3][2];
    result.w = vector.x * matrix.m[0][3] + vector.y * matrix.m[1][3] + vector.z * matrix.m[2][3] + vector.w * matrix.m[3][3];
    return result;
}

Vector3 TransformNormal(const Vector3& v, const Matrix4x4& m) {
	Vector3 result{
		v.x * m.m[0][0] + v.y * m.m[1][0] + v.z * m.m[2][0],
		v.x * m.m[0][1] + v.y * m.m[1][1] + v.z * m.m[2][1],
		v.x * m.m[0][2] + v.y * m.m[1][2] + v.z * m.m[2][2],
	};

	return result;
}

Matrix4x4 Inverse(const Matrix4x4& m) {
	float A =
		m.m[0][0] * m.m[1][1] * m.m[2][2] * m.m[3][3] + m.m[0][0] * m.m[1][2] * m.m[2][3] * m.m[3][1] + m.m[0][0] * m.m[1][3] * m.m[2][1] * m.m[3][2] - m.m[0][0] * m.m[1][3] * m.m[2][2] * m.m[3][1] -
		m.m[0][0] * m.m[1][2] * m.m[2][1] * m.m[3][3] - m.m[0][0] * m.m[1][1] * m.m[2][3] * m.m[3][2] - m.m[0][1] * m.m[1][0] * m.m[2][2] * m.m[3][3] - m.m[0][2] * m.m[1][0] * m.m[2][3] * m.m[3][1] -
		m.m[0][3] * m.m[1][0] * m.m[2][1] * m.m[3][2] + m.m[0][3] * m.m[1][0] * m.m[2][2] * m.m[3][1] + m.m[0][2] * m.m[1][0] * m.m[2][1] * m.m[3][3] + m.m[0][1] * m.m[1][0] * m.m[2][3] * m.m[3][2] +
		m.m[0][1] * m.m[1][2] * m.m[2][0] * m.m[3][3] + m.m[0][2] * m.m[1][3] * m.m[2][0] * m.m[3][1] + m.m[0][3] * m.m[1][1] * m.m[2][0] * m.m[3][2] - m.m[0][3] * m.m[1][2] * m.m[2][0] * m.m[3][1] -
		m.m[0][2] * m.m[1][1] * m.m[2][0] * m.m[3][3] - m.m[0][1] * m.m[1][3] * m.m[2][0] * m.m[3][2] - m.m[0][1] * m.m[1][2] * m.m[2][3] * m.m[3][0] - m.m[0][2] * m.m[1][3] * m.m[2][1] * m.m[3][0] -
		m.m[0][3] * m.m[1][1] * m.m[2][2] * m.m[3][0] + m.m[0][3] * m.m[1][2] * m.m[2][1] * m.m[3][0] + m.m[0][2] * m.m[1][1] * m.m[2][3] * m.m[3][0] + m.m[0][1] * m.m[1][3] * m.m[2][2] * m.m[3][0];

	return {
		(m.m[1][1] * m.m[2][2] * m.m[3][3] + m.m[1][2] * m.m[2][3] * m.m[3][1] + m.m[1][3] * m.m[2][1] * m.m[3][2] - m.m[1][3] * m.m[2][2] * m.m[3][1] - m.m[1][2] * m.m[2][1] * m.m[3][3] -
		 m.m[1][1] * m.m[2][3] * m.m[3][2]) /
			A,

		(-m.m[0][1] * m.m[2][2] * m.m[3][3] - m.m[0][2] * m.m[2][3] * m.m[3][1] - m.m[0][3] * m.m[2][1] * m.m[3][2] + m.m[0][3] * m.m[2][2] * m.m[3][1] + m.m[0][2] * m.m[2][1] * m.m[3][3] +
		 m.m[0][1] * m.m[2][3] * m.m[3][2]) /
			A,

		(m.m[0][1] * m.m[1][2] * m.m[3][3] + m.m[0][2] * m.m[1][3] * m.m[3][1] + m.m[0][3] * m.m[1][1] * m.m[3][2] - m.m[0][3] * m.m[1][2] * m.m[3][1] - m.m[0][2] * m.m[1][1] * m.m[3][3] -
		 m.m[0][1] * m.m[1][3] * m.m[3][2]) /
			A,

		(-m.m[0][1] * m.m[1][2] * m.m[2][3] - m.m[0][2] * m.m[1][3] * m.m[2][1] - m.m[0][3] * m.m[1][1] * m.m[2][2] + m.m[0][3] * m.m[1][2] * m.m[2][1] + m.m[0][2] * m.m[1][1] * m.m[2][3] +
		 m.m[0][1] * m.m[1][3] * m.m[2][2]) /
			A,

		(-m.m[1][0] * m.m[2][2] * m.m[3][3] - m.m[1][2] * m.m[2][3] * m.m[3][0] - m.m[1][3] * m.m[2][0] * m.m[3][2] + m.m[1][3] * m.m[2][2] * m.m[3][0] + m.m[1][2] * m.m[2][0] * m.m[3][3] +
		 m.m[1][0] * m.m[2][3] * m.m[3][2]) /
			A,

		(m.m[0][0] * m.m[2][2] * m.m[3][3] + m.m[0][2] * m.m[2][3] * m.m[3][0] + m.m[0][3] * m.m[2][0] * m.m[3][2] - m.m[0][3] * m.m[2][2] * m.m[3][0] - m.m[0][2] * m.m[2][0] * m.m[3][3] -
		 m.m[0][0] * m.m[2][3] * m.m[3][2]) /
			A,

		(-m.m[0][0] * m.m[1][2] * m.m[3][3] - m.m[0][2] * m.m[1][3] * m.m[3][0] - m.m[0][3] * m.m[1][0] * m.m[3][2] + m.m[0][3] * m.m[1][2] * m.m[3][0] + m.m[0][2] * m.m[1][0] * m.m[3][3] +
		 m.m[0][0] * m.m[1][3] * m.m[3][2]) /
			A,

		(m.m[0][0] * m.m[1][2] * m.m[2][3] + m.m[0][2] * m.m[1][3] * m.m[2][0] + m.m[0][3] * m.m[1][0] * m.m[2][2] - m.m[0][3] * m.m[1][2] * m.m[2][0] - m.m[0][1] * m.m[1][0] * m.m[2][3] -
		 m.m[0][0] * m.m[1][3] * m.m[2][2]) /
			A,

		(m.m[1][0] * m.m[2][1] * m.m[3][3] + m.m[1][1] * m.m[2][3] * m.m[3][0] + m.m[1][3] * m.m[2][0] * m.m[3][1] - m.m[1][3] * m.m[2][1] * m.m[3][0] - m.m[1][1] * m.m[2][0] * m.m[3][3] -
		 m.m[1][0] * m.m[2][3] * m.m[3][1]) /
			A,

		(-m.m[0][0] * m.m[2][1] * m.m[3][3] - m.m[0][1] * m.m[2][3] * m.m[3][0] - m.m[0][3] * m.m[2][0] * m.m[3][1] + m.m[0][3] * m.m[2][1] * m.m[3][0] + m.m[0][1] * m.m[2][0] * m.m[3][3] +
		 m.m[0][0] * m.m[2][3] * m.m[3][1]) /
			A,

		(m.m[0][0] * m.m[1][1] * m.m[3][3] + m.m[0][1] * m.m[1][3] * m.m[3][0] + m.m[0][3] * m.m[1][0] * m.m[3][1] - m.m[0][3] * m.m[1][1] * m.m[3][0] - m.m[0][1] * m.m[1][0] * m.m[3][3] -
		 m.m[0][0] * m.m[1][3] * m.m[3][1]) /
			A,

		(-m.m[0][0] * m.m[1][1] * m.m[2][3] - m.m[0][1] * m.m[1][3] * m.m[2][0] - m.m[0][3] * m.m[1][0] * m.m[2][1] + m.m[0][3] * m.m[1][1] * m.m[2][0] + m.m[0][1] * m.m[1][0] * m.m[2][3] +
		 m.m[0][0] * m.m[1][3] * m.m[2][1]) /
			A,

		(-m.m[1][0] * m.m[2][1] * m.m[3][2] - m.m[1][1] * m.m[2][2] * m.m[3][0] - m.m[1][2] * m.m[2][0] * m.m[3][1] + m.m[1][2] * m.m[2][1] * m.m[3][0] + m.m[1][1] * m.m[2][0] * m.m[3][2] +
		 m.m[1][0] * m.m[2][2] * m.m[3][1]) /
			A,

		(m.m[0][0] * m.m[2][1] * m.m[3][2] + m.m[0][1] * m.m[2][2] * m.m[3][0] + m.m[0][2] * m.m[2][0] * m.m[3][1] - m.m[0][2] * m.m[2][1] * m.m[3][0] - m.m[0][1] * m.m[2][0] * m.m[3][2] -
		 m.m[0][0] * m.m[2][2] * m.m[3][1]) /
			A,

		(-m.m[0][0] * m.m[1][1] * m.m[3][2] - m.m[0][1] * m.m[1][2] * m.m[3][0] - m.m[0][2] * m.m[1][0] * m.m[3][1] + m.m[0][2] * m.m[1][1] * m.m[3][0] + m.m[0][1] * m.m[1][0] * m.m[3][2] +
		 m.m[0][0] * m.m[1][2] * m.m[3][1]) /
			A,

		(m.m[0][0] * m.m[1][1] * m.m[2][2] + m.m[0][1] * m.m[1][2] * m.m[2][0] + m.m[0][2] * m.m[1][0] * m.m[2][1] - m.m[0][2] * m.m[1][1] * m.m[2][0] - m.m[0][1] * m.m[1][0] * m.m[2][2] -
		 m.m[0][0] * m.m[1][2] * m.m[2][1]) /
			A

	};
}

Matrix4x4 Transpose(const Matrix4x4& m) {
	return { m.m[0][0], m.m[1][0], m.m[2][0], m.m[3][0], m.m[0][1], m.m[1][1], m.m[2][1], m.m[3][1], m.m[0][2], m.m[1][2], m.m[2][2], m.m[3][2], m.m[0][3], m.m[1][3], m.m[2][3], m.m[3][3] };
}

Matrix4x4 MakeIdentity4x4() { return { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 }; }

Matrix4x4 MakeRotateXMatrix(float radian) { return { 1, 0, 0, 0, 0, std::cosf(radian), std::sinf(radian), 0, 0, std::sinf(-radian), std::cosf(radian), 0, 0, 0, 0, 1 }; };

Matrix4x4 MakeRotateYMatrix(float radian) { return { std::cosf(radian), 0, std::sinf(-radian), 0, 0, 1, 0, 0, std::sinf(radian), 0, std::cosf(radian), 0, 0, 0, 0, 1 }; };

Matrix4x4 MakeRotateZMatrix(float radian) { return { std::cosf(radian), std::sinf(radian), 0, 0, std::sinf(-radian), std::cosf(radian), 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 }; };

Matrix4x4 MakeRotateXYZMatrix(const Vector3& radian) { return { (MakeRotateXMatrix(radian.x) * MakeRotateYMatrix(radian.y) * MakeRotateZMatrix(radian.z)) }; }

Matrix4x4 MakeAffineMatrix(const Vector3& scale, const Vector3& rotate, const Vector3& translate) {
	Matrix4x4 scaleMatrix = MakeScaleMatrix(scale);
	Matrix4x4 rotateXMatrix = MakeRotateXMatrix(rotate.x);
	Matrix4x4 rotateYMatrix = MakeRotateYMatrix(rotate.y);
	Matrix4x4 rotateZMatrix = MakeRotateZMatrix(rotate.z);
	Matrix4x4 translateMatrix = MakeTranslateMatrix(translate);
	Matrix4x4 rotateMatrix = (rotateXMatrix * rotateYMatrix) * rotateZMatrix;

	return { (scaleMatrix * rotateMatrix) * translateMatrix };
}

float cotf(float theta) { return 1.0f / std::tanf(theta); }

Matrix4x4 MakePerspectiveFovMatrix(float fovY, float aspectRatio, float nearClip, float farClip) {
	return { 1.0f / aspectRatio * cotf(fovY / 2.0f), 0, 0, 0, 0, cotf(fovY / 2.0f), 0, 0, 0, 0, farClip / (farClip - nearClip), 1.0f, 0, 0, -nearClip * farClip / (farClip - nearClip), 0 };
};

Matrix4x4 MakeOrthographicMatrix(float left, float top, float right, float bottom, float nearClip, float farClip) {
	return { 2.0f / (right - left),           0,   0, 0, 0, 2.0f / (top - bottom), 0, 0, 0, 0, 1.0f / (farClip - nearClip), 0, (left + right) / (left - right), (top + bottom) / (bottom - top),
			nearClip / (nearClip - farClip), 1.0f };
};

Matrix4x4 MakeViewPortMatrix(float left, float top, float width, float height, float minDepth, float maxDepth) {
	return { width / 2.0f, 0, 0, 0, 0, -height / 2.0f, 0, 0, 0, 0, maxDepth - minDepth, 0, left + width / 2.0f, top + height / 2.0f, minDepth, 1.0f };
}



float LerpShortAngle(float a, float b, float t) {
	// 角度差分を求める
	float diff = b - a;
	float pi = 3.141592f;
	// 角度を[-2PI,+2PI]に補正する
	diff = std::fmodf(diff, 2.0f * pi);
	// 角度を[-PI,PI]に補正する
	if (diff > pi) {
		diff -= 2.0f * pi;
	}
	else if (diff < -pi) {
		diff += 2.0f * pi;
	}
	return a + diff * t;
}

// 行列から回転成分をオイラー角に変換して取得
Vector3 GetEulerAnglesFromMatrix(const Matrix4x4& mat) {
	Vector3 eulerAngles;

	// Gimbal lock を考慮しながらオイラー角を計算
	if (std::abs(mat.m[2][0]) < 1.0f) {
		// 通常の場合
		eulerAngles.x = std::atan2(-mat.m[2][1], mat.m[2][2]); // Pitch (X軸回転)
		eulerAngles.y = std::asin(mat.m[2][0]);                // Yaw (Y軸回転)
		eulerAngles.z = std::atan2(-mat.m[1][0], mat.m[0][0]); // Roll (Z軸回転)
	}
	else {
		// Gimbal lock の場合
		eulerAngles.x = std::atan2(mat.m[1][2], mat.m[1][1]);
		eulerAngles.y = (mat.m[2][0] > 0.0f) ? std::numbers::pi_v<float> / 2 : -std::numbers::pi_v<float> / 2;
		eulerAngles.z = 0.0f; // 任意の値が取れるため0に設定
	}

	return eulerAngles;
}

float radiansToDegrees(float radians) {
	return radians * (180.0f / std::numbers::pi_v<float>);
}

float degreesToRadians(float degrees) {
	return degrees * (std::numbers::pi_v<float> / 180.0f);
}

//
//void VectorScreenPrintf(int x, int y, const Vector3& vector, const char* label) {
//	Novice::ScreenPrintf(x, y, "%.02f", vector.x);
//	Novice::ScreenPrintf(x + kColumnWidth, y, "%.02f", vector.y);
//	Novice::ScreenPrintf(x + kColumnWidth * 2, y, "%.02f", vector.z);
//	Novice::ScreenPrintf(x + kColumnWidth * 3, y, "%s", label);
//}
//
//void MatrixScreenPrintf(int x, int y, const Matrix4x4& matrix, const char* label) {
//	Novice::ScreenPrintf(x, y, "%s", label);
//	for (int row = 0; row < 4; ++row) {
//		for (int column = 0; column < 4; ++column) {
//			Novice::ScreenPrintf(x + column * kColumnWidth, y + (1 + row) * kRowHeight, "%6.02f", matrix.m[row][column]);
//		}
//	}
//}

Vector3 ScreenTransform(Vector3 worldPos, const ViewProjection& viewProjection) {
	//ビューポート行列
	Matrix4x4 matViewport = MakeViewPortMatrix(0, 0, float(WinApp::kClientWidth), float(WinApp::kClientHeight), 0, 1);
	//ビュー行列とプロジェクション行列、ビューポート行列を合成する
	Matrix4x4 matViewProjectionViewport = viewProjection.matView_ * viewProjection.matProjection_ * matViewport;
	//ワールド→スクリーン変換
	return Transformation(worldPos, matViewProjectionViewport);
}

Vector3 QuaternionToAxis(const Quaternion &q) {
    return q.GetAxis();
}

Matrix4x4 MakeAffineMatrix(const Vector3 &scale, const Quaternion &rotate, const Vector3 &translate) {
    return MakeScaleMatrix(scale) * QuaternionToMatrix4x4(rotate) * MakeTranslateMatrix(translate);
}

Matrix4x4 MakeBoneMatrix(const Vector3 &scale, const Quaternion &rotate, const Vector3 &translate) {
    return MakeScaleMatrix(scale) * QuaternionToBoneMatrix(rotate) * MakeTranslateMatrix(translate);
}

Matrix4x4 QuaternionToBoneMatrix(const Quaternion &q) {
    Matrix4x4 mat;

    // クォータニオンの各成分の積を計算
    float xx = q.x * q.x;
    float yy = q.y * q.y;
    float zz = q.z * q.z;
    float xy = q.x * q.y;
    float xz = q.x * q.z;
    float yz = q.y * q.z;
    float wx = q.w * q.x;
    float wy = q.w * q.y;
    float wz = q.w * q.z;

    // 左手座標系用の回転行列を設定
    mat.m[0][0] = 1.0f - 2.0f * (yy + zz);
    mat.m[0][1] = 2.0f * (xy + wz);
    mat.m[0][2] = 2.0f * (xz - wy);
    mat.m[0][3] = 0.0f;

    mat.m[1][0] = 2.0f * (xy - wz);
    mat.m[1][1] = 1.0f - 2.0f * (xx + zz);
    mat.m[1][2] = 2.0f * (yz + wx);
    mat.m[1][3] = 0.0f;

    mat.m[2][0] = 2.0f * (xz + wy);
    mat.m[2][1] = 2.0f * (yz - wx);
    mat.m[2][2] = 1.0f - 2.0f * (xx + yy);
    mat.m[2][3] = 0.0f;

    mat.m[3][0] = 0.0f;
    mat.m[3][1] = 0.0f;
    mat.m[3][2] = 0.0f;
    mat.m[3][3] = 1.0f;

    return mat;
}

Matrix4x4 QuaternionToMatrix4x4(const Quaternion &q) {
      // 正規化されたクォータニオンを使用
    Quaternion norm = q.Normalize();

    float xx = norm.x * norm.x;
    float yy = norm.y * norm.y;
    float zz = norm.z * norm.z;
    float xy = norm.x * norm.y;
    float xz = norm.x * norm.z;
    float yz = norm.y * norm.z;
    float wx = norm.w * norm.x;
    float wy = norm.w * norm.y;
    float wz = norm.w * norm.z;

    Matrix4x4 mat;

    // 正しい回転行列の計算
    mat.m[0][0] = 1.0f - 2.0f * (yy + zz);
    mat.m[0][1] = 2.0f * (xy - wz);
    mat.m[0][2] = 2.0f * (xz + wy);
    mat.m[0][3] = 0.0f;

    mat.m[1][0] = 2.0f * (xy + wz);
    mat.m[1][1] = 1.0f - 2.0f * (xx + zz);
    mat.m[1][2] = 2.0f * (yz - wx);
    mat.m[1][3] = 0.0f;

    mat.m[2][0] = 2.0f * (xz - wy);
    mat.m[2][1] = 2.0f * (yz + wx);
    mat.m[2][2] = 1.0f - 2.0f * (xx + yy);
    mat.m[2][3] = 0.0f;

    mat.m[3][0] = 0.0f;
    mat.m[3][1] = 0.0f;
    mat.m[3][2] = 0.0f;
    mat.m[3][3] = 1.0f;

    return mat;
}

Quaternion Slerp(const Quaternion &q0, const Quaternion &q1, float t) {
    return Quaternion::Slerp(q0, q1, t);
}

Matrix4x4 MakeRotateXYZMatrix(const Quaternion &quat) {
    // QuaternionToMatrix4x4と同じ実装
    return QuaternionToMatrix4x4(quat);
}

Matrix4x4 MakeRotateMatrix(const Vector3 &right, const Vector3 &up, const Vector3 &forward) {
    Matrix4x4 result;

    // 回転行列の各成分を設定
    result.m[0][0] = right.x;
    result.m[0][1] = right.y;
    result.m[0][2] = right.z;
    result.m[0][3] = 0.0f;
    result.m[1][0] = up.x;
    result.m[1][1] = up.y;
    result.m[1][2] = up.z;
    result.m[1][3] = 0.0f;
    result.m[2][0] = forward.x;
    result.m[2][1] = forward.y;
    result.m[2][2] = forward.z;
    result.m[2][3] = 0.0f;
    result.m[3][0] = 0.0f;
    result.m[3][1] = 0.0f;
    result.m[3][2] = 0.0f;
    result.m[3][3] = 1.0f;

    return result;
}

// Matrix4x4からクォータニオンを抽出する関数
Quaternion MatrixToQuaternion(const Matrix4x4 &matrix) {
    Quaternion q;
    float trace = matrix.m[0][0] + matrix.m[1][1] + matrix.m[2][2];

    if (trace > 0.0f) {
        float s = sqrt(trace + 1.0f);
        q.w = s * 0.5f;
        s = 0.5f / s;
        q.x = (matrix.m[2][1] - matrix.m[1][2]) * s;
        q.y = (matrix.m[0][2] - matrix.m[2][0]) * s;
        q.z = (matrix.m[1][0] - matrix.m[0][1]) * s;
    } else {
        if (matrix.m[0][0] >= matrix.m[1][1] && matrix.m[0][0] >= matrix.m[2][2]) {
            float s = sqrt(1.0f + matrix.m[0][0] - matrix.m[1][1] - matrix.m[2][2]) * 2.0f;
            q.w = (matrix.m[2][1] - matrix.m[1][2]) / s;
            q.x = 0.25f * s;
            q.y = (matrix.m[0][1] + matrix.m[1][0]) / s;
            q.z = (matrix.m[0][2] + matrix.m[2][0]) / s;
        } else if (matrix.m[1][1] > matrix.m[2][2]) {
            float s = sqrt(1.0f + matrix.m[1][1] - matrix.m[0][0] - matrix.m[2][2]) * 2.0f;
            q.w = (matrix.m[0][2] - matrix.m[2][0]) / s;
            q.x = (matrix.m[0][1] + matrix.m[1][0]) / s;
            q.y = 0.25f * s;
            q.z = (matrix.m[1][2] + matrix.m[2][1]) / s;
        } else {
            float s = sqrt(1.0f + matrix.m[2][2] - matrix.m[0][0] - matrix.m[1][1]) * 2.0f;
            q.w = (matrix.m[1][0] - matrix.m[0][1]) / s;
            q.x = (matrix.m[0][2] + matrix.m[2][0]) / s;
            q.y = (matrix.m[1][2] + matrix.m[2][1]) / s;
            q.z = 0.25f * s;
        }
    }
    return q;
}

// クォータニオンでベクトルを回転する関数
Vector3 RotateVector(const Vector3 &vec, const Quaternion &quat) {
    Quaternion vecQuat = {vec.x, vec.y, vec.z, 0.0f};
    Quaternion result = quat * vecQuat * (quat).Conjugate();
    return Vector3(result.x, result.y, result.z);
}

Vector3 ExtractScale(const Matrix4x4 &matrix) {
    Vector3 scale;
    scale.x = sqrt(matrix.m[0][0] * matrix.m[0][0] + matrix.m[0][1] * matrix.m[0][1] + matrix.m[0][2] * matrix.m[0][2]);
    scale.y = sqrt(matrix.m[1][0] * matrix.m[1][0] + matrix.m[1][1] * matrix.m[1][1] + matrix.m[1][2] * matrix.m[1][2]);
    scale.z = sqrt(matrix.m[2][0] * matrix.m[2][0] + matrix.m[2][1] * matrix.m[2][1] + matrix.m[2][2] * matrix.m[2][2]);
    return scale;
}
} // namespace Hagine
