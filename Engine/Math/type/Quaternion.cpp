#include "Quaternion.h"
#include <cmath>
#include <numbers>

namespace Hagine {
void Quaternion::SetFromTo(const Vector3 &from, const Vector3 &to) {
    Vector3 f = from.Normalize();
    Vector3 t = to.Normalize();

    float dot = f.Dot(t);

    // 逆方向の場合の特別処理
    if (dot < -0.999999f) {
        // 90度回転した軸を見つける
        Vector3 axis = Vector3(1.0f, 0.0f, 0.0f).Cross(f);
        if (axis.Length() < 0.000001f) {
            axis = Vector3(0.0f, 1.0f, 0.0f).Cross(f);
        }
        axis = axis.Normalize();

        // 180度回転
        *this = FromAxisAngle(axis, std::numbers::pi_v<float>);
        return;
    }

    // 同じ方向の場合
    if (dot > 0.999999f) {
        *this = IdentityQuaternion();
        return;
    }

    Vector3 cross = f.Cross(t);

    w = std::sqrt((1.0f + dot) * 0.5f);
    float s = 0.5f / w;

    x = cross.x * s;
    y = cross.y * s;
    z = cross.z * s;
}

Quaternion Quaternion::FromEulerAngles(const Vector3 &eulerAngles) {
    // XYZ回転順序でオイラー角からクォータニオンを作成
    float halfX = eulerAngles.x * 0.5f;
    float halfY = eulerAngles.y * 0.5f;
    float halfZ = eulerAngles.z * 0.5f;

    float cosX = std::cos(halfX);
    float sinX = std::sin(halfX);
    float cosY = std::cos(halfY);
    float sinY = std::sin(halfY);
    float cosZ = std::cos(halfZ);
    float sinZ = std::sin(halfZ);

    return Quaternion(
        sinX * cosY * cosZ - cosX * sinY * sinZ, // x
        cosX * sinY * cosZ + sinX * cosY * sinZ, // y
        cosX * cosY * sinZ - sinX * sinY * cosZ, // z
        cosX * cosY * cosZ + sinX * sinY * sinZ  // w
    );
}

Quaternion Quaternion::FromEulerAnglesSafe(const Vector3 &eulerAngles) {
    // YXZ回転順序でジンバルロックを回避
    float halfX = eulerAngles.x * 0.5f;
    float halfY = eulerAngles.y * 0.5f;
    float halfZ = eulerAngles.z * 0.5f;

    float cosX = std::cos(halfX);
    float sinX = std::sin(halfX);
    float cosY = std::cos(halfY);
    float sinY = std::sin(halfY);
    float cosZ = std::cos(halfZ);
    float sinZ = std::sin(halfZ);

    return Quaternion(
        sinX * cosY * cosZ + cosX * sinY * sinZ,
        cosX * sinY * cosZ - sinX * cosY * sinZ,
        cosX * cosY * sinZ + sinX * sinY * cosZ,
        cosX * cosY * cosZ - sinX * sinY * sinZ);
}

Vector3 Quaternion::ToEulerAnglesSafe() const {
    Vector3 angles;

    // YXZ順序での変換
    float test = x * y + z * w;
    if (test > 0.499f) { // 特異点（北極）
        angles.y = 2.0f * std::atan2(x, w);
        angles.x = std::numbers::pi_v<float> / 2.0f;
        angles.z = 0.0f;
        return angles;
    }
    if (test < -0.499f) { // 特異点（南極）
        angles.y = -2.0f * std::atan2(x, w);
        angles.x = -std::numbers::pi_v<float> / 2.0f;
        angles.z = 0.0f;
        return angles;
    }

    float sqx = x * x;
    float sqy = y * y;
    float sqz = z * z;

    angles.y = std::atan2(2.0f * y * w - 2.0f * x * z, 1.0f - 2.0f * sqy - 2.0f * sqz);
    angles.x = std::asin(2.0f * test);
    angles.z = std::atan2(2.0f * x * w - 2.0f * y * z, 1.0f - 2.0f * sqx - 2.0f * sqz);

    return angles;
}

Quaternion Quaternion::FromAxisRotations(const Vector3 &axisRotations) {
    Quaternion qx = FromAxisAngle(Vector3(1.0f, 0.0f, 0.0f), axisRotations.x);
    Quaternion qy = FromAxisAngle(Vector3(0.0f, 1.0f, 0.0f), axisRotations.y);
    Quaternion qz = FromAxisAngle(Vector3(0.0f, 0.0f, 1.0f), axisRotations.z);

    return qy * qx * qz; // YXZ順序
}

Vector3 Quaternion::Rotate(const Vector3 &v) const {
    Quaternion q = this->Normalize();     // 回転用クォータニオン
    Quaternion p = {v.x, v.y, v.z, 0.0f}; // 回転対象ベクトル

    // q * p * q^-1
    Quaternion rotated = q * p * q.Inverse();
    return Vector3(rotated.x, rotated.y, rotated.z);
}

Vector3 Quaternion::ToEulerAngles() const {
    Vector3 angles;

    // X軸回転（ピッチ）
    float sinP = 2.0f * (w * x + y * z);
    float cosP = 1.0f - 2.0f * (x * x + y * y);
    angles.x = std::atan2(sinP, cosP);

    // Y軸回転（ヨー）
    float sinY = 2.0f * (w * y - z * x);
    if (std::abs(sinY) >= 1.0f) {
        angles.y = std::copysign(std::numbers::pi_v<float> / 2.0f, sinY);
    } else {
        angles.y = std::asin(sinY);
    }

    // Z軸回転（ロール）
    float sinR = 2.0f * (w * z + x * y);
    float cosR = 1.0f - 2.0f * (y * y + z * z);
    angles.z = std::atan2(sinR, cosR);

    return angles;
}

Quaternion Quaternion::Conjugate() const {
    return Quaternion(-x, -y, -z, w);
}

Quaternion Quaternion::Normalize() const {
    float length = std::sqrt(x * x + y * y + z * z + w * w);
    if (length < 0.000001f) {
        return IdentityQuaternion();
    }
    return Quaternion(x / length, y / length, z / length, w / length);
}

Quaternion Quaternion::FromLookRotation(const Vector3 &direction, const Vector3 &up) {
    Vector3 forward = direction.Normalize();
    Vector3 upNorm = up.Normalize();

    // 正しい右手座標系の右方向
    // forward × up
    Vector3 right = forward.Cross(upNorm).Normalize();

    // 上方向を再計算
    Vector3 newUp = right.Cross(forward).Normalize();

    // 回転行列の列として配置
    float m00 = right.x;
    float m01 = newUp.x;
    float m02 = forward.x;
    float m10 = right.y;
    float m11 = newUp.y;
    float m12 = forward.y;
    float m20 = right.z;
    float m21 = newUp.z;
    float m22 = forward.z;

    float trace = m00 + m11 + m22;

    if (trace > 0.0f) {
        float s = std::sqrt(trace + 1.0f) * 2.0f;
        return Quaternion(
            (m21 - m12) / s,
            (m02 - m20) / s,
            (m10 - m01) / s,
            0.25f * s);
    } else if (m00 > m11 && m00 > m22) {
        float s = std::sqrt(1.0f + m00 - m11 - m22) * 2.0f;
        return Quaternion(
            0.25f * s,
            (m01 + m10) / s,
            (m02 + m20) / s,
            (m21 - m12) / s);
    } else if (m11 > m22) {
        float s = std::sqrt(1.0f + m11 - m00 - m22) * 2.0f;
        return Quaternion(
            (m01 + m10) / s,
            0.25f * s,
            (m12 + m21) / s,
            (m02 - m20) / s);
    } else {
        float s = std::sqrt(1.0f + m22 - m00 - m11) * 2.0f;
        return Quaternion(
            (m02 + m20) / s,
            (m12 + m21) / s,
            0.25f * s,
            (m01 - m10) / s);
    }
}

Quaternion Quaternion::operator*(const Quaternion &q) const {
    return Quaternion(
        w * q.x + x * q.w + y * q.z - z * q.y,
        w * q.y - x * q.z + y * q.w + z * q.x,
        w * q.z + x * q.y - y * q.x + z * q.w,
        w * q.w - x * q.x - y * q.y - z * q.z);
}

Quaternion Quaternion::operator+(const Quaternion &other) const {
    return Quaternion(x + other.x, y + other.y, z + other.z, w + other.w);
}

Quaternion Quaternion::operator-(const Quaternion &other) const {
    return Quaternion(x - other.x, y - other.y, z - other.z, w - other.w);
}

Quaternion Quaternion::operator/(const Quaternion &other) const {
    Quaternion inverse = other.Inverse();
    return *this * inverse;
}

Quaternion Quaternion::operator*(const float &scalar) const {
    return Quaternion(x * scalar, y * scalar, z * scalar, w * scalar);
}

Quaternion Quaternion::operator+=(const Quaternion &other) {
    x += other.x;
    y += other.y;
    z += other.z;
    w += other.w;
    return *this;
}

// クォータニオンの減算代入
Quaternion Quaternion::operator-=(const Quaternion &other) {
    x -= other.x;
    y -= other.y;
    z -= other.z;
    w -= other.w;
    return *this;
}

Vector3 Quaternion::operator*(const Vector3 &v) const {
    Quaternion q = this->Normalize(); // クォータニオンを正規化
    Quaternion p = {v.x, v.y, v.z, 0.0f};

    // 正しい回転 q * p * q^-1
    Quaternion res = q * p * q.Inverse();

    // Vector3として返す
    return Vector3(res.x, res.y, res.z);
}

Quaternion Quaternion::operator-() const {
    return Quaternion(-x, -y, -z, -w);
}

bool Quaternion::operator==(const Quaternion &other) const {
    constexpr float kEpsilon = 1e-6f;
    return std::abs(x - other.x) < kEpsilon &&
           std::abs(y - other.y) < kEpsilon &&
           std::abs(z - other.z) < kEpsilon &&
           std::abs(w - other.w) < kEpsilon;
}

bool Quaternion::operator!=(const Quaternion &other) const {
    return !(*this == other);
}

Quaternion Quaternion::IdentityQuaternion() {
    return Quaternion(0.0f, 0.0f, 0.0f, 1.0f);
}

float Quaternion::Norm() const {
    return std::sqrt(x * x + y * y + z * z + w * w);
}

float Quaternion::Dot(const Quaternion &other) const {
    return x * other.x + y * other.y + z * other.z + w * other.w;
}

Quaternion Quaternion::Inverse() const {
    float normSquared = x * x + y * y + z * z + w * w;
    if (normSquared < 0.000001f) {
        return IdentityQuaternion();
    }
    Quaternion conjugate = Conjugate();
    return Quaternion(conjugate.x / normSquared, conjugate.y / normSquared,
                      conjugate.z / normSquared, conjugate.w / normSquared);
}

Quaternion Quaternion::Slerp(const Quaternion &q1, const Quaternion &q2, float t) {
    Quaternion q0 = q1;
    Quaternion q1_copy = q2;

    float dot = q0.Dot(q1_copy);

    // 短い回転経路を選択
    if (dot < 0.0f) {
        q0 = Quaternion(-q0.x, -q0.y, -q0.z, -q0.w);
        dot = -dot;
    }

    const float threshold = 0.9995f;
    if (dot > threshold) {
        // 線形補間
        Quaternion result = Quaternion(
            q0.x + t * (q1_copy.x - q0.x),
            q0.y + t * (q1_copy.y - q0.y),
            q0.z + t * (q1_copy.z - q0.z),
            q0.w + t * (q1_copy.w - q0.w));
        return result.Normalize();
    }

    // 球面線形補間
    float theta_0 = std::acos(std::abs(dot));
    float theta = theta_0 * t;

    float sin_theta = std::sin(theta);
    float sin_theta_0 = std::sin(theta_0);

    float s0 = std::cos(theta) - dot * sin_theta / sin_theta_0;
    float s1 = sin_theta / sin_theta_0;

    return Quaternion(
        s0 * q0.x + s1 * q1_copy.x,
        s0 * q0.y + s1 * q1_copy.y,
        s0 * q0.z + s1 * q1_copy.z,
        s0 * q0.w + s1 * q1_copy.w);
}

Vector3 Quaternion::GetAxis() const {
    float sinHalfAngle = std::sqrt(1.0f - w * w);
    if (sinHalfAngle < 0.000001f) {
        return Vector3(1.0f, 0.0f, 0.0f); // 任意の軸
    }
    return Vector3(x / sinHalfAngle, y / sinHalfAngle, z / sinHalfAngle);
}

float Quaternion::GetAngle() const {
    return 2.0f * std::acos(std::abs(w));
}

Quaternion Quaternion::FromAxisAngle(const Vector3 &axis, float angle) {
    Vector3 normalizedAxis = axis.Normalize();
    float halfAngle = angle * 0.5f;
    float sinHalfAngle = std::sin(halfAngle);

    return Quaternion(
        normalizedAxis.x * sinHalfAngle,
        normalizedAxis.y * sinHalfAngle,
        normalizedAxis.z * sinHalfAngle,
        std::cos(halfAngle));
}

Quaternion Quaternion::FromEulerDegrees(const Vector3 &eulerDegrees) {
    // 度数をラジアンに変換
    Vector3 eulerRadians = {
        eulerDegrees.x * std::numbers::pi_v<float> / 180.0f,
        eulerDegrees.y * std::numbers::pi_v<float> / 180.0f,
        eulerDegrees.z * std::numbers::pi_v<float> / 180.0f};

    return FromEulerAngles(eulerRadians);
}

// クォータニオンを度数のオイラー角に変換
Vector3 Quaternion::ToEulerDegrees() const {
    Vector3 eulerRadians = ToEulerAngles();

    return {
        eulerRadians.x * 180.0f / std::numbers::pi_v<float>,
        eulerRadians.y * 180.0f / std::numbers::pi_v<float>,
        eulerRadians.z * 180.0f / std::numbers::pi_v<float>};
}

Quaternion Quaternion::FromMatrix(const Matrix4x4 &matrix) {
    Quaternion result;

    float trace = matrix.m[0][0] + matrix.m[1][1] + matrix.m[2][2];

    if (trace > 0.0f) {
        float s = std::sqrt(trace + 1.0f) * 2.0f; // s = 4 * qw
        result.w = 0.25f * s;
        result.x = (matrix.m[2][1] - matrix.m[1][2]) / s;
        result.y = (matrix.m[0][2] - matrix.m[2][0]) / s;
        result.z = (matrix.m[1][0] - matrix.m[0][1]) / s;
    } else if (matrix.m[0][0] > matrix.m[1][1] && matrix.m[0][0] > matrix.m[2][2]) {
        float s = std::sqrt(1.0f + matrix.m[0][0] - matrix.m[1][1] - matrix.m[2][2]) * 2.0f; // s = 4 * qx
        result.w = (matrix.m[2][1] - matrix.m[1][2]) / s;
        result.x = 0.25f * s;
        result.y = (matrix.m[0][1] + matrix.m[1][0]) / s;
        result.z = (matrix.m[0][2] + matrix.m[2][0]) / s;
    } else if (matrix.m[1][1] > matrix.m[2][2]) {
        float s = std::sqrt(1.0f + matrix.m[1][1] - matrix.m[0][0] - matrix.m[2][2]) * 2.0f; // s = 4 * qy
        result.w = (matrix.m[0][2] - matrix.m[2][0]) / s;
        result.x = (matrix.m[0][1] + matrix.m[1][0]) / s;
        result.y = 0.25f * s;
        result.z = (matrix.m[1][2] + matrix.m[2][1]) / s;
    } else {
        float s = std::sqrt(1.0f + matrix.m[2][2] - matrix.m[0][0] - matrix.m[1][1]) * 2.0f; // s = 4 * qz
        result.w = (matrix.m[1][0] - matrix.m[0][1]) / s;
        result.x = (matrix.m[0][2] + matrix.m[2][0]) / s;
        result.y = (matrix.m[1][2] + matrix.m[2][1]) / s;
        result.z = 0.25f * s;
    }

    return result;
}
} // namespace Hagine
