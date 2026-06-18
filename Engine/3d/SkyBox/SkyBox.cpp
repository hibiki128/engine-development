#include "SkyBox.h"
#include "DirectXCommon.h"
#include "Graphics/PipeLine/PipeLineManager.h"
#include "Graphics/Srv/SrvManager.h"
#include "Graphics/Texture/TextureManager.h"
#include <Shadow/ShadowMap.h>
#include <myMath.h>

namespace Hagine {
void SkyBox::Finalize() {
    vertexResource_.Reset();
    indexResource_.Reset();
    skyBoxResource_.Reset();
    cameraResource_.Reset();
}

void SkyBox::Initialize(std::string filePath) {
    psoManager_ = PipeLineManager::GetInstance();
    dxCommon_ = DirectXCommon::GetInstance();
    srvManager_ = SrvManager::GetInstance();
    CreateShape();
    CreateVertex();
    CreateIndex();
    CreateSkyBox();
    CreateCamera();
    TextureManager::GetInstance()->LoadTexture(filePath);
    textureIndex_ = TextureManager::GetInstance()->GetTextureIndexByFilePath(filePath);
}

void SkyBox::Update(const ViewProjection &viewProjection) {
    // カメラ位置を抽出
    Vector3 cameraPosition = viewProjection.translation_;

    // 平行移動（カメラの位置に置く）
    Matrix4x4 translationMatrix = MakeTranslateMatrix(cameraPosition);

    // スケール
    float skyboxScale = 10000.0f;
    Matrix4x4 scaleMatrix = MakeScaleMatrix({skyboxScale, skyboxScale, skyboxScale});

    // ワールド行列
    skyBoxData_->worldMatrix = scaleMatrix * translationMatrix;

    // カメラの位置を消したビュー行列を作る
    Matrix4x4 viewWithoutTranslation = viewProjection.matView_;
    viewWithoutTranslation.m[3][0] = 0.0f;
    viewWithoutTranslation.m[3][1] = 0.0f;
    viewWithoutTranslation.m[3][2] = 0.0f;

    // ビュー×プロジェクション
    Matrix4x4 viewProjectionMatrix = viewWithoutTranslation * viewProjection.matProjection_;

    // GPUに送信
    cameraData_->viewProjection = viewProjectionMatrix;
    cameraData_->worldPosition = cameraPosition;
}

void SkyBox::Draw(const ViewProjection &viewProjection) {
    if (ShadowMap::GetInstance()->IsShadowPassActive()) return;
    Update(viewProjection);
    ID3D12GraphicsCommandList *commandList = dxCommon_->GetCommandList().Get();
    psoManager_->DrawCommonSetting(PipelineType::kSkybox);

    commandList->IASetVertexBuffers(0, 1, &vertexBufferView_);
    commandList->IASetIndexBuffer(&indexBufferView_);

    // SkyBoxData（ワールド行列）をb0に設定
    commandList->SetGraphicsRootConstantBufferView(0, skyBoxResource_->GetGPUVirtualAddress());
    // CameraData（ビュープロジェクション行列とカメラ位置）をb1に設定
    commandList->SetGraphicsRootConstantBufferView(1, cameraResource_->GetGPUVirtualAddress());
    // テクスチャをt0に設定
    commandList->SetGraphicsRootDescriptorTable(2, srvManager_->GetGPUDescriptorHandle(textureIndex_));

    commandList->DrawIndexedInstanced(UINT(indices_.size()), 1, 0, 0, 0);
}

void SkyBox::CreateShape() {
    // 形状を作成
    vertices_.resize(24);
    indices_.resize(36);

    // 右面
    vertices_[0].position = {1.0f, 1.0f, 1.0f, 1.0f};
    vertices_[1].position = {1.0f, 1.0f, -1.0f, 1.0f};
    vertices_[2].position = {1.0f, -1.0f, 1.0f, 1.0f};
    vertices_[3].position = {1.0f, -1.0f, -1.0f, 1.0f};
    // 左面
    vertices_[4].position = {-1.0f, 1.0f, -1.0f, 1.0f};
    vertices_[5].position = {-1.0f, 1.0f, 1.0f, 1.0f};
    vertices_[6].position = {-1.0f, -1.0f, -1.0f, 1.0f};
    vertices_[7].position = {-1.0f, -1.0f, 1.0f, 1.0f};
    // 前面
    vertices_[8].position = {-1.0f, 1.0f, 1.0f, 1.0f};
    vertices_[9].position = {1.0f, 1.0f, 1.0f, 1.0f};
    vertices_[10].position = {-1.0f, -1.0f, 1.0f, 1.0f};
    vertices_[11].position = {1.0f, -1.0f, 1.0f, 1.0f};
    // 上面
    vertices_[12].position = {-1.0f, 1.0f, 1.0f, 1.0f};
    vertices_[13].position = {1.0f, 1.0f, 1.0f, 1.0f};
    vertices_[14].position = {-1.0f, 1.0f, -1.0f, 1.0f};
    vertices_[15].position = {1.0f, 1.0f, -1.0f, 1.0f};
    // 下面
    vertices_[16].position = {-1.0f, -1.0f, 1.0f, 1.0f};
    vertices_[17].position = {1.0f, -1.0f, 1.0f, 1.0f};
    vertices_[18].position = {-1.0f, -1.0f, -1.0f, 1.0f};
    vertices_[19].position = {1.0f, -1.0f, -1.0f, 1.0f};
    // 背面
    vertices_[20].position = {1.0f, 1.0f, -1.0f, 1.0f};
    vertices_[21].position = {-1.0f, 1.0f, -1.0f, 1.0f};
    vertices_[22].position = {1.0f, -1.0f, -1.0f, 1.0f};
    vertices_[23].position = {-1.0f, -1.0f, -1.0f, 1.0f};

    indices_ = {
        // 右面
        0, 1, 2, 2, 1, 3,
        // 左面
        4, 5, 6, 6, 5, 7,
        // 前面
        8, 9, 10, 10, 9, 11,
        // 上面
        12, 14, 13, 13, 14, 15,
        // 下面
        16, 17, 18, 18, 17, 19,
        // 背面
        20, 21, 22, 22, 21, 23};
}

void SkyBox::CreateVertex() {
    vertexResource_ = dxCommon_->CreateBufferResource(sizeof(SkyBoxVertexData3D) * vertices_.size());
    // リソースの先頭のアドレスから使う
    vertexBufferView_.BufferLocation = vertexResource_->GetGPUVirtualAddress();
    // 使用するリソースのサイズは頂点6つ分のサイズ
    vertexBufferView_.SizeInBytes = UINT(sizeof(SkyBoxVertexData3D) * vertices_.size());
    // 1頂点あたりのサイズ
    vertexBufferView_.StrideInBytes = sizeof(SkyBoxVertexData3D);

    // 頂点データの設定
    vertexResource_->Map(0, nullptr, reinterpret_cast<void **>(&vertexData_));

    std::memcpy(vertexData_, vertices_.data(), sizeof(SkyBoxVertexData3D) * vertices_.size());
}

void SkyBox::CreateIndex() {
    indexResource_ = dxCommon_->CreateBufferResource(sizeof(uint32_t) * indices_.size());
    indexBufferView_.BufferLocation = indexResource_->GetGPUVirtualAddress();
    indexBufferView_.SizeInBytes = UINT(sizeof(uint32_t) * indices_.size());
    indexBufferView_.Format = DXGI_FORMAT_R32_UINT;
    indexResource_->Map(0, nullptr, reinterpret_cast<void **>(&indexData_));
    std::memcpy(indexData_, indices_.data(), sizeof(uint32_t) * indices_.size());
}

void SkyBox::CreateSkyBox() {
    skyBoxResource_ = dxCommon_->CreateBufferResource(sizeof(SkyBoxDataForGPU));
    skyBoxData_ = nullptr;
    skyBoxResource_->Map(0, nullptr, reinterpret_cast<void **>(&skyBoxData_));
    skyBoxData_->worldMatrix = MakeIdentity4x4();
}

void SkyBox::CreateCamera() {
    cameraResource_ = dxCommon_->CreateBufferResource(sizeof(CameraDataForGPU));
    cameraData_ = nullptr;
    cameraResource_->Map(0, nullptr, reinterpret_cast<void **>(&cameraData_));

    // 初期値を設定
    cameraData_->viewProjection = MakeIdentity4x4();
    cameraData_->worldPosition = {0.0f, 0.0f, 0.0f};
    cameraData_->padding = 0.0f;
}
} // namespace Hagine
