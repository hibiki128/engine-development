#include "SrvManager.h"
#include "DirectXCommon.h"
#include <cstdlib>

namespace Hagine {
const uint32_t SrvManager::kMaxSRVCount = 49152;

void SrvManager::Finalize() {
    descriptorHeap_.Reset();
    while (!freeIndices_.empty()) {
        freeIndices_.pop();
    }
}

void SrvManager::Initialize() {
    this->dxCommon_ = DirectXCommon::GetInstance();

    // デスクリプタヒープの生成
    descriptorHeap_ = dxCommon_->CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, kMaxSRVCount, true);
    // デスクリプタ1個分のサイズを取得して記録
    descriptorSize_ = dxCommon_->GetDevice()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
}

void SrvManager::SetDescriptorHeap() {
    ID3D12GraphicsCommandList *commandList = dxCommon_->GetCommandList().Get();
    ID3D12DescriptorHeap *descriptorHeaps[] = {descriptorHeap_.Get()};
    commandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);
}

void SrvManager::CreateSRVforTexture2D(uint32_t srvIndex, ID3D12Resource *pResource, DirectX::TexMetadata metaData, UINT MipLevels) {
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = metaData.format;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    if (metaData.IsCubemap()) {
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
        srvDesc.TextureCube.MostDetailedMip = 0;
        srvDesc.TextureCube.MipLevels = UINT_MAX;
        srvDesc.TextureCube.ResourceMinLODClamp = 0.0f;
    } else {
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = MipLevels;
    }

    dxCommon_->GetDevice()->CreateShaderResourceView(pResource, &srvDesc, GetCPUDescriptorHandle(srvIndex));
}

void SrvManager::CreateSRVforStructuredBuffer(uint32_t srvIndex, ID3D12Resource *pResource, UINT numElements, UINT structureByteStride) {
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = DXGI_FORMAT_UNKNOWN;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    srvDesc.Buffer.FirstElement = 0;
    srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
    srvDesc.Buffer.NumElements = numElements;
    srvDesc.Buffer.StructureByteStride = structureByteStride;

    dxCommon_->GetDevice()->CreateShaderResourceView(pResource, &srvDesc, GetCPUDescriptorHandle(srvIndex));
}

D3D12_CPU_DESCRIPTOR_HANDLE SrvManager::GetCPUDescriptorHandle(uint32_t index) {
    D3D12_CPU_DESCRIPTOR_HANDLE handleCPU = descriptorHeap_->GetCPUDescriptorHandleForHeapStart();
    handleCPU.ptr += (descriptorSize_ * index);
    return handleCPU;
}

D3D12_GPU_DESCRIPTOR_HANDLE SrvManager::GetGPUDescriptorHandle(uint32_t index) {
    D3D12_GPU_DESCRIPTOR_HANDLE handleGPU = descriptorHeap_->GetGPUDescriptorHandleForHeapStart();
    handleGPU.ptr += (descriptorSize_ * index);
    return handleGPU;
}

void SrvManager::SetGraphicsRootDescriptorTable(UINT RootParameterIndex, uint32_t srvIndex) {
    dxCommon_->GetCommandList()->SetGraphicsRootDescriptorTable(RootParameterIndex, GetGPUDescriptorHandle(srvIndex));
}

void SrvManager::CreateSRVforRenderTexture(uint32_t srvIndex, ID3D12Resource *pResource) {
    // SRVの設定。FormatはResourceと同じにしておく
    D3D12_SHADER_RESOURCE_VIEW_DESC renderTextureSrvDesc{};
    renderTextureSrvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    renderTextureSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    renderTextureSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    renderTextureSrvDesc.Texture2D.MipLevels = 1;

    // SRVの生成
    dxCommon_->GetDevice()->CreateShaderResourceView(pResource, &renderTextureSrvDesc, GetCPUDescriptorHandle(srvIndex));
}

void SrvManager::CreateSRVforDepth(uint32_t srvIndex, ID3D12Resource *pResource) {
    D3D12_SHADER_RESOURCE_VIEW_DESC depthTextureSrvDesc{};
    // DXGI_FORMAT_D24_UNORM_S8_UINTのDepthを読むときはDZGI_FORMAT_R24_UNORM_X8_TYPELESS
    depthTextureSrvDesc.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
    depthTextureSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    depthTextureSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    depthTextureSrvDesc.Texture2D.MipLevels = 1;

    dxCommon_->GetDevice()->CreateShaderResourceView(pResource, &depthTextureSrvDesc, GetCPUDescriptorHandle(srvIndex));
}

void SrvManager::CreateUAVStructuredBuffer(uint32_t srvIndex, ID3D12Resource *pResource, UINT numElements, UINT structureByteStride) {
    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
    uavDesc.Format = DXGI_FORMAT_UNKNOWN;
    uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    uavDesc.Buffer.FirstElement = 0;
    uavDesc.Buffer.NumElements = numElements;
    uavDesc.Buffer.CounterOffsetInBytes = 0;
    uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
    uavDesc.Buffer.StructureByteStride = structureByteStride;

    dxCommon_->GetDevice()->CreateUnorderedAccessView(pResource, nullptr, &uavDesc, GetCPUDescriptorHandle(srvIndex));
}

uint32_t SrvManager::Allocate() {
    // 空きインデックスがあれば、それを使用
    if (!freeIndices_.empty()) {
        uint32_t index = freeIndices_.front();
        freeIndices_.pop();
        return index;
    }

    assert(useIndex_ < kMaxSRVCount);

    // returnする番号を一旦記録しておく
    uint32_t index = useIndex_;
    // 次のインデックスへ進める
    useIndex_++;
    // 上で記録した番号をreturn
    return index;
}

void SrvManager::Free(uint32_t srvIndex) {
    // ディスクリプタをクリアしてから解放
    ClearDescriptor(srvIndex);

    // 解放するインデックスを空きリストに追加
    freeIndices_.push(srvIndex);
}

// SRVの最大数チェック
bool SrvManager::CanAllocate() const {
    // useIndexがkMaxSRVCount未満、もしくは空きインデックスがあればtrueを返す
    return useIndex_ < kMaxSRVCount || !freeIndices_.empty();
}

void SrvManager::ClearDescriptor(uint32_t srvIndex) {
    // ダミーのNULL SRVを作成してディスクリプタを無効化
    D3D12_SHADER_RESOURCE_VIEW_DESC nullDesc{};
    nullDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    nullDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    nullDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    nullDesc.Texture2D.MipLevels = 1;

    dxCommon_->GetDevice()->CreateShaderResourceView(nullptr, &nullDesc, GetCPUDescriptorHandle(srvIndex));
}
} // namespace Hagine
