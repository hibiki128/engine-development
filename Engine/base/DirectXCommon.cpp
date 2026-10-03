#include "DirectXCommon.h"
#include "DXCommandList.h"
#include "DXCommandQueue.h"
#include "DXDevice.h"
#include "DXSwapChain.h"
#include "FrameRateLimiter.h"
#include "graphics/dsv/DsvManager.h"
#include "graphics/rtv/RtvManager.h"
#include "ResourceFactory.h"
#include "ShaderCompiler.h"
#include "cassert"
#include <debug/capture/CaptureManager.h>
#include <graphics/srv/SrvManager.h>

namespace Hagine {

// ---- 生成・破棄（部品の定義が見えるここで行う）----
DirectXCommon::DirectXCommon()
{
    static_assert(kFrameCount == DXCommandList::kFrameCount, "DirectXCommon::kFrameCount を DXCommandList と合わせること");
}
DirectXCommon::~DirectXCommon() = default;

DirectXCommon *DirectXCommon::GetInstance()
{
    static DirectXCommon instance;
    return &instance;
}

// ---- 部品へ中継するゲッター（ヘッダーから移した）----
D3D12_CPU_DESCRIPTOR_HANDLE DirectXCommon::GetRTVCPUDescriptorHandle(uint32_t index)
{
    return rtvManager_->GetCPUHandle(index);
}

D3D12_GPU_DESCRIPTOR_HANDLE DirectXCommon::GetRTVGPUDescriptorHandle(uint32_t index)
{
    return rtvManager_->GetGPUHandle(index);
}

D3D12_CPU_DESCRIPTOR_HANDLE DirectXCommon::GetDSVCPUDescriptorHandle(uint32_t index)
{
    return dsvManager_->GetCPUHandle(index);
}

D3D12_GPU_DESCRIPTOR_HANDLE DirectXCommon::GetDSVGPUDescriptorHandle(uint32_t index)
{
    return dsvManager_->GetGPUHandle(index);
}

Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> DirectXCommon::GetCommandList()
{
    return directCommandList_->GetComPtr();
}

Microsoft::WRL::ComPtr<ID3D12Device> DirectXCommon::GetDevice()
{
    return dxDevice_->GetComPtr();
}

ID3D12Device5 *DirectXCommon::GetDevice5()
{
    return dxDevice_->GetDevice5();
}

ID3D12GraphicsCommandList4 *DirectXCommon::GetCommandList4()
{
    return directCommandList_->Get4();
}

bool DirectXCommon::IsRaytracingSupported() const
{
    return dxDevice_->IsRaytracingSupported();
}

IDxcUtils *DirectXCommon::GetDxcUtils()
{
    return shaderCompiler_->GetDxcUtils();
}

IDxcCompiler3 *DirectXCommon::GetDxcCompiler()
{
    return shaderCompiler_->GetDxcCompiler();
}

size_t DirectXCommon::GetBackBufferCount() const
{
    return swapChain_->GetBackBufferCount();
}

IDXGISwapChain4 *DirectXCommon::GetSwapChain()
{
    return swapChain_->Get();
}

Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> DirectXCommon::GetRTVDescriptorHeap()
{
    return rtvManager_->GetHeap();
}

Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> DirectXCommon::GetComputeCommandList()
{
    return computeCommandList_->GetComPtr();
}

ID3D12CommandQueue *DirectXCommon::GetCommandQueue()
{
    return directQueue_->Get();
}

ID3D12CommandQueue *DirectXCommon::GetComputeCommandQueue()
{
    return computeQueue_->Get();
}

Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> DirectXCommon::CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE heapType, UINT numDescriptors, bool shaderVisible)
{
    return dxDevice_->CreateDescriptorHeap(heapType, numDescriptors, shaderVisible);
}


#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dxcompiler.lib")

void DirectXCommon::Finalize()
{
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

void DirectXCommon::Initialize(WinApp *winApp)
{

    // NULL検出
    assert(winApp);

    // メンバ変数に記録
    this->pWinApp_ = winApp;

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
    swapChain_->Initialize(dxDevice_->GetFactory(), directQueue_->Get(), pWinApp_->GetHwnd(), WinApp::GetVirtualWidth(), WinApp::GetVirtualHeight());

    // 深度バッファの生成
    depthStencilResource_ = resourceFactory_->CreateDepthStencilTextureResource(WinApp::GetVirtualWidth(), WinApp::GetVirtualHeight());

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
    // 最終合成用ビューポートの初期化（起動時はウィンドウ＝仮想解像度）
    UpdatePresentViewport(WinApp::GetVirtualWidth(), WinApp::GetVirtualHeight());

    // DXCコンパイラの生成
    shaderCompiler_ = std::make_unique<ShaderCompiler>();
    shaderCompiler_->Initialize();

    // 非同期コンピュートキュー・コマンドリストの初期化
    computeQueue_ = std::make_unique<DXCommandQueue>();
    computeQueue_->Initialize(dxDevice_.get(), D3D12_COMMAND_LIST_TYPE_COMPUTE);
    computeCommandList_ = std::make_unique<DXCommandList>();
    computeCommandList_->Initialize(dxDevice_.get(), D3D12_COMMAND_LIST_TYPE_COMPUTE);
}

void DirectXCommon::CreateOffscreenSRV()
{
    offScreenSrvIndex_ = SrvManager::GetInstance()->Allocate();
    SrvManager::GetInstance()->CreateSRVforRenderTexture(offScreenSrvIndex_, offScreenResource_.Get());
    offScreenSrvHandleCPU_ = SrvManager::GetInstance()->GetCPUDescriptorHandle(offScreenSrvIndex_);
    offScreenSrvHandleGPU_ = SrvManager::GetInstance()->GetGPUDescriptorHandle(offScreenSrvIndex_);

    // ブルームの合成がシーンへ直接足し込むための UAV（+1規約で確保する）
    offScreenUavIndex_ = SrvManager::GetInstance()->Allocate() + 1;
    SrvManager::GetInstance()->CreateUAVforTexture2D(offScreenUavIndex_, offScreenResource_.Get(),
                                                     kSceneColorFormat);
}

void DirectXCommon::CreateDepthSRV()
{
    depthSrvIndex_ = SrvManager::GetInstance()->Allocate();
    SrvManager::GetInstance()->CreateSRVforDepth(depthSrvIndex_, depthStencilResource_.Get());
    depthSrvHandleCPU_ = SrvManager::GetInstance()->GetCPUDescriptorHandle(depthSrvIndex_);
    depthSrvHandleGPU_ = SrvManager::GetInstance()->GetGPUDescriptorHandle(depthSrvIndex_);
}

void DirectXCommon::RenderTargetViewInitialize()
{
    // バックバッファ・オフスクリーン共通のRTV設定
    D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
    rtvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;

    //=================バックバッファ用のRTV（slot 0, 1）======================
    for (uint32_t i = 0; i < swapChain_->GetBackBufferCount(); ++i)
    {
        rtvManager_->Create(i, swapChain_->GetBackBuffer(i), rtvDesc);
    }

    //=================RenderTextureResource用のRTV（slot 2）======================
    // シーンは HDR（リニアFP16）で描く。1.0 を超える明るさを残したまま
    // ポストエフェクトへ渡し、チェーンの出口でトーンマップして見える範囲へ収める。
    clearColorValue_.Format = kSceneColorFormat;
    clearColorValue_.Color[0] = 0.1f;  // 赤成分 (非常に暗い)
    clearColorValue_.Color[1] = 0.25f; // 緑成分 (非常に暗い)
    clearColorValue_.Color[2] = 0.5f;  // 青成分 (少し強め)
    clearColorValue_.Color[3] = 1.0f;  // アルファ値 (完全な不透明)
    // allowUAV: ブルームの合成がコンピュートから直接足し込むため。
    // SRV と UAV は同時にバインドできないので、シーンへ加算するパスは
    // このテクスチャを UAV として読み書きする（BloomPass を参照）
    offScreenResource_ = resourceFactory_->CreateRenderTextureResource(WinApp::GetVirtualWidth(), WinApp::GetVirtualHeight(), clearColorValue_.Format, clearColorValue_, /*allowUAV=*/true);

    D3D12_RENDER_TARGET_VIEW_DESC sceneRtvDesc = rtvDesc;
    sceneRtvDesc.Format = kSceneColorFormat;
    rtvManager_->Create(2, offScreenResource_.Get(), sceneRtvDesc);
}

D3D12_CPU_DESCRIPTOR_HANDLE DirectXCommon::CreateAdditionalRTV(ID3D12Resource *resource, int index)
{
    D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
    rtvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;

    return rtvManager_->Create(3 + index, resource, rtvDesc);
}

void DirectXCommon::PreRenderTexture()
{
    BarrierTransition(offScreenResource_.Get(),
                      D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_RENDER_TARGET);

    // 描画先のRTVとDSVを設定する
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = rtvManager_->GetCPUHandle(2);
    D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = dsvManager_->GetCPUHandle(0);
    ID3D12GraphicsCommandList *pCommandList = directCommandList_->Get();
    pCommandList->OMSetRenderTargets(1, &rtvHandle, false, &dsvHandle);
    pCommandList->ClearRenderTargetView(rtvHandle, clearColorValue_.Color, 0, nullptr);
    // 指定した深度で画面全体をクリアする
    pCommandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

    pCommandList->RSSetViewports(1, &viewport_);       // Viewportを設定
    pCommandList->RSSetScissorRects(1, &scissorRect_); // Scissorを設定
}

void DirectXCommon::PreDraw()
{
    // 深度リソースをシェーダーから読み取る準備。
    // ピクセルシェーダー（深度アウトラインのPS版など）とコンピュートシェーダー
    // （CS版のポストエフェクト）の両方から読むので、読み取り状態を両方立てておく。
    // NON_PIXEL を落とすと、コンピュートから読んだ時点で状態違反になる。
    BarrierTransition(depthStencilResource_.Get(),
                      D3D12_RESOURCE_STATE_DEPTH_WRITE, kDepthReadState);
    BarrierTransition(offScreenResource_.Get(),
                      D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_GENERIC_READ);

    // バックバッファを描画ターゲットに遷移
    UINT backBufferIndex = swapChain_->GetCurrentBackBufferIndex();
    BarrierTransition(swapChain_->GetBackBuffer(backBufferIndex), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);

    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = rtvManager_->GetCPUHandle(backBufferIndex);
    D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = dsvManager_->GetCPUHandle(0);
    ID3D12GraphicsCommandList *pCommandList = directCommandList_->Get();
    pCommandList->OMSetRenderTargets(1, &rtvHandle, false, &dsvHandle);
    pCommandList->ClearRenderTargetView(rtvHandle, clearColorValue_.Color, 0, nullptr);

    pCommandList->RSSetViewports(1, &viewport_);
    pCommandList->RSSetScissorRects(1, &scissorRect_);
}

void DirectXCommon::CaptureDepthForRead()
{
    // 申告が無ければ複製も確保もしない（ソフトパーティクルを使わないゲームは完全に無料）。
    // 申告は各グループの Update から来るのでフレーム頭に立ち、ここで下ろす
    const bool requested = depthCaptureRequested_;
    depthCaptureRequested_ = false;
    if (!requested || !depthStencilResource_)
    {
        return;
    }

    // 初回だけ複製先を作る。ソフトパーティクルを使わないゲームでは1枚も確保しない
    if (!depthCopyResource_)
    {
        depthCopyResource_ = resourceFactory_->CreateDepthStencilTextureResource(WinApp::GetVirtualWidth(),
                                                                                 WinApp::GetVirtualHeight());
        if (!depthCopyResource_)
        {
            return;
        }
        depthCopyResource_->SetName(L"DepthCopyForSoftParticle");
        depthCopySrvIndex_ = SrvManager::GetInstance()->Allocate() + 1;
        SrvManager::GetInstance()->CreateSRVforDepth(depthCopySrvIndex_, depthCopyResource_.Get());
        // 作った直後は DEPTH_WRITE なので、以降の往復に合わせて読み取り状態へ寄せておく
        BarrierTransition(depthCopyResource_.Get(), D3D12_RESOURCE_STATE_DEPTH_WRITE, kDepthReadState,
                          D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES);
    }

    ID3D12GraphicsCommandList *pCommandList = directCommandList_->Get();

    // 本体は描画中 DEPTH_WRITE。コピー元へ落としてから複製し、すぐ戻す。
    // CopyResource はリソース丸ごとを扱うので、深度面(0)だけでなくステンシル面(1)も一緒に遷移させる
    constexpr UINT kAll = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    BarrierTransition(depthStencilResource_.Get(), D3D12_RESOURCE_STATE_DEPTH_WRITE,
                      D3D12_RESOURCE_STATE_COPY_SOURCE, kAll);
    BarrierTransition(depthCopyResource_.Get(), kDepthReadState, D3D12_RESOURCE_STATE_COPY_DEST, kAll);

    pCommandList->CopyResource(depthCopyResource_.Get(), depthStencilResource_.Get());

    BarrierTransition(depthCopyResource_.Get(), D3D12_RESOURCE_STATE_COPY_DEST, kDepthReadState, kAll);
    BarrierTransition(depthStencilResource_.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE,
                      D3D12_RESOURCE_STATE_DEPTH_WRITE, kAll);
}

void DirectXCommon::TransitionDepthBarrier()
{
    BarrierTransition(depthStencilResource_.Get(), kDepthReadState, D3D12_RESOURCE_STATE_DEPTH_WRITE);
}

void DirectXCommon::PreDrawForEffects()
{
    // マルチステージ用: バックバッファは既に遷移済みなので深度とオフスクリーンのみ遷移
    BarrierTransition(depthStencilResource_.Get(),
                      D3D12_RESOURCE_STATE_DEPTH_WRITE, kDepthReadState);
    BarrierTransition(offScreenResource_.Get(),
                      D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_GENERIC_READ);
    ID3D12GraphicsCommandList *pCommandList = directCommandList_->Get();
    pCommandList->RSSetViewports(1, &viewport_);
    pCommandList->RSSetScissorRects(1, &scissorRect_);
}

void DirectXCommon::PostDraw()
{
    UINT backBufferIndex = swapChain_->GetCurrentBackBufferIndex();

    // バックバッファを PRESENT 状態に遷移
    BarrierTransition(swapChain_->GetBackBuffer(backBufferIndex),
                      D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);

    // コマンドリストを確定してGPUに送信
    directCommandList_->Close();
    directQueue_->Execute(directCommandList_->Get());

    // 画面を表示（VSync 待ち）
    swapChain_->Present();

    // スクリーンショット・連番録画はここで読み出す。
    // Present より前だと、まだGPUが描き終わっていない絵を撮ってしまう
    CaptureManager::GetInstance()->EndFrame();

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

    // CreateStaticBuffer の写し元のうち、GPU が写し終わった物を捨てる
    std::erase_if(pendingUploads_, [](PendingUpload &upload) { return --upload.framesLeft == 0; });

    // 次フレームのコマンドアロケータ・リストをリセット
    directCommandList_->Reset(frameIndex_);

    // Compute 側も同スロットをリセット（Direct の完了が Compute 完了を含意するため安全）
    // このフレームでパーティクルが1つも描画されなかった場合はリストが Open のままなので先に Close する
    if (computeCommandList_->IsOpen())
    {
        computeCommandList_->Close();
    }
    computeCommandList_->Reset(frameIndex_);
}

void DirectXCommon::BeginComputeFrame()
{
    // 同フレーム内で前のエミッターが既に Close+Execute 済みの場合
    // → CPU 側で完了を待ち、アロケータをリセットして再オープンする
    if (!computeCommandList_->IsOpen())
    {
        computeQueue_->WaitForFenceCPU(computeQueue_->GetLastSignaledValue());
        computeCommandList_->Reset(frameIndex_);
    }

    // デスクリプタヒープをコンピュートコマンドリストに設定
    ID3D12DescriptorHeap *heaps[] = {SrvManager::GetInstance()->GetDescriptorHeap()};
    computeCommandList_->Get()->SetDescriptorHeaps(_countof(heaps), heaps);
}

void DirectXCommon::ExecuteComputeCommands()
{
    // 記録されていない（リストが既に閉じている）場合は何もしない
    if (!computeCommandList_->IsOpen())
    {
        return;
    }

    computeCommandList_->Close();
    computeQueue_->Execute(computeCommandList_->Get());

    // 完了シグナルを発行
    computeQueue_->Signal();
}

void DirectXCommon::WaitForComputeOnDirectQueue()
{
    // GPU 側で Direct Queue が Compute Queue の完了を待つ（CPU はブロックしない）
    directQueue_->WaitOnGPU(*computeQueue_);
}

void DirectXCommon::FlushComputeQueue()
{
    if (computeQueue_)
    {
        computeQueue_->Flush();
    }
}

void DirectXCommon::WaitForGPU()
{
    // 送信済みの全コマンドが GPU で完了するまで CPU をブロックする
    directQueue_->Flush();
    FlushComputeQueue();
}

void DirectXCommon::TransitionUAVBarrier(ID3D12Resource *pResource)
{
    barrier_.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier_.Transition.pResource = pResource;
    barrier_.Transition.StateBefore = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
    barrier_.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    directCommandList_->Get()->ResourceBarrier(1, &barrier_);
}

void DirectXCommon::TransitionSRVBarrier()
{
    barrier_.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    barrier_.Transition.StateAfter = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
    directCommandList_->Get()->ResourceBarrier(1, &barrier_);
}

void DirectXCommon::BarrierTransition(ID3D12Resource *pResource, D3D12_RESOURCE_STATES Before, D3D12_RESOURCE_STATES After,
                                      UINT subresource)
{
    // 今回のバリアはTransition
    barrier_.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    // Noneにしておく
    barrier_.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    // バリアを張る対象のリソース
    barrier_.Transition.pResource = pResource;
    // 対象のサブリソース
    barrier_.Transition.Subresource = subresource;
    // 遷移前(現在)のResourceState
    barrier_.Transition.StateBefore = Before;
    // 遷移後のResourceState
    barrier_.Transition.StateAfter = After;
    // TransitionBarrierを張る
    directCommandList_->Get()->ResourceBarrier(1, &barrier_);
}

void DirectXCommon::ViewPortRectInitialize()
{
    // クライアント領域のサイズと一緒にして画面全体に表示
    viewport_.Width = FLOAT(WinApp::GetVirtualWidth());
    viewport_.Height = FLOAT(WinApp::GetVirtualHeight());
    viewport_.TopLeftX = 0;
    viewport_.TopLeftY = 0;
    viewport_.MinDepth = 0.0f;
    viewport_.MaxDepth = 1.0f;
}

void DirectXCommon::ScissorRectInitialize()
{
    // 基本的にビューポートと同じ矩形が構成されるようにする
    scissorRect_.left = 0;
    scissorRect_.right = WinApp::GetVirtualWidth();
    scissorRect_.top = 0;
    scissorRect_.bottom = WinApp::GetVirtualHeight();
}

void DirectXCommon::UpdatePresentViewport(uint32_t clientWidth, uint32_t clientHeight)
{
    // 仮想解像度のアスペクト比を保った表示矩形を計算（レターボックス）
    float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;
    WinApp::ComputeLetterboxRect(static_cast<int32_t>(clientWidth), static_cast<int32_t>(clientHeight), x, y, w, h);

    presentViewport_.TopLeftX = x;
    presentViewport_.TopLeftY = y;
    presentViewport_.Width = w;
    presentViewport_.Height = h;
    presentViewport_.MinDepth = 0.0f;
    presentViewport_.MaxDepth = 1.0f;

    presentScissorRect_.left = static_cast<LONG>(x);
    presentScissorRect_.top = static_cast<LONG>(y);
    presentScissorRect_.right = static_cast<LONG>(x + w);
    presentScissorRect_.bottom = static_cast<LONG>(y + h);
}

void DirectXCommon::ResizeSwapChain(uint32_t width, uint32_t height)
{
    if (width == 0 || height == 0)
    {
        return;
    }
    // サイズが変わっていなければ何もしない
    if (width == swapChain_->GetWidth() && height == swapChain_->GetHeight())
    {
        UpdatePresentViewport(width, height);
        return;
    }

    // GPUの全作業完了を待つ（バックバッファ解放の必須条件）
    directQueue_->Flush();
    FlushComputeQueue();

    // スワップチェーンをリサイズしてバックバッファを取得し直す
    swapChain_->Resize(width, height);

    // バックバッファ用RTV（slot 0, 1）を再作成する（同スロットのデスクリプタを上書き）
    D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
    rtvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    for (uint32_t i = 0; i < swapChain_->GetBackBufferCount(); ++i)
    {
        rtvManager_->Create(i, swapChain_->GetBackBuffer(i), rtvDesc);
    }

    // 最終合成用ビューポートを新しいウィンドウサイズに合わせて再計算
    UpdatePresentViewport(width, height);
}

#pragma region 委譲API
IDxcBlob *DirectXCommon::CompileShader(const std::wstring &filePath, const wchar_t *profile)
{
    return shaderCompiler_->Compile(filePath, profile);
}

IDxcBlob *DirectXCommon::CompileShaderWithReflection(const std::wstring &filePath, const wchar_t *profile,
                                                     ID3D12ShaderReflection **ppReflection)
{
    return shaderCompiler_->CompileWithReflection(filePath, profile, ppReflection);
}

bool DirectXCommon::TryCompileShader(const std::wstring &filePath, const wchar_t *profile, std::string *outError)
{
    return shaderCompiler_->TryCompile(filePath, profile, outError);
}

Microsoft::WRL::ComPtr<ID3D12Resource> DirectXCommon::CreateStaticBuffer(const void *data, size_t sizeInBytes, D3D12_RESOURCE_STATES stateAfterCopy)
{
    // 写し元（アップロードヒープ）
    Microsoft::WRL::ComPtr<ID3D12Resource> upload = CreateBufferResource(sizeInBytes);
    void *mapped = nullptr;
    upload->Map(0, nullptr, &mapped);
    std::memcpy(mapped, data, sizeInBytes);
    upload->Unmap(0, nullptr);

    // 写し先（デフォルトヒープ）。バッファは COMMON で作られ、写すときに COPY_DEST へ暗黙に上がる
    D3D12_HEAP_PROPERTIES heapProperties{};
    heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC resourceDesc{};
    resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resourceDesc.Width = sizeInBytes;
    resourceDesc.Height = 1;
    resourceDesc.DepthOrArraySize = 1;
    resourceDesc.MipLevels = 1;
    resourceDesc.SampleDesc.Count = 1;
    resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    Microsoft::WRL::ComPtr<ID3D12Resource> resource;
    HRESULT hr = GetDevice()->CreateCommittedResource(&heapProperties, D3D12_HEAP_FLAG_NONE, &resourceDesc,
                                                      D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&resource));
    assert(SUCCEEDED(hr));
    resource->SetName(L"StaticBuffer");

    ID3D12GraphicsCommandList *pCommandList = directCommandList_->Get();
    pCommandList->CopyBufferRegion(resource.Get(), 0, upload.Get(), 0, sizeInBytes);
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = resource.Get();
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter = stateAfterCopy;
    pCommandList->ResourceBarrier(1, &barrier);

    // 写し元は GPU が写し終わってから捨てる（フレームが一周するまで）
    pendingUploads_.push_back({upload, kFrameCount + 1});
    return resource;
}

void DirectXCommon::ClearShaderCache()
{
    shaderCompiler_->ClearCache();
}

void DirectXCommon::GetShaderCacheStats(size_t &outHits, size_t &outMisses, size_t &outEntries) const
{
    shaderCompiler_->GetCacheStats(outHits, outMisses, outEntries);
}

Microsoft::WRL::ComPtr<ID3D12Resource> DirectXCommon::CreateBufferResource(size_t sizeInBytes, bool isUAV)
{
    return resourceFactory_->CreateBufferResource(sizeInBytes, isUAV);
}

Microsoft::WRL::ComPtr<ID3D12Resource> DirectXCommon::CreateTextureResource(const DirectX::TexMetadata &metadata)
{
    return resourceFactory_->CreateTextureResource(metadata);
}

Microsoft::WRL::ComPtr<ID3D12Resource> DirectXCommon::CreateRenderTextureResource(uint32_t width, uint32_t height, DXGI_FORMAT format, D3D12_CLEAR_VALUE color, bool allowUAV)
{
    return resourceFactory_->CreateRenderTextureResource(width, height, format, color, allowUAV);
}

Microsoft::WRL::ComPtr<ID3D12Resource> DirectXCommon::CreateAdditionalDepthResource(int32_t width, int32_t height)
{
    return resourceFactory_->CreateDepthStencilTextureResource(width, height);
}

Microsoft::WRL::ComPtr<ID3D12Resource> DirectXCommon::UploadTextureData(Microsoft::WRL::ComPtr<ID3D12Resource> texture, const DirectX::ScratchImage &mipImages)
{
    return resourceFactory_->UploadTextureData(texture, mipImages, directCommandList_->Get());
}

ID3D12CommandSignature *DirectXCommon::GetDispatchIndirectCommandSignature()
{
    return resourceFactory_->GetDispatchIndirectCommandSignature();
}
#pragma endregion
} // namespace Hagine
