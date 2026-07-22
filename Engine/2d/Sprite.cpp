#define NOMINMAX
#include "Sprite.h"
#include "SpriteCommon.h"
#include <graphics/texture/TextureManager.h>
#include <MyMath.h>

namespace Hagine {
void Sprite::Initialize(const std::string &textureFilePath, Vector2 position, Vector4 color, Vector2 anchorpoint, bool isFlipX, bool isFlipY)
{
    // 必要なマネージャクラスのインスタンスを取得し、テクスチャを読み込む
    pSpriteCommon_ = SpriteCommon::GetInstance();
    pSrvManager_ = TextureManager::GetInstance()->GetSrvManager();

    fullpath_ = textureFilePath;

    TextureManager::GetInstance()->LoadTexture(fullpath_);

    // 頂点、マテリアル、変換行列の各リソースを作成する
    CreateVertexData();
    CreateMaterial();
    CreateTransformationMatrix();
    SetInstanceCount(1);

    // インスタンス数が1以下の場合は初期の変換行列を算出する
    if (instanceCount_ <= 1)
    {
        Transform transform{
            {size_.x, size_.y, 1.0f},
            {0.0f, 0.0f, rotation_},
            {position_.x, position_.y, 0.0f}};

        Matrix4x4 world = MakeAffineMatrix(transform.scale, transform.rotate, transform.translate);
        Matrix4x4 view = MakeIdentity4x4();
        Matrix4x4 proj = MakeOrthographicMatrix(0.0f, 0.0f, float(WinApp::GetVirtualWidth()), float(WinApp::GetVirtualHeight()), 0.0f, 100.0f);
        Matrix4x4 wvp = world * (view * proj);

        pTransformationMatrixData_[0].World = world;
        pTransformationMatrixData_[0].WVP = wvp;
    }

    // 引数の値をメンバ変数に保持する
    position_ = position;
    pMaterialData_->color = color;
    anchorPoint_ = anchorpoint;
    isFlipX_ = isFlipX;
    isFlipY_ = isFlipY;

    AdjustTextureSize();
}

void Sprite::SetInstanceCount(uint32_t count)
{
    // インスタンス数が制限を超えないようクランプする
    const uint32_t maxInstances = 1000;
    instanceCount_ = std::min(count, maxInstances);
}

void Sprite::SetInstanceTransform(uint32_t index, const TransformationMatrix &transform)
{
    // 指定したインデックスが有効範囲内か確認してから行列をコピーする
    const uint32_t maxInstances = 1000;
    if (index < instanceCount_ && index < maxInstances && pTransformationMatrixData_ != nullptr)
    {
        pTransformationMatrixData_[index] = transform;
    }
}

void Sprite::Update(bool isBackMost)
{
    // スプライトのアンカーポイントに基づいた頂点座標の範囲を計算する
    float left = 0.0f - anchorPoint_.x;
    float right = 1.0f - anchorPoint_.x;
    float top = 0.0f - anchorPoint_.y;
    float bottom = 1.0f - anchorPoint_.y;

    // 反転設定に応じて座標を入れ替える
    if (isFlipX_)
    {
        std::swap(left, right);
    }
    if (isFlipY_)
    {
        std::swap(top, bottom);
    }

    // テクスチャの正規化UV座標を計算する
    const DirectX::TexMetadata &metadata = TextureManager::GetInstance()->GetMetaData(fullpath_);
    float tex_left = textureLeftTop_.x / metadata.width;
    float tex_right = (textureLeftTop_.x + textureSize_.x) / metadata.width;
    float tex_top = textureLeftTop_.y / metadata.height;
    float tex_bottom = (textureLeftTop_.y + textureSize_.y) / metadata.height;

    // 頂点バッファをマップして座標とUVデータを書き込む
    vertexResource_->Map(0, nullptr, reinterpret_cast<void **>(&pVertexData_));
    pVertexData_[0] = {{left, bottom, 0.0f, 1.0f}, {tex_left, tex_bottom}};
    pVertexData_[1] = {{left, top, 0.0f, 1.0f}, {tex_left, tex_top}};
    pVertexData_[2] = {{right, bottom, 0.0f, 1.0f}, {tex_right, tex_bottom}};
    pVertexData_[3] = {{right, top, 0.0f, 1.0f}, {tex_right, tex_top}};

    // インデックスバッファをマップして順序データを書き込む
    indexResource_->Map(0, nullptr, reinterpret_cast<void **>(&pIndexData_));
    pIndexData_[0] = 0;
    pIndexData_[1] = 1;
    pIndexData_[2] = 2;
    pIndexData_[3] = 1;
    pIndexData_[4] = 3;
    pIndexData_[5] = 2;

    // インスタンス数が単体の場合、座標変換行列を再計算して更新する。
    // ただし SpriteManager 所有スプライトは UpdateSpriteInstances が全行列を管理するため
    // ここで上書きしない（上書きするとインスタンス編集が一切反映されなくなる）。
    if (instanceCount_ <= 1 && !useExternalTransforms_)
    {
        Transform transform;
        transform.scale = {size_.x, size_.y, 1.0f};
        transform.rotate = {0.0f, 0.0f, rotation_};
        transform.translate = {
            position_.x,
            position_.y,
            isBackMost ? 10000.0f : 0.0f};

        Matrix4x4 world = MakeAffineMatrix(transform.scale, transform.rotate, transform.translate);
        Matrix4x4 view = MakeIdentity4x4();
        Matrix4x4 proj = MakeOrthographicMatrix(0.0f, 0.0f, float(WinApp::GetVirtualWidth()), float(WinApp::GetVirtualHeight()), 0.0f, 100.0f);
        Matrix4x4 wvp = world * (view * proj);

        pTransformationMatrixData_[0].WVP = wvp;
        pTransformationMatrixData_[0].World = world;
    }

    // UV変換用の行列を作成してマテリアルデータに適用する
    Vector3 pos = {uvPosition_.x, uvPosition_.y, 0.0f};
    Vector3 scale = {uvSize_.x, uvSize_.y, 1.0f};
    Vector3 rotate = {0.0f, 0.0f, uvRotate_};
    pMaterialData_->uvTransform = MakeAffineMatrix(scale, rotate, pos);
}

void Sprite::Draw(bool isBackMost)
{
    // 描画共通設定を適用し、頂点・インデックス・マテリアル・テクスチャ情報をコマンドリストにセットする
    pSpriteCommon_->DrawCommonSetting();

    Update(isBackMost);

    pSpriteCommon_->GetDxCommon()->GetCommandList()->IASetVertexBuffers(0, 1, &vertexBufferView_);
    pSpriteCommon_->GetDxCommon()->GetCommandList()->IASetIndexBuffer(&indexBufferView_);
    pSpriteCommon_->GetDxCommon()->GetCommandList()->SetGraphicsRootConstantBufferView(0, materialResource_->GetGPUVirtualAddress());

    pSrvManager_->SetGraphicsRootDescriptorTable(1, transformationMatrixSrvIndex_);
    pSrvManager_->SetGraphicsRootDescriptorTable(2, TextureManager::GetInstance()->GetTextureIndexByFilePath(fullpath_));

    // インデックス描画を実行する
    pSpriteCommon_->GetDxCommon()->GetCommandList()->DrawIndexedInstanced(6, instanceCount_, 0, 0, 0);
}

void Sprite::SetTexturePath(std::string textureFilePath)
{
    // パスを更新し、新しいテクスチャを読み込む
    fullpath_ = textureFilePath;
    TextureManager::GetInstance()->LoadTexture(textureFilePath);
    TextureManager::GetInstance()->GetTextureIndexByFilePath(fullpath_);

    // 変更後テクスチャサイズに合わせる
    AdjustTextureSize();
}

void Sprite::CreateVertexData()
{
    // 頂点バッファ用のリソースを作成し、頂点バッファビューを設定する
    vertexResource_ = pSpriteCommon_->GetDxCommon()->CreateBufferResource(sizeof(SpriteVertexData) * 6);
    vertexBufferView_.BufferLocation = vertexResource_->GetGPUVirtualAddress();
    vertexBufferView_.SizeInBytes = sizeof(SpriteVertexData) * 6;
    vertexBufferView_.StrideInBytes = sizeof(SpriteVertexData);

    // 頂点バッファをマップする
    vertexResource_->Map(0, nullptr, reinterpret_cast<void **>(&pVertexData_));

    // インデックスバッファ用のリソースを作成し、インデックスバッファビューを設定する
    indexResource_ = pSpriteCommon_->GetDxCommon()->CreateBufferResource(sizeof(uint32_t) * 6);
    indexBufferView_.BufferLocation = indexResource_->GetGPUVirtualAddress();
    indexBufferView_.SizeInBytes = sizeof(uint32_t) * 6;
    indexBufferView_.Format = DXGI_FORMAT_R32_UINT;

    // インデックスバッファをマップする
    indexResource_->Map(0, nullptr, reinterpret_cast<void **>(&pIndexData_));
}

void Sprite::CreateMaterial()
{
    // マテリアル用リソースを作成しマップ、デフォルトのカラーと単位行列で初期化する
    materialResource_ = pSpriteCommon_->GetDxCommon()->CreateBufferResource(sizeof(SpriteMaterial));
    materialResource_->Map(0, nullptr, reinterpret_cast<void **>(&pMaterialData_));
    pMaterialData_->color = Vector4(1.0f, 1.0f, 1.0f, 1.0f);
    pMaterialData_->uvTransform = MakeIdentity4x4();
}

void Sprite::CreateTransformationMatrix()
{
    // 最大インスタンス数を考慮した変換行列用のリソースを作成しマップする
    uint32_t maxInstances = 1000;
    transformationMatrixResource_ = pSpriteCommon_->GetDxCommon()->CreateBufferResource(sizeof(TransformationMatrix) * maxInstances);
    transformationMatrixResource_->Map(0, nullptr, reinterpret_cast<void **>(&pTransformationMatrixData_));

    // 全てのインスタンスの行列を単位行列で初期化する
    for (uint32_t i = 0; i < maxInstances; ++i)
    {
        pTransformationMatrixData_[i].WVP = MakeIdentity4x4();
        pTransformationMatrixData_[i].World = MakeIdentity4x4();
    }

    // SRVマネージャからスロットを確保し、ストラクチャードバッファ用のSRVを作成する
    pSrvManager_ = TextureManager::GetInstance()->GetSrvManager();
    transformationMatrixSrvIndex_ = pSrvManager_->Allocate() + 1;
    pSrvManager_->CreateSRVforStructuredBuffer(transformationMatrixSrvIndex_, transformationMatrixResource_.Get(), maxInstances, sizeof(TransformationMatrix));
}

void Sprite::AdjustTextureSize()
{
    // テクスチャのメタデータから画像サイズを取得し、スプライトのサイズをそれに合わせる
    const DirectX::TexMetadata &metadata = TextureManager::GetInstance()->GetMetaData(fullpath_);

    textureSize_.x = static_cast<float>(metadata.width);
    textureSize_.y = static_cast<float>(metadata.height);
    size_ = textureSize_;
}
} // namespace Hagine
