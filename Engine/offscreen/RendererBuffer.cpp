#include "RendererBuffer.h"

namespace Hagine {
void RenderBuffer::Initialize(DirectXCommon *pDxCommon, SrvManager *pSrvManager)
{
    assert(pDxCommon);
    assert(pSrvManager);
    pDxCommon_ = pDxCommon;
    pSrvManager_ = pSrvManager;
    CreatePingPongBuffers();
    CreateFinalResultTexture();
    CreateComputeScratchTexture();
}

void RenderBuffer::CreateComputeScratchTexture()
{
    // 分離フィルタのようにパスを重ねるCSでは、入力バッファへそのまま書き戻せない。
    // ピンポン2枚は入力用と出力用で埋まっているので、中間置き場を1枚別に用意する。
    D3D12_CLEAR_VALUE clearValue = pDxCommon_->GetClearColorValue();
    clearValue.Format = kPingPongFormat;

    computeScratchResource_ = pDxCommon_->CreateRenderTextureResource(
        WinApp::GetVirtualWidth(), WinApp::GetVirtualHeight(),
        kPingPongFormat, clearValue, /*allowUAV=*/true);

    const uint32_t srvIndex = pSrvManager_->Allocate() + 1;
    pSrvManager_->CreateSRVforRenderTexture(srvIndex, computeScratchResource_.Get(), kPingPongFormat);
    computeScratchSrvHandleCPU_ = pSrvManager_->GetCPUDescriptorHandle(srvIndex);

    const uint32_t uavIndex = pSrvManager_->Allocate() + 1;
    pSrvManager_->CreateUAVforTexture2D(uavIndex, computeScratchResource_.Get(), kPingPongFormat);
    computeScratchUavHandleGPU_ = pSrvManager_->GetGPUDescriptorHandle(uavIndex);
}

void RenderBuffer::CreatePingPongBuffers()
{
    // ピンポンバッファはリニア空間の FP16 で持つ。
    //  ・sRGB フォーマットには UAV を作れないため、コンピュートシェーダーで書くには非sRGBが必須
    //  ・8bit だとエフェクトを重ねるたびに階調が落ちる（ブラー・ブルーム・DoFで顕著）
    // シーンのオフスクリーンは sRGB のままなので、そこからの読み出しはハードウェアが
    // リニアへ戻してくれる。逆に最終結果テクスチャ（sRGB）へ書き戻すときに再びエンコードされる。
    D3D12_CLEAR_VALUE clearValue = pDxCommon_->GetClearColorValue();
    clearValue.Format = kPingPongFormat;

    for (int i = 0; i < kPingPongBufferCount; ++i)
    {
        // レンダーターゲット兼UAVとして作る（PS版エフェクトとCS版エフェクトの両方が書けるように）
        pingPongResources_[i] = pDxCommon_->CreateRenderTextureResource(
            WinApp::GetVirtualWidth(), WinApp::GetVirtualHeight(),
            kPingPongFormat, clearValue, /*allowUAV=*/true);

        // SRV作成
        pingPongSrvIndices_[i] = pSrvManager_->Allocate() + 1;
        pSrvManager_->CreateSRVforRenderTexture(pingPongSrvIndices_[i], pingPongResources_[i].Get(), kPingPongFormat);
        pingPongSrvHandlesCPU_[i] = pSrvManager_->GetCPUDescriptorHandle(pingPongSrvIndices_[i]);
        pingPongSrvHandlesGPU_[i] = pSrvManager_->GetGPUDescriptorHandle(pingPongSrvIndices_[i]);

        // UAV作成（コンピュートシェーダーの書き込み先）
        pingPongUavIndices_[i] = pSrvManager_->Allocate() + 1;
        pSrvManager_->CreateUAVforTexture2D(pingPongUavIndices_[i], pingPongResources_[i].Get(), kPingPongFormat);
        pingPongUavHandlesGPU_[i] = pSrvManager_->GetGPUDescriptorHandle(pingPongUavIndices_[i]);

        // RTVハンドルを取得（DirectXCommonのRTVディスクリプタヒープから）
        // バックバッファ(0,1) + オフスクリーン(2) の後の位置を使用
        int rtvIndex = 3 + i; // オフスクリーン(2)の次から使用

        // RTVディスクリプタハンドルを取得
        D3D12_CPU_DESCRIPTOR_HANDLE rtvStartHandle = pDxCommon_->GetRTVDescriptorHeap()->GetCPUDescriptorHandleForHeapStart();
        UINT descriptorSize = pDxCommon_->GetDevice()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        pingPongRtvHandles_[i].ptr = rtvStartHandle.ptr + (rtvIndex * descriptorSize);

        // RTVを作成
        D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
        rtvDesc.Format = kPingPongFormat;
        rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;

        pDxCommon_->GetDevice()->CreateRenderTargetView(pingPongResources_[i].Get(), &rtvDesc, pingPongRtvHandles_[i]);
    }
}

void RenderBuffer::CreateFinalResultTexture()
{
    // 最終結果テクスチャは sRGB のまま据え置く。
    // ここには UI（スプライト・パーティクル・シーン遷移）も直接描き込まれるため、
    // FP16 にするとそれらのパイプラインまで別フォーマット版が必要になり波及が大きい。
    // リニアFP16なのは「エフェクトのチェーン内だけ」で、チェーンの出口で sRGB へ書き戻す。
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
