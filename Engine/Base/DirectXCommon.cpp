#include "DirectXCommon.h"
#include "cassert"
#include <Graphics/Srv/SrvManager.h>

namespace Hagine {

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dxcompiler.lib")

void DirectXCommon::Finalize() {
    // DXCコンパイラ関連の解放
    shaderCompiler_->Finalize();

    // オフスクリーン・深度リソースの解放
    offScreenResource_.Reset();
    depthStencilResource_.Reset();

    // デスクリプタヒープの解放
    rtvManager_->Finalize();
    dsvManager_->Finalize();

    // コンピュートキューの完了を待ってから解放
    FlushComputeQueue();
    computeCommandList_.reset();
    computeQueue_.reset();

    // コマンド関連の解放
    directCommandList_.reset();
    directQueue_.reset();

    // バックバッファ・スワップチェーンの解放
    swapChain_->Finalize();
    swapChain_.reset();

    // コマンドシグネチャの解放
    resourceFactory_->Finalize();
    resourceFactory_.reset();

    // デバイスは全リソース解放後に最後に解放する
    dxDevice_.reset();
}

void DirectXCommon::Initialize(WinApp *winApp) {

    // NULL検出
    assert(winApp);

    // メンバ変数に記録
    this->winApp_ = winApp;

    // FPS固定初期化
    fpsLimiter_ = std::make_unique<FrameRateLimiter>();
    fpsLimiter_->Initialize();

    // デバイスの生成
    dxDevice_ = std::make_unique<DXDevice>();
    dxDevice_->Initialize();

    // リソース生成の初期化
    resourceFactory_ = std::make_unique<ResourceFactory>();
    resourceFactory_->Initialize(dxDevice_.get());

    // Direct コマンドキュー・コマンドリストの初期化
    directQueue_ = std::make_unique<DXCommandQueue>();
    directQueue_->Initialize(dxDevice_.get(), D3D12_COMMAND_LIST_TYPE_DIRECT);
    directCommandList_ = std::make_unique<DXCommandList>();
    directCommandList_->Initialize(dxDevice_.get(), D3D12_COMMAND_LIST_TYPE_DIRECT);

    // スワップチェーンの生成
    swapChain_ = std::make_unique<DXSwapChain>();
    swapChain_->Initialize(dxDevice_->GetFactory(), directQueue_->Get(), winApp_->GetHwnd(), WinApp::kClientWidth, WinApp::kClientHeight);

    // 深度バッファの生成
    depthStencilResource_ = resourceFactory_->CreateDepthStencilTextureResource(WinApp::kClientWidth, WinApp::kClientHeight);

    // RTV / DSV 管理の初期化
    rtvManager_ = std::make_unique<RtvManager>();
    rtvManager_->Initialize(dxDevice_.get());
    dsvManager_ = std::make_unique<DsvManager>();
    dsvManager_->Initialize(dxDevice_.get());

    // レンダーターゲットビューの初期化
    RenderTargetViewInitialize();

    // 深度ステンシルビューの初期化（DSVHeapの先頭 slot 0 に作る）
    dsvManager_->Create(0, depthStencilResource_.Get(), DXGI_FORMAT_D24_UNORM_S8_UINT);

    // ビューポート矩形の初期化
    ViewPortRectInitialize();
    // シザリング矩形の初期化
    ScissorRectInitialize();

    // DXCコンパイラの生成
    shaderCompiler_ = std::make_unique<ShaderCompiler>();
    shaderCompiler_->Initialize();

    // 非同期コンピュートキュー・コマンドリストの初期化
    computeQueue_ = std::make_unique<DXCommandQueue>();
    computeQueue_->Initialize(dxDevice_.get(), D3D12_COMMAND_LIST_TYPE_COMPUTE);
    computeCommandList_ = std::make_unique<DXCommandList>();
    computeCommandList_->Initialize(dxDevice_.get(), D3D12_COMMAND_LIST_TYPE_COMPUTE);
}

void DirectXCommon::CreateOffscreenSRV() {
    offScreenSrvIndex_ = SrvManager::GetInstance()->Allocate();
    SrvManager::GetInstance()->CreateSRVforRenderTexture(offScreenSrvIndex_, offScreenResource_.Get());
    offScreenSrvHandleCPU_ = SrvManager::GetInstance()->GetCPUDescriptorHandle(offScreenSrvIndex_);
    offScreenSrvHandleGPU_ = SrvManager::GetInstance()->GetGPUDescriptorHandle(offScreenSrvIndex_);
}

void DirectXCommon::CreateDepthSRV() {
    depthSrvIndex_ = SrvManager::GetInstance()->Allocate();
    SrvManager::GetInstance()->CreateSRVforDepth(depthSrvIndex_, depthStencilResource_.Get());
    depthSrvHandleCPU_ = SrvManager::GetInstance()->GetCPUDescriptorHandle(depthSrvIndex_);
    depthSrvHandleGPU_ = SrvManager::GetInstance()->GetGPUDescriptorHandle(depthSrvIndex_);
}

void DirectXCommon::RenderTargetViewInitialize() {
    // バックバッファ・オフスクリーン共通のRTV設定
    D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
    rtvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;

    //=================バックバッファ用のRTV（slot 0, 1）======================
    for (uint32_t i = 0; i < swapChain_->GetBackBufferCount(); ++i) {
        rtvManager_->Create(i, swapChain_->GetBackBuffer(i), rtvDesc);
    }

    //=================RenderTextureResource用のRTV（slot 2）======================
    clearColorValue_.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    clearColorValue_.Color[0] = 0.1f;  // 赤成分 (非常に暗い)
    clearColorValue_.Color[1] = 0.25f; // 緑成分 (非常に暗い)
    clearColorValue_.Color[2] = 0.5f;  // 青成分 (少し強め)
    clearColorValue_.Color[3] = 1.0f;  // アルファ値 (完全な不透明)
    offScreenResource_ = resourceFactory_->CreateRenderTextureResource(WinApp::kClientWidth, WinApp::kClientHeight, clearColorValue_.Format, clearColorValue_);

    rtvManager_->Create(2, offScreenResource_.Get(), rtvDesc);
}

D3D12_CPU_DESCRIPTOR_HANDLE DirectXCommon::CreateAdditionalRTV(ID3D12Resource *resource, int index) {
    D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
    rtvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;

    return rtvManager_->Create(3 + index, resource, rtvDesc);
}

void DirectXCommon::PreRenderTexture() {
    BarrierTransition(offScreenResource_.Get(),
                      D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_RENDER_TARGET);

    // 描画先のRTVとDSVを設定する
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = rtvManager_->GetCPUHandle(2);
    D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = dsvManager_->GetCPUHandle(0);
    ID3D12GraphicsCommandList *commandList = directCommandList_->Get();
    commandList->OMSetRenderTargets(1, &rtvHandle, false, &dsvHandle);
    commandList->ClearRenderTargetView(rtvHandle, clearColorValue_.Color, 0, nullptr);
    // 指定した深度で画面全体をクリアする
    commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

    commandList->RSSetViewports(1, &viewport_);       // Viewportを設定
    commandList->RSSetScissorRects(1, &scissorRect_); // Scissorを設定
}

void DirectXCommon::PreDraw() {
    // 深度リソースをピクセルシェーダーリソースとして読み取る準備
    BarrierTransition(depthStencilResource_.Get(),
                      D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    BarrierTransition(offScreenResource_.Get(),
                      D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_GENERIC_READ);

    // バックバッファを描画ターゲットに遷移
    UINT backBufferIndex = swapChain_->GetCurrentBackBufferIndex();
    BarrierTransition(swapChain_->GetBackBuffer(backBufferIndex), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);

    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = rtvManager_->GetCPUHandle(backBufferIndex);
    D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = dsvManager_->GetCPUHandle(0);
    ID3D12GraphicsCommandList *commandList = directCommandList_->Get();
    commandList->OMSetRenderTargets(1, &rtvHandle, false, &dsvHandle);
    commandList->ClearRenderTargetView(rtvHandle, clearColorValue_.Color, 0, nullptr);

    commandList->RSSetViewports(1, &viewport_);
    commandList->RSSetScissorRects(1, &scissorRect_);
}

void DirectXCommon::TransitionDepthBarrier() {
    BarrierTransition(depthStencilResource_.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE);
}

void DirectXCommon::PreDrawForEffects() {
    // マルチステージ用: バックバッファは既に遷移済みなので深度とオフスクリーンのみ遷移
    BarrierTransition(depthStencilResource_.Get(),
                      D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    BarrierTransition(offScreenResource_.Get(),
                      D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_GENERIC_READ);
    ID3D12GraphicsCommandList *commandList = directCommandList_->Get();
    commandList->RSSetViewports(1, &viewport_);
    commandList->RSSetScissorRects(1, &scissorRect_);
}

void DirectXCommon::PostDraw() {
    UINT backBufferIndex = swapChain_->GetCurrentBackBufferIndex();

    // バックバッファを PRESENT 状態に遷移
    BarrierTransition(swapChain_->GetBackBuffer(backBufferIndex),
                      D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);

    // コマンドリストを確定してGPUに送信
    directCommandList_->Close();
    directQueue_->Execute(directCommandList_->Get());

    // 画面を表示（VSync 待ち）
    swapChain_->Present();

    // ---- ダブルバッファフェンス管理 ----
    // 現フレームスロットに完了シグナルを送る
    fenceValues_[frameIndex_] = directQueue_->Signal();

    // FPS固定（VSync後の余剰時間を調整）
    fpsLimiter_->Wait();

    // 次フレームスロットに切り替える
    frameIndex_ = (frameIndex_ + 1) % DXCommandList::kFrameCount;

    // 次スロットの前回使用分が GPU で完了するまで待つ
    // （同じアロケータを 2 フレーム後に安全に再利用するため）
    directQueue_->WaitForFenceCPU(fenceValues_[frameIndex_]);

    // 次フレームのコマンドアロケータ・リストをリセット
    directCommandList_->Reset(frameIndex_);

    // Compute 側も同スロットをリセット（Direct の完了が Compute 完了を含意するため安全）
    // このフレームでパーティクルが1つも描画されなかった場合はリストが Open のままなので先に Close する
    if (computeCommandList_->IsOpen()) {
        computeCommandList_->Close();
    }
    computeCommandList_->Reset(frameIndex_);
}

void DirectXCommon::BeginComputeFrame() {
    // 同フレーム内で前のエミッターが既に Close+Execute 済みの場合
    // → CPU 側で完了を待ち、アロケータをリセットして再オープンする
    if (!computeCommandList_->IsOpen()) {
        computeQueue_->WaitForFenceCPU(computeQueue_->GetLastSignaledValue());
        computeCommandList_->Reset(frameIndex_);
    }

    // デスクリプタヒープをコンピュートコマンドリストに設定
    ID3D12DescriptorHeap *heaps[] = {SrvManager::GetInstance()->GetDescriptorHeap()};
    computeCommandList_->Get()->SetDescriptorHeaps(_countof(heaps), heaps);
}

void DirectXCommon::ExecuteComputeCommands() {
    // 記録されていない（リストが既に閉じている）場合は何もしない
    if (!computeCommandList_->IsOpen()) {
        return;
    }

    computeCommandList_->Close();
    computeQueue_->Execute(computeCommandList_->Get());

    // 完了シグナルを発行
    computeQueue_->Signal();
}

void DirectXCommon::WaitForComputeOnDirectQueue() {
    // GPU 側で Direct Queue が Compute Queue の完了を待つ（CPU はブロックしない）
    directQueue_->WaitOnGPU(*computeQueue_);
}

void DirectXCommon::FlushComputeQueue() {
    if (computeQueue_) {
        computeQueue_->Flush();
    }
}

void DirectXCommon::TransitionUAVBarrier(ID3D12Resource *pResource) {
    barrier_.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier_.Transition.pResource = pResource;
    barrier_.Transition.StateBefore = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
    barrier_.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    directCommandList_->Get()->ResourceBarrier(1, &barrier_);
}

void DirectXCommon::TransitionSRVBarrier() {
    barrier_.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    barrier_.Transition.StateAfter = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
    directCommandList_->Get()->ResourceBarrier(1, &barrier_);
}

void DirectXCommon::BarrierTransition(ID3D12Resource *pResource, D3D12_RESOURCE_STATES Before, D3D12_RESOURCE_STATES After) {
    // 今回のバリアはTransition
    barrier_.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    // Noneにしておく
    barrier_.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    // バリアを張る対象のリソース
    barrier_.Transition.pResource = pResource;
    // 遷移前(現在)のResourceState
    barrier_.Transition.StateBefore = Before;
    // 遷移後のResourceState
    barrier_.Transition.StateAfter = After;
    // TransitionBarrierを張る
    directCommandList_->Get()->ResourceBarrier(1, &barrier_);
}

void DirectXCommon::ViewPortRectInitialize() {
    // クライアント領域のサイズと一緒にして画面全体に表示
    viewport_.Width = FLOAT(WinApp::kClientWidth);
    viewport_.Height = FLOAT(WinApp::kClientHeight);
    viewport_.TopLeftX = 0;
    viewport_.TopLeftY = 0;
    viewport_.MinDepth = 0.0f;
    viewport_.MaxDepth = 1.0f;
}

void DirectXCommon::ScissorRectInitialize() {
    // 基本的にビューポートと同じ矩形が構成されるようにする
    scissorRect_.left = 0;
    scissorRect_.right = WinApp::kClientWidth;
    scissorRect_.top = 0;
    scissorRect_.bottom = WinApp::kClientHeight;
}

#pragma region 委譲API
IDxcBlob *DirectXCommon::CompileShader(const std::wstring &filePath, const wchar_t *profile) {
    return shaderCompiler_->Compile(filePath, profile);
}

Microsoft::WRL::ComPtr<ID3D12Resource> DirectXCommon::CreateBufferResource(size_t sizeInBytes, bool isUAV) {
    return resourceFactory_->CreateBufferResource(sizeInBytes, isUAV);
}

Microsoft::WRL::ComPtr<ID3D12Resource> DirectXCommon::CreateTextureResource(const DirectX::TexMetadata &metadata) {
    return resourceFactory_->CreateTextureResource(metadata);
}

Microsoft::WRL::ComPtr<ID3D12Resource> DirectXCommon::CreateRenderTextureResource(uint32_t width, uint32_t height, DXGI_FORMAT format, D3D12_CLEAR_VALUE color) {
    return resourceFactory_->CreateRenderTextureResource(width, height, format, color);
}

Microsoft::WRL::ComPtr<ID3D12Resource> DirectXCommon::CreateAdditionalDepthResource(int32_t width, int32_t height) {
    return resourceFactory_->CreateDepthStencilTextureResource(width, height);
}

Microsoft::WRL::ComPtr<ID3D12Resource> DirectXCommon::UploadTextureData(Microsoft::WRL::ComPtr<ID3D12Resource> texture, const DirectX::ScratchImage &mipImages) {
    return resourceFactory_->UploadTextureData(texture, mipImages, directCommandList_->Get());
}

ID3D12CommandSignature *DirectXCommon::GetDispatchIndirectCommandSignature() {
    return resourceFactory_->GetDispatchIndirectCommandSignature();
}
#pragma endregion
} // namespace Hagine
