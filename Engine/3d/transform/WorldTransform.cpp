#include "WorldTransform.h"

namespace Hagine {
WorldTransform::WorldTransform()
{
}

WorldTransform::~WorldTransform()
{
}

void WorldTransform::Initialize()
{
    // スケール、回転、平行移動を初期化
    scale_ = {1.0f, 1.0f, 1.0f};
    eulerRotation_ = {0.0f, 0.0f, 0.0f};
    quaternionRotation_ = Quaternion::IdentityQuaternion();
    translation_ = {0.0f, 0.0f, 0.0f};
    preRotate_ = {0.0f, 0.0f, 0.0f};

    pDxCommon_ = DirectXCommon::GetInstance();
    matWorld_ = MakeIdentity4x4();

    CreateConstBuffer();
    Map();
    UpdateMatrix();
}

void WorldTransform::TransferMatrix()
{
    // 定数バッファに転送
    if (pConstMap_)
    {
        pConstMap_->matWorld = matWorld_;
    }
}

void WorldTransform::CreateConstBuffer()
{
    const UINT bufferSize = sizeof(ConstBufferDataWorldTransform);
    constBuffer_ = pDxCommon_->CreateBufferResource(bufferSize);
}

void WorldTransform::Map()
{
    // バッファのマッピング
    HRESULT hr = constBuffer_->Map(0, nullptr, reinterpret_cast<void **>(&pConstMap_));
    if (FAILED(hr))
    {
        // エラーハンドリング
    }
}

void WorldTransform::UpdateMatrix()
{
    // クォータニオンorオイラー
    isUseQuaternion_ ? UpdateQuaternion() : UpdateEuler();

    // 親があれば親のワールド行列を掛ける
    if (pParent_)
    {
        if (inheritTranslation_ && inheritRotation_ && inheritScale_)
        {
            // 全成分を継承（従来どおり）
            matWorld_ = matWorld_ * pParent_->matWorld_;
        }
        else
        {
            // 継承する成分だけで親行列を組み直して掛ける。
            // 親のワールドSRTを取り出し、継承しない成分は単位（Sなら1, Rなら無回転, Tなら0）にする。
            const Vector3 pScale = inheritScale_ ? pParent_->GetWorldScale() : Vector3{1.0f, 1.0f, 1.0f};
            const Quaternion pRot = inheritRotation_ ? pParent_->GetWorldRotationQuaternion() : Quaternion::IdentityQuaternion();
            const Vector3 pTrans = inheritTranslation_ ? pParent_->GetWorldPosition() : Vector3{0.0f, 0.0f, 0.0f};
            const Matrix4x4 filteredParent = MakeAffineMatrix(pScale, pRot, pTrans);
            matWorld_ = matWorld_ * filteredParent;
        }
    }

    // 定数バッファに転送する
    TransferMatrix();
}

void WorldTransform::SetRotationEuler(const Vector3 &eulerAngles)
{
    eulerRotation_ = eulerAngles;
    if (isUseQuaternion_)
    {
        quaternionRotation_ = Quaternion::FromEulerAngles(eulerAngles);
    }
}

void WorldTransform::SetRotationQuaternion(const Quaternion &quaternion)
{
    quaternionRotation_ = quaternion.Normalize();
    if (!isUseQuaternion_)
    {
        eulerRotation_ = quaternionRotation_.ToEulerAngles();
    }
}

Vector3 WorldTransform::GetRotationEuler() const
{
    return isUseQuaternion_ ? quaternionRotation_.ToEulerAngles() : eulerRotation_;
}

Quaternion WorldTransform::GetRotationQuaternion() const
{
    return quaternionRotation_;
}

Vector3 WorldTransform::GetWorldRotationEuler() const
{
    Quaternion worldRotation = GetWorldRotationQuaternion();
    return worldRotation.ToEulerAngles();
}

Quaternion WorldTransform::GetWorldRotationQuaternion() const
{
    // 親がいない場合はローカル回転をそのまま返す
    if (!pParent_)
    {
        return quaternionRotation_;
    }

    // 親のワールド回転を取得
    Quaternion parentWorldRotation = pParent_->GetWorldRotationQuaternion();

    // 親の回転 * ローカル回転 でワールド回転を計算
    return parentWorldRotation * quaternionRotation_;
}

void WorldTransform::UpdateEuler()
{
    // オイラー角から行列を作成
    matWorld_ = MakeAffineMatrix(scale_, eulerRotation_, translation_);
}

void WorldTransform::UpdateQuaternion()
{
    // 回転処理（オイラー角が変更された場合にクォータニオンを更新）
    if (eulerRotation_.x != preRotate_.x || eulerRotation_.y != preRotate_.y || eulerRotation_.z != preRotate_.z)
    {
        RotateQuaternion();
    }
    // クォータニオンから行列を作成
    matWorld_ = MakeAffineMatrix(scale_, quaternionRotation_, translation_);
    // 回転量計算用変数に挿入
    preRotate_ = eulerRotation_;
}

void WorldTransform::RotateQuaternion()
{
    if (eulerRotation_.x == 0.0f && eulerRotation_.y == 0.0f && eulerRotation_.z == 0.0f)
    {
        quaternionRotation_ = Quaternion::IdentityQuaternion();
    }
    else
    {
        // オイラー角からクォータニオンに変換
        quaternionRotation_ = Quaternion::FromEulerAngles(eulerRotation_);
    }
}

Vector3 WorldTransform::GetWorldPosition() const
{
    Vector3 worldPos;
    // ワールド行列の平行移動成分を取得
    worldPos.x = matWorld_.m[3][0];
    worldPos.y = matWorld_.m[3][1];
    worldPos.z = matWorld_.m[3][2];
    return worldPos;
}

// ワールド座標（回転）
Quaternion WorldTransform::GetWorldRotation() const
{
    const Matrix4x4 &m = matWorld_;

    // スケールを除去した回転行列を作成
    Vector3 scale = GetWorldScale();
    Matrix4x4 rotationMatrix = MakeIdentity4x4();

    // この行列規約は行ベクトル（v * M）なので、基底ベクトルは「行」に入っている。
    // GetWorldScale も行の長さからスケールを求めているので、正規化も行単位で行う。
    const float axisLength[3] = {scale.x, scale.y, scale.z};
    for (int row = 0; row < 3; row++)
    {
        // スケール0の軸は方向が定まらないので、単位行列の行をそのまま使う
        if (std::abs(axisLength[row]) <= 1e-6f)
        {
            continue;
        }
        for (int column = 0; column < 3; column++)
        {
            rotationMatrix.m[row][column] = m.m[row][column] / axisLength[row];
        }
    }

    // 回転行列からクォータニオンを生成
    return Quaternion::FromMatrix(rotationMatrix);
}

// ワールド座標（スケール）
Vector3 WorldTransform::GetWorldScale() const
{
    Vector3 worldScale;
    const Matrix4x4 &m = matWorld_;

    // 各軸のベクトルの長さをスケールとして取得
    worldScale.x = std::sqrt(m.m[0][0] * m.m[0][0] + m.m[0][1] * m.m[0][1] + m.m[0][2] * m.m[0][2]);
    worldScale.y = std::sqrt(m.m[1][0] * m.m[1][0] + m.m[1][1] * m.m[1][1] + m.m[1][2] * m.m[1][2]);
    worldScale.z = std::sqrt(m.m[2][0] * m.m[2][0] + m.m[2][1] * m.m[2][1] + m.m[2][2] * m.m[2][2]);

    return worldScale;
}
} // namespace Hagine
