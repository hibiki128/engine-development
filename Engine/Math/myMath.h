#pragma once
#include "assert.h"
#include "cmath"
#include "type/Matrix4x4.h"
#include "type/Vector4.h"
#include <Camera/ViewProjection/ViewProjection.h>
#include <type/Quaternion.h>
#include <type/Vector3.h>

namespace Hagine {

/// <summary>
/// 球の形状データ
/// </summary>
struct Sphere {
    Vector3 center = {0.0f, 0.0f, 0.0f}; //!< 中心点
    float radius = 0.0f;                 //!< 半径
};

/// <summary>
/// 軸平行境界ボックス（AABB）の形状データ
/// </summary>
struct AABB {
    Vector3 min; //!< 最小点
    Vector3 max; //!< 最大点
};

/// <summary>
/// 有向境界ボックス（OBB）の形状データ
/// </summary>
struct OBB {
    Vector3 rotationCenter;     // 回転中心
    Vector3 scaleCenter;        // スケール中心
    Vector3 scaleCenterRotated; // 回転後のスケール中心
    Vector3 size;               // サイズ
    Vector3 orientations[3];    // 各軸の方向ベクトル
};

/// <summary>
/// 三角形の形状データ
/// メッシュコライダーの最小要素として利用する
/// </summary>
struct Triangle {
    Vector3 v[3];   // 3頂点
    Vector3 normal; // 面法線（正規化済みを想定）
};

class ViewProjection;

static const int kColumnWidth = 60;
static const int kRowHeight = 20;

// 線形補間
float Lerp(float _start, float _end, float _t);
Vector3 Lerp(const Vector3 &_start, const Vector3 &_end, float _t);
Vector4 Lerp(const Vector4 &_start, const Vector4 &_end, float _t);

// 平行移動行列
Matrix4x4 MakeTranslateMatrix(const Vector3 &translate);

// 拡大縮小行列
Matrix4x4 MakeScaleMatrix(const Vector3 &scale);

Matrix4x4 MakeOBBWorldMatrix(const OBB &obb, const Matrix4x4 &rotateMatrix);
AABB ConvertOBBToAABB(const OBB &obb);
float getProjection(const Vector3 &axis, const OBB &obb);

// 座標変換
Vector3 Transformation(const Vector3 &vector, const Matrix4x4 &matrix);
Vector4 Transformation(const Vector4 &vector, const Matrix4x4 &matrix);
// Ray生成用の変換関数（正規化しない版）
Vector4 TransformationRaw(const Vector4 &vector, const Matrix4x4 &matrix);

Vector3 TransformNormal(const Vector3 &v, const Matrix4x4 &m);

// 逆行列
Matrix4x4 Inverse(const Matrix4x4 &m);

// 転置行列
Matrix4x4 Transpose(const Matrix4x4 &m);

// 単位行列の作成
Matrix4x4 MakeIdentity4x4();

// X軸回転行列
Matrix4x4 MakeRotateXMatrix(float radian);
// Y軸回転行列
Matrix4x4 MakeRotateYMatrix(float radian);
// Z軸回転行列
Matrix4x4 MakeRotateZMatrix(float radian);
// X,Y,Z軸回転行列を合成した行列
Matrix4x4 MakeRotateXYZMatrix(const Vector3 &radian);

Matrix4x4 MakeAffineMatrix(const Vector3 &scale, const Vector3 &rotate, const Vector3 &translate);

// tanθの逆数
float cotf(float theta);

// 透視投影行列
Matrix4x4 MakePerspectiveFovMatrix(float fovY, float aspectRatio, float nearClip, float farClip);

// 正射影行列
Matrix4x4 MakeOrthographicMatrix(float left, float top, float right, float bottom, float nearClip, float farClip);

// ビューポート変換行列
Matrix4x4 MakeViewPortMatrix(float left, float top, float width, float height, float minDepth, float maxDepth);

// クォータニオンから回転軸(Vector3)を計算する関数

float LerpShortAngle(float a, float b, float t);

// 行列から回転成分をオイラー角に変換して取得
Vector3 GetEulerAnglesFromMatrix(const Matrix4x4 &mat);

Vector3 ScreenTransform(Vector3 worldPos, const ViewProjection &viewProjection);

float radiansToDegrees(float radians);

float degreesToRadians(float degrees);

Vector3 QuaternionToAxis(const Quaternion &q);

Matrix4x4 MakeAffineMatrix(const Vector3 &scale, const Quaternion &rotate, const Vector3 &translate);

Matrix4x4 QuaternionToMatrix4x4(const Quaternion &q);

Quaternion Slerp(const Quaternion &q0, const Quaternion &q1, float t);

Matrix4x4 MakeRotateXYZMatrix(const Quaternion &quat);
Matrix4x4 MakeRotateMatrix(const Vector3 &right, const Vector3 &up, const Vector3 &forward);

Matrix4x4 MakeBoneMatrix(const Vector3 &scale, const Quaternion &rotate, const Vector3 &translate);

Matrix4x4 QuaternionToBoneMatrix(const Quaternion &q);

// Matrix4x4からクォータニオンを抽出する関数
Quaternion MatrixToQuaternion(const Matrix4x4 &matrix);
// クォータニオンでベクトルを回転する関数
Vector3 RotateVector(const Vector3 &vec, const Quaternion &quat);

Vector3 ExtractScale(const Matrix4x4 &matrix);
//// デバッグ用
// void VectorScreenPrintf(int x, int y, const Vector3& vector, const char* label);
// void MatrixScreenPrintf(int x, int y, const Matrix4x4& matrix, const char* label);
} // namespace Hagine
