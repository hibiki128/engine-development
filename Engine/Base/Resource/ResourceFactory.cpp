#include "ResourceFactory.h"
#include "DXDevice.h"
#include "DirectXTex/d3dx12.h"
#include "cassert"
#include <vector>

namespace Hagine {

void ResourceFactory::Initialize(DXDevice *device)
{
    assert(device);
    pDevice_ = device;
}

void ResourceFactory::Finalize()
{
    dispatchIndirectCommandSignature_.Reset();
}

Microsoft::WRL::ComPtr<ID3D12Resource> ResourceFactory::CreateBufferResource(size_t sizeInBytes, bool isUAV)
{
    if (!isUAV)
    {
        // リソース用のヒープの設定
        D3D12_HEAP_PROPERTIES uploadHeapProperties{};
        uploadHeapProperties.Type = D3D12_HEAP_TYPE_UPLOAD; // UploadHeapを使う
        // リソースの設定
        D3D12_RESOURCE_DESC resourceDesc{};
        // バッファリソース。テクスチャの場合はまた別の設定をする
        resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        resourceDesc.Width = sizeInBytes; // リソースのサイズ。
        // バッファの場合はこれらを1にする決まり
        resourceDesc.Height = 1;
        resourceDesc.DepthOrArraySize = 1;
        resourceDesc.MipLevels = 1;
        resourceDesc.SampleDesc.Count = 1;
        // バッファの場合はこれにする決まり
        resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        // 実際にリソースを作る
        Microsoft::WRL::ComPtr<ID3D12Resource> resource = nullptr;
        HRESULT hr = pDevice_->Get()->CreateCommittedResource(&uploadHeapProperties, D3D12_HEAP_FLAG_NONE,
                                                             &resourceDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                             IID_PPV_ARGS(&resource));
        assert(SUCCEEDED(hr));

        return resource;
    }
    else
    {

        // UAVを使う場合は、デフォルトヒープを使う
        D3D12_HEAP_PROPERTIES defaultHeapProperties{};
        defaultHeapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;
        // UAV用バッファリソースの設定
        D3D12_RESOURCE_DESC resourceDesc{};
        resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        resourceDesc.Width = sizeInBytes;
        resourceDesc.Height = 1;
        resourceDesc.DepthOrArraySize = 1;
        resourceDesc.MipLevels = 1;
        resourceDesc.SampleDesc.Count = 1;
        resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        // UAVを使うためのフラグ
        resourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        Microsoft::WRL::ComPtr<ID3D12Resource> resource = nullptr;
        HRESULT hr = pDevice_->Get()->CreateCommittedResource(&defaultHeapProperties, D3D12_HEAP_FLAG_NONE,
                                                             &resourceDesc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&resource));
        assert(SUCCEEDED(hr));
        return resource;
    }
}

Microsoft::WRL::ComPtr<ID3D12Resource> ResourceFactory::CreateTextureResource(const DirectX::TexMetadata &metadata)
{
    // metadataを基にResourceの設定
    D3D12_RESOURCE_DESC resourceDesc{};
    resourceDesc.Width = UINT(metadata.width);                             // Textureの幅
    resourceDesc.Height = UINT(metadata.height);                           // Textureの高さ
    resourceDesc.MipLevels = UINT16(metadata.mipLevels);                   // mipmapの数
    resourceDesc.DepthOrArraySize = UINT16(metadata.arraySize);            // 奥行き or 配列Textureの配列数
    resourceDesc.Format = metadata.format;                                 // TextureのFormat
    resourceDesc.SampleDesc.Count = 1;                                     // サンプリングカウント。1固定
    resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION(metadata.dimension); // Textureの次元数

    // 利用するHeapの設定
    D3D12_HEAP_PROPERTIES heapProperties{};
    heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;                    // 細かい設定を行う
    heapProperties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN; // WriteBackポリシーでCPUアクセス可能
    heapProperties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;  // プロセッサの近くに配置

    // Resourcesの生成
    Microsoft::WRL::ComPtr<ID3D12Resource> resource = nullptr;
    HRESULT hr = pDevice_->Get()->CreateCommittedResource(
        &heapProperties,                // Heapの設定
        D3D12_HEAP_FLAG_NONE,           // Heapの特殊な設定。
        &resourceDesc,                  // Resourceの設定
        D3D12_RESOURCE_STATE_COPY_DEST, // 初回のResourceState。Textureは基本読むだけ
        nullptr,                        // Clear最適値。使わないのでnullptr
        IID_PPV_ARGS(&resource));       // 作成するResourceポインタへのポインタ
    assert(SUCCEEDED(hr));
    return resource;
}

Microsoft::WRL::ComPtr<ID3D12Resource> ResourceFactory::CreateRenderTextureResource(uint32_t width, uint32_t height, DXGI_FORMAT format, D3D12_CLEAR_VALUE color)
{
    D3D12_RESOURCE_DESC resourceDesc{};
    resourceDesc.Width = width;
    resourceDesc.Height = height;
    resourceDesc.MipLevels = 1;
    resourceDesc.DepthOrArraySize = 1;
    resourceDesc.Format = format;
    resourceDesc.SampleDesc.Count = 1;
    resourceDesc.SampleDesc.Quality = 0;
    resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    resourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

    D3D12_HEAP_PROPERTIES heapProperties{};
    heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;

    Microsoft::WRL::ComPtr<ID3D12Resource> resource = nullptr;
    HRESULT hr = pDevice_->Get()->CreateCommittedResource(&heapProperties,
                                                         D3D12_HEAP_FLAG_NONE, &resourceDesc,
                                                         D3D12_RESOURCE_STATE_GENERIC_READ, &color,
                                                         IID_PPV_ARGS(&resource));
    assert(SUCCEEDED(hr));

    return resource;
}

Microsoft::WRL::ComPtr<ID3D12Resource> ResourceFactory::CreateDepthStencilTextureResource(int32_t width, int32_t height)
{
    // 生成するResourceの設定
    D3D12_RESOURCE_DESC resourceDesc{};
    resourceDesc.Width = width;                          // Textureの幅
    resourceDesc.Height = height;                        // Textureの高さ
    resourceDesc.MipLevels = 1;                          // mipmapの数
    resourceDesc.DepthOrArraySize = 1;                   // 奥行き or 配列Textureの配列数
    resourceDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT; // Depthstencilとして利用可能なフォーマット
    resourceDesc.SampleDesc.Count = 1;                   // サンプリングカウント。1固定。
    resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    // DepthStencilとして使う通知
    resourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    // 利用するHeapの設定
    D3D12_HEAP_PROPERTIES heapProperties{};
    heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT; // VRAM上に作る

    // 深度値のクリア設定
    D3D12_CLEAR_VALUE depthClearValue{};
    depthClearValue.DepthStencil.Depth = 1.0f;              // 1.0f (最大値) でクリア
    depthClearValue.Format = DXGI_FORMAT_D24_UNORM_S8_UINT; // フォーマット。Resourceと合わせる

    // Resourceの生成
    Microsoft::WRL::ComPtr<ID3D12Resource> resource = nullptr;
    HRESULT hr = pDevice_->Get()->CreateCommittedResource(
        &heapProperties,                  // Heapの設定
        D3D12_HEAP_FLAG_NONE,             // Heapの特殊な設定。特になし。
        &resourceDesc,                    // Resourceの設定
        D3D12_RESOURCE_STATE_DEPTH_WRITE, // 深度値を書き込む状態にしておく
        &depthClearValue,                 // clear最適値
        IID_PPV_ARGS(&resource));         // 作成するResourceポインタへのポインタ
    assert(SUCCEEDED(hr));

    return resource;
}

Microsoft::WRL::ComPtr<ID3D12Resource> ResourceFactory::UploadTextureData(Microsoft::WRL::ComPtr<ID3D12Resource> texture, const DirectX::ScratchImage &mipImages, ID3D12GraphicsCommandList *commandList)
{
    std::vector<D3D12_SUBRESOURCE_DATA> subresources;
    DirectX::PrepareUpload(pDevice_->Get(), mipImages.GetImages(), mipImages.GetImageCount(), mipImages.GetMetadata(), subresources);
    uint64_t intermediateSize = GetRequiredIntermediateSize(texture.Get(), 0, UINT(subresources.size()));
    Microsoft::WRL::ComPtr<ID3D12Resource> intermediateResource = CreateBufferResource(intermediateSize);
    UpdateSubresources(commandList, texture.Get(), intermediateResource.Get(), 0, 0, UINT(subresources.size()), subresources.data());
    // Textureへの転送後は利用できるよう、D3D12_RESOURCE_STATE_COPY_DESTからD3D12_RESOURCE_STATE_GENERIC_READへResourceStateを変更する
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Transition.pResource = texture.Get();
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_GENERIC_READ;
    commandList->ResourceBarrier(1, &barrier);
    return intermediateResource;
}

ID3D12CommandSignature *ResourceFactory::GetDispatchIndirectCommandSignature()
{
    // 初回呼び出し時に遅延生成する（未使用なら一切作られない）
    if (!dispatchIndirectCommandSignature_)
    {
        D3D12_INDIRECT_ARGUMENT_DESC arg{};
        arg.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH;

        D3D12_COMMAND_SIGNATURE_DESC desc{};
        desc.ByteStride = sizeof(D3D12_DISPATCH_ARGUMENTS); // 12 バイト (uint x3)
        desc.NumArgumentDescs = 1;
        desc.pArgumentDescs = &arg;
        // DISPATCH のみでルート引数を差し替えないため pRootSignature は nullptr で良い。
        HRESULT hr = pDevice_->Get()->CreateCommandSignature(&desc, nullptr, IID_PPV_ARGS(&dispatchIndirectCommandSignature_));
        assert(SUCCEEDED(hr));
    }
    return dispatchIndirectCommandSignature_.Get();
}
} // namespace Hagine
