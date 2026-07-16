#include "RtvManager.h"
#include "DXDevice.h"
#include "cassert"

namespace Hagine {

void RtvManager::Initialize(DXDevice *device)
{
    assert(device);
    pDevice_ = device;

    // デスクリプタ1つ分のサイズを取得
    descriptorSize_ = pDevice_->Get()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    // RTV用のヒープ。RTVはShader内で触るものではないので、ShaderVisibleはfalse
    heap_ = pDevice_->CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, kMaxRTVCount, false);
}

void RtvManager::Finalize()
{
    heap_.Reset();
}

D3D12_CPU_DESCRIPTOR_HANDLE RtvManager::Create(uint32_t index, ID3D12Resource *resource, const D3D12_RENDER_TARGET_VIEW_DESC &desc)
{
    assert(index < kMaxRTVCount);
    D3D12_CPU_DESCRIPTOR_HANDLE handle = GetCPUHandle(index);
    pDevice_->Get()->CreateRenderTargetView(resource, &desc, handle);
    return handle;
}

D3D12_CPU_DESCRIPTOR_HANDLE RtvManager::GetCPUHandle(uint32_t index) const
{
    D3D12_CPU_DESCRIPTOR_HANDLE handle = heap_->GetCPUDescriptorHandleForHeapStart();
    handle.ptr += static_cast<SIZE_T>(descriptorSize_) * index;
    return handle;
}

D3D12_GPU_DESCRIPTOR_HANDLE RtvManager::GetGPUHandle(uint32_t index) const
{
    D3D12_GPU_DESCRIPTOR_HANDLE handle = heap_->GetGPUDescriptorHandleForHeapStart();
    handle.ptr += static_cast<UINT64>(descriptorSize_) * index;
    return handle;
}
} // namespace Hagine
