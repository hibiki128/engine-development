#include "RendererBuffer.h"

namespace Hagine {
void RenderBuffer::Initialize(DirectXCommon *dxCommon, SrvManager *srvManager)
{
    assert(dxCommon);
    assert(srvManager);
    pDxCommon_ = dxCommon;
    pSrvManager_ = srvManager;
    CreatePingPongBuffers();
    CreateFinalResultTexture();
}

void RenderBuffer::CreatePingPongBuffers()
{
    // ピンポンバッファを作成
    for (int i = 0; i < kPingPongBufferCount; ++i)
    {
        // レンダーターゲットリソースを作成
        pingPongResources_[i] = pDxCommon_->CreateRenderTextureResource(WinApp::GetVirtualWidth(), WinApp::GetVirtualHeight(), pDxCommon_->GetClearColorValue().Format, pDxCommon_->GetClearColorValue());

        // SRV作成
        pingPongSrvIndices_[i] = pSrvManager_->Allocate() + 1;
        pSrvManager_->CreateSRVforRenderTexture(pingPongSrvIndices_[i], pingPongResources_[i].Get());
        pingPongSrvHandlesCPU_[i] = pSrvManager_->GetCPUDescriptorHandle(pingPongSrvIndices_[i]);
        pingPongSrvHandlesGPU_[i] = pSrvManager_->GetGPUDescriptorHandle(pingPongSrvIndices_[i]);

        // RTVハンドルを取得（DirectXCommonのRTVディスクリプタヒープから）
        // バックバッファ(0,1) + オフスクリーン(2) の後の位置を使用
        int rtvIndex = 3 + i; // オフスクリーン(2)の次から使用

        // RTVディスクリプタハンドルを取得
        D3D12_CPU_DESCRIPTOR_HANDLE rtvStartHandle = pDxCommon_->GetRTVDescriptorHeap()->GetCPUDescriptorHandleForHeapStart();
        UINT descriptorSize = pDxCommon_->GetDevice()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        pingPongRtvHandles_[i].ptr = rtvStartHandle.ptr + (rtvIndex * descriptorSize);

        // RTVを作成
        D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
        rtvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;

        pDxCommon_->GetDevice()->CreateRenderTargetView(pingPongResources_[i].Get(), &rtvDesc, pingPongRtvHandles_[i]);
    }
}

void RenderBuffer::CreateFinalResultTexture()
{
    // 最終結果用のレンダーターゲットリソースを作成
    finalResultResource_ = pDxCommon_->CreateRenderTextureResource(
        WinApp::GetVirtualWidth(),
        WinApp::GetVirtualHeight(),
        pDxCommon_->GetClearColorValue().Format,
        pDxCommon_->GetClearColorValue());

    // SRV作成（ImGui表示用）
    finalResultSrvIndex_ = pSrvManager_->Allocate() + 1;
    pSrvManager_->CreateSRVforRenderTexture(finalResultSrvIndex_, finalResultResource_.Get());
    finalResultSrvHandleCPU_ = pSrvManager_->GetCPUDescriptorHandle(finalResultSrvIndex_);
    finalResultSrvHandleGPU_ = pSrvManager_->GetGPUDescriptorHandle(finalResultSrvIndex_);

    // RTVハンドルを取得（ピンポンバッファの次の位置を使用）
    int rtvIndex = 3 + kPingPongBufferCount; // バックバッファ(0,1) + オフスクリーン(2) + ピンポンバッファ(3,4) の次

    D3D12_CPU_DESCRIPTOR_HANDLE rtvStartHandle = pDxCommon_->GetRTVDescriptorHeap()->GetCPUDescriptorHandleForHeapStart();
    UINT descriptorSize = pDxCommon_->GetDevice()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    finalResultRtvHandle_.ptr = rtvStartHandle.ptr + (rtvIndex * descriptorSize);

    // RTVを作成
    D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
    rtvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;

    pDxCommon_->GetDevice()->CreateRenderTargetView(finalResultResource_.Get(), &rtvDesc, finalResultRtvHandle_);
}
} // namespace Hagine
