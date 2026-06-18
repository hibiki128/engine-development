#pragma once
#include"cmath"
/// <summary>
/// 3次元ベクトル
/// </summary>
namespace Hagine {
struct Vector3 final {
	float x;
	float y;
	float z;

	// コンストラクタ
	Vector3(float _x = 0.0f, float _y = 0.0f, float _z = 0.0f) : x(_x), y(_y), z(_z) {}

	// 符号反転
	Vector3 operator-() const { return Vector3(-x, -y, -z); }

	// 加算
	Vector3 operator+(const Vector3& obj) const { return Vector3(x + obj.x, y + obj.y, z + obj.z); }
	// 減算
	Vector3 operator-(const Vector3& obj) const { return Vector3(x - obj.x, y - obj.y, z - obj.z); }
	// 乗算
	Vector3 operator*(const Vector3& obj) const { return Vector3(x * obj.x, y * obj.y, z * obj.z); }
	// 乗算(スカラー倍)(float型)
	Vector3 operator*(const float& scalar) const { return Vector3(x * scalar, y * scalar, z * scalar); }
	// 乗算(スカラー倍)(int型)
	Vector3 operator*(const int& scalar) const { return Vector3(x * scalar, y * scalar, z * scalar); }
	// 除算
	Vector3 operator/(const Vector3& obj) const { return Vector3(x / obj.x, y / obj.y, z / obj.z); }
	// 除算(スカラー)(float型)
	Vector3 operator/(const float& scalar) const { return Vector3(x / scalar, y / scalar, z / scalar); }
	// 除算(スカラー)(int型)
	Vector3 operator/(const int& scalar) const { return Vector3(x / scalar, y / scalar, z / scalar); }

	friend Vector3 operator*(const float& scalar, const Vector3& vec) { return vec * scalar; }
	friend Vector3 operator/(const float& scalar, const Vector3& vec) { return vec / scalar; }
	friend Vector3 operator*(const int& scalar, const Vector3& vec) { return vec * scalar; }
	friend Vector3 operator/(const int& scalar, const Vector3& vec) { return vec / scalar; }

	// +=
	Vector3& operator+=(const Vector3& other) {
		x += other.x;
		y += other.y;
		z += other.z;
		return *this;
	}
	// -=
	Vector3& operator-=(const Vector3& other) {
		x -= other.x;
		y -= other.y;
		z -= other.z;
		return *this;
	}
	// *=
	Vector3& operator*=(const Vector3& other) {
		x *= other.x;
		y *= other.y;
		z *= other.z;
		return *this;
	}
	// /=
	Vector3& operator/=(const Vector3& other) {
		x /= other.x;
		y /= other.y;
		z /= other.z;
		return *this;
	}
	// スカラー倍の+=
	Vector3& operator+=(const float& s) {
		x += s;
		y += s;
		z += s;
		return *this;
	}
	// スカラー倍の-=
	Vector3& operator-=(const float& s) {
		x -= s;
		y -= s;
		z -= s;
		return *this;
	}
	// スカラー倍の*=
	Vector3& operator*=(const float& s) {
		x *= s;
		y *= s;
		z *= s;
		return *this;
	}
	// スカラー倍の/=
	Vector3& operator/=(const float& s) {
		x /= s;
		y /= s;
		z /= s;
		return *this;
	}

	// == 演算子
	bool operator==(const Vector3& other) const { return x == other.x && y == other.y && z == other.z; }

	// != 演算子
	bool operator!=(const Vector3& other) const { return !(*this == other); }

	// ベクトルの長さを計算
	float Length() const { return std::sqrt(x * x + y * y + z * z); }

	// ベクトルの長さの二乗を計算
	float LengthSq() const { return x * x + y * y + z * z; }

	// ベクトルを正規化（単位ベクトルにする）
	Vector3 Normalize() const {
		float len = Length();
		// 長さが0の場合は正規化できないので、ゼロベクトルを返す
		if (len == 0.0f) {
			return Vector3(0.0f, 0.0f, 0.0f);
		}
		return Vector3(x / len, y / len, z / len);
	}

	// ベクトルの内積
	float Dot(const Vector3& other) const {
		return x * other.x + y * other.y + z * other.z;
	}

	// ベクトルの外積
	Vector3 Cross(const Vector3& other) const {
		return Vector3(
			y * other.z - z * other.y,
			z * other.x - x * other.z,
			x * other.y - y * other.x
		);
	}
};
} // namespace Hagine
