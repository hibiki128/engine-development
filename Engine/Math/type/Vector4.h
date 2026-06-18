#pragma once

#include "cmath"
#ifdef _DEBUG
#include "imgui.h"
#endif // _DEBUG
/// <summary>
/// 4次元ベクトル
/// </summary>
namespace Hagine {
struct Vector4 final {
    float x;
    float y;
    float z;
    float w;

    // コンストラクタ
    Vector4(float _x = 0.0f, float _y = 0.0f, float _z = 0.0f, float _w = 0.0f) : x(_x), y(_y), z(_z), w(_w) {}

    // 符号反転
    Vector4 operator-() const { return Vector4(-x, -y, -z, -w); }

    // 加算
    Vector4 operator+(const Vector4 &obj) const { return Vector4(x + obj.x, y + obj.y, z + obj.z, w + obj.w); }
    // 減算
    Vector4 operator-(const Vector4 &obj) const { return Vector4(x - obj.x, y - obj.y, z - obj.z, w - obj.w); }
    // 乗算
    Vector4 operator*(const Vector4 &obj) const { return Vector4(x * obj.x, y * obj.y, z * obj.z, w * obj.w); }
    // 乗算(スカラー倍)(float型)
    Vector4 operator*(const float &scalar) const { return Vector4(x * scalar, y * scalar, z * scalar, w * scalar); }
    // 乗算(スカラー倍)(int型)
    Vector4 operator*(const int &scalar) const { return Vector4(x * scalar, y * scalar, z * scalar, w * scalar); }
    // 除算
    Vector4 operator/(const Vector4 &obj) const { return Vector4(x / obj.x, y / obj.y, z / obj.z, w / obj.w); }
    // 除算(スカラー)(float型)
    Vector4 operator/(const float &scalar) const { return Vector4(x / scalar, y / scalar, z / scalar, w / scalar); }
    // 除算(スカラー)(int型)
    Vector4 operator/(const int &scalar) const { return Vector4(x / scalar, y / scalar, z / scalar, w / scalar); }

    friend Vector4 operator*(const float &scalar, const Vector4 &vec) { return vec * scalar; }
    friend Vector4 operator/(const float &scalar, const Vector4 &vec) { return Vector4(scalar / vec.x, scalar / vec.y, scalar / vec.z, scalar / vec.w); }
    friend Vector4 operator*(const int &scalar, const Vector4 &vec) { return vec * scalar; }
    friend Vector4 operator/(const int &scalar, const Vector4 &vec) { return Vector4(scalar / vec.x, scalar / vec.y, scalar / vec.z, scalar / vec.w); }

    // +=
    Vector4 &operator+=(const Vector4 &other) {
        x += other.x;
        y += other.y;
        z += other.z;
        w += other.w;
        return *this;
    }
    // -=
    Vector4 &operator-=(const Vector4 &other) {
        x -= other.x;
        y -= other.y;
        z -= other.z;
        w -= other.w;
        return *this;
    }
    // *=
    Vector4 &operator*=(const Vector4 &other) {
        x *= other.x;
        y *= other.y;
        z *= other.z;
        w *= other.w;
        return *this;
    }
    // /=
    Vector4 &operator/=(const Vector4 &other) {
        x /= other.x;
        y /= other.y;
        z /= other.z;
        w /= other.w;
        return *this;
    }
    // スカラー倍の+=
    Vector4 &operator+=(const float &s) {
        x += s;
        y += s;
        z += s;
        w += s;
        return *this;
    }
    // スカラー倍の-=
    Vector4 &operator-=(const float &s) {
        x -= s;
        y -= s;
        z -= s;
        w -= s;
        return *this;
    }
    // スカラー倍の*=
    Vector4 &operator*=(const float &s) {
        x *= s;
        y *= s;
        z *= s;
        w *= s;
        return *this;
    }
    // スカラー倍の/=
    Vector4 &operator/=(const float &s) {
        x /= s;
        y /= s;
        z /= s;
        w /= s;
        return *this;
    }

    // == 演算子
    bool operator==(const Vector4 &other) const { return x == other.x && y == other.y && z == other.z && w == other.w; }

    // != 演算子
    bool operator!=(const Vector4 &other) const { return !(*this == other); }

    // ベクトルの長さを計算
    float Length() const { return std::sqrt(x * x + y * y + z * z + w * w); }

    // ベクトルの長さの二乗を計算
    float LengthSq() const { return x * x + y * y + z * z + w * w; }

    // ベクトルを正規化（単位ベクトルにする）
    Vector4 Normalize() const {
        float len = Length();
        if (len == 0.0f) {
            return Vector4(0.0f, 0.0f, 0.0f, 0.0f);
        }
        return Vector4(x / len, y / len, z / len, w / len);
    }

    // ベクトルの内積
    float Dot(const Vector4 &other) const {
        return x * other.x + y * other.y + z * other.z + w * other.w;
    }
#ifdef _DEBUG
    operator ImVec4() const { return ImVec4(x, y, z, w); }
#endif // _DEBUG
};
} // namespace Hagine
