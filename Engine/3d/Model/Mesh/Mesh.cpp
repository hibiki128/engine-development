#include "Mesh.h"
#include "DirectXCommon.h"
namespace Hagine {
void Mesh::Initialize() {
    dxCommon_ = DirectXCommon::GetInstance();

    CreateVartexData();
    CreateIndexResource();
}

void Mesh::PrimitiveInitialize(const PrimitiveType &type) {
    meshData_.vertices = PrimitiveModel::GetInstance()->GetPrimitiveData(type).vertices;
    meshData_.indices = PrimitiveModel::GetInstance()->GetPrimitiveData(type).indices;
}

void Mesh::CreateVartexData() {
    vertexResource_ = dxCommon_->CreateBufferResource(sizeof(VertexData) * meshData_.vertices.size());
    // リソースの先頭のアドレスから使う
    vertexBufferView_.BufferLocation = vertexResource_->GetGPUVirtualAddress();
    // 使用するリソースのサイズは頂点6つ分のサイズ
    vertexBufferView_.SizeInBytes = UINT(sizeof(VertexData) * meshData_.vertices.size());
    // 1頂点あたりのサイズ
    vertexBufferView_.StrideInBytes = sizeof(VertexData);

    // 頂点データの設定
    vertexResource_->Map(0, nullptr, reinterpret_cast<void **>(&vertexData_));

    std::memcpy(vertexData_, meshData_.vertices.data(), sizeof(VertexData) * meshData_.vertices.size());
}

void Mesh::CreateIndexResource() {
    indexResource_ = dxCommon_->CreateBufferResource(sizeof(uint32_t) * meshData_.indices.size());
    indexBufferView_.BufferLocation = indexResource_->GetGPUVirtualAddress();
    indexBufferView_.SizeInBytes = UINT(sizeof(uint32_t) * meshData_.indices.size());
    indexBufferView_.Format = DXGI_FORMAT_R32_UINT;
    indexResource_->Map(0, nullptr, reinterpret_cast<void **>(&indexData_));
    std::memcpy(indexData_, meshData_.indices.data(), sizeof(uint32_t) * meshData_.indices.size());
}
} // namespace Hagine
