#pragma once
#include <cstring>
#include"Vector3.h"
namespace Hagine {

/// <summary>
/// 4x4行列
/// 加減算・乗算・スカラー演算などの基本的な行列演算を提供する
/// </summary>
struct Matrix4x4 {
public:
	float m[4][4]; // 行列要素（[行][列]）

	/// <summary>
	/// 行列同士の加算
	/// </summary>
	Matrix4x4 operator+(const Matrix4x4& mat) const {
		Matrix4x4 result;
		for (int i = 0; i < 4; ++i) {
			for (int j = 0; j < 4; ++j) {
				result.m[i][j] = m[i][j] + mat.m[i][j];
			}
		}
		return result;
	}

	/// <summary>
	/// 行列同士の減算
	/// </summary>
	Matrix4x4 operator-(const Matrix4x4& mat) const {
		Matrix4x4 result;
		for (int i = 0; i < 4; ++i) {
			for (int j = 0; j < 4; ++j) {
				result.m[i][j] = m[i][j] - mat.m[i][j];
			}
		}
		return result;
	}

	/// <summary>
	/// 行列同士の乗算
	/// </summary>
	Matrix4x4 operator*(const Matrix4x4& mat) const {
		Matrix4x4 result;
		for (int i = 0; i < 4; ++i) {
			for (int j = 0; j < 4; ++j) {
				result.m[i][j] = 0;
				for (int k = 0; k < 4; ++k) {
					result.m[i][j] += m[i][k] * mat.m[k][j];
				}
			}
		}
		return result;
	}

	/// <summary>
	/// スカラーとの乗算
	/// </summary>
	Matrix4x4 operator*(const float& scalar) const {
		Matrix4x4 result;
		for (int i = 0; i < 4; ++i) {
			for (int j = 0; j < 4; ++j) {
				result.m[i][j] = m[i][j] * scalar;
			}
		}
		return result;
	}

	/// <summary>
	/// スカラーとの除算
	/// </summary>
	Matrix4x4 operator/(const float& scalar) const {
		Matrix4x4 result;
		for (int i = 0; i < 4; ++i) {
			for (int j = 0; j < 4; ++j) {
				result.m[i][j] = m[i][j] / scalar;
			}
		}
		return result;
	}

	/// <summary>
	/// 行列同士の加算代入
	/// </summary>
	Matrix4x4& operator+=(const Matrix4x4& mat) {
		for (int i = 0; i < 4; ++i) {
			for (int j = 0; j < 4; ++j) {
				m[i][j] += mat.m[i][j];
			}
		}
		return *this;
	}

	/// <summary>
	/// 行列同士の減算代入
	/// </summary>
	Matrix4x4& operator-=(const Matrix4x4& mat) {
		for (int i = 0; i < 4; ++i) {
			for (int j = 0; j < 4; ++j) {
				m[i][j] -= mat.m[i][j];
			}
		}
		return *this;
	}

	/// <summary>
	/// 行列同士の乗算代入
	/// </summary>
	Matrix4x4& operator*=(const Matrix4x4& mat) {
		Matrix4x4 result = (*this) * mat;
		*this = result;
		return *this;
	}

	/// <summary>
	/// スカラーとの除算代入
	/// </summary>
	Matrix4x4& operator/=(const float& scalar) {
		for (int i = 0; i < 4; ++i) {
			for (int j = 0; j < 4; ++j) {
				m[i][j] /= scalar;
			}
		}
		return *this;
	}

	/// <summary>
	/// 指定した列を取り出してVector3で返す
	/// </summary>
	/// <param name="col">列インデックス</param>
	/// <returns>Vector3: 指定列の上位3成分</returns>
	Vector3 GetColumn(int col) const {
		return Vector3(m[0][col], m[1][col], m[2][col]);
	}
};
} // namespace Hagine
