#define NOMINMAX
#include "Mesh.h"
#include "DirectXCommon.h"
#include <algorithm>

namespace Hagine {
void Mesh::Initialize()
{
    pDxCommon_ = DirectXCommon::GetInstance();

    vertexCount_ = static_cast<uint32_t>(meshData_.vertices.size());
    indexCount_ = static_cast<uint32_t>(meshData_.indices.size());

    CreateVertexData();
    CreateIndexResource();
}

void Mesh::PrimitiveInitialize(const PrimitiveType &type)
{
    meshData_.vertices = PrimitiveModel::GetInstance()->GetPrimitiveData(type).vertices;
    meshData_.indices = PrimitiveModel::GetInstance()->GetPrimitiveData(type).indices;
}

void Mesh::PrimitiveInitialize(const PrimitiveType &type, const PrimitiveParams &params)
{
    auto data = PrimitiveModel::GetInstance()->BuildParametricData(type, params);
    meshData_.vertices = data.vertices;
    meshData_.indices = data.indices;
}

void Mesh::InitializeDynamic(uint32_t vertexCapacity, uint32_t indexCapacity)
{
    pDxCommon_ = DirectXCommon::GetInstance();
    isDynamic_ = true;
    vertexCount_ = 0;
    indexCount_ = 0;

    // 0 個だと D3D のバッファ生成が失敗するので最低限は確保しておく
    AllocateDynamicBuffers((std::max)(vertexCapacity, 3u), (std::max)(indexCapacity, 3u));

    // 中身が空のうちは描画されないよう、ビューだけ先頭スロットに向けておく
    dynamicSlot_ = 0;
    vertexResource_ = dynamicVertexResource_[0];
    indexResource_ = dynamicIndexResource_[0];
    vertexBufferView_.BufferLocation = vertexResource_->GetGPUVirtualAddress();
    vertexBufferView_.SizeInBytes = 0;
    vertexBufferView_.StrideInBytes = sizeof(VertexData);
    indexBufferView_.BufferLocation = indexResource_->GetGPUVirtualAddress();
    indexBufferView_.SizeInBytes = 0;
    indexBufferView_.Format = DXGI_FORMAT_R32_UINT;
}

void Mesh::AllocateDynamicBuffers(uint32_t vertexCapacity, uint32_t indexCapacity)
{
    for (uint32_t i = 0; i < kDynamicBufferCount; ++i)
    {
        dynamicVertexResource_[i] = pDxCommon_->CreateBufferResource(sizeof(VertexData) * vertexCapacity);
        dynamicVertexResource_[i]->Map(0, nullptr, reinterpret_cast<void **>(&pDynamicVertexData_[i]));

        dynamicIndexResource_[i] = pDxCommon_->CreateBufferResource(sizeof(uint32_t) * indexCapacity);
        dynamicIndexResource_[i]->Map(0, nullptr, reinterpret_cast<void **>(&pDynamicIndexData_[i]));
    }
    vertexCapacity_ = vertexCapacity;
    indexCapacity_ = indexCapacity;
}

void Mesh::Rebuild(MeshData &&data)
{
    assert(isDynamic_ && "Rebuild は InitializeDynamic した Mesh にしか使えません");

    meshData_ = std::move(data);
    vertexCount_ = static_cast<uint32_t>(meshData_.vertices.size());
    indexCount_ = static_cast<uint32_t>(meshData_.indices.size());

    if (vertexCount_ > vertexCapacity_ || indexCount_ > indexCapacity_)
    {
        // 伸ばす場合は古いバッファを GPU が読み終わってから捨てる必要がある。
        // 容量を超えるのは形を大きく変えたときだけなので、ここで待ってしまってよい
        pDxCommon_->WaitForGPU();
        const uint32_t newVertexCapacity = (std::max)(vertexCount_, vertexCapacity_ * 2);
        const uint32_t newIndexCapacity = (std::max)(indexCount_, indexCapacity_ * 2);
        AllocateDynamicBuffers(newVertexCapacity, newIndexCapacity);
        dynamicSlot_ = 0;
    }
    else
    {
        // 描画中のバッファを踏まないよう、書き込み先をフレームごとに入れ替える
        dynamicSlot_ = (dynamicSlot_ + 1) % kDynamicBufferCount;
    }

    if (vertexCount_ > 0)
    {
        std::memcpy(pDynamicVertexData_[dynamicSlot_], meshData_.vertices.data(),
                    sizeof(VertexData) * vertexCount_);
    }
    if (indexCount_ > 0)
    {
        std::memcpy(pDynamicIndexData_[dynamicSlot_], meshData_.indices.data(),
                    sizeof(uint32_t) * indexCount_);
    }

    // ビューを今回書いたスロットに向け直す
    vertexResource_ = dynamicVertexResource_[dynamicSlot_];
    indexResource_ = dynamicIndexResource_[dynamicSlot_];
    vertexBufferView_.BufferLocation = vertexResource_->GetGPUVirtualAddress();
    vertexBufferView_.SizeInBytes = static_cast<UINT>(sizeof(VertexData) * vertexCount_);
    vertexBufferView_.StrideInBytes = sizeof(VertexData);
    indexBufferView_.BufferLocation = indexResource_->GetGPUVirtualAddress();
    indexBufferView_.SizeInBytes = static_cast<UINT>(sizeof(uint32_t) * indexCount_);
    indexBufferView_.Format = DXGI_FORMAT_R32_UINT;
}

void Mesh::CreateVertexData()
{
    vertexResource_ = pDxCommon_->CreateBufferResource(sizeof(VertexData) * meshData_.vertices.size());
    // リソースの先頭のアドレスから使う
    vertexBufferView_.BufferLocation = vertexResource_->GetGPUVirtualAddress();
    // 使用するリソースのサイズは頂点6つ分のサイズ
    vertexBufferView_.SizeInBytes = UINT(sizeof(VertexData) * meshData_.vertices.size());
    // 1頂点あたりのサイズ
    vertexBufferView_.StrideInBytes = sizeof(VertexData);

    // 頂点データの設定
    vertexResource_->Map(0, nullptr, reinterpret_cast<void **>(&pVertexData_));

    std::memcpy(pVertexData_, meshData_.vertices.data(), sizeof(VertexData) * meshData_.vertices.size());
}

void Mesh::CreateIndexResource()
{
    indexResource_ = pDxCommon_->CreateBufferResource(sizeof(uint32_t) * meshData_.indices.size());
    indexBufferView_.BufferLocation = indexResource_->GetGPUVirtualAddress();
    indexBufferView_.SizeInBytes = UINT(sizeof(uint32_t) * meshData_.indices.size());
    indexBufferView_.Format = DXGI_FORMAT_R32_UINT;
    indexResource_->Map(0, nullptr, reinterpret_cast<void **>(&pIndexData_));
    std::memcpy(pIndexData_, meshData_.indices.data(), sizeof(uint32_t) * meshData_.indices.size());
}
} // namespace Hagine
