#include "DsvManager.h"
#include "DXDevice.h"
#include "cassert"

namespace Hagine {

void DsvManager::Initialize(DXDevice *pDevice)
{
    assert(pDevice);
    pDevice_ = pDevice;

    // デスクリプタ1つ分のサイズを取得
    descriptorSize_ = pDevice_->Get()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);

    // DSV用のヒープ。DSVはShader内で触るものではないので、ShaderVisibleはfalse
    heap_ = pDevice_->CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_DSV, kMaxDSVCount, false);
}

void DsvManager::Finalize()
{
    heap_.Reset();
}

D3D12_CPU_DESCRIPTOR_HANDLE DsvManager::Create(uint32_t index, ID3D12Resource *resource, DXGI_FORMAT format)
{
    assert(index < kMaxDSVCount);

    D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
    dsvDesc.Format = format;                               // Format。基本的にはResourceに合わせる
    dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D; // 2dTexture

    D3D12_CPU_DESCRIPTOR_HANDLE handle = GetCPUHandle(index);
    pDevice_->Get()->CreateDepthStencilView(resource, &dsvDesc, handle);
    return handle;
}

D3D12_CPU_DESCRIPTOR_HANDLE DsvManager::GetCPUHandle(uint32_t index) const
{
    D3D12_CPU_DESCRIPTOR_HANDLE handle = heap_->GetCPUDescriptorHandleForHeapStart();
    handle.ptr += static_cast<SIZE_T>(descriptorSize_) * index;
    return handle;
}

D3D12_GPU_DESCRIPTOR_HANDLE DsvManager::GetGPUHandle(uint32_t index) const
{
    D3D12_GPU_DESCRIPTOR_HANDLE handle = heap_->GetGPUDescriptorHandleForHeapStart();
    handle.ptr += static_cast<UINT64>(descriptorSize_) * index;
    return handle;
}
} // namespace Hagine
