#pragma once
#include "Matrix4x4.h"
#include "Vector3.h"
#include <cmath>
#include <numbers>

namespace Hagine {

/// <summary>
/// クォータニオン（回転を表す四元数）
/// 回転の合成・補間・オイラー角や行列との相互変換などを提供する
/// </summary>
class Quaternion final {
  public:
    float x, y, z, w; // 四元数の各成分

    /// <summary>
    /// コンストラクタ（単位クォータニオンで初期化）
    /// </summary>
    Quaternion() : x(0.0f), y(0.0f), z(0.0f), w(1.0f) {}

    /// <summary>
    /// コンストラクタ（各成分を指定）
    /// </summary>
    Quaternion(float x, float y, float z, float w) : x(x), y(y), z(z), w(w) {}

    /// <summary>
    /// クォータニオン同士の掛け算（回転の合成）
    /// </summary>
    Quaternion operator*(const Quaternion &q) const;

    /// <summary>
    /// クォータニオン同士の加算
    /// </summary>
    Quaternion operator+(const Quaternion &other) const;

    /// <summary>
    /// クォータニオン同士の減算
    /// </summary>
    Quaternion operator-(const Quaternion &other) const;

    /// <summary>
    /// クォータニオン同士の除算
    /// </summary>
    Quaternion operator/(const Quaternion &other) const;

    /// <summary>
    /// スカラーとの掛け算
    /// </summary>
    Quaternion operator*(const float &scalar) const;

    /// <summary>
    /// クォータニオンの加算代入
    /// </summary>
    Quaternion operator+=(const Quaternion &other);

    /// <summary>
    /// クォータニオンの減算代入
    /// </summary>
    Quaternion operator-=(const Quaternion &other);

    /// <summary>
    /// ベクトルを回転して返す
    /// </summary>
    Vector3 operator*(const Vector3 &v) const;

    /// <summary>
    /// 単項マイナス演算子（符号反転）
    /// </summary>
    Quaternion operator-() const;

    /// <summary>
    /// 等価演算子
    /// </summary>
    bool operator==(const Quaternion &other) const;

    /// <summary>
    /// 非等価演算子
    /// </summary>
    bool operator!=(const Quaternion &other) const;

    /// <summary>
    /// 2つのベクトルの間の回転を計算して自身に設定
    /// </summary>
    /// <param name="from">開始方向</param>
    /// <param name="to">目標方向</param>
    void SetFromTo(const Vector3 &from, const Vector3 &to);

    /// <summary>
    /// オイラー角（ラジアン）からクォータニオンを生成
    /// </summary>
    /// <param name="eulerAngles">オイラー角</param>
    /// <returns>Quaternion: 生成された回転</returns>
    static Quaternion FromEulerAngles(const Vector3 &eulerAngles);

    /// <summary>
    /// クォータニオンをオイラー角（ラジアン）に変換
    /// </summary>
    /// <returns>Vector3: オイラー角</returns>
    Vector3 ToEulerAngles() const;
    Vector3 ToEuler() const { return ToEulerAngles(); }

    /// <summary>
    /// クォータニオンの共役を返す
    /// </summary>
    /// <returns>Quaternion: 共役クォータニオン</returns>
    Quaternion Conjugate() const;

    /// <summary>
    /// クォータニオンを正規化して返す
    /// </summary>
    /// <returns>Quaternion: 正規化されたクォータニオン</returns>
    Quaternion Normalize() const;

    /// <summary>
    /// 方向ベクトルと上ベクトルから回転クォータニオンを生成
    /// </summary>
    /// <param name="direction">向く方向</param>
    /// <param name="up">上方向</param>
    /// <returns>Quaternion: 生成された回転</returns>
    static Quaternion FromLookRotation(const Vector3 &direction, const Vector3 &up);

    /// <summary>
    /// 単位クォータニオンを返す
    /// </summary>
    /// <returns>Quaternion: 単位クォータニオン</returns>
    static Quaternion IdentityQuaternion();

    /// <summary>
    /// ノルム（長さ）を計算
    /// </summary>
    /// <returns>float: ノルム</returns>
    float Norm() const;

    /// <summary>
    /// 内積を計算
    /// </summary>
    /// <param name="other">対象のクォータニオン</param>
    /// <returns>float: 内積</returns>
    float Dot(const Quaternion &other) const;

    /// <summary>
    /// 逆クォータニオンを返す
    /// </summary>
    /// <returns>Quaternion: 逆クォータニオン</returns>
    Quaternion Inverse() const;

    /// <summary>
    /// 球面線形補間（Slerp）を計算
    /// </summary>
    /// <param name="q1">開始クォータニオン</param>
    /// <param name="q2">終了クォータニオン</param>
    /// <param name="t">補間係数(0〜1)</param>
    /// <returns>Quaternion: 補間結果</returns>
    static Quaternion Slerp(const Quaternion &q1, const Quaternion &q2, float t);

    /// <summary>
    /// 回転軸を取得
    /// </summary>
    /// <returns>Vector3: 回転軸</returns>
    Vector3 GetAxis() const;

    /// <summary>
    /// 回転角度（ラジアン）を取得
    /// </summary>
    /// <returns>float: 回転角度</returns>
    float GetAngle() const;

    /// <summary>
    /// 軸と角度からクォータニオンを生成
    /// </summary>
    /// <param name="axis">回転軸</param>
    /// <param name="angle">回転角度（ラジアン）</param>
    /// <returns>Quaternion: 生成された回転</returns>
    static Quaternion FromAxisAngle(const Vector3 &axis, float angle);

    /// <summary>
    /// オイラー角（度）からクォータニオンを生成
    /// </summary>
    /// <param name="eulerDegrees">オイラー角（度）</param>
    /// <returns>Quaternion: 生成された回転</returns>
    static Quaternion FromEulerDegrees(const Vector3 &eulerDegrees);

    /// <summary>
    /// クォータニオンをオイラー角（度）に変換
    /// </summary>
    /// <returns>Vector3: オイラー角（度）</returns>
    Vector3 ToEulerDegrees() const;

    /// <summary>
    /// 回転行列からクォータニオンを生成
    /// </summary>
    /// <param name="matrix">回転行列</param>
    /// <returns>Quaternion: 生成された回転</returns>
    static Quaternion FromMatrix(const Matrix4x4 &matrix);

    /// <summary>
    /// ジンバルロックを回避してオイラー角からクォータニオンを生成
    /// </summary>
    /// <param name="eulerAngles">オイラー角</param>
    /// <returns>Quaternion: 生成された回転</returns>
    static Quaternion FromEulerAnglesSafe(const Vector3 &eulerAngles);

    /// <summary>
    /// ジンバルロックを回避してオイラー角に変換
    /// </summary>
    /// <returns>Vector3: オイラー角</returns>
    Vector3 ToEulerAnglesSafe() const;

    /// <summary>
    /// 各軸の回転を個別に適用してクォータニオンを生成
    /// </summary>
    /// <param name="axisRotations">各軸の回転量</param>
    /// <returns>Quaternion: 生成された回転</returns>
    static Quaternion FromAxisRotations(const Vector3 &axisRotations);

    /// <summary>
    /// ベクトルを回転して返す
    /// </summary>
    /// <param name="v">回転対象のベクトル</param>
    /// <returns>Vector3: 回転後のベクトル</returns>
    Vector3 Rotate(const Vector3 &v) const;
};
} // namespace Hagine
